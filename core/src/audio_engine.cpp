#include "audio_engine.hpp"

#include "decoder.hpp"
#include "pcm_ring_buffer.hpp"

#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>

namespace agplayer {

static_assert(std::atomic<float>::is_always_lock_free);
static_assert(std::atomic<bool>::is_always_lock_free);
static_assert(std::atomic<std::int64_t>::is_always_lock_free);
static_assert(std::atomic<EngineState>::is_always_lock_free);
static_assert(std::atomic<ag_result>::is_always_lock_free);

class AudioEngine::Impl final {
public:
    Impl(const AudioBackend backend, const std::size_t buffer_frames)
        : backend_(backend)
        , buffer_frames_(buffer_frames == 0U ? 32'768U : buffer_frames)
    {
    }

    ~Impl()
    {
        shutdown_loaded_media();
    }

    ag_result load(const std::string& utf8_path) noexcept
    {
        if (utf8_path.empty()) {
            return AG_INVALID_ARGUMENT;
        }

        try {
            shutdown_loaded_media();
            state_.store(EngineState::Loading, std::memory_order_release);
            const ag_result decode_result = decoder_.open(utf8_path);
            if (decode_result != AG_OK) {
                return fail_load(decode_result);
            }

            sample_rate_ = decoder_.metadata().sample_rate;
            channels_ = decoder_.metadata().channels;
            duration_ms_ = decoder_.metadata().duration_ms;
            ring_buffer_ = std::make_unique<PcmRingBuffer>(
                buffer_frames_, static_cast<std::size_t>(channels_));

            const ag_result device_result = initialize_device();
            if (device_result != AG_OK) {
                return fail_load(device_result);
            }

            consumed_frames_.store(0, std::memory_order_release);
            decode_eof_.store(false, std::memory_order_release);
            loaded_ = true;
            terminal_error_.store(AG_OK, std::memory_order_release);
            state_.store(EngineState::Stopped, std::memory_order_release);
            const ag_result thread_result = start_decode_thread();
            if (thread_result != AG_OK) {
                return fail_load(thread_result);
            }
            return AG_OK;
        } catch (...) {
            return fail_load(AG_INTERNAL_ERROR);
        }
    }

    ag_result play() noexcept
    {
        EngineState state = state_.load(std::memory_order_acquire);
        if (state == EngineState::Error) {
            return current_error();
        }
        if (!loaded_ || !device_initialized_) {
            return AG_INVALID_ARGUMENT;
        }
        if (state == EngineState::Playing) {
            return AG_OK;
        }
        if (state == EngineState::Stopped
            && decode_eof_.load(std::memory_order_acquire)) {
            const ag_result reset_result = stop();
            if (reset_result != AG_OK) {
                return reset_result;
            }
            state = EngineState::Stopped;
        }
        if (state != EngineState::Stopped && state != EngineState::Paused) {
            return AG_INVALID_ARGUMENT;
        }
        if (!state_.compare_exchange_strong(state,
                                            EngineState::Playing,
                                            std::memory_order_acq_rel,
                                            std::memory_order_acquire)) {
            return state == EngineState::Error ? current_error()
                                               : AG_INVALID_ARGUMENT;
        }
        if (ma_device_start(&device_) != MA_SUCCESS) {
            return enter_error(AG_DEVICE_ERROR);
        }
        if (state_.load(std::memory_order_acquire) == EngineState::Error) {
            const ag_result result = current_error();
            ma_device_stop(&device_);
            stop_decode_thread();
            return result;
        }
        return AG_OK;
    }

    ag_result pause() noexcept
    {
        const EngineState state = state_.load(std::memory_order_acquire);
        if (state == EngineState::Error) {
            return current_error();
        }
        if (!loaded_ || !device_initialized_) {
            return AG_INVALID_ARGUMENT;
        }
        if (state == EngineState::Paused) {
            return AG_OK;
        }
        if (state != EngineState::Playing) {
            return AG_INVALID_ARGUMENT;
        }
        if (ma_device_stop(&device_) != MA_SUCCESS) {
            return enter_error(AG_DEVICE_ERROR);
        }
        EngineState expected = EngineState::Playing;
        if (state_.compare_exchange_strong(expected,
                                           EngineState::Paused,
                                           std::memory_order_acq_rel,
                                           std::memory_order_acquire)) {
            return AG_OK;
        }
        return expected == EngineState::Error ? current_error()
                                              : AG_INVALID_ARGUMENT;
    }

    ag_result stop() noexcept
    {
        if (state_.load(std::memory_order_acquire) == EngineState::Error) {
            return current_error();
        }
        if (!loaded_) {
            return AG_INVALID_ARGUMENT;
        }
        if (device_initialized_ && ma_device_stop(&device_) != MA_SUCCESS) {
            return enter_error(AG_DEVICE_ERROR);
        }

        stop_decode_thread();
        const ag_result seek_result = decoder_.seek(0);
        if (seek_result != AG_OK) {
            return enter_error(seek_result);
        }
        ring_buffer_->clear();
        consumed_frames_.store(0, std::memory_order_release);
        decode_eof_.store(false, std::memory_order_release);
        terminal_error_.store(AG_OK, std::memory_order_release);
        state_.store(EngineState::Stopped, std::memory_order_release);
        const ag_result thread_result = start_decode_thread();
        if (thread_result != AG_OK) {
            return enter_error(thread_result);
        }
        return AG_OK;
    }

    ag_result seek(const std::int64_t position_ms) noexcept
    {
        if (state_.load(std::memory_order_acquire) == EngineState::Error) {
            return current_error();
        }
        if (!loaded_ || position_ms < 0 || position_ms > duration_ms_) {
            return AG_INVALID_ARGUMENT;
        }

        const EngineState previous_state = state_.load(std::memory_order_acquire);
        const bool resume = previous_state == EngineState::Playing;
        if (ma_device_stop(&device_) != MA_SUCCESS) {
            return enter_error(AG_DEVICE_ERROR);
        }

        stop_decode_thread();
        const ag_result seek_result = decoder_.seek(position_ms);
        if (seek_result != AG_OK) {
            return enter_error(seek_result);
        }
        ring_buffer_->clear();
        const std::int64_t position_frames =
            (position_ms * sample_rate_ + 999) / 1'000;
        consumed_frames_.store(position_frames, std::memory_order_release);
        decode_eof_.store(false, std::memory_order_release);
        terminal_error_.store(AG_OK, std::memory_order_release);
        state_.store(previous_state, std::memory_order_release);
        const ag_result thread_result = start_decode_thread();
        if (thread_result != AG_OK) {
            return enter_error(thread_result);
        }

        if (state_.load(std::memory_order_acquire) == EngineState::Error) {
            return current_error();
        }
        if (resume && ma_device_start(&device_) != MA_SUCCESS) {
            return enter_error(AG_DEVICE_ERROR);
        }
        if (state_.load(std::memory_order_acquire) == EngineState::Error) {
            const ag_result result = current_error();
            ma_device_stop(&device_);
            stop_decode_thread();
            return result;
        }
        return AG_OK;
    }

    ag_result set_volume(const float volume) noexcept
    {
        if (!std::isfinite(volume) || volume < 0.0F || volume > 1.0F) {
            return AG_INVALID_ARGUMENT;
        }
        volume_.store(volume, std::memory_order_release);
        return AG_OK;
    }

    void set_muted(const bool muted) noexcept
    {
        muted_.store(muted, std::memory_order_release);
    }

    [[nodiscard]] EngineSnapshot snapshot() const noexcept
    {
        const std::int64_t frames = consumed_frames_.load(std::memory_order_acquire);
        return {
            state_.load(std::memory_order_acquire),
            sample_rate_ <= 0 ? 0 : frames * 1'000 / sample_rate_,
            duration_ms_,
            volume_.load(std::memory_order_acquire),
            muted_.load(std::memory_order_acquire),
        };
    }

private:
    static void data_callback(ma_device* device,
                              void* output,
                              const void*,
                              const ma_uint32 frame_count) noexcept
    {
        auto* const self = static_cast<Impl*>(device->pUserData);
        self->render(static_cast<float*>(output),
                     static_cast<std::size_t>(frame_count));
    }

    void render(float* output, const std::size_t requested_frames) noexcept
    {
        const std::size_t channels = static_cast<std::size_t>(channels_);
        if (state_.load(std::memory_order_acquire) != EngineState::Playing) {
            std::fill(output,
                      output + requested_frames * channels,
                      0.0F);
            return;
        }

        const std::size_t frames = ring_buffer_ == nullptr
                                       ? 0U
                                       : ring_buffer_->read(output, requested_frames);
        const float gain = muted_.load(std::memory_order_relaxed)
                               ? 0.0F
                               : volume_.load(std::memory_order_relaxed);
        for (std::size_t index = 0; index < frames * channels; ++index) {
            output[index] *= gain;
        }
        std::fill(output + frames * channels,
                  output + requested_frames * channels,
                  0.0F);
        consumed_frames_.fetch_add(static_cast<std::int64_t>(frames),
                                   std::memory_order_relaxed);

        if (frames < requested_frames
            && decode_eof_.load(std::memory_order_acquire)
            && ring_buffer_ != nullptr
            && ring_buffer_->available_frames() == 0U) {
            state_.store(EngineState::Stopped, std::memory_order_release);
        }
    }

    ag_result initialize_device() noexcept
    {
        ma_result result = MA_SUCCESS;
        if (backend_ == AudioBackend::Null) {
            const ma_backend backend = ma_backend_null;
            result = ma_context_init(&backend, 1U, nullptr, &context_);
        } else {
            result = ma_context_init(nullptr, 0U, nullptr, &context_);
        }
        if (result != MA_SUCCESS) {
            return AG_DEVICE_ERROR;
        }
        context_initialized_ = true;

        ma_device_config config = ma_device_config_init(ma_device_type_playback);
        config.playback.format = ma_format_f32;
        config.playback.channels = static_cast<ma_uint32>(channels_);
        config.sampleRate = static_cast<ma_uint32>(sample_rate_);
        config.dataCallback = data_callback;
        config.pUserData = this;
        result = ma_device_init(&context_, &config, &device_);
        if (result != MA_SUCCESS) {
            ma_context_uninit(&context_);
            context_initialized_ = false;
            return AG_DEVICE_ERROR;
        }
        device_initialized_ = true;
        return AG_OK;
    }

    ag_result start_decode_thread() noexcept
    {
        stop_decode_.store(false, std::memory_order_release);
        try {
            decode_thread_ = std::thread([this] { decode_loop(); });
            return AG_OK;
        } catch (...) {
            stop_decode_.store(true, std::memory_order_release);
            return AG_INTERNAL_ERROR;
        }
    }

    void stop_decode_thread() noexcept
    {
        stop_decode_.store(true, std::memory_order_release);
        if (decode_thread_.joinable()) {
            decode_thread_.join();
        }
    }

    void decode_loop() noexcept
    {
        DecodedAudioBlock block;
        std::size_t frame_offset = 0U;
        while (!stop_decode_.load(std::memory_order_acquire)) {
            if (frame_offset < block.frames) {
                const std::size_t written = ring_buffer_->write(
                    block.samples.data() + frame_offset
                        * static_cast<std::size_t>(channels_),
                    block.frames - frame_offset);
                frame_offset += written;
                if (written == 0U) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                continue;
            }

            const ag_result result = decoder_.read(block);
            frame_offset = 0U;
            if (result != AG_OK) {
                terminal_error_.store(result, std::memory_order_release);
                state_.store(EngineState::Error, std::memory_order_release);
                return;
            }
            if (block.end_of_stream) {
                decode_eof_.store(true, std::memory_order_release);
                return;
            }
        }
    }

    void shutdown_loaded_media() noexcept
    {
        state_.store(EngineState::Stopped, std::memory_order_release);
        if (device_initialized_) {
            ma_device_stop(&device_);
        }
        stop_decode_thread();
        if (device_initialized_) {
            ma_device_uninit(&device_);
            device_initialized_ = false;
        }
        if (context_initialized_) {
            ma_context_uninit(&context_);
            context_initialized_ = false;
        }
        decoder_.close();
        ring_buffer_.reset();
        loaded_ = false;
        sample_rate_ = 0;
        channels_ = 0;
        duration_ms_ = 0;
        consumed_frames_.store(0, std::memory_order_release);
        decode_eof_.store(false, std::memory_order_release);
        terminal_error_.store(AG_OK, std::memory_order_release);
        state_.store(EngineState::Stopped, std::memory_order_release);
    }

    [[nodiscard]] ag_result current_error() const noexcept
    {
        const ag_result result = terminal_error_.load(std::memory_order_acquire);
        return result == AG_OK ? AG_INTERNAL_ERROR : result;
    }

    ag_result fail_load(const ag_result result) noexcept
    {
        shutdown_loaded_media();
        terminal_error_.store(result, std::memory_order_release);
        state_.store(EngineState::Error, std::memory_order_release);
        return result;
    }

    ag_result enter_error(const ag_result result) noexcept
    {
        terminal_error_.store(result, std::memory_order_release);
        state_.store(EngineState::Error, std::memory_order_release);
        if (device_initialized_) {
            ma_device_stop(&device_);
        }
        stop_decode_thread();
        return result;
    }

    AudioBackend backend_;
    std::size_t buffer_frames_;
    Decoder decoder_;
    std::unique_ptr<PcmRingBuffer> ring_buffer_;
    ma_context context_{};
    ma_device device_{};
    bool context_initialized_ = false;
    bool device_initialized_ = false;
    bool loaded_ = false;
    int sample_rate_ = 0;
    int channels_ = 0;
    std::int64_t duration_ms_ = 0;
    std::thread decode_thread_;
    std::atomic<bool> stop_decode_{false};
    std::atomic<bool> decode_eof_{false};
    std::atomic<EngineState> state_{EngineState::Stopped};
    std::atomic<ag_result> terminal_error_{AG_OK};
    std::atomic<std::int64_t> consumed_frames_{0};
    std::atomic<float> volume_{1.0F};
    std::atomic<bool> muted_{false};
};

AudioEngine::AudioEngine(const AudioBackend backend,
                         const std::size_t buffer_frames)
    : impl_(std::make_unique<Impl>(backend, buffer_frames))
{
}

AudioEngine::~AudioEngine() = default;

ag_result AudioEngine::load(const std::string& utf8_path) noexcept
{
    try {
        return impl_->load(utf8_path);
    } catch (...) {
        return AG_INTERNAL_ERROR;
    }
}

ag_result AudioEngine::play() noexcept
{
    return impl_->play();
}

ag_result AudioEngine::pause() noexcept
{
    return impl_->pause();
}

ag_result AudioEngine::stop() noexcept
{
    return impl_->stop();
}

ag_result AudioEngine::seek(const std::int64_t position_ms) noexcept
{
    return impl_->seek(position_ms);
}

ag_result AudioEngine::set_volume(const float volume) noexcept
{
    return impl_->set_volume(volume);
}

void AudioEngine::set_muted(const bool muted) noexcept
{
    impl_->set_muted(muted);
}

EngineSnapshot AudioEngine::snapshot() const noexcept
{
    return impl_->snapshot();
}

} // namespace agplayer
