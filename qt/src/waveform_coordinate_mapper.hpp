#pragma once

#include <QtGlobal>

class WaveformCoordinateMapper final {
public:
    WaveformCoordinateMapper() = delete;

    static double timeToPixel(qint64 positionMs,
                              qint64 durationMs,
                              double renderWidth) noexcept;
    static qint64 pixelToTime(double x,
                             double renderWidth,
                             qint64 durationMs) noexcept;
    static qint64 timeToSample(qint64 positionMs,
                               qint64 durationMs,
                               qint64 totalSamples) noexcept;
    static double sampleToPeak(qint64 sampleIndex,
                               qint64 totalSamples,
                               qsizetype peakCount) noexcept;
    static double timeToPeak(qint64 positionMs,
                             qint64 durationMs,
                             qint64 totalSamples,
                             qsizetype peakCount) noexcept;
    static double timeToPixelFromSamples(qint64 positionMs,
                                         qint64 durationMs,
                                         qint64 totalSamples,
                                         qsizetype peakCount,
                                         double renderWidth) noexcept;
};
