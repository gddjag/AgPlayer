#include "audio_document.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace agplayer::editor {
namespace {

bool sameSource(const std::shared_ptr<const AudioSource>& left,
                const std::shared_ptr<const AudioSource>& right) noexcept
{
    if (!left || !right) {
        return left == right;
    }
    return left->path == right->path
        && left->sample_rate == right->sample_rate
        && left->channels == right->channels
        && left->total_frames == right->total_frames;
}

SampleFrame spanFrames(const std::vector<AudioSpan>& spans) noexcept
{
    SampleFrame result = 0;
    for (const auto& span : spans) {
        if (span.frame_count > std::numeric_limits<SampleFrame>::max() - result) {
            return std::numeric_limits<SampleFrame>::max();
        }
        result += span.frame_count;
    }
    return result;
}

} // namespace

EditCommand::EditCommand(const Type type, const SampleFrame frame,
                         const SampleFrame frame_count,
                         const float linear_gain) noexcept
    : type_(type), frame_(frame), frame_count_(frame_count),
      linear_gain_(linear_gain)
{
}

EditCommand EditCommand::copySelection() noexcept
{
    return EditCommand(Type::CopySelection);
}
EditCommand EditCommand::cutSelection() noexcept
{
    return EditCommand(Type::CutSelection);
}
EditCommand EditCommand::deleteSelection() noexcept
{
    return EditCommand(Type::DeleteSelection);
}
EditCommand EditCommand::cropToSelection() noexcept
{
    return EditCommand(Type::CropToSelection);
}
EditCommand EditCommand::silenceSelection() noexcept
{
    return EditCommand(Type::SilenceSelection);
}
EditCommand EditCommand::fadeIn() noexcept { return EditCommand(Type::FadeIn); }
EditCommand EditCommand::fadeOut() noexcept { return EditCommand(Type::FadeOut); }
EditCommand EditCommand::gain(const float linear_gain) noexcept
{
    return EditCommand(Type::Gain, 0, 0, linear_gain);
}
EditCommand EditCommand::pasteAt(const SampleFrame frame) noexcept
{
    return EditCommand(Type::PasteAt, frame);
}
EditCommand EditCommand::insertSilence(const SampleFrame frame,
                                      const SampleFrame frame_count) noexcept
{
    return EditCommand(Type::InsertSilence, frame, frame_count);
}

bool operator==(const AudioSpan& left, const AudioSpan& right) noexcept
{
    return sameSource(left.source, right.source)
        && left.source_start == right.source_start
        && left.frame_count == right.frame_count
        && left.silent == right.silent
        && left.gain_start == right.gain_start
        && left.gain_end == right.gain_end;
}

bool operator==(const Selection& left, const Selection& right) noexcept
{
    return left.start == right.start && left.end == right.end;
}

bool operator==(const Marker& left, const Marker& right) noexcept
{
    return left.name == right.name && left.frame == right.frame;
}

bool operator==(const DocumentSnapshot& left,
                const DocumentSnapshot& right) noexcept
{
    return left.spans == right.spans
        && left.markers == right.markers
        && left.selection == right.selection;
}

AudioDocument AudioDocument::fromSource(AudioSource source)
{
    AudioDocument document;
    if (source.sample_rate == 0 || source.channels == 0
        || source.total_frames <= 0) {
        return document;
    }
    auto shared_source = std::make_shared<const AudioSource>(std::move(source));
    document.state_.spans.push_back(
        AudioSpan{shared_source, 0, shared_source->total_frames});
    return document;
}

bool AudioDocument::setSelection(const Selection selection) noexcept
{
    if (!selection.valid() || selection.end > totalFrames()) {
        return false;
    }
    state_.selection = selection;
    return true;
}

bool AudioDocument::clearSelection() noexcept
{
    const bool changed = state_.selection.has_value();
    state_.selection.reset();
    return changed;
}

bool AudioDocument::addMarker(Marker marker)
{
    if (marker.frame < 0 || marker.frame > totalFrames()) {
        return false;
    }
    const auto position = std::lower_bound(
        state_.markers.begin(), state_.markers.end(), marker.frame,
        [](const Marker& item, const SampleFrame frame) {
            return item.frame < frame;
        });
    state_.markers.insert(position, std::move(marker));
    return true;
}

SampleFrame AudioDocument::totalFrames() const noexcept
{
    return spanFrames(state_.spans);
}

bool AudioDocument::hasValidSelection() const noexcept
{
    return state_.selection.has_value() && state_.selection->valid()
        && state_.selection->end <= totalFrames();
}

