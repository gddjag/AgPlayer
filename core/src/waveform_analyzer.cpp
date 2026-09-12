#include "waveform_analyzer.hpp"

#include "decoder.hpp"
#include "waveform_analyzer_filters.hpp"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <limits>
#include <type_traits>
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
                                       const WaveformAggregation aggregation,
                                       const bool streaming)
    : total_frames_(total_frames),
      channels_(channels),
      sample_rate_(sample_rate),
      aggregation_(aggregation),
      buckets_(std::min(total_frames, target_points), 0.0F),
      raw_peaks_(buckets_.size(), 0.0F),
      raw_sum_squares_(buckets_.size(), 0.0F),
      bass_sum_squares_(buckets_.size(), 0.0F),
      mid_sum_squares_(buckets_.size(), 0.0F),
      high_sum_squares_(buckets_.size(), 0.0F),
      bucket_sample_counts_(buckets_.size(), 0U),
      failed_(total_frames == 0U || target_points == 0U || channels == 0U
              || sample_rate <= 0.0F
              || (aggregation != WaveformAggregation::Peak
                  && aggregation != WaveformAggregation::AverageAbsolute
                  && aggregation != WaveformAggregation::Rms)),
      streaming_(streaming)
{
    if (!failed_) {
        bucket_base_frames_ = total_frames_ / buckets_.size();
        bucket_remainder_ = total_frames_ % buckets_.size();
        bucket_error_ = buckets_.size() - 1U;
        if (streaming_) {
            bucket_base_frames_ = (total_frames_ - 1U) / buckets_.size() + 1U;
            bucket_remainder_ = 0U;
        }
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
    if (failed_ || (!streaming_ && frames > total_frames_ - consumed_frames_)
        || frames > std::numeric_limits<std::size_t>::max() - consumed_frames_
        || frames > std::numeric_limits<std::size_t>::max() / channels_
        || samples.size() != frames * channels_) {
        failed_ = true;
        return AG_DECODE_ERROR;
    }

    for (std::size_t frame = 0U; frame < frames; ++frame) {
        const std::size_t absolute_frame = consumed_frames_ + frame;
        if (streaming_) {
            while (absolute_frame / bucket_base_frames_ >= buckets_.size()) {
                compact_streaming_buckets();
            }
            current_bucket_ = absolute_frame / bucket_base_frames_;
            next_bucket_frame_ = (current_bucket_ + 1U) * bucket_base_frames_;
        }
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
            raw_peaks_[current_bucket_] = std::max(raw_peaks_[current_bucket_], std::abs(sample));
            raw_sum_squares_[current_bucket_] += sample * sample;
            bass_sum_squares_[current_bucket_] += bass * bass;
            mid_sum_squares_[current_bucket_] += mid * mid;
            high_sum_squares_[current_bucket_] += high * high;
            ++bucket_sample_counts_[current_bucket_];
        }
    }
    consumed_frames_ += frames;
    return AG_OK;
}

