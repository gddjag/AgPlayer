#pragma once

#include <QString>

struct BpmAnalyzeResult {
    double bpm = 0.0;
    double confidence = 0.0;
};

BpmAnalyzeResult analyze_bpm(const QString& filePath);
