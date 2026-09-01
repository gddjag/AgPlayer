#pragma once

#include <agplayer/c_api.h>

#include "waveform_analyzer_filters.hpp"

#include <atomic>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace agplayer {

inline constexpr float kBassCutoffHz = 250.0F;
inline constexpr float kMidLowCutoffHz = 250.0F;
inline constexpr float kMidHighCutoffHz = 2000.0F;
inline constexpr float kHighCutoffHz = 2000.0F;
inline constexpr float kFilterQ = 0.707F;

enum class WaveformAggregation {
    Peak,
    AverageAbsolute,
    Rms
};

class WaveformBucketizer final {
public:
    WaveformBucketizer(std::size_t total_frames,
                       std::size_t target_points,
                       std::size_t channels,
                       float sample_rate,
                       WaveformAggregation aggregation = WaveformAggregation::Peak,
                       bool include_spectral_index = false);

    [[nodiscard]] ag_result add(const std::vector<float>& samples,
                                std::size_t frames) noexcept;
    [[nodiscard]] ag_result finish(std::vector<float>& peaks,
                                   std::vector<float>& bass,
                                   std::vector<float>& mid,
                                   std::vector<float>& high,
                                   std::vector<std::uint8_t>* spectral_index = nullptr) noexcept;

private:
    void extend_bucket_boundary() noexcept;
    void add_spectral_frame(float mono_sample,
                            std::size_t absolute_frame) noexcept;
    void analyze_spectral_window(std::size_t end_frame) noexcept;

    std::size_t total_frames_ = 0U;
    std::size_t channels_ = 0U;
    float sample_rate_ = 0.0F;
    WaveformAggregation aggregation_ = WaveformAggregation::Peak;
    std::size_t consumed_frames_ = 0U;
    std::size_t current_bucket_ = 0U;
    std::size_t next_bucket_frame_ = 0U;
    std::size_t bucket_base_frames_ = 0U;
    std::size_t bucket_remainder_ = 0U;
    std::size_t bucket_error_ = 0U;
    std::vector<float> buckets_;
    std::vector<float> bass_buckets_;
    std::vector<float> mid_buckets_;
    std::vector<float> high_buckets_;
    std::vector<std::size_t> bucket_sample_counts_;
    std::vector<detail::BiquadFilter> bass_filters_;
    std::vector<detail::BandpassFilter> mid_filters_;
    std::vector<detail::BiquadFilter> high_filters_;
    static constexpr std::size_t spectral_fft_size_ = 512U;
    bool include_spectral_index_ = false;
    std::size_t spectral_hop_frames_ = 128U;
    std::size_t next_spectral_frame_ = spectral_fft_size_ - 1U;
    std::array<float, spectral_fft_size_> spectral_ring_{};
    std::size_t spectral_ring_position_ = 0U;
    std::size_t spectral_ring_count_ = 0U;
    std::vector<double> spectral_weighted_indices_;
    std::vector<double> spectral_weights_;
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
        std::vector<float>& peaks,
        std::vector<float>& bass,
        std::vector<float>& mid,
        std::vector<float>& high,
        WaveformAggregation aggregation = WaveformAggregation::Peak,
        std::uint64_t* duration_ms = nullptr,
        std::uint64_t* total_samples = nullptr,
        int* sample_rate = nullptr,
        std::vector<std::uint8_t>* spectral_index = nullptr,
        void (*pause_checkpoint)(void*) = nullptr,
        void* pause_user_data = nullptr) noexcept;
};

} // namespace agplayer
