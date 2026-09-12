#include "audio_editor_waveform_item.hpp"
#include "waveform_render_limits.hpp"

#include <QSGFlatColorMaterial>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QQuickWindow>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

class EditorWaveformNode final : public QSGGeometryNode {
public:
    EditorWaveformNode()
        : geometry_(QSGGeometry::defaultAttributes_Point2D(), 0)
    {
        geometry_.setDrawingMode(QSGGeometry::DrawTriangles);
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
    qreal device_pixel_ratio_{};
    qreal line_width_{};
    bool sample_mode_{};
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
            if (sourceBuckets == 1U) {
                result[target * 2U] = input[0];
                result[target * 2U + 1U] = input[1];
                continue;
            }
            const double position = static_cast<double>(target)
                * static_cast<double>(sourceBuckets - 1U)
                / static_cast<double>(targetBuckets - 1U);
            const std::size_t left = static_cast<std::size_t>(
                std::floor(position));
            const std::size_t right = std::min(sourceBuckets - 1U, left + 1U);
            const float leftMinimum = input[left * 2U];
            const float leftMaximum = input[left * 2U + 1U];
            const float rightMinimum = input[right * 2U];
            const float rightMaximum = input[right * 2U + 1U];
            if (left == right && std::isfinite(leftMinimum)
                && std::isfinite(leftMaximum)) {
                result[target * 2U] = leftMinimum;
                result[target * 2U + 1U] = leftMaximum;
                continue;
            }
            if (!std::isfinite(leftMinimum) || !std::isfinite(leftMaximum)
                || !std::isfinite(rightMinimum) || !std::isfinite(rightMaximum)) {
                continue;
            }
            const float fraction = static_cast<float>(position - left);
            result[target * 2U] = leftMinimum
                + (rightMinimum - leftMinimum) * fraction;
            result[target * 2U + 1U] = leftMaximum
                + (rightMaximum - leftMaximum) * fraction;
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
    setAntialiasing(true);
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

void AudioEditorWaveformItem::setSampleMode(const bool enabled)
{
    if (sample_mode_ == enabled) return;
    sample_mode_ = enabled;
    update();
    emit sampleModeChanged();
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
    const qreal devicePixelRatio = std::clamp(window() != nullptr
        ? window()->effectiveDevicePixelRatio() : 1.0, 1.0,
        kMaxWaveformDevicePixelRatio);
    const qreal densityScale = std::min<qreal>(2.0, density_) / 2.0;
    const std::size_t maximum_buckets = std::max<std::size_t>(1U,
        static_cast<std::size_t>(std::ceil(
            width() * devicePixelRatio * densityScale)));
    std::vector<std::vector<float>> renderedChannels;
    renderedChannels.reserve(snapshot->channels.size());
    for (std::size_t channel = 0; channel < snapshot->channels.size(); ++channel) {
        renderedChannels.push_back(sample_mode_
            ? snapshot->channels[channel]
            : resampleChannel(snapshot->channels[channel], maximum_buckets));
    }
    std::size_t valid_bucket_count = 0;
    std::size_t segment_count = 0;
    for (const auto& channel : renderedChannels) {
        const std::size_t buckets = channel.size() / 2U;
        for (std::size_t index = 0; index < buckets; ++index) {
            if (std::isfinite(channel[index * 2U])
                && std::isfinite(channel[index * 2U + 1U])) {
                ++valid_bucket_count;
            }
        }
        if (buckets == 1U && std::isfinite(channel[0])
            && std::isfinite(channel[1])) {
            ++segment_count;
        }
        for (std::size_t index = 1; index < buckets; ++index) {
            const std::size_t previous = (index - 1U) * 2U;
            const std::size_t current = index * 2U;
            if (std::isfinite(channel[previous])
                && std::isfinite(channel[previous + 1U])
                && std::isfinite(channel[current])
                && std::isfinite(channel[current + 1U])) {
                ++segment_count;
            }
        }
        for (std::size_t index = 0; index < buckets; ++index) {
            const std::size_t current = index * 2U;
            const bool valid = std::isfinite(channel[current])
                && std::isfinite(channel[current + 1U]);
            const bool previousValid = index > 0U
                && std::isfinite(channel[current - 2U])
                && std::isfinite(channel[current - 1U]);
            const bool nextValid = index + 1U < buckets
                && std::isfinite(channel[current + 2U])
                && std::isfinite(channel[current + 3U]);
            if (valid && !previousValid && !nextValid && buckets > 1U) {
                ++segment_count;
            }
        }
    }
    const std::size_t vertex_count = segment_count * 12U;
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
        || !qFuzzyCompare(node->device_pixel_ratio_, devicePixelRatio)
        || !qFuzzyCompare(node->line_width_, line_width_)
        || node->sample_mode_ != sample_mode_) {
        node->geometry_.allocate(static_cast<int>(vertex_count));
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
            const auto alignedX = [](const qreal value) {
                return std::round(value * 2.0) / 2.0;
            };
            const auto appendQuad = [&vertices, &vertex](
                const qreal left, const qreal topLeft,
                const qreal bottomLeft, const qreal right,
                const qreal topRight, const qreal bottomRight) {
                vertices[vertex++].set(static_cast<float>(left),
                                       static_cast<float>(topLeft));
                vertices[vertex++].set(static_cast<float>(left),
                                       static_cast<float>(bottomLeft));
                vertices[vertex++].set(static_cast<float>(right),
                                       static_cast<float>(topRight));
                vertices[vertex++].set(static_cast<float>(right),
                                       static_cast<float>(topRight));
                vertices[vertex++].set(static_cast<float>(left),
                                       static_cast<float>(bottomLeft));
                vertices[vertex++].set(static_cast<float>(right),
                                       static_cast<float>(bottomRight));
            };
            const qreal centerTop = center - line_width_ * 0.5;
            const qreal centerBottom = center + line_width_ * 0.5;
            if (renderedPairCount == 1U && std::isfinite(peaks[0])
                && std::isfinite(peaks[1])) {
                const qreal middle = alignedX(width() * 0.5);
                const qreal left = std::max<qreal>(0.0, middle - 0.5);
                const qreal right = std::min(width(), middle + 0.5);
                appendQuad(left, center + peaks[0] * half_height,
                           center + peaks[1] * half_height,
                           right, center + peaks[0] * half_height,
                           center + peaks[1] * half_height);
                appendQuad(left, centerTop, centerBottom,
                           right, centerTop, centerBottom);
            }
            for (std::size_t index = 1; index < renderedPairCount; ++index) {
                const std::size_t previous = (index - 1U) * 2U;
                const std::size_t current = index * 2U;
                if (!std::isfinite(peaks[previous])
                    || !std::isfinite(peaks[previous + 1U])
                    || !std::isfinite(peaks[current])
                    || !std::isfinite(peaks[current + 1U])) {
                    continue;
                }
                const qreal left = alignedX(
                    static_cast<qreal>(index - 1U) * width()
                    / static_cast<qreal>(renderedPairCount - 1U));
                const qreal right = alignedX(
                    static_cast<qreal>(index) * width()
                    / static_cast<qreal>(renderedPairCount - 1U));
                appendQuad(left,
                    center + peaks[previous] * half_height,
                    center + peaks[previous + 1U] * half_height,
                    right,
                    center + peaks[current] * half_height,
                    center + peaks[current + 1U] * half_height);
                appendQuad(left, centerTop, centerBottom,
                           right, centerTop, centerBottom);
            }
            for (std::size_t index = 0; index < renderedPairCount; ++index) {
                const std::size_t current = index * 2U;
                const bool valid = std::isfinite(peaks[current])
                    && std::isfinite(peaks[current + 1U]);
                const bool previousValid = index > 0U
                    && std::isfinite(peaks[current - 2U])
                    && std::isfinite(peaks[current - 1U]);
                const bool nextValid = index + 1U < renderedPairCount
                    && std::isfinite(peaks[current + 2U])
                    && std::isfinite(peaks[current + 3U]);
                if (!valid || previousValid || nextValid || renderedPairCount == 1U) {
                    continue;
                }
                const qreal middle = alignedX(static_cast<qreal>(index) * width()
                    / static_cast<qreal>(renderedPairCount - 1U));
                const qreal left = std::max<qreal>(0.0, middle - 0.5);
                const qreal right = std::min(width(), middle + 0.5);
                appendQuad(left, center + peaks[current] * half_height,
                           center + peaks[current + 1U] * half_height,
                           right, center + peaks[current] * half_height,
                           center + peaks[current + 1U] * half_height);
                appendQuad(left, centerTop, centerBottom,
                           right, centerTop, centerBottom);
            }
        }
        node->revision_ = snapshot->revision;
        node->width_ = width();
        node->height_ = height();
        node->density_ = density_;
        node->device_pixel_ratio_ = devicePixelRatio;
        node->line_width_ = line_width_;
        node->sample_mode_ = sample_mode_;
        generated_point_count_.store(static_cast<int>(valid_bucket_count * 2U),
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
