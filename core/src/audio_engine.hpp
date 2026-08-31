#pragma once

#include "playback_session.hpp"
#include "graphic_equalizer.hpp"

#include <agplayer/c_api.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace agplayer {

class IAudioStreamSource;

enum class AudioBackend {
    Default,
    Null,
    Manual,
};

enum class EngineState {
    Stopped,
    Loading,
    Playing,
    Paused,
    Error,
};

struct EngineSnapshot {
    EngineState state = EngineState::Stopped;
    std::int64_t position_ms = 0;
    std::int64_t duration_ms = 0;
    int sample_rate = 0;
    float volume = 1.0F;
    bool muted = false;
    std::size_t track_index = 0U;
    std::size_t track_count = 0U;
    PlaybackMode mode = PlaybackMode::Sequential;
};

struct OutputDevice {
    std::string id;
    std::string name;
};

struct EqualizerStatus {
    std::uint64_t revision = 0;
    bool enabled = true;
    bool bypassed = false;
    bool auto_clip_protection = true;
    int sample_rate = 0;
    bool active = false;
    double protection_db = 0.0;
    double output_peak_db = -120.0;
};

enum class OutputDeviceSwitchTestFailure {
    None,
    StopOutput,
    BeforeStateSnapshot,
};

// Test-only lock-free seam for deterministically holding callbacks and
// injecting device-switch failures. Null in production.
struct OutputDeviceSwitchTestBarrier final {
    std::atomic<int> armed_phase{0};
    std::atomic<int> entered_phase{0};
    std::atomic<int> release_phase{0};
    std::atomic<bool> cancelled{false};
    std::atomic<OutputDeviceSwitchTestFailure> failure{
        OutputDeviceSwitchTestFailure::None};
};

class AudioEngine final {
public:
    AudioEngine(AudioBackend backend, std::size_t buffer_frames);
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    ag_result load(const std::string& utf8_path) noexcept;
    ag_result load_stream(std::shared_ptr<IAudioStreamSource> stream) noexcept;
    ag_result replace_stream(std::shared_ptr<IAudioStreamSource> stream) noexcept;
    ag_result set_queue(std::vector<std::string> utf8_paths,
                        std::size_t start_index) noexcept;
    ag_result set_scoped_queue(std::vector<std::string> utf8_paths,
                               std::size_t start_index,
                               std::size_t scope_size,
                               bool allow_fallback) noexcept;
    ag_result queue_next(std::string utf8_path) noexcept;
    ag_result play() noexcept;
    ag_result pause() noexcept;
    ag_result stop() noexcept;
    ag_result seek(std::int64_t position_ms) noexcept;
    ag_result next() noexcept;
    ag_result previous() noexcept;
    ag_result set_mode(PlaybackMode mode) noexcept;
    ag_result set_volume(float volume) noexcept;
    ag_result set_replay_gain(float gain_db, float peak, bool clip_protection) noexcept;
    ag_result set_equalizer(const GraphicEqSettings& settings,
                            std::uint64_t revision) noexcept;
    [[nodiscard]] EqualizerStatus equalizer_status() const noexcept;
    void set_muted(bool muted) noexcept;
    [[nodiscard]] EngineSnapshot snapshot() const noexcept;
    void render(float* output, std::size_t requested_frames) noexcept;
    ag_result spectrum(float* bins, std::size_t bin_count) noexcept;
    [[nodiscard]] std::size_t buffered_frames() const noexcept;
    [[nodiscard]] bool device_lost() const noexcept;
    ag_result retry_device() noexcept;
    void simulate_device_loss() noexcept;
    [[nodiscard]] std::vector<OutputDevice> output_devices() noexcept;
    ag_result set_output_device(std::string utf8_id,
                                bool exclusive) noexcept;
    // Installs no production behavior unless a test explicitly supplies it.
    void set_output_device_switch_test_barrier(
        OutputDeviceSwitchTestBarrier* barrier) noexcept;
    // One-shot test seam; has no effect unless a test explicitly arms it.
    void fail_next_equalizer_submit_for_test() noexcept;
    [[nodiscard]] bool exclusive_mode_active() const noexcept;
    ag_result set_transition_fade_ms(int milliseconds) noexcept;
    ag_result set_duration_ms(std::int64_t duration_ms) noexcept;
    ag_result set_match_track_sample_rate(bool enabled) noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace agplayer
