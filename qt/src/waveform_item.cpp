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

Rgb gradientColor(double normalizedX,
                  const QColor& start,
                  const QColor& middle,
                  const QColor& end)
{
    const double clamped = std::clamp(normalizedX, 0.0, 1.0);
    const QColor& from = clamped < 0.5 ? start : middle;
    const QColor& to = clamped < 0.5 ? middle : end;
    const double fraction = clamped < 0.5 ? clamped * 2.0
                                          : (clamped - 0.5) * 2.0;
    const auto interpolate = [fraction](int a, int b) {
        return static_cast<int>(std::lround(
            static_cast<double>(a) + static_cast<double>(b - a) * fraction));
    };
    return {interpolate(from.red(), to.red()),
            interpolate(from.green(), to.green()),
            interpolate(from.blue(), to.blue())};
}

struct VertexColor {
    Rgb rgb;
    unsigned char alpha;
};

VertexColor mixColor(double normalizedX,
                     bool played,
                     int visualMode,
                     const QColor& legacyColor,
                     const QColor& baseColor,
                     const QColor& progressColor,
                     const QColor& gradientStart,
                     const QColor& gradientMiddle,
                     const QColor& gradientEnd,
                     bool rgbProgress)
{
    if (visualMode == 0) {
        const QColor color = played ? progressColor : baseColor;
        return {{color.red(), color.green(), color.blue()}, 255U};
    }
    if (visualMode == 1) {
        if (played == rgbProgress) {
            return {gradientColor(normalizedX, gradientStart, gradientMiddle,
                                  gradientEnd), 255U};
        }
        return {{baseColor.red(), baseColor.green(), baseColor.blue()}, 255U};
    }
    if (visualMode == 2) {
        // The spectrum is rendered twice: the complete, unplayed canvas and
        // the clipped played canvas.  Preserve that distinction so a seek
        // immediately moves the played colour with the real playback clock.
        const Rgb color = played
            ? gradientColor(normalizedX, gradientStart, gradientMiddle, gradientEnd)
            : Rgb{baseColor.red(), baseColor.green(), baseColor.blue()};
        return {color, 255U};
    }
    if (legacyColor.isValid()) {
        return {{legacyColor.red(), legacyColor.green(), legacyColor.blue()},
                static_cast<unsigned char>(
                    played ? 255U : WaveformItem::unplayedAlpha())};
    }
    return {gradientColor(normalizedX),
            static_cast<unsigned char>(
                played ? 255U : WaveformItem::unplayedAlpha())};
}

