#pragma once

#include "audio_document.hpp"

#include <atomic>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace agplayer::editor {

struct TimePitchResult final {
    bool success{};
    std::string message;
    AudioSource source;
};

class TimePitchSession final {
public:
    void setOriginalBpm(double bpm) noexcept;
    [[nodiscard]] bool setTargetBpm(double bpm) noexcept;
    [[nodiscard]] bool setSpeedPercent(double percent) noexcept;
    void setKeepPitch(bool value) noexcept { keep_pitch_ = value; }
    void setFormantPreservation(bool value) noexcept
    { formant_preservation_ = value; }
    [[nodiscard]] bool setPitch(int semitones, int cents) noexcept;

    [[nodiscard]] double originalBpm() const noexcept { return original_bpm_; }
    [[nodiscard]] double targetBpm() const noexcept { return target_bpm_; }
    [[nodiscard]] double speedPercent() const noexcept { return speed_percent_; }
    [[nodiscard]] bool keepPitch() const noexcept { return keep_pitch_; }
    [[nodiscard]] bool formantPreservation() const noexcept
    { return formant_preservation_; }
    [[nodiscard]] int pitchCents() const noexcept { return pitch_cents_; }

    [[nodiscard]] TimePitchResult process(
        const TimelineSnapshot& snapshot,
        const std::filesystem::path& output,
        std::optional<Selection> range = std::nullopt,
        const std::atomic_bool* cancelled = nullptr,
        std::function<void(float)> progress = {}) const;

private:
    double original_bpm_{};
    double target_bpm_{};
    double speed_percent_{100.0};
    bool keep_pitch_{true};
    bool formant_preservation_{};
    int pitch_cents_{};
};

} // namespace agplayer::editor
