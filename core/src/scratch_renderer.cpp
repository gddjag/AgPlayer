#include "scratch_renderer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace agplayer {
namespace {

constexpr float half_pi = 1.57079632679489661923F;
constexpr float pi = 3.14159265358979323846F;
constexpr double smoothing_seconds = 0.035;
constexpr double fade_seconds = 0.008;

int direction_of(const float value) noexcept
{
    if (value > 0.0F) return 1;
    if (value < 0.0F) return -1;
    return 0;
}

} // namespace

ScratchRenderer::ScratchRenderer(const std::uint32_t sample_rate,
                                 const std::uint32_t channels)
    : sample_rate_(sample_rate)
    , channels_(channels)
{
    if (sample_rate == 0U || channels == 0U
        || channels > kMaximumChannels) {
        throw std::invalid_argument("invalid scratch renderer shape");
    }
    smoothing_alpha_ = static_cast<float>(1.0 - std::exp(
        -1.0 / (static_cast<double>(sample_rate) * smoothing_seconds)));
    fade_frames_ = std::max<std::size_t>(
        1U, static_cast<std::size_t>(std::llround(
                static_cast<double>(sample_rate) * fade_seconds)));
}

bool ScratchRenderer::begin(const double source_frame,
                            const std::int64_t source_frame_count,
                            const std::uint64_t generation,
                            const std::uint64_t epoch) noexcept
{
    cancel_immediately();
    if (!std::isfinite(source_frame) || source_frame_count <= 0) return false;

    source_frame_count_ = source_frame_count;
    source_frame_ = std::clamp(
        source_frame, 0.0, static_cast<double>(source_frame_count - 1));
    target_rate_ = 0.0F;
    smoothed_rate_ = 0.0F;
    gain_ = 0.0F;
    gain_fade_start_angle_ = 0.0F;
    gain_fade_target_angle_ = 0.0F;
    gain_fade_active_ = false;
    gain_fade_progress_ = 0U;
    active_ = true;
    cache_hit_ = false;
    exit_pending_ = false;
    exit_complete_ = false;
    has_last_sample_ = false;
    current_direction_ = 0;
    pending_direction_ = 0;
    direction_fade_progress_ = 0U;
    direction_fade_active_ = false;
    direction_midpoint_reset_ = false;
    rebase_request_valid_ = false;
    accepted_generation_ = generation;
    session_epoch_ = epoch;
    return true;
}

bool ScratchRenderer::request_exit() noexcept
{
    if (!active_ || exit_pending_) return false;
    exit_pending_ = true;
    if (gain_ <= 0.0F && !gain_fade_active_) {
        active_ = false;
        exit_pending_ = false;
        exit_complete_ = true;
        cache_hit_ = false;
        return true;
    }
    start_gain_fade(0.0F);
    return true;
}

void ScratchRenderer::cancel_immediately() noexcept
{
    active_ = false;
    cache_hit_ = false;
    exit_pending_ = false;
    exit_complete_ = true;
    gain_ = 0.0F;
    gain_fade_active_ = false;
    direction_fade_active_ = false;
    smoothed_rate_ = 0.0F;
    target_rate_ = 0.0F;
}

float ScratchRenderer::normalized_rate(const float rate) const noexcept
{
    if (!std::isfinite(rate) || std::abs(rate) < 0.001F) return 0.0F;
    return std::clamp(rate, -3.0F, 3.0F);
}

bool ScratchRenderer::identity_matches(
    const ScratchPcmWindow* const window,
    const ScratchCommand& command) const noexcept
{
    return window != nullptr
        && window->generation() == command.generation
        && window->epoch() == command.epoch
        && window->sample_rate() == sample_rate_
        && window->channels() == channels_
        && window->frame_count() > 0U;
}

