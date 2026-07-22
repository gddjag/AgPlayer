#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <vector>

namespace agplayer {

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
    }

    [[nodiscard]] std::size_t write(const float* interleaved,
                                    const std::size_t frame_count) noexcept
    {
        if (interleaved == nullptr || frame_count == 0U) {
            return 0U;
        }

        const std::size_t write_frame = write_frame_.load(std::memory_order_relaxed);
        const std::size_t read_frame = read_frame_.load(std::memory_order_acquire);
        const std::size_t writable = capacity_frames_ - (write_frame - read_frame);
        const std::size_t frames = std::min(frame_count, writable);
        copy_into_storage(interleaved, write_frame, frames);
        write_frame_.store(write_frame + frames, std::memory_order_release);
        return frames;
    }

    [[nodiscard]] std::size_t read(float* interleaved,
                                   const std::size_t frame_count) noexcept
    {
        if (interleaved == nullptr || frame_count == 0U) {
            return 0U;
        }

        const std::size_t read_frame = read_frame_.load(std::memory_order_relaxed);
        const std::size_t write_frame = write_frame_.load(std::memory_order_acquire);
        const std::size_t frames = std::min(frame_count, write_frame - read_frame);
        copy_from_storage(interleaved, read_frame, frames);
        read_frame_.store(read_frame + frames, std::memory_order_release);
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
    std::atomic<std::size_t> read_frame_{0U};
    std::atomic<std::size_t> write_frame_{0U};
};

} // namespace agplayer
