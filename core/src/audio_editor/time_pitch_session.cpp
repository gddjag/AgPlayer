#include "time_pitch_session.hpp"

#include "audio_file_analyzer.hpp"
#include "document_renderer.hpp"
#include "../pitch_shifter.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <system_error>

namespace agplayer::editor {
namespace {

bool valid_bpm(const double value) noexcept
{
    return std::isfinite(value) && value >= 20.0 && value <= 400.0;
}

std::filesystem::path render_path_for(const std::filesystem::path& output)
{
    const auto ticks = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    return output.parent_path() / std::filesystem::u8path(
        output.filename().u8string() + ".agplayer-time-pitch-"
        + std::to_string(ticks) + ".wav");
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
    if (snapshot.events.empty() || output.empty()) {
        return {false, "invalid time/pitch request", {}};
    }
    const auto rendered_path = render_path_for(output);
    const auto cleanup = [&] {
        std::error_code ignored;
        std::filesystem::remove(rendered_path, ignored);
        if (cancelled != nullptr && cancelled->load()) {
            std::filesystem::remove(output, ignored);
        }
    };
    const RenderResult rendered = DocumentRenderer{}.renderFloatWav(
        snapshot, range, rendered_path, cancelled,
        progress ? [progress](const float value) { progress(value * 0.30F); }
                 : std::function<void(float)>{});
    if (!rendered.success) {
        cleanup();
        return {false, rendered.message, {}};
    }

    const double speed_ratio = speed_percent_ / 100.0;
    const int speed_pitch = keep_pitch_ ? 0 : static_cast<int>(std::lround(
        1'200.0 * std::log2(speed_ratio)));
    const int effective_pitch = std::clamp(
        pitch_cents_ + speed_pitch, -1'200, 1'200);
    const bool keep_tempo = keep_pitch_;
    const double pitch_rate = std::pow(2.0, effective_pitch / 1'200.0);

    agplayer::PitchShiftConfig config;
    config.output_path = output.u8string();
    config.output_codec_name = "pcm_f32le";
    config.output_sample_rate = static_cast<int>(rendered.sample_rate);
    config.pitch_cents = effective_pitch;
    config.keep_tempo = keep_tempo;
    config.tempo_ratio = keep_tempo ? speed_ratio : speed_ratio / pitch_rate;
    config.vocal_protection = formant_preservation_;
    std::string error;
    const ag_result status = agplayer::pitch_shift(
        rendered_path.u8string(), config, cancelled,
        progress ? [progress](const float value) {
            progress(0.30F + value * 0.70F);
        } : std::function<void(float)>{}, error);
    std::error_code ignored;
    std::filesystem::remove(rendered_path, ignored);
    if (status != AG_OK) {
        std::filesystem::remove(output, ignored);
        return {false, error.empty() ? "time/pitch processing failed" : error, {}};
    }

    const AudioFileAnalysis analysis = AudioFileAnalyzer::analyze(output, 64);
    if (!analysis.success) {
        std::filesystem::remove(output, ignored);
        return {false, analysis.message, {}};
    }
    if (progress) progress(1.0F);
    return {true, {}, analysis.source};
}

} // namespace agplayer::editor
