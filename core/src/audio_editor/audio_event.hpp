#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <limits>
#include <vector>

namespace agplayer::editor {

using SampleFrame = std::int64_t;
using EventId = std::uint64_t;

struct AudioSource final {
    std::filesystem::path path;
    std::uint32_t sample_rate{};
    std::uint32_t channels{};
    SampleFrame total_frames{};
};

struct EnvelopePoint final {
    SampleFrame offset{};
    float gain{1.0F};
};

enum class FadeCurve { Linear, Smooth, Exponential };

inline constexpr std::size_t kMaxEnvelopePoints = 128;
inline constexpr std::size_t kTrackCount = 6;
inline constexpr std::size_t kMaxTimelineEvents = 4096;
inline constexpr std::size_t kMaxTimelineEnvelopePoints = 65536;

// Integer rational conversion, rounded to the nearest frame, without a wide
// integer extension (MSVC also supports this implementation).
[[nodiscard]] inline SampleFrame sourceToProjectFrames(SampleFrame frames,
    std::uint32_t sourceRate, std::uint32_t projectRate) noexcept
{
    if (!sourceRate || !projectRate || sourceRate == projectRate) return frames;
    if (frames < 0) return frames == (std::numeric_limits<SampleFrame>::min)()
        ? -sourceToProjectFrames((std::numeric_limits<SampleFrame>::max)(), sourceRate, projectRate)
        : -sourceToProjectFrames(-frames, sourceRate, projectRate);
    const auto whole = static_cast<std::uint64_t>(frames) / sourceRate;
    const auto fraction = (static_cast<std::uint64_t>(frames) % sourceRate
        * projectRate + sourceRate / 2) / sourceRate;
    const auto limit = static_cast<std::uint64_t>((std::numeric_limits<SampleFrame>::max)());
    if (whole > (limit - fraction) / projectRate) return (std::numeric_limits<SampleFrame>::max)();
    return static_cast<SampleFrame>(whole * projectRate + fraction);
}

[[nodiscard]] inline SampleFrame projectToSourceFrames(SampleFrame frames,
    std::uint32_t sourceRate, std::uint32_t projectRate) noexcept
{ return sourceToProjectFrames(frames, projectRate, sourceRate); }

struct AudioEvent final {
    EventId id{};
    std::shared_ptr<const AudioSource> source;
    SampleFrame sourceStart{}, sourceEnd{}, timelineStart{};
    float gain{1.0F};
    SampleFrame fadeIn{}, fadeOut{};
    double speedRatio{1.0};
    int pitchSemitone{};
    bool mute{};
    std::vector<EnvelopePoint> envelope{};
    FadeCurve fadeInCurve{FadeCurve::Smooth};
    FadeCurve fadeOutCurve{FadeCurve::Smooth};
    int trackIndex{};
    std::uint32_t timelineSampleRate{};
};

[[nodiscard]] inline SampleFrame projectOffsetAt(const AudioEvent& event,
    SampleFrame sourceFrame) noexcept
{
    const auto rate = event.source ? event.source->sample_rate : 0;
    return sourceToProjectFrames(sourceFrame, rate, event.timelineSampleRate)
        - sourceToProjectFrames(event.sourceStart, rate, event.timelineSampleRate);
}

[[nodiscard]] inline SampleFrame sourceOffsetAt(const AudioEvent& event,
    SampleFrame projectOffset) noexcept
{
    const auto rate = event.source ? event.source->sample_rate : 0;
    const auto start = sourceToProjectFrames(event.sourceStart, rate, event.timelineSampleRate);
    if (projectOffset > (std::numeric_limits<SampleFrame>::max)() - start)
        return event.sourceEnd - event.sourceStart;
    return projectToSourceFrames(start + projectOffset, rate, event.timelineSampleRate) - event.sourceStart;
}

[[nodiscard]] inline bool isSupportedFadeCurve(const FadeCurve curve) noexcept
{
    switch (curve) {
    case FadeCurve::Linear:
    case FadeCurve::Smooth:
    case FadeCurve::Exponential:
        return true;
    }
    return false;
}

[[nodiscard]] inline float fadeCurveGainAt(const FadeCurve curve,
                                            const double t) noexcept
{
    const double bounded = std::clamp(t, 0.0, 1.0);
    switch (curve) {
    case FadeCurve::Linear:
        return static_cast<float>(bounded);
    case FadeCurve::Smooth:
        return static_cast<float>(bounded * bounded
                                  * (3.0 - 2.0 * bounded));
    case FadeCurve::Exponential: {
        static const double denominator = std::expm1(5.0);
        return static_cast<float>(std::expm1(5.0 * bounded) / denominator);
    }
    }
    return 0.0F;
}

[[nodiscard]] inline SampleFrame audibleFrames(const AudioEvent& event) noexcept
{
    if (event.sourceStart < 0 || event.sourceEnd <= event.sourceStart) {
        return 0;
    }
    return projectOffsetAt(event, event.sourceEnd);
}

[[nodiscard]] inline float envelopeGainAt(
    const AudioEvent& event, const SampleFrame offset) noexcept
{
    if (event.envelope.empty()) return 1.0F;
    EnvelopePoint previous{0, 1.0F};
    for (const EnvelopePoint& point : event.envelope) {
        if (offset <= point.offset) {
            if (point.offset == previous.offset) return point.gain;
            const double fraction = static_cast<double>(offset - previous.offset)
                / static_cast<double>(point.offset - previous.offset);
            return static_cast<float>(previous.gain
                + (point.gain - previous.gain) * fraction);
        }
        previous = point;
    }
    return previous.gain;
}

[[nodiscard]] inline float fadeGainAt(
    const AudioEvent& event, const SampleFrame offset) noexcept
{
    const SampleFrame frames = audibleFrames(event);
    double result = 1.0;
    if (event.fadeIn > 0 && offset < event.fadeIn) {
        const double t = event.fadeIn == 1 ? 0.0
            : static_cast<double>(offset)
                / static_cast<double>(event.fadeIn - 1);
        result *= fadeCurveGainAt(event.fadeInCurve, t);
    }
    const SampleFrame fadeOutStart = frames - event.fadeOut;
    if (event.fadeOut > 0 && offset >= fadeOutStart) {
        const double t = event.fadeOut == 1 ? 0.0
            : static_cast<double>(frames - 1 - offset)
                / static_cast<double>(event.fadeOut - 1);
        result *= fadeCurveGainAt(event.fadeOutCurve, t);
    }
    return static_cast<float>(result);
}

[[nodiscard]] inline float eventAmplitudeGainAt(
    const AudioEvent& event, const SampleFrame offset) noexcept
{
    return event.mute ? 0.0F : (std::max)(0.0F,
        event.gain * fadeGainAt(event, offset)
            * envelopeGainAt(event, offset));
}

[[nodiscard]] inline bool isValid(const AudioEvent& event) noexcept
{
    if (event.trackIndex < 0 || event.trackIndex >= static_cast<int>(kTrackCount)
        || !event.source || event.source->total_frames <= 0
        || event.sourceStart < 0 || event.sourceEnd <= event.sourceStart
        || event.sourceEnd > event.source->total_frames
        || event.timelineStart < 0 || !std::isfinite(event.gain)
        || !std::isfinite(event.speedRatio) || event.speedRatio <= 0.0) {
        return false;
    }

    const SampleFrame frames = audibleFrames(event);
    if (frames <= 0 || event.fadeIn < 0 || event.fadeOut < 0 || event.fadeIn > frames
        || event.fadeOut > frames || event.fadeIn > frames - event.fadeOut
        || event.envelope.size() > kMaxEnvelopePoints
        || !isSupportedFadeCurve(event.fadeInCurve)
        || !isSupportedFadeCurve(event.fadeOutCurve)) {
        return false;
    }

    SampleFrame previous_offset = -1;
    for (const EnvelopePoint& point : event.envelope) {
        if (point.offset < 0 || point.offset >= frames
            || point.offset <= previous_offset || !std::isfinite(point.gain)) {
            return false;
        }
        previous_offset = point.offset;
    }
    return true;
}

} // namespace agplayer::editor
