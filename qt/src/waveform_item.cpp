#include "waveform_item.hpp"

#include <QHoverEvent>
#include <QMouseEvent>
#include <QQuickWindow>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QSGVertexColorMaterial>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace {

struct Rgb {
    int red;
    int green;
    int blue;
};

constexpr std::array<Rgb, 5> gradientStops{{
    {0x00, 0xD4, 0xFF},
    {0x16, 0x88, 0xFF},
    {0x7B, 0x2F, 0xF7},
    {0xE6, 0x2E, 0x9B},
    {0xFF, 0x40, 0x57},
}};

// Strict RGBA layer colors from project conventions.
// Alpha is split into played / unplayed values derived from the base opacity
// and WaveformItem::unplayedAlpha().
struct LayerColor {
    unsigned char red;
    unsigned char green;
    unsigned char blue;
    unsigned char playedAlpha;
    unsigned char unplayedAlpha;
};

constexpr LayerColor kBassColor{170U, 55U, 55U, 158U, 55U};
constexpr LayerColor kMidColor{55U, 140U, 55U, 148U, 52U};
constexpr LayerColor kHighColor{55U, 90U, 145U, 133U, 46U};

Rgb gradientColor(double normalizedX)
{
    const double clamped = std::clamp(normalizedX, 0.0, 1.0);
    const double scaled = clamped * static_cast<double>(gradientStops.size() - 1U);
    const auto lower = static_cast<std::size_t>(std::floor(scaled));
    const auto upper = std::min(lower + 1U, gradientStops.size() - 1U);
    const double fraction = scaled - static_cast<double>(lower);
    const auto interpolate = [fraction](int from, int to) {
        return static_cast<int>(std::lround(static_cast<double>(from)
                                            + static_cast<double>(to - from) * fraction));
    };
    return {interpolate(gradientStops[lower].red, gradientStops[upper].red),
            interpolate(gradientStops[lower].green, gradientStops[upper].green),
            interpolate(gradientStops[lower].blue, gradientStops[upper].blue)};
}

std::size_t computePlayedCount(std::size_t peakCount, qint64 position, qint64 duration)
{
    if (peakCount == 0U) {
        return 0U;
    }
    if (duration <= 0) {
        return 1U;
    }
    const double playedFraction = std::clamp(
        static_cast<double>(position) / static_cast<double>(duration), 0.0, 1.0);
    if (peakCount == 1U) {
        return playedFraction >= 0.5 ? 1U : 0U;
    }
    const double playedIndex = playedFraction * static_cast<double>(peakCount - 1U);
    return std::min(peakCount,
                    static_cast<std::size_t>(std::floor(playedIndex)) + 1U);
}

void updateMixVertexColors(QSGGeometry::ColoredPoint2D* vertices,
                           std::size_t peakCount,
                           std::size_t playedCount,
                           const QColor& waveformColor)
{
    for (std::size_t index = 0; index < peakCount; ++index) {
        const double normalizedX = peakCount == 1U
                                       ? 0.5
                                       : static_cast<double>(index)
                                             / static_cast<double>(peakCount - 1U);
        const auto alpha = static_cast<unsigned char>(
            index < playedCount ? 255 : WaveformItem::unplayedAlpha());

        unsigned char red = 0;
        unsigned char green = 0;
        unsigned char blue = 0;
        if (waveformColor.isValid()) {
            red = static_cast<unsigned char>(waveformColor.red());
            green = static_cast<unsigned char>(waveformColor.green());
            blue = static_cast<unsigned char>(waveformColor.blue());
        } else {
            const Rgb color = gradientColor(normalizedX);
            red = static_cast<unsigned char>(color.red);
            green = static_cast<unsigned char>(color.green);
            blue = static_cast<unsigned char>(color.blue);
        }

        vertices[index * 2U].r = red;
        vertices[index * 2U].g = green;
        vertices[index * 2U].b = blue;
        vertices[index * 2U].a = alpha;
        vertices[index * 2U + 1U].r = red;
        vertices[index * 2U + 1U].g = green;
        vertices[index * 2U + 1U].b = blue;
        vertices[index * 2U + 1U].a = alpha;
    }
}

