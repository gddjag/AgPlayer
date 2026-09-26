// Test files deliberately keep assert() active even in Release builds.
#undef NDEBUG

#include "scratch_command_mailbox.hpp"

#include <atomic>
#include <cassert>
#include <cstdint>
#include <thread>

namespace {

agplayer::ScratchCommand command_for(const std::uint64_t generation) noexcept
{
    return {
        generation,
        generation ^ 0xA5A5A5A5A5A5A5A5ULL,
        (generation & 1U) != 0U,
        static_cast<float>(static_cast<int>(generation % 7U) - 3),
    };
}

bool coherent(const agplayer::ScratchCommand& command) noexcept
{
    const auto expected = command_for(command.generation);
    return command.epoch == expected.epoch
        && command.active == expected.active
        && command.signed_rate == expected.signed_rate;
}

} // namespace

int main()
{
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    static_assert(std::atomic<float>::is_always_lock_free);
    static_assert(std::atomic<bool>::is_always_lock_free);

    agplayer::ScratchCommandMailbox mailbox;
    mailbox.publish(command_for(1U));
    agplayer::ScratchCommand snapshot{};
    assert(mailbox.try_read(snapshot));
    assert(snapshot.generation == 1U);
    assert(coherent(snapshot));

    constexpr std::uint64_t last_generation = 500'000U;
    std::atomic<bool> writer_done{false};
    std::atomic<bool> torn{false};
    std::thread writer([&] {
        for (std::uint64_t generation = 2U;
             generation <= last_generation; ++generation) {
            mailbox.publish(command_for(generation));
        }
        writer_done.store(true, std::memory_order_release);
    });

    while (!writer_done.load(std::memory_order_acquire)) {
        if (mailbox.try_read(snapshot) && !coherent(snapshot)) {
            torn.store(true, std::memory_order_relaxed);
            break;
        }
    }
    writer.join();
    assert(!torn.load(std::memory_order_relaxed));

    bool read_latest = false;
    for (int attempt = 0; attempt < 100 && !read_latest; ++attempt) {
        read_latest = mailbox.try_read(snapshot);
    }
    assert(read_latest);
    assert(snapshot.generation == last_generation);
    assert(coherent(snapshot));
}
