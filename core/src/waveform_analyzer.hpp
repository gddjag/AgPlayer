#pragma once

#include <agplayer/c_api.h>

#include <atomic>
#include <cstddef>
#include <string>
#include <vector>

namespace agplayer {

class WaveformBucketizer final {
public:
    WaveformBucketizer(std::size_t total_frames,
                       std::size_t target_points,
                       std::size_t channels);

    [[nodiscard]] ag_result add(const std::vector<float>& samples,
                                std::size_t frames) noexcept;
    [[nodiscard]] ag_result finish(std::vector<float>& peaks) noexcept;

private:
    void extend_bucket_boundary() noexcept;

    std::size_t total_frames_ = 0U;
    std::size_t channels_ = 0U;
    std::size_t consumed_frames_ = 0U;
    std::size_t current_bucket_ = 0U;
    std::size_t next_bucket_frame_ = 0U;
    std::size_t bucket_base_frames_ = 0U;
    std::size_t bucket_remainder_ = 0U;
    std::size_t bucket_error_ = 0U;
    std::vector<float> buckets_;
    bool failed_ = false;
};

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