void WaveformBucketizer::compact_streaming_buckets() noexcept
{
    // Incorrect/unknown container duration must not create unbounded storage.
    // Combining sufficient statistics retains peaks and energy in one decode.
    const auto merge = [](auto& values, const bool maximum) {
        using Value = typename std::decay_t<decltype(values)>::value_type;
        const std::size_t old_size = values.size();
        const std::size_t kept = (old_size + 1U) / 2U;
        for (std::size_t i = 0; i < kept; ++i) {
            const auto left = values[2U * i];
            const auto right = 2U * i + 1U < old_size ? values[2U * i + 1U] : Value{};
            values[i] = maximum ? std::max(left, right) : left + right;
        }
        std::fill(values.begin() + kept, values.end(), Value{});
    };
    merge(buckets_, aggregation_ == WaveformAggregation::Peak);
    merge(raw_peaks_, true);
    merge(raw_sum_squares_, false);
    merge(bass_sum_squares_, false);
    merge(mid_sum_squares_, false);
    merge(high_sum_squares_, false);
    merge(bucket_sample_counts_, false);
    bucket_base_frames_ *= 2U;
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

void finalize_frequency_energy(std::vector<float>& sum_squares,
                               const std::vector<std::size_t>& counts) noexcept
{
    for (std::size_t index = 0U; index < sum_squares.size(); ++index) {
        if (counts[index] == 0U) {
            sum_squares[index] = 0.0F;
            continue;
        }
        const float rms = std::sqrt(sum_squares[index]
                                    / static_cast<float>(counts[index]));
        sum_squares[index] = std::clamp(rms, 0.0F, 1.0F);
    }
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
                                     std::vector<float>* raw_peak,
                                     std::vector<float>* raw_rms) noexcept
{
    peaks.clear();
    bass.clear();
    mid.clear();
    high.clear();
    if (raw_peak) raw_peak->clear();
    if (raw_rms) raw_rms->clear();
    if (failed_ || (!streaming_ && consumed_frames_ != total_frames_)
        || consumed_frames_ == 0U || buckets_.empty()) {
        return AG_DECODE_ERROR;
    }

    if (streaming_) {
        const auto count = (consumed_frames_ - 1U) / bucket_base_frames_ + 1U;
        buckets_.resize(count); raw_peaks_.resize(count); raw_sum_squares_.resize(count);
        bass_sum_squares_.resize(count); mid_sum_squares_.resize(count); high_sum_squares_.resize(count);
        bucket_sample_counts_.resize(count);
    }
    finalize_aggregation(raw_sum_squares_, bucket_sample_counts_, WaveformAggregation::Rms);
    for (std::size_t i = 0; i < raw_peaks_.size(); ++i) {
        raw_peaks_[i] = std::clamp(raw_peaks_[i], 0.0F, 1.0F);
        raw_sum_squares_[i] = std::clamp(raw_sum_squares_[i], 0.0F, raw_peaks_[i]);
    }
    if (raw_peak) *raw_peak = std::move(raw_peaks_);
    if (raw_rms) *raw_rms = std::move(raw_sum_squares_);

    finalize_aggregation(buckets_, bucket_sample_counts_, aggregation_);
    finalize_frequency_energy(bass_sum_squares_, bucket_sample_counts_);
    finalize_frequency_energy(mid_sum_squares_, bucket_sample_counts_);
    finalize_frequency_energy(high_sum_squares_, bucket_sample_counts_);
    normalize_layer(buckets_);

    peaks = std::move(buckets_);
    bass = std::move(bass_sum_squares_);
    mid = std::move(mid_sum_squares_);
    high = std::move(high_sum_squares_);
    return AG_OK;
}

ag_result WaveformBucketizer::snapshot(ag_waveform_snapshot_callback callback,
                                      void* user_data) const noexcept
{
    if (!callback || failed_ || !streaming_) return AG_INVALID_ARGUMENT;
    try {
        constexpr std::size_t max_preview_points = 4096U;
        const auto group = (buckets_.size() - 1U) / max_preview_points + 1U;
        const auto count = (buckets_.size() - 1U) / group + 1U;
        std::vector<float> mix(count), peak(count), rms(count), bass(count), mid(count), high(count);
        std::vector<std::size_t> counts(count);
        const auto complete = std::min(consumed_frames_ / bucket_base_frames_, buckets_.size());
        // Copy only sufficient statistics from completed source buckets. The
        // zero tail costs at most 4096 values/layer even on multi-hour tracks.
        for (std::size_t i = 0; i < complete; ++i) {
            const auto target = i / group;
            mix[target] = aggregation_ == WaveformAggregation::Peak
                ? std::max(mix[target], buckets_[i]) : mix[target] + buckets_[i];
            peak[target] = std::max(peak[target], raw_peaks_[i]);
            rms[target] += raw_sum_squares_[i];
            bass[target] += bass_sum_squares_[i];
            mid[target] += mid_sum_squares_[i];
            high[target] += high_sum_squares_[i];
            counts[target] += bucket_sample_counts_[i];
        }
        finalize_aggregation(mix, counts, aggregation_);
        finalize_aggregation(rms, counts, WaveformAggregation::Rms);
        finalize_frequency_energy(bass, counts);
        finalize_frequency_energy(mid, counts);
        finalize_frequency_energy(high, counts);
        // Unit gain throughout preview: later loud passages cannot pump earlier
        // amplitudes or alter crest factor. Final mix retains legacy scaling.
        for (auto* layer : {&mix, &peak, &rms, &bass, &mid, &high}) {
            for (auto& value : *layer) value = std::clamp(value, 0.0F, 1.0F);
        }
        const ag_waveform_snapshot view{mix.data(), bass.data(), mid.data(), high.data(),
            peak.data(), rms.data(), mix.size(),
            static_cast<std::uint64_t>(mix.size() * group * bucket_base_frames_),
            static_cast<int>(sample_rate_)};
        callback(&view, user_data);
        return AG_OK;
    } catch (...) { return AG_INTERNAL_ERROR; }
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
    void (*const pause_checkpoint)(void*),
    void* const pause_user_data,
    std::vector<float>* const raw_peak,
    std::vector<float>* const raw_rms,
    const ag_waveform_snapshot_callback snapshot_callback,
    void* const snapshot_user_data) noexcept
{
    peaks.clear();
    bass.clear();
    mid.clear();
    high.clear();
    if (raw_peak) raw_peak->clear();
    if (raw_rms) raw_rms->clear();
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

        Decoder decoder;
        const ag_result open_result = decoder.open(utf8_path);
        if (open_result != AG_OK) {
            return open_result;
        }

        const MediaMetadata& metadata = decoder.metadata();
        const float sample_rate = static_cast<float>(metadata.sample_rate);
        if (metadata.channels <= 0 || sample_rate <= 0.0F) {
            return AG_UNSUPPORTED_FORMAT;
        }
        const long double estimate = std::max<std::int64_t>(1, metadata.duration_ms)
            * static_cast<long double>(metadata.sample_rate) / 1000.0L;
        if (estimate >= static_cast<long double>(std::numeric_limits<std::size_t>::max())) {
            return AG_UNSUPPORTED_FORMAT;
        }
        const std::size_t total_frames = std::max<std::size_t>(
            target_points, static_cast<std::size_t>(std::ceil(estimate)));
        WaveformBucketizer bucketizer(
            total_frames, target_points,
            static_cast<std::size_t>(metadata.channels),
            sample_rate, aggregation, true);
        DecodedAudioBlock block;
        std::size_t processed_frames = 0U;
        auto last_snapshot = std::chrono::steady_clock::time_point{};
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
                                      std::min(ratio, 1.0L) * 0.99L),
                                  user_data);
            }
            if (is_cancelled(cancelled)) {
                return AG_CANCELLED;
            }
            const auto now = std::chrono::steady_clock::now();
            if (snapshot_callback && !block.end_of_stream
                && now - last_snapshot >= std::chrono::milliseconds(250)) {
                const auto snapshot_result = bucketizer.snapshot(snapshot_callback, snapshot_user_data);
                if (snapshot_result != AG_OK) return snapshot_result;
                last_snapshot = now;
                if (is_cancelled(cancelled)) return AG_CANCELLED;
            }
        } while (!block.end_of_stream);

        const ag_result finish_result = bucketizer.finish(
            peaks, bass, mid, high, raw_peak, raw_rms);
        if (finish_result != AG_OK) {
            return finish_result;
        }
        if (duration_ms) *duration_ms = static_cast<std::uint64_t>(std::llround(
            static_cast<long double>(processed_frames) * 1000.0L / sample_rate));
        if (total_samples) *total_samples = processed_frames;
        if (output_sample_rate) *output_sample_rate = metadata.sample_rate;
        if (progress_callback != nullptr) {
            progress_callback(1.0F, user_data);
        }
        if (is_cancelled(cancelled)) {
            peaks.clear();
            bass.clear();
            mid.clear();
            high.clear();
            if (raw_peak) raw_peak->clear();
            if (raw_rms) raw_rms->clear();
            return AG_CANCELLED;
        }
        return AG_OK;
    } catch (...) {
        peaks.clear();
        bass.clear();
        mid.clear();
        high.clear();
        if (raw_peak) raw_peak->clear();
        if (raw_rms) raw_rms->clear();
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
