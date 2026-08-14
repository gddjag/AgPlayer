#pragma once

#include <QColor>
#include <QQuickItem>
#include <QVariantList>

#include <cstdint>
#include <memory>
#include <vector>

class AudioEditorWaveformItem : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QVariantList channelPeaks READ channelPeaks WRITE setChannelPeaks
                   NOTIFY channelPeaksChanged)
    Q_PROPERTY(int renderMode READ renderMode WRITE setRenderMode
                   NOTIFY renderModeChanged)
    Q_PROPERTY(QColor waveformColor READ waveformColor WRITE setWaveformColor
                   NOTIFY waveformColorChanged)
    Q_PROPERTY(qreal visibleStartRatio READ visibleStartRatio WRITE setVisibleStartRatio
                   NOTIFY visibleRangeChanged)
    Q_PROPERTY(qreal visibleEndRatio READ visibleEndRatio WRITE setVisibleEndRatio
                   NOTIFY visibleRangeChanged)

public:
    explicit AudioEditorWaveformItem(QQuickItem* parent = nullptr);

    [[nodiscard]] QVariantList channelPeaks() const { return channel_peaks_; }
    void setChannelPeaks(const QVariantList& channels);
    [[nodiscard]] int renderMode() const noexcept { return render_mode_; }
    void setRenderMode(int mode);
    [[nodiscard]] QColor waveformColor() const noexcept { return waveform_color_; }
    void setWaveformColor(const QColor& color);
    [[nodiscard]] qreal visibleStartRatio() const noexcept { return visible_start_ratio_; }
    void setVisibleStartRatio(qreal ratio);
    [[nodiscard]] qreal visibleEndRatio() const noexcept { return visible_end_ratio_; }
    void setVisibleEndRatio(qreal ratio);

signals:
    void channelPeaksChanged();
    void renderModeChanged();
    void waveformColorChanged();
    void visibleRangeChanged();

protected:
    void geometryChange(const QRectF& newGeometry,
                        const QRectF& oldGeometry) override;
    QSGNode* updatePaintNode(QSGNode* oldNode,
                             UpdatePaintNodeData* data) override;

private:
    struct Snapshot final {
        std::vector<std::vector<float>> channels;
        std::uint64_t revision{};
    };

    QVariantList channel_peaks_;
    std::shared_ptr<const Snapshot> snapshot_;
    std::uint64_t next_revision_{1};
    QColor waveform_color_{QStringLiteral("#36d1c4")};
    int render_mode_{2};
    qreal visible_start_ratio_{};
    qreal visible_end_ratio_{1.0};
};
