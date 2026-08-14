#include "audio_editor_waveform_item.hpp"

#include <QSGFlatColorMaterial>
#include <QSGGeometry>
#include <QSGGeometryNode>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

class EditorWaveformNode final : public QSGGeometryNode {
public:
    EditorWaveformNode()
        : geometry_(QSGGeometry::defaultAttributes_Point2D(), 0)
    {
        geometry_.setDrawingMode(QSGGeometry::DrawLines);
        setGeometry(&geometry_);
        setFlag(OwnsGeometry, false);
        setMaterial(&material_);
        setFlag(OwnsMaterial, false);
    }

    QSGGeometry geometry_;
    QSGFlatColorMaterial material_;
    std::uint64_t revision_{};
    int render_mode_{};
    qreal width_{};
    qreal height_{};
    qreal visible_start_ratio_{};
    qreal visible_end_ratio_{1.0};
    QColor color_;
};

std::size_t visible_mode_stride(const std::size_t visiblePairCount,
                               const qreal width,
                               const int renderMode)
{
    if (width <= 0.0 || visiblePairCount <= 1U) return 1U;
    const std::size_t target = static_cast<std::size_t>(
        std::max<qreal>(1.0, std::round(width)));
    if (renderMode == 0) {
        return std::max<std::size_t>(1U, visiblePairCount / (target * 2U));
    }
    if (renderMode == 1) {
        return std::max<std::size_t>(1U, visiblePairCount / (target * 3U));
    }
    return 1U;
}

} // namespace

AudioEditorWaveformItem::AudioEditorWaveformItem(QQuickItem* parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
}

void AudioEditorWaveformItem::setChannelPeaks(const QVariantList& channels)
{
    auto snapshot = std::make_shared<Snapshot>();
    QVariantList normalized;
    for (const QVariant& channel_value : channels) {
        const QVariantList values = channel_value.toList();
        if (values.empty() || values.size() % 2 != 0) {
            snapshot->channels.clear();
            normalized.clear();
            break;
        }
        std::vector<float> channel;
        channel.reserve(static_cast<std::size_t>(values.size()));
        QVariantList normalized_channel;
        bool valid = true;
        for (qsizetype index = 0; index < values.size(); index += 2) {
            const double minimum = values[index].toDouble();
            const double maximum = values[index + 1].toDouble();
            if (!std::isfinite(minimum) || !std::isfinite(maximum)
                || minimum > maximum) {
                valid = false;
                break;
            }
            const float bounded_min = static_cast<float>(
                std::clamp(minimum, -1.0, 1.0));
            const float bounded_max = static_cast<float>(
                std::clamp(maximum, -1.0, 1.0));
            channel.push_back(bounded_min);
            channel.push_back(bounded_max);
            normalized_channel.append(bounded_min);
            normalized_channel.append(bounded_max);
        }
        if (!valid) {
            snapshot->channels.clear();
            normalized.clear();
            break;
        }
        snapshot->channels.push_back(std::move(channel));
        normalized.append(QVariant(normalized_channel));
    }
    if (channel_peaks_ == normalized) {
        return;
    }
    snapshot->revision = next_revision_++;
    snapshot_ = snapshot->channels.empty() ? nullptr : std::move(snapshot);
    channel_peaks_ = std::move(normalized);
    update();
    emit channelPeaksChanged();
}

void AudioEditorWaveformItem::setRenderMode(const int mode)
{
    const int bounded = std::clamp(mode, 0, 2);
    if (render_mode_ == bounded) return;
    render_mode_ = bounded;
    update();
    emit renderModeChanged();
}

void AudioEditorWaveformItem::setWaveformColor(const QColor& color)
{
    if (!color.isValid() || color == waveform_color_) {
        return;
    }
    waveform_color_ = color;
    update();
    emit waveformColorChanged();
}

void AudioEditorWaveformItem::setVisibleStartRatio(const qreal ratio)
{
    const qreal bounded = std::clamp(ratio, 0.0, 1.0);
    if (qFuzzyCompare(visible_start_ratio_, bounded)) return;
    visible_start_ratio_ = bounded;
    update();
    emit visibleRangeChanged();
}

