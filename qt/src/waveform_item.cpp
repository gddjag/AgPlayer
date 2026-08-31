#include "waveform_item.hpp"
#include "waveform_coordinate_mapper.hpp"
#include "waveform_layer_material.hpp"

#include <QHoverEvent>
#include <QMouseEvent>
#include <QQuickWindow>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QSGRendererInterface>
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
        // Spectrum colour represents frequency, not playback progress.  A
        // solid preset supplies three identical stops; custom RGB supplies
        // the configured start/middle/end stops.  Seeking must not recolour
        // bars or their peak-hold caps.
        return {gradientColor(normalizedX, gradientStart, gradientMiddle,
                              gradientEnd),
                255U};
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

std::size_t computePlayedCount(std::size_t peakCount,
                               qint64 position,
                               qint64 duration,
                               qint64 totalSamples)
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
    const qint64 sampleCount = totalSamples > 0 ? totalSamples : duration;
    const double playedIndex = WaveformCoordinateMapper::timeToPeak(
        position, duration, sampleCount, static_cast<qsizetype>(peakCount));
    if (peakCount == 1U) {
        return position >= duration / 2 ? 1U : 0U;
    }
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

void resampleValues(const std::vector<float>& values,
                    std::size_t pointCount,
                    std::vector<float>& result)
{
    if (values.empty() || pointCount == 0U) {
        result.clear();
        return;
    }
    result.resize(pointCount);
    if (values.size() == pointCount) {
        std::copy(values.begin(), values.end(), result.begin());
        return;
    }

    if (values.size() < pointCount) {
        if (values.size() == 1U) {
            std::fill(result.begin(), result.end(), values.front());
            return;
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
        return;
    }

    for (std::size_t index = 0U; index < pointCount; ++index) {
        // Use continuous source coverage per pixel.
        // Keep real peaks when a target pixel covers multiple source buckets,
        // use smooth linear interpolation only when the coverage is within
        // one source bucket.
        const double sourceStart =
            std::min(static_cast<double>(index) * static_cast<double>(values.size())
                         / static_cast<double>(pointCount),
                     static_cast<double>(values.size() - 1U));
        const double sourceEnd =
            std::min(static_cast<double>(index + 1U)
                         * static_cast<double>(values.size())
                         / static_cast<double>(pointCount),
                     static_cast<double>(values.size()));
        const std::size_t leftBucket =
            static_cast<std::size_t>(std::floor(sourceStart));
        const std::size_t rightBoundary =
            std::min(static_cast<std::size_t>(std::ceil(sourceEnd)),
                     values.size());

        if (rightBoundary <= leftBucket + 1U) {
            const double sourcePosition = std::clamp(
                (sourceStart + sourceEnd) * 0.5,
                0.0, static_cast<double>(values.size() - 1U));
            const std::size_t left = static_cast<std::size_t>(sourcePosition);
            const std::size_t right = std::min(left + 1U, values.size() - 1U);
            const double fraction = sourcePosition - static_cast<double>(left);
            result[index] = static_cast<float>(values[left]
                                               + (values[right] - values[left])
                                                     * fraction);
            continue;
        }

        float bucketMax = values[leftBucket];
        for (std::size_t i = leftBucket + 1U; i < rightBoundary; ++i) {
            bucketMax = std::max(bucketMax, values[i]);
        }
        result[index] = bucketMax;
    }
}

std::vector<float> resampleValues(const std::vector<float>& values,
                                  std::size_t pointCount)
{
    std::vector<float> result;
    resampleValues(values, pointCount, result);
    return result;
}

void resampleVisibleValues(const std::vector<float>& values,
                           qint64 visibleStartMs,
                           qint64 visibleEndMs,
                           qint64 durationMs,
                           std::size_t pointCount,
                           std::vector<float>& result)
{
    if (values.empty()) {
        result.clear();
        return;
    }
    if (durationMs <= 0
        || (visibleStartMs == 0 && visibleEndMs == durationMs)) {
        resampleValues(values, pointCount, result);
        return;
    }
    const std::size_t lastIndex = values.size() - 1U;
    const std::size_t first = std::min(lastIndex, static_cast<std::size_t>(
        std::floor(static_cast<double>(visibleStartMs)
                   / static_cast<double>(durationMs) * lastIndex)));
    const std::size_t last = std::min(lastIndex, std::max(first, static_cast<std::size_t>(
        std::ceil(static_cast<double>(visibleEndMs)
                  / static_cast<double>(durationMs) * lastIndex))));
    const std::vector<float> window(values.begin() + static_cast<qsizetype>(first),
                                    values.begin() + static_cast<qsizetype>(last) + 1);
    resampleValues(window, pointCount, result);
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
    qint64 visibleStartMs_ = -1;
    qint64 visibleEndMs_ = -1;
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
    std::vector<float> mixValues_;
    std::vector<float> heldSpectrumValues_;
};

} // namespace

void FrequencyFramePressure::resetWindow() noexcept
{
    slow_frames = 0;
    slow_window_.fill(0U);
    window_samples_ = 0;
    window_cursor_ = 0;
}

