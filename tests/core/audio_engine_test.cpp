// Test files deliberately keep assert() active even in Release builds: many
// test cases embed function calls with side effects inside assert(), and
// silencing them under NDEBUG would skip those calls and crash on cleanup.
#undef NDEBUG

#include <agplayer/c_api.h>

#include "../../core/src/audio_editor/editor_player_bridge.hpp"
#include "../../core/src/audio_engine.hpp"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <crtdbg.h>
#endif

namespace agplayer {

class AudioEngineTestAccess final {
public:
    using TimelineHook = void (*)(void*, bool) noexcept;

    static void setTimelineHook(AudioEngine& engine, TimelineHook hook,
                                void* context) noexcept
    {
        engine.set_timeline_test_hook(hook, context);
    }
};

} // namespace agplayer

namespace {

class TimelineBarrier final {
public:
    static void hook(void* context, const bool realtime) noexcept
    {
        static_cast<TimelineBarrier*>(context)->arrive(realtime);
    }

    void blockNext(const bool realtime)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        target_realtime_ = realtime;
        entered_ = false;
        released_ = false;
    }

    void waitUntilEntered()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        const bool entered = cv_.wait_for(
            lock, std::chrono::seconds(2), [this] { return entered_; });
        assert(entered);
    }

    void release()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        released_ = true;
        cv_.notify_all();
    }

private:
    void arrive(const bool realtime) noexcept
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (realtime != target_realtime_ || entered_) return;
        entered_ = true;
        cv_.notify_all();
        cv_.wait(lock, [this] { return released_; });
    }

    std::mutex mutex_;
    std::condition_variable cv_;
    bool target_realtime_{};
    bool entered_{};
    bool released_{};
};

class ObservedRampStream final : public agplayer::IAudioStreamSource {
public:
    ObservedRampStream()
    {
        metadata_.sample_rate = 48'000;
        metadata_.channels = 1;
        metadata_.duration_ms = 4'000;
    }

    const agplayer::MediaMetadata& metadata() const noexcept override
    {
        return metadata_;
    }

