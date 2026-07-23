#pragma once

#include <QColor>
#include <QQuickItem>
#include <QVariantList>

#include <cstdint>
#include <memory>
#include <vector>

class WaveformItem : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QVariantList peaks READ peaks WRITE setPeaks NOTIFY peaksChanged)
    Q_PROPERTY(qreal position READ position WRITE setPosition NOTIFY positionChanged)
    Q_PROPERTY(qreal duration READ duration WRITE setDuration NOTIFY durationChanged)
    Q_PROPERTY(QColor waveformColor READ waveformColor WRITE setWaveformColor
                   NOTIFY waveformColorChanged)
    Q_PROPERTY(qint64 hoverPosition READ hoverPosition NOTIFY hoverPositionChanged)
    Q_PROPERTY(double analysisProgress READ analysisProgress WRITE setAnalysisProgress
                   NOTIFY analysisProgressChanged)

public:
    explicit WaveformItem(QQuickItem* parent = nullptr);

    QVariantList peaks() const;
    void setPeaks(const QVariantList& peaks);

    qreal position() const;
    void setPosition(qreal position);

    qreal duration() const;
    void setDuration(qreal duration);

    QColor waveformColor() const;
    void setWaveformColor(const QColor& color);

    qint64 hoverPosition() const;

    double analysisProgress() const;
    void setAnalysisProgress(double progress);

    Q_INVOKABLE qint64 timeForX(qreal x) const;

    static constexpr int unplayedAlpha() noexcept { return 89; }

signals:
    void peaksChanged();
    void positionChanged();
    void durationChanged();
    void waveformColorChanged();
    void hoverPositionChanged();
    void analysisProgressChanged();
    void seekRequested(qint64 position);

protected:
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* data) override;
    void hoverMoveEvent(QHoverEvent* event) override;
    void hoverLeaveEvent(QHoverEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseUngrabEvent() override;

private:
    struct PeakSnapshot {
        std::vector<float> values;
        std::uint64_t revision = 0;
    };

    void setHoverPosition(qint64 position);

    QVariantList peaks_;
    std::shared_ptr<const PeakSnapshot> peakSnapshot_;
    std::uint64_t nextRevision_ = 1;
    qint64 position_ = 0;
    qint64 duration_ = 0;
    QColor waveformColor_;
    qint64 hoverPosition_ = -1;
    double analysisProgress_ = 0.0;
    bool pointerPressed_ = false;
};
