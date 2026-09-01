#define _USE_MATH_DEFINES

#include "waveform_analyzer.hpp"

#include "decoder.hpp"
#include "waveform_analyzer_filters.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
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

constexpr double pi = 3.14159265358979323846;

void fft_in_place(std::array<std::complex<double>, 512U>& values) noexcept
{
    constexpr std::size_t size = 512U;
    for (std::size_t i = 1U, j = 0U; i < size; ++i) {
        std::size_t bit = size >> 1U;
        for (; (j & bit) != 0U; bit >>= 1U) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            std::swap(values[i], values[j]);
        }
    }
    for (std::size_t length = 2U; length <= size; length <<= 1U) {
        const double angle = -2.0 * pi / static_cast<double>(length);
        const std::complex<double> step(std::cos(angle), std::sin(angle));
        for (std::size_t base = 0U; base < size; base += length) {
            std::complex<double> twiddle(1.0, 0.0);
            const std::size_t half = length >> 1U;
            for (std::size_t offset = 0U; offset < half; ++offset) {
                const std::complex<double> even = values[base + offset];
                const std::complex<double> odd =
                    values[base + offset + half] * twiddle;
                values[base + offset] = even + odd;
                values[base + offset + half] = even - odd;
                twiddle *= step;
            }
        }
    }
}

} // namespace

