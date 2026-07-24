#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace agplayer {

struct WaveformCacheCue final {
    std::uint64_t position_ms = 0U;
    std::string label;
};

struct WaveformCacheData final {
    std::vector<float> mix;
    std::vector<float> bass;
    std::vector<float> mid;
    std::vector<float> high;
    double bpm = 0.0;
    std::vector<WaveformCacheCue> cues;
};

class WaveformCache final {
public:
    [[nodiscard]] static std::string key_for(
        const std::filesystem::path& source_path,
        std::uint32_t version = 1U);

    // v1 compatibility: read/write single-layer (mix) caches.
    [[nodiscard]] static bool load(
        const std::filesystem::path& cache_path,
        const std::filesystem::path& source_path,
        std::vector<float>& peaks) noexcept;
    [[nodiscard]] static bool save(
        const std::filesystem::path& cache_path,
        const std::filesystem::path& source_path,
        const std::vector<float>& peaks) noexcept;

    // v2: read/write multi-layer caches with BPM and optional CUE metadata.
    [[nodiscard]] static bool load_v2(
        const std::filesystem::path& cache_path,
        const std::filesystem::path& source_path,
        WaveformCacheData& data) noexcept;
    [[nodiscard]] static bool save_v2(
        const std::filesystem::path& cache_path,
        const std::filesystem::path& source_path,
        const WaveformCacheData& data) noexcept;
};

} // namespace agplayer
