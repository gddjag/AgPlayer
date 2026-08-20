#pragma once

#include "timeline_edit_command.hpp"

#include <cstddef>
#include <deque>
#include <memory>
#include <optional>

namespace agplayer::editor {

struct TimelineHistoryLimits final {
    std::size_t maxCommands{256};
    std::size_t maxBytes{1024U * 1024U};
};

class TimelineUndoStack final {
public:
    explicit TimelineUndoStack(TimelineHistoryLimits limits = {}) noexcept;

    [[nodiscard]] bool executeAndPush(
        std::unique_ptr<TimelineEditCommand> command, EventTimeline& timeline);
    [[nodiscard]] bool undo(EventTimeline& timeline);
    [[nodiscard]] bool redo(EventTimeline& timeline);

    void beginCoalescedEdit(EventId eventId) noexcept;
    void endCoalescedEdit() noexcept;
    void clear() noexcept;

    [[nodiscard]] bool canUndo() const noexcept { return !undo_.empty(); }
    [[nodiscard]] bool canRedo() const noexcept { return !redo_.empty(); }
    [[nodiscard]] std::size_t undoCount() const noexcept { return undo_.size(); }
    [[nodiscard]] std::size_t redoCount() const noexcept { return redo_.size(); }
    [[nodiscard]] std::size_t retainedBytes() const noexcept
    { return retained_bytes_; }

private:
    void clearRedo() noexcept;
    void enforceLimits() noexcept;

    TimelineHistoryLimits limits_;
    std::deque<std::unique_ptr<TimelineEditCommand>> undo_;
    std::deque<std::unique_ptr<TimelineEditCommand>> redo_;
    std::size_t retained_bytes_{};
    std::optional<EventId> active_event_;
    bool active_has_item_{};
};

} // namespace agplayer::editor
