#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
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

inline constexpr std::size_t kMaxEnvelopePoints = 64;

struct AudioEvent final {
    EventId id{};
    std::shared_ptr<const AudioSource> source;
    SampleFrame sourceStart{}, sourceEnd{}, timelineStart{};
    float gain{1.0F};
    SampleFrame fadeIn{}, fadeOut{};
    double speedRatio{1.0};
    int pitchSemitone{};
    bool mute{};
    std::vector<EnvelopePoint> envelope;
};

[[nodiscard]] inline SampleFrame audibleFrames(const AudioEvent& event) noexcept
{
    if (event.sourceStart < 0 || event.sourceEnd <= event.sourceStart) {
        return 0;
    }
    return event.sourceEnd - event.sourceStart;
}

[[nodiscard]] inline bool isValid(const AudioEvent& event) noexcept
{
    if (!event.source || event.source->total_frames <= 0
        || event.sourceStart < 0 || event.sourceEnd <= event.sourceStart
        || event.sourceEnd > event.source->total_frames
        || event.timelineStart < 0 || !std::isfinite(event.gain)
        || !std::isfinite(event.speedRatio) || event.speedRatio <= 0.0) {
        return false;
    }

    const SampleFrame frames = audibleFrames(event);
    if (event.fadeIn < 0 || event.fadeOut < 0 || event.fadeIn > frames
        || event.fadeOut > frames || event.fadeIn > frames - event.fadeOut
        || event.envelope.size() > kMaxEnvelopePoints) {
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
