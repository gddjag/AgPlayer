#include "scratch_backfill_worker.hpp"

#include "decoder.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace agplayer {
namespace {

enum class SlotState : std::uint8_t {
    Free,
    Filling,
    Ready,
    Active,
};

static_assert(std::atomic<SlotState>::is_always_lock_free);
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);

void lower_worker_priority() noexcept
{
#if defined(_WIN32)
    (void)SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#endif
}

} // namespace

class ScratchBackfillWorker::Impl final {
public:
    struct Slot final {
        Slot(const std::uint32_t sample_rate, const std::uint32_t channels)
            : window(sample_rate, channels)
        {
        }

        ScratchPcmWindow window;
        std::atomic<SlotState> state{SlotState::Free};
        std::atomic<std::uint64_t> token{0U};
        std::atomic<std::uint64_t> request_serial{0U};
    };

    struct QueuedRequest final {
        ScratchBackfillRequest request;
        std::uint64_t serial{};
    };

    struct InterruptContext final {
        const Impl* worker{};
        std::uint64_t serial{};
    };

    Impl(const std::uint32_t sample_rate, const std::uint32_t channels)
        : sample_rate_(sample_rate)
        , channels_(channels)
        , first_(sample_rate, channels)
        , second_(sample_rate, channels)
        , slots_{&first_, &second_}
        , thread_([this] { run(); })
    {
    }

    ~Impl() { shutdown(); }

    [[nodiscard]] bool request(const ScratchBackfillRequest& incoming)
    {
        if (!valid(incoming)) return false;
        ScratchBackfillRequest copy = incoming;

        {
            std::lock_guard<std::mutex> lock(control_mutex_);
            if (stopping_.load(std::memory_order_acquire)) return false;
            if (has_latest_
                && (copy.epoch < latest_epoch_
                    || (copy.epoch == latest_epoch_
                        && copy.generation <= latest_generation_))) {
                return false;
            }

            const std::uint64_t serial = next_serial_ + 1U;
            pending_ = QueuedRequest{std::move(copy), serial};
            next_serial_ = serial;
            latest_epoch_ = incoming.epoch;
            latest_generation_ = incoming.generation;
            has_latest_ = true;
            latest_serial_.store(serial, std::memory_order_release);
        }
        control_condition_.notify_one();
        return true;
    }

    void cancel()
    {
        {
            std::lock_guard<std::mutex> lock(control_mutex_);
            if (stopping_.load(std::memory_order_acquire)) return;
            ++next_serial_;
            pending_.reset();
            latest_serial_.store(next_serial_, std::memory_order_release);
        }
        control_condition_.notify_one();
    }

    void shutdown() noexcept
    {
        {
            std::lock_guard<std::mutex> lock(control_mutex_);
            if (!stopping_.exchange(true, std::memory_order_acq_rel)) {
                ++next_serial_;
                pending_.reset();
                latest_serial_.store(next_serial_, std::memory_order_release);
            }
        }
        control_condition_.notify_all();
        if (thread_.joinable()) thread_.join();
    }

    [[nodiscard]] bool try_acquire(ScratchBackfillLease& lease) noexcept
    {
        if (lease) return false;
        const std::uint64_t descriptor =
            published_.load(std::memory_order_acquire);
        if (descriptor == 0U) return false;

        const std::uint32_t slot_index =
            static_cast<std::uint32_t>(descriptor & 1U);
        const std::uint64_t token = descriptor >> 1U;
        Slot& slot = *slots_[slot_index];
        SlotState expected = SlotState::Ready;
        if (!slot.state.compare_exchange_strong(
                expected, SlotState::Active,
                std::memory_order_acq_rel, std::memory_order_acquire)) {
            return false;
        }
        if (slot.token.load(std::memory_order_relaxed) != token
            || slot.request_serial.load(std::memory_order_relaxed)
                != latest_serial_.load(std::memory_order_acquire)
            || stopping_.load(std::memory_order_acquire)) {
            slot.state.store(SlotState::Free, std::memory_order_release);
            return false;
        }

        lease.window = &slot.window;
        lease.token = token;
        lease.slot = slot_index;
        return true;
    }

    [[nodiscard]] bool release(ScratchBackfillLease& lease) noexcept
    {
        if (!lease || lease.slot >= slots_.size()) return false;
        Slot& slot = *slots_[lease.slot];
        if (lease.window != &slot.window
            || lease.token != slot.token.load(std::memory_order_relaxed)) {
            return false;
        }
        SlotState expected = SlotState::Active;
        if (!slot.state.compare_exchange_strong(
                expected, SlotState::Free,
                std::memory_order_release, std::memory_order_relaxed)) {
            return false;
        }
        lease = {};
        return true;
    }

