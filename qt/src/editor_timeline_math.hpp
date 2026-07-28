#pragma once

#include <QtGlobal>

struct ClipBounds {
    qint64 timelineStartMs = 0;
    qint64 inMs = 0;
    qint64 outMs = 0;
};

qint64 beatGridMs(double bpm, int denominator);
qint64 snapMs(qint64 value, double bpm, int denominator);
ClipBounds normalizeClip(ClipBounds clip, qint64 sourceDurationMs);
