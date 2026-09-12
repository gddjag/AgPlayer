#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace agplayer {

static_assert(std::atomic<std::size_t>::is_always_lock_free);

class PcmRingBuffer final {
public:
    PcmRingBuffer(const std::size_t capacity_frames, const std::size_t channels)
        : capacity_frames_(capacity_frames)
        , channels_(channels)
    {
        if (capacity_frames_ == 0U || channels_ == 0U) {
            throw std::invalid_argument("PCM buffer dimensions must be non-zero");
        }
        if (capacity_frames_ > std::numeric_limits<std::size_t>::max() / channels_) {
            throw std::length_error("PCM buffer dimensions are too large");
        }
        storage_.resize(capacity_frames_ * channels_);
        source_frame_storage_.resize(capacity_frames_);
        source_generation_storage_.resize(capacity_frames_);
    }

    [[nodiscard]] std::size_t write(const float* interleaved,
                                    const std::size_t frame_count) noexcept
    {
        return write(interleaved, nullptr, frame_count);
    }

    [[nodiscard]] std::size_t write(
        const float* interleaved,
        const std::int64_t* source_frame_after,
        const std::size_t frame_count) noexcept
    {
        return write(interleaved, source_frame_after, frame_count, 0U);
    }

    [[nodiscard]] std::size_t write(
        const float* interleaved,
        const std::int64_t* source_frame_after,
        const std::size_t frame_count,
        const std::uint64_t source_generation) noexcept
    {
        if (interleaved == nullptr || frame_count == 0U) {
            return 0U;
        }

        const std::size_t write_frame = write_frame_.load(std::memory_order_relaxed);
        const std::size_t read_frame = read_frame_.load(std::memory_order_acquire);
        const std::size_t writable = capacity_frames_ - (write_frame - read_frame);
        const std::size_t frames = std::min(frame_count, writable);
        copy_into_storage(interleaved, write_frame, frames);
        copy_into_source_storage(source_frame_after, write_frame, frames);
        copy_into_generation_storage(source_generation, write_frame, frames);
        write_frame_.store(write_frame + frames, std::memory_order_release);
        return frames;
    }

    [[nodiscard]] std::size_t read(float* interleaved,
                                   const std::size_t frame_count) noexcept
    {
        return read(interleaved, frame_count, nullptr);
    }

    [[nodiscard]] std::size_t read(
        float* interleaved,
        const std::size_t frame_count,
        std::int64_t* last_source_frame) noexcept
    {
        return read(interleaved, frame_count, last_source_frame, nullptr);
    }

    [[nodiscard]] std::size_t read(
        float* interleaved,
        const std::size_t frame_count,
        std::int64_t* last_source_frame,
        std::uint64_t* last_source_generation) noexcept
    {
        if (interleaved == nullptr || frame_count == 0U) {
            return 0U;
        }

        const std::size_t read_frame = read_frame_.load(std::memory_order_relaxed);
        const std::size_t write_frame = write_frame_.load(std::memory_order_acquire);
        const std::size_t frames = std::min(frame_count, write_frame - read_frame);
        copy_from_storage(interleaved, read_frame, frames);
        const std::int64_t source_frame = frames == 0U ? 0
            : source_frame_storage_[(read_frame + frames - 1U)
                                    % capacity_frames_];
        const std::uint64_t source_generation = frames == 0U ? 0U
            : source_generation_storage_[(read_frame + frames - 1U)
                                          % capacity_frames_];
        // The CAS validates only that clear() did not advance the read cursor;
        // it does not by itself make a copied payload safe from concurrent
        // clear/write reuse. AudioEngine therefore holds its outer timeline
        // writer across read/copy and every clear path. A failed CAS publishes
        // neither stale PCM nor stale source progress.
        std::size_t expected = read_frame;
        if (!read_frame_.compare_exchange_strong(
                expected,
                read_frame + frames,
                std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            return 0U;
        }
        if (last_source_frame != nullptr && frames > 0U) {
            *last_source_frame = source_frame;
        }
        if (last_source_generation != nullptr && frames > 0U) {
            *last_source_generation = source_generation;
        }
        return frames;
    }

    void clear() noexcept
    {
        read_frame_.store(write_frame_.load(std::memory_order_acquire),
                          std::memory_order_release);
    }

    [[nodiscard]] std::size_t available_frames() const noexcept
    {
        const std::size_t write_frame = write_frame_.load(std::memory_order_acquire);
        const std::size_t read_frame = read_frame_.load(std::memory_order_acquire);
        return write_frame - read_frame;
    }

private:
    void copy_into_generation_storage(
        const std::uint64_t generation,
        const std::size_t write_frame,
        const std::size_t frame_count) noexcept
    {
        const std::size_t start_frame = write_frame % capacity_frames_;
        const std::size_t first_frames = std::min(
            frame_count, capacity_frames_ - start_frame);
        std::fill_n(source_generation_storage_.data() + start_frame,
                    first_frames, generation);
        std::fill_n(source_generation_storage_.data(),
                    frame_count - first_frames, generation);
    }

    void copy_into_source_storage(
        const std::int64_t* source,
        const std::size_t write_frame,
        const std::size_t frame_count) noexcept
    {
        const std::size_t start_frame = write_frame % capacity_frames_;
        const std::size_t first_frames = std::min(
            frame_count, capacity_frames_ - start_frame);
        if (source == nullptr) {
            std::fill_n(source_frame_storage_.data() + start_frame,
                        first_frames, std::int64_t{0});
            std::fill_n(source_frame_storage_.data(),
                        frame_count - first_frames, std::int64_t{0});
            return;
        }
        std::copy_n(source, first_frames,
                    source_frame_storage_.data() + start_frame);
        std::copy_n(source + first_frames, frame_count - first_frames,
                    source_frame_storage_.data());
    }

    void copy_into_storage(const float* source,
                           const std::size_t write_frame,
                           const std::size_t frame_count) noexcept
    {
        const std::size_t start_frame = write_frame % capacity_frames_;
        const std::size_t first_frames = std::min(frame_count, capacity_frames_ - start_frame);
        const std::size_t first_samples = first_frames * channels_;
        std::copy_n(source, first_samples, storage_.data() + start_frame * channels_);
        std::copy_n(source + first_samples,
                    (frame_count - first_frames) * channels_,
                    storage_.data());
    }

    void copy_from_storage(float* destination,
                           const std::size_t read_frame,
                           const std::size_t frame_count) const noexcept
    {
        const std::size_t start_frame = read_frame % capacity_frames_;
        const std::size_t first_frames = std::min(frame_count, capacity_frames_ - start_frame);
        const std::size_t first_samples = first_frames * channels_;
        std::copy_n(storage_.data() + start_frame * channels_, first_samples, destination);
        std::copy_n(storage_.data(),
                    (frame_count - first_frames) * channels_,
                    destination + first_samples);
    }

    std::size_t capacity_frames_;
    std::size_t channels_;
    std::vector<float> storage_;
    std::vector<std::int64_t> source_frame_storage_;
    std::vector<std::uint64_t> source_generation_storage_;
    std::atomic<std::size_t> read_frame_{0U};
    std::atomic<std::size_t> write_frame_{0U};
};

} // namespace agplayer
