#include "waveform_coordinate_mapper.hpp"

#include <algorithm>
#include <cmath>

namespace {

double clampedFraction(qint64 value, qint64 maximum) noexcept
{
    if (maximum <= 0) {
        return 0.0;
    }
    const qint64 clamped = std::clamp(value, qint64{0}, maximum);
    return static_cast<double>(clamped) / static_cast<double>(maximum);
}

} // namespace

double WaveformCoordinateMapper::timeToPixel(qint64 positionMs,
                                             qint64 durationMs,
                                             double renderWidth) noexcept
{
    if (!std::isfinite(renderWidth) || renderWidth <= 0.0) {
        return 0.0;
    }
    return clampedFraction(positionMs, durationMs) * renderWidth;
}

qint64 WaveformCoordinateMapper::pixelToTime(double x,
                                             double renderWidth,
                                             qint64 durationMs) noexcept
{
    if (!std::isfinite(x) || !std::isfinite(renderWidth)
        || renderWidth <= 0.0 || durationMs <= 0) {
        return 0;
    }
    const double fraction = std::clamp(x / renderWidth, 0.0, 1.0);
    return static_cast<qint64>(std::llround(
        fraction * static_cast<double>(durationMs)));
}

qint64 WaveformCoordinateMapper::timeToSample(qint64 positionMs,
                                              qint64 durationMs,
                                              qint64 totalSamples) noexcept
{
    if (totalSamples <= 0) {
        return 0;
    }
    return static_cast<qint64>(std::llround(
        clampedFraction(positionMs, durationMs)
        * static_cast<double>(totalSamples)));
}

double WaveformCoordinateMapper::sampleToPeak(qint64 sampleIndex,
                                              qint64 totalSamples,
                                              qsizetype peakCount) noexcept
{
    if (totalSamples <= 0 || peakCount <= 1) {
        return 0.0;
    }
    return clampedFraction(sampleIndex, totalSamples)
           * static_cast<double>(peakCount - 1);
}

double WaveformCoordinateMapper::timeToPeak(qint64 positionMs,
                                            qint64 durationMs,
                                            qint64 totalSamples,
                                            qsizetype peakCount) noexcept
{
    return sampleToPeak(
        timeToSample(positionMs, durationMs, totalSamples),
        totalSamples,
        peakCount);
}

double WaveformCoordinateMapper::timeToPixelFromSamples(
    qint64 positionMs,
    qint64 durationMs,
    qint64 totalSamples,
    qsizetype peakCount,
    double renderWidth) noexcept
{
    if (!std::isfinite(renderWidth) || renderWidth <= 0.0 || peakCount <= 1) {
        return 0.0;
    }
    const double peak = timeToPeak(
        positionMs, durationMs, totalSamples, peakCount);
    return peak / static_cast<double>(peakCount - 1) * renderWidth;
}
