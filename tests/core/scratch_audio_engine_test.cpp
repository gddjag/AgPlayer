// Test files deliberately keep assert() active in Release builds.
#undef NDEBUG

#include "../../core/src/audio_engine.hpp"
#include "../../core/src/audio_stream_source.hpp"
#include "../../core/src/playback_time_pitch_stage.hpp"

#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <limits>
#include <memory>
#include <iostream>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <crtdbg.h>
#endif

namespace agplayer {

class AudioEngineTestAccess final {
public:
    using TimelineHook = void (*)(void*, bool) noexcept;
    using ScratchCommitHook = void (*)(void*, int) noexcept;

    static std::int64_t scratchSourceFrame(const AudioEngine& engine) noexcept
    {
        return engine.scratch_source_frame_for_testing();
    }

    static std::int64_t consumedSourceFrame(const AudioEngine& engine) noexcept
    {
        return engine.consumed_source_frame_for_testing();
    }

    static std::uint64_t physicalScratchSeekCount(
        const AudioEngine& engine) noexcept
    {
        return engine.scratch_physical_seek_count_for_testing();
    }

    static std::int64_t pendingBoundary(const AudioEngine& engine) noexcept
    {
        return engine.pending_boundary_for_testing();
    }

    static void markDeviceLost(AudioEngine& engine) noexcept
    {
        engine.mark_device_lost_for_testing();
    }

    static void setTimelineHook(AudioEngine& engine, TimelineHook hook,
                                void* context) noexcept
    {
        engine.set_timeline_test_hook(hook, context);
    }

    static void notifyDeviceLostFromBackend(AudioEngine& engine) noexcept
    {
        engine.notify_device_lost_from_backend_for_testing();
    }

    static void setScratchCommitHook(AudioEngine& engine,
                                     ScratchCommitHook hook,
                                     void* context) noexcept
    {
        engine.set_scratch_commit_test_hook(hook, context);
    }

    static void requestDecodeExit(AudioEngine& engine) noexcept
    {
        engine.request_decode_exit_for_testing();
    }

    static ag_result enterError(AudioEngine& engine,
                                const ag_result result) noexcept
    {
        return engine.enter_error_for_testing(result);
    }
};

} // namespace agplayer

