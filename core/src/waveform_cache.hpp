#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace agplayer {

class WaveformCache final {
public:
    [[nodiscard]] static std::string key_for(
        const std::filesystem::path& source_path);
    [[nodiscard]] static bool load(
        const std::filesystem::path& cache_path,
        const std::filesystem::path& source_path,
        std::vector<float>& peaks) noexcept;
    [[nodiscard]] static bool save(
        const std::filesystem::path& cache_path,
        const std::filesystem::path& source_path,
        const std::vector<float>& peaks) noexcept;
};

} // namespace agplayer
