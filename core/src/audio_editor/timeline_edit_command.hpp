#pragma once

#include "event_timeline.hpp"

#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace agplayer::editor {

class TimelineEditCommand final {
public:
    [[nodiscard]] static std::optional<TimelineEditCommand> move(
        const EventTimeline& timeline, EventId id, SampleFrame timelineStart);
    [[nodiscard]] static std::optional<TimelineEditCommand> move(
        const TimelineSnapshot& timeline, EventId id, SampleFrame timelineStart);
    [[nodiscard]] static std::optional<TimelineEditCommand> trim(
        const EventTimeline& timeline, EventId id, SampleFrame sourceStart,
        SampleFrame sourceEnd, SampleFrame timelineStart);
    [[nodiscard]] static std::optional<TimelineEditCommand> trim(
        const TimelineSnapshot& timeline, EventId id, SampleFrame sourceStart,
        SampleFrame sourceEnd, SampleFrame timelineStart);
    [[nodiscard]] static std::optional<TimelineEditCommand> fromCandidate(
        const EventTimeline& timeline, std::vector<AudioEvent> candidate);

    [[nodiscard]] bool execute(EventTimeline& timeline) const;
    [[nodiscard]] bool undo(EventTimeline& timeline) const;
    [[nodiscard]] std::size_t byteCost() const noexcept;
    [[nodiscard]] std::size_t affectedEventCount() const noexcept;
    [[nodiscard]] std::vector<std::shared_ptr<const AudioSource>> referencedSources() const;
    [[nodiscard]] bool canCoalesceWith(const TimelineEditCommand& newer,
                                       EventId eventId) const noexcept;
    void coalesceWith(TimelineEditCommand newer);

private:
    enum class Kind { Generic, Move, Trim };

    TimelineEditCommand(Kind kind, AudioEvent before, AudioEvent after)
        : kind_(kind), before_{std::move(before)}, after_{std::move(after)} {}
    TimelineEditCommand(Kind kind, std::vector<AudioEvent> before,
                        std::vector<AudioEvent> after)
        : kind_(kind), before_(std::move(before)), after_(std::move(after)) {}

    [[nodiscard]] bool apply(EventTimeline& timeline,
                             const std::vector<AudioEvent>& expected,
                             const std::vector<AudioEvent>& replacement) const;
    [[nodiscard]] static bool sameEvent(const AudioEvent& left,
                                        const AudioEvent& right) noexcept;
    [[nodiscard]] static std::size_t eventByteCost(
        const AudioEvent& event) noexcept;

    Kind kind_;
    std::vector<AudioEvent> before_;
    std::vector<AudioEvent> after_;
};

} // namespace agplayer::editor
