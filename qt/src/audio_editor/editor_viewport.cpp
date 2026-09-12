#include "editor_viewport.hpp"

#include "audio_editor/time_pixel_mapper.hpp"

#include <algorithm>
#include <cmath>

using agplayer::editor::SampleFrame;
using agplayer::editor::TimePixelMapper;

EditorViewport::EditorViewport(QObject* parent) : QObject(parent) {}

qreal EditorViewport::overviewStartRatio() const noexcept
{
    return document_frames_ > 0
        ? static_cast<qreal>(visible_start_) / static_cast<qreal>(document_frames_)
        : 0.0;
}

qreal EditorViewport::overviewWidthRatio() const noexcept
{
    return document_frames_ > 0
        ? static_cast<qreal>(visibleFrameCount())
            / static_cast<qreal>(document_frames_)
        : 0.0;
}

void EditorViewport::setDocumentFrames(const qint64 frames) noexcept
{
    const qint64 normalized = std::max<qint64>(0, frames);
    if (normalized == document_frames_) {
        return;
    }
    document_frames_ = normalized;
    if (document_frames_ == 0) {
        visible_start_ = 0;
        visible_end_ = 0;
    } else if (visibleFrameCount() <= 0) {
        visible_start_ = 0;
        visible_end_ = document_frames_;
    } else {
        assignRange(visible_start_, std::min(visibleFrameCount(), document_frames_));
    }
    emit viewportChanged();
}

void EditorViewport::setViewportWidth(const qreal width) noexcept
{
    const qreal normalized = std::isfinite(width) ? std::max<qreal>(0.0, width) : 0.0;
    if (qFuzzyCompare(normalized, viewport_width_)) {
        return;
    }
    viewport_width_ = normalized;
    emit viewportChanged();
}

bool EditorViewport::setVisibleRange(const qint64 start, const qint64 end) noexcept
{
    if (start < 0 || end <= start || end > document_frames_) {
        return false;
    }
    if (start == visible_start_ && end == visible_end_) {
        return true;
    }
    visible_start_ = start;
    visible_end_ = end;
    emit viewportChanged();
    return true;
}

void EditorViewport::assignRange(const qint64 start, const qint64 count) noexcept
{
    if (document_frames_ <= 0 || count <= 0) {
        visible_start_ = 0;
        visible_end_ = 0;
        return;
    }
    const qint64 bounded_count = std::clamp<qint64>(count, 1, document_frames_);
    visible_start_ = std::clamp<qint64>(start, 0, document_frames_ - bounded_count);
    visible_end_ = visible_start_ + bounded_count;
}

void EditorViewport::moveOverviewWindow(const qreal startRatio) noexcept
{
    if (document_frames_ <= 0 || !std::isfinite(startRatio)) {
        return;
    }
    const qint64 count = visibleFrameCount();
    const qint64 start = static_cast<qint64>(std::llround(
        std::clamp<qreal>(startRatio, 0.0, 1.0)
        * static_cast<qreal>(document_frames_)));
    const qint64 old_start = visible_start_;
    assignRange(start, count);
    if (old_start != visible_start_) {
        emit viewportChanged();
    }
}

void EditorViewport::zoomAt(const qreal factor, const qreal anchorPixel) noexcept
{
    if (document_frames_ <= 0 || viewport_width_ <= 0.0
        || !std::isfinite(factor) || factor <= 0.0
        || !std::isfinite(anchorPixel)) {
        return;
    }
    const qint64 old_count = visibleFrameCount();
    const qint64 anchor_frame = frameAtPixel(anchorPixel);
    const qreal anchor_ratio = std::clamp(anchorPixel / viewport_width_, 0.0, 1.0);
    const qint64 new_count = std::clamp<qint64>(
        static_cast<qint64>(std::llround(static_cast<qreal>(old_count) / factor)),
        1, document_frames_);
    const qint64 start = anchor_frame - static_cast<qint64>(
        std::llround(anchor_ratio * static_cast<qreal>(new_count)));
    const qint64 old_start = visible_start_;
    assignRange(start, new_count);
    if (old_start != visible_start_ || old_count != visibleFrameCount()) {
        emit viewportChanged();
    }
}

void EditorViewport::panByPixels(const qreal pixelDelta) noexcept
{
    const qint64 count = visibleFrameCount();
    if (document_frames_ <= 0 || count <= 0 || viewport_width_ <= 0.0
        || !std::isfinite(pixelDelta) || qFuzzyIsNull(pixelDelta)) {
        return;
    }
    const long double requested = static_cast<long double>(visible_start_)
        + static_cast<long double>(pixelDelta)
            * static_cast<long double>(count)
            / static_cast<long double>(viewport_width_);
    const qint64 maximum_start = document_frames_ - count;
    const qint64 start = requested <= 0.0L ? 0
        : requested >= static_cast<long double>(maximum_start) ? maximum_start
        : static_cast<qint64>(std::llround(requested));
    if (start == visible_start_) {
        return;
    }
    assignRange(start, count);
    emit viewportChanged();
}

qreal EditorViewport::timelineContentWidth() const noexcept
{
    const qint64 count = visibleFrameCount();
    if (document_frames_ <= 0 || count <= 0 || viewport_width_ <= 0.0) {
        return 0.0;
    }
    return static_cast<qreal>(
        static_cast<long double>(document_frames_)
        * static_cast<long double>(viewport_width_)
        / static_cast<long double>(count));
}

qreal EditorViewport::scrollOffsetPixels() const noexcept
{
    const qint64 count = visibleFrameCount();
    if (visible_start_ <= 0 || count <= 0 || viewport_width_ <= 0.0) {
        return 0.0;
    }
    return static_cast<qreal>(
        static_cast<long double>(visible_start_)
        * static_cast<long double>(viewport_width_)
        / static_cast<long double>(count));
}

void EditorViewport::panToScrollOffset(const qreal pixelOffset) noexcept
{
    if (!std::isfinite(pixelOffset)) {
        return;
    }
    panByPixels(pixelOffset - scrollOffsetPixels());
}

qint64 EditorViewport::frameAtPixel(const qreal pixel) const noexcept
{
    if (visibleFrameCount() <= 0 || viewport_width_ <= 0.0) {
        return 0;
    }
    TimePixelMapper mapper{document_frames_, viewport_width_,
        static_cast<double>(visibleFrameCount()) / viewport_width_};
    mapper.setVisibleStart(visible_start_);
    return mapper.pixelToFrame(pixel);
}

qreal EditorViewport::pixelAtFrame(const qint64 frame) const noexcept
{
    if (visibleFrameCount() <= 0 || viewport_width_ <= 0.0) {
        return 0.0;
    }
    TimePixelMapper mapper{document_frames_, viewport_width_,
        static_cast<double>(visibleFrameCount()) / viewport_width_};
    mapper.setVisibleStart(visible_start_);
    return mapper.frameToPixel(static_cast<SampleFrame>(frame));
}
