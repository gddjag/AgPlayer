#include "waveform_analyzer.hpp"

#include "decoder.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

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
                                       const std::size_t channels)
    : total_frames_(total_frames),
      channels_(channels),
      buckets_(std::min(total_frames, target_points), 0.0F),
      failed_(total_frames == 0U || target_points == 0U || channels == 0U)
{
    if (!failed_) {
        bucket_base_frames_ = total_frames_ / buckets_.size();
        bucket_remainder_ = total_frames_ % buckets_.size();
        bucket_error_ = buckets_.size() - 1U;
        extend_bucket_boundary();
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
        float frame_peak = 0.0F;
        const std::size_t sample_offset = frame * channels_;
        for (std::size_t channel = 0U; channel < channels_; ++channel) {
            const float sample = samples[sample_offset + channel];
            if (!std::isfinite(sample)) {
                failed_ = true;
                return AG_DECODE_ERROR;
            }
            frame_peak = std::max(frame_peak, std::abs(sample));
        }
        const std::size_t absolute_frame = consumed_frames_ + frame;
        while (current_bucket_ + 1U < buckets_.size()
               && absolute_frame >= next_bucket_frame_) {
            ++current_bucket_;
            extend_bucket_boundary();
        }
        buckets_[current_bucket_] =
            std::max(buckets_[current_bucket_], frame_peak);
    }
    consumed_frames_ += frames;
    return AG_OK;
}

ag_result WaveformBucketizer::finish(std::vector<float>& peaks) noexcept
{
    peaks.clear();
    if (failed_ || consumed_frames_ != total_frames_ || buckets_.empty()) {
        return AG_DECODE_ERROR;
    }

    const float maximum = *std::max_element(buckets_.begin(), buckets_.end());
    if (!std::isfinite(maximum)) {
        failed_ = true;
        return AG_DECODE_ERROR;
    }
    if (maximum > 0.0F) {
        for (float& peak : buckets_) {
            peak = std::clamp(peak / maximum, 0.0F, 1.0F);
            if (!std::isfinite(peak)) {
                failed_ = true;
                return AG_DECODE_ERROR;
            }
        }
    }
    peaks = std::move(buckets_);
    return AG_OK;
}

ag_result WaveformAnalyzer::analyze(
    const std::string& utf8_path,
    const std::size_t target_points,
    const std::atomic_bool* cancelled,
    const ag_progress_callback progress_callback,
    void* const user_data,
    std::vector<float>& peaks) noexcept
{
    peaks.clear();
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
            static_cast<std::size_t>(metadata.channels));
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

        const ag_result finish_result = bucketizer.finish(peaks);
        if (finish_result != AG_OK) {
            return finish_result;
        }
        if (progress_callback != nullptr) {
            progress_callback(1.0F, user_data);
        }
        if (is_cancelled(cancelled)) {
            peaks.clear();
            return AG_CANCELLED;
        }
        return AG_OK;
    } catch (...) {
        peaks.clear();
        return AG_INTERNAL_ERROR;
    }
}

} // namespace agplayer
