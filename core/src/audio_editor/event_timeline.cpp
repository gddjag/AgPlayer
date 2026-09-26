#include "event_timeline.hpp"

#include <algorithm>
#include <limits>
#include <unordered_set>

namespace agplayer::editor {

bool EventTimeline::insert(AudioEvent candidate)
{
    const auto rate = sample_rate_ ? sample_rate_ : (candidate.source ? candidate.source->sample_rate : 0);
    candidate.timelineSampleRate = rate;
    if (events_.size() >= kMaxTimelineEvents || !isValid(candidate)
        || event(candidate.id) || candidate.timelineStart > std::numeric_limits<SampleFrame>::max() - audibleFrames(candidate)
        || overlaps(candidate)) return false;
    std::size_t points = candidate.envelope.size();
    for (const auto& item : events_) points += item.envelope.size();
    if (points > kMaxTimelineEnvelopePoints) return false;
    const auto position = std::upper_bound(events_.begin(), events_.end(), candidate.timelineStart,
        [](SampleFrame start, const AudioEvent& event) { return start < event.timelineStart; });
    events_.insert(position, std::move(candidate));
    sample_rate_ = rate;
    ++revision_;
    return true;
}

bool EventTimeline::replace(std::vector<AudioEvent> events)
{
    if (events.size() > kMaxTimelineEvents) return false;
    const auto rate = sample_rate_ ? sample_rate_ :
        (events.empty() || !events.front().source ? 0 : events.front().source->sample_rate);
    std::size_t points = 0;
    for (auto& item : events) {
        item.timelineSampleRate = rate;
        points += item.envelope.size();
        if (points > kMaxTimelineEnvelopePoints) return false;
    }
    std::sort(events.begin(), events.end(),
              [](const AudioEvent& left, const AudioEvent& right) {
                  return left.timelineStart < right.timelineStart;
              });
    std::array<SampleFrame, kTrackCount> lastEnds{};
    std::unordered_set<EventId> ids;
    for (const auto& candidate : events) {
        if (!isValid(candidate)
            || candidate.timelineStart > std::numeric_limits<SampleFrame>::max()
                - audibleFrames(candidate) || !ids.insert(candidate.id).second) {
            return false;
        }
        auto& end = lastEnds[static_cast<std::size_t>(candidate.trackIndex)];
        if (candidate.timelineStart < end) return false;
        end = endFrame(candidate);
    }
    events_.swap(events);
    sample_rate_ = rate;
    ++revision_;
    return true;
}

bool EventTimeline::restore(TimelineSnapshot state)
{
    if (state.channels == 0 || state.channels > 32
        || !std::isfinite(state.legacyMasterGain) || state.legacyMasterGain < 0) return false;
    for (const auto& track : state.tracks)
        if (!std::isfinite(track.gain) || track.gain < 0 || track.gain > 2) return false;
    EventTimeline candidate = *this;
    candidate.sample_rate_ = state.sampleRate;
    if (!candidate.replace(std::move(state.events))) return false;
    candidate.channels_ = state.channels;
    candidate.tracks_ = state.tracks;
    candidate.legacy_master_gain_ = state.legacyMasterGain;
    *this = std::move(candidate);
    return true;
}

bool EventTimeline::moveEvent(const EventId id, const SampleFrame timeline_start)
{
    const auto* found = event(id);
    return found && moveEvent(id, timeline_start, found->trackIndex);
}

bool EventTimeline::moveEvent(const EventId id, const SampleFrame timeline_start, const int trackIndex)
{
    const auto found = std::find_if(
        events_.begin(), events_.end(), [id](const AudioEvent& candidate) {
            return candidate.id == id;
        });
    if (found == events_.end() || (found->timelineStart == timeline_start && found->trackIndex == trackIndex)) {
        return false;
    }

    AudioEvent candidate = *found;
    candidate.timelineStart = timeline_start;
    candidate.trackIndex = trackIndex;
    return replaceEvent(id, std::move(candidate));
}

bool EventTimeline::trimEvent(const EventId id, const SampleFrame source_start,
                              const SampleFrame source_end,
                              const SampleFrame timeline_start)
{
    const auto found = std::find_if(
        events_.begin(), events_.end(), [id](const AudioEvent& candidate) {
            return candidate.id == id;
        });
    if (found == events_.end()
        || (found->sourceStart == source_start && found->sourceEnd == source_end
            && found->timelineStart == timeline_start)) {
        return false;
    }

    AudioEvent candidate = *found;
    candidate.sourceStart = source_start;
    candidate.sourceEnd = source_end;
    candidate.timelineStart = timeline_start;
    return replaceEvent(id, std::move(candidate));
}

const AudioEvent* EventTimeline::event(const EventId id) const noexcept
{
    const auto found = std::find_if(
        events_.begin(), events_.end(), [id](const AudioEvent& candidate) {
            return candidate.id == id;
        });
    return found == events_.end() ? nullptr : &*found;
}

std::vector<EventId> EventTimeline::eventsAt(const SampleFrame frame) const
{
    if (frame < 0) {
        return {};
    }

    std::vector<EventId> result;
    for (const AudioEvent& candidate : events_) {
        if (candidate.timelineStart > frame) {
            break;
        }
        if (frame < endFrame(candidate)) {
            result.push_back(candidate.id);
        }
    }
    return result;
}

SampleFrame EventTimeline::totalFrames() const noexcept
{
    SampleFrame result = 0;
    for (const auto& item : events_) result = std::max(result, endFrame(item));
    return result;
}

TimelineSnapshot EventTimeline::snapshot() const
{
    return {events_, totalFrames(), revision_, tracks_, sample_rate_, channels_, legacy_master_gain_};
}

bool EventTimeline::overlaps(const AudioEvent& candidate) const noexcept
{
    const SampleFrame candidate_end = endFrame(candidate);
    if (candidate_end < candidate.timelineStart) {
        return true;
    }
    for (const AudioEvent& existing : events_) {
        const SampleFrame existing_end = endFrame(existing);
        if (candidate.trackIndex == existing.trackIndex && candidate.timelineStart < existing_end
            && existing.timelineStart < candidate_end) {
            return true;
        }
    }
    return false;
}

bool EventTimeline::replaceEvent(const EventId id, AudioEvent candidate)
{
    const auto found = std::find_if(
        events_.begin(), events_.end(), [id](const AudioEvent& existing) {
            return existing.id == id;
        });
    if (found == events_.end() || candidate.id != id || !isValid(candidate)
        || candidate.timelineStart > std::numeric_limits<SampleFrame>::max()
            - audibleFrames(candidate)) {
        return false;
    }

    const SampleFrame candidate_end = endFrame(candidate);
    for (const AudioEvent& existing : events_) {
        if (existing.id == id) {
            continue;
        }
        const SampleFrame existing_end = endFrame(existing);
        if (candidate.trackIndex == existing.trackIndex && candidate.timelineStart < existing_end
            && existing.timelineStart < candidate_end) {
            return false;
        }
    }

    std::vector<AudioEvent> updated = events_;
    const auto replacement = std::find_if(
        updated.begin(), updated.end(), [id](const AudioEvent& existing) {
            return existing.id == id;
        });
    *replacement = std::move(candidate);
    std::sort(updated.begin(), updated.end(),
              [](const AudioEvent& left, const AudioEvent& right) {
                  return left.timelineStart < right.timelineStart;
              });
    events_.swap(updated);
    ++revision_;
    return true;
}

SampleFrame EventTimeline::endFrame(const AudioEvent& candidate) noexcept
{
    const SampleFrame frames = audibleFrames(candidate);
    if (frames <= 0
        || candidate.timelineStart > std::numeric_limits<SampleFrame>::max()
            - frames) {
        return std::numeric_limits<SampleFrame>::max();
    }
    return candidate.timelineStart + frames;
}

} // namespace agplayer::editor