void updateLayerVertexColors(QSGGeometry::ColoredPoint2D* vertices,
                             std::size_t peakCount,
                             std::size_t playedCount,
                             const LayerColor& color)
{
    for (std::size_t index = 0; index < peakCount; ++index) {
        const auto alpha = static_cast<unsigned char>(
            index < playedCount ? color.playedAlpha : color.unplayedAlpha);
        vertices[index * 2U].r = color.red;
        vertices[index * 2U].g = color.green;
        vertices[index * 2U].b = color.blue;
        vertices[index * 2U].a = alpha;
        vertices[index * 2U + 1U].r = color.red;
        vertices[index * 2U + 1U].g = color.green;
        vertices[index * 2U + 1U].b = color.blue;
        vertices[index * 2U + 1U].a = alpha;
    }
}

class WaveformNode final : public QSGGeometryNode {
public:
    WaveformNode()
        : geometry_(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0)
    {
        geometry_.setDrawingMode(QSGGeometry::DrawLines);
        geometry_.setLineWidth(1.0F);
        geometry_.setVertexDataPattern(QSGGeometry::DynamicPattern);
        setGeometry(&geometry_);
        setMaterial(&material_);
    }

    QSGGeometry geometry_;
    QSGVertexColorMaterial material_;
    std::uint64_t revision_ = 0;
    qreal width_ = -1.0;
    qreal height_ = -1.0;
    qreal devicePixelRatio_ = -1.0;
    qint64 position_ = -1;
    qint64 duration_ = -1;
    QColor waveformColor_;
    std::size_t peakCount_ = 0;
    std::size_t playedCount_ = 0;
    unsigned char layerMask_ = 0;
};

} // namespace

WaveformItem::WaveformItem(QQuickItem* parent)
    : QQuickItem(parent)
    , peakSnapshot_(std::make_shared<const PeakSnapshot>())
{
    setFlag(ItemHasContents, true);
    setAcceptHoverEvents(true);
    setAcceptedMouseButtons(Qt::LeftButton);
}

QVariantList WaveformItem::peaks() const
{
    return peaks_;
}

void WaveformItem::setPeaks(const QVariantList& peaks)
{
    auto snapshot = std::make_shared<PeakSnapshot>();
    snapshot->revision = nextRevision_++;

    QVariantList normalizedPeaks;
    normalizedPeaks.reserve(peaks.size());
    auto mix = std::make_shared<LayerSnapshot>();
    mix->values.reserve(static_cast<std::size_t>(peaks.size()));

    for (const QVariant& value : peaks) {
        bool converted = false;
        double peak = value.toDouble(&converted);
        if (!converted || !std::isfinite(peak)) {
            peak = 0.0;
        }
        peak = std::clamp(std::abs(peak), 0.0, 1.0);
        mix->values.push_back(static_cast<float>(peak));
        normalizedPeaks.append(peak);
    }

    snapshot->mix = std::move(mix);
    peaks_ = std::move(normalizedPeaks);
    layers_.clear();
    peakSnapshot_ = std::move(snapshot);
    emit peaksChanged();
    emit layersChanged();
    update();
}

QVariantMap WaveformItem::layers() const
{
    return layers_;
}

void WaveformItem::setLayers(const QVariantMap& layers)
{
    auto snapshot = std::make_shared<PeakSnapshot>();
    snapshot->revision = nextRevision_++;

    auto extract = [](const QVariantMap& map, const QString& key) {
        return map.value(key).toList();
    };

    QVariantList normalizedMix;
    auto mix = std::make_shared<LayerSnapshot>();
    normalizeLayerInput(normalizedMix, extract(layers, QStringLiteral("mix")), mix);

    auto bass = std::make_shared<LayerSnapshot>();
    auto mid = std::make_shared<LayerSnapshot>();
    auto high = std::make_shared<LayerSnapshot>();
    QVariantList normalizedBass;
    QVariantList normalizedMid;
    QVariantList normalizedHigh;
    normalizeLayerInput(normalizedBass, extract(layers, QStringLiteral("bass")), bass);
    normalizeLayerInput(normalizedMid, extract(layers, QStringLiteral("mid")), mid);
    normalizeLayerInput(normalizedHigh, extract(layers, QStringLiteral("high")), high);

    snapshot->mix = mix->values.empty() ? nullptr : std::move(mix);
    snapshot->bass = bass->values.empty() ? nullptr : std::move(bass);
    snapshot->mid = mid->values.empty() ? nullptr : std::move(mid);
    snapshot->high = high->values.empty() ? nullptr : std::move(high);

    layers_ = layers;
    peaks_.clear();
    peakSnapshot_ = std::move(snapshot);
    emit peaksChanged();
    emit layersChanged();
    update();
}