void FrequencyFramePressure::record(
    const std::chrono::milliseconds interval) noexcept
{
    const bool slow = interval > std::chrono::milliseconds{22};
    if (window_samples_ == static_cast<int>(slow_window_.size())) {
        slow_frames -= slow_window_[static_cast<std::size_t>(window_cursor_)] != 0U
            ? 1 : 0;
    } else {
        ++window_samples_;
    }
    slow_window_[static_cast<std::size_t>(window_cursor_)] = slow ? 1U : 0U;
    slow_frames += slow ? 1 : 0;
    window_cursor_ = (window_cursor_ + 1)
        % static_cast<int>(slow_window_.size());

    stable_frames = interval < std::chrono::milliseconds{18}
        ? stable_frames + 1 : 0;
    if (slow_frames >= 45 && quality_penalty < 2) {
        ++quality_penalty;
        stable_frames = 0;
        resetWindow();
        return;
    }
    if (stable_frames >= 240 && quality_penalty > 0) {
        --quality_penalty;
        stable_frames = 0;
        resetWindow();
    }
}

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
    snapshot->peakCount = peaks.size();

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
    snapshot->peakCount = std::max({normalizedMix.size(), normalizedBass.size(),
                                    normalizedMid.size(), normalizedHigh.size()});
    snapshot->sampleRate =
        std::max<qint64>(0, layers.value(QStringLiteral("_sampleRate")).toLongLong());
    snapshot->totalSamples =
        std::max<qint64>(0, layers.value(QStringLiteral("_totalSamples")).toLongLong());
    const qint64 declaredPeakCount =
        layers.value(QStringLiteral("_peakCount")).toLongLong();
    if (declaredPeakCount > 0) {
        snapshot->peakCount = static_cast<qsizetype>(declaredPeakCount);
    }

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

qreal WaveformItem::cursorPosition() const
{
    return static_cast<qreal>(cursorPosition_);
}

void WaveformItem::setCursorPosition(qreal position)
{
    const qint64 integralPosition = std::isfinite(position) ? qRound64(position) : 0;
    const qint64 clamped = std::clamp(integralPosition, qint64{0},
                                      std::max(duration_, qint64{0}));
    if (clamped == cursorPosition_) {
        return;
    }
    cursorPosition_ = clamped;
    emit cursorPositionChanged();
    emit waveformCursorXChanged();
    if (visualMode_ == 3) {
        recordFrequencyFrameInterval();
        update();
    }
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
    emit waveformCursorXChanged();
    recordFrequencyFrameInterval();
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
    const qint64 previousVisibleStart = visibleStartMs_;
    const qint64 previousVisibleEnd = visibleEndMs_;
    duration_ = clamped;
    visibleStartMs_ = std::clamp(visibleStartMs_, qint64{0}, duration_);
    visibleEndMs_ = std::clamp(visibleEndMs_, visibleStartMs_, duration_);
    if (visibleEndMs_ == visibleStartMs_) {
        visibleStartMs_ = 0;
        visibleEndMs_ = duration_;
    }
    emit durationChanged();
    if (previousVisibleStart != visibleStartMs_) {
        emit visibleStartMsChanged();
    }
    if (previousVisibleEnd != visibleEndMs_) {
        emit visibleEndMsChanged();
    }
    emit waveformCursorXChanged();

    if (position_ > duration_) {
        position_ = duration_;
        emit positionChanged();
    }
    if (cursorPosition_ > duration_) {
        cursorPosition_ = duration_;
        emit cursorPositionChanged();
    }
    if (hoverPosition_ > duration_) {
        setHoverPosition(duration_);
    }
    update();
}

qint64 WaveformItem::visibleStartMs() const noexcept
{
    return visibleStartMs_;
}

qint64 WaveformItem::visibleEndMs() const noexcept
{
    return visibleEndMs_;
}

void WaveformItem::setVisibleStartMs(qint64 startMs)
{
    setVisibleRange(startMs, visibleEndMs_);
}

