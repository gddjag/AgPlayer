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
    Q_PROPERTY(int geometryRevision READ geometryRevision
                   NOTIFY geometryRevisionChanged)

public:
    static constexpr int kPeakCount = 128;

    explicit TrackWaveformThumbnailItem(QQuickItem* parent = nullptr);

    QByteArray peaks() const;
    void setPeaks(const QByteArray& peaks);
    QColor waveformColor() const;
    void setWaveformColor(const QColor& color);
    int geometryRevision() const noexcept;

signals:
    void peaksChanged();
    void waveformColorChanged();
    void geometryRevisionChanged();

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
    int geometryRevision_ = 0;
};
