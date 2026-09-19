#include "timeline_edit_command.hpp"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace agplayer::editor {

std::optional<TimelineEditCommand> TimelineEditCommand::move(
    const EventTimeline& timeline, const EventId id,
    const SampleFrame timeline_start)
{
    return move(timeline.snapshot(), id, timeline_start);
}

std::optional<TimelineEditCommand> TimelineEditCommand::move(
    const TimelineSnapshot& timeline, const EventId id,
    const SampleFrame timeline_start)
{
    const auto found = std::find_if(
        timeline.events.begin(), timeline.events.end(), [id](const AudioEvent& event) {
            return event.id == id;
        });
    if (found == timeline.events.end() || found->timelineStart == timeline_start) {
        return std::nullopt;
    }

    AudioEvent after = *found;
    after.timelineStart = timeline_start;
    return TimelineEditCommand{Kind::Move, *found, std::move(after)};
}

std::optional<TimelineEditCommand> TimelineEditCommand::trim(
    const EventTimeline& timeline, const EventId id,
    const SampleFrame source_start, const SampleFrame source_end,
    const SampleFrame timeline_start)
{
    return trim(timeline.snapshot(), id, source_start, source_end, timeline_start);
}

std::optional<TimelineEditCommand> TimelineEditCommand::trim(
    const TimelineSnapshot& timeline, const EventId id,
    const SampleFrame source_start, const SampleFrame source_end,
    const SampleFrame timeline_start)
{
    const auto found = std::find_if(
        timeline.events.begin(), timeline.events.end(), [id](const AudioEvent& event) {
            return event.id == id;
        });
    if (found == timeline.events.end() || (found->sourceStart == source_start
                                           && found->sourceEnd == source_end
                                           && found->timelineStart == timeline_start)) {
        return std::nullopt;
    }

    AudioEvent after = *found;
    after.sourceStart = source_start;
    after.sourceEnd = source_end;
    after.timelineStart = timeline_start;
    return TimelineEditCommand{Kind::Trim, *found, std::move(after)};
}

std::optional<TimelineEditCommand> TimelineEditCommand::fromCandidate(
    const EventTimeline& timeline, std::vector<AudioEvent> candidate)
{
    EventTimeline validated = timeline;
    if (!validated.replace(std::move(candidate))) return std::nullopt;
    candidate = validated.snapshot().events;
    const TimelineSnapshot snapshot = timeline.snapshot();
    const std::vector<AudioEvent>& current = snapshot.events;
    std::vector<AudioEvent> before;
    std::vector<AudioEvent> after;
    std::unordered_map<EventId, const AudioEvent*> currentById;
    std::unordered_map<EventId, const AudioEvent*> candidateById;
    currentById.reserve(current.size());
    candidateById.reserve(candidate.size());
    for (const AudioEvent& event : current) currentById.emplace(event.id, &event);
    for (const AudioEvent& event : candidate) candidateById.emplace(event.id, &event);
    for (const AudioEvent& event : current) {
        const auto found = candidateById.find(event.id);
        if (found == candidateById.end() || !sameEvent(event, *found->second)) {
            before.push_back(event);
        }
    }
    for (const AudioEvent& event : candidate) {
        const auto found = currentById.find(event.id);
        if (found == currentById.end() || !sameEvent(*found->second, event)) {
            after.push_back(event);
        }
    }
    if (before.empty() && after.empty()) {
        return std::nullopt;
    }
    return TimelineEditCommand{Kind::Generic, std::move(before),
                               std::move(after)};
}

std::optional<TimelineEditCommand> TimelineEditCommand::fromSnapshot(
    const EventTimeline& timeline, TimelineSnapshot candidate)
{
    EventTimeline validated = timeline;
    if (!validated.restore(std::move(candidate))) return std::nullopt;
    auto after = validated.snapshot();
    auto before = timeline.snapshot();
    auto command = fromCandidate(timeline, after.events);
    const bool metadataChanged = before.tracks != after.tracks
        || before.sampleRate != after.sampleRate || before.channels != after.channels
        || before.legacyMasterGain != after.legacyMasterGain;
    if (!command && !metadataChanged) return std::nullopt;
    if (!command) command = TimelineEditCommand{Kind::Generic, std::vector<AudioEvent>{}, std::vector<AudioEvent>{}};
    if (metadataChanged) {
        before.events = std::vector<AudioEvent>{};
        after.events = std::vector<AudioEvent>{};
        command->before_state_ = std::move(before);
        command->after_state_ = std::move(after);
    }
    return command;
}

bool TimelineEditCommand::execute(EventTimeline& timeline) const
{
    if (after_state_) {
        const auto current = timeline.snapshot();
        if (current.tracks != before_state_->tracks || current.sampleRate != before_state_->sampleRate
            || current.channels != before_state_->channels || current.legacyMasterGain != before_state_->legacyMasterGain) return false;
        EventTimeline candidate = timeline;
        if (!apply(candidate, before_, after_)) return false;
        auto state = *after_state_; state.events = candidate.snapshot().events;
        if (!candidate.restore(std::move(state))) return false;
        timeline = std::move(candidate); return true;
    }
    return apply(timeline, before_, after_);
}

