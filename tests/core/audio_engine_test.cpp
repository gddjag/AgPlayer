// Test files deliberately keep assert() active even in Release builds: many
// test cases embed function calls with side effects inside assert(), and
// silencing them under NDEBUG would skip those calls and crash on cleanup.
#undef NDEBUG

#include <agplayer/c_api.h>

#include "../../core/src/audio_editor/editor_player_bridge.hpp"
#include "../../core/src/audio_engine.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <condition_variable>
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

void waitForBufferedFrames(agplayer::AudioEngine& engine,
                           const std::size_t minimum)
{
    for (int attempt = 0; attempt < 20'000; ++attempt) {
        if (engine.buffered_frames() >= minimum) return;
        std::this_thread::yield();
    }
    assert(false && "decode thread did not prepare PCM");
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
    equalizer.band_gain_db[5] = 12.1;
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
        waitForBufferedFrames(engine, 2'048U);

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

        waitForBufferedFrames(engine, 512U);
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
}
