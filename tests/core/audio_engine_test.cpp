// Test files deliberately keep assert() active even in Release builds: many
// test cases embed function calls with side effects inside assert(), and
// silencing them under NDEBUG would skip those calls and crash on cleanup.
#undef NDEBUG

#include <agplayer/c_api.h>

#include "../../core/src/audio_engine.hpp"
#include "../../core/src/audio_editor/editor_player_bridge.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <crtdbg.h>
#endif

namespace {

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
    std::vector<float> output(64U * 2U);
    engine.render(output.data(), 64U);
    if (engine.equalizer_status().output_peak_db <= kMeterFloorDb) {
        std::fprintf(stderr, "boundary regression setup did not publish a peak\n");
        return false;
    }

    assert(engine.pause() == AG_OK);
    assert(engine.play() == AG_OK);
    bool ok = engine.equalizer_status().output_peak_db == kMeterFloorDb;

    assert(waitForBufferedFrames(engine, 64U,
                                 std::chrono::milliseconds(1'000)));
    engine.render(output.data(), 64U);
    engine.set_muted(true);
    engine.set_muted(false);
    ok = ok && engine.equalizer_status().output_peak_db == kMeterFloorDb;

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

} // namespace

int main(const int argc, char** argv)
{
#ifdef _WIN32
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
    assert(argc == 2);

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
    assert(ag_player_snapshot(nullptr, &snapshot) == AG_INVALID_ARGUMENT);

    const std::filesystem::path fixture_path = argv[1];
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
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    assert(ag_player_snapshot(player, &snapshot) == AG_OK);
    assert(snapshot.state == AG_STOPPED);

    assert(ag_player_seek(player, 500) == AG_OK);
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    assert(ag_player_snapshot(player, &snapshot) == AG_OK);
    assert(snapshot.state == AG_STOPPED);
    assert(snapshot.position_ms == 500);

    assert(ag_player_play(player) == AG_OK);
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    assert(ag_player_snapshot(player, &snapshot) == AG_OK);
    assert(snapshot.state == AG_PLAYING);
    assert(snapshot.position_ms > 500);

    assert(agplayer::editor::load_editor_playback_stream(
               player, std::make_shared<SilentEditorStream>(2'000)) == AG_OK);
    assert(ag_player_play(player) == AG_OK);
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    assert(agplayer::editor::replace_editor_playback_stream(
               player, std::make_shared<SilentEditorStream>(3'000)) == AG_OK);
    assert(ag_player_snapshot(player, &snapshot) == AG_OK);
    assert(snapshot.state == AG_STOPPED);
    assert(snapshot.duration_ms == 3'000);
    assert(ag_player_seek(player, 500) == AG_OK);
    assert(ag_player_play(player) == AG_OK);
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    assert(ag_player_snapshot(player, &snapshot) == AG_OK);
    assert(snapshot.state == AG_PLAYING);
    assert(snapshot.position_ms >= 500);

    ag_player_destroy(player);

    const bool boundaryRegression =
        outputMeterInvalidatesAcrossPlaybackBoundaries();
    const bool partialUnderrunRegression =
        outputMeterUsesRequestedFramesForPartialUnderrun();
    assert(boundaryRegression);
    assert(partialUnderrunRegression);
}
