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
    std::vector<float> peak;
    std::vector<float> rms;
    std::vector<float> bass;
    std::vector<float> mid;
    std::vector<float> high;
    double bpm = 0.0;
    std::uint64_t duration_ms = 0U;
    std::uint64_t total_samples = 0U;
    std::uint32_t sample_rate = 0U;
    std::vector<WaveformCacheCue> cues;
};

class WaveformCache final {
public:
    // Current cache key based on source path metadata and analysis schema.
    [[nodiscard]] static std::string key_for(
        const std::filesystem::path& source_path);
    // Compatibility lookup for caches written by the v2 analysis schema.
    [[nodiscard]] static std::string legacy_v2_key_for(
        const std::filesystem::path& source_path);

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

    // v4: one complete amplitude + Low/Mid/High payload using the current
    // 250 Hz / 4 kHz analysis schema.
    [[nodiscard]] static bool load_v4(
        const std::filesystem::path& cache_path,
        const std::filesystem::path& source_path,
        WaveformCacheData& data) noexcept;
    [[nodiscard]] static bool save_v4(
        const std::filesystem::path& cache_path,
        const std::filesystem::path& source_path,
        const WaveformCacheData& data) noexcept;
};

} // namespace agplayer
