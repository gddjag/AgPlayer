#pragma once

#include <agplayer/c_api.h>

#include <atomic>
#include <cstddef>
#include <string>
#include <vector>

namespace agplayer {

class WaveformAnalyzer final {
public:
    [[nodiscard]] static ag_result analyze(
        const std::string& utf8_path,
        std::size_t target_points,
        const std::atomic_bool* cancelled,
        ag_progress_callback progress_callback,
        void* user_data,
        std::vector<float>& peaks) noexcept;
};

} // namespace agplayer
