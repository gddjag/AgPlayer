#include "time_pitch_session.hpp"

#include "document_renderer.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace agplayer::editor {
namespace {

bool valid_bpm(const double value) noexcept
{
    return std::isfinite(value) && value >= 20.0 && value <= 400.0;
}

} // namespace

void TimePitchSession::setOriginalBpm(const double bpm) noexcept
{
    if (!valid_bpm(bpm)) {
        original_bpm_ = 0.0;
        target_bpm_ = 0.0;
        speed_percent_ = 100.0;
        return;
    }
    original_bpm_ = bpm;
    target_bpm_ = bpm;
    speed_percent_ = 100.0;
}

bool TimePitchSession::setTargetBpm(const double bpm) noexcept
{
    if (!valid_bpm(bpm)) return false;
    if (original_bpm_ <= 0.0) {
        original_bpm_ = bpm;
        target_bpm_ = bpm;
        speed_percent_ = 100.0;
        return true;
    }
    const double speed = bpm / original_bpm_ * 100.0;
    if (speed < 50.0 || speed > 200.0) return false;
    if (std::abs(target_bpm_ - bpm) < 0.000001
        && std::abs(speed_percent_ - speed) < 0.000001) {
        return false;
    }
    target_bpm_ = bpm;
    speed_percent_ = speed;
    return true;
}

bool TimePitchSession::setSpeedPercent(const double percent) noexcept
{
    if (!std::isfinite(percent) || percent < 50.0 || percent > 200.0) {
        return false;
    }
    if (std::abs(speed_percent_ - percent) < 0.000001) return false;
    speed_percent_ = percent;
    target_bpm_ = original_bpm_ > 0.0
        ? original_bpm_ * percent / 100.0 : 0.0;
    return true;
}

bool TimePitchSession::setPitch(const int semitones, const int cents) noexcept
{
    const int total = semitones * 100 + cents;
    if (semitones < -12 || semitones > 12 || cents < -99 || cents > 99
        || total < -1'200 || total > 1'200) {
        return false;
    }
    if (pitch_cents_ == total) return false;
    pitch_cents_ = total;
    return true;
}

TimePitchResult TimePitchSession::process(
    const TimelineSnapshot& snapshot,
    const std::filesystem::path& output,
    const std::optional<Selection> range,
    const std::atomic_bool* cancelled,
    std::function<void(float)> progress) const
{
    EditorPlaybackParameters parameters;
    parameters.speed_ratio = speed_percent_ / 100.0;
    parameters.keep_pitch = keep_pitch_;
    parameters.pitch_cents = pitch_cents_;
    parameters.formant_preservation = formant_preservation_;
    const auto rendered = DocumentRenderer{}.renderFloatWav(
        snapshot, range, output, cancelled, std::move(progress), parameters);
    if (!rendered.success) return {false, rendered.message, {}};
    return {true, {}, AudioSource{output, rendered.sample_rate,
        rendered.channels, rendered.frames}};
}

} // namespace agplayer::editor