bool AudioDocument::splitAt(std::vector<AudioSpan>& spans,
                            const SampleFrame frame) const
{
    const SampleFrame total = spanFrames(spans);
    if (frame < 0 || frame > total) {
        return false;
    }
    if (frame == 0 || frame == total) {
        return true;
    }
    SampleFrame cursor = 0;
    for (auto iterator = spans.begin(); iterator != spans.end(); ++iterator) {
        const SampleFrame next = cursor + iterator->frame_count;
        if (frame == cursor || frame == next) {
            return true;
        }
        if (frame > cursor && frame < next) {
            const SampleFrame left_count = frame - cursor;
            AudioSpan right = *iterator;
            right.source_start += left_count;
            right.frame_count -= left_count;
            if (iterator->gain_start != iterator->gain_end) {
                const float ratio = static_cast<float>(left_count)
                    / static_cast<float>(iterator->frame_count);
                const float middle = iterator->gain_start
                    + (iterator->gain_end - iterator->gain_start) * ratio;
                right.gain_start = middle;
                iterator->gain_end = middle;
            }
            iterator->frame_count = left_count;
            spans.insert(std::next(iterator), std::move(right));
            return true;
        }
        cursor = next;
    }
    return false;
}

std::vector<AudioSpan> AudioDocument::selectedSpans(
    std::vector<AudioSpan> spans) const
{
    if (!hasValidSelection()) {
        return {};
    }
    const Selection selection = *state_.selection;
    if (!splitAt(spans, selection.start) || !splitAt(spans, selection.end)) {
        return {};
    }
    std::vector<AudioSpan> result;
    SampleFrame cursor = 0;
    for (const auto& span : spans) {
        const SampleFrame next = cursor + span.frame_count;
        if (cursor >= selection.start && next <= selection.end) {
            result.push_back(span);
        }
        cursor = next;
    }
    return result;
}

bool AudioDocument::deleteSelectedRange(State& candidate) const
{
    if (!hasValidSelection()) {
        return false;
    }
    const Selection selection = *state_.selection;
    if (!splitAt(candidate.spans, selection.start)
        || !splitAt(candidate.spans, selection.end)) {
        return false;
    }
    SampleFrame cursor = 0;
    candidate.spans.erase(
        std::remove_if(candidate.spans.begin(), candidate.spans.end(),
            [&](const AudioSpan& span) {
                const SampleFrame next = cursor + span.frame_count;
                const bool selected = cursor >= selection.start
                    && next <= selection.end;
                cursor = next;
                return selected;
            }),
        candidate.spans.end());
    const SampleFrame removed = selection.end - selection.start;
    candidate.markers.erase(
        std::remove_if(candidate.markers.begin(), candidate.markers.end(),
            [&](Marker& marker) {
                if (marker.frame >= selection.start && marker.frame < selection.end) {
                    return true;
                }
                if (marker.frame >= selection.end) {
                    marker.frame -= removed;
                }
                return false;
            }),
        candidate.markers.end());
    candidate.selection.reset();
    return true;
}

void AudioDocument::commit(State candidate)
{
    undo_stack_.push_back(state_);
    state_ = std::move(candidate);
    redo_stack_.clear();
}

