#pragma once

#include <agplayer/c_api.h>

namespace agplayer {

struct BpmAnalyzeInput {
    const char* file_path = nullptr;
    int max_duration_seconds = 90;
};

struct BpmAnalyzeOutput {
    double bpm = 0.0;
    double confidence = 0.0;
};

// Offline BPM analysis. Returns AG_OK on success.
ag_result analyze_bpm(const BpmAnalyzeInput& input, BpmAnalyzeOutput* out);

} // namespace agplayer
