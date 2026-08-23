#pragma once

#include <QByteArray>
#include <QColor>
#include <QQuickItem>

class QSGGeometry;

class TrackWaveformThumbnailItem : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QByteArray peaks READ peaks WRITE setPeaks NOTIFY peaksChanged)
    Q_PROPERTY(QColor waveformColor READ waveformColor WRITE setWaveformColor
                   NOTIFY waveformColorChanged)

public:
    static constexpr int kPeakCount = 2048;
    static constexpr int kPeakDataSize = kPeakCount * 2;

    explicit TrackWaveformThumbnailItem(QQuickItem* parent = nullptr);

    QByteArray peaks() const;
    void setPeaks(const QByteArray& peaks);
    QColor waveformColor() const;
    void setWaveformColor(const QColor& color);

signals:
    void peaksChanged();
    void waveformColorChanged();

protected:
    void geometryChange(const QRectF& newGeometry,
                        const QRectF& oldGeometry) override;
    QSGNode* updatePaintNode(QSGNode* oldNode,
                             UpdatePaintNodeData* data) override;

private:
    void markGeometryDirty();
    void markColorDirty();
    void rebuildGeometry(QSGGeometry* geometry);

    QByteArray peaks_;
    QColor waveformColor_ = Qt::white;
    bool geometryDirty_ = true;
    bool colorDirty_ = true;
};
