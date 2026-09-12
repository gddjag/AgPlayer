#include "timeline_undo_stack.hpp"

#include <unordered_set>
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
        && undo_.back().command->canCoalesceWith(*command, *active_event_);
    if (!command->execute(timeline)) return false;

    clearRedo();
    if (coalescing) {
        retained_bytes_ -= undo_.back().command->byteCost();
        undo_.back().command->coalesceWith(std::move(*command));
        retained_bytes_ += undo_.back().command->byteCost();
        undo_.back().afterState = next_state_id_++;
        current_state_id_ = undo_.back().afterState;
    } else {
        const std::size_t bytes = command->byteCost();
        if (limits_.maxCommands == 0 || bytes > limits_.maxBytes) {
            clear();
            current_state_id_ = next_state_id_++;
            return true;
        }
        const std::uint64_t before = current_state_id_;
        const std::uint64_t after = next_state_id_++;
        retained_bytes_ += bytes;
        undo_.push_back({std::move(command), before, after});
        current_state_id_ = after;
        active_has_item_ = active_event_.has_value();
    }
    if (!undo_.empty()
        && undo_.back().command->byteCost() > limits_.maxBytes) {
        clear();
        return true;
    }
    enforceLimits();
    return true;
}

bool TimelineUndoStack::undo(EventTimeline& timeline)
{
    if (undo_.empty() || !undo_.back().command->undo(timeline)) return false;
    current_state_id_ = undo_.back().beforeState;
    redo_.push_back(std::move(undo_.back()));
    undo_.pop_back();
    endCoalescedEdit();
    return true;
}

bool TimelineUndoStack::redo(EventTimeline& timeline)
{
    if (redo_.empty() || !redo_.back().command->execute(timeline)) return false;
    current_state_id_ = redo_.back().afterState;
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

std::vector<std::shared_ptr<const AudioSource>> TimelineUndoStack::retainedSources() const
{
    std::vector<std::shared_ptr<const AudioSource>> result;
    std::unordered_set<const AudioSource*> seen;
    const auto append = [&result, &seen](const std::deque<Entry>& entries) {
        for (const Entry& entry : entries) {
            for (const auto& source : entry.command->referencedSources()) {
                if (seen.insert(source.get()).second) result.push_back(source);
            }
        }
    };
    append(undo_);
    append(redo_);
    return result;
}

void TimelineUndoStack::clearRedo() noexcept
{
    for (const auto& entry : redo_) {
        retained_bytes_ -= entry.command->byteCost();
    }
    redo_.clear();
}

void TimelineUndoStack::enforceLimits() noexcept
{
    while (!undo_.empty()
           && (undo_.size() > limits_.maxCommands
               || retained_bytes_ > limits_.maxBytes)) {
        retained_bytes_ -= undo_.front().command->byteCost();
        undo_.pop_front();
    }
}

} // namespace agplayer::editor