bool AudioDocument::apply(const EditCommand& command)
{
    if (command.type() == EditCommand::Type::CopySelection) {
        auto copied = selectedSpans(state_.spans);
        if (copied.empty()) {
            return false;
        }
        clipboard_ = std::move(copied);
        return true;
    }

    State candidate = state_;
    switch (command.type()) {
    case EditCommand::Type::CutSelection:
        clipboard_ = selectedSpans(state_.spans);
        if (clipboard_.empty() || !deleteSelectedRange(candidate)) {
            clipboard_.clear();
            return false;
        }
        break;
    case EditCommand::Type::DeleteSelection:
        if (!deleteSelectedRange(candidate)) {
            return false;
        }
        break;
    case EditCommand::Type::CropToSelection: {
        auto selected = selectedSpans(candidate.spans);
        if (selected.empty()) {
            return false;
        }
        const Selection selection = *state_.selection;
        candidate.spans = std::move(selected);
        candidate.markers.erase(
            std::remove_if(candidate.markers.begin(), candidate.markers.end(),
                [&](Marker& marker) {
                    if (marker.frame < selection.start || marker.frame >= selection.end) {
                        return true;
                    }
                    marker.frame -= selection.start;
                    return false;
                }), candidate.markers.end());
        candidate.selection.reset();
        break;
    }
    case EditCommand::Type::SilenceSelection:
    case EditCommand::Type::FadeIn:
    case EditCommand::Type::FadeOut:
    case EditCommand::Type::Gain: {
        const std::optional<Selection> processing_range = hasValidSelection()
            ? state_.selection
            : (command.type() == EditCommand::Type::Gain && totalFrames() > 0
                ? std::optional<Selection>{Selection{0, totalFrames()}}
                : std::nullopt);
        if (!processing_range
            || !splitAt(candidate.spans, processing_range->start)
            || !splitAt(candidate.spans, processing_range->end)) {
            return false;
        }
        SampleFrame cursor = 0;
        const SampleFrame selection_frames = processing_range->end
            - processing_range->start;
        for (auto& span : candidate.spans) {
            const SampleFrame next = cursor + span.frame_count;
            if (cursor >= processing_range->start
                && next <= processing_range->end) {
                if (command.type() == EditCommand::Type::SilenceSelection) {
                    span.source.reset();
                    span.source_start = 0;
                    span.silent = true;
                    span.gain_start = 0.0F;
                    span.gain_end = 0.0F;
                } else if (command.type() == EditCommand::Type::FadeIn) {
                    span.gain_start = static_cast<float>(
                        cursor - processing_range->start)
                        / static_cast<float>(selection_frames);
                    span.gain_end = static_cast<float>(
                        next - processing_range->start)
                        / static_cast<float>(selection_frames);
                } else if (command.type() == EditCommand::Type::FadeOut) {
                    span.gain_start = 1.0F - static_cast<float>(
                        cursor - processing_range->start)
                        / static_cast<float>(selection_frames);
                    span.gain_end = 1.0F - static_cast<float>(
                        next - processing_range->start)
                        / static_cast<float>(selection_frames);
                } else {
                    if (!std::isfinite(command.linearGain())
                        || command.linearGain() < 0.0F) {
                        return false;
                    }
                    span.gain_start *= command.linearGain();
                    span.gain_end *= command.linearGain();
                }
            }
            cursor = next;
        }
        break;
    }
    case EditCommand::Type::PasteAt: {
        if (clipboard_.empty() || !splitAt(candidate.spans, command.frame())) {
            return false;
        }
        SampleFrame cursor = 0;
        auto position = candidate.spans.end();
        for (auto iterator = candidate.spans.begin(); iterator != candidate.spans.end();
             ++iterator) {
            if (cursor == command.frame()) {
                position = iterator;
                break;
            }
            cursor += iterator->frame_count;
        }
        const SampleFrame inserted = spanFrames(clipboard_);
        candidate.spans.insert(position, clipboard_.begin(), clipboard_.end());
        for (auto& marker : candidate.markers) {
            if (marker.frame >= command.frame()) {
                marker.frame += inserted;
            }
        }
        candidate.selection.reset();
        break;
    }
    case EditCommand::Type::InsertSilence: {
        if (command.frameCount() <= 0
            || command.frameCount() > std::numeric_limits<SampleFrame>::max()
                - totalFrames()
            || !splitAt(candidate.spans, command.frame())) {
            return false;
        }
        SampleFrame cursor = 0;
        auto position = candidate.spans.end();
        for (auto iterator = candidate.spans.begin(); iterator != candidate.spans.end();
             ++iterator) {
            if (cursor == command.frame()) {
                position = iterator;
                break;
            }
            cursor += iterator->frame_count;
        }
        candidate.spans.insert(position,
            AudioSpan{nullptr, 0, command.frameCount(), true, 0.0F, 0.0F});
        for (auto& marker : candidate.markers) {
            if (marker.frame >= command.frame()) {
                marker.frame += command.frameCount();
            }
        }
        candidate.selection.reset();
        break;
    }
    case EditCommand::Type::CopySelection:
        return false;
    }
    commit(std::move(candidate));
    return true;
}

bool AudioDocument::undo()
{
    if (undo_stack_.empty()) {
        return false;
    }
    redo_stack_.push_back(state_);
    state_ = std::move(undo_stack_.back());
    undo_stack_.pop_back();
    return true;
}

bool AudioDocument::redo()
{
    if (redo_stack_.empty()) {
        return false;
    }
    undo_stack_.push_back(state_);
    state_ = std::move(redo_stack_.back());
    redo_stack_.pop_back();
    return true;
}

} // namespace agplayer::editor
