#pragma once

#include <atomic>
#include <cstdint>

namespace agplayer {

struct ScratchCommand final {
    std::uint64_t generation{};
    std::uint64_t epoch{};
    bool active{};
    float signed_rate{};
};

// Single-producer, multi-consumer mailbox. Every field remains atomic so the
// sequence retry cannot hide a C++ data race.
class ScratchCommandMailbox final {
public:
    ScratchCommandMailbox() noexcept = default;

    void publish(const ScratchCommand& command) noexcept
    {
        // A weakly ordered seqlock spread across independent atomics can let a
        // reader pair an old even sequence with newer fields. Keep every edge
        // sequentially consistent until the snapshot fits one lock-free atom.
        sequence_.fetch_add(1U, std::memory_order_seq_cst);
        generation_.store(command.generation, std::memory_order_seq_cst);
        epoch_.store(command.epoch, std::memory_order_seq_cst);
        active_.store(command.active, std::memory_order_seq_cst);
        signed_rate_.store(command.signed_rate, std::memory_order_seq_cst);
        sequence_.fetch_add(1U, std::memory_order_seq_cst);
    }

    [[nodiscard]] bool try_read(ScratchCommand& command) const noexcept
    {
        constexpr int maximum_attempts = 8;
        for (int attempt = 0; attempt < maximum_attempts; ++attempt) {
            const std::uint64_t before =
                sequence_.load(std::memory_order_seq_cst);
            if ((before & 1U) != 0U) continue;

            ScratchCommand candidate{
                generation_.load(std::memory_order_seq_cst),
                epoch_.load(std::memory_order_seq_cst),
                active_.load(std::memory_order_seq_cst),
                signed_rate_.load(std::memory_order_seq_cst),
            };
            const std::uint64_t after =
                sequence_.load(std::memory_order_seq_cst);
            if (before == after) {
                command = candidate;
                return true;
            }
        }
        return false;
    }

private:
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    static_assert(std::atomic<bool>::is_always_lock_free);
    static_assert(std::atomic<float>::is_always_lock_free);

    std::atomic<std::uint64_t> sequence_{0U};
    std::atomic<std::uint64_t> generation_{0U};
    std::atomic<std::uint64_t> epoch_{0U};
    std::atomic<bool> active_{false};
    std::atomic<float> signed_rate_{0.0F};
};

} // namespace agplayer
