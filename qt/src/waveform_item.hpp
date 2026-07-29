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
    Q_PROPERTY(QVariantMap layers READ layers WRITE setLayers NOTIFY layersChanged)
    Q_PROPERTY(qreal position READ position WRITE setPosition NOTIFY positionChanged)
    Q_PROPERTY(qreal duration READ duration WRITE setDuration NOTIFY durationChanged)
    Q_PROPERTY(QColor waveformColor READ waveformColor WRITE setWaveformColor
                   NOTIFY waveformColorChanged)
    Q_PROPERTY(int visualMode READ visualMode WRITE setVisualMode
                   NOTIFY visualModeChanged)
    Q_PROPERTY(QColor baseColor READ baseColor WRITE setBaseColor
                   NOTIFY baseColorChanged)
    Q_PROPERTY(QColor progressColor READ progressColor WRITE setProgressColor
                   NOTIFY progressColorChanged)
    Q_PROPERTY(QColor gradientStartColor READ gradientStartColor
                   WRITE setGradientStartColor NOTIFY gradientStartColorChanged)
    Q_PROPERTY(QColor gradientMiddleColor READ gradientMiddleColor
                   WRITE setGradientMiddleColor NOTIFY gradientMiddleColorChanged)
    Q_PROPERTY(QColor gradientEndColor READ gradientEndColor
                   WRITE setGradientEndColor NOTIFY gradientEndColorChanged)
    Q_PROPERTY(bool rgbProgress READ rgbProgress WRITE setRgbProgress
                   NOTIFY rgbProgressChanged)
    Q_PROPERTY(qreal amplitudeScale READ amplitudeScale WRITE setAmplitudeScale
                   NOTIFY amplitudeScaleChanged)
    Q_PROPERTY(qint64 hoverPosition READ hoverPosition NOTIFY hoverPositionChanged)
    Q_PROPERTY(double analysisProgress READ analysisProgress WRITE setAnalysisProgress
                   NOTIFY analysisProgressChanged)
    Q_PROPERTY(qreal density READ density WRITE setDensity NOTIFY densityChanged)
    Q_PROPERTY(qreal lineWidth READ lineWidth WRITE setLineWidth NOTIFY lineWidthChanged)

public:
    explicit WaveformItem(QQuickItem* parent = nullptr);

    QVariantList peaks() const;
    void setPeaks(const QVariantList& peaks);

    QVariantMap layers() const;
    void setLayers(const QVariantMap& layers);

    qreal position() const;
    void setPosition(qreal position);

    qreal duration() const;
    void setDuration(qreal duration);

    QColor waveformColor() const;
    void setWaveformColor(const QColor& color);
    int visualMode() const noexcept;
    void setVisualMode(int mode);
    QColor baseColor() const;
    void setBaseColor(const QColor& color);
    QColor progressColor() const;
    void setProgressColor(const QColor& color);
    QColor gradientStartColor() const;
    void setGradientStartColor(const QColor& color);
    QColor gradientMiddleColor() const;
    void setGradientMiddleColor(const QColor& color);
    QColor gradientEndColor() const;
    void setGradientEndColor(const QColor& color);
    bool rgbProgress() const noexcept;
    void setRgbProgress(bool value);
    qreal amplitudeScale() const noexcept;
    void setAmplitudeScale(qreal value);

    qint64 hoverPosition() const;

    double analysisProgress() const;
    void setAnalysisProgress(double progress);

    qreal density() const;
    void setDensity(qreal density);

    qreal lineWidth() const;
    void setLineWidth(qreal width);

    Q_INVOKABLE qint64 timeForX(qreal x) const;

    static constexpr int unplayedAlpha() noexcept { return 89; }

signals:
    void peaksChanged();
    void layersChanged();
    void positionChanged();
    void durationChanged();
    void waveformColorChanged();
    void visualModeChanged();
    void baseColorChanged();
    void progressColorChanged();
    void gradientStartColorChanged();
    void gradientMiddleColorChanged();
    void gradientEndColorChanged();
    void rgbProgressChanged();
    void amplitudeScaleChanged();
    void hoverPositionChanged();
    void analysisProgressChanged();
    void densityChanged();
    void lineWidthChanged();
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
    struct LayerSnapshot {
        std::vector<float> values;
    };

    struct PeakSnapshot {
        std::shared_ptr<const LayerSnapshot> mix;
        std::shared_ptr<const LayerSnapshot> bass;
        std::shared_ptr<const LayerSnapshot> mid;
        std::shared_ptr<const LayerSnapshot> high;
        std::uint64_t revision = 0;
    };

    void setHoverPosition(qint64 position);
    void normalizeLayerInput(QVariantList& normalized,
                             const QVariantList& input,
                             std::shared_ptr<LayerSnapshot>& snapshot);

    QVariantList peaks_;
    QVariantMap layers_;
    std::shared_ptr<const PeakSnapshot> peakSnapshot_;
    std::uint64_t nextRevision_ = 1;
    qint64 position_ = 0;
    qint64 duration_ = 0;
    QColor waveformColor_;
    int visualMode_ = -1;
    QColor baseColor_ = QColor(QStringLiteral("#e8edf4"));
    QColor progressColor_ = QColor(QStringLiteral("#ffdd00"));
    QColor gradientStartColor_ = QColor(QStringLiteral("#00d4ff"));
    QColor gradientMiddleColor_ = QColor(QStringLiteral("#7b2ff7"));
    QColor gradientEndColor_ = QColor(QStringLiteral("#e62e9b"));
    bool rgbProgress_ = true;
    qreal amplitudeScale_ = 1.0;
    qint64 hoverPosition_ = -1;
    double analysisProgress_ = 0.0;
    qreal density_ = 1.0;
    qreal lineWidth_ = 2.0;
    bool pointerPressed_ = false;
};