void WaveformItem::setVisibleEndMs(qint64 endMs)
{
    setVisibleRange(visibleStartMs_, endMs);
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
    const int clamped = std::clamp(mode, -1, 3);
    if (visualMode_ == clamped) {
        return;
    }
    visualMode_ = clamped;
    spectrumVisual_.clear();
    spectrumPeakHold_.clear();
    spectrumTimer_.invalidate();
    frequencyFrameTimer_.invalidate();
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
AGPLAYER_WAVEFORM_COLOR_ACCESSORS(
    frequencyLowColor, setFrequencyLowColor, frequencyLowColor_,
    frequencyLowColorChanged)
AGPLAYER_WAVEFORM_COLOR_ACCESSORS(
    frequencyMidColor, setFrequencyMidColor, frequencyMidColor_,
    frequencyMidColorChanged)
AGPLAYER_WAVEFORM_COLOR_ACCESSORS(
    frequencyHighColor, setFrequencyHighColor, frequencyHighColor_,
    frequencyHighColorChanged)

#undef AGPLAYER_WAVEFORM_COLOR_ACCESSORS

qreal WaveformItem::frequencyStrength() const noexcept { return frequencyStrength_; }

void WaveformItem::setFrequencyStrength(qreal value)
{
    const qreal finite = std::isfinite(value) ? value : 0.85;
    const qreal clamped = std::clamp(finite, qreal{0.0}, qreal{1.0});
    if (qFuzzyCompare(frequencyStrength_, clamped)) {
        return;
    }
    frequencyStrength_ = clamped;
    emit frequencyStrengthChanged();
    update();
}

bool WaveformItem::frequencyDarkSurface() const noexcept
{
    return frequencyDarkSurface_;
}

void WaveformItem::setFrequencyDarkSurface(bool value)
{
    if (frequencyDarkSurface_ == value) {
        return;
    }
    frequencyDarkSurface_ = value;
    emit frequencyDarkSurfaceChanged();
    update();
}

QColor WaveformItem::frequencyMixColor() const { return frequencyMixColor_; }

void WaveformItem::setFrequencyMixColor(const QColor& color)
{
    if (!color.isValid() || frequencyMixColor_ == color) {
        return;
    }
    frequencyMixColor_ = color;
    emit frequencyStyleChanged();
    update();
}

namespace {

double finiteUnitValue(double value, double fallback)
{
    return std::clamp(std::isfinite(value) ? value : fallback, 0.0, 1.0);
}

} // namespace

#define AGPLAYER_FREQUENCY_OPACITY_ACCESSORS(Getter, Setter, Member) \
    double WaveformItem::Getter() const noexcept { return Member; }   \
    void WaveformItem::Setter(double value)                          \
    {                                                                \
        const double clamped = finiteUnitValue(value, Member);       \
        if (qFuzzyCompare(Member + 1.0, clamped + 1.0)) {             \
            return;                                                  \
        }                                                            \
        Member = clamped;                                            \
        emit frequencyStyleChanged();                                \
        update();                                                     \
    }

AGPLAYER_FREQUENCY_OPACITY_ACCESSORS(
    frequencyMixOpacity, setFrequencyMixOpacity, frequencyMixOpacity_)
AGPLAYER_FREQUENCY_OPACITY_ACCESSORS(
    frequencyLowOpacity, setFrequencyLowOpacity, frequencyLowOpacity_)
AGPLAYER_FREQUENCY_OPACITY_ACCESSORS(
    frequencyMidOpacity, setFrequencyMidOpacity, frequencyMidOpacity_)
AGPLAYER_FREQUENCY_OPACITY_ACCESSORS(
    frequencyHighOpacity, setFrequencyHighOpacity, frequencyHighOpacity_)
AGPLAYER_FREQUENCY_OPACITY_ACCESSORS(
    frequencyBandFade, setFrequencyBandFade, frequencyBandFade_)

#undef AGPLAYER_FREQUENCY_OPACITY_ACCESSORS

bool WaveformItem::frequencyPlayFocus() const noexcept
{
    return frequencyPlayFocus_;
}

void WaveformItem::setFrequencyPlayFocus(bool value)
{
    if (frequencyPlayFocus_ == value) {
        return;
    }
    frequencyPlayFocus_ = value;
    emit frequencyStyleChanged();
    update();
}

QColor WaveformItem::frequencyFocusColor() const
{
    return frequencyFocusColor_;
}

void WaveformItem::setFrequencyFocusColor(const QColor& color)
{
    if (!color.isValid() || frequencyFocusColor_ == color) {
        return;
    }
    frequencyFocusColor_ = color;
    emit frequencyStyleChanged();
    update();
}

int WaveformItem::effectiveFrequencyQuality() const noexcept
{
    if (window() != nullptr && window()->rendererInterface() != nullptr
        && window()->rendererInterface()->graphicsApi()
            == QSGRendererInterface::Software) {
        return 2;
    }
    return effectiveFrequencyQuality_;
}

std::uint64_t WaveformItem::frequencyGeometryRevision() const noexcept
{
    return frequencyGeometryRevision_;
}

std::uint64_t WaveformItem::frequencyMaterialRevision() const noexcept
{
    return frequencyMaterialRevision_;
}

void WaveformItem::recordFrequencyFrameInterval()
{
    if (visualMode_ != 3 || !isVisible()) {
        frequencyFrameTimer_.invalidate();
        return;
    }
    if (!frequencyFrameTimer_.isValid()) {
        frequencyFrameTimer_.start();
        return;
    }
    const qint64 elapsed = frequencyFrameTimer_.restart();
    const int previousPenalty = frequencyFramePressure_.quality_penalty;
    frequencyFramePressure_.record(std::chrono::milliseconds{
        std::max<qint64>(0, elapsed)});
    if (frequencyFramePressure_.quality_penalty != previousPenalty) {
        updateEffectiveFrequencyQuality();
    }
}

void WaveformItem::updateEffectiveFrequencyQuality()
{
    const int nextQuality = std::min(
        2, baseFrequencyQuality_ + frequencyFramePressure_.quality_penalty);
    if (effectiveFrequencyQuality_ == nextQuality) {
        return;
    }
    effectiveFrequencyQuality_ = nextQuality;
    emit effectiveFrequencyQualityChanged();
    update();
}

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
    const qreal width = renderWidth_ > 0.0 ? renderWidth_ : this->width();
    const qint64 span = visibleEndMs_ - visibleStartMs_;
    return visibleStartMs_ + WaveformCoordinateMapper::pixelToTime(x, width, span);
}

qreal WaveformItem::pixelForTime(qint64 positionMs) const
{
    const qreal width = renderWidth_ > 0.0 ? renderWidth_ : this->width();
    return WaveformCoordinateMapper::timeToPixel(
        positionMs - visibleStartMs_, visibleEndMs_ - visibleStartMs_, width);
}

void WaveformItem::zoomAt(qreal x, qreal factor)
{
    const qreal width = renderWidth_ > 0.0 ? renderWidth_ : this->width();
    if (duration_ <= 0 || width <= 0.0 || !std::isfinite(factor) || factor <= 0.0) {
        return;
    }

    const qint64 currentSpan = visibleEndMs_ - visibleStartMs_;
    const qint64 minimumSpan = std::max<qint64>(1, (duration_ + 7) / 8);
    const qint64 nextSpan = std::clamp(
        qRound64(static_cast<qreal>(currentSpan) / factor), minimumSpan, duration_);
    const qreal fraction = std::clamp(x / width, qreal{0.0}, qreal{1.0});
    const qint64 anchor = visibleStartMs_ + qRound64(fraction * currentSpan);
    const qint64 start = std::clamp(
        anchor - qRound64(fraction * nextSpan), qint64{0}, duration_ - nextSpan);
    setVisibleRange(start, start + nextSpan);
}

