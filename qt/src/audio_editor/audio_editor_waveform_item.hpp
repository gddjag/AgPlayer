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
    Q_PROPERTY(QColor waveformColor READ waveformColor WRITE setWaveformColor
                   NOTIFY waveformColorChanged)

public:
    explicit AudioEditorWaveformItem(QQuickItem* parent = nullptr);

    [[nodiscard]] QVariantList channelPeaks() const { return channel_peaks_; }
    void setChannelPeaks(const QVariantList& channels);
    [[nodiscard]] QColor waveformColor() const noexcept { return waveform_color_; }
    void setWaveformColor(const QColor& color);

signals:
    void channelPeaksChanged();
    void waveformColorChanged();

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
};
