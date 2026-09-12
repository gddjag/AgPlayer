// Test files deliberately keep assert() active even in Release builds: many
// test cases embed function calls with side effects inside assert(), and
// silencing them under NDEBUG would skip those calls and crash on cleanup.
#undef NDEBUG

#include "pcm_ring_buffer.hpp"

#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <thread>

int main()
{
    bool rejected_invalid_shape = false;
    try {
        const agplayer::PcmRingBuffer invalid(0, 2);
    } catch (const std::invalid_argument&) {
        rejected_invalid_shape = true;
    }
    assert(rejected_invalid_shape);

    agplayer::PcmRingBuffer buffer(4, 2);
    const std::array<float, 6> input{1, 2, 3, 4, 5, 6};
    assert(buffer.write(input.data(), 3) == 3);
    assert(buffer.available_frames() == 3);

    std::array<float, 4> first_output{};
    assert(buffer.read(first_output.data(), 2) == 2);
    assert((first_output == std::array<float, 4>{1, 2, 3, 4}));

    const std::array<float, 6> wrapped_input{7, 8, 9, 10, 11, 12};
    assert(buffer.write(wrapped_input.data(), 3) == 3);
    assert(buffer.available_frames() == 4);

    std::array<float, 8> wrapped_output{};
    assert(buffer.read(wrapped_output.data(), 4) == 4);
    assert((wrapped_output == std::array<float, 8>{5, 6, 7, 8, 9, 10, 11, 12}));
    assert(buffer.read(wrapped_output.data(), 1) == 0);
    assert(buffer.write(nullptr, 1) == 0);

    const std::array<float, 6> mapped_input{13, 14, 15, 16, 17, 18};
    const std::array<std::int64_t, 3> source_frames{101, 103, 108};
    assert(buffer.write(mapped_input.data(), source_frames.data(), 3, 7U) == 3);
    std::int64_t last_source_frame = -1;
    std::uint64_t last_generation = 0U;
    assert(buffer.read(first_output.data(), 2, &last_source_frame,
                       &last_generation) == 2);
    assert(last_source_frame == 103);
    assert(last_generation == 7U);
    assert(buffer.read(first_output.data(), 1, &last_source_frame,
                       &last_generation) == 1);
    assert(last_source_frame == 108);
    assert(last_generation == 7U);

    assert(buffer.write(input.data(), 3) == 3);
    buffer.clear();
    assert(buffer.available_frames() == 0);

    static_assert(std::atomic<std::size_t>::is_always_lock_free);
    agplayer::PcmRingBuffer concurrent_buffer(64, 1);
    constexpr std::size_t concurrent_frames = 10'000;
    std::atomic<bool> sequence_failed{false};

    std::thread producer([&concurrent_buffer] {
        for (std::size_t frame = 0; frame < concurrent_frames; ++frame) {
            const float sample = static_cast<float>(frame);
            while (concurrent_buffer.write(&sample, 1) == 0U) {
                std::this_thread::yield();
            }
        }
    });
    std::thread consumer([&concurrent_buffer, &sequence_failed] {
        for (std::size_t frame = 0; frame < concurrent_frames; ++frame) {
            float sample = 0.0F;
            while (concurrent_buffer.read(&sample, 1) == 0U) {
                std::this_thread::yield();
            }
            if (sample != static_cast<float>(frame)) {
                sequence_failed.store(true, std::memory_order_relaxed);
            }
        }
    });

    producer.join();
    consumer.join();
    assert(!sequence_failed.load(std::memory_order_relaxed));
}
