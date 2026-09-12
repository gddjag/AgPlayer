#pragma once

#include <QByteArray>
#include <QColor>
#include <QQuickItem>

class QSGNode;

class TrackWaveformThumbnailItem : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QByteArray peaks READ peaks WRITE setPeaks NOTIFY peaksChanged)
    Q_PROPERTY(QColor waveformColor READ waveformColor WRITE setWaveformColor
                   NOTIFY waveformColorChanged)
    Q_PROPERTY(QByteArray bass READ bass WRITE setBass NOTIFY bassChanged)
    Q_PROPERTY(QByteArray mid READ mid WRITE setMid NOTIFY midChanged)
    Q_PROPERTY(QByteArray high READ high WRITE setHigh NOTIFY highChanged)
    Q_PROPERTY(QColor lowColor READ lowColor WRITE setLowColor
                   NOTIFY lowColorChanged)
    Q_PROPERTY(QColor midColor READ midColor WRITE setMidColor
                   NOTIFY midColorChanged)
    Q_PROPERTY(QColor highColor READ highColor WRITE setHighColor
                   NOTIFY highColorChanged)

public:
    static constexpr int kPeakCount = 2048;
    static constexpr int kPeakDataSize = kPeakCount * 2;

    explicit TrackWaveformThumbnailItem(QQuickItem* parent = nullptr);

    QByteArray peaks() const;
    void setPeaks(const QByteArray& peaks);
    QColor waveformColor() const;
    void setWaveformColor(const QColor& color);
    QByteArray bass() const;
    void setBass(const QByteArray& bass);
    QByteArray mid() const;
    void setMid(const QByteArray& mid);
    QByteArray high() const;
    void setHigh(const QByteArray& high);
    QColor lowColor() const;
    void setLowColor(const QColor& color);
    QColor midColor() const;
    void setMidColor(const QColor& color);
    QColor highColor() const;
    void setHighColor(const QColor& color);

signals:
    void peaksChanged();
    void waveformColorChanged();
    void bassChanged();
    void midChanged();
    void highChanged();
    void lowColorChanged();
    void midColorChanged();
    void highColorChanged();

protected:
    void geometryChange(const QRectF& newGeometry,
                        const QRectF& oldGeometry) override;
    QSGNode* updatePaintNode(QSGNode* oldNode,
                             UpdatePaintNodeData* data) override;

private:
    void markGeometryDirty();
    void markColorDirty();
    void rebuildGeometry(QSGNode* sceneNode);
    void recolorGeometry(QSGNode* sceneNode);

    QByteArray peaks_;
    QColor waveformColor_ = Qt::white;
    QByteArray bass_;
    QByteArray mid_;
    QByteArray high_;
    QColor lowColor_ = QColor(QStringLiteral("#FF0000"));
    QColor midColor_ = QColor(QStringLiteral("#00FF00"));
    QColor highColor_ = QColor(QStringLiteral("#0000FF"));
    bool geometryDirty_ = true;
    bool colorDirty_ = true;
};
