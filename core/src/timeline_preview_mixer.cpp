#include "timeline_preview_mixer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace agplayer {

namespace {

constexpr unsigned int kProcessorReceiveFrames = 2048U;
constexpr std::size_t kProcessorFeedFrames = 1024U;

std::int64_t milliseconds_to_frames(const long long milliseconds,
                                    const int sample_rate) noexcept
{
    return milliseconds <= 0 || sample_rate <= 0
               ? 0
               : (milliseconds * static_cast<std::int64_t>(sample_rate) + 500)
                     / 1000;
}

std::int64_t configured_frames(const long long sample_count,
                               const long long milliseconds,
                               const int sample_rate) noexcept
{
    return sample_count >= 0 ? sample_count
                             : milliseconds_to_frames(milliseconds, sample_rate);
}

} // namespace

TimelinePreviewMixer::~TimelinePreviewMixer() = default;
TimelinePreviewMixer::ClipState::~ClipState() = default;
TimelinePreviewMixer::ClipState::ClipState(ClipState&&) noexcept = default;
TimelinePreviewMixer::ClipState& TimelinePreviewMixer::ClipState::operator=(
    ClipState&&) noexcept = default;

float TimelinePreviewMixer::fade_gain(const FadeCurve curve,
                                      const float progress) noexcept
{
    const float value = std::clamp(progress, 0.0F, 1.0F);
    switch (curve) {
    case FadeCurve::Linear:
        return value;
    case FadeCurve::Smooth:
        return value * value * (3.0F - 2.0F * value);
    case FadeCurve::EqualPower:
    default:
        return std::sin(value * 1.57079632679489661923F);
    }
}

ag_result TimelinePreviewMixer::configure(const MultiTrackEditConfig& config,
                                          const int sample_rate,
                                          const int channels,
                                          std::string& error)
{
    error.clear();
    if (sample_rate <= 0 || channels < 1 || channels > 2 || config.tracks.empty()) {
        error = "Timeline preview requires audio tracks and a valid output format.";
        return AG_INVALID_ARGUMENT;
    }

    std::vector<ClipState> configured;
    configured.reserve(config.tracks.size());
    std::int64_t max_end_frame = 0;
    for (const MultiTrackEditConfig::Track& track : config.tracks) {
        if (track.input_path.empty()) {
            continue;
        }
        auto decoder = std::make_unique<Decoder>();
        const ag_result open_result = decoder->open(track.input_path, sample_rate, 2);
        if (open_result != AG_OK) {
            error = "Unable to decode a timeline clip.";
            return open_result;
        }
        const std::int64_t source_frames = std::max<std::int64_t>(
            0, milliseconds_to_frames(decoder->metadata().duration_ms, sample_rate));
        const std::int64_t trim_start = std::clamp<std::int64_t>(
            configured_frames(track.trim_start_sample, track.trim_start_ms, sample_rate),
            0, source_frames);
        const std::int64_t requested_trim_end = configured_frames(
            track.trim_end_sample, track.trim_end_ms, sample_rate);
        const std::int64_t trim_end = requested_trim_end <= 0
                                         ? source_frames
                                         : std::clamp(requested_trim_end,
                                                      trim_start, source_frames);
        if (trim_end <= trim_start) {
            continue;
        }
        const std::int64_t source_duration = trim_end - trim_start;
        const std::int64_t requested_duration = configured_frames(
            track.timeline_duration_samples, track.timeline_duration_ms, sample_rate);
        const double speed_ratio = std::clamp(track.speed_ratio, 0.5, 2.0);
        const std::int64_t transformed_duration = std::max<std::int64_t>(
            1, static_cast<std::int64_t>(std::llround(
                   static_cast<double>(source_duration) / speed_ratio)));
        const std::int64_t timeline_duration = track.loop && requested_duration > 0
                                                   ? requested_duration
                                                   : (requested_duration > 0
                                                          ? std::min(requested_duration,
                                                                     transformed_duration)
                                                          : transformed_duration);
        if (timeline_duration <= 0) {
            continue;
        }
        ClipState state;
        state.config = track;
        state.decoder = std::move(decoder);
        state.trim_start_frame = trim_start;
        state.trim_end_frame = trim_end;
        state.timeline_start_frame = configured_frames(
            track.timeline_start_sample, track.timeline_start_ms, sample_rate);
        state.timeline_start_frame = std::max<std::int64_t>(0,
                                                            state.timeline_start_frame);
        state.timeline_duration_frames = timeline_duration;
        state.uses_time_processor = std::abs(speed_ratio - 1.0) >= 0.0001
            || track.pitch_cents != 0;
        if (state.uses_time_processor) {
            state.processor = create_time_pitch_engine();
            const double speed_pitch = track.keep_pitch
                ? 0.0 : 1200.0 * std::log2(speed_ratio);
            if (state.processor == nullptr
                || !state.processor->configure(sample_rate, 2)
                || !state.processor->setTempoRatio(speed_ratio)
                || !state.processor->setPitchCents(
                    static_cast<double>(track.pitch_cents) + speed_pitch)) {
                error = "Unable to configure timeline time/pitch processing.";
                return AG_INVALID_ARGUMENT;
            }
            state.processor_output.reserve(
                static_cast<std::size_t>(kProcessorReceiveFrames) * 2U);
            state.processor_input.reserve(kProcessorFeedFrames * 2U);
            state.processor_receive.resize(
                static_cast<std::size_t>(kProcessorReceiveFrames) * 2U);
        }
        max_end_frame = std::max(max_end_frame,
                                 state.timeline_start_frame + timeline_duration);
        configured.push_back(std::move(state));
    }
    if (configured.empty()) {
        error = "Timeline contains no playable clips.";
        return AG_INVALID_ARGUMENT;
    }

    clips_ = std::move(configured);
    sample_rate_ = sample_rate;
    channels_ = channels;
    position_frames_ = 0;
    duration_frames_ = max_end_frame;
    at_end_ = false;
    return AG_OK;
}

