#pragma once

#include <QString>
#include "../../core/src/audio_editor/audio_document.hpp"

#include <agplayer/c_api.h>

#include <atomic>

struct BpmAnalyzeResult {
    double bpm = 0.0;
    double confidence = 0.0;
    ag_result status = AG_OK;
    QString error;
};

BpmAnalyzeResult analyze_bpm(const QString& filePath);
BpmAnalyzeResult analyze_bpm(
    agplayer::editor::TimelineSnapshot snapshot,
    const std::atomic_bool* cancelled);
