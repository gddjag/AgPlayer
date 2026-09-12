#pragma once

#include "scratch_command_mailbox.hpp"
#include "scratch_pcm_window.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace agplayer {

struct ScratchRenderStatus final {
    double source_frame{};
    float smoothed_rate{};
    bool active{};
    bool ready{};
    bool buffering{};
    bool rebase_requested{};
    bool exit_complete{};
};

class ScratchRenderer final {
public:
    static constexpr std::uint32_t kMaximumChannels = 32U;

    ScratchRenderer(std::uint32_t sample_rate, std::uint32_t channels);

    [[nodiscard]] bool begin(double source_frame,
                             std::int64_t source_frame_count,
                             std::uint64_t generation,
                             std::uint64_t epoch) noexcept;
    [[nodiscard]] bool request_exit() noexcept;
    void cancel_immediately() noexcept;

    [[nodiscard]] ScratchRenderStatus render(
        const ScratchPcmWindow* window,
        const ScratchCommand& command,
        float* interleaved_output,
        std::size_t frame_count) noexcept;

private:
    [[nodiscard]] float normalized_rate(float rate) const noexcept;
    [[nodiscard]] bool identity_matches(
        const ScratchPcmWindow* window,
        const ScratchCommand& command) const noexcept;
    void start_gain_fade(float target) noexcept;
    [[nodiscard]] float advance_gain() noexcept;
    void update_direction(float target_rate) noexcept;
    [[nodiscard]] float advance_direction_gain(
        bool& midpoint_crossed) noexcept;
    [[nodiscard]] ScratchRenderStatus status(bool rebase_requested) const noexcept;

    std::uint32_t sample_rate_{};
    std::uint32_t channels_{};
    std::size_t fade_frames_{};
    float smoothing_alpha_{};
    double source_frame_{};
    std::int64_t source_frame_count_{};
    float target_rate_{};
    float smoothed_rate_{};
    float gain_{};
    float gain_fade_start_angle_{};
    float gain_fade_target_angle_{};
    std::size_t gain_fade_progress_{};
    bool gain_fade_active_{};
    bool active_{};
    bool cache_hit_{};
    bool exit_pending_{};
    bool exit_complete_{};
    bool has_last_sample_{};
    std::array<float, kMaximumChannels> last_sample_{};
    int current_direction_{};
    int pending_direction_{};
    std::size_t direction_fade_progress_{};
    bool direction_fade_active_{};
    bool direction_midpoint_reset_{};
    bool rebase_request_valid_{};
    std::uint64_t rebase_generation_{};
    std::uint64_t rebase_epoch_{};
    std::uint64_t accepted_generation_{};
    std::uint64_t session_epoch_{};
};

} // namespace agplayer