void WaveformItem::normalizeLayerInput(QVariantList& normalized,
                                       const QVariantList& input,
                                       std::shared_ptr<LayerSnapshot>& snapshot)
{
    normalized.clear();
    snapshot->values.clear();
    normalized.reserve(input.size());
    snapshot->values.reserve(static_cast<std::size_t>(input.size()));
    for (const QVariant& value : input) {
        bool converted = false;
        double peak = value.toDouble(&converted);
        if (!converted || !std::isfinite(peak)) {
            peak = 0.0;
        }
        peak = std::clamp(std::abs(peak), 0.0, 1.0);
        snapshot->values.push_back(static_cast<float>(peak));
        normalized.append(peak);
    }
}

qreal WaveformItem::position() const
{
    return static_cast<qreal>(position_);
}

void WaveformItem::setPosition(qreal position)
{
    const qint64 integralPosition = std::isfinite(position) ? qRound64(position) : 0;
    const qint64 upperBound = duration_ > 0 ? duration_ : 0;
    const qint64 clamped = std::clamp(integralPosition, qint64{0}, upperBound);
    if (clamped == position_) {
        return;
    }
    position_ = clamped;
    emit positionChanged();
    update();
}

qreal WaveformItem::duration() const
{
    return static_cast<qreal>(duration_);
}

void WaveformItem::setDuration(qreal duration)
{
    const qint64 integralDuration = std::isfinite(duration) ? qRound64(duration) : 0;
    const qint64 clamped = std::max(integralDuration, qint64{0});
    if (clamped == duration_) {
        return;
    }
    duration_ = clamped;
    emit durationChanged();

    if (position_ > duration_) {
        position_ = duration_;
        emit positionChanged();
    }
    if (hoverPosition_ > duration_) {
        setHoverPosition(duration_);
    }
    update();
}

QColor WaveformItem::waveformColor() const
{
    return waveformColor_;
}

void WaveformItem::setWaveformColor(const QColor& color)
{
    if (color == waveformColor_) {
        return;
    }
    waveformColor_ = color;
    emit waveformColorChanged();
    update();
}

qint64 WaveformItem::hoverPosition() const
{
    return hoverPosition_;
}

double WaveformItem::analysisProgress() const
{
    return analysisProgress_;
}

void WaveformItem::setAnalysisProgress(double progress)
{
    const double finite = std::isfinite(progress) ? progress : 0.0;
    const double clamped = std::clamp(finite, 0.0, 1.0);
    if (qFuzzyCompare(clamped, analysisProgress_)) {
        return;
    }
    analysisProgress_ = clamped;
    emit analysisProgressChanged();
}

qint64 WaveformItem::timeForX(qreal x) const
{
    if (width() <= 0.0 || duration_ <= 0 || !std::isfinite(x)) {
        return 0;
    }
    if (x <= 0.0) {
        return 0;
    }
    if (x >= width()) {
        return duration_;
    }

    const long double ratio = static_cast<long double>(x / width());
    const long double rounded = std::floor(
        ratio * static_cast<long double>(duration_) + static_cast<long double>(0.5));
    if (rounded >= static_cast<long double>(duration_)) {
        return duration_;
    }
    return static_cast<qint64>(rounded);
}

