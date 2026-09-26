#include "audio_document.hpp"

#include <algorithm>
#include <limits>
#include <unordered_set>
#include <utility>

namespace agplayer::editor {

namespace {

void fitEnvelopePointLimit(std::vector<EnvelopePoint>& points)
{
    while (points.size() > kMaxEnvelopePoints) {
        std::size_t removeIndex = 1;
        double smallestError = std::numeric_limits<double>::infinity();
        for (std::size_t index = 1; index + 1 < points.size(); ++index) {
            const EnvelopePoint& previous = points[index - 1];
            const EnvelopePoint& point = points[index];
            const EnvelopePoint& next = points[index + 1];
            const double fraction = static_cast<double>(
                point.offset - previous.offset)
                / static_cast<double>(next.offset - previous.offset);
            const double interpolated = previous.gain
                + (next.gain - previous.gain) * fraction;
            const double error = std::abs(point.gain - interpolated);
            if (error < smallestError) {
                smallestError = error;
                removeIndex = index;
            }
        }
        points.erase(points.begin()
            + static_cast<std::ptrdiff_t>(removeIndex));
    }
}

std::optional<std::vector<EnvelopePoint>> reframedEnvelope(
    const AudioEvent& event, const SampleFrame newSourceStart,
    const SampleFrame newSourceEnd)
{
    const SampleFrame newFrames = projectOffsetAt(event, newSourceEnd) - projectOffsetAt(event, newSourceStart);
    if (newFrames <= 0) return std::nullopt;
    const SampleFrame oldOffsetAtNewStart = projectOffsetAt(event, newSourceStart);
    const SampleFrame oldOffsetAtNewEnd = projectOffsetAt(event, newSourceEnd);
    std::vector<EnvelopePoint> result;
    result.reserve(event.envelope.size() + 1U);

    if (oldOffsetAtNewStart > 0
        && oldOffsetAtNewStart < audibleFrames(event)) {
        const float boundaryGain = envelopeGainAt(event, oldOffsetAtNewStart);
        if (boundaryGain != 1.0F) {
            result.push_back({0, boundaryGain});
        }
    }
    for (const EnvelopePoint& point : event.envelope) {
        if (point.offset < oldOffsetAtNewStart
            || point.offset >= oldOffsetAtNewEnd) {
            continue;
        }
        const SampleFrame rebased = point.offset - oldOffsetAtNewStart;
        if (rebased < 0 || rebased >= newFrames) continue;
        if (!result.empty() && result.back().offset == rebased) {
            result.back().gain = point.gain;
        } else {
            result.push_back({rebased, point.gain});
        }
    }
    if (oldOffsetAtNewEnd > 0
        && oldOffsetAtNewEnd < audibleFrames(event)) {
        const SampleFrame boundaryOffset = newFrames - 1;
        const float boundaryGain = envelopeGainAt(
            event, oldOffsetAtNewEnd - 1);
        if (!result.empty() && result.back().offset == boundaryOffset) {
            result.back().gain = boundaryGain;
        } else if (boundaryGain != 1.0F
                   && (result.empty()
                       || result.back().gain != boundaryGain)) {
            result.push_back({boundaryOffset, boundaryGain});
        }
    }
    fitEnvelopePointLimit(result);
    return result;
}

bool reframeEvent(AudioEvent& event, const SampleFrame newSourceStart,
                  const SampleFrame newSourceEnd,
                  const SampleFrame newTimelineStart)
{
    if (!event.source || newSourceStart < 0
        || newSourceEnd <= newSourceStart
        || newSourceEnd > event.source->total_frames
        || newTimelineStart < 0) {
        return false;
    }
    const SampleFrame oldFrames = audibleFrames(event);
    const SampleFrame sliceStart = projectOffsetAt(event, newSourceStart);
    const SampleFrame sliceEnd = projectOffsetAt(event, newSourceEnd);
    const SampleFrame newFrames = sliceEnd - sliceStart;
    const auto envelope = reframedEnvelope(
        event, newSourceStart, newSourceEnd);
    if (!envelope) return false;

    const SampleFrame fadeInRemaining = sliceStart <= 0
        ? event.fadeIn : std::max<SampleFrame>(0, event.fadeIn - sliceStart);
    const SampleFrame removedTail = std::max<SampleFrame>(0,
        oldFrames - sliceEnd);
    const SampleFrame fadeOutRemaining = sliceEnd >= oldFrames
        ? event.fadeOut : std::max<SampleFrame>(0, event.fadeOut - removedTail);

    event.sourceStart = newSourceStart;
    event.sourceEnd = newSourceEnd;
    event.timelineStart = newTimelineStart;
    event.fadeIn = std::min(fadeInRemaining, newFrames);
    event.fadeOut = std::min(fadeOutRemaining,
        std::max<SampleFrame>(0, newFrames - event.fadeIn));
    event.envelope = std::move(*envelope);
    return isValid(event);
}

struct SourceEnvelopePoint final {
    SampleFrame sourceFrame{};
    float gain{1.0F};
};

void appendSourceEnvelope(const AudioEvent& event,
                          std::vector<SourceEnvelopePoint>& result)
{
    result.push_back({event.sourceStart, envelopeGainAt(event, 0)});
    for (const EnvelopePoint& point : event.envelope) {
        result.push_back({event.sourceStart + sourceOffsetAt(event, point.offset), point.gain});
    }
}

std::vector<SourceEnvelopePoint> sharedSourceEnvelope(
    const AudioEvent& left, const AudioEvent& right)
{
    std::vector<SourceEnvelopePoint> result;
    result.reserve(left.envelope.size() + right.envelope.size() + 2U);
    appendSourceEnvelope(left, result);
    appendSourceEnvelope(right, result);
    std::stable_sort(result.begin(), result.end(),
        [](const SourceEnvelopePoint& first, const SourceEnvelopePoint& second) {
            return first.sourceFrame < second.sourceFrame;
        });
    std::vector<SourceEnvelopePoint> unique;
    unique.reserve(result.size());
    for (const SourceEnvelopePoint& point : result) {
        if (!unique.empty() && unique.back().sourceFrame == point.sourceFrame) {
            unique.back().gain = point.gain;
        } else {
            unique.push_back(point);
        }
    }
    return unique;
}

float sourceEnvelopeGainAt(const std::vector<SourceEnvelopePoint>& points,
                           const SampleFrame sourceFrame) noexcept
{
    if (points.empty()) return 1.0F;
    SourceEnvelopePoint previous = points.front();
    if (sourceFrame <= previous.sourceFrame) return previous.gain;
    for (std::size_t index = 1; index < points.size(); ++index) {
        const SourceEnvelopePoint& next = points[index];
        if (sourceFrame <= next.sourceFrame) {
            const double fraction = static_cast<double>(
                sourceFrame - previous.sourceFrame)
                / static_cast<double>(next.sourceFrame - previous.sourceFrame);
            return previous.gain + static_cast<float>(
                (next.gain - previous.gain) * fraction);
        }
        previous = next;
    }
    return previous.gain;
}

std::vector<EnvelopePoint> reframeSharedEnvelope(
    const std::vector<SourceEnvelopePoint>& points,
    const AudioEvent& event)
{
    const SampleFrame sourceStart = event.sourceStart, sourceEnd = event.sourceEnd;
    const SampleFrame frames = audibleFrames(event);
    std::vector<EnvelopePoint> result;
    result.reserve(points.size() + 1U);
    const float startGain = sourceEnvelopeGainAt(points, sourceStart);
    if (startGain != 1.0F) result.push_back({0, startGain});
    for (const SourceEnvelopePoint& point : points) {
        if (point.sourceFrame <= sourceStart || point.sourceFrame >= sourceEnd) {
            continue;
        }
        const SampleFrame offset = projectOffsetAt(event, point.sourceFrame);
        if (offset >= frames) continue;
        if (!result.empty() && result.back().offset == offset) {
            result.back().gain = point.gain;
        } else {
            result.push_back({offset, point.gain});
        }
    }
    const SampleFrame lastOffset = frames - 1;
    const float lastGain = sourceEnvelopeGainAt(points,
                                                sourceStart + sourceOffsetAt(event, lastOffset));
    if (!result.empty() && result.back().offset == lastOffset) {
        result.back().gain = lastGain;
    } else if (lastGain != 1.0F
               && (result.empty() || result.back().gain != lastGain)) {
        result.push_back({lastOffset, lastGain});
    }
    fitEnvelopePointLimit(result);
    return result;
}

bool sameSharedBoundaryParameters(const AudioEvent& left,
                                  const AudioEvent& right) noexcept
{
    return left.trackIndex == right.trackIndex && left.timelineSampleRate == right.timelineSampleRate
        && left.source == right.source && left.gain == right.gain
        && left.speedRatio == right.speedRatio
        && left.pitchSemitone == right.pitchSemitone && left.mute == right.mute;
}

} // namespace

bool operator==(const Selection& left, const Selection& right) noexcept
{
    return left.start == right.start && left.end == right.end && left.trackIndex == right.trackIndex;
}

bool operator==(const Marker& left, const Marker& right) noexcept
{
    return left.name == right.name && left.frame == right.frame;
}

bool reframeSharedBoundary(AudioEvent& left, AudioEvent& right,
                           const SampleFrame sourceBoundary)
{
    if (!left.source || left.source != right.source
        || left.timelineStart + audibleFrames(left) != right.timelineStart
        || left.sourceEnd != right.sourceStart
        || !sameSharedBoundaryParameters(left, right)
        || sourceBoundary <= left.sourceStart
        || sourceBoundary >= right.sourceEnd) {
        return false;
    }
    const auto envelope = sharedSourceEnvelope(left, right);
    const SampleFrame leftTimelineStart = left.timelineStart;
    const SampleFrame rightSourceEnd = right.sourceEnd;
    if (!reframeEvent(left, left.sourceStart, sourceBoundary,
                      leftTimelineStart)
        || !reframeEvent(right, sourceBoundary, rightSourceEnd,
                         leftTimelineStart + audibleFrames(left))) {
        return false;
    }
    left.envelope = reframeSharedEnvelope(envelope, left);
    right.envelope = reframeSharedEnvelope(envelope, right);
    return isValid(left) && isValid(right);
}

AudioDocument AudioDocument::fromSource(AudioSource source)
{
    AudioDocument document;
    if (source.sample_rate == 0 || source.channels == 0 || source.total_frames <= 0) {
        return document;
    }
    auto shared = std::make_shared<const AudioSource>(std::move(source));
    if (!document.timeline_.insert(AudioEvent{1, shared, 0, shared->total_frames, 0,
                                               1.0F, 0, 0, 1.0, 0, false, {}})) {
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
    TimelineSnapshot state;
    state.events = std::move(events);
    return fromSnapshot(std::move(state));
}

AudioDocument AudioDocument::fromSnapshot(TimelineSnapshot state)
{
    AudioDocument document;
    EventId highest{};
    for (const AudioEvent& event : state.events) {
        if (event.id == std::numeric_limits<EventId>::max()) return {};
        highest = std::max(highest, event.id);
    }
    if (!document.timeline_.restore(std::move(state))) return {};
    document.next_event_id_ = highest + 1;
    return document;
}

bool AudioDocument::setProjectFormat(std::uint32_t sampleRate, std::uint32_t channels)
{
    auto state = timeline_.snapshot();
    if (!state.events.empty() || sampleRate == 0 || channels == 0 || channels > 32) return false;
    state.sampleRate = sampleRate; state.channels = channels;
    return applySnapshot(std::move(state));
}

bool AudioDocument::setTrackMuted(int trackIndex, bool muted)
{
    if (trackIndex < 0 || trackIndex >= static_cast<int>(kTrackCount)) return false;
    auto state = timeline_.snapshot();
    state.tracks[static_cast<std::size_t>(trackIndex)].muted = muted;
    return applySnapshot(std::move(state));
}

bool AudioDocument::setTrackSolo(int trackIndex, bool solo)
{
    if (trackIndex < 0 || trackIndex >= static_cast<int>(kTrackCount)) return false;
    auto state = timeline_.snapshot();
    if (state.tracks[static_cast<std::size_t>(trackIndex)].solo == solo) return true;
    state.tracks[static_cast<std::size_t>(trackIndex)].solo = solo;
    return applySnapshot(std::move(state));
}

bool AudioDocument::cropEventToSelection(EventId id)
{
    if (!hasValidSelection()) return false;
    const auto* event = timeline_.event(id);
    if (!event || (selection_->trackIndex >= 0 && selection_->trackIndex != event->trackIndex)) return false;
    const auto start = std::max(selection_->start, event->timelineStart);
    const auto end = std::min(selection_->end, event->timelineStart + audibleFrames(*event));
    if (end <= start) return false;
    const auto sourceStart = event->sourceStart + sourceOffsetAt(*event, start - event->timelineStart);
    const auto sourceEnd = event->sourceStart + sourceOffsetAt(*event, end - event->timelineStart);
    return trimEvent(id, sourceStart, sourceEnd, event->timelineStart + projectOffsetAt(*event, sourceStart));
}

bool AudioDocument::setTrackGain(int trackIndex, float gain)
{
    if (trackIndex < 0 || trackIndex >= static_cast<int>(kTrackCount)
        || !std::isfinite(gain) || gain < 0 || gain > 2) return false;
    auto state = timeline_.snapshot();
    state.tracks[static_cast<std::size_t>(trackIndex)].gain = gain;
    return applySnapshot(std::move(state));
}

bool AudioDocument::setTrackSpeedRatio(int trackIndex, double ratio)
{
    if (trackIndex < -1 || trackIndex >= static_cast<int>(kTrackCount)
        || !std::isfinite(ratio) || ratio < 0.5 || ratio > 2.0) return false;
    auto candidate = timeline_.snapshot().events;
    bool changed = false;
    for (int track = 0; track < static_cast<int>(kTrackCount); ++track) {
        if (trackIndex >= 0 && track != trackIndex) continue;
        std::vector<AudioEvent*> trackEvents;
        for (auto& event : candidate)
            if (event.trackIndex == track) trackEvents.push_back(&event);
        std::stable_sort(trackEvents.begin(), trackEvents.end(),
            [](const AudioEvent* a, const AudioEvent* b) { return a->timelineStart < b->timelineStart; });
        SampleFrame shift = 0;
        for (AudioEvent* event : trackEvents) {
            const SampleFrame oldFrames = audibleFrames(*event);
            const SampleFrame oldStart = event->timelineStart;
            const long double newStart = static_cast<long double>(oldStart) + shift;
            if (newStart < 0 || newStart > static_cast<long double>((std::numeric_limits<SampleFrame>::max)())) return false;
            event->timelineStart = static_cast<SampleFrame>(newStart);
            if (event->speedRatio != ratio) {
                changed = true;
                event->speedRatio = ratio;
                const SampleFrame newFrames = audibleFrames(*event);
                if (newFrames <= 0) return false;
                const auto scaleOffset = [oldFrames, newFrames](SampleFrame offset) {
                    return static_cast<SampleFrame>(std::llround(
                        static_cast<long double>(offset) * newFrames / oldFrames));
                };
                event->fadeIn = std::min(newFrames, scaleOffset(event->fadeIn));
                event->fadeOut = std::min(newFrames - event->fadeIn, scaleOffset(event->fadeOut));
                SampleFrame previous = -1;
                std::vector<EnvelopePoint> points;
                points.reserve(event->envelope.size());
                for (auto point : event->envelope) {
                    point.offset = std::min(newFrames - 1, scaleOffset(point.offset));
                    if (point.offset == previous) points.back().gain = point.gain;
                    else { points.push_back(point); previous = point.offset; }
                }
                event->envelope = std::move(points);
                const long double nextShift = static_cast<long double>(shift) + newFrames - oldFrames;
                if (nextShift < static_cast<long double>((std::numeric_limits<SampleFrame>::min)())
                    || nextShift > static_cast<long double>((std::numeric_limits<SampleFrame>::max)())) return false;
                shift = static_cast<SampleFrame>(nextShift);
            }
        }
    }
    return changed && applyCandidate(std::move(candidate));
}

bool AudioDocument::setTrackPitchSemitone(int trackIndex, int semitones)
{
    if (trackIndex < -1 || trackIndex >= static_cast<int>(kTrackCount)
        || semitones < -12 || semitones > 12) return false;
    auto candidate = timeline_.snapshot().events;
    bool changed = false;
    for (auto& event : candidate) {
        if ((trackIndex < 0 || event.trackIndex == trackIndex)
            && event.pitchSemitone != semitones) {
            event.pitchSemitone = semitones;
            changed = true;
        }
    }
    return changed && applyCandidate(std::move(candidate));
}

bool AudioDocument::moveEvent(EventId id, SampleFrame start, int trackIndex)
{
    auto candidate = timeline_.snapshot().events;
    const auto found = std::find_if(candidate.begin(), candidate.end(),
        [id](const AudioEvent& event) { return event.id == id; });
    if (found == candidate.end()) return false;
    found->timelineStart = start; found->trackIndex = trackIndex;
    return applyCandidate(std::move(candidate));
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
    std::vector<AudioEvent> candidate = timeline_.snapshot().events;
    const auto event = std::find_if(candidate.begin(), candidate.end(),
        [id](const AudioEvent& item) { return item.id == id; });
    if (event == candidate.end()
        || (event->sourceStart == sourceStart && event->sourceEnd == sourceEnd
            && event->timelineStart == timelineStart)
        || !reframeEvent(*event, sourceStart, sourceEnd, timelineStart)) {
        return false;
    }
    return applyCandidate(std::move(candidate));
}

bool AudioDocument::trimSharedBoundary(const EventId leftId,
                                       const EventId rightId,
                                       const SampleFrame sourceBoundary)
{
    if (leftId == rightId) return false;
    std::vector<AudioEvent> candidate = timeline_.snapshot().events;
    const auto left = std::find_if(candidate.begin(), candidate.end(),
        [leftId](const AudioEvent& event) { return event.id == leftId; });
    const auto right = std::find_if(candidate.begin(), candidate.end(),
        [rightId](const AudioEvent& event) { return event.id == rightId; });
    if (left == candidate.end() || right == candidate.end()
        || !reframeSharedBoundary(*left, *right, sourceBoundary)) {
        return false;
    }
    return applyCandidate(std::move(candidate));
}

bool AudioDocument::splitAt(std::vector<AudioEvent>& events, const EventId id,
                            const SampleFrame frame, const EventId rightId)
{
    const auto found = std::find_if(events.begin(), events.end(),
        [id](const AudioEvent& event) { return event.id == id; });
    if (found == events.end() || frame <= found->timelineStart
        || frame >= found->timelineStart + audibleFrames(*found) || rightId == id
        || rightId == std::numeric_limits<EventId>::max()) {
        return false;
    }
    const AudioEvent original = *found;
    AudioEvent left = original;
    AudioEvent right = original;
    const SampleFrame sourceSplit = found->sourceStart + sourceOffsetAt(*found, frame - found->timelineStart);
    if (!reframeEvent(left, original.sourceStart, sourceSplit,
                      original.timelineStart)
        || !reframeEvent(right, sourceSplit, original.sourceEnd, original.timelineStart + audibleFrames(left))) {
        return false;
    }
    right.id = rightId;
    *found = std::move(left);
    events.insert(std::next(found), std::move(right));
    return true;
}

bool AudioDocument::splitAtFrame(std::vector<AudioEvent>& events,
                                 const SampleFrame frame, EventId& nextId, const int trackIndex)
{
    std::vector<EventId> toSplit;
    for (const AudioEvent& event : events) {
        if (trackIndex >= 0 && event.trackIndex != trackIndex) continue;
        const SampleFrame end = event.timelineStart + audibleFrames(event);
        if (frame > event.timelineStart && frame < end) {
            toSplit.push_back(event.id);
        }
    }
    for (const auto id : toSplit) {
        if (nextId == std::numeric_limits<EventId>::max() || !splitAt(events, id, frame, nextId)) return false;
        ++nextId;
    }
    return true;
}

bool AudioDocument::splitEventAt(const EventId id, const SampleFrame frame)
{
    if (next_event_id_ == std::numeric_limits<EventId>::max()) return false;
    std::vector<AudioEvent> candidate = timeline_.snapshot().events;
    if (!splitAt(candidate, id, frame, next_event_id_)
        || !applyCandidate(std::move(candidate))) return false;
    ++next_event_id_;
    return true;
}

bool AudioDocument::clearTimeline()
{
    auto state = timeline_.snapshot();
    state.events.clear(); state.tracks = {}; state.legacyMasterGain = 1.0F;
    return applySnapshot(std::move(state));
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
        if ((selection_->trackIndex < 0 || event.trackIndex == selection_->trackIndex)
            && event.timelineStart >= selection_->start && end <= selection_->end) {
            selected.push_back(event);
        }
    }
    return selected;
}

bool AudioDocument::splitSelectionBoundaries(
    std::vector<AudioEvent>& events, EventId& nextId) const
{
    if (!hasValidSelection()) return false;
    return splitAtFrame(events, selection_->start, nextId, selection_->trackIndex)
        && splitAtFrame(events, selection_->end, nextId, selection_->trackIndex);
}

bool AudioDocument::deleteSelection()
{
    if (!hasValidSelection()) return false;
    std::vector<AudioEvent> candidate = timeline_.snapshot().events;
    EventId candidateId = next_event_id_;
    if (!splitSelectionBoundaries(candidate, candidateId)) return false;
    const auto oldSize = candidate.size();
    candidate.erase(std::remove_if(candidate.begin(), candidate.end(),
        [this](const AudioEvent& event) {
            const SampleFrame end = event.timelineStart + audibleFrames(event);
            return (selection_->trackIndex < 0 || event.trackIndex == selection_->trackIndex)
                && event.timelineStart >= selection_->start && end <= selection_->end;
        }), candidate.end());
    if (candidate.size() == oldSize || !applyCandidate(std::move(candidate))) return false;
    next_event_id_ = candidateId;
    selection_.reset();
    return true;
}

bool AudioDocument::cropToSelection()
{
    if (!hasValidSelection()) return false;
    const int selectionTrack = selection_->trackIndex;
    std::vector<AudioEvent> candidate = timeline_.snapshot().events;
    EventId candidateId = next_event_id_;
    if (!splitSelectionBoundaries(candidate, candidateId)) return false;
    candidate.erase(std::remove_if(candidate.begin(), candidate.end(),
        [this](const AudioEvent& event) {
            const SampleFrame end = event.timelineStart + audibleFrames(event);
            return (selection_->trackIndex < 0 || event.trackIndex == selection_->trackIndex)
                && (event.timelineStart < selection_->start || end > selection_->end);
        }), candidate.end());
    if (candidate.empty()) return false;
    for (AudioEvent& event : candidate) {
        if (selection_->trackIndex >= 0 && event.trackIndex != selection_->trackIndex) continue;
        if (selection_->trackIndex < 0) event.timelineStart -= selection_->start;
        const SampleFrame lastOffset = audibleFrames(event) - 1;
        const float firstGain = envelopeGainAt(event, 0);
        const float lastGain = envelopeGainAt(event, lastOffset);
        if (event.envelope.empty() || event.envelope.front().offset != 0) {
            event.envelope.insert(event.envelope.begin(), {0, firstGain});
        }
        if (event.envelope.back().offset != lastOffset) {
            event.envelope.push_back({lastOffset, lastGain});
        }
        fitEnvelopePointLimit(event.envelope);
    }
    if (!applyCandidate(std::move(candidate))) return false;
    next_event_id_ = candidateId;
    if (selectionTrack < 0) selection_ = Selection{0, timeline_.totalFrames()};
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
        if ((selection_->trackIndex < 0 || event.trackIndex == selection_->trackIndex)
            && event.timelineStart >= selection_->start && end <= selection_->end) {
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
        if ((selection_->trackIndex < 0 || event.trackIndex == selection_->trackIndex)
            && event.timelineStart >= selection_->start && end <= selection_->end) {
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
        if ((selection_->trackIndex < 0 || event.trackIndex == selection_->trackIndex)
            && event.timelineStart >= selection_->start && end <= selection_->end) {
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

bool AudioDocument::setEventFadeIn(const EventId id,
                                   const SampleFrame frames)
{
    std::vector<AudioEvent> candidate = timeline_.snapshot().events;
    const auto event = std::find_if(candidate.begin(), candidate.end(),
        [id](const AudioEvent& item) { return item.id == id; });
    if (event == candidate.end()) return false;
    event->fadeIn = frames;
    return isValid(*event) && applyCandidate(std::move(candidate));
}

bool AudioDocument::setEventFadeCurve(const EventId id, const bool fadeIn,
                                      const FadeCurve curve)
{
    if (!isSupportedFadeCurve(curve)) return false;
    std::vector<AudioEvent> candidate = timeline_.snapshot().events;
    const auto event = std::find_if(candidate.begin(), candidate.end(),
        [id](const AudioEvent& item) { return item.id == id; });
    if (event == candidate.end()) return false;
    FadeCurve& current = fadeIn ? event->fadeInCurve : event->fadeOutCurve;
    if (current == curve) return false;
    current = curve;
    return isValid(*event) && applyCandidate(std::move(candidate));
}

bool AudioDocument::addEnvelopePoint(const EventId id,
                                     const SampleFrame offset,
                                     const float gain)
{
    if (!std::isfinite(gain)) return false;
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
    event->envelope.insert(
        point, EnvelopePoint{offset, std::clamp(gain, 0.0F, 2.0F)});
    return isValid(*event) && applyCandidate(std::move(candidate));
}

bool AudioDocument::setEventGain(const EventId id, const float gain)
{
    if (!std::isfinite(gain)) return false;
    std::vector<AudioEvent> candidate = timeline_.snapshot().events;
    const auto event = std::find_if(candidate.begin(), candidate.end(),
        [id](const AudioEvent& item) { return item.id == id; });
    if (event == candidate.end()) return false;
    const float bounded = std::clamp(gain, 0.0F, 2.0F);
    if (event->gain == bounded) return false;
    event->gain = bounded;
    return isValid(*event) && applyCandidate(std::move(candidate));
}

bool AudioDocument::moveEnvelopePoint(const EventId id,
                                      const SampleFrame originalOffset,
                                      const SampleFrame offset,
                                      const float gain)
{
    if (!std::isfinite(gain)) return false;
    std::vector<AudioEvent> candidate = timeline_.snapshot().events;
    const auto event = std::find_if(candidate.begin(), candidate.end(),
        [id](const AudioEvent& item) { return item.id == id; });
    if (event == candidate.end()) return false;
    const auto point = std::find_if(event->envelope.begin(), event->envelope.end(),
        [originalOffset](const EnvelopePoint& item) {
            return item.offset == originalOffset;
        });
    if (point == event->envelope.end()) return false;
    const SampleFrame boundedOffset = std::clamp<SampleFrame>(
        offset, 0, audibleFrames(*event) - 1);
    const float boundedGain = std::clamp(gain, 0.0F, 2.0F);
    if (point->offset == boundedOffset && point->gain == boundedGain) {
        return false;
    }
    if (std::any_of(event->envelope.cbegin(), event->envelope.cend(),
        [point, boundedOffset](const EnvelopePoint& item) {
            return &item != &*point && item.offset == boundedOffset;
        })) {
        return false;
    }
    point->offset = boundedOffset;
    point->gain = boundedGain;
    std::sort(event->envelope.begin(), event->envelope.end(),
        [](const EnvelopePoint& left, const EnvelopePoint& right) {
            return left.offset < right.offset;
        });
    return isValid(*event) && applyCandidate(std::move(candidate));
}

bool AudioDocument::removeEnvelopePoint(const EventId id,
                                        const SampleFrame offset)
{
    std::vector<AudioEvent> candidate = timeline_.snapshot().events;
    const auto event = std::find_if(candidate.begin(), candidate.end(),
        [id](const AudioEvent& item) { return item.id == id; });
    if (event == candidate.end()) return false;
    const auto point = std::find_if(event->envelope.begin(), event->envelope.end(),
        [offset](const EnvelopePoint& item) { return item.offset == offset; });
    if (point == event->envelope.end()) return false;
    event->envelope.erase(point);
    return applyCandidate(std::move(candidate));
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
    if (!splitSelectionBoundaries(candidate, candidateId)) return false;
    candidate.erase(std::remove_if(candidate.begin(), candidate.end(),
        [this](const AudioEvent& event) {
            const SampleFrame end = event.timelineStart + audibleFrames(event);
            return (selection_->trackIndex < 0 || event.trackIndex == selection_->trackIndex)
                && event.timelineStart >= selection_->start && end <= selection_->end;
        }), candidate.end());
    if (!applyCandidate(std::move(candidate))) return false;
    clipboard_ = copied;
    next_event_id_ = candidateId;
    selection_.reset();
    return true;
}

bool AudioDocument::copyEvent(const EventId id)
{
    const TimelineSnapshot snapshot = timeline_.snapshot();
    const auto event = std::find_if(snapshot.events.cbegin(), snapshot.events.cend(),
        [id](const AudioEvent& item) { return item.id == id; });
    if (event == snapshot.events.cend()) return false;
    clipboard_ = {*event};
    return true;
}

bool AudioDocument::cutEvent(const EventId id)
{
    std::vector<AudioEvent> candidate = timeline_.snapshot().events;
    const auto event = std::find_if(candidate.cbegin(), candidate.cend(),
        [id](const AudioEvent& item) { return item.id == id; });
    if (event == candidate.cend()) return false;
    const AudioEvent copied = *event;
    candidate.erase(event);
    if (!applyCandidate(std::move(candidate))) return false;
    clipboard_ = {copied};
    return true;
}

bool AudioDocument::deleteEvent(const EventId id)
{
    std::vector<AudioEvent> candidate = timeline_.snapshot().events;
    const auto event = std::find_if(candidate.cbegin(), candidate.cend(),
        [id](const AudioEvent& item) { return item.id == id; });
    if (event == candidate.cend()) return false;
    candidate.erase(event);
    return applyCandidate(std::move(candidate));
}

bool AudioDocument::silenceEvent(const EventId id)
{
    std::vector<AudioEvent> candidate = timeline_.snapshot().events;
    const auto event = std::find_if(candidate.begin(), candidate.end(),
        [id](const AudioEvent& item) { return item.id == id; });
    if (event == candidate.end() || event->mute) return false;
    event->mute = true;
    return applyCandidate(std::move(candidate));
}

bool AudioDocument::fadeEvent(const EventId id, const bool fadeIn)
{
    std::vector<AudioEvent> candidate = timeline_.snapshot().events;
    const auto event = std::find_if(candidate.begin(), candidate.end(),
        [id](const AudioEvent& item) { return item.id == id; });
    if (event == candidate.end()) return false;
    const SampleFrame frames = audibleFrames(*event);
    SampleFrame& fade = fadeIn ? event->fadeIn : event->fadeOut;
    const SampleFrame opposite = fadeIn ? event->fadeOut : event->fadeIn;
    const SampleFrame target = frames - opposite;
    if (fade == target) return false;
    fade = target;
    return applyCandidate(std::move(candidate));
}

bool AudioDocument::pasteAt(const SampleFrame playhead)
{
    return pasteAt(playhead, -1);
}

bool AudioDocument::pasteAt(const SampleFrame playhead, const int trackIndex)
{
    if (clipboard_.empty() || playhead < 0 || trackIndex < -1 || trackIndex >= static_cast<int>(kTrackCount)) return false;
    const SampleFrame origin = clipboard_.front().timelineStart;
    std::vector<AudioEvent> candidate = timeline_.snapshot().events;
    std::vector<AudioEvent> clones;
    clones.reserve(clipboard_.size());
    EventId candidateId = next_event_id_;
    constexpr EventId reserved = std::numeric_limits<EventId>::max();
    if (candidateId == reserved
        || clipboard_.size() > static_cast<std::size_t>(reserved - candidateId)) {
        return false;
    }
    SampleFrame clipboardEnd = origin;
    for (const AudioEvent& original : clipboard_) {
        const SampleFrame offset = original.timelineStart - origin;
        if (offset < 0 || playhead > std::numeric_limits<SampleFrame>::max() - offset) {
            return false;
        }
        AudioEvent clone = original;
        if (trackIndex >= 0) {
            clone.trackIndex += trackIndex - clipboard_.front().trackIndex;
            if (clone.trackIndex < 0 || clone.trackIndex >= static_cast<int>(kTrackCount)) return false;
        }
        clone.id = candidateId++;
        clone.timelineStart = playhead + offset;
        const SampleFrame cloneFrames = audibleFrames(clone);
        if (clone.timelineStart > std::numeric_limits<SampleFrame>::max()
                - cloneFrames
            || original.timelineStart > std::numeric_limits<SampleFrame>::max()
                - cloneFrames) {
            return false;
        }
        clipboardEnd = std::max(clipboardEnd,
            original.timelineStart + cloneFrames);
        clones.push_back(std::move(clone));
    }

    const bool collides = std::any_of(clones.cbegin(), clones.cend(),
        [&candidate](const AudioEvent& clone) {
            const SampleFrame cloneEnd = clone.timelineStart + audibleFrames(clone);
            return std::any_of(candidate.cbegin(), candidate.cend(),
                [clone, cloneEnd](const AudioEvent& existing) {
                    const SampleFrame existingEnd = existing.timelineStart
                        + audibleFrames(existing);
                    return clone.trackIndex == existing.trackIndex && clone.timelineStart < existingEnd
                        && existing.timelineStart < cloneEnd;
                });
        });
    if (collides) {
        const SampleFrame clipboardFrames = clipboardEnd - origin;
        if (clipboardFrames <= 0
            || playhead > std::numeric_limits<SampleFrame>::max()
                - clipboardFrames) {
            return false;
        }
        std::array<bool, kTrackCount> splitTracks{};
        for (const auto& clone : clones) {
            auto& split = splitTracks[static_cast<std::size_t>(clone.trackIndex)];
            if (!split && !splitAtFrame(candidate, playhead, candidateId, clone.trackIndex)) return false;
            split = true;
        }
        for (AudioEvent& event : candidate) {
            if (std::none_of(clones.begin(), clones.end(), [&event](const AudioEvent& clone) { return clone.trackIndex == event.trackIndex; })) continue;
            if (event.timelineStart < playhead) continue;
            if (event.timelineStart > std::numeric_limits<SampleFrame>::max()
                    - clipboardFrames) {
                return false;
            }
            event.timelineStart += clipboardFrames;
        }
    }

    candidate.insert(candidate.end(), std::make_move_iterator(clones.begin()),
                     std::make_move_iterator(clones.end()));
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
    return left.trackIndex == right.trackIndex && left.timelineSampleRate == right.timelineSampleRate
        && left.source == right.source && left.gain == right.gain
        && left.fadeIn == right.fadeIn && left.fadeOut == right.fadeOut
        && left.fadeInCurve == right.fadeInCurve
        && left.fadeOutCurve == right.fadeOutCurve
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
            return (replacement.trackIndex < 0 || event.trackIndex == replacement.trackIndex)
                && event.timelineStart >= replacement.start
                && end <= replacement.end;
        }), candidate.end());
    auto shared = std::make_shared<const AudioSource>(std::move(source));
    candidate.push_back(AudioEvent{candidateId, shared, 0,
                                   shared->total_frames, replacement.start,
                                   1.0F, 0, 0, 1.0, 0, false, {}});
    candidate.back().trackIndex = std::max(0, replacement.trackIndex);
    if (!applyCandidate(std::move(candidate))) return false;
    next_event_id_ = candidateId + 1;
    selection_.reset();
    return true;
}

bool AudioDocument::insertSourceAtCursor(AudioSource source,
                                         const SampleFrame cursor)
{
    return insertSource(std::move(source), cursor, 0);
}

bool AudioDocument::replaceEventWithSource(EventId id, AudioSource source,
    SampleFrame start, SampleFrame end)
{
    auto state = timeline_.snapshot();
    const auto found = std::find_if(state.events.begin(), state.events.end(),
        [id](const AudioEvent& event) { return event.id == id; });
    if (found == state.events.end() || start < found->timelineStart || end <= start
        || end > found->timelineStart + audibleFrames(*found)
        || source.sample_rate == 0 || source.channels == 0 || source.total_frames <= 0
        || sourceToProjectFrames(source.total_frames, source.sample_rate, state.sampleRate) != end - start)
        return false;
    const AudioEvent original = *found;
    const auto sourceBegin = original.sourceStart + sourceOffsetAt(original, start - original.timelineStart);
    const auto sourceEnd = original.sourceStart + sourceOffsetAt(original, end - original.timelineStart);
    AudioEvent processed = original;
    if (!reframeEvent(processed, sourceBegin, sourceEnd, start)) return false;
    processed.source = std::make_shared<const AudioSource>(std::move(source));
    processed.sourceStart = 0; processed.sourceEnd = processed.source->total_frames;
    if (!isValid(processed)) return false;
    auto candidateId = next_event_id_;
    std::vector<AudioEvent> pieces;
    if (start > original.timelineStart) {
        if (candidateId == std::numeric_limits<EventId>::max()) return false;
        auto left = original;
        if (!reframeEvent(left, original.sourceStart, sourceBegin, original.timelineStart)) return false;
        left.id = candidateId++; pieces.push_back(std::move(left));
    }
    pieces.push_back(std::move(processed));
    if (end < original.timelineStart + audibleFrames(original)) {
        if (candidateId == std::numeric_limits<EventId>::max()) return false;
        auto right = original;
        if (!reframeEvent(right, sourceEnd, original.sourceEnd, end)) return false;
        right.id = candidateId++; pieces.push_back(std::move(right));
    }
    state.events.erase(found);
    state.events.insert(state.events.end(), pieces.begin(), pieces.end());
    if (!applyCandidate(std::move(state.events))) return false;
    next_event_id_ = candidateId;
    return true;
}

bool AudioDocument::overwriteSource(AudioSource source, const SampleFrame cursor, const int trackIndex)
{
    const auto current = timeline_.snapshot();
    if (cursor < 0 || trackIndex < 0 || trackIndex >= 6 || source.sample_rate == 0
        || source.channels == 0 || source.total_frames <= 0 || current.sampleRate == 0) return false;
    const auto duration = sourceToProjectFrames(source.total_frames, source.sample_rate, current.sampleRate);
    if (duration <= 0 || cursor > std::numeric_limits<SampleFrame>::max() - duration) return false;
    const auto end = cursor + duration;
    auto candidate = current.events;
    auto nextId = next_event_id_;
    if (!splitAtFrame(candidate, cursor, nextId, trackIndex)
        || !splitAtFrame(candidate, end, nextId, trackIndex)
        || nextId == std::numeric_limits<EventId>::max()) return false;
    candidate.erase(std::remove_if(candidate.begin(), candidate.end(), [&](const AudioEvent& event) {
        return event.trackIndex == trackIndex && event.timelineStart >= cursor && event.timelineStart < end;
    }), candidate.end());
    auto shared = std::make_shared<const AudioSource>(std::move(source));
    AudioEvent take{nextId++, shared, 0, shared->total_frames, cursor};
    take.trackIndex = trackIndex;
    candidate.push_back(std::move(take));
    if (!applyCandidate(std::move(candidate))) return false;
    next_event_id_ = nextId;
    return true;
}

bool AudioDocument::insertSource(AudioSource source, const SampleFrame cursor, const int trackIndex)
{
    if (cursor < 0 || source.sample_rate == 0 || source.channels == 0
        || source.total_frames <= 0
        || next_event_id_ == std::numeric_limits<EventId>::max()) return false;
    const TimelineSnapshot current = timeline_.snapshot();
    auto shared = std::make_shared<const AudioSource>(std::move(source));
    std::vector<AudioEvent> candidate = current.events;
    candidate.push_back(AudioEvent{next_event_id_, shared, 0,
                                   shared->total_frames, cursor,
                                   1.0F, 0, 0, 1.0, 0, false, {}});
    candidate.back().trackIndex = trackIndex;
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

bool AudioDocument::applySnapshot(TimelineSnapshot candidate)
{
    auto command = TimelineEditCommand::fromSnapshot(timeline_, std::move(candidate));
    if (!command || !history_.executeAndPush(
        std::make_unique<TimelineEditCommand>(std::move(*command)), timeline_)) return false;
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
