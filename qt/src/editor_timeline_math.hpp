#pragma once

#include <QtGlobal>
#include <QVariantList>

struct ClipBounds {
    qint64 timelineStartMs = 0;
    qint64 inMs = 0;
    qint64 outMs = 0;
};

struct BeatGridEstimate {
    double bpm = 0.0;
    qint64 offsetMs = 0;
    bool reliable = false;
};

// Fits recurring onsets near a known tempo. This estimates beat phase, not
// musical bar/downbeat identity; uncertain or coarse envelopes are rejected.
BeatGridEstimate estimateBeatGrid(const QVariantList& peaks,
                                 qint64 durationMs, double bpm);

qint64 beatGridMs(double bpm, int denominator);
qint64 snapMs(qint64 value, double bpm, int denominator);
qint64 estimateFirstBeatOffsetMs(const QVariantList& peaks,
                                 qint64 durationMs,
                                 double bpm);
ClipBounds normalizeClip(ClipBounds clip, qint64 sourceDurationMs);
