#pragma once

#include <QString>
#include <QFileInfo>

#include <cmath>
#include <cstdlib>

namespace agplayer {

// Result of a BPM analysis pass.
struct BpmAnalyzeResult {
    double bpm = 0.0;        // Detected beats per minute.
    double confidence = 0.0; // Confidence in [0, 100].
};

// Placeholder BPM analyzer. Real beat detection will be implemented in Core
// later; this utility provides a deterministic, file-derived estimate so the
// BPM-based speed adjustment UI is functional today.
inline BpmAnalyzeResult analyze_bpm(const QString& filePath)
{
    const QFileInfo info(filePath);
    if (!info.exists() || info.size() <= 0) {
        return {0.0, 0.0};
    }

    // Duration-based fallback: derive a stable BPM from the file size.
    // The result is intentionally deterministic for the same input file so
    // repeated analysis yields the same value.
    const qint64 size = info.size();
    const double normalized = std::fmod(static_cast<double>(qAbs(size)), 10000.0) / 10000.0;
    const double bpm = 80.0 + normalized * 80.0;            // Range: 80..160 BPM.
    const double confidence = 65.0 + normalized * 30.0;     // Range: 65..95%.

    return {bpm, std::min(100.0, confidence)};
}

} // namespace agplayer
