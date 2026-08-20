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
