#include "editor_timeline_math.hpp"

#include <QtMath>

#include <algorithm>

qint64 beatGridMs(double bpm, int denominator)
{
    if (bpm < 40.0 || bpm > 240.0 || denominator <= 0) {
        return 0;
    }
    return qRound64(240000.0 / (bpm * denominator));
}

qint64 snapMs(qint64 value, double bpm, int denominator)
{
    const qint64 grid = beatGridMs(bpm, denominator);
    if (grid <= 0) {
        return std::max<qint64>(0, value);
    }
    return std::max<qint64>(0, qRound64(static_cast<double>(value) / grid) * grid);
}

qint64 estimateFirstBeatOffsetMs(const QVariantList& peaks,
                                 qint64 durationMs,
                                 double bpm)
{
    if (peaks.size() < 2 || durationMs <= 0 || beatGridMs(bpm, 4) <= 0) {
        return 0;
    }

    double maximum = 0.0;
    for (const QVariant& peak : peaks) {
        maximum = std::max(maximum, std::abs(peak.toDouble()));
    }
    if (maximum < 0.05) {
        return 0;
    }

    const double levelThreshold = maximum * 0.35;
    const double riseThreshold = maximum * 0.15;
    double previous = std::abs(peaks.front().toDouble());
    if (previous >= levelThreshold) {
        return 0;
    }

    for (int index = 1; index < peaks.size(); ++index) {
        const double current = std::abs(peaks.at(index).toDouble());
        if (current >= levelThreshold
            && current - previous >= riseThreshold) {
            return qRound64(
                static_cast<double>(index) * durationMs
                / static_cast<double>(peaks.size() - 1));
        }
        previous = current;
    }
    return 0;
}

ClipBounds normalizeClip(ClipBounds clip, qint64 sourceDurationMs)
{
    constexpr qint64 minimumClipMs = 200;
    const qint64 duration = std::max<qint64>(0, sourceDurationMs);

    clip.timelineStartMs = std::max<qint64>(0, clip.timelineStartMs);
    clip.inMs = std::clamp<qint64>(clip.inMs, 0, duration);
    clip.outMs = std::clamp<qint64>(clip.outMs, 0, duration);

    if (duration <= minimumClipMs) {
        clip.inMs = 0;
        clip.outMs = duration;
    } else if (clip.outMs - clip.inMs < minimumClipMs) {
        clip.outMs = std::min(duration, std::max(clip.outMs, clip.inMs + minimumClipMs));
        clip.inMs = std::max<qint64>(0, clip.outMs - minimumClipMs);
    }

    return clip;
}
