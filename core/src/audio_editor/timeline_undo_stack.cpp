#include "timeline_undo_stack.hpp"

#include <utility>

namespace agplayer::editor {

TimelineUndoStack::TimelineUndoStack(const TimelineHistoryLimits limits) noexcept
    : limits_(limits)
{
}

bool TimelineUndoStack::executeAndPush(
    std::unique_ptr<TimelineEditCommand> command, EventTimeline& timeline)
{
    if (!command) return false;
    const bool coalescing = active_event_ && active_has_item_ && !undo_.empty()
        && undo_.back()->canCoalesceWith(*command, *active_event_);
    if (!command->execute(timeline)) return false;

    clearRedo();
    if (coalescing) {
        retained_bytes_ -= undo_.back()->byteCost();
        undo_.back()->coalesceWith(std::move(*command));
        retained_bytes_ += undo_.back()->byteCost();
    } else {
        const std::size_t bytes = command->byteCost();
        if (limits_.maxCommands == 0 || bytes > limits_.maxBytes) {
            clear();
            return true;
        }
        retained_bytes_ += bytes;
        undo_.push_back(std::move(command));
        active_has_item_ = active_event_.has_value();
    }
    if (!undo_.empty() && undo_.back()->byteCost() > limits_.maxBytes) {
        clear();
        return true;
    }
    enforceLimits();
    return true;
}

bool TimelineUndoStack::undo(EventTimeline& timeline)
{
    if (undo_.empty() || !undo_.back()->undo(timeline)) return false;
    redo_.push_back(std::move(undo_.back()));
    undo_.pop_back();
    endCoalescedEdit();
    return true;
}

bool TimelineUndoStack::redo(EventTimeline& timeline)
{
    if (redo_.empty() || !redo_.back()->execute(timeline)) return false;
    undo_.push_back(std::move(redo_.back()));
    redo_.pop_back();
    endCoalescedEdit();
    return true;
}

void TimelineUndoStack::beginCoalescedEdit(const EventId eventId) noexcept
{
    active_event_ = eventId;
    active_has_item_ = false;
}

void TimelineUndoStack::endCoalescedEdit() noexcept
{
    active_event_.reset();
    active_has_item_ = false;
}

void TimelineUndoStack::clear() noexcept
{
    undo_.clear();
    redo_.clear();
    retained_bytes_ = 0;
    endCoalescedEdit();
}

void TimelineUndoStack::clearRedo() noexcept
{
    for (const auto& command : redo_) retained_bytes_ -= command->byteCost();
    redo_.clear();
}

void TimelineUndoStack::enforceLimits() noexcept
{
    while (!undo_.empty()
           && (undo_.size() > limits_.maxCommands
               || retained_bytes_ > limits_.maxBytes)) {
        retained_bytes_ -= undo_.front()->byteCost();
        undo_.pop_front();
    }
}

} // namespace agplayer::editor