std::size_t computePlayedCount(std::size_t peakCount, qint64 position, qint64 duration)
{
    if (peakCount == 0U) {
        return 0U;
    }
    if (duration <= 0) {
        return 1U;
    }
    if (position <= 0) {
        return 0U;
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
                           const QColor& waveformColor,
                           int visualMode,
                           const QColor& baseColor,
                           const QColor& progressColor,
                           const QColor& gradientStart,
                           const QColor& gradientMiddle,
                           const QColor& gradientEnd,
                           bool rgbProgress,
                           std::size_t strokeCopies)
{
    for (std::size_t copy = 0; copy < strokeCopies; ++copy) {
        for (std::size_t index = 0; index < peakCount; ++index) {
            const double normalizedX = peakCount == 1U
                                           ? 0.5
                                           : static_cast<double>(index)
                                                 / static_cast<double>(peakCount - 1U);
            const VertexColor color =
                mixColor(normalizedX, index < playedCount, visualMode,
                         waveformColor, baseColor, progressColor, gradientStart,
                         gradientMiddle, gradientEnd, rgbProgress);

            const std::size_t vertex = (copy * peakCount + index) * 2U;
            vertices[vertex].r = static_cast<unsigned char>(color.rgb.red);
            vertices[vertex].g = static_cast<unsigned char>(color.rgb.green);
            vertices[vertex].b = static_cast<unsigned char>(color.rgb.blue);
            vertices[vertex].a = color.alpha;
            vertices[vertex + 1U].r = static_cast<unsigned char>(color.rgb.red);
            vertices[vertex + 1U].g = static_cast<unsigned char>(color.rgb.green);
            vertices[vertex + 1U].b = static_cast<unsigned char>(color.rgb.blue);
            vertices[vertex + 1U].a = color.alpha;
        }
    }
}

std::size_t renderedSpectrumBarCount(qreal width, qreal devicePixelRatio)
{
    // Keep the visualizer dense on wide windows.  The audio engine still
    // produces a small fixed spectrum; resample it at paint time instead of
    // leaving unused rails at both ends of the waveform canvas.
    constexpr qreal desiredStride = WaveformItem::spectrumBarWidth()
        + WaveformItem::spectrumBarGap();
    const qreal physicalWidth = std::max<qreal>(0.0, width * devicePixelRatio);
    const auto forWidth = static_cast<std::size_t>(std::ceil(
        physicalWidth / desiredStride));
    return std::clamp(forWidth, std::size_t{1U}, std::size_t{1024U});
}

std::vector<float> resampleValues(const std::vector<float>& values,
                                  std::size_t pointCount)
{
    std::vector<float> result;
    if (values.empty() || pointCount == 0U) {
        return result;
    }
    result.resize(pointCount);
    if (values.size() == pointCount) {
        std::copy(values.begin(), values.end(), result.begin());
        return result;
    }
    if (values.size() < pointCount) {
        if (values.size() == 1U) {
            std::fill(result.begin(), result.end(), values.front());
            return result;
        }
        for (std::size_t index = 0U; index < pointCount; ++index) {
            const double sourcePosition = pointCount == 1U
                ? 0.0
                : static_cast<double>(index)
                    * static_cast<double>(values.size() - 1U)
                    / static_cast<double>(pointCount - 1U);
            const std::size_t left = static_cast<std::size_t>(sourcePosition);
            const std::size_t right = std::min(left + 1U, values.size() - 1U);
            const double fraction = sourcePosition - static_cast<double>(left);
            result[index] = static_cast<float>(
                values[left] + (values[right] - values[left]) * fraction);
        }
        return result;
    }

    const std::size_t bucketSize = values.size() / pointCount;
    const std::size_t remainder = values.size() % pointCount;
    std::size_t rawIndex = 0U;
    for (std::size_t index = 0U; index < pointCount; ++index) {
        const std::size_t end = rawIndex + bucketSize + (index < remainder ? 1U : 0U);
        float bucketMax = 0.0F;
        for (std::size_t i = rawIndex; i < end; ++i) {
            bucketMax = std::max(bucketMax, values[i]);
        }
        result[index] = bucketMax;
        rawIndex = end;
    }
    return result;
}

void updateLayerVertexColors(QSGGeometry::ColoredPoint2D* vertices,
                             std::size_t peakCount,
                             std::size_t playedCount,
                             const LayerColor& color,
                             std::size_t strokeCopies)
{
    for (std::size_t copy = 0; copy < strokeCopies; ++copy) {
        for (std::size_t index = 0; index < peakCount; ++index) {
            const auto alpha = static_cast<unsigned char>(
                index < playedCount ? color.playedAlpha : color.unplayedAlpha);
            const std::size_t vertex = (copy * peakCount + index) * 2U;
            vertices[vertex].r = color.red;
            vertices[vertex].g = color.green;
            vertices[vertex].b = color.blue;
            vertices[vertex].a = alpha;
            vertices[vertex + 1U].r = color.red;
            vertices[vertex + 1U].g = color.green;
            vertices[vertex + 1U].b = color.blue;
            vertices[vertex + 1U].a = alpha;
        }
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
    qreal density_ = -1.0;
    qreal lineWidth_ = -1.0;
    qint64 position_ = -1;
    qint64 duration_ = -1;
    QColor waveformColor_;
    int visualMode_ = -2;
    QColor baseColor_;
    QColor progressColor_;
    QColor gradientStartColor_;
    QColor gradientMiddleColor_;
    QColor gradientEndColor_;
    bool rgbProgress_ = true;
    qreal amplitudeScale_ = -1.0;
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
    std::vector<float> inputValues;
    inputValues.reserve(static_cast<std::size_t>(peaks.size()));

    for (const QVariant& value : peaks) {
        bool converted = false;
        double peak = value.toDouble(&converted);
        if (!converted || !std::isfinite(peak)) {
            peak = 0.0;
        }
        peak = std::clamp(std::abs(peak), 0.0, 1.0);
        inputValues.push_back(static_cast<float>(peak));
        normalizedPeaks.append(peak);
    }

    auto mix = std::make_shared<LayerSnapshot>();
    if (visualMode_ == 2 && !inputValues.empty()) {
        const std::vector<float> target = resampleValues(
            inputValues, static_cast<std::size_t>(spectrumBarCount()));
        const bool initialize = spectrumVisual_.size() != target.size()
                                || spectrumPeakHold_.size() != target.size();
        double elapsedSeconds = 1.0 / 60.0;
        if (spectrumTimer_.isValid()) {
            elapsedSeconds = std::clamp(
                static_cast<double>(spectrumTimer_.restart()) / 1000.0,
                0.001, 0.1);
        } else {
            spectrumTimer_.start();
        }
        if (initialize) {
            spectrumVisual_ = target;
            spectrumPeakHold_ = target;
        } else {
            for (std::size_t index = 0U; index < target.size(); ++index) {
                const double seconds = target[index] > spectrumVisual_[index]
                    ? spectrumAttackSeconds() : spectrumDecaySeconds();
                const float alpha = static_cast<float>(
                    1.0 - std::exp(-elapsedSeconds / seconds));
                spectrumVisual_[index] += (target[index] - spectrumVisual_[index]) * alpha;
                if (target[index] >= spectrumPeakHold_[index]) {
                    spectrumPeakHold_[index] = target[index];
                } else {
                    const float peakAlpha = static_cast<float>(
                        1.0 - std::exp(-elapsedSeconds
                                       / spectrumPeakFallSeconds()));
                    spectrumPeakHold_[index] +=
                        (target[index] - spectrumPeakHold_[index]) * peakAlpha;
                }
            }
        }
        mix->values = spectrumVisual_;
        auto held = std::make_shared<LayerSnapshot>();
        held->values = spectrumPeakHold_;
        snapshot->spectrumPeakHold = std::move(held);
    } else {
        mix->values = std::move(inputValues);
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

int WaveformItem::visualMode() const noexcept { return visualMode_; }

void WaveformItem::setVisualMode(int mode)
{
    const int clamped = std::clamp(mode, -1, 2);
    if (visualMode_ == clamped) {
        return;
    }
    visualMode_ = clamped;
    spectrumVisual_.clear();
    spectrumPeakHold_.clear();
    spectrumTimer_.invalidate();
    emit visualModeChanged();
    update();
}

QColor WaveformItem::baseColor() const { return baseColor_; }

void WaveformItem::setBaseColor(const QColor& color)
{
    if (!color.isValid() || baseColor_ == color) {
        return;
    }
    baseColor_ = color;
    emit baseColorChanged();
    update();
}

QColor WaveformItem::progressColor() const { return progressColor_; }

void WaveformItem::setProgressColor(const QColor& color)
{
    if (!color.isValid() || progressColor_ == color) {
        return;
    }
    progressColor_ = color;
    emit progressColorChanged();
    update();
}

#define AGPLAYER_WAVEFORM_COLOR_ACCESSORS(Getter, Setter, Member, Signal) \
    QColor WaveformItem::Getter() const { return Member; }                \
    void WaveformItem::Setter(const QColor& color)                        \
    {                                                                     \
        if (!color.isValid() || Member == color) {                        \
            return;                                                       \
        }                                                                 \
        Member = color;                                                   \
        emit Signal();                                                    \
        update();                                                         \
    }

AGPLAYER_WAVEFORM_COLOR_ACCESSORS(
    gradientStartColor, setGradientStartColor, gradientStartColor_,
    gradientStartColorChanged)
AGPLAYER_WAVEFORM_COLOR_ACCESSORS(
    gradientMiddleColor, setGradientMiddleColor, gradientMiddleColor_,
    gradientMiddleColorChanged)
AGPLAYER_WAVEFORM_COLOR_ACCESSORS(
    gradientEndColor, setGradientEndColor, gradientEndColor_,
    gradientEndColorChanged)

#undef AGPLAYER_WAVEFORM_COLOR_ACCESSORS

bool WaveformItem::rgbProgress() const noexcept { return rgbProgress_; }

void WaveformItem::setRgbProgress(bool value)
{
    if (rgbProgress_ == value) {
        return;
    }
    rgbProgress_ = value;
    emit rgbProgressChanged();
    update();
}

qreal WaveformItem::amplitudeScale() const noexcept { return amplitudeScale_; }

void WaveformItem::setAmplitudeScale(qreal value)
{
    const qreal finite = std::isfinite(value) ? value : 0.8;
    const qreal clamped = std::clamp(finite, qreal{0.3}, qreal{1.5});
    if (qFuzzyCompare(amplitudeScale_, clamped)) {
        return;
    }
    amplitudeScale_ = clamped;
    emit amplitudeScaleChanged();
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

qreal WaveformItem::density() const
{
    return density_;
}

void WaveformItem::setDensity(qreal density)
{
    const qreal finite = std::isfinite(density) ? density : 2.0;
    const qreal clamped = std::clamp(finite, qreal{0.15}, qreal{5.0});
    if (qFuzzyCompare(clamped, density_)) {
        return;
    }
    density_ = clamped;
    emit densityChanged();
    update();
}

qreal WaveformItem::lineWidth() const
{
    return lineWidth_;
}

void WaveformItem::setLineWidth(qreal width)
{
    const qreal finite = std::isfinite(width) ? width : 1.0;
    const qreal clamped = std::clamp(finite, qreal{0.3}, qreal{8.0});
    if (qFuzzyCompare(clamped, lineWidth_)) {
        return;
    }
    lineWidth_ = clamped;
    emit lineWidthChanged();
    update();
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
            std::ceil(std::max(
                0.0, width() * devicePixelRatio * density_ / 2.0))));

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
    const std::size_t peakCount = visualMode_ == 2
        ? renderedSpectrumBarCount(width(), devicePixelRatio)
        : std::min(rawPeakCount, maxPoints);

    const unsigned char layerMask =
        (hasMix ? 1U : 0U)
        | (hasBass ? 2U : 0U)
        | (hasMid ? 4U : 0U)
        | (hasHigh ? 8U : 0U);

    const bool geometryChanged = node->revision_ != snapshot->revision
                                 || !qFuzzyCompare(node->width_, width())
                                 || !qFuzzyCompare(node->height_, height())
                                 || !qFuzzyCompare(node->devicePixelRatio_, devicePixelRatio)
                                 || !qFuzzyCompare(node->density_, density_)
                                 || !qFuzzyCompare(node->lineWidth_, lineWidth_)
                                 || node->visualMode_ != visualMode_
                                 || node->baseColor_ != baseColor_
                                 || node->progressColor_ != progressColor_
                                 || node->gradientStartColor_ != gradientStartColor_
                                 || node->gradientMiddleColor_ != gradientMiddleColor_
                                 || node->gradientEndColor_ != gradientEndColor_
                                 || node->rgbProgress_ != rgbProgress_
                                 || !qFuzzyCompare(node->amplitudeScale_,
                                                   amplitudeScale_)
                                 || (visualMode_ == 2
                                     && node->waveformColor_ != waveformColor_)
                                 || node->layerMask_ != layerMask;

    if (geometryChanged) {
        // Wide GPU lines are unsupported by several Qt RHI backends. Render
        // adjacent 1 px lines instead so thickness works without warnings.
        node->geometry_.setLineWidth(1.0F);
        const std::size_t strokeCopies = visualMode_ == 2
            ? static_cast<std::size_t>(spectrumBarWidth())
            : static_cast<std::size_t>(std::max(1.0, std::ceil(lineWidth_)));
        const std::size_t activeLayers = visualMode_ == 2
            ? (hasMix ? 1U : 0U)
            : (hasMix ? 1U : 0U) + (hasBass ? 1U : 0U)
                + (hasMid ? 1U : 0U) + (hasHigh ? 1U : 0U);
        // Spectrum bars are drawn as adjacent vertical 1 px lines plus a
        // horizontal cap. The cap makes each peak readable on dense displays
        // without introducing a separate scene-graph node per bar.
        const std::size_t spectrumCapVertices = visualMode_ == 2
            ? peakCount * 2U
            : 0U;
        const auto vertexCount = peakCount * 2U
            * activeLayers * strokeCopies + spectrumCapVertices;
        if (vertexCount > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
            delete node;
            return nullptr;
        }

        node->geometry_.allocate(static_cast<int>(vertexCount));
        auto* vertices = node->geometry_.vertexDataAsColoredPoint2D();
        const float center = static_cast<float>(height() * 0.5);
        const float spectrumBaseline = static_cast<float>(height());
        const std::size_t playedCount = computePlayedCount(peakCount, position_, duration_);

        std::vector<float> mixValues = hasMix
                                                 ? resampleValues(snapshot->mix->values, peakCount)
                                                  : std::vector<float>{};
        std::vector<float> heldSpectrumValues = visualMode_ == 2
            && snapshot->spectrumPeakHold
            && !snapshot->spectrumPeakHold->values.empty()
            ? resampleValues(snapshot->spectrumPeakHold->values, peakCount)
            : std::vector<float>{};
        const std::vector<float> bassValues = hasBass
                                                  ? resampleValues(snapshot->bass->values, peakCount)
                                                  : std::vector<float>{};
        const std::vector<float> midValues = hasMid
                                                 ? resampleValues(snapshot->mid->values, peakCount)
                                                 : std::vector<float>{};
        const std::vector<float> highValues = hasHigh
                                                  ? resampleValues(snapshot->high->values, peakCount)
                                                  : std::vector<float>{};

        if (visualMode_ == 2 && !mixValues.empty()) {
            const float peak = *std::max_element(mixValues.begin(), mixValues.end());
            if (peak > 0.0F) {
                const auto normalizeSpectrum = [peak](std::vector<float>& values) {
                    for (float& value : values) {
                        value = std::pow(
                            std::clamp(value / peak, 0.0F, 1.0F), 0.58F);
                    }
                };
                normalizeSpectrum(mixValues);
                normalizeSpectrum(heldSpectrumValues);
            }
        }

        std::size_t vertexOffset = 0U;
        const auto writeLayer = [&](const std::vector<float>& values,
                                    const LayerColor* color) {
            if (values.empty()) {
                return;
            }
            for (std::size_t copy = 0U; copy < strokeCopies; ++copy) {
                const qreal offset = static_cast<qreal>(copy)
                                     - static_cast<qreal>(strokeCopies - 1U) * 0.5;
                for (std::size_t index = 0U; index < peakCount; ++index) {
                    const double normalizedX = peakCount == 1U
                                                   ? 0.5
                                                   : static_cast<double>(index)
                                                         / static_cast<double>(peakCount - 1U);
                    const double spectrumNaturalSpan =
                        static_cast<double>(peakCount) * spectrumBarWidth()
                        + static_cast<double>(peakCount - 1U) * spectrumBarGap();
                    const double spectrumSpan = std::min(width(), spectrumNaturalSpan);
                    const double spectrumStart = (width() - spectrumSpan) * 0.5;
                    const double spectrumStride = peakCount <= 1U
                        ? 0.0
                        : (spectrumSpan - spectrumBarWidth())
                            / static_cast<double>(peakCount - 1U);
                    const double logicalX = visualMode_ == 2
                        ? spectrumStart + spectrumBarWidth() * 0.5
                            + static_cast<double>(index) * spectrumStride
                        : normalizedX * width();
                    const float x = static_cast<float>(std::clamp(
                        logicalX + offset, 0.0, width()));
                    const double spectrumEnvelope = visualMode_ == 2
                        ? 0.60 + 0.40 * std::sin(
                              normalizedX * 3.141592653589793)
                        : 1.0;
                    float amplitude = static_cast<float>(
                        values[index]
                        * (visualMode_ == 2
                               ? std::min(height(), spectrumMaxHeight())
                               : center)
                        * amplitudeScale_
                        * spectrumEnvelope);
                    if (visualMode_ != 2 && color == nullptr) {
                        amplitude = std::max(
                            amplitude, std::min(0.5F, center));
                    }

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
                    } else {
                        const VertexColor c =
                            mixColor(normalizedX, index < playedCount,
                                     visualMode_, waveformColor_, baseColor_,
                                     progressColor_, gradientStartColor_,
                                     gradientMiddleColor_, gradientEndColor_,
                                     rgbProgress_);
                        red = static_cast<unsigned char>(c.rgb.red);
                        green = static_cast<unsigned char>(c.rgb.green);
                        blue = static_cast<unsigned char>(c.rgb.blue);
                        playedAlpha = c.alpha;
                        unplayedAlpha = c.alpha;
                    }
                    const auto alpha = static_cast<unsigned char>(
                        index < playedCount ? playedAlpha : unplayedAlpha);
                    const std::size_t vertex =
                        vertexOffset + (copy * peakCount + index) * 2U;

                    if (visualMode_ == 2) {
                        vertices[vertex].set(
                            x, spectrumBaseline - amplitude,
                            red, green, blue, alpha);
                        vertices[vertex + 1U].set(
                            x, spectrumBaseline,
                            red, green, blue, alpha);
                    } else {
                        vertices[vertex].set(
                            x, center - amplitude, red, green, blue, alpha);
                        vertices[vertex + 1U].set(
                            x, center + amplitude, red, green, blue, alpha);
                    }
                }
            }
            if (visualMode_ == 2) {
                const float capHalfWidth = static_cast<float>(spectrumBarWidth() * 0.5);
                for (std::size_t index = 0U; index < peakCount; ++index) {
                    const double normalizedX = peakCount == 1U
                        ? 0.5
                        : static_cast<double>(index)
                            / static_cast<double>(peakCount - 1U);
                    const double spectrumNaturalSpan =
                        static_cast<double>(peakCount) * spectrumBarWidth()
                        + static_cast<double>(peakCount - 1U) * spectrumBarGap();
                    const double spectrumSpan = std::min(width(), spectrumNaturalSpan);
                    const double spectrumStart = (width() - spectrumSpan) * 0.5;
                    const double spectrumStride = peakCount <= 1U
                        ? 0.0
                        : (spectrumSpan - spectrumBarWidth())
                            / static_cast<double>(peakCount - 1U);
                    const float x = static_cast<float>(std::clamp(
                        spectrumStart + spectrumBarWidth() * 0.5
                            + static_cast<double>(index) * spectrumStride,
                        0.0, width()));
                    const double envelope = 0.60 + 0.40 * std::sin(
                        normalizedX * 3.141592653589793);
                    const float heldValue = heldSpectrumValues.empty()
                        ? values[index] : heldSpectrumValues[index];
                    const float amplitude = static_cast<float>(
                        heldValue * std::min(height(), spectrumMaxHeight())
                        * amplitudeScale_ * envelope);
                    const VertexColor capColor = mixColor(
                        normalizedX, index < playedCount, visualMode_, waveformColor_,
                        baseColor_, progressColor_, gradientStartColor_,
                        gradientMiddleColor_, gradientEndColor_, rgbProgress_);
                    const std::size_t vertex = vertexOffset
                        + peakCount * 2U * strokeCopies + index * 2U;
                    const float y = spectrumBaseline - amplitude;
                    vertices[vertex].set(
                        std::max(0.0F, x - capHalfWidth), y,
                        static_cast<unsigned char>(capColor.rgb.red),
                        static_cast<unsigned char>(capColor.rgb.green),
                        static_cast<unsigned char>(capColor.rgb.blue), capColor.alpha);
                    vertices[vertex + 1U].set(
                        std::min(static_cast<float>(width()), x + capHalfWidth), y,
                        static_cast<unsigned char>(capColor.rgb.red),
                        static_cast<unsigned char>(capColor.rgb.green),
                        static_cast<unsigned char>(capColor.rgb.blue), capColor.alpha);
                }
                vertexOffset += peakCount * 2U * (strokeCopies + 1U);
            } else {
                vertexOffset += peakCount * 2U * strokeCopies;
            }
        };

        writeLayer(mixValues, nullptr);
        if (visualMode_ != 2) {
            writeLayer(bassValues, &kBassColor);
            writeLayer(midValues, &kMidColor);
            writeLayer(highValues, &kHighColor);
        }

        node->revision_ = snapshot->revision;
        node->width_ = width();
        node->height_ = height();
        node->devicePixelRatio_ = devicePixelRatio;
        node->density_ = density_;
        node->lineWidth_ = lineWidth_;
        node->position_ = position_;
        node->duration_ = duration_;
        node->waveformColor_ = waveformColor_;
        node->visualMode_ = visualMode_;
        node->baseColor_ = baseColor_;
        node->progressColor_ = progressColor_;
        node->gradientStartColor_ = gradientStartColor_;
        node->gradientMiddleColor_ = gradientMiddleColor_;
        node->gradientEndColor_ = gradientEndColor_;
        node->rgbProgress_ = rgbProgress_;
        node->amplitudeScale_ = amplitudeScale_;
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
    const std::size_t strokeCopies = node->visualMode_ == 2
        ? static_cast<std::size_t>(spectrumBarWidth())
        : static_cast<std::size_t>(std::max(1.0, std::ceil(node->lineWidth_)));
    std::size_t vertexOffset = 0U;
    if (hasMix) {
        updateMixVertexColors(
            vertices + vertexOffset, peakCount, newPlayedCount, waveformColor_,
            visualMode_, baseColor_, progressColor_, gradientStartColor_,
            gradientMiddleColor_, gradientEndColor_, rgbProgress_,
            strokeCopies);
        vertexOffset += peakCount * 2U * strokeCopies;
        if (node->visualMode_ == 2) {
            // Peak-hold caps are a separate geometry range.  Recolor them in
            // the same update so their colour never trails the bar below.
            updateMixVertexColors(
                vertices + vertexOffset, peakCount, newPlayedCount, waveformColor_,
                visualMode_, baseColor_, progressColor_, gradientStartColor_,
                gradientMiddleColor_, gradientEndColor_, rgbProgress_, 1U);
            vertexOffset += peakCount * 2U;
        }
    }
    if (hasBass) {
        updateLayerVertexColors(
            vertices + vertexOffset, peakCount, newPlayedCount, kBassColor,
            strokeCopies);
        vertexOffset += peakCount * 2U * strokeCopies;
    }
    if (hasMid) {
        updateLayerVertexColors(
            vertices + vertexOffset, peakCount, newPlayedCount, kMidColor,
            strokeCopies);
        vertexOffset += peakCount * 2U * strokeCopies;
    }
    if (hasHigh) {
        updateLayerVertexColors(
            vertices + vertexOffset, peakCount, newPlayedCount, kHighColor,
            strokeCopies);
        vertexOffset += peakCount * 2U * strokeCopies;
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
