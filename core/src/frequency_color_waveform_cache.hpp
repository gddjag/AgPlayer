#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace agplayer {

struct FrequencyColorCacheData final {
    std::vector<std::uint8_t> low;
    std::vector<std::uint8_t> mid;
    std::vector<std::uint8_t> high;
    std::uint32_t point_count = 0U;
    std::uint32_t sample_rate = 0U;
    std::uint64_t timeline_frames = 0U;
    std::uint16_t crossover_low_hz = 180U;
    std::uint16_t crossover_high_hz = 2'800U;
    std::uint32_t algorithm_version = 1U;
};

[[nodiscard]] std::uint8_t quantize_frequency_color_peak(float value) noexcept;

class FrequencyColorWaveformCache final {
public:
    [[nodiscard]] static bool load(
        const std::filesystem::path& cache,
        const std::filesystem::path& source,
        std::uint32_t expected_algorithm,
        FrequencyColorCacheData& out) noexcept;

    [[nodiscard]] static bool save_atomic(
        const std::filesystem::path& cache,
        const std::filesystem::path& source,
        const FrequencyColorCacheData& data) noexcept;
};

namespace testing {

/* Internal deterministic failure seam used by the cache persistence tests. */
void fail_next_frequency_color_cache_replace() noexcept;

} // namespace testing

} // namespace agplayer
