#pragma once

#include "scratch_pcm_window.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace agplayer {

class ScratchBackfillWorkerTestAccess;

struct ScratchBackfillRequest final {
    std::string media_path;
    std::uint32_t sample_rate{};
    std::uint32_t channels{};
    std::int64_t source_frame_count{};
    std::int64_t anchor_frame{};
    std::uint64_t generation{};
    std::uint64_t epoch{};
};

struct ScratchBackfillLease final {
    static constexpr std::uint32_t kInvalidSlot = UINT32_MAX;

    const ScratchPcmWindow* window{};
    std::uint64_t token{};
    std::uint32_t slot{kInvalidSlot};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return window != nullptr;
    }
};

class ScratchBackfillWorker final {
public:
    static constexpr std::size_t kBankCount = 2U;

    ScratchBackfillWorker(std::uint32_t sample_rate,
                          std::uint32_t channels);
    ~ScratchBackfillWorker();

    ScratchBackfillWorker(const ScratchBackfillWorker&) = delete;
    ScratchBackfillWorker& operator=(const ScratchBackfillWorker&) = delete;
    ScratchBackfillWorker(ScratchBackfillWorker&&) = delete;
    ScratchBackfillWorker& operator=(ScratchBackfillWorker&&) = delete;

    // Non-real-time control surface. The request is copied before returning.
    [[nodiscard]] bool request(const ScratchBackfillRequest& request);
    void cancel();
    void shutdown() noexcept;

    // Callback-safe lease surface: bounded lock-free atomics only.
    [[nodiscard]] bool try_acquire(ScratchBackfillLease& lease) noexcept;
    [[nodiscard]] bool release(ScratchBackfillLease& lease) noexcept;

    [[nodiscard]] std::size_t total_payload_bytes() const noexcept;
    [[nodiscard]] bool is_shutdown() const noexcept;

private:
    friend class ScratchBackfillWorkerTestAccess;

    [[nodiscard]] bool has_ready_for_test() const noexcept;

    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace agplayer