    ag_result read(agplayer::DecodedAudioBlock& block) noexcept override
    {
        constexpr std::int64_t totalFrames = 48'000 * 4;
        const std::size_t frames = static_cast<std::size_t>(
            std::max<std::int64_t>(0, std::min<std::int64_t>(
                1'024, totalFrames - position_frames_)));
        block = {};
        block.frames = frames;
        block.timestamp_frame = position_frames_;
        block.timestamp_ms = position_frames_ * 1'000 / 48'000;
        block.samples.resize(frames);
        for (std::size_t frame = 0; frame < frames; ++frame) {
            block.samples[frame] = static_cast<float>(
                (position_frames_ + static_cast<std::int64_t>(frame)) % 997)
                / 997.0F;
        }
        position_frames_ += static_cast<std::int64_t>(frames);
        block.end_of_stream = position_frames_ >= totalFrames;
        return AG_OK;
    }

    ag_result seek(const std::int64_t positionMs) noexcept override
    {
        if (positionMs < 0 || positionMs > metadata_.duration_ms) {
            return AG_INVALID_ARGUMENT;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            position_frames_ = positionMs * 48'000 / 1'000;
            ++seek_count_;
        }
        cv_.notify_all();
        return AG_OK;
    }

    void waitForSeek()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        const bool observed = cv_.wait_for(
            lock, std::chrono::seconds(2), [this] { return seek_count_ > 0; });
        assert(observed);
    }

private:
    agplayer::MediaMetadata metadata_;
    std::int64_t position_frames_{};
    std::mutex mutex_;
    std::condition_variable cv_;
    int seek_count_{};
};

constexpr double kMeterFloorDb = -120.0;

bool waitForBufferedFrames(agplayer::AudioEngine& engine,
                           const std::size_t minimumFrames,
                           const std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    do {
        if (engine.buffered_frames() >= minimumFrames) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}

bool waitForBarrierPhase(std::atomic<int>& phase,
                         const int expected,
                         const std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    do {
        if (phase.load(std::memory_order_acquire) >= expected) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}

bool waitForFlag(std::atomic<bool>& flag,
                 const std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    do {
        if (flag.load(std::memory_order_acquire)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}

bool waitForPlayerState(ag_player* const player,
                        const ag_playback_state expected,
                        ag_playback_snapshot& snapshot,
                        const std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    do {
        if (ag_player_snapshot(player, &snapshot) == AG_OK
            && snapshot.state == expected) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}

template <typename Predicate>
bool waitForEngineOutputPeak(agplayer::AudioEngine& engine,
                             Predicate&& predicate,
                             const std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    do {
        if (predicate(engine.equalizer_status().output_peak_db)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}

template <typename Predicate>
double waitForOutputPeak(ag_player* const player,
                         Predicate&& predicate,
                         const std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    double peakDb = kMeterFloorDb;
    do {
        ag_equalizer_status status{};
        assert(ag_player_equalizer_status(player, &status) == AG_OK);
        peakDb = status.output_peak_db;
        if (predicate(peakDb)) {
            return peakDb;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while (std::chrono::steady_clock::now() < deadline);
    assert(false && "timed out waiting for output peak");
    return peakDb;
}

class SilentEditorStream final : public agplayer::IAudioStreamSource {
public:
    explicit SilentEditorStream(const std::int64_t durationMs)
    {
        metadata_.sample_rate = 48'000;
        metadata_.channels = 2;
        metadata_.duration_ms = durationMs;
        total_frames_ = durationMs * metadata_.sample_rate / 1'000;
    }

    const agplayer::MediaMetadata& metadata() const noexcept override
    {
        return metadata_;
    }

    ag_result read(agplayer::DecodedAudioBlock& block) noexcept override
    {
        const std::int64_t remaining = total_frames_ - position_frames_;
        const std::size_t frames = static_cast<std::size_t>(
            std::max<std::int64_t>(0, std::min<std::int64_t>(1'024, remaining)));
        block = {};
        block.frames = frames;
        block.timestamp_frame = position_frames_;
        block.timestamp_ms = position_frames_ * 1'000 / metadata_.sample_rate;
        block.samples.assign(frames * static_cast<std::size_t>(metadata_.channels),
                             0.0F);
        position_frames_ += static_cast<std::int64_t>(frames);
        block.end_of_stream = position_frames_ >= total_frames_;
        return AG_OK;
    }

    ag_result seek(const std::int64_t positionMs) noexcept override
    {
        if (positionMs < 0 || positionMs > metadata_.duration_ms) {
            return AG_INVALID_ARGUMENT;
        }
        position_frames_ = positionMs * metadata_.sample_rate / 1'000;
        return AG_OK;
    }

private:
    agplayer::MediaMetadata metadata_;
    std::int64_t total_frames_{};
    std::int64_t position_frames_{};
};

class PartialUnderrunStream final : public agplayer::IAudioStreamSource {
public:
    PartialUnderrunStream()
    {
        metadata_.sample_rate = 1'000;
        metadata_.channels = 1;
        metadata_.duration_ms = 10'000;
    }

    const agplayer::MediaMetadata& metadata() const noexcept override
    {
        return metadata_;
    }

    ag_result read(agplayer::DecodedAudioBlock& block) noexcept override
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (stage_ == 0) {
            stage_ = 1;
            lock.unlock();
            block = {};
            block.frames = 100U;
            block.samples.assign(100U, 1.0F);
            return AG_OK;
        }
        if (stage_ == 1) {
            condition_.wait(lock, [this] { return release_second_ || finish_; });
            if (!finish_) {
                stage_ = 2;
                lock.unlock();
                block = {};
                block.frames = 25U;
                block.samples.assign(25U, 0.0F);
                return AG_OK;
            }
        }
        condition_.wait(lock, [this] { return finish_; });
        lock.unlock();
        block = {};
        block.end_of_stream = true;
        return AG_OK;
    }

    ag_result seek(const std::int64_t positionMs) noexcept override
    {
        return positionMs >= 0 && positionMs <= metadata_.duration_ms
            ? AG_OK
            : AG_INVALID_ARGUMENT;
    }

    void releaseSecondBlock()
    {
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            release_second_ = true;
        }
        condition_.notify_all();
    }

    void finish()
    {
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            finish_ = true;
        }
        condition_.notify_all();
    }

private:
    agplayer::MediaMetadata metadata_;
    std::mutex mutex_;
    std::condition_variable condition_;
    int stage_ = 0;
    bool release_second_ = false;
    bool finish_ = false;
};

class ContinuousAudioStream final : public agplayer::IAudioStreamSource {
public:
    ContinuousAudioStream()
    {
        metadata_.sample_rate = 48'000;
        metadata_.channels = 2;
        metadata_.duration_ms = 60'000;
    }

    const agplayer::MediaMetadata& metadata() const noexcept override
    {
        return metadata_;
    }

    ag_result read(agplayer::DecodedAudioBlock& block) noexcept override
    {
        constexpr std::size_t frames = 1'024U;
        block = {};
        block.frames = frames;
        block.timestamp_frame = position_frames_;
        block.timestamp_ms = position_frames_ * 1'000 / metadata_.sample_rate;
        block.samples.assign(frames * 2U, 0.5F);
        position_frames_ += static_cast<std::int64_t>(frames);
        return AG_OK;
    }

    ag_result seek(const std::int64_t positionMs) noexcept override
    {
        if (positionMs < 0 || positionMs > metadata_.duration_ms) {
            return AG_INVALID_ARGUMENT;
        }
        position_frames_ = positionMs * metadata_.sample_rate / 1'000;
        return AG_OK;
    }

private:
    agplayer::MediaMetadata metadata_;
    std::int64_t position_frames_ = 0;
};

bool outputMeterInvalidatesAcrossPlaybackBoundaries()
{
    agplayer::AudioEngine engine(agplayer::AudioBackend::Manual, 4'096U);
    assert(engine.load_stream(std::make_shared<ContinuousAudioStream>())
           == AG_OK);
    assert(waitForBufferedFrames(engine, 64U,
                                 std::chrono::milliseconds(1'000)));
    assert(engine.play() == AG_OK);
    engine.set_visual_pcm_enabled(true);
    std::vector<float> output(64U * 2U);
    engine.render(output.data(), 64U);
    if (engine.equalizer_status().output_peak_db <= kMeterFloorDb) {
        std::fprintf(stderr, "boundary regression setup did not publish a peak\n");
        return false;
    }
    ag_visual_pcm_snapshot beforePause{};
    engine.read_visual_pcm(beforePause);
    assert(beforePause.sample_count == 64U);

    assert(engine.pause() == AG_OK);
    assert(engine.play() == AG_OK);
    bool ok = engine.equalizer_status().output_peak_db == kMeterFloorDb;

    assert(waitForBufferedFrames(engine, 64U,
                                 std::chrono::milliseconds(1'000)));
    engine.render(output.data(), 64U);
    ag_visual_pcm_snapshot afterPause{};
    engine.read_visual_pcm(afterPause);
    ok = ok && afterPause.generation == beforePause.generation
        && afterPause.sample_count == 64U
        && afterPause.first_sample_index
            == beforePause.first_sample_index + beforePause.sample_count;
    const ag_visual_pcm_snapshot beforeMute = afterPause;
    assert(beforeMute.sample_count == 64U);
    engine.set_muted(true);
    engine.set_muted(false);
    ok = ok && engine.equalizer_status().output_peak_db == kMeterFloorDb;

    assert(waitForBufferedFrames(engine, 64U,
                                 std::chrono::milliseconds(1'000)));
    engine.render(output.data(), 64U);
    ag_visual_pcm_snapshot afterMute{};
    engine.read_visual_pcm(afterMute);
    ok = ok && afterMute.generation == beforeMute.generation
        && afterMute.sample_count == 64U
        && afterMute.first_sample_index
            == beforeMute.first_sample_index + beforeMute.sample_count;

    assert(waitForBufferedFrames(engine, 64U,
                                 std::chrono::milliseconds(1'000)));
    engine.render(output.data(), 64U);
    assert(engine.seek(500) == AG_OK);
    ok = ok && engine.equalizer_status().output_peak_db == kMeterFloorDb;

    assert(waitForBufferedFrames(engine, 64U,
                                 std::chrono::milliseconds(1'000)));
    engine.render(output.data(), 64U);
    engine.simulate_device_loss();
    assert(engine.retry_device() == AG_OK);
    assert(engine.play() == AG_OK);
    ok = ok && engine.equalizer_status().output_peak_db == kMeterFloorDb;

    assert(engine.stop() == AG_OK);
    assert(engine.play() == AG_OK);
    ok = ok && engine.equalizer_status().output_peak_db == kMeterFloorDb;
    if (!ok) {
        std::fprintf(stderr,
                     "output meter exposed a stale peak after a playback boundary\n");
    }
    return ok;
}

bool outputDeviceSwitchRejectsInFlightOldCallback()
{
    agplayer::OutputDeviceSwitchTestBarrier barrier;
    agplayer::AudioEngine engine(agplayer::AudioBackend::Null, 4'096U);
    assert(engine.load_stream(std::make_shared<ContinuousAudioStream>())
           == AG_OK);
    assert(waitForBufferedFrames(engine, 64U,
                                 std::chrono::milliseconds(1'000)));
    assert(engine.play() == AG_OK);

    engine.set_output_device_switch_test_barrier(&barrier);
    std::atomic<ag_result> switch_result{AG_INTERNAL_ERROR};
    std::thread switch_thread([&] {
        switch_result.store(engine.set_output_device({}, true),
                            std::memory_order_release);
    });

    const bool old_callback_entered = waitForBarrierPhase(
        barrier.entered_phase, 1, std::chrono::milliseconds(2'000));
    if (!old_callback_entered) {
        barrier.cancelled.store(true, std::memory_order_release);
        switch_thread.join();
        engine.set_output_device_switch_test_barrier(nullptr);
        std::fprintf(stderr,
                     "old output callback did not enter switch barrier\n");
        return false;
    }
    barrier.release_phase.store(1, std::memory_order_release);

    const bool new_callback_entered = waitForBarrierPhase(
        barrier.entered_phase, 2, std::chrono::milliseconds(2'000));
    const bool stale_peak_rejected =
        engine.equalizer_status().output_peak_db == kMeterFloorDb;
    barrier.release_phase.store(2, std::memory_order_release);
    if (!new_callback_entered) {
        barrier.cancelled.store(true, std::memory_order_release);
    }
    switch_thread.join();
    engine.set_output_device_switch_test_barrier(nullptr);

    const bool switched =
        switch_result.load(std::memory_order_acquire) == AG_OK;
    if (!new_callback_entered) {
        std::fprintf(stderr,
                     "new output callback did not enter switch barrier\n");
    }
    if (!stale_peak_rejected) {
        std::fprintf(stderr,
                     "old output callback peak survived device switch\n");
    }
    return switched && new_callback_entered && stale_peak_rejected;
}

bool outputDeviceSwitchFailureRejectsInFlightOldCallback(
    const agplayer::OutputDeviceSwitchTestFailure failure,
    const ag_result expected_result,
    const char* const label)
{
    agplayer::OutputDeviceSwitchTestBarrier barrier;
    agplayer::AudioEngine engine(agplayer::AudioBackend::Null, 4'096U);
    assert(engine.load_stream(std::make_shared<ContinuousAudioStream>())
           == AG_OK);
    assert(waitForBufferedFrames(engine, 1'024U,
                                 std::chrono::milliseconds(1'000)));
    assert(engine.play() == AG_OK);
    assert(waitForEngineOutputPeak(
        engine, [](const double peak) { return peak > kMeterFloorDb; },
        std::chrono::milliseconds(1'000)));

    barrier.failure.store(failure, std::memory_order_release);
    engine.set_output_device_switch_test_barrier(&barrier);
    std::atomic<ag_result> switch_result{AG_OK};
    std::atomic<bool> switch_done{false};
    std::thread switch_thread([&] {
        switch_result.store(engine.set_output_device({}, true),
                            std::memory_order_release);
        switch_done.store(true, std::memory_order_release);
    });

    const bool old_callback_entered = waitForBarrierPhase(
        barrier.entered_phase, 1, std::chrono::milliseconds(2'000));
    const bool failed_while_old_callback_held = old_callback_entered
        && waitForFlag(switch_done, std::chrono::milliseconds(2'000));
    if (!failed_while_old_callback_held) {
        barrier.cancelled.store(true, std::memory_order_release);
        barrier.release_phase.store(2, std::memory_order_release);
        switch_thread.join();
        engine.set_output_device_switch_test_barrier(nullptr);
        std::fprintf(stderr, "%s did not fail while old callback was held\n",
                     label);
        return false;
    }
    switch_thread.join();

    barrier.armed_phase.store(2, std::memory_order_release);
    barrier.release_phase.store(1, std::memory_order_release);
    const bool fresh_callback_held = waitForBarrierPhase(
        barrier.entered_phase, 2, std::chrono::milliseconds(2'000));
    const bool stale_peak_rejected = fresh_callback_held
        && engine.equalizer_status().output_peak_db == kMeterFloorDb;
    barrier.release_phase.store(2, std::memory_order_release);
    if (!fresh_callback_held) {
        barrier.cancelled.store(true, std::memory_order_release);
    }
    engine.set_output_device_switch_test_barrier(nullptr);

    const bool result_matches =
        switch_result.load(std::memory_order_acquire) == expected_result;
    const bool fresh_peak_published = waitForEngineOutputPeak(
        engine, [](const double peak) { return peak > kMeterFloorDb; },
        std::chrono::milliseconds(1'000));
    if (!stale_peak_rejected) {
        std::fprintf(stderr, "%s accepted an in-flight stale peak\n", label);
    }
    if (!fresh_peak_published) {
        std::fprintf(stderr, "%s did not resume fresh meter publication\n",
                     label);
    }
    return result_matches && stale_peak_rejected && fresh_peak_published;
}

bool outputDeviceStopFailureRejectsInFlightOldCallback()
{
    return outputDeviceSwitchFailureRejectsInFlightOldCallback(
        agplayer::OutputDeviceSwitchTestFailure::StopOutput,
        AG_DEVICE_ERROR, "stop_output failure");
}

bool outputDeviceSnapshotExceptionRejectsInFlightOldCallback()
{
    return outputDeviceSwitchFailureRejectsInFlightOldCallback(
        agplayer::OutputDeviceSwitchTestFailure::BeforeStateSnapshot,
        AG_INTERNAL_ERROR, "pre-shutdown snapshot exception");
}

bool outputMeterUsesRequestedFramesForPartialUnderrun()
{
    auto stream = std::make_shared<PartialUnderrunStream>();
    agplayer::AudioEngine engine(agplayer::AudioBackend::Manual, 128U);
    assert(engine.load_stream(stream) == AG_OK);
    assert(waitForBufferedFrames(engine, 100U,
                                 std::chrono::milliseconds(1'000)));
    assert(engine.play() == AG_OK);
    std::vector<float> output(100U);
    engine.render(output.data(), 100U);
    assert(std::abs(engine.equalizer_status().output_peak_db) < 0.01);

    stream->releaseSecondBlock();
    assert(waitForBufferedFrames(engine, 25U,
                                 std::chrono::milliseconds(1'000)));
    engine.render(output.data(), 100U);
    const double partialPeakDb = engine.equalizer_status().output_peak_db;
    const bool usesRequestedFrames =
        partialPeakDb < -1.1 && partialPeakDb > -1.3;

    engine.render(output.data(), 100U);
    const bool emptyReadFloors =
        engine.equalizer_status().output_peak_db == kMeterFloorDb;
    stream->finish();
    if (!usesRequestedFrames) {
        std::fprintf(stderr,
                     "partial underrun release was %.3f dB, expected about -1.2 dB\n",
                     partialPeakDb);
    }
    if (!emptyReadFloors) {
        std::fprintf(stderr, "zero-frame render did not publish meter floor\n");
    }
    return usesRequestedFrames && emptyReadFloors;
}

bool equalizerSubmitFailureKeepsPublishedState()
{
    agplayer::AudioEngine engine(agplayer::AudioBackend::Manual, 128U);
    assert(engine.load_stream(std::make_shared<SilentEditorStream>(3'000))
           == AG_OK);

    agplayer::GraphicEqSettings accepted;
    accepted.enabled = true;
    accepted.auto_clip_protection = true;
    accepted.preamp_db = -1.5;
    accepted.band_gain_db[0] = 4.0;
    assert(engine.set_equalizer(accepted, 41U) == AG_OK);
    const agplayer::EqualizerStatus before = engine.equalizer_status();

    agplayer::GraphicEqSettings rejected = accepted;
    rejected.enabled = false;
    rejected.bypassed = true;
    rejected.auto_clip_protection = false;
    rejected.preamp_db = 3.0;
    rejected.band_gain_db[0] = -6.0;
    engine.fail_next_equalizer_submit_for_test();
    const ag_result rejectedResult = engine.set_equalizer(rejected, 42U);
    const agplayer::EqualizerStatus afterFailure = engine.equalizer_status();

    const bool failurePreservedState =
        rejectedResult == AG_INTERNAL_ERROR
        && afterFailure.revision == before.revision
        && afterFailure.enabled == before.enabled
        && afterFailure.bypassed == before.bypassed
        && afterFailure.auto_clip_protection == before.auto_clip_protection
        && afterFailure.sample_rate == before.sample_rate
        && afterFailure.active == before.active
        && afterFailure.protection_db == before.protection_db;

    const bool nextSubmissionSucceeds =
        engine.set_equalizer(rejected, 43U) == AG_OK
        && engine.equalizer_status().revision == 43U
        && !engine.equalizer_status().enabled
        && engine.equalizer_status().bypassed
        && !engine.equalizer_status().auto_clip_protection;
    if (!failurePreservedState) {
        std::fprintf(stderr,
                     "failed EQ submission changed published engine state\n");
    }
    if (!nextSubmissionSucceeds) {
        std::fprintf(stderr, "EQ failure seam was not one-shot\n");
    }
    return failurePreservedState && nextSubmissionSucceeds;
}

} // namespace

int main(const int argc, char** argv)
{
#ifdef _WIN32
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
    assert(argc == 3);

    ag_player_config config{};
    config.backend = AG_AUDIO_BACKEND_NULL;
    config.buffer_frames = 4'096U;
    ag_playback_snapshot snapshot{};

    ag_player* player = reinterpret_cast<ag_player*>(1);
    assert(ag_player_create_with_config(nullptr, &player) == AG_INVALID_ARGUMENT);
    assert(player == nullptr);
    config.backend = static_cast<ag_audio_backend>(99);
    assert(ag_player_create_with_config(&config, &player) == AG_INVALID_ARGUMENT);
    assert(player == nullptr);
    config.backend = AG_AUDIO_BACKEND_NULL;
    assert(ag_player_create_with_config(&config, &player) == AG_OK);
    assert(player != nullptr);
    assert(ag_player_load(nullptr, argv[1]) == AG_INVALID_ARGUMENT);
    assert(ag_player_play(nullptr) == AG_INVALID_ARGUMENT);
    assert(ag_player_pause(nullptr) == AG_INVALID_ARGUMENT);
    assert(ag_player_stop(nullptr) == AG_INVALID_ARGUMENT);
    assert(ag_player_seek(nullptr, 0) == AG_INVALID_ARGUMENT);
    assert(ag_player_set_volume(nullptr, 0.5F) == AG_INVALID_ARGUMENT);
    assert(ag_player_set_replay_gain(nullptr, 0.0F, 1.0F, 1)
           == AG_INVALID_ARGUMENT);
    ag_equalizer_settings equalizer{};
    equalizer.revision = 7U;
    equalizer.enabled = 1;
    equalizer.auto_clip_protection = 1;
    equalizer.q = 1.414;
    equalizer.transition_ms = 25.0;
    assert(ag_player_set_equalizer(nullptr, &equalizer)
           == AG_INVALID_ARGUMENT);
    assert(ag_player_set_equalizer(player, nullptr)
           == AG_INVALID_ARGUMENT);
    assert(ag_player_equalizer_status(nullptr, nullptr)
           == AG_INVALID_ARGUMENT);
    ag_equalizer_status null_status{};
    assert(ag_player_equalizer_status(nullptr, &null_status)
           == AG_INVALID_ARGUMENT);
    assert(ag_player_equalizer_status(player, nullptr)
           == AG_INVALID_ARGUMENT);
    assert(ag_player_set_muted(nullptr, 0) == AG_INVALID_ARGUMENT);
    ag_playback_time_pitch_config time_pitch{1.0, 1};
    assert(ag_player_get_time_pitch(nullptr, &time_pitch)
           == AG_INVALID_ARGUMENT);
    assert(ag_player_get_time_pitch(player, nullptr)
           == AG_INVALID_ARGUMENT);
    assert(ag_player_set_time_pitch(nullptr, &time_pitch)
           == AG_INVALID_ARGUMENT);
    assert(ag_player_set_time_pitch(player, nullptr)
           == AG_INVALID_ARGUMENT);
    time_pitch.speed_ratio = 0.74;
    assert(ag_player_set_time_pitch(player, &time_pitch)
           == AG_INVALID_ARGUMENT);
    time_pitch.speed_ratio = 1.51;
    assert(ag_player_set_time_pitch(player, &time_pitch)
           == AG_INVALID_ARGUMENT);
    time_pitch.speed_ratio = std::numeric_limits<double>::quiet_NaN();
    assert(ag_player_set_time_pitch(player, &time_pitch)
           == AG_INVALID_ARGUMENT);
    time_pitch.speed_ratio = std::numeric_limits<double>::infinity();
    assert(ag_player_set_time_pitch(player, &time_pitch)
           == AG_INVALID_ARGUMENT);
    time_pitch = {1.0, 2};
    assert(ag_player_set_time_pitch(player, &time_pitch)
           == AG_INVALID_ARGUMENT);
    time_pitch = {1.25, 1};
    assert(ag_player_set_time_pitch(player, &time_pitch) == AG_OK);
    time_pitch = {};
    assert(ag_player_get_time_pitch(player, &time_pitch) == AG_OK);
    assert(std::abs(time_pitch.speed_ratio - 1.25) < 0.000001);
    assert(time_pitch.keep_pitch == 1);
    time_pitch = {1.0, 1};
    assert(ag_player_set_time_pitch(player, &time_pitch) == AG_OK);
    assert(ag_player_snapshot(nullptr, &snapshot) == AG_INVALID_ARGUMENT);

    const std::filesystem::path fixture_path = argv[1];
    const std::filesystem::path video_only_path = argv[2];
    const std::filesystem::path missing_path =
        fixture_path.parent_path() / "missing-audio-engine.wav";
    std::filesystem::remove(missing_path);
    const std::string missing_filename = missing_path.string();
    assert(ag_player_load(player, missing_filename.c_str()) == AG_IO_ERROR);
    assert(ag_player_snapshot(player, &snapshot) == AG_OK);
    assert(snapshot.state == AG_ERROR);
    assert(ag_player_play(player) != AG_OK);
    assert(ag_player_pause(player) != AG_OK);
    assert(ag_player_stop(player) != AG_OK);
    assert(ag_player_seek(player, 0) != AG_OK);
    assert(ag_player_snapshot(player, &snapshot) == AG_OK);
    assert(snapshot.state == AG_ERROR);
    assert(ag_player_load(player, video_only_path.string().c_str()) == AG_OK);
    assert(ag_player_snapshot(player, &snapshot) == AG_OK);
    assert(snapshot.state == AG_STOPPED);
    assert(snapshot.duration_ms > 0);
    const long long video_seek_target = snapshot.duration_ms / 2;
    assert(video_seek_target > 0);
    assert(ag_player_play(player) == AG_OK);
    assert(ag_player_seek(player, video_seek_target) == AG_OK);
    assert(ag_player_snapshot(player, &snapshot) == AG_OK);
    assert(snapshot.state == AG_PLAYING);
    assert(snapshot.position_ms >= video_seek_target);
    assert(snapshot.position_ms <= snapshot.duration_ms);
    assert(ag_player_stop(player) == AG_OK);
    assert(ag_player_load(player, argv[1]) == AG_OK);

    snapshot = {};
    assert(ag_player_snapshot(player, &snapshot) == AG_OK);
    assert(snapshot.state == AG_STOPPED);
    assert(snapshot.duration_ms >= 1'990);
    assert(snapshot.position_ms == 0);

    ag_equalizer_status meter_status{};
    assert(ag_player_equalizer_status(player, &meter_status) == AG_OK);
    assert(meter_status.output_peak_db == kMeterFloorDb);

    assert(ag_player_set_volume(player, 1.0F) == AG_OK);
    assert(ag_player_set_replay_gain(player, 0.0F, 1.0F, 1) == AG_OK);
    assert(ag_player_set_muted(player, 0) == AG_OK);
    assert(ag_player_play(player) == AG_OK);
    const double full_volume_peak = waitForOutputPeak(
        player,
        [](const double peakDb) {
            return std::isfinite(peakDb) && peakDb > -100.0;
        },
        std::chrono::milliseconds(1'000));
    assert(ag_player_set_volume(player, 0.1F) == AG_OK);
    const double reduced_volume_peak = waitForOutputPeak(
        player,
        [full_volume_peak](const double peakDb) {
            return std::isfinite(peakDb)
                   && peakDb > kMeterFloorDb
                   && peakDb <= full_volume_peak - 6.0;
        },
        std::chrono::milliseconds(1'000));
    assert(reduced_volume_peak < full_volume_peak);
    assert(ag_player_set_muted(player, 1) == AG_OK);
    assert(waitForOutputPeak(
               player,
               [](const double peakDb) { return peakDb == kMeterFloorDb; },
               std::chrono::milliseconds(250))
           == kMeterFloorDb);
    assert(ag_player_set_muted(player, 0) == AG_OK);
    assert(ag_player_set_volume(player, 1.0F) == AG_OK);

    assert(ag_player_play(player) == AG_OK);
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    assert(ag_player_snapshot(player, &snapshot) == AG_OK);
    assert(snapshot.state == AG_PLAYING);
    assert(snapshot.position_ms > 0);

    assert(ag_player_pause(player) == AG_OK);
    assert(ag_player_snapshot(player, &snapshot) == AG_OK);
    const long long paused_position = snapshot.position_ms;
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    assert(ag_player_snapshot(player, &snapshot) == AG_OK);
    assert(snapshot.state == AG_PAUSED);
    assert(snapshot.position_ms == paused_position);

    assert(ag_player_seek(player, 1'000) == AG_OK);
    assert(ag_player_set_volume(player, 0.25F) == AG_OK);
    assert(ag_player_set_replay_gain(player, -3.0F, 0.8F, 1) == AG_OK);
    equalizer.band_gain_db[5] = 6.0;
    equalizer.preamp_db = -1.5;
    assert(ag_player_set_equalizer(player, &equalizer) == AG_OK);
    ag_equalizer_status equalizer_status{};
    assert(ag_player_equalizer_status(player, &equalizer_status) == AG_OK);
    assert(equalizer_status.revision == equalizer.revision);
    assert(equalizer_status.enabled == 1);
    assert(equalizer_status.auto_clip_protection == 1);
    assert(equalizer_status.protection_db <= 0.0);
    assert(ag_player_set_muted(player, 1) == AG_OK);
    assert(ag_player_snapshot(player, &snapshot) == AG_OK);
    assert(snapshot.state == AG_PAUSED);
    assert(snapshot.position_ms == 1'000);
    assert(std::abs(snapshot.volume - 0.25F) < 0.001F);
    assert(snapshot.muted == 1);

    assert(ag_player_set_volume(player, -0.01F) == AG_INVALID_ARGUMENT);
    assert(ag_player_set_volume(player, 1.01F) == AG_INVALID_ARGUMENT);
    assert(ag_player_set_replay_gain(player, NAN, 1.0F, 1)
           == AG_INVALID_ARGUMENT);
    assert(ag_player_set_replay_gain(player, 0.0F, -1.0F, 1)
           == AG_INVALID_ARGUMENT);
    equalizer.band_gain_db[5] = 18.1;
    assert(ag_player_set_equalizer(player, &equalizer)
           == AG_INVALID_ARGUMENT);
    equalizer.band_gain_db[5] = 0.0;
    equalizer.enabled = 2;
    assert(ag_player_set_equalizer(player, &equalizer)
           == AG_INVALID_ARGUMENT);
    assert(ag_player_set_muted(player, 2) == AG_INVALID_ARGUMENT);
    assert(ag_player_seek(player, snapshot.duration_ms + 1) == AG_INVALID_ARGUMENT);
    assert(ag_player_stop(player) == AG_OK);
    assert(ag_player_snapshot(player, &snapshot) == AG_OK);
    assert(snapshot.state == AG_STOPPED);
    assert(snapshot.position_ms == 0);
    assert(ag_player_equalizer_status(player, &meter_status) == AG_OK);
    assert(meter_status.output_peak_db == kMeterFloorDb);

    assert(ag_player_seek(player, 1'900) == AG_OK);
    assert(ag_player_play(player) == AG_OK);
    assert(waitForPlayerState(player, AG_STOPPED, snapshot,
                              std::chrono::milliseconds(2'000)));

    assert(ag_player_seek(player, 500) == AG_OK);
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    assert(ag_player_snapshot(player, &snapshot) == AG_OK);
    assert(snapshot.state == AG_STOPPED);
    assert(snapshot.position_ms == 500);

    assert(ag_player_play(player) == AG_OK);
    const auto resume_deadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(2);
    do {
        assert(ag_player_snapshot(player, &snapshot) == AG_OK);
        if (snapshot.state == AG_PLAYING && snapshot.position_ms > 500) {
            break;
        }
        std::this_thread::yield();
    } while (std::chrono::steady_clock::now() < resume_deadline);
    if (snapshot.state != AG_PLAYING || snapshot.position_ms <= 500) {
        std::cerr << "resume readiness timeout: state=" << snapshot.state
                  << " position_ms=" << snapshot.position_ms
                  << " duration_ms=" << snapshot.duration_ms << '\n';
    }
    assert(snapshot.state == AG_PLAYING);
    assert(snapshot.position_ms > 500);

    time_pitch = {1.25, 0};
    assert(ag_player_set_time_pitch(player, &time_pitch) == AG_OK);
    assert(agplayer::editor::load_editor_playback_stream(
               player, std::make_shared<SilentEditorStream>(2'000)) == AG_OK);
    assert(ag_player_get_time_pitch(player, &time_pitch) == AG_OK);
    assert(time_pitch.speed_ratio == 1.0);
    assert(time_pitch.keep_pitch == 0);
    time_pitch = {1.25, 1};
    assert(ag_player_set_time_pitch(player, &time_pitch)
           == AG_INVALID_ARGUMENT);
    assert(ag_player_play(player) == AG_OK);
    const auto editor_play_deadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(2);
    do {
        assert(ag_player_snapshot(player, &snapshot) == AG_OK);
        if (snapshot.state == AG_PLAYING && snapshot.position_ms > 0) break;
        std::this_thread::yield();
    } while (std::chrono::steady_clock::now() < editor_play_deadline);
    if (snapshot.state != AG_PLAYING || snapshot.position_ms <= 0) {
        std::cerr << "editor play readiness timeout: state=" << snapshot.state
                  << " position_ms=" << snapshot.position_ms
                  << " duration_ms=" << snapshot.duration_ms << '\n';
    }
    assert(snapshot.state == AG_PLAYING);
    assert(snapshot.position_ms > 0);
    assert(agplayer::editor::replace_editor_playback_stream(
               player, std::make_shared<SilentEditorStream>(3'000)) == AG_OK);
    assert(ag_player_snapshot(player, &snapshot) == AG_OK);
    assert(snapshot.state == AG_STOPPED);
    assert(snapshot.duration_ms == 3'000);
    assert(ag_player_seek(player, 500) == AG_OK);
    assert(ag_player_play(player) == AG_OK);
    const auto replacement_play_deadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(2);
    do {
        assert(ag_player_snapshot(player, &snapshot) == AG_OK);
        if (snapshot.state == AG_PLAYING && snapshot.position_ms >= 500) break;
        std::this_thread::yield();
    } while (std::chrono::steady_clock::now()
             < replacement_play_deadline);
    if (snapshot.state != AG_PLAYING || snapshot.position_ms < 500) {
        std::cerr << "replacement play readiness timeout: state="
                  << snapshot.state
                  << " position_ms=" << snapshot.position_ms
                  << " duration_ms=" << snapshot.duration_ms << '\n';
    }
    assert(snapshot.state == AG_PLAYING);
    assert(snapshot.position_ms >= 500);

    {
        auto stream = std::make_shared<ObservedRampStream>();
        agplayer::AudioEngine engine(agplayer::AudioBackend::Manual, 4'096U);
        assert(engine.load_stream(stream) == AG_OK);
        assert(engine.play() == AG_OK);
        assert(waitForBufferedFrames(engine, 2'048U,
                                     std::chrono::milliseconds(2'000)));

        TimelineBarrier barrier;
        barrier.blockNext(false);
        agplayer::AudioEngineTestAccess::setTimelineHook(
            engine, &TimelineBarrier::hook, &barrier);
        std::thread controlWriter([&] {
            assert(engine.set_mode(agplayer::PlaybackMode::RepeatOne) == AG_OK);
        });
        barrier.waitUntilEntered();

        const std::size_t bufferedBefore = engine.buffered_frames();
        std::vector<float> output(256U, 1.0F);
        engine.render(output.data(), output.size());
        assert(std::all_of(output.begin(), output.end(), [](const float sample) {
            return sample == 0.0F;
        }));
        assert(engine.buffered_frames() == bufferedBefore);
        barrier.release();
        controlWriter.join();

        barrier.blockNext(true);
        std::thread callback([&] {
            output.assign(256U, 0.0F);
            engine.render(output.data(), output.size());
        });
        barrier.waitUntilEntered();
        std::atomic<bool> seekDone{false};
        std::thread fastSeek([&] {
            assert(engine.seek(1'000) == AG_OK);
            seekDone.store(true, std::memory_order_release);
        });
        stream->waitForSeek();
        assert(!seekDone.load(std::memory_order_acquire));
        barrier.release();
        callback.join();
        fastSeek.join();
        assert(seekDone.load(std::memory_order_acquire));

        assert(waitForBufferedFrames(engine, 512U,
                                     std::chrono::milliseconds(2'000)));
        barrier.blockNext(false);
        std::thread deviceWriter([&] { engine.simulate_device_loss(); });
        barrier.waitUntilEntered();
        const std::size_t bufferedAtDeviceLoss = engine.buffered_frames();
        output.assign(256U, 1.0F);
        engine.render(output.data(), output.size());
        assert(std::all_of(output.begin(), output.end(), [](const float sample) {
            return sample == 0.0F;
        }));
        assert(engine.buffered_frames() == bufferedAtDeviceLoss);
        barrier.release();
        deviceWriter.join();
        agplayer::AudioEngineTestAccess::setTimelineHook(engine, nullptr,
                                                         nullptr);
        assert(engine.retry_device() == AG_OK);
    }

    ag_player_destroy(player);

    const bool boundaryRegression =
        outputMeterInvalidatesAcrossPlaybackBoundaries();
    const bool deviceSwitchRegression =
        outputDeviceSwitchRejectsInFlightOldCallback();
    const bool deviceStopFailureRegression =
        outputDeviceStopFailureRejectsInFlightOldCallback();
    const bool deviceSnapshotExceptionRegression =
        outputDeviceSnapshotExceptionRejectsInFlightOldCallback();
    const bool partialUnderrunRegression =
        outputMeterUsesRequestedFramesForPartialUnderrun();
    const bool equalizerSubmitFailureRegression =
        equalizerSubmitFailureKeepsPublishedState();
    assert(boundaryRegression);
    assert(deviceSwitchRegression);
    assert(deviceStopFailureRegression);
    assert(deviceSnapshotExceptionRegression);
    assert(partialUnderrunRegression);
    assert(equalizerSubmitFailureRegression);
}
