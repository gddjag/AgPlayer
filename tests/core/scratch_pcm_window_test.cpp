// Test files deliberately keep assert() active even in Release builds.
#undef NDEBUG

#include "scratch_pcm_window.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <type_traits>

static_assert(!std::is_copy_constructible_v<agplayer::ScratchPcmWindow>);
static_assert(!std::is_copy_assignable_v<agplayer::ScratchPcmWindow>);
static_assert(!std::is_move_constructible_v<agplayer::ScratchPcmWindow>);
static_assert(!std::is_move_assignable_v<agplayer::ScratchPcmWindow>);

namespace {

bool near(const float actual, const float expected) noexcept
{
    return std::abs(actual - expected) < 1.0e-6F;
}

} // namespace

int main()
{
    using agplayer::ScratchPcmWindow;

    const ScratchPcmWindow normal(48'000U, 2U);
    assert(normal.sample_rate() == 48'000U);
    assert(normal.channels() == 2U);
    assert(normal.capacity_frames() == 48'000U * 16U);
    assert(normal.payload_bytes()
           <= ScratchPcmWindow::default_bank_payload_budget());

    const ScratchPcmWindow high_rate(384'000U, 8U);
    assert(high_rate.capacity_frames() < 384'000U * 16U);
    assert(normal.payload_bytes() + high_rate.payload_bytes()
           <= ScratchPcmWindow::kTotalPayloadLimitBytes);
    const ScratchPcmWindow requested_oversized_bank(
        384'000U, 8U, 8.0, ScratchPcmWindow::kTotalPayloadLimitBytes);
    assert(requested_oversized_bank.payload_bytes()
           <= ScratchPcmWindow::default_bank_payload_budget());
    assert(requested_oversized_bank.payload_bytes() * 2U
           <= ScratchPcmWindow::kTotalPayloadLimitBytes);

    const auto centered = normal.planned_range(500'000, 1'000'000);
    assert(centered.first_frame == 116'000);
    assert(centered.frame_count == normal.capacity_frames());
    const auto near_start = normal.planned_range(12, 1'000'000);
    assert(near_start.first_frame == 0);
    assert(near_start.frame_count == normal.capacity_frames());
    const auto near_end = normal.planned_range(999'999, 1'000'000);
    assert(near_end.first_frame == 232'000);
    assert(near_end.frame_count == normal.capacity_frames());
    const auto short_track = normal.planned_range(50, 100);
    assert(short_track.first_frame == 0);
    assert(short_track.frame_count == 100U);
    const auto empty_track = normal.planned_range(0, 0);
    assert(empty_track.first_frame == 0);
    assert(empty_track.frame_count == 0U);

    ScratchPcmWindow tiny(10U, 2U, 0.2);
    const std::array<float, 8> pcm{
        0.0F, 10.0F,
        2.0F, 12.0F,
        4.0F, 14.0F,
        6.0F, 16.0F,
    };
    assert(tiny.assign(20, pcm.data(), 4U, 7U, 11U));
    assert(tiny.first_frame() == 20);
    assert(tiny.frame_count() == 4U);
    assert(tiny.generation() == 7U);
    assert(tiny.epoch() == 11U);

    float sample = -1.0F;
    assert(tiny.read_linear(20.5, 0U, sample));
    assert(near(sample, 1.0F));
    assert(tiny.read_linear(21.25, 1U, sample));
    assert(near(sample, 12.5F));
    assert(tiny.read_linear(23.0, 0U, sample));
    assert(near(sample, 6.0F));
    assert(!tiny.read_linear(19.999, 0U, sample));
    assert(!tiny.read_linear(23.25, 0U, sample));
    assert(!tiny.read_linear(20.0, 2U, sample));

    const auto preserved_generation = tiny.generation();
    assert(!tiny.assign(-1, pcm.data(), 4U, 8U, 12U));
    assert(!tiny.assign(20, nullptr, 4U, 8U, 12U));
    assert(!tiny.assign(20, pcm.data(), tiny.capacity_frames() + 1U,
                        8U, 12U));
    assert(!tiny.assign(std::numeric_limits<std::int64_t>::max() - 1,
                        pcm.data(), 4U, 8U, 12U));
    assert(tiny.generation() == preserved_generation);

    ScratchPcmWindow direct_fill(10U, 2U, 0.2);
    assert(direct_fill.capacity_samples()
           == direct_fill.capacity_frames() * direct_fill.channels());
    assert(direct_fill.payload_bytes()
           == direct_fill.capacity_samples() * sizeof(float));
    assert(direct_fill.writable_data() != nullptr);
    std::copy(pcm.begin(), pcm.end(), direct_fill.writable_data());
    assert(direct_fill.publish(30, 4U, 9U, 13U));
    assert(direct_fill.first_frame() == 30);
    assert(direct_fill.frame_count() == 4U);
    assert(direct_fill.generation() == 9U);
    assert(direct_fill.epoch() == 13U);
    assert(direct_fill.read_linear(30.5, 0U, sample));
    assert(near(sample, 1.0F));
    assert(direct_fill.read_linear(30.5, 1U, sample));
    assert(near(sample, 11.0F));

    assert(!direct_fill.publish(-1, 4U, 10U, 14U));
    assert(!direct_fill.publish(30, direct_fill.capacity_frames() + 1U,
                                10U, 14U));
    assert(!direct_fill.publish(std::numeric_limits<std::int64_t>::max() - 1,
                                4U, 10U, 14U));
    assert(direct_fill.first_frame() == 30);
    assert(direct_fill.frame_count() == 4U);
    assert(direct_fill.generation() == 9U);
    assert(direct_fill.epoch() == 13U);
    assert(direct_fill.assign(40, pcm.data(), 4U, 10U, 14U));
    assert(direct_fill.first_frame() == 40);
    assert(direct_fill.generation() == 10U);
    assert(direct_fill.epoch() == 14U);

    bool rejected_shape = false;
    try {
        const ScratchPcmWindow invalid(0U, 2U);
    } catch (const std::invalid_argument&) {
        rejected_shape = true;
    }
    assert(rejected_shape);

    bool rejected_budget = false;
    try {
        const ScratchPcmWindow invalid(48'000U, 2U, 8.0, 4U);
    } catch (const std::length_error&) {
        rejected_budget = true;
    }
    assert(rejected_budget);
}
