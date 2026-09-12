#undef NDEBUG
#include "visual_pcm_tap.hpp"
#include <cassert>
#include <array>
#include <atomic>
#include <thread>

int main()
{
    // A delayed GUI poll must consume the oldest contiguous data first,
    // retaining the remainder for the next bounded read rather than dropping it.
    {
        agplayer::VisualPcmTap delayed;
        delayed.set_enabled(true);
        std::array<float, 512> block{};
        for (int index = 0; index < 6; ++index) {
            block.fill(float(index));
            delayed.write(block.data(), block.size(), 96000);
        }
        ag_visual_pcm_snapshot part{};
        for (int index = 0; index < 3; ++index) {
            delayed.read(part);
            assert(part.first_sample_index == std::uint64_t(index * 1024));
            assert(part.sample_count == 1024 && part.sample_rate == 96000);
            assert(part.samples[0] == float(index * 2));
            assert(part.samples[1023] == float(index * 2 + 1));
        }
        delayed.read(part);
        assert(part.sample_count == 0);
    }
    agplayer::VisualPcmTap tap;
    std::array<float, 512> mono{};
    mono.fill(0.25F);
    ag_visual_pcm_snapshot out{};
    tap.write(mono.data(), mono.size(), 48000);
    tap.read(out);
    assert(out.sample_count == 0);
    tap.set_enabled(true);
    tap.write(mono.data(), mono.size(), 48000);
    tap.read(out);
    assert(out.sample_count == 512 && out.sample_rate == 48000);
    assert(out.samples[0] == 0.25F && out.samples[511] == 0.25F);
    const auto generation = out.generation;
    const auto next = out.first_sample_index + out.sample_count;
    tap.write(mono.data(), mono.size(), 48000);
    tap.read(out);
    assert(out.generation == generation && out.first_sample_index == next);
    tap.read(out);
    assert(out.sample_count == 0);
    tap.write(mono.data(), mono.size(), 48000);
    tap.invalidate();
    tap.read(out);
    assert(out.sample_count == 0 && out.generation != generation);
    tap.write(mono.data(), mono.size(), 44100);
    tap.set_enabled(false);
    tap.set_enabled(true);
    tap.read(out);
    assert(out.sample_count == 0);
    for (int i = 0; i < 3; ++i) tap.write(mono.data(), mono.size(), 44100);
    tap.read(out);
    assert(out.sample_count == 1024 && out.sample_rate == 44100);
    const auto beforeOverflow = out.generation;
    for (int i = 0; i < 100; ++i) tap.write(mono.data(), mono.size(), 44100);
    tap.read(out);
    assert(out.generation != beforeOverflow && out.sample_count == 0);
    tap.write(mono.data(), mono.size(), 44100);
    tap.read(out);
    assert(out.sample_count == 512);
    const auto beforeRateChange = out.generation;
    tap.write(mono.data(), mono.size(), 48000);
    tap.read(out);
    assert(out.sample_count == 512 && out.sample_rate == 48000);
    assert(out.generation != beforeRateChange);

    // Concurrent payload reuse and epoch invalidation must not tear samples.
    std::atomic<bool> done{false};
    std::thread producer([&] {
        std::array<float, 512> block{};
        for (int i = 1; i <= 10000; ++i) {
            block.fill(static_cast<float>(i));
            tap.write(block.data(), block.size(), 48000);
        }
        done.store(true, std::memory_order_release);
    });
    int reads = 0;
    while (!done.load(std::memory_order_acquire)) {
        tap.read(out);
        for (size_t i = 0; i < out.sample_count; i += 512)
            for (size_t j = 1; j < 512; ++j)
                assert(out.samples[i] == out.samples[i + j]);
        if (++reads % 7 == 0) {
            tap.set_enabled(false);
            tap.set_enabled(true);
        }
    }
    producer.join();
}
