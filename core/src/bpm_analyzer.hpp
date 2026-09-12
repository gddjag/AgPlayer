#pragma once

#include <agplayer/c_api.h>

#include <atomic>
#include <cstddef>

namespace agplayer {

class IAudioStreamSource;

struct BpmAnalyzeInput {
    const char* file_path = nullptr;
    int max_duration_seconds = 90;
    const std::atomic_bool* cancelled = nullptr;
};

struct BpmAnalyzeOutput {
    double bpm = 0.0;
    double confidence = 0.0;
};

// Offline BPM analysis. Returns AG_OK on success.
ag_result analyze_bpm(const BpmAnalyzeInput& input, BpmAnalyzeOutput* out);
ag_result analyze_bpm(IAudioStreamSource& stream, int max_duration_seconds,
                      const std::atomic_bool* cancelled,
                      BpmAnalyzeOutput* out);

} // namespace agplayer