void AudioEditorWaveformItem::setVisibleEndRatio(const qreal ratio)
{
    const qreal bounded = std::clamp(ratio, 0.0, 1.0);
    if (qFuzzyCompare(visible_end_ratio_, bounded)) return;
    visible_end_ratio_ = bounded;
    update();
    emit visibleRangeChanged();
}

void AudioEditorWaveformItem::geometryChange(const QRectF& newGeometry,
                                             const QRectF& oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        update();
    }
}

QSGNode* AudioEditorWaveformItem::updatePaintNode(
    QSGNode* oldNode, UpdatePaintNodeData*)
{
    const auto snapshot = snapshot_;
    if (!snapshot || snapshot->channels.empty() || width() <= 0.0
        || height() <= 0.0) {
        delete oldNode;
        return nullptr;
    }
    const std::size_t pair_count = snapshot->channels.front().size() / 2U;
    if (pair_count == 0U || !std::all_of(
            snapshot->channels.begin(), snapshot->channels.end(),
            [pair_count](const auto& channel) {
                return channel.size() / 2U == pair_count;
            })) {
        delete oldNode;
        return nullptr;
    }
    const std::size_t first_pair = std::min(pair_count - 1U,
        static_cast<std::size_t>(std::floor(visible_start_ratio_ * pair_count)));
    const std::size_t end_pair = std::clamp(
        static_cast<std::size_t>(std::ceil(visible_end_ratio_ * pair_count)),
        first_pair + 1U, pair_count);
    const std::size_t visible_pair_count = end_pair - first_pair;
    const std::size_t stride = visible_mode_stride(visible_pair_count, width(),
                                                   render_mode_);
    const std::size_t draw_pair_count = (visible_pair_count > 0U
        ? ((visible_pair_count - 1U) / stride) + 1U : 0U);
    const std::size_t vertex_count = draw_pair_count * 2U
        * snapshot->channels.size();
    if (vertex_count > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        delete oldNode;
        return nullptr;
    }
    auto* node = static_cast<EditorWaveformNode*>(oldNode);
    if (!node) {
        node = new EditorWaveformNode();
    }
    if (node->revision_ != snapshot->revision
        || !qFuzzyCompare(node->width_, width())
        || !qFuzzyCompare(node->height_, height())
        || !qFuzzyCompare(node->visible_start_ratio_, visible_start_ratio_)
        || !qFuzzyCompare(node->visible_end_ratio_, visible_end_ratio_)
        || node->render_mode_ != render_mode_) {
        node->geometry_.allocate(static_cast<int>(vertex_count));
        auto* vertices = node->geometry_.vertexDataAsPoint2D();
        const qreal channel_height = height()
            / static_cast<qreal>(snapshot->channels.size());
        std::size_t vertex = 0;
        for (std::size_t channel_index = 0;
             channel_index < snapshot->channels.size(); ++channel_index) {
            const auto& peaks = snapshot->channels[channel_index];
            const qreal center = (static_cast<qreal>(channel_index) + 0.5)
                * channel_height;
            const qreal half_height = channel_height * 0.46;
            for (std::size_t index = first_pair; index < end_pair;
                 index += stride) {
                const std::size_t visible_index = (index - first_pair) / stride;
                const qreal x = draw_pair_count <= 1U ? width() * 0.5
                    : static_cast<qreal>(visible_index) * width()
                        / static_cast<qreal>(draw_pair_count - 1U);
                vertices[vertex++].set(static_cast<float>(x),
                    static_cast<float>(center + peaks[index * 2U] * half_height));
                vertices[vertex++].set(static_cast<float>(x),
                    static_cast<float>(center + peaks[index * 2U + 1U] * half_height));
            }
        }
        node->revision_ = snapshot->revision;
        node->width_ = width();
        node->height_ = height();
        node->visible_start_ratio_ = visible_start_ratio_;
        node->visible_end_ratio_ = visible_end_ratio_;
        node->render_mode_ = render_mode_;
        node->markDirty(QSGNode::DirtyGeometry);
    }
    if (node->color_ != waveform_color_) {
        node->material_.setColor(waveform_color_);
        node->color_ = waveform_color_;
        node->markDirty(QSGNode::DirtyMaterial);
    }
    return node;
}
