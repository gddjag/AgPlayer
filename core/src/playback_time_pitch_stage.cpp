#include "playback_time_pitch_stage.hpp"

#include <algorithm>
#include <cmath>

namespace agplayer {
namespace {

bool valid_config(const int sampleRate, const int channels,
                  const PlaybackTimePitchConfig& config) noexcept
{
    return sampleRate > 0 && channels > 0 && channels <= 2
        && std::isfinite(config.speed_ratio)
        && config.speed_ratio >= 0.75 && config.speed_ratio <= 1.50;
}

} // namespace

ag_result PlaybackTimePitchStage::configure(
    const int sampleRate, const int channels,
    const PlaybackTimePitchConfig config) noexcept
{
    if (!valid_config(sampleRate, channels, config)) {
        return AG_INVALID_ARGUMENT;
    }
    try {
        channels_ = channels;
        configured_ = true;
        finished_ = false;
        processor_.reset();
        receive_buffer_.clear();
        pending_source_frames_.clear();
        pending_source_offset_ = 0U;
        source_frame_debt_ = 0U;
        source_step_accumulator_ = 0.0;
        speed_ratio_ = config.speed_ratio;
        last_source_frame_ = 0;
        last_mapped_source_frame_ = 0;
        if (std::abs(config.speed_ratio - 1.0) < 0.000001) {
            return AG_OK;
        }
        processor_ = factory_ == nullptr ? nullptr : factory_();
        if (processor_ == nullptr
            || !processor_->configure(sampleRate, channels)
            || !processor_->setPitchCents(0.0)
            || !processor_->setFormantPreservation(false)
            || !processor_->setTempoRatio(
                config.keep_pitch ? config.speed_ratio : 1.0)
            || !processor_->setRateRatio(
                config.keep_pitch ? 1.0 : config.speed_ratio)) {
            processor_.reset();
            configured_ = false;
            return AG_INTERNAL_ERROR;
        }
        receive_buffer_.resize(
            work_frames * static_cast<std::size_t>(channels_));
        pending_source_frames_.reserve(work_frames * 2U);
        return AG_OK;
    } catch (...) {
        processor_.reset();
        configured_ = false;
        return AG_INTERNAL_ERROR;
    }
}

ag_result PlaybackTimePitchStage::process(
    const float* const samples, const std::size_t frames,
    std::vector<float>& output) noexcept
{
    std::vector<std::int64_t> unused_mapping;
    return process(samples, frames, last_source_frame_,
                   last_source_frame_ + static_cast<std::int64_t>(frames),
                   output, unused_mapping);
}

ag_result PlaybackTimePitchStage::process(
    const float* const samples, const std::size_t frames,
    const std::int64_t source_start_frame,
    const std::int64_t source_end_frame,
    std::vector<float>& output,
    std::vector<std::int64_t>& source_frame_after) noexcept
{
    if (!configured_ || finished_ || samples == nullptr || frames == 0U
        || source_end_frame < source_start_frame) {
        return frames == 0U && configured_ && !finished_
                   ? AG_OK : AG_INVALID_ARGUMENT;
    }
    try {
        const std::size_t channels = static_cast<std::size_t>(channels_);
        if (processor_ == nullptr) {
            output.insert(output.end(), samples, samples + frames * channels);
            append_input_mapping(frames, source_start_frame, source_end_frame);
            append_output_mapping(frames, source_frame_after);
            return AG_OK;
        }
        std::size_t offset = 0U;
        while (offset < frames) {
            const std::size_t count = std::min(work_frames, frames - offset);
            const std::int64_t chunk_start = source_start_frame
                + static_cast<std::int64_t>(
                    (static_cast<long double>(source_end_frame
                                              - source_start_frame)
                     * static_cast<long double>(offset))
                    / static_cast<long double>(frames));
            const std::int64_t chunk_end = source_start_frame
                + static_cast<std::int64_t>(
                    (static_cast<long double>(source_end_frame
                                              - source_start_frame)
                     * static_cast<long double>(offset + count))
                    / static_cast<long double>(frames));
            append_input_mapping(count, chunk_start, chunk_end);
            processor_->put(samples + offset * channels, count);
            if (processor_->failed()) return AG_INTERNAL_ERROR;
            const ag_result result = drain(output, source_frame_after);
            if (result != AG_OK) return result;
            offset += count;
        }
        return AG_OK;
    } catch (...) {
        return AG_INTERNAL_ERROR;
    }
}

ag_result PlaybackTimePitchStage::finish(std::vector<float>& output) noexcept
{
    std::vector<std::int64_t> unused_mapping;
    return finish(output, unused_mapping);
}

ag_result PlaybackTimePitchStage::finish(
    std::vector<float>& output,
    std::vector<std::int64_t>& source_frame_after) noexcept
{
    if (!configured_ || finished_) return AG_INVALID_ARGUMENT;
    finished_ = true;
    if (processor_ == nullptr) return AG_OK;
    try {
        const std::size_t mapping_start = source_frame_after.size();
        processor_->flush();
        if (processor_->failed()) return AG_INTERNAL_ERROR;
        const ag_result result = drain(output, source_frame_after);
        if (result == AG_OK && source_frame_after.size() > mapping_start) {
            source_frame_after.back() = last_source_frame_;
        }
        return result;
    } catch (...) {
        return AG_INTERNAL_ERROR;
    }
}

void PlaybackTimePitchStage::reset() noexcept
{
    if (processor_ != nullptr) processor_->reset();
    finished_ = false;
    pending_source_frames_.clear();
    pending_source_offset_ = 0U;
    source_frame_debt_ = 0U;
    source_step_accumulator_ = 0.0;
    last_source_frame_ = 0;
    last_mapped_source_frame_ = 0;
}

ag_result PlaybackTimePitchStage::drain(
    std::vector<float>& output,
    std::vector<std::int64_t>& source_frame_after) noexcept
{
    const std::size_t channels = static_cast<std::size_t>(channels_);
    for (;;) {
        const std::size_t received = processor_->receive(
            receive_buffer_.data(), work_frames);
        if (processor_->failed()) return AG_INTERNAL_ERROR;
        if (received == 0U) return AG_OK;
        output.insert(output.end(), receive_buffer_.data(),
                      receive_buffer_.data() + received * channels);
        append_output_mapping(received, source_frame_after);
    }
}

void PlaybackTimePitchStage::append_input_mapping(
    const std::size_t frames, const std::int64_t source_start_frame,
    const std::int64_t source_end_frame)
{
    compact_consumed_mapping();
    const long double extent = static_cast<long double>(
        source_end_frame - source_start_frame);
    for (std::size_t frame = 0U; frame < frames; ++frame) {
        pending_source_frames_.push_back(
            source_start_frame + static_cast<std::int64_t>(
                std::ceil(extent * static_cast<long double>(frame + 1U)
                          / static_cast<long double>(frames))));
    }
    last_source_frame_ = source_end_frame;
}

void PlaybackTimePitchStage::append_output_mapping(
    const std::size_t frames,
    std::vector<std::int64_t>& source_frame_after)
{
    source_frame_after.reserve(source_frame_after.size() + frames);
    for (std::size_t frame = 0U; frame < frames; ++frame) {
        source_step_accumulator_ += speed_ratio_;
        const std::size_t advance = source_frame_debt_
            + static_cast<std::size_t>(
                std::floor(source_step_accumulator_));
        source_step_accumulator_ -= static_cast<double>(
            advance - source_frame_debt_);
        const std::size_t available = pending_source_frames_.size()
            - pending_source_offset_;
        const std::size_t consumed = std::min(advance, available);
        source_frame_debt_ = advance - consumed;
        if (consumed > 0U) {
            pending_source_offset_ += consumed;
            last_mapped_source_frame_ =
                pending_source_frames_[pending_source_offset_ - 1U];
        }
        source_frame_after.push_back(last_mapped_source_frame_);
    }
    compact_consumed_mapping();
}

void PlaybackTimePitchStage::compact_consumed_mapping()
{
    if (pending_source_offset_ == pending_source_frames_.size()) {
        pending_source_frames_.clear();
        pending_source_offset_ = 0U;
    } else if (pending_source_offset_ >= mapping_compaction_threshold) {
        pending_source_frames_.erase(
            pending_source_frames_.begin(),
            pending_source_frames_.begin()
                + static_cast<std::ptrdiff_t>(pending_source_offset_));
        pending_source_offset_ = 0U;
    }
}

} // namespace agplayer
