#include "waveform_item.hpp"
#include "frequency_color_mix.hpp"
#include "waveform_coordinate_mapper.hpp"

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

Rgb shadedRgb(const Rgb& rgb, const double brightness)
{
    const std::array<double, 3> linear{
        agplayer::ui::detail::srgbToLinear(rgb.red / 255.0),
        agplayer::ui::detail::srgbToLinear(rgb.green / 255.0),
        agplayer::ui::detail::srgbToLinear(rgb.blue / 255.0)};
    const double peak = *std::max_element(linear.begin(), linear.end());
    // Highlight the whole mixture with one in-gamut gain. Clipping each
    // channel independently shifts hue when any channel is already full.
    const double gain = peak > 0.0 ? std::min(brightness, 1.0 / peak) : brightness;
    const auto encode = [gain](double value) {
        return static_cast<int>(std::lround(
            agplayer::ui::detail::linearToSrgb(value * gain) * 255.0));
    };
    return {encode(linear[0]), encode(linear[1]), encode(linear[2])};
}

Rgb premultipliedRgb(const Rgb& rgb, const unsigned char alpha)
{
    // QSGVertexColorMaterial consumes premultiplied vertex colors.
    return {(rgb.red * alpha + 127) / 255,
            (rgb.green * alpha + 127) / 255,
            (rgb.blue * alpha + 127) / 255};
}

void writeVertexColor(QSGGeometry::ColoredPoint2D& vertex,
                      const Rgb& rgb,
                      const unsigned char alpha)
{
    const Rgb premultiplied = premultipliedRgb(rgb, alpha);
    vertex.r = static_cast<unsigned char>(premultiplied.red);
    vertex.g = static_cast<unsigned char>(premultiplied.green);
    vertex.b = static_cast<unsigned char>(premultiplied.blue);
    vertex.a = alpha;
}

VertexColor mixColor(double normalizedX,
                     bool played,
                     int visualMode,
                     const QColor& legacyColor,
                     const QColor& baseColor,
                     const QColor& progressColor,
                     const QColor& gradientStart,
                     const QColor& gradientMiddle,
                     const QColor& gradientEnd,
                     bool rgbProgress,
                     double lowEnergy,
                     double midEnergy,
                     double highEnergy,
                     const QColor& lowColor,
                     const QColor& midColor,
                     const QColor& highColor,
                     qreal frequencyUnplayedOpacity)
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
    if (visualMode == 3) {
        const QColor mixed = agplayer::ui::mixFrequencyColor(
            lowEnergy, midEnergy, highEnergy,
            lowColor, midColor, highColor);
        // The setting is presented as unplayed-region brightness. Applying it
        // to alpha made the waveform disappear into a light background and
        // produced a much weaker perceived contrast on dark themes. Keep the
        // geometry opaque and shade its own colour instead, so one 0..100%
        // value remains legible and theme-independent.
        return {shadedRgb({mixed.red(), mixed.green(), mixed.blue()},
                          played ? 1.0 : frequencyUnplayedOpacity),
                255U};
    }
    if (legacyColor.isValid()) {
        return {{legacyColor.red(), legacyColor.green(), legacyColor.blue()}, 255U};
    }
    return {gradientColor(normalizedX), 255U};
}

