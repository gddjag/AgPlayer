#define _USE_MATH_DEFINES

#include "waveform_analyzer.hpp"

#include "decoder.hpp"
#include "waveform_analyzer_filters.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace agplayer {
namespace {

bool is_cancelled(const std::atomic_bool* cancelled) noexcept
{
    return cancelled != nullptr
           && cancelled->load(std::memory_order_relaxed);
}

} // namespace

WaveformBucketizer::WaveformBucketizer(const std::size_t total_frames,
                                       const std::size_t target_points,
                                       const std::size_t channels,
                                       const float sample_rate,
                                       const WaveformAggregation aggregation)
    : total_frames_(total_frames),
      channels_(channels),
      sample_rate_(sample_rate),
      aggregation_(aggregation),
      buckets_(std::min(total_frames, target_points), 0.0F),
      bass_buckets_(buckets_.size(), 0.0F),
      mid_buckets_(buckets_.size(), 0.0F),
      high_buckets_(buckets_.size(), 0.0F),
      bucket_sample_counts_(buckets_.size(), 0U),
      failed_(total_frames == 0U || target_points == 0U || channels == 0U
              || sample_rate <= 0.0F
              || (aggregation != WaveformAggregation::Peak
                  && aggregation != WaveformAggregation::AverageAbsolute
                  && aggregation != WaveformAggregation::Rms))
{
    if (!failed_) {
        bucket_base_frames_ = total_frames_ / buckets_.size();
        bucket_remainder_ = total_frames_ % buckets_.size();
        bucket_error_ = buckets_.size() - 1U;
        extend_bucket_boundary();

        bass_filters_.reserve(channels_);
        mid_filters_.reserve(channels_);
        high_filters_.reserve(channels_);
        for (std::size_t channel = 0U; channel < channels_; ++channel) {
            bass_filters_.emplace_back(
                detail::BiquadFilter::Type::Lowpass, sample_rate_, kBassCutoffHz, kFilterQ);
            mid_filters_.emplace_back(sample_rate_, kMidLowCutoffHz, kMidHighCutoffHz);
            high_filters_.emplace_back(
                detail::BiquadFilter::Type::Highpass, sample_rate_, kHighCutoffHz, kFilterQ);
        }
    }
}

void WaveformBucketizer::extend_bucket_boundary() noexcept
{
    std::size_t frames_in_bucket = bucket_base_frames_;
    if (bucket_remainder_ != 0U) {
        const std::size_t threshold = buckets_.size() - bucket_remainder_;
        if (bucket_error_ >= threshold) {
            ++frames_in_bucket;
            bucket_error_ -= threshold;
        } else {
            bucket_error_ += bucket_remainder_;
        }
    }
    next_bucket_frame_ += frames_in_bucket;
}

ag_result WaveformBucketizer::add(const std::vector<float>& samples,
                                  const std::size_t frames) noexcept
{
    if (failed_ || frames > total_frames_ - consumed_frames_
        || frames > std::numeric_limits<std::size_t>::max() / channels_
        || samples.size() != frames * channels_) {
        failed_ = true;
        return AG_DECODE_ERROR;
    }

    for (std::size_t frame = 0U; frame < frames; ++frame) {
        const std::size_t absolute_frame = consumed_frames_ + frame;
        while (current_bucket_ + 1U < buckets_.size()
               && absolute_frame >= next_bucket_frame_) {
            ++current_bucket_;
            extend_bucket_boundary();
        }

        const auto accumulate = [this](std::vector<float>& layer,
                                       const float value) noexcept {
            const float absolute = std::abs(value);
            switch (aggregation_) {
            case WaveformAggregation::Peak:
                layer[current_bucket_] =
                    std::max(layer[current_bucket_], absolute);
                break;
            case WaveformAggregation::AverageAbsolute:
                layer[current_bucket_] += absolute;
                break;
            case WaveformAggregation::Rms:
                layer[current_bucket_] += value * value;
                break;
            }
        };
        const std::size_t sample_offset = frame * channels_;

        for (std::size_t channel = 0U; channel < channels_; ++channel) {
            const float sample = samples[sample_offset + channel];
            if (!std::isfinite(sample)) {
                failed_ = true;
                return AG_DECODE_ERROR;
            }

            const float bass = bass_filters_[channel].process(sample);
            const float mid = mid_filters_[channel].process(sample);
            const float high = high_filters_[channel].process(sample);

            accumulate(buckets_, sample);
            accumulate(bass_buckets_, bass);
            accumulate(mid_buckets_, mid);
            accumulate(high_buckets_, high);
            ++bucket_sample_counts_[current_bucket_];
        }
    }
    consumed_frames_ += frames;
    return AG_OK;
}

