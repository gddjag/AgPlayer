#pragma once

#include "playback_session.hpp"

#include <agplayer/c_api.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace agplayer {

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

class AudioEngine final {
public:
    AudioEngine(AudioBackend backend, std::size_t buffer_frames);
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    ag_result load(const std::string& utf8_path) noexcept;
    ag_result set_queue(std::vector<std::string> utf8_paths,
                        std::size_t start_index) noexcept;
    ag_result play() noexcept;
    ag_result pause() noexcept;
    ag_result stop() noexcept;
    ag_result seek(std::int64_t position_ms) noexcept;
    ag_result next() noexcept;
    ag_result previous() noexcept;
    ag_result set_mode(PlaybackMode mode) noexcept;
    ag_result set_volume(float volume) noexcept;
    void set_muted(bool muted) noexcept;
    [[nodiscard]] EngineSnapshot snapshot() const noexcept;
    void render(float* output, std::size_t requested_frames) noexcept;
    [[nodiscard]] std::size_t buffered_frames() const noexcept;
    [[nodiscard]] bool device_lost() const noexcept;
    ag_result retry_device() noexcept;
    void simulate_device_loss() noexcept;
    [[nodiscard]] std::vector<OutputDevice> output_devices() noexcept;
    ag_result set_output_device(std::string utf8_id,
                                bool exclusive) noexcept;
    [[nodiscard]] bool exclusive_mode_active() const noexcept;
    ag_result set_transition_fade_ms(int milliseconds) noexcept;
    ag_result set_match_track_sample_rate(bool enabled) noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace agplayer
