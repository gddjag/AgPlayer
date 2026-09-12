#pragma once

#include <QObject>

class EditorViewport final : public QObject {
    Q_OBJECT
    Q_PROPERTY(qint64 documentFrames READ documentFrames WRITE setDocumentFrames
                   NOTIFY viewportChanged)
    Q_PROPERTY(qint64 visibleStartFrame READ visibleStartFrame NOTIFY viewportChanged)
    Q_PROPERTY(qint64 visibleEndFrame READ visibleEndFrame NOTIFY viewportChanged)
    Q_PROPERTY(qint64 visibleFrameCount READ visibleFrameCount NOTIFY viewportChanged)
    Q_PROPERTY(qreal viewportWidth READ viewportWidth WRITE setViewportWidth
                   NOTIFY viewportChanged)
    Q_PROPERTY(qreal overviewStartRatio READ overviewStartRatio NOTIFY viewportChanged)
    Q_PROPERTY(qreal overviewWidthRatio READ overviewWidthRatio NOTIFY viewportChanged)

public:
    explicit EditorViewport(QObject* parent = nullptr);

    [[nodiscard]] qint64 documentFrames() const noexcept { return document_frames_; }
    [[nodiscard]] qint64 visibleStartFrame() const noexcept { return visible_start_; }
    [[nodiscard]] qint64 visibleEndFrame() const noexcept { return visible_end_; }
    [[nodiscard]] qint64 visibleFrameCount() const noexcept
    {
        return visible_end_ - visible_start_;
    }
    [[nodiscard]] qreal viewportWidth() const noexcept { return viewport_width_; }
    [[nodiscard]] qreal overviewStartRatio() const noexcept;
    [[nodiscard]] qreal overviewWidthRatio() const noexcept;

    void setDocumentFrames(qint64 frames) noexcept;
    Q_INVOKABLE void setViewportWidth(qreal width) noexcept;
    Q_INVOKABLE bool setVisibleRange(qint64 start, qint64 end) noexcept;
    Q_INVOKABLE void moveOverviewWindow(qreal startRatio) noexcept;
    Q_INVOKABLE void zoomAt(qreal factor, qreal anchorPixel) noexcept;
    Q_INVOKABLE void panByPixels(qreal pixelDelta) noexcept;
    Q_INVOKABLE qreal timelineContentWidth() const noexcept;
    Q_INVOKABLE qreal scrollOffsetPixels() const noexcept;
    Q_INVOKABLE void panToScrollOffset(qreal pixelOffset) noexcept;
    Q_INVOKABLE qint64 frameAtPixel(qreal pixel) const noexcept;
    Q_INVOKABLE qreal pixelAtFrame(qint64 frame) const noexcept;

signals:
    void viewportChanged();

private:
    void assignRange(qint64 start, qint64 count) noexcept;

    qint64 document_frames_{};
    qint64 visible_start_{};
    qint64 visible_end_{};
    qreal viewport_width_{};
};
