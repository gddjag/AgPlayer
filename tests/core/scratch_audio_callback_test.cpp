// Test files deliberately keep assert() active in Release builds.
#undef NDEBUG

#include "../../core/src/audio_engine.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <new>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <malloc.h>
#endif

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

void* allocate_aligned(const std::size_t size, const std::size_t alignment)
{
    if (record_allocations.load(std::memory_order_relaxed)) {
        allocation_count.fetch_add(1U, std::memory_order_relaxed);
    }
#ifdef _WIN32
    if (void* const pointer = _aligned_malloc(size == 0U ? 1U : size,
                                              alignment)) {
        return pointer;
    }
#else
    const std::size_t bytes = ((size == 0U ? 1U : size) + alignment - 1U)
        / alignment * alignment;
    if (void* const pointer = std::aligned_alloc(alignment, bytes)) {
        return pointer;
    }
#endif
    throw std::bad_alloc();
}

void release_aligned(void* const pointer) noexcept
{
    if (record_allocations.load(std::memory_order_relaxed)) {
        deletion_count.fetch_add(1U, std::memory_order_relaxed);
    }
#ifdef _WIN32
    _aligned_free(pointer);
#else
    std::free(pointer);
#endif
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
void* operator new(const std::size_t size, const std::nothrow_t&) noexcept
{
    try {
        return allocate(size);
    } catch (...) {
        return nullptr;
    }
}
void* operator new[](const std::size_t size, const std::nothrow_t&) noexcept
{
    try {
        return allocate(size);
    } catch (...) {
        return nullptr;
    }
}
void* operator new(const std::size_t size, const std::align_val_t alignment)
{
    return allocate_aligned(size, static_cast<std::size_t>(alignment));
}
void* operator new[](const std::size_t size, const std::align_val_t alignment)
{
    return allocate_aligned(size, static_cast<std::size_t>(alignment));
}
void operator delete(void* const pointer,
                     const std::align_val_t) noexcept
{
    release_aligned(pointer);
}
void operator delete[](void* const pointer,
                       const std::align_val_t) noexcept
{
    release_aligned(pointer);
}
void operator delete(void* const pointer, std::size_t,
                     const std::align_val_t) noexcept
{
    release_aligned(pointer);
}
void operator delete[](void* const pointer, std::size_t,
                       const std::align_val_t) noexcept
{
    release_aligned(pointer);
}

namespace agplayer {

class AudioEngineTestAccess final {
public:
    static std::int64_t consumedSourceFrame(const AudioEngine& engine) noexcept
    {
        return engine.consumed_source_frame_for_testing();
    }

    static std::uint64_t physicalScratchSeekCount(
        const AudioEngine& engine) noexcept
    {
        return engine.scratch_physical_seek_count_for_testing();
    }
};

} // namespace agplayer

int main(const int argc, char** argv)
{
    assert(argc == 2);
    static_assert(noexcept(std::declval<agplayer::AudioEngine&>().render(
        nullptr, 0U)));

    agplayer::AudioEngine engine(agplayer::AudioBackend::Manual, 4'096U);
    assert(engine.load(std::filesystem::u8path(argv[1]).string()) == AG_OK);
    assert(engine.seek(10'000) == AG_OK);
    assert(engine.play() == AG_OK);
    const auto buffered_deadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(3);
    while (engine.buffered_frames() < 1'024U
           && std::chrono::steady_clock::now() < buffered_deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(engine.buffered_frames() >= 1'024U);
    assert(engine.begin_scratch() == AG_OK);

    std::vector<float> output(256U * 2U);
    const auto ready_deadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(5);
    while (!engine.scratch_status().ready
           && std::chrono::steady_clock::now() < ready_deadline) {
        engine.render(output.data(), 256U);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(engine.scratch_status().ready);
    assert(engine.update_scratch(1.0F) == AG_OK);
    engine.render(output.data(), 256U);

    const std::size_t buffered_before = engine.buffered_frames();
    const std::int64_t source_before =
        agplayer::AudioEngineTestAccess::consumedSourceFrame(engine);
    const std::uint64_t seeks_before =
        agplayer::AudioEngineTestAccess::physicalScratchSeekCount(engine);
    allocation_count.store(0U, std::memory_order_relaxed);
    deletion_count.store(0U, std::memory_order_relaxed);
    record_allocations.store(true, std::memory_order_release);
    for (int callback = 0; callback < 100; ++callback) {
        engine.render(output.data(), 256U);
    }
    record_allocations.store(false, std::memory_order_release);

    assert(allocation_count.load(std::memory_order_relaxed) == 0U);
    assert(deletion_count.load(std::memory_order_relaxed) == 0U);
    assert(engine.buffered_frames() == buffered_before);
    assert(agplayer::AudioEngineTestAccess::consumedSourceFrame(engine)
           == source_before);
    assert(agplayer::AudioEngineTestAccess::physicalScratchSeekCount(engine)
           == seeks_before);
    assert(engine.end_scratch() == AG_OK);
    return 0;
}
