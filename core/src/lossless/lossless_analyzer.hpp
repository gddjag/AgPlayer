#pragma once

#include "lossless_types.hpp"

#include <atomic>
#include <string>

namespace agplayer::lossless {

[[nodiscard]] AnalysisResult analyzeFile(
    const std::string& utf8Path,
    const AnalysisOptions& options,
    const std::atomic_bool& cancelled,
    ProgressCallback progressCallback = {});

} // namespace agplayer::lossless
