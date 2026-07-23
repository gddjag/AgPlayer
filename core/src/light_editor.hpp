#pragma once

#include <atomic>
#include <functional>
#include <string>

#include "agplayer/c_api.h"

namespace agplayer {

// LightEditConfig: parameters for basic audio editing (trim, fade, gain).
// All time values are in milliseconds. gain is a linear amplitude factor
// (1.0 = no change, 0.5 = -6dB, 2.0 = +6dB).
struct LightEditConfig {
    long long trim_start_ms = 0;   // 0 = start of file
    long long trim_end_ms = 0;     // 0 = end of file
    int fade_in_ms = 0;            // 0 = no fade in
    int fade_out_ms = 0;           // 0 = no fade out
    double gain = 1.0;             // linear gain factor (0.0..4.0)
    std::string output_path;
};

// Apply light editing (trim + fade in/out + gain) to an audio file.
// Decodes to float32, applies edits in-memory, re-encodes with the same
// codec as the input. Returns AG_OK on success, AG_CANCELLED if cancelled.
ag_result light_edit(const std::string& input_path,
                     const LightEditConfig& config,
                     const std::atomic_bool* cancelled,
                     std::function<void(float)> progress_callback,
                     std::string& error);

} // namespace agplayer
