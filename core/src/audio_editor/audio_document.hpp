#pragma once

#include "event_timeline.hpp"

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

class AudioDocument final {
public:
    [[nodiscard]] static AudioDocument fromSource(AudioSource source);

    bool setSelection(Selection selection) noexcept;
    bool clearSelection() noexcept;
    bool addMarker(Marker marker);
    bool renameMarker(std::size_t index, std::string name);
    bool removeMarker(std::size_t index);
    bool moveEvent(EventId id, SampleFrame timelineStart);
    bool trimEvent(EventId id, SampleFrame sourceStart, SampleFrame sourceEnd,
                   SampleFrame timelineStart);
    bool splitEventAt(EventId id, SampleFrame frame);
    bool deleteSelection();
    bool copySelection();
    bool cutSelection();
    bool pasteAt(SampleFrame playhead);
    bool mergeEvents(EventId left, EventId right);
    bool insertSource(AudioSource source, SampleFrame timelineStart);

    [[nodiscard]] bool canUndo() const noexcept { return false; }
    [[nodiscard]] bool canRedo() const noexcept { return false; }
    [[nodiscard]] bool hasClipboard() const noexcept { return !clipboard_.empty(); }
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
    [[nodiscard]] std::vector<AudioEvent> selectedEvents() const;

    EventTimeline timeline_;
    std::vector<Marker> markers_;
    std::optional<Selection> selection_;
    std::vector<AudioEvent> clipboard_;
    EventId next_event_id_{1};
};

} // namespace agplayer::editor
