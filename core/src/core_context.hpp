#pragma once

#include "audio_engine.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace agplayer {

class CoreContext final {
public:
    CoreContext(AudioBackend backend, std::size_t buffer_frames);

    [[nodiscard]] const std::string& last_error() const noexcept;
    void set_error(std::string value) noexcept;
    ag_result load(const std::string& utf8_path) noexcept;
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
    ag_result set_replay_gain(float gain_db, float peak,
                              bool clip_protection) noexcept;
    ag_result set_equalizer(const GraphicEqSettings& settings,
                            std::uint64_t revision) noexcept;
    [[nodiscard]] EqualizerStatus equalizer_status() const noexcept;
    void set_muted(bool muted) noexcept;
    [[nodiscard]] EngineSnapshot snapshot() const noexcept;
    ag_result spectrum(float* bins, std::size_t bin_count) noexcept;
    [[nodiscard]] bool device_lost() const noexcept;
    ag_result retry_device() noexcept;
    void simulate_device_loss() noexcept;
    [[nodiscard]] std::vector<OutputDevice> output_devices() noexcept;
    ag_result set_output_device(std::string utf8_id,
                                bool exclusive) noexcept;
    [[nodiscard]] bool exclusive_mode_active() const noexcept;
    ag_result set_transition_fade_ms(int milliseconds) noexcept;
    ag_result set_duration_ms(std::int64_t duration_ms) noexcept;
    ag_result set_match_track_sample_rate(bool enabled) noexcept;

private:
    ag_result record(ag_result result, const char* operation) noexcept;

    AudioEngine audio_engine_;
    std::string last_error_;
};

} // namespace agplayer