bool TimelinePreviewMixer::source_frame_for(const ClipState& clip,
                                             const std::int64_t timeline_frame,
                                             std::int64_t& source_frame,
                                             std::int64_t& local_frame) const noexcept
{
    local_frame = timeline_frame - clip.timeline_start_frame;
    if (local_frame < 0 || local_frame >= clip.timeline_duration_frames) {
        return false;
    }
    if (clip.uses_time_processor) {
        source_frame = 0;
        return true;
    }
    const std::int64_t source_duration = clip.trim_end_frame - clip.trim_start_frame;
    if (source_duration <= 0) {
        return false;
    }
    if (clip.config.loop) {
        local_frame %= source_duration;
    } else if (local_frame >= source_duration) {
        return false;
    }
    source_frame = clip.trim_start_frame + local_frame;
    return true;
}

ag_result TimelinePreviewMixer::seek_clip(ClipState& clip,
                                           const std::int64_t source_frame,
                                           std::string& error)
{
    const std::int64_t target_ms = source_frame * 1000 / sample_rate_;
    const ag_result result = clip.decoder->seek(target_ms);
    if (result != AG_OK) {
        error = "Unable to seek a timeline clip.";
        return result;
    }
    clip.block = {};
    clip.block_offset = 0U;
    clip.next_source_frame = source_frame;
    return AG_OK;
}

ag_result TimelinePreviewMixer::sample_clip(ClipState& clip,
                                             const std::int64_t source_frame,
                                             float& left,
                                             float& right,
                                             std::string& error)
{
    if (clip.next_source_frame != source_frame) {
        const ag_result seek_result = seek_clip(clip, source_frame, error);
        if (seek_result != AG_OK) {
            return seek_result;
        }
    }
    while (clip.block_offset >= clip.block.frames) {
        const ag_result result = clip.decoder->read(clip.block);
        clip.block_offset = 0U;
        if (result != AG_OK) {
            error = "Unable to read a timeline clip.";
            return result;
        }
        if (clip.block.frames == 0U && clip.block.end_of_stream) {
            left = 0.0F;
            right = 0.0F;
            return AG_OK;
        }
    }
    const std::size_t sample_index = clip.block_offset * 2U;
    left = clip.block.samples[sample_index];
    right = clip.block.samples[sample_index + 1U];
    ++clip.block_offset;
    ++clip.next_source_frame;
    return AG_OK;
}

