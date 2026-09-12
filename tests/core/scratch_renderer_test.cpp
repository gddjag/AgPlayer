// Test files deliberately keep assert() active even in Release builds.
#undef NDEBUG

#include "scratch_renderer.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <new>
#include <utility>
#include <vector>

namespace {

std::atomic<bool> record_allocations{false};
std::atomic<std::size_t> allocation_count{0U};
std::atomic<std::size_t> deletion_count{0U};

void* allocate(const std::size_t size)
{
    if (record_allocations.load(std::memory_order_relaxed)) {
        allocation_count.fetch_add(1U, std::memory_order_relaxed);
    }
    if (void* const pointer = std::malloc(size == 0U ? 1U : size)) {
        return pointer;
    }
    throw std::bad_alloc();
}

void release(void* const pointer) noexcept
{
    if (record_allocations.load(std::memory_order_relaxed)) {
        deletion_count.fetch_add(1U, std::memory_order_relaxed);
    }
    std::free(pointer);
}

bool near(const float actual, const float expected,
          const float tolerance = 1.0e-3F) noexcept
{
    return std::abs(actual - expected) <= tolerance;
}

} // namespace

void* operator new(const std::size_t size) { return allocate(size); }
void* operator new[](const std::size_t size) { return allocate(size); }
void operator delete(void* const pointer) noexcept { release(pointer); }
void operator delete[](void* const pointer) noexcept { release(pointer); }
void operator delete(void* const pointer, std::size_t) noexcept
{
    release(pointer);
}
void operator delete[](void* const pointer, std::size_t) noexcept
{
    release(pointer);
}

