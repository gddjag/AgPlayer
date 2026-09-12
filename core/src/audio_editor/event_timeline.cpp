#include "event_timeline.hpp"

#include <algorithm>
#include <limits>

namespace agplayer::editor {

bool EventTimeline::insert(AudioEvent candidate)
{
    if (!isValid(candidate) || event(candidate.id) != nullptr
        || candidate.timelineStart > std::numeric_limits<SampleFrame>::max()
            - audibleFrames(candidate)
        || overlaps(candidate)) {
        return false;
    }

    const auto position = std::lower_bound(
        events_.begin(), events_.end(), candidate.timelineStart,
        [](const AudioEvent& existing, const SampleFrame timeline_start) {
            return existing.timelineStart < timeline_start;
        });
    events_.insert(position, std::move(candidate));
    ++revision_;
    return true;
}

bool EventTimeline::replace(std::vector<AudioEvent> events)
{
    std::sort(events.begin(), events.end(),
              [](const AudioEvent& left, const AudioEvent& right) {
                  return left.timelineStart < right.timelineStart;
              });
    for (std::size_t left = 0; left < events.size(); ++left) {
        const AudioEvent& candidate = events[left];
        if (!isValid(candidate)
            || candidate.timelineStart > std::numeric_limits<SampleFrame>::max()
                - audibleFrames(candidate)) {
            return false;
        }
        for (std::size_t right = left + 1; right < events.size(); ++right) {
            if (candidate.id == events[right].id
                || (candidate.timelineStart < endFrame(events[right])
                    && events[right].timelineStart < endFrame(candidate))) {
                return false;
            }
        }
    }
    events_.swap(events);
    ++revision_;
    return true;
}

bool EventTimeline::moveEvent(const EventId id, const SampleFrame timeline_start)
{
    const auto found = std::find_if(
        events_.begin(), events_.end(), [id](const AudioEvent& candidate) {
            return candidate.id == id;
        });
    if (found == events_.end() || found->timelineStart == timeline_start) {
        return false;
    }

    AudioEvent candidate = *found;
    candidate.timelineStart = timeline_start;
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
    return events_.empty() ? 0 : endFrame(events_.back());
}

TimelineSnapshot EventTimeline::snapshot() const
{
    return {events_, totalFrames(), revision_};
}

bool EventTimeline::overlaps(const AudioEvent& candidate) const noexcept
{
    const SampleFrame candidate_end = endFrame(candidate);
    if (candidate_end < candidate.timelineStart) {
        return true;
    }
    for (const AudioEvent& existing : events_) {
        const SampleFrame existing_end = endFrame(existing);
        if (candidate.timelineStart < existing_end
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
        if (candidate.timelineStart < existing_end
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
