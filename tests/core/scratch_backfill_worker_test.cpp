// Test files deliberately keep assert() active even in Release builds.
#undef NDEBUG

#include "scratch_backfill_worker.hpp"

#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <thread>

namespace agplayer {

class ScratchBackfillWorkerTestAccess final {
public:
    [[nodiscard]] static bool has_ready(
        const ScratchBackfillWorker& worker) noexcept
    {
        return worker.has_ready_for_test();
    }
};

} // namespace agplayer

namespace {

bool wait_for_bank(agplayer::ScratchBackfillWorker& worker,
                   agplayer::ScratchBackfillLease& lease)
{
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(5);
    do {
        if (worker.try_acquire(lease)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}

bool acquire_within(agplayer::ScratchBackfillWorker& worker,
                    agplayer::ScratchBackfillLease& lease,
                    const std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    do {
        if (worker.try_acquire(lease)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}

bool wait_until_ready(const agplayer::ScratchBackfillWorker& worker)
{
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(5);
    do {
        if (agplayer::ScratchBackfillWorkerTestAccess::has_ready(worker)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}

} // namespace

int main(const int argc, char* const argv[])
{
    assert(argc == 2);
    constexpr std::uint32_t sample_rate = 44'100U;
    constexpr std::uint32_t channels = 2U;
    constexpr std::int64_t source_frames = 88'200;
    constexpr std::uint64_t generation = 1U;
    constexpr std::uint64_t epoch = 7U;

    agplayer::ScratchBackfillWorker worker(sample_rate, channels);
    assert(worker.total_payload_bytes()
           <= agplayer::ScratchPcmWindow::kTotalPayloadLimitBytes);
    const agplayer::ScratchBackfillRequest request{
        argv[1], sample_rate, channels, source_frames, source_frames / 2,
        generation, epoch,
    };
    assert(worker.request(request));

    agplayer::ScratchBackfillLease lease{};
    assert(wait_for_bank(worker, lease));
    assert(lease.window != nullptr);
    assert(lease.window->first_frame() == 0);
    assert(lease.window->frame_count() == static_cast<std::size_t>(source_frames));
    assert(lease.window->generation() == generation);
    assert(lease.window->epoch() == epoch);

    float decoded{};
    assert(lease.window->read_linear(100.0, 0U, decoded));
    const double phase = 2.0 * std::acos(-1.0) * 440.0 * 100.0
        / static_cast<double>(sample_rate);
    const auto expected_pcm = static_cast<std::int16_t>(std::lround(
        std::sin(phase) * 0.251188643150958 * 32'767.0));
    const float expected = static_cast<float>(expected_pcm) / 32'768.0F;
    assert(std::abs(decoded - expected) < 0.000'1F);

    assert(worker.release(lease));
    worker.shutdown();

    agplayer::ScratchBackfillWorker cancelled_ready(sample_rate, channels);
    agplayer::ScratchBackfillRequest cancelled_request = request;
    cancelled_request.generation = 2U;
    assert(cancelled_ready.request(cancelled_request));
    assert(wait_until_ready(cancelled_ready));
    cancelled_ready.cancel();
    agplayer::ScratchBackfillLease cancelled_lease{};
    assert(!cancelled_ready.try_acquire(cancelled_lease));
    cancelled_ready.shutdown();

    agplayer::ScratchBackfillWorker superseded_ready(sample_rate, channels);
    agplayer::ScratchBackfillRequest old_ready = request;
    old_ready.generation = 10U;
    old_ready.epoch = 20U;
    assert(superseded_ready.request(old_ready));
    assert(wait_until_ready(superseded_ready));
    agplayer::ScratchBackfillRequest replacement = old_ready;
    replacement.generation = 11U;
    assert(superseded_ready.request(replacement));
    agplayer::ScratchBackfillLease replacement_lease{};
    assert(wait_for_bank(superseded_ready, replacement_lease));
    assert(replacement_lease.window->generation() == replacement.generation);
    assert(replacement_lease.window->epoch() == replacement.epoch);
    assert(superseded_ready.release(replacement_lease));
    assert(!superseded_ready.request(replacement));
    agplayer::ScratchBackfillRequest older_generation = replacement;
    older_generation.generation = 10U;
    assert(!superseded_ready.request(older_generation));
    agplayer::ScratchBackfillRequest older_epoch = replacement;
    older_epoch.epoch = 19U;
    older_epoch.generation = 999U;
    assert(!superseded_ready.request(older_epoch));
    superseded_ready.shutdown();

    agplayer::ScratchBackfillWorker rotating(sample_rate, channels);
    agplayer::ScratchBackfillRequest rotating_request = request;
    rotating_request.epoch = 30U;
    rotating_request.generation = 1U;
    assert(rotating.request(rotating_request));
    agplayer::ScratchBackfillLease first_lease{};
    assert(wait_for_bank(rotating, first_lease));
    agplayer::ScratchBackfillLease stale_first = first_lease;

    rotating_request.generation = 2U;
    assert(rotating.request(rotating_request));
    agplayer::ScratchBackfillLease second_lease{};
    assert(wait_for_bank(rotating, second_lease));
    assert(first_lease.slot != second_lease.slot);
    assert(first_lease.window != second_lease.window);
    assert(first_lease.window->generation() == 1U);
    assert(second_lease.window->generation() == 2U);

    rotating_request.generation = 3U;
    assert(rotating.request(rotating_request));
    assert(!agplayer::ScratchBackfillWorkerTestAccess::has_ready(rotating));
    assert(first_lease.window->generation() == 1U);
    assert(second_lease.window->generation() == 2U);
    assert(rotating.release(first_lease));

    agplayer::ScratchBackfillLease third_lease{};
    assert(wait_for_bank(rotating, third_lease));
    assert(third_lease.slot == stale_first.slot);
    assert(third_lease.token != stale_first.token);
    assert(third_lease.window->generation() == 3U);
    assert(!rotating.release(stale_first));
    assert(second_lease.window->generation() == 2U);

    rotating_request.generation = 4U;
    assert(rotating.request(rotating_request));
    rotating.cancel();
    assert(rotating.release(second_lease));
    agplayer::ScratchBackfillLease cancelled_pending{};
    assert(!acquire_within(rotating, cancelled_pending,
                           std::chrono::milliseconds(100)));

    rotating_request.generation = 5U;
    assert(rotating.request(rotating_request));
    agplayer::ScratchBackfillLease after_cancel{};
    assert(wait_for_bank(rotating, after_cancel));
    assert(after_cancel.window->generation() == 5U);
    assert(rotating.release(third_lease));
    assert(rotating.release(after_cancel));
    rotating.shutdown();

    agplayer::ScratchBackfillWorker retained_on_shutdown(
        sample_rate, channels);
    agplayer::ScratchBackfillRequest retained_request = request;
    retained_request.epoch = 40U;
    retained_request.generation = 1U;
    assert(retained_on_shutdown.request(retained_request));
    agplayer::ScratchBackfillLease retained_lease{};
    assert(wait_for_bank(retained_on_shutdown, retained_lease));
    float retained_sample{};
    assert(retained_lease.window->read_linear(100.0, 0U,
                                              retained_sample));
    const auto shutdown_started = std::chrono::steady_clock::now();
    retained_on_shutdown.shutdown();
    assert(std::chrono::steady_clock::now() - shutdown_started
           < std::chrono::seconds(2));
    assert(retained_on_shutdown.is_shutdown());
    float after_shutdown_sample{};
    assert(retained_lease.window->read_linear(100.0, 0U,
                                              after_shutdown_sample));
    assert(after_shutdown_sample == retained_sample);
    assert(retained_on_shutdown.release(retained_lease));

    agplayer::ScratchBackfillWorker shutdown_ready(sample_rate, channels);
    agplayer::ScratchBackfillRequest shutdown_request = request;
    shutdown_request.epoch = 50U;
    shutdown_request.generation = 1U;
    assert(shutdown_ready.request(shutdown_request));
    assert(wait_until_ready(shutdown_ready));
    shutdown_ready.shutdown();
    agplayer::ScratchBackfillLease shutdown_lease{};
    assert(!shutdown_ready.try_acquire(shutdown_lease));
}