int main()
{
    using agplayer::ScratchCommand;
    using agplayer::ScratchPcmWindow;
    using agplayer::ScratchRenderer;

    static_assert(noexcept(std::declval<ScratchRenderer&>().render(
        nullptr, std::declval<const ScratchCommand&>(), nullptr, 0U)));

    constexpr std::uint32_t sample_rate = 1'000U;
    constexpr std::int64_t track_frames = 2'000;
    const ScratchCommand stopped{1U, 9U, true, 0.0F};
    ScratchCommand forward = stopped;
    forward.signed_rate = 1.0F;

    ScratchPcmWindow constant_window(sample_rate, 1U);
    std::vector<float> constant_pcm(static_cast<std::size_t>(track_frames),
                                    1.0F);
    assert(constant_window.assign(0, constant_pcm.data(), constant_pcm.size(),
                                  stopped.generation, stopped.epoch));

    ScratchRenderer entry(sample_rate, 1U);
    assert(entry.begin(100.0, track_frames,
                       forward.generation, forward.epoch));
    std::array<float, 8> entry_output{};
    auto status = entry.render(&constant_window, forward,
                               entry_output.data(), entry_output.size());
    assert(status.active && status.ready && !status.buffering);
    assert(near(entry_output[0], 0.19509F, 0.02F));
    assert(near(entry_output[3], 0.707107F, 0.02F));
    assert(near(entry_output[7], 1.0F, 0.001F));
    assert(std::is_sorted(entry_output.begin(), entry_output.end()));

    ScratchPcmWindow ramp_window(sample_rate, 1U);
    std::vector<float> ramp_pcm(static_cast<std::size_t>(track_frames));
    for (std::size_t frame = 0U; frame < ramp_pcm.size(); ++frame) {
        ramp_pcm[frame] = static_cast<float>(frame);
    }
    assert(ramp_window.assign(0, ramp_pcm.data(), ramp_pcm.size(),
                              stopped.generation, stopped.epoch));

    ScratchRenderer interpolation(sample_rate, 1U);
    assert(interpolation.begin(20.5, track_frames,
                               forward.generation, forward.epoch));
    std::array<float, 8> interpolated{};
    status = interpolation.render(&ramp_window, forward,
                                  interpolated.data(), interpolated.size());
    assert(near(interpolated.front(), 20.5F * 0.19509F, 0.02F));
    assert(status.source_frame > 20.5);

    ScratchPcmWindow stereo_window(sample_rate, 2U, 0.01);
    const std::array<float, 8> stereo_pcm{
        0.0F, 10.0F,
        2.0F, 12.0F,
        4.0F, 14.0F,
        6.0F, 16.0F,
    };
    const ScratchCommand stereo_forward{5U, 6U, true, 1.0F};
    assert(stereo_window.assign(20, stereo_pcm.data(), 4U,
                                stereo_forward.generation,
                                stereo_forward.epoch));
    ScratchRenderer stereo_interpolation(sample_rate, 2U);
    assert(stereo_interpolation.begin(20.5, 100,
                                      stereo_forward.generation,
                                      stereo_forward.epoch));
    std::array<float, 16> stereo_output{};
    status = stereo_interpolation.render(&stereo_window, stereo_forward,
                                         stereo_output.data(), 8U);
    assert(near(stereo_output[0], 1.0F * 0.19509F, 0.02F));
    assert(near(stereo_output[1], 11.0F * 0.19509F, 0.02F));

    ScratchRenderer movement(sample_rate, 1U);
    assert(movement.begin(100.0, track_frames,
                          forward.generation, forward.epoch));
    std::array<float, 400> movement_output{};
    status = movement.render(&ramp_window, forward,
                             movement_output.data(), movement_output.size());
    assert(status.source_frame > 450.0);
    assert(status.smoothed_rate > 0.99F && status.smoothed_rate <= 1.0F);
    const double forward_position = status.source_frame;

    ScratchCommand reverse = forward;
    reverse.signed_rate = -1.0F;
    status = movement.render(&ramp_window, reverse,
                             movement_output.data(), 200U);
    assert(status.source_frame < forward_position);
    assert(status.smoothed_rate < -0.98F);

    ScratchRenderer stopped_needle(sample_rate, 1U);
    assert(stopped_needle.begin(500.0, track_frames,
                                forward.generation, forward.epoch));
    status = stopped_needle.render(&constant_window, forward,
                                   movement_output.data(), 300U);
    const double moving_position = status.source_frame;
    ScratchCommand zero = forward;
    zero.signed_rate = 0.0005F;
    std::array<float, 8> stop_fade_output{};
    status = stopped_needle.render(&constant_window, zero,
                                   stop_fade_output.data(),
                                   stop_fade_output.size());
    const double stopped_position = status.source_frame;
    assert(status.smoothed_rate == 0.0F);
    assert(stopped_position == moving_position);
    assert(stop_fade_output.front() > stop_fade_output.back());
    assert(near(stop_fade_output.back(), 0.0F));
    std::array<float, 32> stopped_output{};
    status = stopped_needle.render(&constant_window, stopped,
                                   stopped_output.data(), stopped_output.size());
    assert(status.smoothed_rate == 0.0F);
    assert(status.source_frame == stopped_position);
    assert(std::all_of(stopped_output.begin(), stopped_output.end(),
                       [](const float sample) { return sample == 0.0F; }));
    std::array<float, 8> restart_output{};
    status = stopped_needle.render(&constant_window, forward,
                                   restart_output.data(), restart_output.size());
    assert(restart_output.front() < restart_output.back());
    assert(near(restart_output.back(), 1.0F));

    ScratchRenderer smoothing(sample_rate, 1U);
    assert(smoothing.begin(100.0, track_frames,
                           forward.generation, forward.epoch));
    float one_sample{};
    status = smoothing.render(&ramp_window, forward, &one_sample, 1U);
    assert(status.smoothed_rate > 0.0F && status.smoothed_rate < 0.05F);
    std::array<float, 34> smoothing_output{};
    status = smoothing.render(&ramp_window, forward,
                              smoothing_output.data(), smoothing_output.size());
    assert(status.smoothed_rate > 0.60F && status.smoothed_rate < 0.66F);

    ScratchRenderer clamped(sample_rate, 1U);
    assert(clamped.begin(500.0, track_frames,
                         forward.generation, forward.epoch));
    ScratchCommand too_fast = forward;
    too_fast.signed_rate = 30.0F;
    status = clamped.render(&ramp_window, too_fast,
                            movement_output.data(), movement_output.size());
    assert(status.smoothed_rate > 2.99F && status.smoothed_rate <= 3.0F);
    ScratchCommand too_fast_reverse = too_fast;
    too_fast_reverse.signed_rate = -30.0F;
    status = clamped.render(&ramp_window, too_fast_reverse,
                            movement_output.data(), movement_output.size());
    assert(status.smoothed_rate < -2.99F && status.smoothed_rate >= -3.0F);

    ScratchRenderer start_bound(sample_rate, 1U);
    assert(start_bound.begin(20.0, track_frames,
                             reverse.generation, reverse.epoch));
    status = start_bound.render(&constant_window, reverse,
                                movement_output.data(), 200U);
    assert(status.source_frame == 0.0);
    assert(near(movement_output[199], 0.0F));
    std::array<float, 8> leave_start_output{};
    status = start_bound.render(&constant_window, forward,
                                leave_start_output.data(),
                                leave_start_output.size());
    assert(status.source_frame > 0.0);
    assert(std::all_of(leave_start_output.begin(),
                       leave_start_output.begin() + 4,
                       [](const float sample) { return sample == 0.0F; }));
    assert(leave_start_output.back() > 0.95F);

    ScratchRenderer end_bound(sample_rate, 1U);
    assert(end_bound.begin(static_cast<double>(track_frames - 21), track_frames,
                           forward.generation, forward.epoch));
    status = end_bound.render(&constant_window, forward,
                              movement_output.data(), 200U);
    assert(status.source_frame == static_cast<double>(track_frames - 1));
    assert(near(movement_output[199], 0.0F));
    std::array<float, 8> leave_end_output{};
    status = end_bound.render(&constant_window, reverse,
                              leave_end_output.data(), leave_end_output.size());
    assert(status.source_frame < static_cast<double>(track_frames - 1));
    assert(std::all_of(leave_end_output.begin(),
                       leave_end_output.begin() + 4,
                       [](const float sample) { return sample == 0.0F; }));
    assert(leave_end_output.back() > 0.95F);

    // 44.1 kHz produces an odd 353-frame fade. Leaving a clamped boundary
    // must reuse the direction fade after it crosses the midpoint; a float
    // equality check cannot observe an exact zero-crossing in this case.
    constexpr std::uint32_t cd_sample_rate = 44'100U;
    constexpr std::size_t cd_fade_frames = 353U;
    constexpr std::int64_t cd_track_frames = 4'000;
    ScratchPcmWindow cd_window(cd_sample_rate, 1U, 0.1);
    std::vector<float> cd_pcm(static_cast<std::size_t>(cd_track_frames), 1.0F);
    const ScratchCommand cd_reverse{12U, 13U, true, -1.0F};
    assert(cd_window.assign(0, cd_pcm.data(), cd_pcm.size(),
                            cd_reverse.generation, cd_reverse.epoch));
    ScratchRenderer cd_boundary(cd_sample_rate, 1U);
    assert(cd_boundary.begin(20.0, cd_track_frames,
                             cd_reverse.generation, cd_reverse.epoch));
    std::array<float, 2'000> cd_to_boundary{};
    status = cd_boundary.render(&cd_window, cd_reverse,
                                cd_to_boundary.data(), cd_to_boundary.size());
    assert(status.source_frame == 0.0);
    ScratchCommand cd_leave = cd_reverse;
    cd_leave.signed_rate = 1.0F;
    std::array<float, cd_fade_frames> cd_leave_output{};
    status = cd_boundary.render(&cd_window, cd_leave,
                                cd_leave_output.data(), cd_leave_output.size());
    assert(status.source_frame > 0.0);
    assert(std::all_of(cd_leave_output.begin(),
                       cd_leave_output.begin() + cd_fade_frames / 2U,
                       [](const float sample) { return sample == 0.0F; }));
    assert(cd_leave_output.back() > 0.95F);

    ScratchRenderer direction_fade(sample_rate, 1U);
    assert(direction_fade.begin(500.0, track_frames,
                                forward.generation, forward.epoch));
    (void)direction_fade.render(&constant_window, forward,
                                movement_output.data(), movement_output.size());
    std::array<float, 8> direction_output{};
    status = direction_fade.render(&constant_window, reverse,
                                   direction_output.data(), direction_output.size());
    assert(direction_output.front() > 0.8F);
    assert(*std::min_element(direction_output.begin(), direction_output.end())
           < 0.01F);
    assert(direction_output.back() > 0.95F);

    ScratchRenderer miss(sample_rate, 1U);
    assert(miss.begin(500.0, track_frames,
                      forward.generation, forward.epoch));
    (void)miss.render(&constant_window, forward,
                      movement_output.data(), movement_output.size());
    ScratchCommand stale_generation = forward;
    stale_generation.generation = 2U;
    std::array<float, 8> miss_output{};
    status = miss.render(&constant_window, stale_generation,
                         miss_output.data(), miss_output.size());
    assert(!status.ready && status.buffering && status.rebase_requested);
    assert(miss_output.front() > miss_output.back());
    assert(near(miss_output.back(), 0.0F));
    status = miss.render(&constant_window, stale_generation,
                         miss_output.data(), miss_output.size());
    assert(status.buffering && !status.rebase_requested);
    const double position_during_miss = status.source_frame;
    stale_generation.signed_rate = 1.0F;
    status = miss.render(&constant_window, stale_generation,
                         movement_output.data(), movement_output.size());
    assert(status.source_frame > position_during_miss);

    assert(constant_window.assign(0, constant_pcm.data(), constant_pcm.size(),
                                  stale_generation.generation,
                                  stale_generation.epoch));
    status = miss.render(&constant_window, stale_generation,
                         miss_output.data(), miss_output.size());
    assert(status.ready && !status.buffering && !status.rebase_requested);
    assert(miss_output.front() < miss_output.back());

    ScratchPcmWindow later_window(sample_rate, 1U, 0.01);
    std::array<float, 16> later_pcm{};
    assert(later_window.assign(2'000, later_pcm.data(), later_pcm.size(),
                               stale_generation.generation,
                               stale_generation.epoch));
    status = miss.render(&later_window, stale_generation,
                         miss_output.data(), miss_output.size());
    assert(!status.ready && status.buffering && status.rebase_requested);
    status = miss.render(&later_window, stale_generation,
                         miss_output.data(), miss_output.size());
    assert(!status.rebase_requested);

    ScratchCommand stale_epoch = stale_generation;
    ++stale_epoch.epoch;
    status = miss.render(&constant_window, stale_epoch,
                         miss_output.data(), miss_output.size());
    assert(!status.ready && status.buffering && !status.rebase_requested);
    status = miss.render(&constant_window, stale_epoch,
                         miss_output.data(), miss_output.size());
    assert(!status.rebase_requested);

    ScratchCommand next_generation = stale_generation;
    ++next_generation.generation;
    status = miss.render(&constant_window, next_generation,
                         miss_output.data(), miss_output.size());
    assert(!status.ready && status.buffering && status.rebase_requested);
    status = miss.render(&constant_window, next_generation,
                         miss_output.data(), miss_output.size());
    assert(!status.rebase_requested);

    ScratchRenderer session_bound(sample_rate, 1U);
    assert(session_bound.begin(100.0, track_frames, 20U, 30U));
    status = session_bound.render(&constant_window, stale_generation,
                                  miss_output.data(), miss_output.size());
    assert(!status.ready && status.buffering && !status.rebase_requested);
    ScratchPcmWindow session_window(sample_rate, 1U);
    assert(session_window.assign(0, constant_pcm.data(), constant_pcm.size(),
                                 20U, 30U));
    const ScratchCommand session_command{20U, 30U, true, 1.0F};
    status = session_bound.render(&session_window, session_command,
                                  miss_output.data(), miss_output.size());
    assert(status.ready && !status.buffering);
    ScratchCommand newer_session_generation = session_command;
    ++newer_session_generation.generation;
    status = session_bound.render(&session_window, newer_session_generation,
                                  miss_output.data(), miss_output.size());
    assert(!status.ready && status.buffering && status.rebase_requested);
    status = session_bound.render(&session_window, session_command,
                                  miss_output.data(), miss_output.size());
    assert(!status.ready && status.buffering && !status.rebase_requested);
    ScratchCommand stale_inactive = stale_generation;
    stale_inactive.active = false;
    status = session_bound.render(&constant_window, stale_inactive,
                                  miss_output.data(), miss_output.size());
    assert(status.active && !status.ready && status.buffering
           && !status.exit_complete && !status.rebase_requested);

    ScratchPcmWindow narrow(sample_rate, 1U, 0.01);
    std::array<float, 20> narrow_pcm{};
    assert(narrow.assign(0, narrow_pcm.data(), narrow_pcm.size(), 4U, 12U));
    const ScratchCommand matching_but_outside{4U, 12U, true, 0.0F};
    ScratchRenderer outside_matching_window(sample_rate, 1U);
    assert(outside_matching_window.begin(50.0, 100,
                                         matching_but_outside.generation,
                                         matching_but_outside.epoch));
    status = outside_matching_window.render(
        &narrow, matching_but_outside, miss_output.data(), miss_output.size());
    assert(!status.ready && status.buffering && status.rebase_requested);

    ScratchRenderer exiting(sample_rate, 1U);
    assert(exiting.begin(100.0, track_frames,
                         stale_generation.generation,
                         stale_generation.epoch));
    (void)exiting.render(&constant_window, stale_generation,
                         movement_output.data(), movement_output.size());
    assert(exiting.request_exit());
    assert(!exiting.request_exit());
    std::array<float, 8> exit_output{};
    status = exiting.render(&constant_window, stale_generation,
                            exit_output.data(), exit_output.size());
    assert(exit_output.front() > exit_output.back());
    assert(near(exit_output.back(), 0.0F));
    assert(!status.active && status.exit_complete);

    ScratchRenderer inactive_command(sample_rate, 1U);
    assert(inactive_command.begin(100.0, track_frames,
                                  stale_generation.generation,
                                  stale_generation.epoch));
    (void)inactive_command.render(&constant_window, stale_generation,
                                  movement_output.data(), movement_output.size());
    ScratchCommand inactive = stale_generation;
    inactive.active = false;
    status = inactive_command.render(&constant_window, inactive,
                                     exit_output.data(), exit_output.size());
    assert(!status.active && status.exit_complete);
    assert(near(exit_output.back(), 0.0F));

    exiting.cancel_immediately();
    status = exiting.render(&constant_window, stale_generation,
                            exit_output.data(), exit_output.size());
    assert(!status.active);
    assert(std::all_of(exit_output.begin(), exit_output.end(),
                       [](const float sample) { return sample == 0.0F; }));

    ScratchRenderer realtime(sample_rate, 1U);
    assert(realtime.begin(100.0, track_frames,
                          stale_generation.generation,
                          stale_generation.epoch));
    (void)realtime.render(&constant_window, stale_generation,
                          movement_output.data(), movement_output.size());
    std::array<float, 64> realtime_output{};
    const std::size_t allocations_before = allocation_count.load();
    const std::size_t deletions_before = deletion_count.load();
    record_allocations.store(true, std::memory_order_release);
    status = realtime.render(&constant_window, stale_generation,
                             realtime_output.data(), realtime_output.size());
    record_allocations.store(false, std::memory_order_release);
    assert(status.active);
    assert(allocation_count.load() == allocations_before);
    assert(deletion_count.load() == deletions_before);
}