namespace {

void normalize_layer(std::vector<float>& layer) noexcept
{
    if (layer.empty()) {
        return;
    }
    const float maximum = *std::max_element(layer.begin(), layer.end());
    if (!std::isfinite(maximum)) {
        std::fill(layer.begin(), layer.end(), 0.0F);
        return;
    }
    if (maximum > 0.0F) {
        for (float& value : layer) {
            value = std::clamp(value / maximum, 0.0F, 1.0F);
            if (!std::isfinite(value)) {
                value = 0.0F;
            }
        }
    }
}

void normalize_frequency_layers(std::vector<float>& bass,
                                std::vector<float>& mid,
                                std::vector<float>& high) noexcept
{
    float maximum = 0.0F;
    const auto include_maximum = [&maximum](const std::vector<float>& layer) {
        for (const float value : layer) {
            if (std::isfinite(value)) {
                maximum = std::max(maximum, value);
            }
        }
    };
    include_maximum(bass);
    include_maximum(mid);
    include_maximum(high);

    const auto normalize = [maximum](std::vector<float>& layer) {
        for (float& value : layer) {
            value = maximum > 0.0F && std::isfinite(value)
                ? std::clamp(value / maximum, 0.0F, 1.0F)
                : 0.0F;
        }
    };
    normalize(bass);
    normalize(mid);
    normalize(high);
}

void finalize_aggregation(std::vector<float>& layer,
                          const std::vector<std::size_t>& counts,
                          const WaveformAggregation aggregation) noexcept
{
    if (aggregation == WaveformAggregation::Peak) {
        return;
    }
    for (std::size_t index = 0U; index < layer.size(); ++index) {
        if (counts[index] == 0U) {
            layer[index] = 0.0F;
            continue;
        }
        layer[index] /= static_cast<float>(counts[index]);
        if (aggregation == WaveformAggregation::Rms) {
            layer[index] = std::sqrt(layer[index]);
        }
    }
}

} // namespace

ag_result WaveformBucketizer::finish(std::vector<float>& peaks,
                                     std::vector<float>& bass,
                                     std::vector<float>& mid,
                                     std::vector<float>& high) noexcept
{
    peaks.clear();
    bass.clear();
    mid.clear();
    high.clear();

    if (failed_ || consumed_frames_ != total_frames_ || buckets_.empty()) {
        return AG_DECODE_ERROR;
    }

    finalize_aggregation(buckets_, bucket_sample_counts_, aggregation_);
    finalize_aggregation(bass_buckets_, bucket_sample_counts_, aggregation_);
    finalize_aggregation(mid_buckets_, bucket_sample_counts_, aggregation_);
    finalize_aggregation(high_buckets_, bucket_sample_counts_, aggregation_);
    normalize_layer(buckets_);
    normalize_frequency_layers(bass_buckets_, mid_buckets_, high_buckets_);

    peaks = std::move(buckets_);
    bass = std::move(bass_buckets_);
    mid = std::move(mid_buckets_);
    high = std::move(high_buckets_);
    return AG_OK;
}

