#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace agplayer {

struct ScratchWindowRange final {
    std::int64_t first_frame{};
    std::size_t frame_count{};
};

class ScratchPcmWindow final {
public:
    static constexpr std::size_t kTotalPayloadLimitBytes =
        12U * 1024U * 1024U;
    static constexpr std::size_t kDefaultBankCount = 2U;

    [[nodiscard]] static constexpr std::size_t
    default_bank_payload_budget() noexcept
    {
        return kTotalPayloadLimitBytes / kDefaultBankCount;
    }

    ScratchPcmWindow(
        std::uint32_t sample_rate,
        std::uint32_t channels,
        double desired_half_span_seconds = 8.0,
        std::size_t payload_budget_bytes = default_bank_payload_budget());
    ScratchPcmWindow(const ScratchPcmWindow&) = delete;
    ScratchPcmWindow& operator=(const ScratchPcmWindow&) = delete;
    ScratchPcmWindow(ScratchPcmWindow&&) = delete;
    ScratchPcmWindow& operator=(ScratchPcmWindow&&) = delete;

    [[nodiscard]] std::uint32_t sample_rate() const noexcept
    {
        return sample_rate_;
    }
    [[nodiscard]] std::uint32_t channels() const noexcept { return channels_; }
    [[nodiscard]] std::size_t capacity_frames() const noexcept
    {
        return capacity_frames_;
    }
    [[nodiscard]] std::size_t capacity_samples() const noexcept
    {
        return storage_.size();
    }
    [[nodiscard]] std::size_t payload_bytes() const noexcept
    {
        return storage_.size() * sizeof(float);
    }
    [[nodiscard]] std::int64_t first_frame() const noexcept
    {
        return first_frame_;
    }
    [[nodiscard]] std::size_t frame_count() const noexcept
    {
        return frame_count_;
    }
    [[nodiscard]] std::uint64_t generation() const noexcept
    {
        return generation_;
    }
    [[nodiscard]] std::uint64_t epoch() const noexcept { return epoch_; }

    [[nodiscard]] ScratchWindowRange planned_range(
        std::int64_t anchor_frame,
        std::int64_t source_frame_count) const noexcept;

    // Non-RT backfill only: the caller must exclusively own an unpublished
    // bank until publish(). After publication the bank is immutable and may
    // not be written or destroyed until the RT reader explicitly releases or
    // acknowledges it. Release/acquire publishes the first contents; it does
    // not make bank reclamation safe. Task 3B owns that slot state machine.
    [[nodiscard]] float* writable_data() noexcept { return storage_.data(); }

    [[nodiscard]] bool publish(std::int64_t first_frame,
                               std::size_t frame_count,
                               std::uint64_t generation,
                               std::uint64_t epoch) noexcept;

    [[nodiscard]] bool assign(std::int64_t first_frame,
                              const float* interleaved,
                              std::size_t frame_count,
                              std::uint64_t generation,
                              std::uint64_t epoch) noexcept;

    [[nodiscard]] bool read_linear(double source_frame,
                                   std::size_t channel,
                                   float& sample) const noexcept;

private:
    [[nodiscard]] bool valid_range(std::int64_t first_frame,
                                   std::size_t frame_count) const noexcept;

    std::uint32_t sample_rate_{};
    std::uint32_t channels_{};
    std::size_t capacity_frames_{};
    std::vector<float> storage_;
    std::int64_t first_frame_{};
    std::size_t frame_count_{};
    std::uint64_t generation_{};
    std::uint64_t epoch_{};
};

} // namespace agplayer
