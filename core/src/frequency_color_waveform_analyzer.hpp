#pragma once

#include <agplayer/c_api.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace agplayer {

struct FrequencyColorWaveformData final {
    std::vector<float> mix;
    std::vector<float> low;
    std::vector<float> mid;
    std::vector<float> high;
    std::uint64_t timeline_frames = 0U;
    std::uint64_t decoded_frames = 0U;
    std::uint64_t duration_ms = 0U;
    std::uint32_t sample_rate = 0U;
};

struct FrequencyColorAnalysisDiagnostics final {
    std::uint32_t decoder_open_count = 0U;
    std::uint64_t decoded_block_count = 0U;
    std::size_t accumulator_bytes = 0U;
};

namespace detail {

struct BucketStats final {
    float peak = 0.0F;
    double sum_squares = 0.0;
    std::uint64_t count = 0U;
};

struct FrequencyFrameValues final {
    float mix = 0.0F;
    float low = 0.0F;
    float mid = 0.0F;
    float high = 0.0F;
};

class FrequencyColorAccumulator final {
public:
    FrequencyColorAccumulator(std::uint64_t timeline_frames,
                              std::size_t point_count);

    [[nodiscard]] ag_result beginBlock(std::int64_t timestamp_frame,
                                       std::size_t frame_count) noexcept;
    [[nodiscard]] ag_result addFrame(std::uint64_t absolute_frame,
                                     const FrequencyFrameValues& values) noexcept;
    [[nodiscard]] ag_result addBlock(
        std::int64_t timestamp_frame,
        const std::vector<FrequencyFrameValues>& frames) noexcept;
    [[nodiscard]] ag_result finish(FrequencyColorWaveformData& output) noexcept;
    [[nodiscard]] std::size_t memoryBytes() const noexcept;

private:
    std::uint64_t timeline_frames_ = 0U;
    std::size_t point_count_ = 0U;
    std::uint64_t next_allowed_frame_ = 0U;
    std::uint64_t active_begin_ = 0U;
    std::uint64_t active_end_ = 0U;
    std::vector<BucketStats> mix_;
    std::vector<BucketStats> low_;
    std::vector<BucketStats> mid_;
    std::vector<BucketStats> high_;
    std::vector<std::uint8_t> occupied_;
    bool failed_ = false;
    bool finished_ = false;
};

} // namespace detail

class FrequencyColorWaveformAnalyzer final {
public:
    static constexpr std::uint32_t kAlgorithmVersion = 1U;
    static constexpr std::size_t kDefaultPointCount = 2'000U;

    [[nodiscard]] static ag_result analyze(
        const std::string& utf8_path,
        std::size_t point_count,
        const std::atomic_bool* cancelled,
        ag_progress_callback progress,
        void* user_data,
        FrequencyColorWaveformData& output,
        FrequencyColorAnalysisDiagnostics* diagnostics = nullptr) noexcept;
};

} // namespace agplayer