void TimelinePreviewMixer::reset_processor(ClipState& clip) noexcept
{
    if (clip.processor != nullptr) {
        clip.processor->reset();
    }
    clip.processor_output.clear();
    clip.processor_output_offset = 0U;
    clip.next_processed_frame = 0;
    clip.next_processor_source_frame = 0;
    clip.processor_flushed = false;
    clip.block = {};
    clip.block_offset = 0U;
    clip.next_source_frame = -1;
}

ag_result TimelinePreviewMixer::sample_processed_clip(
    ClipState& clip, const std::int64_t local_frame, float& left,
    float& right, std::string& error)
{
    if (clip.processor == nullptr || local_frame < 0) {
        error = "Timeline processor is unavailable.";
        return AG_INVALID_ARGUMENT;
    }
    if (local_frame < clip.next_processed_frame) {
        reset_processor(clip);
    }

    while (clip.next_processed_frame <= local_frame) {
        if (clip.processor_output_offset + 2U <= clip.processor_output.size()) {
            const std::size_t offset = clip.processor_output_offset;
            const float next_left = clip.processor_output[offset];
            const float next_right = clip.processor_output[offset + 1U];
            clip.processor_output_offset += 2U;
            if (clip.processor_output_offset == clip.processor_output.size()) {
                clip.processor_output.clear();
                clip.processor_output_offset = 0U;
            }
            if (clip.next_processed_frame == local_frame) {
                left = next_left;
                right = next_right;
            }
            ++clip.next_processed_frame;
            continue;
        }

        const std::size_t received = clip.processor->receive(
            clip.processor_receive.data(), kProcessorReceiveFrames);
        if (received > 0U) {
            clip.processor_output.assign(
                clip.processor_receive.cbegin(),
                clip.processor_receive.cbegin()
                    + static_cast<std::ptrdiff_t>(received) * 2);
            clip.processor_output_offset = 0U;
            continue;
        }

        clip.processor_input.clear();
        const std::int64_t source_duration =
            clip.trim_end_frame - clip.trim_start_frame;
        while (clip.processor_input.size() / 2U < kProcessorFeedFrames) {
            if (source_duration <= 0) {
                break;
            }
            if (!clip.config.loop
                && clip.next_processor_source_frame >= source_duration) {
                break;
            }
            const std::int64_t source_offset = clip.config.loop
                ? clip.next_processor_source_frame % source_duration
                : clip.next_processor_source_frame;
            float source_left = 0.0F;
            float source_right = 0.0F;
            const ag_result read_result = sample_clip(
                clip, clip.trim_start_frame + source_offset,
                source_left, source_right, error);
            if (read_result != AG_OK) {
                return read_result;
            }
            clip.processor_input.push_back(source_left);
            clip.processor_input.push_back(source_right);
            ++clip.next_processor_source_frame;
        }
        if (!clip.processor_input.empty()) {
            clip.processor->put(clip.processor_input.data(),
                                clip.processor_input.size() / 2U);
            continue;
        }
        if (!clip.processor_flushed) {
            clip.processor->flush();
            clip.processor_flushed = true;
            continue;
        }

        left = 0.0F;
        right = 0.0F;
        ++clip.next_processed_frame;
    }
    return AG_OK;
}

