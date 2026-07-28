#pragma once

#include "agplayer/c_api.h"

#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace agplayer {

struct MultiTrackEditConfig {
    struct Track {
        std::string input_path;
        long long timeline_start_ms = 0;
        long long trim_start_ms = 0;
        long long trim_end_ms = 0;
        int fade_in_ms = 0;
        int fade_out_ms = 0;
        double gain = 1.0;
    };

    std::vector<Track> tracks;
    std::string output_path;
};

ag_result multitrack_edit(const MultiTrackEditConfig& config,
                          const std::atomic_bool* cancelled,
                          std::function<void(float)> progress_callback,
                          std::string& error);

} // namespace agplayer
