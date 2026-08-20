#pragma once

#include "event_timeline.hpp"

#include <optional>
#include <utility>

namespace agplayer::editor {

class TimelineEditCommand final {
public:
    [[nodiscard]] static std::optional<TimelineEditCommand> move(
        const EventTimeline& timeline, EventId id, SampleFrame timelineStart);
    [[nodiscard]] static std::optional<TimelineEditCommand> trim(
        const EventTimeline& timeline, EventId id, SampleFrame sourceStart,
        SampleFrame sourceEnd, SampleFrame timelineStart);

    [[nodiscard]] bool execute(EventTimeline& timeline) const;
    [[nodiscard]] bool undo(EventTimeline& timeline) const;

private:
    enum class Kind { Move, Trim };

    TimelineEditCommand(Kind kind, AudioEvent before, AudioEvent after)
        : kind_(kind), before_(std::move(before)), after_(std::move(after)) {}

    [[nodiscard]] bool apply(EventTimeline& timeline,
                             const AudioEvent& expected,
                             const AudioEvent& replacement) const;
    [[nodiscard]] static bool sameEvent(const AudioEvent& left,
                                        const AudioEvent& right) noexcept;

    Kind kind_;
    AudioEvent before_;
    AudioEvent after_;
};

} // namespace agplayer::editor