ag_result WaveformAnalyzer::analyze(
    const std::string& utf8_path,
    const std::size_t target_points,
    const std::atomic_bool* cancelled,
    const ag_progress_callback progress_callback,
    void* const user_data,
    std::vector<float>& peaks,
    std::vector<float>& bass,
    std::vector<float>& mid,
    std::vector<float>& high,
    const WaveformAggregation aggregation,
    std::uint64_t* const duration_ms,
    std::uint64_t* const total_samples,
    int* const output_sample_rate) noexcept
{
    peaks.clear();
    bass.clear();
    mid.clear();
    high.clear();
    if (duration_ms != nullptr) {
        *duration_ms = 0U;
    }
    if (total_samples != nullptr) {
        *total_samples = 0U;
    }
    if (output_sample_rate != nullptr) {
        *output_sample_rate = 0;
    }

    if (utf8_path.empty() || target_points == 0U) {
        return AG_INVALID_ARGUMENT;
    }
    if (is_cancelled(cancelled)) {
        return AG_CANCELLED;
    }

    try {
        if (progress_callback != nullptr) {
            progress_callback(0.0F, user_data);
        }
        if (is_cancelled(cancelled)) {
            return AG_CANCELLED;
        }

        Decoder counting_decoder;
        const ag_result open_result = counting_decoder.open(utf8_path);
        if (open_result != AG_OK) {
            return open_result;
        }

        std::size_t total_frames = 0U;
        DecodedAudioBlock block;
        do {
            if (is_cancelled(cancelled)) {
                return AG_CANCELLED;
            }
            const ag_result read_result = counting_decoder.read(block);
            if (read_result != AG_OK) {
                return read_result;
            }
            if (block.frames
                > std::numeric_limits<std::size_t>::max() - total_frames) {
                return AG_INTERNAL_ERROR;
            }
            total_frames += block.frames;
            if (is_cancelled(cancelled)) {
                return AG_CANCELLED;
            }
        } while (!block.end_of_stream);

        if (total_frames == 0U) {
            return AG_DECODE_ERROR;
        }
        const float sample_rate = static_cast<float>(counting_decoder.metadata().sample_rate);
        if (sample_rate <= 0.0F) {
            return AG_UNSUPPORTED_FORMAT;
        }
        if (duration_ms != nullptr) {
            *duration_ms = static_cast<std::uint64_t>(std::llround(
                static_cast<long double>(total_frames) * 1000.0L
                / static_cast<long double>(sample_rate)));
        }
        if (total_samples != nullptr) {
            *total_samples = static_cast<std::uint64_t>(total_frames);
        }
        if (output_sample_rate != nullptr) {
            *output_sample_rate = counting_decoder.metadata().sample_rate;
        }
        counting_decoder.close();
        if (progress_callback != nullptr) {
            progress_callback(0.5F, user_data);
        }
        if (is_cancelled(cancelled)) {
            return AG_CANCELLED;
        }

        Decoder decoder;
        const ag_result second_open_result = decoder.open(utf8_path);
        if (second_open_result != AG_OK) {
            return second_open_result;
        }
        const MediaMetadata& metadata = decoder.metadata();
        if (metadata.channels <= 0) {
            return AG_UNSUPPORTED_FORMAT;
        }
        WaveformBucketizer bucketizer(
            total_frames, target_points,
            static_cast<std::size_t>(metadata.channels),
            sample_rate, aggregation);
        std::size_t processed_frames = 0U;
        do {
            if (is_cancelled(cancelled)) {
                return AG_CANCELLED;
            }
            const ag_result read_result = decoder.read(block);
            if (read_result != AG_OK) {
                return read_result;
            }
            const ag_result add_result = bucketizer.add(block.samples,
                                                        block.frames);
            if (add_result != AG_OK) {
                return add_result;
            }
            processed_frames += block.frames;
            if (progress_callback != nullptr) {
                const long double ratio =
                    static_cast<long double>(processed_frames)
                    / static_cast<long double>(total_frames);
                progress_callback(static_cast<float>(
                                      0.5L + std::min(ratio, 1.0L) * 0.49L),
                                  user_data);
            }
            if (is_cancelled(cancelled)) {
                return AG_CANCELLED;
            }
        } while (!block.end_of_stream);

        const ag_result finish_result = bucketizer.finish(peaks, bass, mid, high);
        if (finish_result != AG_OK) {
            return finish_result;
        }
        if (progress_callback != nullptr) {
            progress_callback(1.0F, user_data);
        }
        if (is_cancelled(cancelled)) {
            peaks.clear();
            bass.clear();
            mid.clear();
            high.clear();
            return AG_CANCELLED;
        }
        return AG_OK;
    } catch (...) {
        peaks.clear();
        bass.clear();
        mid.clear();
        high.clear();
        if (duration_ms != nullptr) {
            *duration_ms = 0U;
        }
        if (total_samples != nullptr) {
            *total_samples = 0U;
        }
        if (output_sample_rate != nullptr) {
            *output_sample_rate = 0;
        }
        return AG_INTERNAL_ERROR;
    }
}

} // namespace agplayer
