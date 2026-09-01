#pragma once

#include <QByteArray>
#include <QColor>
#include <QQuickItem>
#include <QVariantList>

class QSGNode;

class TrackWaveformThumbnailItem : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QByteArray peaks READ peaks WRITE setPeaks NOTIFY peaksChanged)
    Q_PROPERTY(QColor waveformColor READ waveformColor WRITE setWaveformColor
                   NOTIFY waveformColorChanged)
    Q_PROPERTY(QByteArray spectralIndex READ spectralIndex WRITE setSpectralIndex
                   NOTIFY spectralIndexChanged)
    Q_PROPERTY(QVariantList spectralPalette READ spectralPalette
                   WRITE setSpectralPalette NOTIFY spectralPaletteChanged)

public:
    static constexpr int kPeakCount = 2048;
    static constexpr int kPeakDataSize = kPeakCount * 2;

    explicit TrackWaveformThumbnailItem(QQuickItem* parent = nullptr);

    QByteArray peaks() const;
    void setPeaks(const QByteArray& peaks);
    QColor waveformColor() const;
    void setWaveformColor(const QColor& color);
    QByteArray spectralIndex() const;
    void setSpectralIndex(const QByteArray& spectralIndex);
    QVariantList spectralPalette() const;
    void setSpectralPalette(const QVariantList& palette);

signals:
    void peaksChanged();
    void waveformColorChanged();
    void spectralIndexChanged();
    void spectralPaletteChanged();

protected:
    void geometryChange(const QRectF& newGeometry,
                        const QRectF& oldGeometry) override;
    QSGNode* updatePaintNode(QSGNode* oldNode,
                             UpdatePaintNodeData* data) override;

private:
    void markGeometryDirty();
    void markColorDirty();
    void rebuildGeometry(QSGNode* sceneNode);

    QByteArray peaks_;
    QColor waveformColor_ = Qt::white;
    QByteArray spectralIndex_;
    QVariantList spectralPalette_;
    QVector<QColor> spectralColors_;
    bool geometryDirty_ = true;
    bool colorDirty_ = true;
};