WaveformBucketizer::WaveformBucketizer(const std::size_t total_frames,
                                       const std::size_t target_points,
                                       const std::size_t channels,
                                       const float sample_rate,
                                       const WaveformAggregation aggregation,
                                       const bool include_spectral_index)
    : total_frames_(total_frames),
      channels_(channels),
      sample_rate_(sample_rate),
      aggregation_(aggregation),
      buckets_(std::min(total_frames, target_points), 0.0F),
      bass_buckets_(buckets_.size(), 0.0F),
      mid_buckets_(buckets_.size(), 0.0F),
      high_buckets_(buckets_.size(), 0.0F),
      bucket_sample_counts_(buckets_.size(), 0U),
      include_spectral_index_(include_spectral_index),
      spectral_weighted_indices_(include_spectral_index ? buckets_.size() : 0U,
                                 0.0),
      spectral_weights_(include_spectral_index ? buckets_.size() : 0U, 0.0),
      failed_(total_frames == 0U || target_points == 0U || channels == 0U
              || sample_rate <= 0.0F
              || (aggregation != WaveformAggregation::Peak
                  && aggregation != WaveformAggregation::AverageAbsolute
                  && aggregation != WaveformAggregation::Rms))
{
    if (!failed_) {
        const std::size_t frames_per_bucket =
            std::max<std::size_t>(1U, total_frames_ / buckets_.size());
        spectral_hop_frames_ = std::max<std::size_t>(
            128U, frames_per_bucket / 2U);
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

void WaveformBucketizer::add_spectral_frame(
    const float mono_sample,
    const std::size_t absolute_frame) noexcept
{
    if (!include_spectral_index_) {
        return;
    }
    spectral_ring_[spectral_ring_position_] = mono_sample;
    spectral_ring_position_ =
        (spectral_ring_position_ + 1U) % spectral_fft_size_;
    spectral_ring_count_ = std::min(
        spectral_fft_size_, spectral_ring_count_ + 1U);
    if (spectral_ring_count_ < spectral_fft_size_
        || absolute_frame < next_spectral_frame_) {
        return;
    }
    analyze_spectral_window(absolute_frame);
    next_spectral_frame_ = absolute_frame + spectral_hop_frames_;
}

void WaveformBucketizer::analyze_spectral_window(
    const std::size_t end_frame) noexcept
{
    std::array<std::complex<double>, spectral_fft_size_> spectrum{};
    double sum_squares = 0.0;
    for (std::size_t index = 0U; index < spectral_fft_size_; ++index) {
        const double sample = spectral_ring_[
            (spectral_ring_position_ + index) % spectral_fft_size_];
        sum_squares += sample * sample;
        const double window = 0.5
            * (1.0 - std::cos(2.0 * pi * static_cast<double>(index)
                              / static_cast<double>(spectral_fft_size_ - 1U)));
        spectrum[index] = {sample * window, 0.0};
    }
    const double rms = std::sqrt(sum_squares
                                 / static_cast<double>(spectral_fft_size_));
    constexpr double silence_rms = 1.0e-4;
    if (!std::isfinite(rms) || rms < silence_rms) {
        return;
    }

    fft_in_place(spectrum);
    double magnitude_sum = 0.0;
    double weighted_frequency = 0.0;
    for (std::size_t bin = 1U; bin < spectral_fft_size_ / 2U; ++bin) {
        const double magnitude = std::abs(spectrum[bin]);
        const double frequency = static_cast<double>(bin)
            * static_cast<double>(sample_rate_)
            / static_cast<double>(spectral_fft_size_);
        magnitude_sum += magnitude;
        weighted_frequency += frequency * magnitude;
    }
    if (!std::isfinite(magnitude_sum) || magnitude_sum <= 1.0e-12) {
        return;
    }

    const double centroid = weighted_frequency / magnitude_sum;
    constexpr double low_hz = 80.0;
    const double high_hz = std::min(
        14'000.0, static_cast<double>(sample_rate_) * 0.45);
    if (!(high_hz > low_hz) || !std::isfinite(centroid)) {
        return;
    }
    const double clamped = std::clamp(centroid, low_hz, high_hz);
    const double normalized = std::clamp(
        (std::log2(clamped) - std::log2(low_hz))
            / (std::log2(high_hz) - std::log2(low_hz)),
        0.0, 1.0);
    const std::size_t center_frame = end_frame
        - (spectral_fft_size_ - 1U) / 2U;
    const std::size_t bucket = std::min(
        buckets_.size() - 1U,
        center_frame * buckets_.size() / total_frames_);
    spectral_weighted_indices_[bucket] += normalized * rms;
    spectral_weights_[bucket] += rms;
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
        double mono_sample = 0.0;

        for (std::size_t channel = 0U; channel < channels_; ++channel) {
            const float sample = samples[sample_offset + channel];
            if (!std::isfinite(sample)) {
                failed_ = true;
                return AG_DECODE_ERROR;
            }

            const float bass = bass_filters_[channel].process(sample);
            const float mid = mid_filters_[channel].process(sample);
            const float high = high_filters_[channel].process(sample);
            mono_sample += sample;

            accumulate(buckets_, sample);
            accumulate(bass_buckets_, bass);
            accumulate(mid_buckets_, mid);
            accumulate(high_buckets_, high);
            ++bucket_sample_counts_[current_bucket_];
        }
        add_spectral_frame(
            static_cast<float>(mono_sample / static_cast<double>(channels_)),
            absolute_frame);
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
                                     std::vector<float>& high,
                                     std::vector<std::uint8_t>* spectral_index) noexcept
{
    peaks.clear();
    bass.clear();
    mid.clear();
    high.clear();
    if (spectral_index != nullptr) {
        spectral_index->clear();
    }

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
    if (spectral_index != nullptr && include_spectral_index_) {
        const std::size_t point_count = peaks.size();
        std::vector<double> normalized(point_count, 0.0);
        std::vector<std::size_t> left_valid(point_count, point_count);
        std::vector<std::size_t> right_valid(point_count, point_count);
        std::size_t latest = point_count;
        for (std::size_t index = 0U; index < point_count; ++index) {
            if (spectral_weights_[index] > 0.0) {
                normalized[index] = spectral_weighted_indices_[index]
                    / spectral_weights_[index];
                latest = index;
            }
            left_valid[index] = latest;
        }
        latest = point_count;
        for (std::size_t index = point_count; index > 0U; --index) {
            const std::size_t current = index - 1U;
            if (spectral_weights_[current] > 0.0) {
                latest = current;
            }
            right_valid[current] = latest;
        }
        for (std::size_t index = 0U; index < normalized.size(); ++index) {
            if (spectral_weights_[index] > 0.0) {
                continue;
            }
            const std::size_t left = left_valid[index];
            const std::size_t right = right_valid[index];
            if (left == normalized.size() && right == normalized.size()) {
                normalized[index] = 0.0;
            } else if (left == normalized.size()) {
                normalized[index] = normalized[right];
            } else if (right == normalized.size()) {
                normalized[index] = normalized[left];
            } else {
                const double fraction = static_cast<double>(index - left)
                    / static_cast<double>(right - left);
                normalized[index] = normalized[left]
                    + (normalized[right] - normalized[left]) * fraction;
            }
        }
        const std::vector<double> unsmoothed = normalized;
        for (std::size_t index = 0U; index < normalized.size(); ++index) {
            const std::size_t begin = index == 0U ? 0U : index - 1U;
            const std::size_t end = std::min(normalized.size() - 1U, index + 1U);
            double sum = 0.0;
            for (std::size_t neighbor = begin; neighbor <= end; ++neighbor) {
                sum += unsmoothed[neighbor];
            }
            normalized[index] = sum / static_cast<double>(end - begin + 1U);
        }
        spectral_index->resize(normalized.size());
        for (std::size_t index = 0U; index < normalized.size(); ++index) {
            (*spectral_index)[index] = static_cast<std::uint8_t>(std::lround(
                std::clamp(normalized[index], 0.0, 1.0) * 255.0));
        }
    }
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
    int* const output_sample_rate,
    std::vector<std::uint8_t>* const spectral_index,
    void (*const pause_checkpoint)(void*),
    void* const pause_user_data) noexcept
{
    peaks.clear();
    bass.clear();
    mid.clear();
    high.clear();
    if (spectral_index != nullptr) {
        spectral_index->clear();
    }
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
            if (pause_checkpoint != nullptr) {
                pause_checkpoint(pause_user_data);
            }
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
            sample_rate, aggregation, spectral_index != nullptr);
        std::size_t processed_frames = 0U;
        do {
            if (pause_checkpoint != nullptr) {
                pause_checkpoint(pause_user_data);
            }
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

        const ag_result finish_result = bucketizer.finish(
            peaks, bass, mid, high, spectral_index);
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
            if (spectral_index != nullptr) {
                spectral_index->clear();
            }
            return AG_CANCELLED;
        }
        return AG_OK;
    } catch (...) {
        peaks.clear();
        bass.clear();
        mid.clear();
        high.clear();
        if (spectral_index != nullptr) {
            spectral_index->clear();
        }
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