ag_result TimelinePreviewMixer::read(float* const output,
                                     const std::size_t frame_count,
                                     std::string& error)
{
    error.clear();
    if (output == nullptr || frame_count == 0U || clips_.empty()) {
        error = "Timeline preview is not configured.";
        return AG_INVALID_ARGUMENT;
    }
    std::fill_n(output, frame_count * static_cast<std::size_t>(channels_), 0.0F);
    for (std::size_t frame = 0U; frame < frame_count; ++frame) {
        const std::int64_t timeline_frame = position_frames_ + static_cast<std::int64_t>(frame);
        if (timeline_frame >= duration_frames_) {
            at_end_ = true;
            continue;
        }
        float mixed_left = 0.0F;
        float mixed_right = 0.0F;
        for (ClipState& clip : clips_) {
            std::int64_t local_frame = 0;
            std::int64_t source_frame = 0;
            if (!source_frame_for(clip, timeline_frame, source_frame, local_frame)) {
                continue;
            }
            float left = 0.0F;
            float right = 0.0F;
            const ag_result result = clip.uses_time_processor
                ? sample_processed_clip(clip, local_frame, left, right, error)
                : sample_clip(clip, source_frame, left, right, error);
            if (result != AG_OK) {
                return result;
            }
            const std::int64_t fade_in = configured_frames(
                clip.config.fade_in_samples, clip.config.fade_in_ms, sample_rate_);
            const std::int64_t fade_out = configured_frames(
                clip.config.fade_out_samples, clip.config.fade_out_ms, sample_rate_);
            float gain = static_cast<float>(clip.config.gain);
            if (fade_in > 0 && local_frame < fade_in) {
                gain *= fade_gain(clip.config.fade_in_curve,
                                  static_cast<float>(local_frame)
                                      / static_cast<float>(fade_in));
            }
            const std::int64_t from_end = clip.timeline_duration_frames - local_frame;
            if (fade_out > 0 && from_end <= fade_out) {
                gain *= fade_gain(clip.config.fade_out_curve,
                                  static_cast<float>(from_end)
                                      / static_cast<float>(fade_out));
            }
            const float pan = std::clamp(static_cast<float>(clip.config.pan), -1.0F, 1.0F);
            mixed_left += left * gain * (pan > 0.0F ? 1.0F - pan : 1.0F);
            mixed_right += right * gain * (pan < 0.0F ? 1.0F + pan : 1.0F);
        }
        if (channels_ == 1) {
            output[frame] = (mixed_left + mixed_right) * 0.5F;
        } else {
            output[frame * 2U] = mixed_left;
            output[frame * 2U + 1U] = mixed_right;
        }
    }
    position_frames_ = std::min<std::int64_t>(duration_frames_,
                                               position_frames_
                                                   + static_cast<std::int64_t>(frame_count));
    at_end_ = position_frames_ >= duration_frames_;
    return AG_OK;
}

ag_result TimelinePreviewMixer::seek(const std::int64_t position_ms,
                                     std::string& error)
{
    error.clear();
    if (position_ms < 0 || sample_rate_ <= 0) {
        error = "Invalid timeline seek position.";
        return AG_INVALID_ARGUMENT;
    }
    position_frames_ = std::min<std::int64_t>(duration_frames_,
                                               milliseconds_to_frames(position_ms,
                                                                      sample_rate_));
    for (ClipState& clip : clips_) {
        if (clip.uses_time_processor) {
            reset_processor(clip);
        } else {
            clip.block = {};
            clip.block_offset = 0U;
            clip.next_source_frame = -1;
        }
    }
    at_end_ = position_frames_ >= duration_frames_;
    return AG_OK;
}

std::int64_t TimelinePreviewMixer::position_ms() const noexcept
{
    return sample_rate_ <= 0 ? 0 : position_frames_ * 1000 / sample_rate_;
}

std::int64_t TimelinePreviewMixer::position_frames() const noexcept
{
    return position_frames_;
}

std::int64_t TimelinePreviewMixer::duration_ms() const noexcept
{
    return sample_rate_ <= 0 ? 0 : duration_frames_ * 1000 / sample_rate_;
}

bool TimelinePreviewMixer::at_end() const noexcept
{
    return at_end_;
}

int TimelinePreviewMixer::sample_rate() const noexcept
{
    return sample_rate_;
}

int TimelinePreviewMixer::channels() const noexcept
{
    return channels_;
}

} // namespace agplayer
