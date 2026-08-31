#include "scratch_pcm_window.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace agplayer {

ScratchPcmWindow::ScratchPcmWindow(
    const std::uint32_t sample_rate,
    const std::uint32_t channels,
    const double desired_half_span_seconds,
    const std::size_t payload_budget_bytes)
    : sample_rate_(sample_rate)
    , channels_(channels)
{
    if (sample_rate == 0U || channels == 0U
        || !std::isfinite(desired_half_span_seconds)
        || desired_half_span_seconds <= 0.0) {
        throw std::invalid_argument("invalid scratch PCM window shape");
    }

    const std::size_t budget = std::min(
        payload_budget_bytes, default_bank_payload_budget());
    const std::size_t bytes_per_frame =
        static_cast<std::size_t>(channels) * sizeof(float);
    if (budget < bytes_per_frame) {
        throw std::length_error("scratch PCM payload budget is too small");
    }

    const std::size_t budget_frames = budget / bytes_per_frame;
    const long double desired_frames =
        static_cast<long double>(sample_rate)
        * static_cast<long double>(desired_half_span_seconds) * 2.0L;
    capacity_frames_ = desired_frames
            >= static_cast<long double>(budget_frames)
        ? budget_frames
        : static_cast<std::size_t>(desired_frames);
    if (capacity_frames_ == 0U
        || capacity_frames_ > std::numeric_limits<std::size_t>::max()
            / static_cast<std::size_t>(channels)) {
        throw std::length_error("scratch PCM window is too large");
    }
    storage_.resize(capacity_frames_ * static_cast<std::size_t>(channels));
}

ScratchWindowRange ScratchPcmWindow::planned_range(
    const std::int64_t anchor_frame,
    const std::int64_t source_frame_count) const noexcept
{
    if (source_frame_count <= 0) return {};

    const auto total_frames = static_cast<std::uint64_t>(source_frame_count);
    const std::size_t count = total_frames < capacity_frames_
        ? static_cast<std::size_t>(total_frames)
        : capacity_frames_;
    const std::int64_t anchor = std::clamp<std::int64_t>(
        anchor_frame, 0, source_frame_count - 1);
    const std::int64_t half = static_cast<std::int64_t>(count / 2U);
    const std::int64_t maximum_first =
        source_frame_count - static_cast<std::int64_t>(count);
    return {
        std::clamp<std::int64_t>(anchor - half, 0, maximum_first),
        count,
    };
}

bool ScratchPcmWindow::assign(const std::int64_t first_frame,
                              const float* const interleaved,
                              const std::size_t frame_count,
                              const std::uint64_t generation,
                              const std::uint64_t epoch) noexcept
{
    if (!valid_range(first_frame, frame_count)
        || (frame_count > 0U && interleaved == nullptr)) {
        return false;
    }

    if (frame_count > 0U) {
        std::copy_n(interleaved,
                    frame_count * static_cast<std::size_t>(channels_),
                    storage_.data());
    }
    return publish(first_frame, frame_count, generation, epoch);
}

bool ScratchPcmWindow::publish(const std::int64_t first_frame,
                               const std::size_t frame_count,
                               const std::uint64_t generation,
                               const std::uint64_t epoch) noexcept
{
    if (!valid_range(first_frame, frame_count)) return false;
    first_frame_ = first_frame;
    frame_count_ = frame_count;
    generation_ = generation;
    epoch_ = epoch;
    return true;
}

bool ScratchPcmWindow::valid_range(const std::int64_t first_frame,
                                   const std::size_t frame_count) const noexcept
{
    return first_frame >= 0 && frame_count <= capacity_frames_
        && (frame_count == 0U
            || first_frame <= std::numeric_limits<std::int64_t>::max()
                - static_cast<std::int64_t>(frame_count - 1U));
}

bool ScratchPcmWindow::read_linear(const double source_frame,
                                   const std::size_t channel,
                                   float& sample) const noexcept
{
    if (!std::isfinite(source_frame) || channel >= channels_
        || frame_count_ == 0U
        || source_frame < static_cast<double>(first_frame_)) {
        return false;
    }

    const double relative = source_frame - static_cast<double>(first_frame_);
    const double base_double = std::floor(relative);
    if (base_double < 0.0
        || base_double >= static_cast<double>(frame_count_)) {
        return false;
    }

    const auto base_frame = static_cast<std::size_t>(base_double);
    const double fraction = relative - base_double;
    const std::size_t first_sample =
        base_frame * static_cast<std::size_t>(channels_) + channel;
    if (fraction == 0.0) {
        sample = storage_[first_sample];
        return true;
    }
    if (base_frame + 1U >= frame_count_) return false;

    const float first = storage_[first_sample];
    const float second = storage_[first_sample + channels_];
    sample = first + (second - first) * static_cast<float>(fraction);
    return true;
}

} // namespace agplayer
