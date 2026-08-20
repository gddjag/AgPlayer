#include "timeline_edit_command.hpp"

#include <algorithm>

namespace agplayer::editor {

std::optional<TimelineEditCommand> TimelineEditCommand::move(
    const EventTimeline& timeline, const EventId id,
    const SampleFrame timeline_start)
{
    const AudioEvent* const before = timeline.event(id);
    if (!before || before->timelineStart == timeline_start) {
        return std::nullopt;
    }

    AudioEvent after = *before;
    after.timelineStart = timeline_start;
    return TimelineEditCommand{Kind::Move, *before, std::move(after)};
}

std::optional<TimelineEditCommand> TimelineEditCommand::trim(
    const EventTimeline& timeline, const EventId id,
    const SampleFrame source_start, const SampleFrame source_end,
    const SampleFrame timeline_start)
{
    const AudioEvent* const before = timeline.event(id);
    if (!before || (before->sourceStart == source_start
                    && before->sourceEnd == source_end
                    && before->timelineStart == timeline_start)) {
        return std::nullopt;
    }

    AudioEvent after = *before;
    after.sourceStart = source_start;
    after.sourceEnd = source_end;
    after.timelineStart = timeline_start;
    return TimelineEditCommand{Kind::Trim, *before, std::move(after)};
}

bool TimelineEditCommand::execute(EventTimeline& timeline) const
{
    return apply(timeline, before_, after_);
}

bool TimelineEditCommand::undo(EventTimeline& timeline) const
{
    return apply(timeline, after_, before_);
}

bool TimelineEditCommand::apply(EventTimeline& timeline,
                                const AudioEvent& expected,
                                const AudioEvent& replacement) const
{
    const AudioEvent* const current = timeline.event(expected.id);
    if (!current || !sameEvent(*current, expected)) {
        return false;
    }

    if (kind_ == Kind::Move) {
        return timeline.moveEvent(replacement.id, replacement.timelineStart);
    }
    return timeline.trimEvent(replacement.id, replacement.sourceStart,
                              replacement.sourceEnd, replacement.timelineStart);
}

bool TimelineEditCommand::sameEvent(const AudioEvent& left,
                                    const AudioEvent& right) noexcept
{
    return left.id == right.id && left.source == right.source
        && left.sourceStart == right.sourceStart && left.sourceEnd == right.sourceEnd
        && left.timelineStart == right.timelineStart && left.gain == right.gain
        && left.fadeIn == right.fadeIn && left.fadeOut == right.fadeOut
        && left.speedRatio == right.speedRatio
        && left.pitchSemitone == right.pitchSemitone && left.mute == right.mute
        && left.envelope.size() == right.envelope.size()
        && std::equal(left.envelope.begin(), left.envelope.end(),
                      right.envelope.begin(),
                      [](const EnvelopePoint& left_point,
                         const EnvelopePoint& right_point) {
                          return left_point.offset == right_point.offset
                              && left_point.gain == right_point.gain;
                      });
}

} // namespace agplayer::editor
