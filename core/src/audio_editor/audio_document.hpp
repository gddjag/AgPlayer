#pragma once

#include "timeline_undo_stack.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace agplayer::editor {

struct Selection final {
    SampleFrame start{};
    SampleFrame end{};

    [[nodiscard]] bool valid() const noexcept
    {
        return start >= 0 && end > start;
    }
};

struct Marker final {
    std::string name;
    SampleFrame frame{};
};

[[nodiscard]] bool operator==(const Selection& left,
                              const Selection& right) noexcept;
[[nodiscard]] bool operator==(const Marker& left,
                              const Marker& right) noexcept;

// Rebuilds two contiguous views of one source around a new shared boundary.
// Both event envelopes are mapped onto their source coordinate before being
// reframed, so extending either side retains automation from the other side.
[[nodiscard]] bool reframeSharedBoundary(AudioEvent& left, AudioEvent& right,
                                         SampleFrame sourceBoundary);

class AudioDocument final {
public:
    [[nodiscard]] static AudioDocument fromSource(AudioSource source);
    [[nodiscard]] static AudioDocument fromEvents(std::vector<AudioEvent> events);

    bool setSelection(Selection selection) noexcept;
    bool clearSelection() noexcept;
    bool addMarker(Marker marker);
    bool renameMarker(std::size_t index, std::string name);
    bool removeMarker(std::size_t index);
    bool moveEvent(EventId id, SampleFrame timelineStart);
    bool trimEvent(EventId id, SampleFrame sourceStart, SampleFrame sourceEnd,
                   SampleFrame timelineStart);
    bool trimSharedBoundary(EventId leftId, EventId rightId,
                            SampleFrame sourceBoundary);
    bool splitEventAt(EventId id, SampleFrame frame);
    bool clearTimeline();
    bool deleteSelection();
    bool cropToSelection();
    bool silenceSelection();
    bool fadeIn();
    bool fadeOut();
    bool setEventFadeIn(EventId id, SampleFrame frames);
    bool setEventFadeOut(EventId id, SampleFrame frames);
    bool setEventFadeCurve(EventId id, bool fadeIn, FadeCurve curve);
    bool setEventGain(EventId id, float gain);
    bool addEnvelopePoint(EventId id, SampleFrame offset, float gain);
    bool moveEnvelopePoint(EventId id, SampleFrame originalOffset,
                           SampleFrame offset, float gain);
    bool removeEnvelopePoint(EventId id, SampleFrame offset);
    bool copySelection();
    bool cutSelection();
    bool copyEvent(EventId id);
    bool cutEvent(EventId id);
    bool deleteEvent(EventId id);
    bool silenceEvent(EventId id);
    bool fadeEvent(EventId id, bool fadeIn);
    bool pasteAt(SampleFrame playhead);
    bool duplicateEvent(EventId id, SampleFrame timelineStart);
    bool mergeEvents(EventId left, EventId right);
    bool replaceSelectionWithSource(AudioSource source);
    bool insertSourceAtCursor(AudioSource source, SampleFrame cursor);
    bool insertSource(AudioSource source, SampleFrame timelineStart);
    bool undo();
    bool redo();
    void beginCoalescedEdit(EventId id) noexcept
    { history_.beginCoalescedEdit(id); }
    void endCoalescedEdit() noexcept { history_.endCoalescedEdit(); }

    [[nodiscard]] bool canUndo() const noexcept { return history_.canUndo(); }
    [[nodiscard]] bool canRedo() const noexcept { return history_.canRedo(); }
    [[nodiscard]] std::uint64_t historyStateId() const noexcept
    { return history_.stateId(); }
    [[nodiscard]] bool hasClipboard() const noexcept { return !clipboard_.empty(); }
    [[nodiscard]] std::vector<std::shared_ptr<const AudioSource>> retainedSources() const;
    [[nodiscard]] SampleFrame totalFrames() const noexcept;
    [[nodiscard]] const std::vector<Marker>& markers() const noexcept
    {
        return markers_;
    }
    [[nodiscard]] const std::optional<Selection>& selection() const noexcept
    { return selection_; }
    [[nodiscard]] TimelineSnapshot timelineSnapshot() const
    { return timeline_.snapshot(); }

private:
    [[nodiscard]] bool hasValidSelection() const noexcept;
    [[nodiscard]] static bool splitAt(std::vector<AudioEvent>& events,
                                      EventId id, SampleFrame frame,
                                      EventId rightId);
    [[nodiscard]] static bool splitAtFrame(std::vector<AudioEvent>& events,
                                           SampleFrame frame, EventId& nextId);
    [[nodiscard]] static bool sameParameters(const AudioEvent& left,
                                             const AudioEvent& right) noexcept;
    [[nodiscard]] bool splitSelectionBoundaries(
        std::vector<AudioEvent>& events, EventId& nextId) const;
    [[nodiscard]] std::vector<AudioEvent> selectedEvents() const;
    [[nodiscard]] bool applyCandidate(std::vector<AudioEvent> candidate);
    void normalizeEditorState() noexcept;

    EventTimeline timeline_;
    TimelineUndoStack history_;
    std::vector<Marker> markers_;
    std::optional<Selection> selection_;
    std::vector<AudioEvent> clipboard_;
    EventId next_event_id_{1};
};

} // namespace agplayer::editor
