#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QQuickItem>
#include <QVariantList>

#include <cstdint>
#include <memory>
#include <vector>
#include "waveform_display_summary.hpp"

class WaveformItem : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QVariantList peaks READ peaks WRITE setPeaks NOTIFY peaksChanged)
    Q_PROPERTY(QVariantMap layers READ layers WRITE setLayers NOTIFY layersChanged)
    Q_PROPERTY(qreal position READ position WRITE setPosition NOTIFY positionChanged)
    Q_PROPERTY(qreal cursorPosition READ cursorPosition WRITE setCursorPosition
                   NOTIFY cursorPositionChanged)
    Q_PROPERTY(qreal duration READ duration WRITE setDuration NOTIFY durationChanged)
    Q_PROPERTY(qint64 visibleStartMs READ visibleStartMs WRITE setVisibleStartMs
                   NOTIFY visibleStartMsChanged)
    Q_PROPERTY(qint64 visibleEndMs READ visibleEndMs WRITE setVisibleEndMs
                   NOTIFY visibleEndMsChanged)
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
    Q_PROPERTY(QColor lowColor READ lowColor WRITE setLowColor
                   NOTIFY frequencyStyleChanged)
    Q_PROPERTY(QColor midColor READ midColor WRITE setMidColor
                   NOTIFY frequencyStyleChanged)
    Q_PROPERTY(QColor highColor READ highColor WRITE setHighColor
                   NOTIFY frequencyStyleChanged)
    Q_PROPERTY(qreal frequencyUnplayedOpacity READ frequencyUnplayedOpacity
                   WRITE setFrequencyUnplayedOpacity NOTIFY frequencyStyleChanged)
    Q_PROPERTY(bool rgbProgress READ rgbProgress WRITE setRgbProgress
                   NOTIFY rgbProgressChanged)
    Q_PROPERTY(qreal amplitudeScale READ amplitudeScale WRITE setAmplitudeScale
                   NOTIFY amplitudeScaleChanged)
    Q_PROPERTY(qint64 hoverPosition READ hoverPosition NOTIFY hoverPositionChanged)
    Q_PROPERTY(double analysisProgress READ analysisProgress WRITE setAnalysisProgress
                   NOTIFY analysisProgressChanged)
    Q_PROPERTY(qreal density READ density WRITE setDensity NOTIFY densityChanged)
    Q_PROPERTY(bool preserveSourcePeakDensity READ preserveSourcePeakDensity
                   WRITE setPreserveSourcePeakDensity
                   NOTIFY preserveSourcePeakDensityChanged)
    Q_PROPERTY(bool sourceAnchoredSampling READ sourceAnchoredSampling
                   WRITE setSourceAnchoredSampling
                   NOTIFY sourceAnchoredSamplingChanged)
    Q_PROPERTY(qreal lineWidth READ lineWidth WRITE setLineWidth NOTIFY lineWidthChanged)
    Q_PROPERTY(qreal renderWidth READ renderWidth NOTIFY renderWidthChanged)
    Q_PROPERTY(qreal waveformCursorX READ waveformCursorX NOTIFY waveformCursorXChanged)
    Q_PROPERTY(bool pointerInteractionEnabled READ pointerInteractionEnabled
                   WRITE setPointerInteractionEnabled
                   NOTIFY pointerInteractionEnabledChanged)
    Q_PROPERTY(qint64 totalSamples READ totalSamples NOTIFY layersChanged)
    Q_PROPERTY(qint64 sampleRate READ sampleRate NOTIFY layersChanged)
    Q_PROPERTY(qsizetype peakCount READ peakCount NOTIFY layersChanged)
    Q_PROPERTY(int spectrumBarCount READ spectrumBarCount CONSTANT)
    Q_PROPERTY(qreal spectrumBarWidth READ spectrumBarWidth CONSTANT)
    Q_PROPERTY(qreal spectrumBarGap READ spectrumBarGap CONSTANT)
    Q_PROPERTY(qreal spectrumMaxHeight READ spectrumMaxHeight CONSTANT)
    Q_PROPERTY(qreal spectrumAttackSeconds READ spectrumAttackSeconds CONSTANT)
    Q_PROPERTY(qreal spectrumDecaySeconds READ spectrumDecaySeconds CONSTANT)
    Q_PROPERTY(qreal spectrumPeakFallSeconds READ spectrumPeakFallSeconds CONSTANT)