void WaveformItem::setHoverPositionForInteraction(qint64 position)
{
    setHoverPosition(position);
}

qreal WaveformItem::renderWidth() const noexcept
{
    return renderWidth_;
}

qreal WaveformItem::waveformCursorX() const noexcept
{
    const qint64 cursor = cursorPosition_ >= 0 ? cursorPosition_ : position_;
    return pixelForTime(cursor);
}

bool WaveformItem::pointerInteractionEnabled() const noexcept
{
    return pointerInteractionEnabled_;
}

void WaveformItem::setPointerInteractionEnabled(bool enabled)
{
    if (pointerInteractionEnabled_ == enabled) {
        return;
    }
    pointerInteractionEnabled_ = enabled;
    pointerPressed_ = false;
    emit pointerInteractionEnabledChanged();
}

qint64 WaveformItem::totalSamples() const noexcept
{
    return peakSnapshot_ ? peakSnapshot_->totalSamples : 0;
}

qint64 WaveformItem::sampleRate() const noexcept
{
    return peakSnapshot_ ? peakSnapshot_->sampleRate : 0;
}

qsizetype WaveformItem::peakCount() const noexcept
{
    return peakSnapshot_ ? peakSnapshot_->peakCount : 0;
}

void WaveformItem::geometryChange(const QRectF& newGeometry,
                                  const QRectF& oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    const qreal actualRenderWidth = std::max<qreal>(0.0, newGeometry.width());
    if (!qFuzzyCompare(renderWidth_ + 1.0, actualRenderWidth + 1.0)) {
        renderWidth_ = actualRenderWidth;
        emit renderWidthChanged();
        emit waveformCursorXChanged();
    }
    baseFrequencyQuality_ = newGeometry.height() >= 120.0
        ? 0 : newGeometry.height() >= 64.0 ? 1 : 2;
    updateEffectiveFrequencyQuality();
}