    [[nodiscard]] std::size_t total_payload_bytes() const noexcept
    {
        return first_.window.payload_bytes() + second_.window.payload_bytes();
    }

    [[nodiscard]] bool is_shutdown() const noexcept
    {
        return stopping_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool has_ready_for_test() const noexcept
    {
        if (stopping_.load(std::memory_order_acquire)) return false;
        const std::uint64_t serial =
            latest_serial_.load(std::memory_order_acquire);
        for (const Slot* const slot : slots_) {
            if (slot->state.load(std::memory_order_acquire) == SlotState::Ready
                && slot->request_serial.load(std::memory_order_relaxed)
                    == serial) {
                return true;
            }
        }
        return false;
    }

private:
    [[nodiscard]] bool valid(const ScratchBackfillRequest& request) const noexcept
    {
        return !request.media_path.empty()
            && request.sample_rate == sample_rate_
            && request.channels == channels_
            && request.sample_rate
                <= static_cast<std::uint32_t>(std::numeric_limits<int>::max())
            && request.channels
                <= static_cast<std::uint32_t>(std::numeric_limits<int>::max())
            && request.source_frame_count > 0;
    }

    [[nodiscard]] static bool interrupted(void* opaque) noexcept
    {
        const auto* context = static_cast<const InterruptContext*>(opaque);
        return context == nullptr || context->worker == nullptr
            || context->worker->stopping_.load(std::memory_order_acquire)
            || context->worker->latest_serial_.load(std::memory_order_acquire)
                != context->serial;
    }

    [[nodiscard]] Slot* claim_slot(const std::uint64_t serial) noexcept
    {
        while (!stopping_.load(std::memory_order_acquire)
               && latest_serial_.load(std::memory_order_acquire) == serial) {
            for (Slot* const slot : slots_) {
                SlotState expected = SlotState::Free;
                if (slot->state.compare_exchange_strong(
                        expected, SlotState::Filling,
                        std::memory_order_acq_rel, std::memory_order_acquire)) {
                    return slot;
                }
            }
            for (Slot* const slot : slots_) {
                SlotState expected = SlotState::Ready;
                if (slot->state.compare_exchange_strong(
                        expected, SlotState::Filling,
                        std::memory_order_acq_rel, std::memory_order_acquire)) {
                    return slot;
                }
            }

            std::unique_lock<std::mutex> lock(control_mutex_);
            control_condition_.wait_for(lock, std::chrono::milliseconds(2),
                                        [this, serial] {
                return stopping_.load(std::memory_order_acquire)
                    || latest_serial_.load(std::memory_order_acquire) != serial
                    || pending_.has_value();
            });
        }
        return nullptr;
    }

    [[nodiscard]] bool fill(Slot& slot,
                            const QueuedRequest& queued) noexcept
    {
        const ScratchWindowRange range = slot.window.planned_range(
            queued.request.anchor_frame, queued.request.source_frame_count);
        if (range.frame_count == 0U) return false;

        InterruptContext interrupt_context{this, queued.serial};
        Decoder decoder;
        DecoderOpenOptions options{};
        options.output_sample_rate = static_cast<int>(sample_rate_);
        options.output_channels = static_cast<int>(channels_);
        options.interrupt_callback = &interrupted;
        options.interrupt_context = &interrupt_context;
        if (decoder.open(queued.request.media_path, options) != AG_OK) {
            return false;
        }
        const DecodedAudioFormat& format = decoder.output_format();
        if (format.sample_rate != static_cast<int>(sample_rate_)
            || format.channels != static_cast<int>(channels_)
            || decoder.seekFrame(range.first_frame) != AG_OK) {
            return false;
        }

        float* const destination = slot.window.writable_data();
        std::size_t written = 0U;
        DecodedAudioBlock block;
        while (written < range.frame_count && !interrupted(&interrupt_context)) {
            if (decoder.read(block) != AG_OK) return false;
            if (block.frames == 0U) {
                if (block.end_of_stream) break;
                continue;
            }
            if (block.frames > std::numeric_limits<std::size_t>::max()
                    / static_cast<std::size_t>(channels_)
                || block.samples.size()
                    < block.frames * static_cast<std::size_t>(channels_)
                || block.timestamp_frame < 0
                || block.frames > static_cast<std::size_t>(
                    std::numeric_limits<std::int64_t>::max()
                    - block.timestamp_frame)) {
                return false;
            }

            const std::int64_t expected_frame = range.first_frame
                + static_cast<std::int64_t>(written);
            const std::int64_t block_end = block.timestamp_frame
                + static_cast<std::int64_t>(block.frames);
            if (block_end <= expected_frame) {
                if (block.end_of_stream) break;
                continue;
            }
            if (block.timestamp_frame > expected_frame) return false;

            const std::size_t source_offset = static_cast<std::size_t>(
                expected_frame - block.timestamp_frame);
            const std::size_t available = block.frames - source_offset;
            const std::size_t copy_frames = std::min(
                available, range.frame_count - written);
            std::copy_n(
                block.samples.data()
                    + source_offset * static_cast<std::size_t>(channels_),
                copy_frames * static_cast<std::size_t>(channels_),
                destination + written * static_cast<std::size_t>(channels_));
            written += copy_frames;
            if (block.end_of_stream) break;
        }
        if (written == 0U || interrupted(&interrupt_context)) return false;

        std::lock_guard<std::mutex> lock(control_mutex_);
        if (stopping_.load(std::memory_order_acquire)
            || latest_serial_.load(std::memory_order_acquire) != queued.serial
            || !slot.window.publish(range.first_frame, written,
                                    queued.request.generation,
                                    queued.request.epoch)) {
            return false;
        }
        const std::size_t slot_index = &slot == slots_[0] ? 0U : 1U;
        const std::uint64_t token = ++publication_token_;
        slot.token.store(token, std::memory_order_relaxed);
        slot.request_serial.store(queued.serial, std::memory_order_relaxed);
        slot.state.store(SlotState::Ready, std::memory_order_release);
        published_.store((token << 1U) | slot_index,
                         std::memory_order_release);
        return true;
    }

    void run() noexcept
    {
        lower_worker_priority();
        for (;;) {
            QueuedRequest queued;
            {
                std::unique_lock<std::mutex> lock(control_mutex_);
                control_condition_.wait(lock, [this] {
                    return stopping_.load(std::memory_order_acquire)
                        || pending_.has_value();
                });
                if (stopping_.load(std::memory_order_acquire)) return;
                queued = std::move(*pending_);
                pending_.reset();
            }

            Slot* const slot = claim_slot(queued.serial);
            if (slot == nullptr) continue;
            bool published = false;
            try {
                published = fill(*slot, queued);
            } catch (...) {
                published = false;
            }
            if (!published) {
                slot->state.store(SlotState::Free,
                                  std::memory_order_release);
            }
        }
    }

    std::uint32_t sample_rate_{};
    std::uint32_t channels_{};
    Slot first_;
    Slot second_;
    std::array<Slot*, kBankCount> slots_{};
    std::atomic<bool> stopping_{false};
    std::atomic<std::uint64_t> latest_serial_{0U};
    std::atomic<std::uint64_t> published_{0U};
    std::mutex control_mutex_;
    std::condition_variable control_condition_;
    std::optional<QueuedRequest> pending_;
    std::uint64_t next_serial_{};
    std::uint64_t latest_epoch_{};
    std::uint64_t latest_generation_{};
    std::uint64_t publication_token_{};
    bool has_latest_{};
    std::thread thread_;
};

ScratchBackfillWorker::ScratchBackfillWorker(
    const std::uint32_t sample_rate,
    const std::uint32_t channels)
    : impl_(std::make_unique<Impl>(sample_rate, channels))
{
}

ScratchBackfillWorker::~ScratchBackfillWorker() { shutdown(); }

bool ScratchBackfillWorker::request(const ScratchBackfillRequest& request)
{
    return impl_->request(request);
}

void ScratchBackfillWorker::cancel() { impl_->cancel(); }

void ScratchBackfillWorker::shutdown() noexcept { impl_->shutdown(); }

bool ScratchBackfillWorker::try_acquire(
    ScratchBackfillLease& lease) noexcept
{
    return impl_->try_acquire(lease);
}

bool ScratchBackfillWorker::release(ScratchBackfillLease& lease) noexcept
{
    return impl_->release(lease);
}

std::size_t ScratchBackfillWorker::total_payload_bytes() const noexcept
{
    return impl_->total_payload_bytes();
}

bool ScratchBackfillWorker::is_shutdown() const noexcept
{
    return impl_->is_shutdown();
}

bool ScratchBackfillWorker::has_ready_for_test() const noexcept
{
    return impl_->has_ready_for_test();
}

} // namespace agplayer
