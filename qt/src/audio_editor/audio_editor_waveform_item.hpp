#pragma once

#include <QColor>
#include <QQuickItem>
#include <QVariantList>

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

class AudioEditorWaveformItem : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QVariantList channelPeaks READ channelPeaks WRITE setChannelPeaks
                   NOTIFY channelPeaksChanged)
    Q_PROPERTY(QColor waveformColor READ waveformColor WRITE setWaveformColor
                   NOTIFY waveformColorChanged)
    Q_PROPERTY(bool sampleMode READ sampleMode WRITE setSampleMode
                   NOTIFY sampleModeChanged)

public:
    explicit AudioEditorWaveformItem(QQuickItem* parent = nullptr);

    [[nodiscard]] QVariantList channelPeaks() const { return channel_peaks_; }
    void setChannelPeaks(const QVariantList& channels);
    [[nodiscard]] QColor waveformColor() const noexcept { return waveform_color_; }
    void setWaveformColor(const QColor& color);
    [[nodiscard]] bool sampleMode() const noexcept { return sample_mode_; }
    void setSampleMode(bool enabled);
    [[nodiscard]] int generatedPointCount() const noexcept
    { return generated_point_count_.load(std::memory_order_acquire); }

signals:
    void channelPeaksChanged();
    void waveformColorChanged();
    void sampleModeChanged();

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
    bool sample_mode_{};
    std::atomic_int generated_point_count_{};
};
