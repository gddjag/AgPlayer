#pragma once

#include "agplayer/c_api.h"

#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace agplayer {

enum class FadeCurve {
    Linear = 0,
    EqualPower = 1,
    Smooth = 2,
};

struct MultiTrackEditConfig {
    struct Track {
        std::string input_path;
        long long timeline_start_ms = 0;
        long long timeline_start_sample = -1;
        long long trim_start_ms = 0;
        long long trim_start_sample = -1;
        long long trim_end_ms = 0;
        long long trim_end_sample = -1;
        int fade_in_ms = 0;
        long long fade_in_samples = -1;
        FadeCurve fade_in_curve = FadeCurve::EqualPower;
        int fade_out_ms = 0;
        long long fade_out_samples = -1;
        FadeCurve fade_out_curve = FadeCurve::EqualPower;
        double gain = 1.0;
        double pan = 0.0;
        long long timeline_duration_ms = 0;
        long long timeline_duration_samples = -1;
        bool loop = false;
        // Preview and export share these non-destructive DSP controls.
        // speed_ratio is tempo relative to the original clip; pitch_cents is
        // applied independently when keep_pitch is enabled.
        double speed_ratio = 1.0;
        int pitch_cents = 0;
        bool keep_pitch = true;
    };

    std::vector<Track> tracks;
    std::string output_path;
};

ag_result multitrack_edit(const MultiTrackEditConfig& config,
                          const std::atomic_bool* cancelled,
                          std::function<void(float)> progress_callback,
                          std::string& error);

} // namespace agplayer
