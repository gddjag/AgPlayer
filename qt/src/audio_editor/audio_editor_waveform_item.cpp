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
    QColor color_;
    bool sample_mode_{};
    qreal density_{1.0};
    qreal line_width_{1.0};
};

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
    qsizetype pairCount = -1;
    for (const QVariant& channel_value : channels) {
        const QVariantList values = channel_value.toList();
        if (values.empty() || values.size() % 2 != 0) {
            snapshot->channels.clear();
            normalized.clear();
            break;
        }
        if (pairCount < 0) pairCount = values.size() / 2;
        if (values.size() / 2 != pairCount) {
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
    if (!snapshot->channels.empty()) {
        std::vector<float> mix(static_cast<std::size_t>(pairCount) * 2U,
                               std::numeric_limits<float>::quiet_NaN());
        for (qsizetype pair = 0; pair < pairCount; ++pair) {
            double minimum = 0.0;
            double maximum = 0.0;
            std::size_t contributors = 0;
            for (const auto& channel : snapshot->channels) {
                const std::size_t index = static_cast<std::size_t>(pair) * 2U;
                if (!std::isfinite(channel[index])
                    || !std::isfinite(channel[index + 1U])) {
                    continue;
                }
                minimum += channel[index];
                maximum += channel[index + 1U];
                ++contributors;
            }
            if (contributors > 0U) {
                const std::size_t index = static_cast<std::size_t>(pair) * 2U;
                mix[index] = static_cast<float>(minimum / contributors);
                mix[index + 1U] = static_cast<float>(maximum / contributors);
            }
        }
        snapshot->channels = {std::move(mix)};
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

void AudioEditorWaveformItem::setDensity(const double density)
{
    if (!std::isfinite(density)) return;
    const double bounded = std::clamp(density, 0.5, 5.0);
    if (qFuzzyCompare(density_, bounded)) return;
    density_ = bounded;
    update();
    emit densityChanged();
}

void AudioEditorWaveformItem::setLineWidth(const double width)
{
    if (!std::isfinite(width)) return;
    const double bounded = std::clamp(width, 0.1, 8.0);
    if (qFuzzyCompare(line_width_, bounded)) return;
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
    const std::size_t maximum_buckets = std::max<std::size_t>(1U,
        static_cast<std::size_t>(std::floor(width() * density_)));
    const std::size_t stride = std::max<std::size_t>(
        1U, (pair_count + maximum_buckets - 1U) / maximum_buckets);
    std::size_t valid_bucket_count = 0;
    if (sample_mode_) {
        for (const auto& channel : snapshot->channels) {
            for (std::size_t index = 1; index < pair_count; ++index) {
                const std::size_t previous = (index - 1U) * 2U;
                const std::size_t current = index * 2U;
                if (std::isfinite(channel[previous])
                    && std::isfinite(channel[previous + 1U])
                    && std::isfinite(channel[current])
                    && std::isfinite(channel[current + 1U])) {
                    ++valid_bucket_count;
                }
            }
        }
    } else {
        for (const auto& channel : snapshot->channels) {
            for (std::size_t start = 0; start < pair_count; start += stride) {
                const std::size_t end = std::min(pair_count, start + stride);
                bool has_value = false;
                for (std::size_t index = start; index < end; ++index) {
                    if (std::isfinite(channel[index * 2U])
                        && std::isfinite(channel[index * 2U + 1U])) {
                        has_value = true;
                        break;
                    }
                }
                if (has_value) ++valid_bucket_count;
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
        || node->sample_mode_ != sample_mode_
        || !qFuzzyCompare(node->density_, density_)
        || !qFuzzyCompare(node->line_width_, line_width_)) {
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
            if (sample_mode_) {
                for (std::size_t index = 1; index < pair_count; ++index) {
                    const std::size_t previous = (index - 1U) * 2U;
                    const std::size_t current = index * 2U;
                    if (!std::isfinite(peaks[previous])
                        || !std::isfinite(peaks[previous + 1U])
                        || !std::isfinite(peaks[current])
                        || !std::isfinite(peaks[current + 1U])) {
                        continue;
                    }
                    const qreal previousX = pair_count == 1U ? width() * 0.5
                        : static_cast<qreal>(index - 1U) * width()
                            / static_cast<qreal>(pair_count - 1U);
                    const qreal currentX = static_cast<qreal>(index) * width()
                        / static_cast<qreal>(pair_count - 1U);
                    const float previousSample = (peaks[previous]
                        + peaks[previous + 1U]) * 0.5F;
                    const float currentSample = (peaks[current]
                        + peaks[current + 1U]) * 0.5F;
                    vertices[vertex++].set(static_cast<float>(previousX),
                        static_cast<float>(center + previousSample * half_height));
                    vertices[vertex++].set(static_cast<float>(currentX),
                        static_cast<float>(center + currentSample * half_height));
                }
                continue;
            }
            for (std::size_t start = 0; start < pair_count; start += stride) {
                const std::size_t end = std::min(pair_count, start + stride);
                float minimum = 1.0F;
                float maximum = -1.0F;
                bool has_value = false;
                for (std::size_t index = start; index < end; ++index) {
                    const float bucket_minimum = peaks[index * 2U];
                    const float bucket_maximum = peaks[index * 2U + 1U];
                    if (!std::isfinite(bucket_minimum)
                        || !std::isfinite(bucket_maximum)) {
                        continue;
                    }
                    minimum = std::min(minimum, bucket_minimum);
                    maximum = std::max(maximum, bucket_maximum);
                    has_value = true;
                }
                if (!has_value) continue;
                const qreal bucket_center = static_cast<qreal>(start + end - 1U) * 0.5;
                const qreal x = pair_count == 1U ? width() * 0.5
                    : bucket_center * width() / static_cast<qreal>(pair_count - 1U);
                vertices[vertex++].set(static_cast<float>(x),
                    static_cast<float>(center + minimum * half_height));
                vertices[vertex++].set(static_cast<float>(x),
                    static_cast<float>(center + maximum * half_height));
            }
        }
        node->revision_ = snapshot->revision;
        node->width_ = width();
        node->height_ = height();
        node->sample_mode_ = sample_mode_;
        node->density_ = density_;
        node->line_width_ = line_width_;
        node->geometry_.setLineWidth(static_cast<float>(line_width_));
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