namespace {

static_assert(std::atomic<std::uint64_t>::is_always_lock_free);

void countTimelineWrite(void* const context, const bool) noexcept
{
    static_cast<std::atomic<int>*>(context)->fetch_add(
        1, std::memory_order_relaxed);
}

struct ScratchCommitBarrier final {
    int blocking_phase{};
    std::atomic<unsigned> seen_mask{0U};
    std::atomic<bool> release{false};
};

void scratchCommitBarrier(void* const context, const int phase) noexcept
{
    auto& barrier = *static_cast<ScratchCommitBarrier*>(context);
    barrier.seen_mask.fetch_or(1U << static_cast<unsigned>(phase),
                               std::memory_order_release);
    if (phase == barrier.blocking_phase) {
        while (!barrier.release.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
    }
}

bool waitForCommitPhase(const ScratchCommitBarrier& barrier,
                        const int phase,
                        const std::chrono::milliseconds timeout)
{
    const unsigned bit = 1U << static_cast<unsigned>(phase);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while ((barrier.seen_mask.load(std::memory_order_acquire) & bit) == 0U
           && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    return (barrier.seen_mask.load(std::memory_order_acquire) & bit) != 0U;
}

bool waitForTrue(const std::atomic<bool>& value,
                 const std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!value.load(std::memory_order_acquire)
           && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    return value.load(std::memory_order_acquire);
}

void waitForBufferedFrames(agplayer::AudioEngine& engine,
                           const std::size_t minimum)
{
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline) {
        if (engine.buffered_frames() >= minimum) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(false && "decode thread did not prepare PCM");
}

void waitForScratchReady(agplayer::AudioEngine& engine,
                         const std::size_t channels = 2U)
{
    assert(engine.scratch_status().active);
    std::vector<float> output(512U * channels);
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(5);
    while (!engine.scratch_status().ready
           && std::chrono::steady_clock::now() < deadline) {
        engine.render(output.data(), 512U);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(engine.scratch_status().ready);
    assert(!engine.scratch_status().buffering);
}

void testBeginAndCommandValidation(const std::filesystem::path& media)
{
    agplayer::AudioEngine engine(agplayer::AudioBackend::Manual, 4'096U);
    assert(engine.begin_scratch() == AG_INVALID_ARGUMENT);
    assert(engine.load(media.string()) == AG_OK);
    assert(engine.begin_scratch() == AG_INVALID_ARGUMENT);

    assert(engine.play() == AG_OK);
    waitForBufferedFrames(engine, 512U);
    assert(engine.begin_scratch() == AG_OK);

    const agplayer::ScratchStatus begun = engine.scratch_status();
    assert(begun.active);
    assert(!begun.ready);
    assert(begun.buffering);
    assert(engine.update_scratch(
               std::numeric_limits<float>::quiet_NaN())
           == AG_INVALID_ARGUMENT);
    assert(engine.update_scratch(7.0F) == AG_OK);
    assert(engine.end_scratch() == AG_OK);
    assert(engine.end_scratch() == AG_OK);
    assert(!engine.scratch_status().active);
}

float energy(const std::vector<float>& samples)
{
    float sum = 0.0F;
    for (const float sample : samples) sum += std::abs(sample);
    return sum;
}

float channelPeak(const std::vector<float>& samples,
                  const std::size_t channel)
{
    float peak = 0.0F;
    for (std::size_t sample = channel; sample < samples.size(); sample += 2U) {
        peak = (std::max)(peak, std::abs(samples[sample]));
    }
    return (std::min)(peak, 1.0F);
}

float channelRms(const std::vector<float>& samples,
                 const std::size_t channel)
{
    double squares = 0.0;
    std::size_t count = 0U;
    for (std::size_t sample = channel; sample < samples.size(); sample += 2U) {
        squares += static_cast<double>(samples[sample]) * samples[sample];
        ++count;
    }
    return count == 0U ? 0.0F : (std::min)(static_cast<float>(
        std::sqrt(squares / static_cast<double>(count))), 1.0F);
}

class SilentStream final : public agplayer::IAudioStreamSource {
public:
    SilentStream()
    {
        metadata_.sample_rate = 44'100;
        metadata_.channels = 2;
        metadata_.duration_ms = 2'000;
    }

    const agplayer::MediaMetadata& metadata() const noexcept override
    {
        return metadata_;
    }

    ag_result read(agplayer::DecodedAudioBlock& block) noexcept override
    {
        block = {};
        block.frames = 1'024U;
        block.samples.assign(block.frames * 2U, 0.0F);
        position_ += static_cast<std::int64_t>(block.frames);
        block.timestamp_frame = position_ - static_cast<std::int64_t>(block.frames);
        block.timestamp_ms = block.timestamp_frame * 1'000 / 44'100;
        block.end_of_stream = position_ >= 88'200;
        return AG_OK;
    }

    ag_result seek(const std::int64_t position_ms) noexcept override
    {
        position_ = position_ms * 44'100 / 1'000;
        return AG_OK;
    }

private:
    agplayer::MediaMetadata metadata_{};
    std::int64_t position_{};
};

void testPausedRawScratchDirectionsAndFinalCommit(
    const std::filesystem::path& media)
{
    agplayer::AudioEngine engine(agplayer::AudioBackend::Manual, 4'096U);
    assert(engine.load(media.string()) == AG_OK);
    assert(engine.set_time_pitch({1.25, true}) == AG_OK);
    assert(engine.seek(1'000) == AG_OK);
    assert(engine.play() == AG_OK);
    waitForBufferedFrames(engine, 2'048U);
    assert(engine.pause() == AG_OK);
    assert(engine.begin_scratch() == AG_OK);
    assert(engine.snapshot().state == agplayer::EngineState::Paused);
    assert(!engine.scratch_status().ready);
    assert(engine.scratch_status().buffering);
    waitForScratchReady(engine);

    const std::size_t buffered = engine.buffered_frames();
    const std::int64_t normal_source =
        agplayer::AudioEngineTestAccess::consumedSourceFrame(engine);
    const std::int64_t start =
        agplayer::AudioEngineTestAccess::scratchSourceFrame(engine);
    std::vector<float> output(4'096U * 2U);

    assert(engine.update_scratch(1.0F) == AG_OK);
    engine.render(output.data(), 4'096U);
    const std::int64_t forward =
        agplayer::AudioEngineTestAccess::scratchSourceFrame(engine);
    assert(forward > start);
    assert(energy(output) > 1.0F);
    const agplayer::OutputLevels scratch_levels = engine.output_levels();
    assert(std::abs(scratch_levels.left_peak - channelPeak(output, 0U))
           < 1.0e-5F);
    assert(std::abs(scratch_levels.right_peak - channelPeak(output, 1U))
           < 1.0e-5F);
    assert(std::abs(scratch_levels.left_rms - channelRms(output, 0U))
           < 1.0e-5F);
    assert(std::abs(scratch_levels.right_rms - channelRms(output, 1U))
           < 1.0e-5F);
    assert(engine.buffered_frames() == buffered);
    assert(agplayer::AudioEngineTestAccess::consumedSourceFrame(engine)
           == normal_source);

    assert(engine.update_scratch(0.0F) == AG_OK);
    engine.render(output.data(), 4'096U);
    const std::int64_t stopped =
        agplayer::AudioEngineTestAccess::scratchSourceFrame(engine);
    std::fill(output.begin(), output.end(), 1.0F);
    engine.render(output.data(), 4'096U);
    assert(agplayer::AudioEngineTestAccess::scratchSourceFrame(engine)
           == stopped);
    assert(energy(output) < 0.001F);
    assert(engine.output_levels().left_peak < 0.001F);
    assert(engine.output_levels().right_peak < 0.001F);

    assert(engine.update_scratch(-1.0F) == AG_OK);
    engine.render(output.data(), 4'096U);
    const std::int64_t reverse =
        agplayer::AudioEngineTestAccess::scratchSourceFrame(engine);
    assert(reverse < stopped);
    assert(energy(output) > 1.0F);

    const std::uint64_t seeks_before =
        agplayer::AudioEngineTestAccess::physicalScratchSeekCount(engine);
    assert(engine.end_scratch() == AG_OK);
    assert(agplayer::AudioEngineTestAccess::physicalScratchSeekCount(engine)
           == seeks_before + 1U);
    assert(engine.snapshot().state == agplayer::EngineState::Paused);
    assert(!engine.scratch_status().active);
    const agplayer::PlaybackTimePitchConfig tempo = engine.time_pitch_config();
    assert(std::abs(tempo.speed_ratio - 1.25) < 0.000001);
    assert(tempo.keep_pitch);
    assert(std::llabs(engine.snapshot().position_ms
                      - reverse * 1'000 / 44'100)
           <= 2);
}

void testCacheMissMovesCursorAndRebases(
    const std::filesystem::path& media)
{
    agplayer::AudioEngine engine(agplayer::AudioBackend::Manual, 4'096U);
    assert(engine.load(media.string()) == AG_OK);
    assert(engine.seek(8'000) == AG_OK);
    assert(engine.play() == AG_OK);
    waitForBufferedFrames(engine, 2'048U);
    assert(engine.begin_scratch() == AG_OK);
    assert(!engine.scratch_status().ready);
    assert(engine.scratch_status().buffering);
    waitForScratchReady(engine);
    assert(engine.update_scratch(3.0F) == AG_OK);

    std::vector<float> output(1'024U * 2U);
    const auto miss_deadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(3);
    while (!engine.scratch_status().buffering
           && std::chrono::steady_clock::now() < miss_deadline) {
        engine.render(output.data(), 1'024U);
    }
    const agplayer::ScratchStatus miss = engine.scratch_status();
    assert(!miss.ready);
    assert(miss.buffering);
    const std::int64_t before =
        agplayer::AudioEngineTestAccess::scratchSourceFrame(engine);
    engine.render(output.data(), 1'024U);
    assert(agplayer::AudioEngineTestAccess::scratchSourceFrame(engine)
           > before);

    const auto ready_deadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(3);
    while (!engine.scratch_status().ready
           && std::chrono::steady_clock::now() < ready_deadline) {
        engine.render(output.data(), 1'024U);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(engine.scratch_status().ready);
    assert(!engine.scratch_status().buffering);
    assert(engine.end_scratch() == AG_OK);
}

void testPlayingRestorationAndLifecycleInvalidation(
    const std::filesystem::path& short_media,
    const std::filesystem::path& long_media)
{
    agplayer::AudioEngine playing(agplayer::AudioBackend::Manual, 4'096U);
    assert(playing.load(short_media.string()) == AG_OK);
    assert(playing.play() == AG_OK);
    waitForBufferedFrames(playing, 1'024U);
    assert(playing.begin_scratch() == AG_OK);
    waitForScratchReady(playing);
    assert(playing.update_scratch(1.0F) == AG_OK);
    std::vector<float> output(4'096U * 2U);
    playing.render(output.data(), 4'096U);
    assert(playing.end_scratch() == AG_OK);
    assert(playing.snapshot().state == agplayer::EngineState::Playing);
    waitForBufferedFrames(playing, 512U);

    agplayer::AudioEngine stopped(agplayer::AudioBackend::Manual, 4'096U);
    assert(stopped.load(long_media.string()) == AG_OK);
    assert(stopped.play() == AG_OK);
    waitForBufferedFrames(stopped, 1'024U);
    assert(stopped.begin_scratch() == AG_OK);
    const std::uint64_t scratch_seeks =
        agplayer::AudioEngineTestAccess::physicalScratchSeekCount(stopped);
    assert(stopped.stop() == AG_OK);
    assert(!stopped.scratch_status().active);
    assert(agplayer::AudioEngineTestAccess::physicalScratchSeekCount(stopped)
           == scratch_seeks);

    assert(stopped.play() == AG_OK);
    waitForBufferedFrames(stopped, 1'024U);
    assert(stopped.begin_scratch() == AG_OK);
    assert(stopped.seek(5'000) == AG_OK);
    assert(!stopped.scratch_status().active);
    assert(agplayer::AudioEngineTestAccess::physicalScratchSeekCount(stopped)
           == scratch_seeks);

    agplayer::AudioEngine queued(agplayer::AudioBackend::Manual, 4'096U);
    assert(queued.set_queue({long_media.string(), short_media.string()}, 0U)
           == AG_OK);
    assert(queued.play() == AG_OK);
    waitForBufferedFrames(queued, 1'024U);
    assert(queued.begin_scratch() == AG_OK);
    assert(queued.next() == AG_OK);
    assert(!queued.scratch_status().active);
    assert(queued.snapshot().track_index == 1U);

    agplayer::AudioEngine lost(agplayer::AudioBackend::Manual, 4'096U);
    assert(lost.load(long_media.string()) == AG_OK);
    assert(lost.play() == AG_OK);
    waitForBufferedFrames(lost, 1'024U);
    assert(lost.begin_scratch() == AG_OK);
    lost.simulate_device_loss();
    assert(!lost.scratch_status().active);

    agplayer::AudioEngine stream(agplayer::AudioBackend::Manual, 4'096U);
    assert(stream.load_stream(std::make_shared<SilentStream>()) == AG_OK);
    assert(stream.play() == AG_OK);
    assert(stream.begin_scratch() == AG_INVALID_ARGUMENT);
}

void testQueueEofCancelAndStaleEpoch(
    const std::filesystem::path& short_media,
    const std::filesystem::path& long_media)
{
    agplayer::AudioEngine queue(agplayer::AudioBackend::Manual, 4'096U);
    assert(queue.set_queue({long_media.string(), short_media.string()}, 0U)
           == AG_OK);
    assert(queue.seek(20'000) == AG_OK);
    assert(queue.play() == AG_OK);
    waitForBufferedFrames(queue, 1'024U);
    assert(queue.begin_scratch() == AG_OK);
    waitForScratchReady(queue);
    assert(queue.update_scratch(3.0F) == AG_OK);
    std::vector<float> output(1'024U * 2U);
    const std::int64_t final_frame = 44'100 * 40 - 1;
    const auto end_deadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(10);
    while (agplayer::AudioEngineTestAccess::scratchSourceFrame(queue)
               < final_frame
           && std::chrono::steady_clock::now() < end_deadline) {
        queue.render(output.data(), 1'024U);
        if (queue.scratch_status().buffering) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    assert(agplayer::AudioEngineTestAccess::scratchSourceFrame(queue)
           == final_frame);
    assert(queue.snapshot().state == agplayer::EngineState::Playing);
    assert(queue.snapshot().track_index == 0U);
    assert(agplayer::AudioEngineTestAccess::pendingBoundary(queue) < 0);

    const std::uint64_t before_cancel =
        agplayer::AudioEngineTestAccess::physicalScratchSeekCount(queue);
    assert(queue.cancel_scratch() == AG_OK);
    assert(agplayer::AudioEngineTestAccess::physicalScratchSeekCount(queue)
           == before_cancel + 1U);
    assert(queue.cancel_scratch() == AG_OK);
    assert(agplayer::AudioEngineTestAccess::physicalScratchSeekCount(queue)
           == before_cancel + 1U);

    agplayer::AudioEngine stale(agplayer::AudioBackend::Manual, 4'096U);
    assert(stale.load(long_media.string()) == AG_OK);
    assert(stale.play() == AG_OK);
    waitForBufferedFrames(stale, 1'024U);
    const ag_result second_begin = stale.begin_scratch();
    if (second_begin != AG_OK) {
        std::cerr << "second begin after load failed: " << second_begin
                  << " state=" << static_cast<int>(stale.snapshot().state)
                  << " buffered=" << stale.buffered_frames() << '\n';
    }
    assert(second_begin == AG_OK);
    const std::uint64_t before_load =
        agplayer::AudioEngineTestAccess::physicalScratchSeekCount(stale);
    assert(stale.load(short_media.string()) == AG_OK);
    assert(!stale.scratch_status().active);
    assert(agplayer::AudioEngineTestAccess::physicalScratchSeekCount(stale)
           == before_load);
    assert(stale.play() == AG_OK);
    waitForBufferedFrames(stale, 512U);
    const ag_result reloaded_begin = stale.begin_scratch();
    if (reloaded_begin != AG_OK) {
        std::cerr << "begin after replacement load failed: "
                  << reloaded_begin
                  << " state=" << static_cast<int>(stale.snapshot().state)
                  << " buffered=" << stale.buffered_frames() << '\n';
    }
    assert(reloaded_begin == AG_OK);
    assert(stale.end_scratch() == AG_OK);
}

void testPlayPauseCommandsDuringScratch(
    const std::filesystem::path& media)
{
    std::vector<float> output(1'024U * 2U);

    agplayer::AudioEngine paused(agplayer::AudioBackend::Manual, 4'096U);
    assert(paused.load(media.string()) == AG_OK);
    assert(paused.play() == AG_OK);
    waitForBufferedFrames(paused, 1'024U);
    assert(paused.pause() == AG_OK);
    assert(paused.begin_scratch() == AG_OK);
    waitForScratchReady(paused);
    assert(paused.update_scratch(1.0F) == AG_OK);
    paused.render(output.data(), 1'024U);
    const std::uint64_t paused_seeks =
        agplayer::AudioEngineTestAccess::physicalScratchSeekCount(paused);
    assert(paused.pause() == AG_OK);
    assert(!paused.scratch_status().active);
    assert(paused.snapshot().state == agplayer::EngineState::Paused);
    assert(agplayer::AudioEngineTestAccess::physicalScratchSeekCount(paused)
           == paused_seeks + 1U);

    agplayer::AudioEngine playing(agplayer::AudioBackend::Manual, 4'096U);
    assert(playing.load(media.string()) == AG_OK);
    assert(playing.play() == AG_OK);
    waitForBufferedFrames(playing, 1'024U);
    assert(playing.begin_scratch() == AG_OK);
    waitForScratchReady(playing);
    const std::uint64_t playing_seeks =
        agplayer::AudioEngineTestAccess::physicalScratchSeekCount(playing);
    assert(playing.pause() == AG_OK);
    assert(!playing.scratch_status().active);
    assert(playing.snapshot().state == agplayer::EngineState::Paused);
    assert(agplayer::AudioEngineTestAccess::physicalScratchSeekCount(playing)
           == playing_seeks + 1U);

    assert(playing.begin_scratch() == AG_OK);
    assert(playing.play() == AG_INVALID_ARGUMENT);
    assert(playing.scratch_status().active);
    assert(playing.snapshot().state == agplayer::EngineState::Paused);
    assert(playing.cancel_scratch() == AG_OK);
}

void testConcurrentRateUpdatesRebaseAndLifecycle(
    const std::filesystem::path& media)
{
    const auto run_updater = [](agplayer::AudioEngine& engine,
                                std::atomic<bool>& running,
                                std::atomic<int>& unexpected) {
        constexpr std::array<float, 5U> rates{
            0.0F, 0.25F, -0.25F, 0.5F, -0.5F};
        std::size_t index = 0U;
        while (running.load(std::memory_order_acquire)) {
            const ag_result result = engine.update_scratch(
                rates[index++ % rates.size()]);
            if (result != AG_OK && result != AG_INVALID_ARGUMENT) {
                unexpected.fetch_add(1, std::memory_order_relaxed);
            }
            std::this_thread::yield();
        }
    };

    agplayer::AudioEngine cancelled(agplayer::AudioBackend::Manual, 4'096U);
    assert(cancelled.load(media.string()) == AG_OK);
    assert(cancelled.seek(20'000) == AG_OK);
    assert(cancelled.play() == AG_OK);
    waitForBufferedFrames(cancelled, 1'024U);
    assert(cancelled.begin_scratch() == AG_OK);
    waitForScratchReady(cancelled);

    std::atomic<bool> updating{false};
    std::atomic<int> unexpected{0};
    std::vector<float> output(1'024U * 2U);
    assert(cancelled.update_scratch(3.0F) == AG_OK);
    const auto miss_deadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(3);
    while (!cancelled.scratch_status().buffering
           && std::chrono::steady_clock::now() < miss_deadline) {
        cancelled.render(output.data(), 1'024U);
        const agplayer::ScratchStatus status = cancelled.scratch_status();
        assert(!(status.ready && status.buffering));
    }
    assert(cancelled.scratch_status().buffering);
    updating.store(true, std::memory_order_release);
    std::thread update_thread(
        run_updater, std::ref(cancelled), std::ref(updating),
        std::ref(unexpected));
    const auto rebase_deadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(3);
    while (!cancelled.scratch_status().ready
           && std::chrono::steady_clock::now() < rebase_deadline) {
        cancelled.render(output.data(), 1'024U);
        std::this_thread::yield();
    }
    const bool rebased = cancelled.scratch_status().ready;
    const ag_result cancel_result = cancelled.cancel_scratch();
    updating.store(false, std::memory_order_release);
    update_thread.join();
    assert(rebased);
    assert(cancel_result == AG_OK);
    assert(unexpected.load(std::memory_order_acquire) == 0);
    assert(!cancelled.scratch_status().active);

    agplayer::AudioEngine stopped(agplayer::AudioBackend::Manual, 4'096U);
    assert(stopped.load(media.string()) == AG_OK);
    assert(stopped.play() == AG_OK);
    waitForBufferedFrames(stopped, 1'024U);
    assert(stopped.begin_scratch() == AG_OK);
    waitForScratchReady(stopped);
    updating.store(true, std::memory_order_release);
    unexpected.store(0, std::memory_order_release);
    std::thread stop_update_thread(
        run_updater, std::ref(stopped), std::ref(updating),
        std::ref(unexpected));
    for (int iteration = 0; iteration < 64; ++iteration) {
        stopped.render(output.data(), 1'024U);
    }
    assert(stopped.stop() == AG_OK);
    updating.store(false, std::memory_order_release);
    stop_update_thread.join();
    assert(unexpected.load(std::memory_order_acquire) == 0);
    const agplayer::ScratchStatus stopped_status = stopped.scratch_status();
    assert(!stopped_status.active);
    assert(!stopped_status.ready);
    assert(!stopped_status.buffering);
}

void testQueueDeviceEditorAndErrorLifecycleGates(
    const std::filesystem::path& media,
    const std::filesystem::path& queued_media)
{
    agplayer::AudioEngine queued(agplayer::AudioBackend::Manual, 4'096U);
    assert(queued.set_queue(
               {media.string(), queued_media.string()}, 0U) == AG_OK);
    assert(queued.play() == AG_OK);
    waitForBufferedFrames(queued, 1'024U);
    assert(queued.begin_scratch() == AG_OK);
    waitForScratchReady(queued);
    const std::uint64_t queue_seeks =
        agplayer::AudioEngineTestAccess::physicalScratchSeekCount(queued);
    assert(queued.queue_next(queued_media.string()) == AG_OK);
    assert(!queued.scratch_status().active);
    assert(agplayer::AudioEngineTestAccess::physicalScratchSeekCount(queued)
           == queue_seeks);
    assert(queued.snapshot().state == agplayer::EngineState::Playing);

    agplayer::AudioEngine notified(agplayer::AudioBackend::Manual, 4'096U);
    assert(notified.load(media.string()) == AG_OK);
    assert(notified.play() == AG_OK);
    waitForBufferedFrames(notified, 1'024U);
    assert(notified.begin_scratch() == AG_OK);
    waitForScratchReady(notified);
    const std::uint64_t notification_seeks =
        agplayer::AudioEngineTestAccess::physicalScratchSeekCount(notified);
    agplayer::AudioEngineTestAccess::markDeviceLost(notified);
    const agplayer::ScratchStatus lost_status = notified.scratch_status();
    assert(!lost_status.active);
    assert(!lost_status.ready);
    assert(!lost_status.buffering);
    assert(agplayer::AudioEngineTestAccess::physicalScratchSeekCount(notified)
           == notification_seeks);
    assert(notified.retry_device() == AG_OK);
    assert(notified.snapshot().state == agplayer::EngineState::Paused);
    assert(notified.begin_scratch() == AG_OK);
    assert(notified.cancel_scratch() == AG_OK);

    agplayer::AudioEngine device(agplayer::AudioBackend::Manual, 4'096U);
    assert(device.load(media.string()) == AG_OK);
    assert(device.play() == AG_OK);
    waitForBufferedFrames(device, 1'024U);
    assert(device.begin_scratch() == AG_OK);
    agplayer::AudioEngineTestAccess::markDeviceLost(device);
    assert(device.set_output_device({}, false) == AG_OK);
    assert(!device.scratch_status().active);
    assert(device.snapshot().state == agplayer::EngineState::Paused);
    assert(device.begin_scratch() == AG_OK);
    assert(device.cancel_scratch() == AG_OK);

    agplayer::AudioEngine editor(agplayer::AudioBackend::Manual, 4'096U);
    assert(editor.load(media.string()) == AG_OK);
    assert(editor.play() == AG_OK);
    waitForBufferedFrames(editor, 1'024U);
    assert(editor.begin_scratch() == AG_OK);
    const std::uint64_t editor_seeks =
        agplayer::AudioEngineTestAccess::physicalScratchSeekCount(editor);
    assert(editor.load_stream(std::make_shared<SilentStream>(), true) == AG_OK);
    assert(!editor.scratch_status().active);
    assert(agplayer::AudioEngineTestAccess::physicalScratchSeekCount(editor)
           == editor_seeks);
    assert(editor.begin_scratch() == AG_INVALID_ARGUMENT);

    agplayer::AudioEngine failed(agplayer::AudioBackend::Manual, 4'096U);
    assert(failed.load(media.string()) == AG_OK);
    assert(failed.play() == AG_OK);
    waitForBufferedFrames(failed, 1'024U);
    assert(failed.begin_scratch() == AG_OK);
    const std::uint64_t error_seeks =
        agplayer::AudioEngineTestAccess::physicalScratchSeekCount(failed);
    assert(agplayer::AudioEngineTestAccess::enterError(
               failed, AG_INTERNAL_ERROR) == AG_INTERNAL_ERROR);
    assert(!failed.scratch_status().active);
    assert(failed.snapshot().state == agplayer::EngineState::Error);
    assert(agplayer::AudioEngineTestAccess::physicalScratchSeekCount(failed)
           == error_seeks);
}

void testBackendDeviceNotificationUsesOnlyBoundedAtomics()
{
    agplayer::AudioEngine engine(agplayer::AudioBackend::Manual, 4'096U);
    std::atomic<int> timeline_writes{0};
    agplayer::AudioEngineTestAccess::setTimelineHook(
        engine, &countTimelineWrite, &timeline_writes);
    agplayer::AudioEngineTestAccess::notifyDeviceLostFromBackend(engine);
    agplayer::AudioEngineTestAccess::setTimelineHook(engine, nullptr, nullptr);

    assert(timeline_writes.load(std::memory_order_acquire) == 0);
    assert(engine.device_lost());
    assert(engine.snapshot().state == agplayer::EngineState::Error);
    assert(!engine.scratch_status().active);
}

void testDeviceLossBeforePendingPreventsScratchCommit(
    const std::filesystem::path& media)
{
    constexpr int before_pending = 1;
    constexpr int pending_published = 3;
    agplayer::AudioEngine engine(agplayer::AudioBackend::Manual, 4'096U);
    assert(engine.load(media.string()) == AG_OK);
    assert(engine.play() == AG_OK);
    waitForBufferedFrames(engine, 1'024U);
    assert(engine.begin_scratch() == AG_OK);
    waitForScratchReady(engine);

    const std::uint64_t seeks_before =
        agplayer::AudioEngineTestAccess::physicalScratchSeekCount(engine);
    ScratchCommitBarrier barrier{before_pending};
    agplayer::AudioEngineTestAccess::setScratchCommitHook(
        engine, &scratchCommitBarrier, &barrier);
    std::atomic<bool> end_done{false};
    ag_result end_result = AG_OK;
    std::thread end_thread([&] {
        end_result = engine.end_scratch();
        end_done.store(true, std::memory_order_release);
    });

    const bool reached = waitForCommitPhase(
        barrier, before_pending, std::chrono::seconds(3));
    if (reached) {
        agplayer::AudioEngineTestAccess::notifyDeviceLostFromBackend(engine);
    }
    barrier.release.store(true, std::memory_order_release);
    const bool published_after_loss = waitForCommitPhase(
        barrier, pending_published, std::chrono::milliseconds(250));
    const bool completed_without_rescue = waitForTrue(
        end_done, std::chrono::seconds(1));
    if (!completed_without_rescue) {
        agplayer::AudioEngineTestAccess::requestDecodeExit(engine);
    }
    assert(waitForTrue(end_done, std::chrono::seconds(3)));
    end_thread.join();
    agplayer::AudioEngineTestAccess::setScratchCommitHook(
        engine, nullptr, nullptr);

    assert(reached);
    assert(!published_after_loss);
    assert(completed_without_rescue);
    assert(end_result != AG_OK);
    assert(agplayer::AudioEngineTestAccess::physicalScratchSeekCount(engine)
           == seeks_before);
}

void testDeviceLossBeforePhysicalSeekCancelsScratchCommit(
    const std::filesystem::path& media)
{
    constexpr int before_physical_seek = 2;
    agplayer::AudioEngine engine(agplayer::AudioBackend::Manual, 4'096U);
    assert(engine.load(media.string()) == AG_OK);
    assert(engine.play() == AG_OK);
    waitForBufferedFrames(engine, 1'024U);
    assert(engine.begin_scratch() == AG_OK);
    waitForScratchReady(engine);

    const std::uint64_t seeks_before =
        agplayer::AudioEngineTestAccess::physicalScratchSeekCount(engine);
    ScratchCommitBarrier barrier{before_physical_seek};
    agplayer::AudioEngineTestAccess::setScratchCommitHook(
        engine, &scratchCommitBarrier, &barrier);
    std::atomic<bool> end_done{false};
    ag_result end_result = AG_OK;
    std::thread end_thread([&] {
        end_result = engine.end_scratch();
        end_done.store(true, std::memory_order_release);
    });

    const bool reached = waitForCommitPhase(
        barrier, before_physical_seek, std::chrono::seconds(3));
    if (reached) {
        agplayer::AudioEngineTestAccess::notifyDeviceLostFromBackend(engine);
    }
    barrier.release.store(true, std::memory_order_release);
    const bool completed_without_rescue = waitForTrue(
        end_done, std::chrono::seconds(1));
    if (!completed_without_rescue) {
        agplayer::AudioEngineTestAccess::requestDecodeExit(engine);
    }
    assert(waitForTrue(end_done, std::chrono::seconds(3)));
    end_thread.join();
    agplayer::AudioEngineTestAccess::setScratchCommitHook(
        engine, nullptr, nullptr);

    assert(reached);
    assert(completed_without_rescue);
    assert(end_result != AG_OK);
    assert(agplayer::AudioEngineTestAccess::physicalScratchSeekCount(engine)
           == seeks_before);
}

} // namespace

int main(const int argc, char** argv)
{
#ifdef _WIN32
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
    assert(argc == 3);
    const std::filesystem::path media = std::filesystem::u8path(argv[1]);
    testBackendDeviceNotificationUsesOnlyBoundedAtomics();
    testDeviceLossBeforePendingPreventsScratchCommit(
        std::filesystem::u8path(argv[2]));
    testDeviceLossBeforePhysicalSeekCancelsScratchCommit(
        std::filesystem::u8path(argv[2]));
    testBeginAndCommandValidation(media);
    testPausedRawScratchDirectionsAndFinalCommit(media);
    testCacheMissMovesCursorAndRebases(
        std::filesystem::u8path(argv[2]));
    testPlayingRestorationAndLifecycleInvalidation(
        media, std::filesystem::u8path(argv[2]));
    testQueueEofCancelAndStaleEpoch(
        media, std::filesystem::u8path(argv[2]));
    testPlayPauseCommandsDuringScratch(
        std::filesystem::u8path(argv[2]));
    testConcurrentRateUpdatesRebaseAndLifecycle(
        std::filesystem::u8path(argv[2]));
    testQueueDeviceEditorAndErrorLifecycleGates(
        media, std::filesystem::u8path(argv[2]));
    return 0;
}