QSGNode* WaveformItem::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*)
{
    // Qt calls this during Scene Graph synchronization while the GUI thread is blocked,
    // so copying the shared immutable snapshot here hands one stable revision to rendering.
    const std::shared_ptr<const PeakSnapshot> snapshot = peakSnapshot_;
    const bool hasMix = snapshot && snapshot->mix && !snapshot->mix->values.empty();
    const bool hasBass = snapshot && snapshot->bass && !snapshot->bass->values.empty();
    const bool hasMid = snapshot && snapshot->mid && !snapshot->mid->values.empty();
    const bool hasHigh = snapshot && snapshot->high && !snapshot->high->values.empty();
    const bool hasAny = hasMix || hasBass || hasMid || hasHigh;
    if (!hasAny || width() <= 0.0 || height() <= 0.0) {
        delete oldNode;
        return nullptr;
    }

    auto* node = static_cast<WaveformNode*>(oldNode);
    if (node == nullptr) {
        node = new WaveformNode();
    }

    const qreal devicePixelRatio = window() ? window()->devicePixelRatio() : 1.0;
    const std::size_t maxPoints = std::max<std::size_t>(
        1U,
        static_cast<std::size_t>(
            std::ceil(std::max(0.0, width() * devicePixelRatio / 4.0))));

    // Use the mix layer for geometry sizing when available; otherwise use the
    // first present frequency layer so all layers share the same point count.
    const std::vector<float>* sizeReference = nullptr;
    if (hasMix) {
        sizeReference = &snapshot->mix->values;
    } else if (hasBass) {
        sizeReference = &snapshot->bass->values;
    } else if (hasMid) {
        sizeReference = &snapshot->mid->values;
    } else {
        sizeReference = &snapshot->high->values;
    }
    const std::size_t rawPeakCount = sizeReference->size();
    const std::size_t peakCount = std::min(rawPeakCount, maxPoints);

    const unsigned char layerMask =
        (hasMix ? 1U : 0U)
        | (hasBass ? 2U : 0U)
        | (hasMid ? 4U : 0U)
        | (hasHigh ? 8U : 0U);

    const bool geometryChanged = node->revision_ != snapshot->revision
                                 || !qFuzzyCompare(node->width_, width())
                                 || !qFuzzyCompare(node->height_, height())
                                 || !qFuzzyCompare(node->devicePixelRatio_, devicePixelRatio)
                                 || node->layerMask_ != layerMask;

    if (geometryChanged) {
        const std::size_t activeLayers =
            (hasMix ? 1U : 0U) + (hasBass ? 1U : 0U) + (hasMid ? 1U : 0U) + (hasHigh ? 1U : 0U);
        const auto vertexCount = peakCount * 2U * activeLayers;
        if (vertexCount > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
            delete node;
            return nullptr;
        }

        node->geometry_.allocate(static_cast<int>(vertexCount));
        auto* vertices = node->geometry_.vertexDataAsColoredPoint2D();
        const float center = static_cast<float>(height() * 0.5);
        const std::size_t playedCount = computePlayedCount(peakCount, position_, duration_);

        const auto downsampleLayer = [](const std::vector<float>& values,
                                        std::size_t peakCount,
                                        std::size_t maxPoints) {
            std::vector<float> result;
            if (values.empty() || peakCount == 0U) {
                return result;
            }
            result.resize(peakCount);
            if (values.size() <= maxPoints) {
                for (std::size_t i = 0U; i < peakCount; ++i) {
                    result[i] = values[i];
                }
                return result;
            }
            const std::size_t bucketSize = values.size() / peakCount;
            const std::size_t remainder = values.size() % peakCount;
            std::size_t rawIndex = 0U;
            for (std::size_t index = 0U; index < peakCount; ++index) {
                const std::size_t extra = index < remainder ? 1U : 0U;
                const std::size_t end = rawIndex + bucketSize + extra;
                float bucketMax = 0.0F;
                for (std::size_t i = rawIndex; i < end; ++i) {
                    bucketMax = std::max(bucketMax, values[i]);
                }
                result[index] = bucketMax;
                rawIndex = end;
            }
            return result;
        };

        const std::vector<float> mixValues = hasMix
                                                 ? downsampleLayer(snapshot->mix->values, peakCount, maxPoints)
                                                 : std::vector<float>{};
        const std::vector<float> bassValues = hasBass
                                                  ? downsampleLayer(snapshot->bass->values, peakCount, maxPoints)
                                                  : std::vector<float>{};
        const std::vector<float> midValues = hasMid
                                                 ? downsampleLayer(snapshot->mid->values, peakCount, maxPoints)
                                                 : std::vector<float>{};
        const std::vector<float> highValues = hasHigh
                                                  ? downsampleLayer(snapshot->high->values, peakCount, maxPoints)
                                                  : std::vector<float>{};

        std::size_t vertexOffset = 0U;
        const auto writeLayer = [&](const std::vector<float>& values,
                                    const LayerColor* color) {
            if (values.empty()) {
                return;
            }
            for (std::size_t index = 0U; index < peakCount; ++index) {
                const double normalizedX = peakCount == 1U
                                               ? 0.5
                                               : static_cast<double>(index)
                                                     / static_cast<double>(peakCount - 1U);
                const float x = static_cast<float>(normalizedX * width());
                const float amplitude = values[index] * center;

                unsigned char red = 0;
                unsigned char green = 0;
                unsigned char blue = 0;
                unsigned char playedAlpha = 255;
                unsigned char unplayedAlpha = WaveformItem::unplayedAlpha();
                if (color != nullptr) {
                    red = color->red;
                    green = color->green;
                    blue = color->blue;
                    playedAlpha = color->playedAlpha;
                    unplayedAlpha = color->unplayedAlpha;
                } else if (waveformColor_.isValid()) {
                    red = static_cast<unsigned char>(waveformColor_.red());
                    green = static_cast<unsigned char>(waveformColor_.green());
                    blue = static_cast<unsigned char>(waveformColor_.blue());
                } else {
                    const Rgb c = gradientColor(normalizedX);
                    red = static_cast<unsigned char>(c.red);
                    green = static_cast<unsigned char>(c.green);
                    blue = static_cast<unsigned char>(c.blue);
                }
                const auto alpha = static_cast<unsigned char>(
                    index < playedCount ? playedAlpha : unplayedAlpha);

                vertices[vertexOffset + index * 2U].set(
                    x, center - amplitude, red, green, blue, alpha);
                vertices[vertexOffset + index * 2U + 1U].set(
                    x, center + amplitude, red, green, blue, alpha);
            }
            vertexOffset += peakCount * 2U;
        };

        writeLayer(mixValues, nullptr);
        writeLayer(bassValues, &kBassColor);
        writeLayer(midValues, &kMidColor);
        writeLayer(highValues, &kHighColor);

        node->revision_ = snapshot->revision;
        node->width_ = width();
        node->height_ = height();
        node->devicePixelRatio_ = devicePixelRatio;
        node->position_ = position_;
        node->duration_ = duration_;
        node->waveformColor_ = waveformColor_;
        node->peakCount_ = peakCount;
        node->playedCount_ = playedCount;
        node->layerMask_ = layerMask;
        node->markDirty(QSGNode::DirtyGeometry | QSGNode::DirtyMaterial);
        return node;
    }

    const std::size_t newPlayedCount = computePlayedCount(peakCount, position_, duration_);
    const bool colorChanged = node->waveformColor_ != waveformColor_
                              || node->playedCount_ != newPlayedCount;
    if (!colorChanged) {
        return node;
    }

    auto* vertices = node->geometry_.vertexDataAsColoredPoint2D();
    std::size_t vertexOffset = 0U;
    if (hasMix) {
        updateMixVertexColors(
            vertices + vertexOffset, peakCount, newPlayedCount, waveformColor_);
        vertexOffset += peakCount * 2U;
    }
    if (hasBass) {
        updateLayerVertexColors(
            vertices + vertexOffset, peakCount, newPlayedCount, kBassColor);
        vertexOffset += peakCount * 2U;
    }
    if (hasMid) {
        updateLayerVertexColors(
            vertices + vertexOffset, peakCount, newPlayedCount, kMidColor);
        vertexOffset += peakCount * 2U;
    }
    if (hasHigh) {
        updateLayerVertexColors(
            vertices + vertexOffset, peakCount, newPlayedCount, kHighColor);
        vertexOffset += peakCount * 2U;
    }

    node->position_ = position_;
    node->duration_ = duration_;
    node->waveformColor_ = waveformColor_;
    node->playedCount_ = newPlayedCount;
    node->markDirty(QSGNode::DirtyMaterial);
    return node;
}