public:
    explicit WaveformItem(QQuickItem* parent = nullptr);

    QVariantList peaks() const;
    void setPeaks(const QVariantList& peaks);

    QVariantMap layers() const;
    void setLayers(const QVariantMap& layers);

    qreal position() const;
    void setPosition(qreal position);
    qreal cursorPosition() const;
    void setCursorPosition(qreal position);

    qreal duration() const;
    void setDuration(qreal duration);
    qint64 visibleStartMs() const noexcept;
    void setVisibleStartMs(qint64 startMs);
    qint64 visibleEndMs() const noexcept;
    void setVisibleEndMs(qint64 endMs);

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
    QColor lowColor() const;
    void setLowColor(const QColor& color);
    QColor midColor() const;
    void setMidColor(const QColor& color);
    QColor highColor() const;
    void setHighColor(const QColor& color);
    qreal frequencyUnplayedOpacity() const noexcept;
    void setFrequencyUnplayedOpacity(qreal opacity);
    bool rgbProgress() const noexcept;
    void setRgbProgress(bool value);
    qreal amplitudeScale() const noexcept;
    void setAmplitudeScale(qreal value);

    qint64 hoverPosition() const;

    double analysisProgress() const;
    void setAnalysisProgress(double progress);

    qreal density() const;
    void setDensity(qreal density);
    bool preserveSourcePeakDensity() const noexcept;
    void setPreserveSourcePeakDensity(bool preserve);
    bool sourceAnchoredSampling() const noexcept;
    void setSourceAnchoredSampling(bool enabled);

    qreal lineWidth() const;
    void setLineWidth(qreal width);
    qreal renderWidth() const noexcept;
    qreal waveformCursorX() const noexcept;
    bool pointerInteractionEnabled() const noexcept;
    void setPointerInteractionEnabled(bool enabled);
    qint64 totalSamples() const noexcept;
    qint64 sampleRate() const noexcept;
    qsizetype peakCount() const noexcept;

    static constexpr int spectrumBarCount() noexcept { return 128; }
    static constexpr qreal spectrumBarWidth() noexcept { return 5.0; }
    static constexpr qreal spectrumBarGap() noexcept { return 2.0; }
    static constexpr qreal spectrumMaxHeight() noexcept { return 96.0; }
    static constexpr qreal spectrumAttackSeconds() noexcept { return 0.02; }
    static constexpr qreal spectrumDecaySeconds() noexcept { return 0.10; }
    static constexpr qreal spectrumPeakFallSeconds() noexcept { return 0.75; }

    Q_INVOKABLE qint64 timeForX(qreal x) const;
    Q_INVOKABLE qreal pixelForTime(qint64 positionMs) const;
    Q_INVOKABLE void zoomAt(qreal x, qreal factor);
    Q_INVOKABLE void setVisibleRange(qint64 startMs, qint64 endMs);
    Q_INVOKABLE void setHoverPositionForInteraction(qint64 position);

signals:
    void peaksChanged();
    void layersChanged();
    void positionChanged();
    void cursorPositionChanged();
    void durationChanged();
    void visibleStartMsChanged();
    void visibleEndMsChanged();
    void waveformColorChanged();
    void visualModeChanged();
    void baseColorChanged();
    void progressColorChanged();
    void gradientStartColorChanged();
    void gradientMiddleColorChanged();
    void gradientEndColorChanged();
    void frequencyStyleChanged();
    void rgbProgressChanged();
    void amplitudeScaleChanged();
    void hoverPositionChanged();
    void analysisProgressChanged();
    void densityChanged();
    void preserveSourcePeakDensityChanged();
    void sourceAnchoredSamplingChanged();
    void lineWidthChanged();
    void renderWidthChanged();
    void waveformCursorXChanged();
    void pointerInteractionEnabledChanged();
    void seekRequested(qint64 position);

protected:
    void geometryChange(const QRectF& newGeometry,
                        const QRectF& oldGeometry) override;
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
        agplayer::ui::WaveformDisplaySummary summary;
    };

    struct PeakSnapshot {
        std::shared_ptr<const LayerSnapshot> mix;
        std::shared_ptr<const LayerSnapshot> bass;
        std::shared_ptr<const LayerSnapshot> mid;
        std::shared_ptr<const LayerSnapshot> high;
        std::shared_ptr<const LayerSnapshot> spectrumPeakHold;
        std::uint64_t revision = 0;
        qint64 totalSamples = 0;
        qint64 sampleRate = 0;
        qsizetype peakCount = 0;
    };

    void setHoverPosition(qint64 position);
    void normalizeLayerInput(const QVariantList& input,
                             std::shared_ptr<LayerSnapshot>& snapshot);

    QVariantList peaks_;
    QVariantMap layers_;
    std::shared_ptr<const PeakSnapshot> peakSnapshot_;
    std::uint64_t nextRevision_ = 1;
    qint64 position_ = 0;
    qint64 cursorPosition_ = -1;
    qint64 duration_ = 0;
    qint64 visibleStartMs_ = 0;
    qint64 visibleEndMs_ = 0;
    QColor waveformColor_;
    int visualMode_ = -1;
    QColor baseColor_ = QColor(QStringLiteral("#9098a6"));
    QColor progressColor_ = QColor(QStringLiteral("#d27722"));
    QColor gradientStartColor_ = QColor(QStringLiteral("#00d4ff"));
    QColor gradientMiddleColor_ = QColor(QStringLiteral("#7b2ff7"));
    QColor gradientEndColor_ = QColor(QStringLiteral("#e62e9b"));
    QColor lowColor_{QStringLiteral("#FF0000")};
    QColor midColor_{QStringLiteral("#00FF00")};
    QColor highColor_{QStringLiteral("#0000FF")};
    qreal frequencyUnplayedOpacity_ = 0.20;
    bool rgbProgress_ = true;
    qreal amplitudeScale_ = 1.0;
    qint64 hoverPosition_ = -1;
    double analysisProgress_ = 0.0;
    qreal density_ = 1.0;
    bool preserveSourcePeakDensity_ = false;
    bool sourceAnchoredSampling_ = false;
    qreal lineWidth_ = 2.0;
    qreal renderWidth_ = 0.0;
    bool pointerInteractionEnabled_ = true;
    std::vector<float> spectrumVisual_;
    std::vector<float> spectrumPeakHold_;
    QElapsedTimer spectrumTimer_;
    bool pointerPressed_ = false;
};