bool TimelineEditCommand::undo(EventTimeline& timeline) const
{
    if (before_state_) {
        const auto current = timeline.snapshot();
        if (current.tracks != after_state_->tracks || current.sampleRate != after_state_->sampleRate
            || current.channels != after_state_->channels || current.legacyMasterGain != after_state_->legacyMasterGain) return false;
        EventTimeline candidate = timeline;
        if (!apply(candidate, after_, before_)) return false;
        auto state = *before_state_; state.events = candidate.snapshot().events;
        if (!candidate.restore(std::move(state))) return false;
        timeline = std::move(candidate); return true;
    }
    return apply(timeline, after_, before_);
}

bool TimelineEditCommand::apply(EventTimeline& timeline,
                                const std::vector<AudioEvent>& expected,
                                const std::vector<AudioEvent>& replacement) const
{
    if (kind_ == Kind::Move || kind_ == Kind::Trim) {
        if (expected.size() != 1 || replacement.size() != 1
            || expected.front().id != replacement.front().id) {
            return false;
        }
        const AudioEvent* const current = timeline.event(expected.front().id);
        if (!current || !sameEvent(*current, expected.front())) {
            return false;
        }
        if (kind_ == Kind::Move) {
            return timeline.moveEvent(replacement.front().id,
                                      replacement.front().timelineStart, replacement.front().trackIndex);
        }
        return timeline.trimEvent(replacement.front().id,
                                  replacement.front().sourceStart,
                                  replacement.front().sourceEnd,
                                  replacement.front().timelineStart);
    }

    const TimelineSnapshot snapshot = timeline.snapshot();
    for (const AudioEvent& event : expected) {
        const AudioEvent* const current = timeline.event(event.id);
        if (!current || !sameEvent(*current, event)) {
            return false;
        }
    }

    std::unordered_set<EventId> expectedIds;
    expectedIds.reserve(expected.size());
    for (const AudioEvent& event : expected) {
        expectedIds.insert(event.id);
    }
    for (const AudioEvent& event : replacement) {
        if (expectedIds.find(event.id) == expectedIds.end()
            && timeline.event(event.id) != nullptr) {
            return false;
        }
    }

    std::vector<AudioEvent> candidate;
    candidate.reserve(snapshot.events.size() - expected.size()
                      + replacement.size());
    std::copy_if(snapshot.events.begin(), snapshot.events.end(),
                 std::back_inserter(candidate), [&expectedIds](const AudioEvent& event) {
                     return expectedIds.find(event.id) == expectedIds.end();
                 });
    candidate.insert(candidate.end(), replacement.begin(), replacement.end());
    return timeline.replace(std::move(candidate));
}

std::size_t TimelineEditCommand::eventByteCost(const AudioEvent& event) noexcept
{
    return sizeof(AudioEvent)
        + event.envelope.size() * sizeof(EnvelopePoint);
}

std::size_t TimelineEditCommand::byteCost() const noexcept
{
    std::size_t result = sizeof(TimelineEditCommand);
    for (const AudioEvent& event : before_) result += eventByteCost(event);
    for (const AudioEvent& event : after_) result += eventByteCost(event);
    return result;
}

std::size_t TimelineEditCommand::affectedEventCount() const noexcept
{
    std::size_t count = before_.size();
    for (const AudioEvent& event : after_) {
        const auto found = std::find_if(before_.begin(), before_.end(),
            [&event](const AudioEvent& item) { return item.id == event.id; });
        if (found == before_.end()) ++count;
    }
    return count;
}

std::vector<std::shared_ptr<const AudioSource>> TimelineEditCommand::referencedSources() const
{
    std::vector<std::shared_ptr<const AudioSource>> result;
    std::unordered_set<const AudioSource*> seen;
    const auto append = [&result, &seen](const std::vector<AudioEvent>& events) {
        for (const AudioEvent& event : events) {
            if (event.source && seen.insert(event.source.get()).second) {
                result.push_back(event.source);
            }
        }
    };
    append(before_);
    append(after_);
    return result;
}

bool TimelineEditCommand::canCoalesceWith(
    const TimelineEditCommand& newer, const EventId eventId) const noexcept
{
    const bool compatibleKind = kind_ == newer.kind_
        && (kind_ == Kind::Move || kind_ == Kind::Trim);
    return compatibleKind && before_.size() == 1 && after_.size() == 1
        && newer.before_.size() == 1 && newer.after_.size() == 1
        && before_.front().id == eventId && after_.front().id == eventId
        && newer.before_.front().id == eventId
        && newer.after_.front().id == eventId
        && sameEvent(after_.front(), newer.before_.front());
}

void TimelineEditCommand::coalesceWith(TimelineEditCommand newer)
{
    after_ = std::move(newer.after_);
}

bool TimelineEditCommand::sameEvent(const AudioEvent& left,
                                    const AudioEvent& right) noexcept
{
    return left.id == right.id && left.source == right.source
        && left.trackIndex == right.trackIndex && left.timelineSampleRate == right.timelineSampleRate
        && left.sourceStart == right.sourceStart && left.sourceEnd == right.sourceEnd
        && left.timelineStart == right.timelineStart && left.gain == right.gain
        && left.fadeIn == right.fadeIn && left.fadeOut == right.fadeOut
        && left.fadeInCurve == right.fadeInCurve
        && left.fadeOutCurve == right.fadeOutCurve
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
