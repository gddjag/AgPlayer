#pragma once
#include <agplayer/c_api.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <iterator>

namespace agplayer {
// One audio callback producer, one GUI consumer. Control/reset only changes
// epoch; it never rewinds cursors or reuses payload while the reader copies it.
class VisualPcmTap final {
public:
    void set_enabled(bool enabled) noexcept
    {
        auto state = epoch_.load(std::memory_order_acquire);
        while ((state & 1U) != static_cast<unsigned>(enabled)
               && !epoch_.compare_exchange_weak(state, (state + 2U) ^ 1U,
                                               std::memory_order_acq_rel)) {}
    }
    void invalidate() noexcept { epoch_.fetch_add(2U, std::memory_order_acq_rel); }
    void write(const float* mono, std::size_t count, int rate) noexcept
    {
        auto epoch = epoch_.load(std::memory_order_acquire);
        if (!(epoch & 1U) || !mono || count == 0 || rate <= 0) return;
        if (last_rate_ != 0 && last_rate_ != rate) {
            invalidate();
            epoch = epoch_.load(std::memory_order_acquire);
            if (!(epoch & 1U)) return;
        }
        last_rate_ = rate;
        const auto w = write_.load(std::memory_order_relaxed);
        const auto r = read_.load(std::memory_order_acquire);
        if (w - r == blocks_.size()) { invalidate(); return; }
        auto& block = blocks_[w % blocks_.size()];
        block.count = (std::min)(count, block.samples.size());
        block.epoch = epoch;
        block.rate = rate;
        block.first = next_sample_;
        next_sample_ += count;
        std::copy_n(mono + count - block.count, block.count, block.samples.data());
        block.first += count - block.count;
        write_.store(w + 1U, std::memory_order_release);
    }
    void read(ag_visual_pcm_snapshot& out) noexcept
    {
        out = {};
        const auto epoch = epoch_.load(std::memory_order_acquire);
        out.generation = epoch;
        auto r = read_.load(std::memory_order_relaxed);
        const auto end = write_.load(std::memory_order_acquire);
        for (; r != end; ++r) {
            const auto& block = blocks_[r % blocks_.size()];
            if (block.epoch != epoch || !(epoch & 1U)) continue;
            // Preserve FIFO continuity across bounded reads. Taking only the
            // newest 1024 samples caused a visual reset whenever a GUI poll
            // was late (and on every normal poll at high sample rates).
            if (out.sample_count != 0
                && (out.sample_rate != block.rate
                    || out.first_sample_index + out.sample_count != block.first)) break;
            if (out.sample_count + block.count > std::size(out.samples)) break;
            if (out.sample_count == 0) out.first_sample_index = block.first;
            std::copy_n(block.samples.data(), block.count, out.samples + out.sample_count);
            out.sample_count += block.count;
            out.sample_rate = block.rate;
        }
        read_.store(r, std::memory_order_release);
        const auto current = epoch_.load(std::memory_order_acquire);
        if (current != epoch) { out = {}; out.generation = current; }
    }
private:
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    static_assert(std::atomic<std::size_t>::is_always_lock_free);
    struct Block {
        std::array<float, 512> samples{};
        std::size_t count = 0;
        int rate = 0;
        std::uint64_t epoch = 0, first = 0;
    };
    std::array<Block, 32> blocks_{};
    std::atomic<std::size_t> write_{0}, read_{0};
    std::atomic<std::uint64_t> epoch_{0};
    std::uint64_t next_sample_ = 0;
    int last_rate_ = 0;
};
}