QSGNode* WaveformItem::updateFrequencyPaintNode(
    QSGNode* oldNode,
    const std::shared_ptr<const PeakSnapshot>& snapshot)
{
    if (oldNode != nullptr && oldNode->type() != QSGNode::BasicNodeType) {
        delete oldNode;
        oldNode = nullptr;
    }
    auto* root = static_cast<FrequencyWaveformRootNode*>(oldNode);
    if (root == nullptr) {
        root = new FrequencyWaveformRootNode();
    }

    const bool hasMix = snapshot->mix && !snapshot->mix->values.empty();
    const bool hasLow = snapshot->bass && !snapshot->bass->values.empty();
    const bool hasMid = snapshot->mid && !snapshot->mid->values.empty();
    const bool hasHigh = snapshot->high && !snapshot->high->values.empty();
    const unsigned char layerMask = (hasMix ? 1U : 0U)
        | (hasLow ? 2U : 0U) | (hasMid ? 4U : 0U) | (hasHigh ? 8U : 0U);

    const qreal dpr = std::clamp(
        window() ? window()->effectiveDevicePixelRatio() : qreal{1.0},
        qreal{1.0}, qreal{4.0});
    bool software = false;
    if (window() != nullptr && window()->rendererInterface() != nullptr) {
        software = software
            || window()->rendererInterface()->graphicsApi()
                == QSGRendererInterface::Software;
    }
    const int quality = effectiveFrequencyQuality();
    const bool lineFallback = software || quality == 2;
    const double qualityScale = quality == 0 ? 1.0 : quality == 1 ? 0.75 : 0.5;
    const std::size_t baseRequestedPoints = std::max<std::size_t>(
        2U, static_cast<std::size_t>(std::ceil(
                std::max(0.0, width() * dpr * density_ / 2.0))));
    const std::size_t requestedPoints = std::max<std::size_t>(
        2U, static_cast<std::size_t>(std::ceil(
                static_cast<double>(baseRequestedPoints) * qualityScale)));
    const std::size_t pointCount = lineFallback
        ? std::min<std::size_t>(512U, baseRequestedPoints)
        : requestedPoints;

    const bool geometryChanged = root->revision_ != snapshot->revision
        || !qFuzzyCompare(root->width_, width())
        || !qFuzzyCompare(root->height_, height())
        || !qFuzzyCompare(root->devicePixelRatio_, dpr)
        || !qFuzzyCompare(root->density_, density_)
        || !qFuzzyCompare(root->amplitudeScale_, amplitudeScale_)
        || root->duration_ != duration_
        || root->visibleStartMs_ != visibleStartMs_
        || root->visibleEndMs_ != visibleEndMs_
        || root->layerMask_ != layerMask
        || root->quality_ != quality
        || root->lineFallback_ != lineFallback;

    std::array<const std::vector<float>*, 4> sourceValues{{
        hasMix ? &snapshot->mix->values : nullptr,
        hasLow ? &snapshot->bass->values : nullptr,
        hasMid ? &snapshot->mid->values : nullptr,
        hasHigh ? &snapshot->high->values : nullptr,
    }};
    const bool compactMode = height() < 32.0 && (hasLow || hasMid || hasHigh);
    if (geometryChanged && compactMode) {
        const std::size_t combinedSize = std::max({
            hasLow ? snapshot->bass->values.size() : std::size_t{0},
            hasMid ? snapshot->mid->values.size() : std::size_t{0},
            hasHigh ? snapshot->high->values.size() : std::size_t{0},
        });
        root->compactBandValues_.assign(combinedSize, 0.0F);
        const auto combine = [root](
                                 const std::shared_ptr<const LayerSnapshot>& layer) {
            if (!layer) {
                return;
            }
            for (std::size_t index = 0; index < layer->values.size(); ++index) {
                root->compactBandValues_[index] = std::max(
                    root->compactBandValues_[index], layer->values[index]);
            }
        };
        combine(snapshot->bass);
        combine(snapshot->mid);
        combine(snapshot->high);
        ++root->compactBandBuildCount_;
    } else if (geometryChanged) {
        root->compactBandValues_.clear();
    }
    if (compactMode) {
        sourceValues[1] = &root->compactBandValues_;
        sourceValues[2] = nullptr;
        sourceValues[3] = nullptr;
    }

    if (geometryChanged) {
        ++frequencyGeometryRevision_;
        auto* baselineGeometry = root->baseline()->geometry();
        baselineGeometry->allocate(2);
        auto* baselineVertices = baselineGeometry->vertexDataAsColoredPoint2D();
        baselineVertices[0].x = 0.0F;
        baselineVertices[0].y = static_cast<float>(height() * 0.5);
        baselineVertices[1].x = static_cast<float>(width());
        baselineVertices[1].y = static_cast<float>(height() * 0.5);
        root->baseline()->markDirty(QSGNode::DirtyGeometry);

        const float center = static_cast<float>(height() * 0.5);
        const float outlineHalf = 0.4F;
        for (std::size_t roleIndex = 0; roleIndex < sourceValues.size(); ++roleIndex) {
            auto* layerNode = root->layers_[roleIndex];
            const auto* values = sourceValues[roleIndex];
            if (lineFallback) {
                layerNode->useLineFallback();
                QSGGeometry& geometry = layerNode->lineGeometry();
                if (values == nullptr) {
                    geometry.allocate(0);
                    continue;
                }
                std::vector<float> sampled;
                resampleVisibleValues(*values, visibleStartMs_, visibleEndMs_,
                                      duration_, pointCount, sampled);
                geometry.allocate(static_cast<int>(sampled.size() * 2U));
                auto* vertices = geometry.vertexDataAsColoredPoint2D();
                for (std::size_t index = 0; index < sampled.size(); ++index) {
                    const float normalizedX = sampled.size() <= 1U ? 0.5F
                        : static_cast<float>(index)
                            / static_cast<float>(sampled.size() - 1U);
                    const float x = normalizedX * static_cast<float>(width());
                    const float amplitude = std::max(
                        0.5F, sampled[index] * center
                            * static_cast<float>(amplitudeScale_));
                    vertices[index * 2U].x = x;
                    vertices[index * 2U].y = center - amplitude;
                    vertices[index * 2U + 1U].x = x;
                    vertices[index * 2U + 1U].y = center + amplitude;
                }
                geometry.setLineWidth(1.0F);
                layerNode->markDirty(QSGNode::DirtyGeometry);
                continue;
            }

            layerNode->useTriangleMaterial();
            QSGGeometry& geometry = layerNode->triangleGeometry();
            if (values == nullptr) {
                geometry.allocate(0, 0);
                continue;
            }
            std::vector<float> sampled;
            resampleVisibleValues(*values, visibleStartMs_, visibleEndMs_,
                                  duration_, pointCount, sampled);
            const std::size_t segmentCount = sampled.size() > 1U
                ? sampled.size() - 1U : 0U;
            const std::size_t vertexCount = segmentCount * 12U;
            const std::size_t indexCount = segmentCount * 18U;
            if (vertexCount > static_cast<std::size_t>(std::numeric_limits<int>::max())
                || indexCount > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
                geometry.allocate(0, 0);
                continue;
            }
            geometry.allocate(static_cast<int>(vertexCount),
                              static_cast<int>(indexCount));
            auto* vertices = static_cast<FrequencyWaveformVertex*>(
                geometry.vertexData());
            auto* indices = geometry.indexDataAsUInt();
            for (std::size_t segment = 0; segment < segmentCount; ++segment) {
                const float x0 = static_cast<float>(segment)
                    / static_cast<float>(segmentCount)
                    * static_cast<float>(width());
                const float x1 = static_cast<float>(segment + 1U)
                    / static_cast<float>(segmentCount)
                    * static_cast<float>(width());
                const float a0 = std::max(0.5F, sampled[segment] * center
                    * static_cast<float>(amplitudeScale_));
                const float a1 = std::max(0.5F, sampled[segment + 1U] * center
                    * static_cast<float>(amplitudeScale_));
                const float top0 = center - a0;
                const float top1 = center - a1;
                const float bottom0 = center + a0;
                const float bottom1 = center + a1;
                const std::size_t vertex = segment * 12U;
                vertices[vertex + 0U] = {x0, top0, 0.0F};
                vertices[vertex + 1U] = {x0, bottom0, 0.0F};
                vertices[vertex + 2U] = {x1, top1, 0.0F};
                vertices[vertex + 3U] = {x1, bottom1, 0.0F};
                vertices[vertex + 4U] = {x0, top0 - outlineHalf, 1.0F};
                vertices[vertex + 5U] = {x0, top0 + outlineHalf, 1.0F};
                vertices[vertex + 6U] = {x1, top1 - outlineHalf, 1.0F};
                vertices[vertex + 7U] = {x1, top1 + outlineHalf, 1.0F};
                vertices[vertex + 8U] = {x0, bottom0 - outlineHalf, 1.0F};
                vertices[vertex + 9U] = {x0, bottom0 + outlineHalf, 1.0F};
                vertices[vertex + 10U] = {x1, bottom1 - outlineHalf, 1.0F};
                vertices[vertex + 11U] = {x1, bottom1 + outlineHalf, 1.0F};
                const std::size_t output = segment * 18U;
                constexpr std::array<unsigned int, 18> localIndices{{
                    0, 1, 2, 2, 1, 3,
                    4, 5, 6, 6, 5, 7,
                    8, 9, 10, 10, 9, 11,
                }};
                for (std::size_t i = 0; i < localIndices.size(); ++i) {
                    indices[output + i] = static_cast<unsigned int>(
                        vertex + localIndices[i]);
                }
            }
            layerNode->markDirty(QSGNode::DirtyGeometry);
        }
        root->revision_ = snapshot->revision;
        root->width_ = width();
        root->height_ = height();
        root->devicePixelRatio_ = dpr;
        root->density_ = density_;
        root->amplitudeScale_ = amplitudeScale_;
        root->duration_ = duration_;
        root->visibleStartMs_ = visibleStartMs_;
        root->visibleEndMs_ = visibleEndMs_;
        root->layerMask_ = layerMask;
        root->quality_ = quality;
        root->lineFallback_ = lineFallback;
    }

    const bool baselineStyleChanged = geometryChanged
        || !root->baselineStyleInitialized_
        || root->baselineDarkSurface_ != frequencyDarkSurface_;
    if (baselineStyleChanged) {
        auto* baselineVertices = root->baseline()->geometry()
            ->vertexDataAsColoredPoint2D();
        const unsigned char baselineAlpha = frequencyDarkSurface_ ? 42U : 34U;
        const auto baselinePremultiply = [baselineAlpha](int channel) {
            return static_cast<unsigned char>(std::lround(
                static_cast<double>(channel) * baselineAlpha / 255.0));
        };
        for (int index = 0; index < 2; ++index) {
            baselineVertices[index].r = baselinePremultiply(128);
            baselineVertices[index].g = baselinePremultiply(136);
            baselineVertices[index].b = baselinePremultiply(148);
            baselineVertices[index].a = baselineAlpha;
        }
        root->baseline()->markDirty(QSGNode::DirtyGeometry);
        root->baselineStyleInitialized_ = true;
        root->baselineDarkSurface_ = frequencyDarkSurface_;
    }

    const double legacyScale = frequencyStrength_ / 0.62;
    const std::array<QColor, 4> colors{{
        frequencyMixColor_, frequencyLowColor_, frequencyMidColor_,
        frequencyHighColor_,
    }};
    const std::array<double, 4> opacities{{
        frequencyMixOpacity_, frequencyLowOpacity_, frequencyMidOpacity_,
        frequencyHighOpacity_,
    }};
    const std::array<double, 4> darkOutlineRatios{{
        0.38 / 0.18, 0.82 / 0.44, 0.80 / 0.38, 0.84 / 0.46,
    }};
    const std::array<double, 4> lightOutlineRatios{{
        0.36 / 0.14, 0.78 / 0.36, 0.76 / 0.32, 0.80 / 0.40,
    }};
    const float progressX = static_cast<float>(std::clamp(
        pixelForTime(cursorPosition_ >= 0 ? cursorPosition_ : position_),
        qreal{0.0}, width()));
    const float featherWidth = static_cast<float>(std::clamp(
        12.0 / dpr, 2.0, 12.0));
    for (std::size_t index = 0; index < root->layers_.size(); ++index) {
        auto* layerNode = root->layers_[index];
        const float fill = static_cast<float>(std::clamp(
            opacities[index] * legacyScale, 0.0, 1.0));
        const double ratio = frequencyDarkSurface_
            ? darkOutlineRatios[index] : lightOutlineRatios[index];
        const float outline = static_cast<float>(std::clamp(
            static_cast<double>(fill) * ratio, 0.0, 1.0));
        const float fade = index == 0U ? 1.0F
            : static_cast<float>(frequencyBandFade_);
        if (!lineFallback) {
            auto* material = layerNode->waveformMaterial();
            material->setStyle(colors[index], fill, outline, fade);
            material->setProgress(progressX, featherWidth);
            layerNode->markDirty(QSGNode::DirtyMaterial);
        } else if (sourceValues[index] != nullptr) {
            auto* vertices = layerNode->lineGeometry().vertexDataAsColoredPoint2D();
            const int vertexCount = layerNode->lineGeometry().vertexCount();
            const float alpha = outline * fade;
            const bool fallbackStyleChanged = geometryChanged
                || !root->fallbackStyleInitialized_[index]
                || root->fallbackColors_[index] != colors[index]
                || !qFuzzyCompare(root->fallbackAlphas_[index] + 1.0F,
                                  alpha + 1.0F);
            if (!fallbackStyleChanged) {
                continue;
            }
            const unsigned char byteAlpha = static_cast<unsigned char>(
                std::lround(alpha * 255.0F));
            const auto premultiply = [byteAlpha](int channel) {
                return static_cast<unsigned char>(std::lround(
                    static_cast<double>(channel) * byteAlpha / 255.0));
            };
            for (int vertex = 0; vertex < vertexCount; ++vertex) {
                vertices[vertex].r = premultiply(colors[index].red());
                vertices[vertex].g = premultiply(colors[index].green());
                vertices[vertex].b = premultiply(colors[index].blue());
                vertices[vertex].a = byteAlpha;
            }
            layerNode->markDirty(QSGNode::DirtyGeometry);
            root->fallbackColors_[index] = colors[index];
            root->fallbackAlphas_[index] = alpha;
            root->fallbackStyleInitialized_[index] = true;
        }
    }
    ++frequencyMaterialRevision_;

    auto* focusGeometry = root->focusGeometryNode()->geometry();
    const bool focusVisible = frequencyPlayFocus_
        && (cursorPosition_ >= 0 ? cursorPosition_ : position_) >= visibleStartMs_
        && (cursorPosition_ >= 0 ? cursorPosition_ : position_) <= visibleEndMs_;
    const bool focusGeometryChanged = geometryChanged
        || root->focusColor_ != frequencyFocusColor_
        || root->focusVisible_ != focusVisible;
    if (focusGeometryChanged && focusVisible) {
            focusGeometry->allocate(16, 24);
            auto* vertices = focusGeometry->vertexDataAsColoredPoint2D();
            auto* indices = focusGeometry->indexDataAsUShort();
            const std::array<float, 4> widths = quality == 0
                ? std::array<float, 4>{{8.0F, 4.0F, 1.0F, 6.0F}}
                : std::array<float, 4>{{4.0F, 2.0F, 1.0F, 6.0F}};
            const std::array<unsigned char, 4> alphas{{12U, 24U, 232U, 232U}};
            for (std::size_t quad = 0; quad < widths.size(); ++quad) {
                const float halfWidth = widths[quad] * 0.5F;
                const bool dot = quad == 3U;
                const float y0 = dot
                    ? static_cast<float>(height() * 0.5 - 3.0) : 0.0F;
                const float y1 = dot
                    ? static_cast<float>(height() * 0.5 + 3.0)
                    : static_cast<float>(height());
                const unsigned char alpha = alphas[quad];
                const auto premultiply = [alpha](int channel) {
                    return static_cast<unsigned char>(std::lround(
                        static_cast<double>(channel) * alpha / 255.0));
                };
                const std::size_t vertex = quad * 4U;
                vertices[vertex + 0U].set(-halfWidth, y0,
                    premultiply(frequencyFocusColor_.red()),
                    premultiply(frequencyFocusColor_.green()),
                    premultiply(frequencyFocusColor_.blue()), alpha);
                vertices[vertex + 1U] = vertices[vertex + 0U];
                vertices[vertex + 1U].x = halfWidth;
                vertices[vertex + 2U] = vertices[vertex + 0U];
                vertices[vertex + 2U].y = y1;
                vertices[vertex + 3U] = vertices[vertex + 1U];
                vertices[vertex + 3U].y = y1;
                const std::size_t output = quad * 6U;
                const quint16 base = static_cast<quint16>(vertex);
                indices[output + 0U] = base;
                indices[output + 1U] = base + 1U;
                indices[output + 2U] = base + 2U;
                indices[output + 3U] = base + 2U;
                indices[output + 4U] = base + 1U;
                indices[output + 5U] = base + 3U;
            }
            root->focusGeometryNode()->markDirty(
                QSGNode::DirtyGeometry | QSGNode::DirtyMaterial);
    } else if (focusGeometryChanged) {
        focusGeometry->allocate(0, 0);
        root->focusGeometryNode()->markDirty(QSGNode::DirtyGeometry);
    }
    if (focusVisible) {
        const qreal snappedX = std::round(progressX * dpr) / dpr;
        QMatrix4x4 transform;
        transform.translate(static_cast<float>(snappedX), 0.0F);
        root->focusTransform()->setMatrix(transform);
    }
    root->focusColor_ = frequencyFocusColor_;
    root->focusVisible_ = focusVisible;
    return root;
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
    const bool hasRenderableLayer = visualMode_ == 3 ? hasAny : hasMix;
    if (!hasRenderableLayer || width() <= 0.0 || height() <= 0.0) {
        delete oldNode;
        return nullptr;
    }

    if (visualMode_ == 3) {
        return updateFrequencyPaintNode(oldNode, snapshot);
    }

    if (oldNode != nullptr && oldNode->type() != QSGNode::GeometryNodeType) {
        delete oldNode;
        oldNode = nullptr;
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

    const std::size_t peakCount = visualMode_ == 2
        ? renderedSpectrumBarCount(width(), devicePixelRatio)
        // The analysed waveform stays immutable at its compact source
        // resolution.  Map it onto the complete display budget here so a
        // wider player gains visual detail instead of stretching a sparse
        // set of vertical lines.  resampleValues() interpolates on expansion
        // and preserves extrema when several source buckets share a pixel.
        : maxPoints;

    const unsigned char layerMask = hasMix ? 1U : 0U;

    const bool geometryChanged = node->revision_ != snapshot->revision
                                 || !qFuzzyCompare(node->width_, width())
                                 || !qFuzzyCompare(node->height_, height())
                                 || !qFuzzyCompare(node->devicePixelRatio_, devicePixelRatio)
                                 || !qFuzzyCompare(node->density_, density_)
                                 || !qFuzzyCompare(node->lineWidth_, lineWidth_)
                                 || node->visibleStartMs_ != visibleStartMs_
                                 || node->visibleEndMs_ != visibleEndMs_
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
        const double waveformLeftInset = std::min(
            width() * 0.5,
            static_cast<double>(strokeCopies) * 0.5
                + 0.5 / std::max(1.0, devicePixelRatio));
        const double waveformRightInset = std::min(
            width() - waveformLeftInset,
            static_cast<double>(strokeCopies) * 0.5);
        const double waveformSpan = std::max(
            0.0, width() - waveformLeftInset - waveformRightInset);
        const std::size_t activeLayers = visualMode_ == 2
            ? (hasMix ? 1U : 0U)
            : (hasMix ? 1U : 0U);
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

        if (node->geometry_.vertexCount() != static_cast<int>(vertexCount)) {
            node->geometry_.allocate(static_cast<int>(vertexCount));
        }
        auto* vertices = node->geometry_.vertexDataAsColoredPoint2D();
        const float center = static_cast<float>(height() * 0.5);
        const float spectrumBaseline = static_cast<float>(height());
        const std::size_t playedCount = computePlayedCount(
            peakCount, position_ - visibleStartMs_,
            visibleEndMs_ - visibleStartMs_, 0);

        if (hasMix) {
            resampleVisibleValues(snapshot->mix->values, visibleStartMs_,
                                  visibleEndMs_, duration_, peakCount,
                                  node->mixValues_);
        } else {
            node->mixValues_.clear();
        }
        const bool hasHeldSpectrum = visualMode_ == 2
            && snapshot->spectrumPeakHold
            && !snapshot->spectrumPeakHold->values.empty();
        if (hasHeldSpectrum) {
            resampleVisibleValues(snapshot->spectrumPeakHold->values,
                                  visibleStartMs_, visibleEndMs_, duration_,
                                  peakCount, node->heldSpectrumValues_);
        } else {
            node->heldSpectrumValues_.clear();
        }
        auto& mixValues = node->mixValues_;
        auto& heldSpectrumValues = node->heldSpectrumValues_;

        if (visualMode_ == 2 && !mixValues.empty()) {
            // Input magnitudes are already normalized by the analyser. Per-frame
            // peak normalization makes quiet frames hit the ceiling and hides
            // the attack/decay relationship to the music.
            const auto shapeSpectrum = [](std::vector<float>& values) {
                for (float& value : values) {
                    value = std::pow(std::clamp(value, 0.0F, 1.0F), 0.58F);
                }
            };
            shapeSpectrum(mixValues);
            shapeSpectrum(heldSpectrumValues);
        }

        std::size_t vertexOffset = 0U;
        const auto writeLayer = [&](const std::vector<float>& values) {
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
                        : waveformLeftInset + normalizedX * waveformSpan;
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
                    if (visualMode_ != 2) {
                        amplitude = std::max(
                            amplitude, std::min(0.5F, center));
                    }

                    const VertexColor color = mixColor(
                        normalizedX, index < playedCount, visualMode_,
                        waveformColor_, baseColor_, progressColor_,
                        gradientStartColor_, gradientMiddleColor_,
                        gradientEndColor_, rgbProgress_);
                    const auto red = static_cast<unsigned char>(color.rgb.red);
                    const auto green = static_cast<unsigned char>(color.rgb.green);
                    const auto blue = static_cast<unsigned char>(color.rgb.blue);
                    const std::size_t vertex =
                        vertexOffset + (copy * peakCount + index) * 2U;

                    if (visualMode_ == 2) {
                        vertices[vertex].set(
                            x, spectrumBaseline - amplitude,
                            red, green, blue, color.alpha);
                        vertices[vertex + 1U].set(
                            x, spectrumBaseline,
                            red, green, blue, color.alpha);
                    } else {
                        vertices[vertex].set(
                            x, center - amplitude, red, green, blue, color.alpha);
                        vertices[vertex + 1U].set(
                            x, center + amplitude, red, green, blue, color.alpha);
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

        writeLayer(mixValues);

        node->revision_ = snapshot->revision;
        node->width_ = width();
        node->height_ = height();
        node->devicePixelRatio_ = devicePixelRatio;
        node->density_ = density_;
        node->lineWidth_ = lineWidth_;
        node->position_ = position_;
        node->duration_ = duration_;
        node->visibleStartMs_ = visibleStartMs_;
        node->visibleEndMs_ = visibleEndMs_;
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

    const std::size_t newPlayedCount = computePlayedCount(
        peakCount, position_ - visibleStartMs_, visibleEndMs_ - visibleStartMs_, 0);
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
            vertices + vertexOffset, peakCount, newPlayedCount,
            waveformColor_, visualMode_, baseColor_, progressColor_,
            gradientStartColor_, gradientMiddleColor_, gradientEndColor_,
            rgbProgress_, strokeCopies);
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
    node->position_ = position_;
    node->duration_ = duration_;
    node->visibleStartMs_ = visibleStartMs_;
    node->visibleEndMs_ = visibleEndMs_;
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
    if (!pointerInteractionEnabled_ || event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }
    pointerPressed_ = true;
    setHoverPosition(timeForX(event->position().x()));
    event->accept();
}

void WaveformItem::mouseMoveEvent(QMouseEvent* event)
{
    if (!pointerInteractionEnabled_ || !pointerPressed_) {
        event->ignore();
        return;
    }
    setHoverPosition(timeForX(event->position().x()));
    event->accept();
}

void WaveformItem::mouseReleaseEvent(QMouseEvent* event)
{
    if (!pointerInteractionEnabled_ || !pointerPressed_
        || event->button() != Qt::LeftButton) {
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

void WaveformItem::setVisibleRange(qint64 startMs, qint64 endMs)
{
    if (duration_ <= 0) {
        startMs = 0;
        endMs = 0;
    } else {
        startMs = std::clamp(startMs, qint64{0}, duration_);
        endMs = std::clamp(endMs, qint64{0}, duration_);
        if (endMs < startMs) {
            std::swap(startMs, endMs);
        }
        if (endMs == startMs) {
            startMs = 0;
            endMs = duration_;
        }
    }
    const bool startChanged = visibleStartMs_ != startMs;
    const bool endChanged = visibleEndMs_ != endMs;
    if (!startChanged && !endChanged) {
        return;
    }
    visibleStartMs_ = startMs;
    visibleEndMs_ = endMs;
    if (startChanged) {
        emit visibleStartMsChanged();
    }
    if (endChanged) {
        emit visibleEndMsChanged();
    }
    emit waveformCursorXChanged();
    update();
}