bool playbackProgressAffectsColor(int visualMode,
                                  qreal frequencyUnplayedOpacity) noexcept
{
    return visualMode == 0 || visualMode == 1
           || (visualMode == 3
               && !qFuzzyCompare(frequencyUnplayedOpacity + 1.0, 2.0));
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
                           const std::vector<float>& lowValues,
                           const std::vector<float>& midValues,
                           const std::vector<float>& highValues,
                           const QColor& lowColor,
                           const QColor& midColor,
                           const QColor& highColor,
                           qreal frequencyUnplayedOpacity,
                           std::size_t strokeCopies)
{
    constexpr std::size_t verticesPerStroke = 2U;
    for (std::size_t copy = 0; copy < strokeCopies; ++copy) {
        for (std::size_t index = 0; index < peakCount; ++index) {
            const double normalizedX = peakCount == 1U
                                           ? 0.5
                                           : static_cast<double>(index)
                                                 / static_cast<double>(peakCount - 1U);
            const VertexColor color =
                mixColor(normalizedX, index < playedCount, visualMode,
                         waveformColor, baseColor, progressColor, gradientStart,
                         gradientMiddle, gradientEnd, rgbProgress,
                         index < lowValues.size() ? lowValues[index] : 0.0,
                         index < midValues.size() ? midValues[index] : 0.0,
                         index < highValues.size() ? highValues[index] : 0.0,
                         lowColor, midColor, highColor,
                         frequencyUnplayedOpacity);

            const std::size_t vertex =
                (copy * peakCount + index) * verticesPerStroke;
            writeVertexColor(vertices[vertex], color.rgb, color.alpha);
            writeVertexColor(vertices[vertex + 1U], color.rgb, color.alpha);
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
    if (values.empty() || pointCount == 0U) {
        result.clear();
        return;
    }
    if (durationMs <= 0
        || (visibleStartMs == 0 && visibleEndMs == durationMs)) {
        resampleValues(values, pointCount, result);
        return;
    }
    const std::size_t lastIndex = values.size() - 1U;
    const double first = static_cast<double>(visibleStartMs) / durationMs * lastIndex;
    const double last = static_cast<double>(visibleEndMs) / durationMs * lastIndex;
    const double stride = (last - first) / static_cast<double>(pointCount);
    const auto sample = [&](double position) {
        position = std::clamp(position, 0.0, static_cast<double>(lastIndex));
        const auto left = static_cast<std::size_t>(position);
        const auto right = std::min(left + 1U, lastIndex);
        return static_cast<float>(values[left]
            + (values[right] - values[left]) * (position - left));
    };
    result.resize(pointCount);
    for (std::size_t index = 0; index < pointCount; ++index) {
        const double begin = first + index * stride;
        const double end = std::min(last, begin + stride);
        if (stride < 1.0) {
            // Preserve the fractional source position. Snapping the slice to
            // whole buckets caused visible pauses followed by jumps while panning.
            result[index] = sample((begin + end) * 0.5);
        } else {
            float maximum = std::max(sample(begin), sample(end));
            for (std::size_t source = static_cast<std::size_t>(std::ceil(begin));
                 source <= lastIndex && static_cast<double>(source) < end; ++source) {
                maximum = std::max(maximum, values[source]);
            }
            result[index] = maximum;
        }
    }
}

void resampleSummary(const std::vector<float>& values,
                     const agplayer::ui::WaveformDisplaySummary& summary,
                     qint64 startMs, qint64 endMs, qint64 durationMs,
                     std::size_t count,
                     std::vector<float>& result)
{
    if (values.empty() || count == 0U) {
        result.clear();
        return;
    }
    const double size = static_cast<double>(values.size());
    const double first = durationMs > 0
        ? std::clamp(static_cast<double>(startMs) / durationMs, 0.0, 1.0) * size : 0.0;
    const double last = durationMs > 0
        ? std::clamp(static_cast<double>(endMs) / durationMs, 0.0, 1.0) * size : size;
    const double stride = (last - first) / static_cast<double>(count);
    result.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        const double begin = first + static_cast<double>(i) * stride;
        const double end = std::min(last, begin + stride);
        // These values summarize time intervals, not point samples. Linear
        // interpolation invents cross-band energy outside the pixel interval.
        // Fractional integration still gives smooth energy transitions at
        // bucket boundaries without pretending to recover sub-bucket detail.
        result[i] = summary.rms(begin, end);
    }
}

struct SourceAnchoredWindow {
    std::size_t firstPoint = 0U;
    std::size_t globalPointCount = 0U;
    std::vector<double> normalizedX;
};

SourceAnchoredWindow sourceAnchoredWindow(const std::size_t pointCount,
                                          const qint64 visibleStartMs,
                                          const qint64 visibleEndMs,
                                          const qint64 durationMs)
{
    SourceAnchoredWindow window;
    const qint64 visibleDurationMs = visibleEndMs - visibleStartMs;
    if (pointCount == 0U || durationMs <= 0 || visibleDurationMs <= 0) {
        return window;
    }

    // Project the viewport's physical-pixel budget onto one global sample
    // lattice anchored at source time zero. Panning then changes only x; the
    // source interval (and therefore amplitude and frequency colour) assigned
    // to an overlapping sample remains identical.
    const double projectedCount = static_cast<double>(durationMs)
                                  / static_cast<double>(visibleDurationMs)
                                  * static_cast<double>(pointCount);
    window.globalPointCount = std::max(
        pointCount, static_cast<std::size_t>(std::llround(projectedCount)));
    const double firstProjected = static_cast<double>(visibleStartMs)
                                  / static_cast<double>(durationMs)
                                  * window.globalPointCount;
    const auto first = static_cast<std::int64_t>(
        std::ceil(firstProjected - 0.5));
    const std::size_t maximumFirst = window.globalPointCount - pointCount;
    window.firstPoint = static_cast<std::size_t>(std::clamp<std::int64_t>(
        first, 0, static_cast<std::int64_t>(maximumFirst)));

    window.normalizedX.reserve(pointCount);
    for (std::size_t index = 0U; index < pointCount; ++index) {
        const double sourceFraction =
            (static_cast<double>(window.firstPoint + index) + 0.5)
            / static_cast<double>(window.globalPointCount);
        const double sourceTimeMs = sourceFraction * durationMs;
        window.normalizedX.push_back(std::clamp(
            (sourceTimeMs - visibleStartMs)
                / static_cast<double>(visibleDurationMs),
            0.0, 1.0));
    }
    return window;
}

float sampleLinear(const std::vector<float>& values, double position)
{
    if (values.empty()) return 0.0F;
    position = std::clamp(position, 0.0,
                          static_cast<double>(values.size() - 1U));
    const auto left = static_cast<std::size_t>(position);
    const auto right = std::min(left + 1U, values.size() - 1U);
    const double fraction = position - static_cast<double>(left);
    return static_cast<float>(values[left]
                              + (values[right] - values[left]) * fraction);
}

void resampleSourceAnchoredPeaks(
    const std::vector<float>& values,
    const SourceAnchoredWindow& window,
    std::vector<float>& result)
{
    const std::size_t count = window.normalizedX.size();
    if (values.empty() || count == 0U || window.globalPointCount == 0U) {
        result.clear();
        return;
    }
    const double sourceSize = static_cast<double>(values.size());
    result.resize(count);
    for (std::size_t index = 0U; index < count; ++index) {
        const double begin = static_cast<double>(window.firstPoint + index)
                             / static_cast<double>(window.globalPointCount)
                             * sourceSize;
        const double end = static_cast<double>(window.firstPoint + index + 1U)
                           / static_cast<double>(window.globalPointCount)
                           * sourceSize;
        if (end - begin < 1.0) {
            result[index] = sampleLinear(values, (begin + end) * 0.5);
            continue;
        }
        float maximum = std::max(sampleLinear(values, begin),
                                 sampleLinear(values, end));
        for (std::size_t source = static_cast<std::size_t>(std::ceil(begin));
             source < values.size() && static_cast<double>(source) < end;
             ++source) {
            maximum = std::max(maximum, values[source]);
        }
        result[index] = maximum;
    }
}

void resampleSourceAnchoredEnergy(
    const std::vector<float>& values,
    const agplayer::ui::WaveformDisplaySummary& summary,
    const SourceAnchoredWindow& window,
    std::vector<float>& result)
{
    const std::size_t count = window.normalizedX.size();
    if (values.empty() || count == 0U || window.globalPointCount == 0U) {
        result.clear();
        return;
    }
    const double sourceSize = static_cast<double>(values.size());
    result.resize(count);
    for (std::size_t index = 0U; index < count; ++index) {
        const double begin = static_cast<double>(window.firstPoint + index)
                             / static_cast<double>(window.globalPointCount)
                             * sourceSize;
        const double end = static_cast<double>(window.firstPoint + index + 1U)
                           / static_cast<double>(window.globalPointCount)
                           * sourceSize;
        result[index] = summary.rms(begin, end);
    }
}

std::size_t sourceAnchoredPlayedCount(const SourceAnchoredWindow& window,
                                      const qint64 positionMs,
                                      const qint64 durationMs)
{
    if (window.normalizedX.empty() || window.globalPointCount == 0U
        || durationMs <= 0) {
        return 0U;
    }
    const double projected = static_cast<double>(positionMs)
                             / static_cast<double>(durationMs)
                             * static_cast<double>(window.globalPointCount);
    const auto count = static_cast<std::int64_t>(std::floor(projected + 0.5))
                       - static_cast<std::int64_t>(window.firstPoint);
    return static_cast<std::size_t>(std::clamp<std::int64_t>(
        count, 0, static_cast<std::int64_t>(window.normalizedX.size())));
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
    bool preserveSourcePeakDensity_ = false;
    bool sourceAnchoredSampling_ = false;
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
    std::vector<float> lowValues_;
    std::vector<float> midValues_;
    std::vector<float> highValues_;
    std::vector<double> normalizedX_;
    QColor lowColor_;
    QColor midColor_;
    QColor highColor_;
    qreal frequencyUnplayedOpacity_ = -1.0;
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
    // The shared session publishes duration and layers separately. QML can
    // therefore deliver the same implicitly shared payload more than once.
    // Keep the render snapshot and its summaries instead of reallocating them.
    if (peaks_.isEmpty() && layers_ == layers) return;

    auto snapshot = std::make_shared<PeakSnapshot>();
    snapshot->revision = nextRevision_++;

    auto extract = [](const QVariantMap& map, const QString& key) {
        return map.value(key).toList();
    };

    auto mix = std::make_shared<LayerSnapshot>();
    normalizeLayerInput(extract(layers, QStringLiteral("mix")), mix);

    auto bass = std::make_shared<LayerSnapshot>();
    auto mid = std::make_shared<LayerSnapshot>();
    auto high = std::make_shared<LayerSnapshot>();
    normalizeLayerInput(extract(layers, QStringLiteral("bass")), bass);
    normalizeLayerInput(extract(layers, QStringLiteral("mid")), mid);
    normalizeLayerInput(extract(layers, QStringLiteral("high")), high);

    bass->summary.build(bass->values);
    mid->summary.build(mid->values);
    high->summary.build(high->values);

    snapshot->peakCount = static_cast<qsizetype>(std::max({mix->values.size(),
        bass->values.size(), mid->values.size(), high->values.size()}));
    snapshot->mix = mix->values.empty() ? nullptr : std::move(mix);
    snapshot->bass = bass->values.empty() ? nullptr : std::move(bass);
    snapshot->mid = mid->values.empty() ? nullptr : std::move(mid);
    snapshot->high = high->values.empty() ? nullptr : std::move(high);
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

void WaveformItem::normalizeLayerInput(const QVariantList& input,
                                       std::shared_ptr<LayerSnapshot>& snapshot)
{
    snapshot->values.clear();
    // Retain the input QVariantMap through implicit sharing. Only the typed,
    // sanitized render snapshot is needed; do not rebuild six throwaway lists.
    snapshot->values.reserve(static_cast<std::size_t>(input.size()));
    for (const QVariant& value : input) {
        bool converted = false;
        double peak = value.toDouble(&converted);
        if (!converted || !std::isfinite(peak)) {
            peak = 0.0;
        }
        peak = std::clamp(std::abs(peak), 0.0, 1.0);
        snapshot->values.push_back(static_cast<float>(peak));
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
    if (playbackProgressAffectsColor(visualMode_,
                                     frequencyUnplayedOpacity_)) {
        update();
    }
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
    const bool showingFullTrack = visibleStartMs_ == 0
        && visibleEndMs_ == duration_;
    duration_ = clamped;
    visibleStartMs_ = std::clamp(visibleStartMs_, qint64{0}, duration_);
    // A full-track view must follow the next track (or corrected decoded
    // duration). Otherwise a previous 2:22 track truncates every longer track.
    // Explicit zoom/rolling ranges retain their chosen source-time interval.
    visibleEndMs_ = showingFullTrack ? duration_
        : std::clamp(visibleEndMs_, visibleStartMs_, duration_);
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

QColor WaveformItem::lowColor() const { return lowColor_; }
QColor WaveformItem::midColor() const { return midColor_; }
QColor WaveformItem::highColor() const { return highColor_; }

#define AGPLAYER_FREQUENCY_COLOR_SETTER(Setter, Member) \
    void WaveformItem::Setter(const QColor& color)       \
    {                                                     \
        if (!color.isValid() || Member == color) return;  \
        Member = color;                                   \
        emit frequencyStyleChanged();                     \
        update();                                         \
    }

AGPLAYER_FREQUENCY_COLOR_SETTER(setLowColor, lowColor_)
AGPLAYER_FREQUENCY_COLOR_SETTER(setMidColor, midColor_)
AGPLAYER_FREQUENCY_COLOR_SETTER(setHighColor, highColor_)

#undef AGPLAYER_FREQUENCY_COLOR_SETTER

qreal WaveformItem::frequencyUnplayedOpacity() const noexcept
{
    return frequencyUnplayedOpacity_;
}

void WaveformItem::setFrequencyUnplayedOpacity(qreal opacity)
{
    const qreal clamped = std::clamp(
        std::isfinite(opacity) ? opacity : qreal{0.20}, qreal{0.0}, qreal{1.0});
    if (qFuzzyCompare(frequencyUnplayedOpacity_ + 1.0, clamped + 1.0)) {
        return;
    }
    frequencyUnplayedOpacity_ = clamped;
    emit frequencyStyleChanged();
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

bool WaveformItem::preserveSourcePeakDensity() const noexcept
{
    return preserveSourcePeakDensity_;
}

void WaveformItem::setPreserveSourcePeakDensity(bool preserve)
{
    if (preserveSourcePeakDensity_ == preserve) {
        return;
    }
    preserveSourcePeakDensity_ = preserve;
    emit preserveSourcePeakDensityChanged();
    update();
}

bool WaveformItem::sourceAnchoredSampling() const noexcept
{
    return sourceAnchoredSampling_;
}

void WaveformItem::setSourceAnchoredSampling(const bool enabled)
{
    if (sourceAnchoredSampling_ == enabled) {
        return;
    }
    sourceAnchoredSampling_ = enabled;
    emit sourceAnchoredSamplingChanged();
    update();
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
}

QSGNode* WaveformItem::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*)
{
    // Qt calls this during Scene Graph synchronization while the GUI thread is blocked,
    // so copying the shared immutable snapshot here hands one stable revision to rendering.
    const std::shared_ptr<const PeakSnapshot> snapshot = peakSnapshot_;
    const bool hasMix = snapshot && snapshot->mix && !snapshot->mix->values.empty();
    const bool hasRenderableLayer = visualMode_ == 3
        ? hasMix && snapshot->bass && snapshot->mid && snapshot->high
            && snapshot->bass->values.size() == snapshot->mix->values.size()
            && snapshot->mid->values.size() == snapshot->mix->values.size()
            && snapshot->high->values.size() == snapshot->mix->values.size()
        : hasMix;
    if (!hasRenderableLayer || width() <= 0.0 || height() <= 0.0) {
        delete oldNode;
        return nullptr;
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

    const qint64 visibleDurationMs = std::max<qint64>(
        1, visibleEndMs_ - visibleStartMs_);
    const double visibleFraction = duration_ > 0
        ? std::clamp(static_cast<double>(visibleDurationMs)
                         / static_cast<double>(duration_),
                     0.0, 1.0)
        : 1.0;
    const std::size_t visibleSourcePeaks = hasMix
        ? std::max<std::size_t>(
              1U, static_cast<std::size_t>(std::ceil(
                      static_cast<double>(snapshot->mix->values.size())
                      * visibleFraction)))
        : 1U;
    const std::size_t peakCount = visualMode_ == 2
        ? renderedSpectrumBarCount(width(), devicePixelRatio)
        : preserveSourcePeakDensity_
          ? std::min(maxPoints, visibleSourcePeaks)
          // Map the selected source-time window to the physical pixel budget.
          // Downsampling is peak-preserving, while zooming in only interpolates
          // between the high-density source buckets supplied by the provider.
          : maxPoints;
    const SourceAnchoredWindow anchoredWindow = sourceAnchoredSampling_
        ? sourceAnchoredWindow(peakCount, visibleStartMs_, visibleEndMs_, duration_)
        : SourceAnchoredWindow{};

    const unsigned char layerMask = hasMix ? 1U : 0U;

    const bool geometryChanged = node->revision_ != snapshot->revision
                                 || !qFuzzyCompare(node->width_, width())
                                 || !qFuzzyCompare(node->height_, height())
                                 || !qFuzzyCompare(node->devicePixelRatio_, devicePixelRatio)
                                 || !qFuzzyCompare(node->density_, density_)
                                 || node->preserveSourcePeakDensity_
                                        != preserveSourcePeakDensity_
                                 || node->sourceAnchoredSampling_
                                        != sourceAnchoredSampling_
                                 || !qFuzzyCompare(node->lineWidth_, lineWidth_)
                                 || node->visibleStartMs_ != visibleStartMs_
                                 || node->visibleEndMs_ != visibleEndMs_
                                 || node->duration_ != duration_
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
        // Stroke copies are centred around each logical sample. Keep the
        // existing one-pixel leading inset so the first peak cannot rasterize
        // as a solid border. Reserve the outer-copy offset plus half a physical
        // pixel at the trailing side: a line centred exactly on width() is
        // clipped in half by the scene graph and made the final non-zero peak
        // look truncated, especially in compact classic and mini waveforms.
        const double strokeOuterOffset =
            static_cast<double>(strokeCopies - 1U) * 0.5;
        const double waveformLeftInset = std::min(
            width() * 0.5,
            static_cast<double>(strokeCopies) * 0.5
                + 0.5 / std::max(1.0, devicePixelRatio));
        const double waveformRightInset = std::min(
            width() - waveformLeftInset,
            strokeOuterOffset + 0.5 / std::max(1.0, devicePixelRatio));
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
        constexpr std::size_t verticesPerStroke = 2U;
        const auto vertexCount = peakCount * verticesPerStroke
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
        const std::size_t playedCount = sourceAnchoredSampling_
            ? sourceAnchoredPlayedCount(anchoredWindow, position_, duration_)
            : computePlayedCount(peakCount, position_ - visibleStartMs_,
                                 visibleEndMs_ - visibleStartMs_, 0);

        if (hasMix) {
            // All waveform modes share one amplitude contour. Frequency bands
            // affect color only, never override mix with a louder peak envelope.
            if (sourceAnchoredSampling_) {
                resampleSourceAnchoredPeaks(snapshot->mix->values,
                                            anchoredWindow,
                                            node->mixValues_);
            } else {
                resampleVisibleValues(snapshot->mix->values, visibleStartMs_,
                                      visibleEndMs_, duration_, peakCount,
                                      node->mixValues_);
            }
            if (visualMode_ == 3) {
                if (sourceAnchoredSampling_) {
                    resampleSourceAnchoredEnergy(snapshot->bass->values,
                                                 snapshot->bass->summary,
                                                 anchoredWindow,
                                                 node->lowValues_);
                    resampleSourceAnchoredEnergy(snapshot->mid->values,
                                                 snapshot->mid->summary,
                                                 anchoredWindow,
                                                 node->midValues_);
                    resampleSourceAnchoredEnergy(snapshot->high->values,
                                                 snapshot->high->summary,
                                                 anchoredWindow,
                                                 node->highValues_);
                } else {
                    resampleSummary(snapshot->bass->values, snapshot->bass->summary,
                                    visibleStartMs_, visibleEndMs_, duration_, peakCount,
                                    node->lowValues_);
                    resampleSummary(snapshot->mid->values, snapshot->mid->summary,
                                    visibleStartMs_, visibleEndMs_, duration_, peakCount,
                                    node->midValues_);
                    resampleSummary(snapshot->high->values, snapshot->high->summary,
                                    visibleStartMs_, visibleEndMs_, duration_, peakCount,
                                    node->highValues_);
                }
            } else {
                node->lowValues_.clear();
                node->midValues_.clear();
                node->highValues_.clear();
            }
        } else {
            node->mixValues_.clear();
            node->lowValues_.clear();
            node->midValues_.clear();
            node->highValues_.clear();
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
        node->normalizedX_ = anchoredWindow.normalizedX;

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
                    const double normalizedX = sourceAnchoredSampling_
                        && index < node->normalizedX_.size()
                        ? node->normalizedX_[index]
                        : peakCount == 1U
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
                        gradientEndColor_, rgbProgress_,
                        index < node->lowValues_.size()
                            ? node->lowValues_[index] : 0.0,
                        index < node->midValues_.size()
                            ? node->midValues_[index] : 0.0,
                        index < node->highValues_.size()
                            ? node->highValues_[index] : 0.0,
                        lowColor_, midColor_, highColor_,
                        frequencyUnplayedOpacity_);
                    const auto red = static_cast<unsigned char>(color.rgb.red);
                    const auto green = static_cast<unsigned char>(color.rgb.green);
                    const auto blue = static_cast<unsigned char>(color.rgb.blue);
                    const std::size_t vertex = vertexOffset
                        + (copy * peakCount + index) * verticesPerStroke;

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
                        gradientMiddleColor_, gradientEndColor_, rgbProgress_,
                        0.0, 0.0, 0.0, lowColor_, midColor_, highColor_,
                        frequencyUnplayedOpacity_);
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
                vertexOffset += peakCount * verticesPerStroke * strokeCopies;
            }
        };

        writeLayer(mixValues);

        node->revision_ = snapshot->revision;
        node->width_ = width();
        node->height_ = height();
        node->devicePixelRatio_ = devicePixelRatio;
        node->density_ = density_;
        node->preserveSourcePeakDensity_ = preserveSourcePeakDensity_;
        node->sourceAnchoredSampling_ = sourceAnchoredSampling_;
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
        node->lowColor_ = lowColor_;
        node->midColor_ = midColor_;
        node->highColor_ = highColor_;
        node->frequencyUnplayedOpacity_ = frequencyUnplayedOpacity_;
        node->amplitudeScale_ = amplitudeScale_;
        node->peakCount_ = peakCount;
        node->playedCount_ = playedCount;
        node->layerMask_ = layerMask;
        node->markDirty(QSGNode::DirtyGeometry | QSGNode::DirtyMaterial);
        return node;
    }

    // Spectrum and a fully opaque frequency waveform do not encode playback
    // progress in their colours. Keep the cached bucket so a position-only
    // update cannot rewrite and re-upload an identical vertex buffer.
    const bool progressAffectsColor = playbackProgressAffectsColor(
        visualMode_, frequencyUnplayedOpacity_);
    const std::size_t newPlayedCount = !progressAffectsColor
        ? node->playedCount_
        : sourceAnchoredSampling_
          ? sourceAnchoredPlayedCount(anchoredWindow, position_, duration_)
          : computePlayedCount(peakCount, position_ - visibleStartMs_,
                               visibleEndMs_ - visibleStartMs_, 0);
    const bool colorChanged = node->waveformColor_ != waveformColor_
                              || node->playedCount_ != newPlayedCount
                              || node->lowColor_ != lowColor_
                              || node->midColor_ != midColor_
                              || node->highColor_ != highColor_
                              || !qFuzzyCompare(
                                  node->frequencyUnplayedOpacity_ + 1.0,
                                  frequencyUnplayedOpacity_ + 1.0);
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
            rgbProgress_, node->lowValues_, node->midValues_,
            node->highValues_, lowColor_, midColor_, highColor_,
            frequencyUnplayedOpacity_, strokeCopies);
        constexpr std::size_t verticesPerStroke = 2U;
        vertexOffset += peakCount * verticesPerStroke * strokeCopies;
        if (node->visualMode_ == 2) {
            // Peak-hold caps are a separate geometry range.  Recolor them in
            // the same update so their colour never trails the bar below.
            updateMixVertexColors(
                vertices + vertexOffset, peakCount, newPlayedCount, waveformColor_,
                visualMode_, baseColor_, progressColor_, gradientStartColor_,
                gradientMiddleColor_, gradientEndColor_, rgbProgress_,
                node->lowValues_, node->midValues_, node->highValues_,
                lowColor_, midColor_, highColor_,
                frequencyUnplayedOpacity_, 1U);
            vertexOffset += peakCount * 2U;
        }
    }
    node->position_ = position_;
    node->duration_ = duration_;
    node->visibleStartMs_ = visibleStartMs_;
    node->visibleEndMs_ = visibleEndMs_;
    node->waveformColor_ = waveformColor_;
    node->lowColor_ = lowColor_;
    node->midColor_ = midColor_;
    node->highColor_ = highColor_;
    node->frequencyUnplayedOpacity_ = frequencyUnplayedOpacity_;
    node->playedCount_ = newPlayedCount;
    // RGBA lives in the ColoredPoint2D vertex buffer, so the scene graph must
    // upload geometry even when only frequency/progress colours changed.
    node->markDirty(QSGNode::DirtyGeometry);
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
