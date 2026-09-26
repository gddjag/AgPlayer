// Test files deliberately keep assert() active even in Release builds.
#undef NDEBUG

#include "scratch_command_mailbox.hpp"
#include "scratch_pcm_window.hpp"
#include "scratch_renderer.hpp"

#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <random>
#include <thread>
#include <vector>

namespace {

std::atomic<bool> record_allocations{false};
std::atomic<std::size_t> allocation_count{0U};
std::atomic<std::size_t> deletion_count{0U};

void* allocate(const std::size_t size)
{
    if (record_allocations.load(std::memory_order_relaxed)) {
        allocation_count.fetch_add(1U, std::memory_order_relaxed);
    }
    if (void* const pointer = std::malloc(size == 0U ? 1U : size)) {
        return pointer;
    }
    throw std::bad_alloc();
}

void release(void* const pointer) noexcept
{
    if (record_allocations.load(std::memory_order_relaxed)) {
        deletion_count.fetch_add(1U, std::memory_order_relaxed);
    }
    std::free(pointer);
}

} // namespace

void* operator new(const std::size_t size) { return allocate(size); }
void* operator new[](const std::size_t size) { return allocate(size); }
void operator delete(void* const pointer) noexcept { release(pointer); }
void operator delete[](void* const pointer) noexcept { release(pointer); }
void operator delete(void* const pointer, std::size_t) noexcept
{
    release(pointer);
}
void operator delete[](void* const pointer, std::size_t) noexcept
{
    release(pointer);
}

int main(const int argc, char* const argv[])
{
    constexpr std::uint32_t sample_rate = 48'000U;
    constexpr std::uint32_t channels = 2U;
    agplayer::ScratchPcmWindow window(sample_rate, channels);
    std::vector<float> pcm(window.capacity_frames() * channels);
    for (std::size_t frame = 0U; frame < window.capacity_frames(); ++frame) {
        const float sample = static_cast<float>(
            std::sin(static_cast<double>(frame) * 0.01));
        pcm[frame * channels] = sample;
        pcm[frame * channels + 1U] = -sample;
    }
    assert(window.assign(0, pcm.data(), window.capacity_frames(), 1U, 1U));

    int seconds = 60;
    if (argc > 1) {
        char* end = nullptr;
        const long requested = std::strtol(argv[1], &end, 10);
        if (end != argv[1] && *end == '\0') {
            seconds = static_cast<int>(std::clamp(requested, 1L, 60L));
        }
    }

    agplayer::ScratchCommandMailbox mailbox;
    mailbox.publish({1U, 1U, true, 0.0F});
    std::atomic<bool> stop{false};
    std::thread producer([&] {
        std::mt19937 random(0xA621U);
        std::uniform_real_distribution<float> rate(-3.5F, 3.5F);
        while (!stop.load(std::memory_order_acquire)) {
            mailbox.publish({1U, 1U, true, rate(random)});
            std::this_thread::yield();
        }
    });

    agplayer::ScratchRenderer renderer(sample_rate, channels);
    assert(renderer.begin(static_cast<double>(window.capacity_frames() / 2U),
                          static_cast<std::int64_t>(window.capacity_frames()),
                          1U, 1U));
    std::array<float, 256U * channels> output{};
    agplayer::ScratchCommand command{};
    auto deadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(seconds);
    std::uint64_t blocks = 0U;
    record_allocations.store(true, std::memory_order_release);
    while (std::chrono::steady_clock::now() < deadline) {
        if (mailbox.try_read(command)) {
            const auto status = renderer.render(
                &window, command, output.data(), output.size() / channels);
            assert(status.active);
            assert(status.source_frame >= 0.0);
            assert(status.source_frame
                   <= static_cast<double>(window.capacity_frames() - 1U));
            assert(status.smoothed_rate >= -3.0F
                   && status.smoothed_rate <= 3.0F);
            for (const float sample : output) assert(std::isfinite(sample));
            ++blocks;
        }
    }
    record_allocations.store(false, std::memory_order_release);
    stop.store(true, std::memory_order_release);
    producer.join();
    assert(blocks > 1'000U);
    assert(allocation_count.load(std::memory_order_relaxed) == 0U);
    assert(deletion_count.load(std::memory_order_relaxed) == 0U);
    assert(window.payload_bytes()
           <= agplayer::ScratchPcmWindow::default_bank_payload_budget());
}