void WaveformItem::hoverMoveEvent(QHoverEvent* event)
{
    setHoverPosition(timeForX(event->position().x()));
    event->accept();
}

void WaveformItem::hoverLeaveEvent(QHoverEvent* event)
{
    if (!pointerPressed_) {
        setHoverPosition(-1);
    }
    event->accept();
}

void WaveformItem::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }
    pointerPressed_ = true;
    setHoverPosition(timeForX(event->position().x()));
    event->accept();
}

void WaveformItem::mouseMoveEvent(QMouseEvent* event)
{
    if (!pointerPressed_) {
        event->ignore();
        return;
    }
    setHoverPosition(timeForX(event->position().x()));
    event->accept();
}

void WaveformItem::mouseReleaseEvent(QMouseEvent* event)
{
    if (!pointerPressed_ || event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }
    pointerPressed_ = false;
    const qint64 committedPosition = timeForX(event->position().x());
    setHoverPosition(committedPosition);
    emit seekRequested(committedPosition);
    event->accept();
}

void WaveformItem::mouseUngrabEvent()
{
    pointerPressed_ = false;
}

void WaveformItem::setHoverPosition(qint64 position)
{
    if (position == hoverPosition_) {
        return;
    }
    hoverPosition_ = position;
    emit hoverPositionChanged();
}
