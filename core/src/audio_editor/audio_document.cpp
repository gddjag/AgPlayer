#include "audio_document.hpp"

#include <algorithm>
#include <limits>
#include <unordered_set>
#include <utility>

namespace agplayer::editor {

bool operator==(const Selection& left, const Selection& right) noexcept
{
    return left.start == right.start && left.end == right.end;
}

bool operator==(const Marker& left, const Marker& right) noexcept
{
    return left.name == right.name && left.frame == right.frame;
}

AudioDocument AudioDocument::fromSource(AudioSource source)
{
    AudioDocument document;
    if (source.sample_rate == 0 || source.channels == 0 || source.total_frames <= 0) {
        return document;
    }
    auto shared = std::make_shared<const AudioSource>(std::move(source));
    if (!document.timeline_.insert(AudioEvent{1, shared, 0, shared->total_frames, 0})) {
        return {};
    }
    document.next_event_id_ = 2;
    return document;
}

std::vector<std::shared_ptr<const AudioSource>> AudioDocument::retainedSources() const
{
    std::vector<std::shared_ptr<const AudioSource>> result = history_.retainedSources();
    std::unordered_set<const AudioSource*> seen;
    for (const auto& source : result) seen.insert(source.get());
    for (const AudioEvent& event : clipboard_) {
        if (event.source && seen.insert(event.source.get()).second) {
            result.push_back(event.source);
        }
    }
    return result;
}

AudioDocument AudioDocument::fromEvents(std::vector<AudioEvent> events)
{
    AudioDocument document;
    EventId highest{};
    for (const AudioEvent& event : events) {
        if (event.id == std::numeric_limits<EventId>::max()) return {};
        highest = std::max(highest, event.id);
    }
    if (!document.timeline_.replace(std::move(events))) return {};
    document.next_event_id_ = highest + 1;
    return document;
}

bool AudioDocument::setSelection(const Selection selection) noexcept
{
    if (!selection.valid() || selection.end > totalFrames()) return false;
    selection_ = selection;
    return true;
}

bool AudioDocument::clearSelection() noexcept
{
    const bool changed = selection_.has_value();
    selection_.reset();
    return changed;
}

bool AudioDocument::addMarker(Marker marker)
{
    if (marker.frame < 0 || marker.frame > totalFrames()) return false;
    const auto position = std::upper_bound(markers_.begin(), markers_.end(), marker.frame,
        [](const SampleFrame frame, const Marker& item) { return frame < item.frame; });
    markers_.insert(position, std::move(marker));
    return true;
}

bool AudioDocument::renameMarker(const std::size_t index, std::string name)
{
    if (index >= markers_.size()) return false;
    markers_[index].name = std::move(name);
    return true;
}

bool AudioDocument::removeMarker(const std::size_t index)
{
    if (index >= markers_.size()) return false;
    markers_.erase(markers_.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

bool AudioDocument::moveEvent(const EventId id, const SampleFrame timelineStart)
{
    const auto command = TimelineEditCommand::move(timeline_, id, timelineStart);
    if (!command || !history_.executeAndPush(
            std::make_unique<TimelineEditCommand>(std::move(*command)), timeline_)) {
        return false;
    }
    normalizeEditorState();
    return true;
}

bool AudioDocument::trimEvent(const EventId id, const SampleFrame sourceStart,
                              const SampleFrame sourceEnd,
                              const SampleFrame timelineStart)
{
    const auto command = TimelineEditCommand::trim(
        timeline_, id, sourceStart, sourceEnd, timelineStart);
    if (!command || !history_.executeAndPush(
            std::make_unique<TimelineEditCommand>(std::move(*command)), timeline_)) {
        return false;
    }
    normalizeEditorState();
    return true;
}

bool AudioDocument::splitAt(std::vector<AudioEvent>& events, const EventId id,
                            const SampleFrame frame, const EventId rightId)
{
    const auto found = std::find_if(events.begin(), events.end(),
        [id](const AudioEvent& event) { return event.id == id; });
    if (found == events.end() || frame <= found->timelineStart
        || frame >= found->timelineStart + audibleFrames(*found) || rightId == id) {
        return false;
    }
    AudioEvent right = *found;
    const SampleFrame sourceSplit = found->sourceStart + (frame - found->timelineStart);
    found->sourceEnd = sourceSplit;
    right.id = rightId;
    right.sourceStart = sourceSplit;
    right.timelineStart = frame;
    if (!isValid(*found) || !isValid(right)) return false;
    events.insert(std::next(found), std::move(right));
    return true;
}

bool AudioDocument::splitAtFrame(std::vector<AudioEvent>& events,
                                 const SampleFrame frame, EventId& nextId)
{
    for (const AudioEvent& event : events) {
        const SampleFrame end = event.timelineStart + audibleFrames(event);
        if (frame == event.timelineStart || frame == end) return true;
        if (frame > event.timelineStart && frame < end) {
            if (!splitAt(events, event.id, frame, nextId)) return false;
            ++nextId;
            return true;
        }
    }
    return true;
}

bool AudioDocument::splitEventAt(const EventId id, const SampleFrame frame)
{
    std::vector<AudioEvent> candidate = timeline_.snapshot().events;
    if (!splitAt(candidate, id, frame, next_event_id_)
        || !applyCandidate(std::move(candidate))) return false;
    ++next_event_id_;
    return true;
}

bool AudioDocument::hasValidSelection() const noexcept
{
    return selection_ && selection_->valid() && selection_->end <= totalFrames();
}

std::vector<AudioEvent> AudioDocument::selectedEvents() const
{
    if (!hasValidSelection()) return {};
    std::vector<AudioEvent> candidate = timeline_.snapshot().events;
    EventId candidateId = next_event_id_;
    if (!splitSelectionBoundaries(candidate, candidateId)) return {};
    std::vector<AudioEvent> selected;
    for (const AudioEvent& event : candidate) {
        const SampleFrame end = event.timelineStart + audibleFrames(event);
        if (event.timelineStart >= selection_->start && end <= selection_->end) {
            selected.push_back(event);
        }
    }
    return selected;
}

bool AudioDocument::splitSelectionBoundaries(
    std::vector<AudioEvent>& events, EventId& nextId) const
{
    if (!hasValidSelection()) return false;
    return splitAtFrame(events, selection_->start, nextId)
        && splitAtFrame(events, selection_->end, nextId);
}

bool AudioDocument::deleteSelection()
{
    if (!hasValidSelection()) return false;
    std::vector<AudioEvent> candidate = timeline_.snapshot().events;
    EventId candidateId = next_event_id_;
    if (!splitAtFrame(candidate, selection_->start, candidateId)
        || !splitAtFrame(candidate, selection_->end, candidateId)) return false;
    const auto oldSize = candidate.size();
    candidate.erase(std::remove_if(candidate.begin(), candidate.end(),
        [this](const AudioEvent& event) {
            const SampleFrame end = event.timelineStart + audibleFrames(event);
            return event.timelineStart >= selection_->start && end <= selection_->end;
        }), candidate.end());
    if (candidate.size() == oldSize || !applyCandidate(std::move(candidate))) return false;
    next_event_id_ = candidateId;
    selection_.reset();
    return true;
}

bool AudioDocument::cropToSelection()
{
    if (!hasValidSelection()) return false;
    std::vector<AudioEvent> candidate = timeline_.snapshot().events;
    EventId candidateId = next_event_id_;
    if (!splitSelectionBoundaries(candidate, candidateId)) return false;
    candidate.erase(std::remove_if(candidate.begin(), candidate.end(),
        [this](const AudioEvent& event) {
            const SampleFrame end = event.timelineStart + audibleFrames(event);
            return event.timelineStart < selection_->start || end > selection_->end;
        }), candidate.end());
    if (candidate.empty()) return false;
    for (AudioEvent& event : candidate) {
        event.timelineStart -= selection_->start;
    }
    if (!applyCandidate(std::move(candidate))) return false;
    next_event_id_ = candidateId;
    selection_.reset();
    return true;
}

bool AudioDocument::silenceSelection()
{
    if (!hasValidSelection()) return false;
    std::vector<AudioEvent> candidate = timeline_.snapshot().events;
    EventId candidateId = next_event_id_;
    if (!splitSelectionBoundaries(candidate, candidateId)) return false;
    for (AudioEvent& event : candidate) {
        const SampleFrame end = event.timelineStart + audibleFrames(event);
        if (event.timelineStart >= selection_->start && end <= selection_->end) {
            event.mute = true;
        }
    }
    if (!applyCandidate(std::move(candidate))) return false;
    next_event_id_ = candidateId;
    return true;
}

bool AudioDocument::fadeIn()
{
    if (!hasValidSelection()) return false;
    std::vector<AudioEvent> candidate = timeline_.snapshot().events;
    EventId candidateId = next_event_id_;
    if (!splitSelectionBoundaries(candidate, candidateId)) return false;
    for (AudioEvent& event : candidate) {
        const SampleFrame frames = audibleFrames(event);
        const SampleFrame end = event.timelineStart + frames;
        if (event.timelineStart >= selection_->start && end <= selection_->end) {
            event.fadeIn = frames - event.fadeOut;
        }
    }
    if (!applyCandidate(std::move(candidate))) return false;
    next_event_id_ = candidateId;
    return true;
}

bool AudioDocument::fadeOut()
{
    if (!hasValidSelection()) return false;
    std::vector<AudioEvent> candidate = timeline_.snapshot().events;
    EventId candidateId = next_event_id_;
    if (!splitSelectionBoundaries(candidate, candidateId)) return false;
    for (AudioEvent& event : candidate) {
        const SampleFrame frames = audibleFrames(event);
        const SampleFrame end = event.timelineStart + frames;
        if (event.timelineStart >= selection_->start && end <= selection_->end) {
            event.fadeOut = frames - event.fadeIn;
        }
    }
    if (!applyCandidate(std::move(candidate))) return false;
    next_event_id_ = candidateId;
    return true;
}

bool AudioDocument::setEventFadeOut(const EventId id,
                                    const SampleFrame frames)
{
    std::vector<AudioEvent> candidate = timeline_.snapshot().events;
    const auto event = std::find_if(candidate.begin(), candidate.end(),
        [id](const AudioEvent& item) { return item.id == id; });
    if (event == candidate.end()) return false;
    event->fadeOut = frames;
    return isValid(*event) && applyCandidate(std::move(candidate));
}

bool AudioDocument::addEnvelopePoint(const EventId id,
                                     const SampleFrame offset,
                                     const float gain)
{
    std::vector<AudioEvent> candidate = timeline_.snapshot().events;
    const auto event = std::find_if(candidate.begin(), candidate.end(),
        [id](const AudioEvent& item) { return item.id == id; });
    if (event == candidate.end()) return false;
    const auto point = std::lower_bound(event->envelope.begin(),
        event->envelope.end(), offset,
        [](const EnvelopePoint& item, const SampleFrame value) {
            return item.offset < value;
        });
    if (point != event->envelope.end() && point->offset == offset) return false;
    event->envelope.insert(point, EnvelopePoint{offset, gain});
    return isValid(*event) && applyCandidate(std::move(candidate));
}

bool AudioDocument::copySelection()
{
    const std::vector<AudioEvent> copied = selectedEvents();
    if (copied.empty()) return false;
    clipboard_ = copied;
    return true;
}

bool AudioDocument::cutSelection()
{
    if (!hasValidSelection()) return false;
    const std::vector<AudioEvent> copied = selectedEvents();
    if (copied.empty()) return false;
    std::vector<AudioEvent> candidate = timeline_.snapshot().events;
    EventId candidateId = next_event_id_;
    if (!splitAtFrame(candidate, selection_->start, candidateId)
        || !splitAtFrame(candidate, selection_->end, candidateId)) return false;
    candidate.erase(std::remove_if(candidate.begin(), candidate.end(),
        [this](const AudioEvent& event) {
            const SampleFrame end = event.timelineStart + audibleFrames(event);
            return event.timelineStart >= selection_->start && end <= selection_->end;
        }), candidate.end());
    if (!applyCandidate(std::move(candidate))) return false;
    clipboard_ = copied;
    next_event_id_ = candidateId;
    selection_.reset();
    return true;
}

bool AudioDocument::pasteAt(const SampleFrame playhead)
{
    if (clipboard_.empty() || playhead < 0) return false;
    const SampleFrame origin = clipboard_.front().timelineStart;
    std::vector<AudioEvent> candidate = timeline_.snapshot().events;
    EventId candidateId = next_event_id_;
    for (const AudioEvent& original : clipboard_) {
        const SampleFrame offset = original.timelineStart - origin;
        if (offset < 0 || playhead > std::numeric_limits<SampleFrame>::max() - offset) {
            return false;
        }
        AudioEvent clone = original;
        clone.id = candidateId++;
        clone.timelineStart = playhead + offset;
        candidate.push_back(std::move(clone));
    }
    if (!applyCandidate(std::move(candidate))) return false;
    next_event_id_ = candidateId;
    selection_.reset();
    return true;
}

bool AudioDocument::duplicateEvent(const EventId id,
                                   const SampleFrame timelineStart)
{
    if (timelineStart < 0 || next_event_id_ == std::numeric_limits<EventId>::max()) {
        return false;
    }
    std::vector<AudioEvent> candidate = timeline_.snapshot().events;
    const auto original = std::find_if(candidate.cbegin(), candidate.cend(),
        [id](const AudioEvent& event) { return event.id == id; });
    if (original == candidate.cend()) return false;
    AudioEvent clone = *original;
    clone.id = next_event_id_;
    clone.timelineStart = timelineStart;
    candidate.push_back(std::move(clone));
    if (!applyCandidate(std::move(candidate))) return false;
    ++next_event_id_;
    return true;
}

bool AudioDocument::sameParameters(const AudioEvent& left,
                                   const AudioEvent& right) noexcept
{
    const bool sameEnvelope = left.envelope.size() == right.envelope.size()
        && std::equal(left.envelope.begin(), left.envelope.end(),
                      right.envelope.begin(), [](const EnvelopePoint& lhs,
                                                  const EnvelopePoint& rhs) {
                          return lhs.offset == rhs.offset && lhs.gain == rhs.gain;
                      });
    return left.source == right.source && left.gain == right.gain
        && left.fadeIn == right.fadeIn && left.fadeOut == right.fadeOut
        && left.speedRatio == right.speedRatio
        && left.pitchSemitone == right.pitchSemitone && left.mute == right.mute
        && sameEnvelope;
}

bool AudioDocument::mergeEvents(const EventId leftId, const EventId rightId)
{
    if (leftId == rightId) return false;
    std::vector<AudioEvent> candidate = timeline_.snapshot().events;
    const auto left = std::find_if(candidate.begin(), candidate.end(),
        [leftId](const AudioEvent& event) { return event.id == leftId; });
    const auto right = std::find_if(candidate.begin(), candidate.end(),
        [rightId](const AudioEvent& event) { return event.id == rightId; });
    if (left == candidate.end() || right == candidate.end()
        || left->timelineStart + audibleFrames(*left) != right->timelineStart
        || left->sourceEnd != right->sourceStart || !sameParameters(*left, *right)) {
        return false;
    }
    left->sourceEnd = right->sourceEnd;
    candidate.erase(right);
    return applyCandidate(std::move(candidate));
}

bool AudioDocument::replaceSelectionWithSource(AudioSource source)
{
    if (!hasValidSelection() || source.sample_rate == 0 || source.channels == 0
        || source.total_frames != selection_->end - selection_->start) {
        return false;
    }
    const TimelineSnapshot current = timeline_.snapshot();
    if (current.events.empty()) return false;
    const AudioSource& format = *current.events.front().source;
    if (source.sample_rate != format.sample_rate
        || source.channels != format.channels) {
        return false;
    }
    const Selection replacement = *selection_;
    std::vector<AudioEvent> candidate = current.events;
    EventId candidateId = next_event_id_;
    if (!splitSelectionBoundaries(candidate, candidateId)
        || candidateId == std::numeric_limits<EventId>::max()) {
        return false;
    }
    candidate.erase(std::remove_if(candidate.begin(), candidate.end(),
        [&replacement](const AudioEvent& event) {
            const SampleFrame end = event.timelineStart + audibleFrames(event);
            return event.timelineStart >= replacement.start
                && end <= replacement.end;
        }), candidate.end());
    auto shared = std::make_shared<const AudioSource>(std::move(source));
    candidate.push_back(AudioEvent{candidateId, shared, 0,
                                   shared->total_frames, replacement.start});
    if (!applyCandidate(std::move(candidate))) return false;
    next_event_id_ = candidateId + 1;
    selection_.reset();
    return true;
}

bool AudioDocument::insertSourceAtCursor(AudioSource source,
                                         const SampleFrame cursor)
{
    if (cursor < 0 || source.sample_rate == 0 || source.channels == 0
        || source.total_frames <= 0
        || next_event_id_ == std::numeric_limits<EventId>::max()) return false;
    const TimelineSnapshot current = timeline_.snapshot();
    if (!current.events.empty()) {
        const AudioSource& format = *current.events.front().source;
        if (source.sample_rate != format.sample_rate
            || source.channels != format.channels) {
            return false;
        }
    }
    auto shared = std::make_shared<const AudioSource>(std::move(source));
    std::vector<AudioEvent> candidate = current.events;
    candidate.push_back(AudioEvent{next_event_id_, shared, 0,
                                   shared->total_frames, cursor});
    if (!applyCandidate(std::move(candidate))) return false;
    ++next_event_id_;
    return true;
}

bool AudioDocument::insertSource(AudioSource source,
                                 const SampleFrame timelineStart)
{
    return insertSourceAtCursor(std::move(source), timelineStart);
}

SampleFrame AudioDocument::totalFrames() const noexcept
{
    return timeline_.totalFrames();
}

bool AudioDocument::undo()
{
    if (!history_.undo(timeline_)) return false;
    normalizeEditorState();
    return true;
}

bool AudioDocument::redo()
{
    if (!history_.redo(timeline_)) return false;
    normalizeEditorState();
    return true;
}

bool AudioDocument::applyCandidate(std::vector<AudioEvent> candidate)
{
    auto command = TimelineEditCommand::fromCandidate(
        timeline_, std::move(candidate));
    if (!command || !history_.executeAndPush(
            std::make_unique<TimelineEditCommand>(std::move(*command)), timeline_)) {
        return false;
    }
    normalizeEditorState();
    return true;
}

void AudioDocument::normalizeEditorState() noexcept
{
    const SampleFrame total = totalFrames();
    if (selection_) {
        if (total <= 0 || selection_->start >= total) {
            selection_.reset();
        } else if (selection_->end > total) {
            selection_->end = total;
        }
    }
    for (Marker& marker : markers_) {
        marker.frame = std::clamp(marker.frame, SampleFrame{0}, total);
    }
}

} // namespace agplayer::editor