void ScratchRenderer::start_gain_fade(const float target) noexcept
{
    const float clamped_target = std::clamp(target, 0.0F, 1.0F);
    const float target_angle = clamped_target * half_pi;
    if (std::abs(gain_fade_target_angle_ - target_angle) <= 1.0e-6F
        && (gain_fade_active_
            || std::abs(gain_ - clamped_target) <= 1.0e-6F)) {
        return;
    }
    if (std::abs(gain_ - clamped_target) <= 1.0e-6F) {
        gain_ = clamped_target;
        gain_fade_target_angle_ = target_angle;
        gain_fade_active_ = false;
        return;
    }
    gain_fade_start_angle_ = std::asin(std::clamp(gain_, 0.0F, 1.0F));
    gain_fade_target_angle_ = target_angle;
    gain_fade_progress_ = 0U;
    gain_fade_active_ = true;
}

float ScratchRenderer::advance_gain() noexcept
{
    if (!gain_fade_active_) return gain_;

    ++gain_fade_progress_;
    const float progress = std::min(
        1.0F, static_cast<float>(gain_fade_progress_)
            / static_cast<float>(fade_frames_));
    gain_ = std::sin(gain_fade_start_angle_
                     + (gain_fade_target_angle_ - gain_fade_start_angle_)
                         * progress);
    if (gain_fade_progress_ >= fade_frames_) {
        gain_ = gain_fade_target_angle_ == 0.0F ? 0.0F : 1.0F;
        gain_fade_active_ = false;
        if (exit_pending_ && gain_ == 0.0F) {
            active_ = false;
            exit_pending_ = false;
            exit_complete_ = true;
            cache_hit_ = false;
        }
    }
    return gain_;
}

void ScratchRenderer::update_direction(const float target_rate) noexcept
{
    const int direction = direction_of(target_rate);
    if (direction == 0) return;
    if (current_direction_ == 0) {
        current_direction_ = direction;
        pending_direction_ = direction;
        return;
    }
    if (direction_fade_active_) {
        if (direction != pending_direction_) {
            pending_direction_ = direction;
            direction_fade_progress_ = 0U;
            direction_midpoint_reset_ = false;
        }
        return;
    }
    if (direction != current_direction_) {
        pending_direction_ = direction;
        direction_fade_progress_ = 0U;
        direction_fade_active_ = true;
        direction_midpoint_reset_ = false;
    }
}

float ScratchRenderer::advance_direction_gain(
    bool& midpoint_crossed) noexcept
{
    midpoint_crossed = false;
    if (!direction_fade_active_) return 1.0F;

    ++direction_fade_progress_;
    const float progress = std::min(
        1.0F, static_cast<float>(direction_fade_progress_)
            / static_cast<float>(fade_frames_));
    if (!direction_midpoint_reset_ && progress >= 0.5F) {
        smoothed_rate_ = 0.0F;
        direction_midpoint_reset_ = true;
        midpoint_crossed = true;
    }
    const float gain = progress <= 0.5F
        ? std::cos(pi * progress)
        : std::sin(pi * (progress - 0.5F));
    if (direction_fade_progress_ >= fade_frames_) {
        current_direction_ = pending_direction_;
        direction_fade_active_ = false;
        direction_midpoint_reset_ = false;
    }
    return std::clamp(gain, 0.0F, 1.0F);
}

ScratchRenderStatus ScratchRenderer::status(
    const bool rebase_requested) const noexcept
{
    return {
        source_frame_,
        smoothed_rate_,
        active_,
        active_ && cache_hit_,
        active_ && !exit_pending_ && !cache_hit_,
        rebase_requested,
        exit_complete_,
    };
}

