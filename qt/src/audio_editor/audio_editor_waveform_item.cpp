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
    qreal width_{};
    qreal height_{};
    qreal density_{};
    qreal line_width_{};
    QColor color_;
};

std::vector<float> resampleChannel(const std::vector<float>& input,
                                   const std::size_t targetBuckets)
{
    const float blank = std::numeric_limits<float>::quiet_NaN();
    std::vector<float> result(targetBuckets * 2U, blank);
    const std::size_t sourceBuckets = input.size() / 2U;
    if (sourceBuckets == 0U || targetBuckets == 0U) return result;

    if (sourceBuckets <= targetBuckets) {
        for (std::size_t target = 0; target < targetBuckets; ++target) {
            const std::size_t source = std::min(
                sourceBuckets - 1U, target * sourceBuckets / targetBuckets);
            result[target * 2U] = input[source * 2U];
            result[target * 2U + 1U] = input[source * 2U + 1U];
        }
        return result;
    }

    for (std::size_t target = 0; target < targetBuckets; ++target) {
        const std::size_t start = target * sourceBuckets / targetBuckets;
        const std::size_t end = std::max(
            start + 1U, (target + 1U) * sourceBuckets / targetBuckets);
        float minimum = 1.0F;
        float maximum = -1.0F;
        bool hasValue = false;
        bool hasBlank = false;
        for (std::size_t source = start;
             source < std::min(end, sourceBuckets); ++source) {
            const float sourceMinimum = input[source * 2U];
            const float sourceMaximum = input[source * 2U + 1U];
            if (!std::isfinite(sourceMinimum)
                || !std::isfinite(sourceMaximum)) {
                hasBlank = true;
                break;
            }
            minimum = std::min(minimum, sourceMinimum);
            maximum = std::max(maximum, sourceMaximum);
            hasValue = true;
        }
        if (hasValue && !hasBlank) {
            result[target * 2U] = minimum;
            result[target * 2U + 1U] = maximum;
        }
    }
    return result;
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
            const bool blankMinimum = !values[index].isValid()
                || values[index].isNull();
            const bool blankMaximum = !values[index + 1].isValid()
                || values[index + 1].isNull();
            if (blankMinimum || blankMaximum) {
                if (blankMinimum != blankMaximum) {
                    valid = false;
                    break;
                }
                channel.push_back(std::numeric_limits<float>::quiet_NaN());
                channel.push_back(std::numeric_limits<float>::quiet_NaN());
                normalized_channel.append(QVariant{});
                normalized_channel.append(QVariant{});
                continue;
            }
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

void AudioEditorWaveformItem::setWaveformColor(const QColor& color)
{
    if (!color.isValid() || color == waveform_color_) {
        return;
    }
    waveform_color_ = color;
    update();
    emit waveformColorChanged();
}

void AudioEditorWaveformItem::setDensity(const qreal value)
{
    const qreal bounded = std::clamp(value, 0.5, 5.0);
    if (qFuzzyCompare(bounded, density_)) return;
    density_ = bounded;
    update();
    emit densityChanged();
}

void AudioEditorWaveformItem::setLineWidth(const qreal value)
{
    const qreal bounded = std::clamp(value, 0.3, 3.0);
    if (qFuzzyCompare(bounded, line_width_)) return;
    line_width_ = bounded;
    update();
    emit lineWidthChanged();
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
        generated_point_count_.store(0, std::memory_order_release);
        delete oldNode;
        return nullptr;
    }
    const std::size_t pair_count = snapshot->channels.front().size() / 2U;
    if (pair_count == 0U || !std::all_of(
            snapshot->channels.begin(), snapshot->channels.end(),
            [pair_count](const auto& channel) {
                return channel.size() / 2U == pair_count;
            })) {
        generated_point_count_.store(0, std::memory_order_release);
        delete oldNode;
        return nullptr;
    }
    const qreal densityScale = std::min<qreal>(2.0, density_) / 2.0;
    const std::size_t maximum_buckets = std::max<std::size_t>(1U,
        static_cast<std::size_t>(std::ceil(width() * densityScale)));
    std::vector<std::vector<float>> renderedChannels;
    renderedChannels.reserve(snapshot->channels.size());
    for (std::size_t channel = 0; channel < snapshot->channels.size(); ++channel) {
        const std::size_t budget = maximum_buckets / snapshot->channels.size()
            + (channel < maximum_buckets % snapshot->channels.size() ? 1U : 0U);
        renderedChannels.push_back(
            resampleChannel(snapshot->channels[channel], budget));
    }
    std::size_t valid_bucket_count = 0;
    for (const auto& channel : renderedChannels) {
        for (std::size_t index = 0; index < channel.size() / 2U; ++index) {
            if (std::isfinite(channel[index * 2U])
                && std::isfinite(channel[index * 2U + 1U])) {
                ++valid_bucket_count;
            }
        }
    }
    const std::size_t vertex_count = valid_bucket_count * 2U;
    if (vertex_count > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        generated_point_count_.store(0, std::memory_order_release);
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
        || !qFuzzyCompare(node->density_, density_)
        || !qFuzzyCompare(node->line_width_, line_width_)) {
        node->geometry_.allocate(static_cast<int>(vertex_count));
        node->geometry_.setLineWidth(static_cast<float>(line_width_));
        auto* vertices = node->geometry_.vertexDataAsPoint2D();
        const qreal channel_height = height()
            / static_cast<qreal>(snapshot->channels.size());
        std::size_t vertex = 0;
        for (std::size_t channel_index = 0;
             channel_index < renderedChannels.size(); ++channel_index) {
            const auto& peaks = renderedChannels[channel_index];
            const std::size_t renderedPairCount = peaks.size() / 2U;
            if (renderedPairCount == 0U) continue;
            const qreal center = (static_cast<qreal>(channel_index) + 0.5)
                * channel_height;
            const qreal half_height = channel_height * 0.46;
            for (std::size_t index = 0; index < renderedPairCount; ++index) {
                const float minimum = peaks[index * 2U];
                const float maximum = peaks[index * 2U + 1U];
                if (!std::isfinite(minimum) || !std::isfinite(maximum)) continue;
                const qreal x = renderedPairCount == 1U ? width() * 0.5
                    : static_cast<qreal>(index) * width()
                        / static_cast<qreal>(renderedPairCount - 1U);
                vertices[vertex++].set(static_cast<float>(x),
                    static_cast<float>(center + minimum * half_height));
                vertices[vertex++].set(static_cast<float>(x),
                    static_cast<float>(center + maximum * half_height));
            }
        }
        node->revision_ = snapshot->revision;
        node->width_ = width();
        node->height_ = height();
        node->density_ = density_;
        node->line_width_ = line_width_;
        generated_point_count_.store(static_cast<int>(vertex),
                                     std::memory_order_release);
        node->markDirty(QSGNode::DirtyGeometry);
    }
    if (node->color_ != waveform_color_) {
        node->material_.setColor(waveform_color_);
        node->color_ = waveform_color_;
        node->markDirty(QSGNode::DirtyMaterial);
    }
    return node;
}
