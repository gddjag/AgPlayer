#include "waveform_analyzer.hpp"

#include "decoder.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace agplayer {
namespace {

bool is_cancelled(const std::atomic_bool* cancelled) noexcept
{
    return cancelled != nullptr
           && cancelled->load(std::memory_order_relaxed);
}

std::size_t frame_bucket(const std::uint64_t frame,
                         const std::uint64_t estimated_frames,
                         const std::size_t target_points) noexcept
{
    if (estimated_frames == 0U || target_points == 0U) {
        return 0U;
    }
    const long double scaled = static_cast<long double>(frame)
                               * static_cast<long double>(target_points)
                               / static_cast<long double>(estimated_frames);
    return std::min(static_cast<std::size_t>(scaled), target_points - 1U);
}

void report_progress(const std::uint64_t decoded_frames,
                     const std::uint64_t estimated_frames,
                     ag_progress_callback callback,
                     void* user_data)
{
    if (callback == nullptr) {
        return;
    }
    const long double ratio = estimated_frames == 0U
                                  ? 0.0L
                                  : static_cast<long double>(decoded_frames)
                                        / static_cast<long double>(estimated_frames);
    callback(static_cast<float>(std::min(ratio, 0.99L)), user_data);
}

} // namespace

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

        Decoder decoder;
        const ag_result open_result = decoder.open(utf8_path);
        if (open_result != AG_OK) {
            return open_result;
        }

        const MediaMetadata& metadata = decoder.metadata();
        if (metadata.sample_rate <= 0 || metadata.channels <= 0) {
            return AG_UNSUPPORTED_FORMAT;
        }
        const std::uint64_t estimated_frames = metadata.duration_ms > 0
            ? (static_cast<std::uint64_t>(metadata.duration_ms)
                   * static_cast<std::uint64_t>(metadata.sample_rate)
               + 999U)
                  / 1'000U
            : 0U;

        std::vector<float> buckets(target_points, 0.0F);
        std::uint64_t decoded_frames = 0U;
        std::size_t populated_points = 0U;
        DecodedAudioBlock block;
        do {
            if (is_cancelled(cancelled)) {
                return AG_CANCELLED;
            }
            const ag_result read_result = decoder.read(block);
            if (read_result != AG_OK) {
                return read_result;
            }

            for (std::size_t frame = 0U; frame < block.frames; ++frame) {
                float frame_peak = 0.0F;
                const std::size_t sample_offset =
                    frame * static_cast<std::size_t>(metadata.channels);
                for (int channel = 0; channel < metadata.channels; ++channel) {
                    frame_peak = std::max(
                        frame_peak,
                        std::abs(block.samples[sample_offset
                                               + static_cast<std::size_t>(channel)]));
                }
                const std::size_t bucket = estimated_frames == 0U
                    ? std::min(static_cast<std::size_t>(decoded_frames + frame),
                               target_points - 1U)
                    : frame_bucket(decoded_frames + frame, estimated_frames,
                                   target_points);
                buckets[bucket] = std::max(buckets[bucket], frame_peak);
                populated_points = std::max(populated_points, bucket + 1U);
            }
            decoded_frames += block.frames;
            report_progress(decoded_frames, estimated_frames,
                            progress_callback, user_data);
            if (is_cancelled(cancelled)) {
                return AG_CANCELLED;
            }
        } while (!block.end_of_stream);

        if (populated_points == 0U) {
            return AG_DECODE_ERROR;
        }
        buckets.resize(populated_points);
        const float maximum = *std::max_element(buckets.begin(), buckets.end());
        if (maximum > 0.0F && std::isfinite(maximum)) {
            for (float& peak : buckets) {
                peak /= maximum;
            }
        }
        peaks = std::move(buckets);
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