ScratchRenderStatus ScratchRenderer::render(
    const ScratchPcmWindow* const window,
    const ScratchCommand& command,
    float* const interleaved_output,
    const std::size_t frame_count) noexcept
{
    if (frame_count == 0U) return status(false);
    if (interleaved_output == nullptr
        || frame_count > std::numeric_limits<std::size_t>::max()
            / static_cast<std::size_t>(channels_)) {
        return status(false);
    }

    const std::size_t sample_count =
        frame_count * static_cast<std::size_t>(channels_);
    std::fill_n(interleaved_output, sample_count, 0.0F);
    if (!active_) return status(false);

    const bool command_matches_session = command.epoch == session_epoch_
        && command.generation >= accepted_generation_;
    if (command_matches_session
        && command.generation > accepted_generation_) {
        accepted_generation_ = command.generation;
    }
    if (command_matches_session && !command.active) (void)request_exit();
    target_rate_ = command_matches_session
        ? normalized_rate(command.signed_rate)
        : 0.0F;
    if (target_rate_ == 0.0F) smoothed_rate_ = 0.0F;
    if (!exit_pending_) update_direction(target_rate_);
    const bool matching_identity = command_matches_session
        && identity_matches(window, command);
    bool rebase_requested = false;
    std::array<float, kMaximumChannels> frame_samples{};

    for (std::size_t frame = 0U; frame < frame_count && active_; ++frame) {
        bool direction_midpoint_crossed = false;
        const float direction_gain = advance_direction_gain(
            direction_midpoint_crossed);
        float next_smoothed_rate = smoothed_rate_
            + smoothing_alpha_ * (target_rate_ - smoothed_rate_);
        if (target_rate_ == 0.0F && std::abs(next_smoothed_rate) < 1.0e-5F) {
            next_smoothed_rate = 0.0F;
        }
        const double next_source_frame = std::clamp(
            source_frame_ + static_cast<double>(next_smoothed_rate),
            0.0, static_cast<double>(source_frame_count_ - 1));
        const bool can_move = next_source_frame != source_frame_;

        bool hit = matching_identity;
        if (hit) {
            for (std::size_t channel = 0U; channel < channels_; ++channel) {
                if (!window->read_linear(source_frame_, channel,
                                         frame_samples[channel])) {
                    hit = false;
                    break;
                }
            }
        }

        if (hit != cache_hit_) cache_hit_ = hit;
        if (hit) {
            // An initial async fill can legitimately miss before its matching
            // bank is published. Once that bank is observed, re-arm the latch
            // so a later out-of-window miss in the same generation can ask
            // for exactly one new rebase.
            rebase_request_valid_ = false;
        }
        if (!exit_pending_) {
            const bool should_sound = hit && command.active
                && can_move;
            if (should_sound && direction_midpoint_crossed) {
                // Reuse the direction envelope after its midpoint rather than
                // stacking a second 8 ms fade. Odd fade lengths do not have a
                // sample whose floating-point direction gain is exactly zero.
                gain_ = 1.0F;
                gain_fade_start_angle_ = half_pi;
                gain_fade_target_angle_ = half_pi;
                gain_fade_progress_ = 0U;
                gain_fade_active_ = false;
            } else {
                start_gain_fade(should_sound ? 1.0F : 0.0F);
            }
        }
        if (!hit && command_matches_session && command.active && !exit_pending_
            && (!rebase_request_valid_
                || rebase_generation_ != command.generation
                || rebase_epoch_ != command.epoch)) {
            rebase_request_valid_ = true;
            rebase_generation_ = command.generation;
            rebase_epoch_ = command.epoch;
            rebase_requested = true;
        }

        if (hit) {
            for (std::size_t channel = 0U; channel < channels_; ++channel) {
                last_sample_[channel] = frame_samples[channel];
            }
            has_last_sample_ = true;
        }

        const float gain = advance_gain() * direction_gain;
        const std::size_t output_offset =
            frame * static_cast<std::size_t>(channels_);
        if (hit || has_last_sample_) {
            const auto& source = hit ? frame_samples : last_sample_;
            for (std::size_t channel = 0U; channel < channels_; ++channel) {
                interleaved_output[output_offset + channel]
                    = source[channel] * gain;
            }
        }

        if (!active_) break;
        smoothed_rate_ = next_smoothed_rate;
        source_frame_ = next_source_frame;
    }

    return status(rebase_requested);
}

} // namespace agplayer
