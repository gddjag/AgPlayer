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
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <random>
#include <thread>
#include <utility>

namespace agplayer {

static_assert(std::atomic<float>::is_always_lock_free);
static_assert(std::atomic<bool>::is_always_lock_free);
static_assert(std::atomic<std::size_t>::is_always_lock_free);
static_assert(std::atomic<std::int64_t>::is_always_lock_free);
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
static_assert(std::atomic<EngineState>::is_always_lock_free);
static_assert(std::atomic<PlaybackMode>::is_always_lock_free);
static_assert(std::atomic<ag_result>::is_always_lock_free);

class AudioEngine::Impl final {
public:
    Impl(const AudioBackend backend, const std::size_t buffer_frames)
        : backend_(backend)
        , buffer_frames_(buffer_frames == 0U ? 32'768U : buffer_frames)
        , session_([this](const std::size_t current,
                          const std::size_t count) {
            const std::lock_guard<std::mutex> lock(random_mutex_);
            if (count <= 1U) {
                return std::size_t{0U};
            }
            std::uniform_int_distribution<std::size_t> distribution(0U,
                                                                    count - 2U);
            const std::size_t candidate = distribution(random_);
            return candidate >= current ? candidate + 1U : candidate;
        })
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
            std::vector<std::string> queue;
            queue.push_back(utf8_path);
            return set_queue(std::move(queue), 0U);
        } catch (...) {
            return fail_load(AG_INTERNAL_ERROR);
        }
    }

    ag_result set_queue(std::vector<std::string> paths,
                        const std::size_t start_index) noexcept
    {
        if (paths.empty() || start_index >= paths.size()
            || std::any_of(paths.begin(), paths.end(), [](const std::string& path) {
                   return path.empty();
               })) {
            return AG_INVALID_ARGUMENT;
        }

        try {
            shutdown_loaded_media();
            state_.store(EngineState::Loading, std::memory_order_release);
            session_.set_queue(std::move(paths), start_index);
            decode_track_index_ = start_index;

            const ag_result decode_result = decoder_.open(session_.current_path());
            if (decode_result != AG_OK) {
                return fail_load(decode_result);
            }

            sample_rate_ = decoder_.metadata().sample_rate;
            channels_ = decoder_.metadata().channels;
            duration_ms_.store(decoder_.metadata().duration_ms,
                               std::memory_order_release);
            ring_buffer_ = std::make_unique<PcmRingBuffer>(
                buffer_frames_, static_cast<std::size_t>(channels_));

            const ag_result device_result = initialize_device();
            if (device_result != AG_OK) {
                return fail_load(device_result);
            }

            reset_timeline(0);
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
        if (!loaded_ || !output_ready()) {
            return AG_INVALID_ARGUMENT;
        }
        if (state == EngineState::Playing) {
            return AG_OK;
        }
        if (state == EngineState::Stopped
            && decode_eof_.load(std::memory_order_acquire)
            && ring_buffer_ != nullptr
            && ring_buffer_->available_frames() == 0U) {
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
        if (start_output() != AG_OK) {
            return enter_error(AG_DEVICE_ERROR);
        }
        if (state_.load(std::memory_order_acquire) == EngineState::Error) {
            const ag_result result = current_error();
            stop_output();
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
        if (!loaded_ || !output_ready()) {
            return AG_INVALID_ARGUMENT;
        }
        if (state == EngineState::Paused) {
            return AG_OK;
        }
        if (state != EngineState::Playing) {
            return AG_INVALID_ARGUMENT;
        }
        if (stop_output() != AG_OK) {
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
        if (stop_output() != AG_OK) {
            return enter_error(AG_DEVICE_ERROR);
        }

        stop_decode_thread();
        const ag_result restore_result = restore_published_decoder();
        if (restore_result != AG_OK) {
            return enter_error(restore_result);
        }
        const ag_result seek_result = decoder_.seek(0);
        if (seek_result != AG_OK) {
            return enter_error(seek_result);
        }
        ring_buffer_->clear();
        decode_track_index_ = session_.index();
        reset_timeline(0);
        terminal_error_.store(AG_OK, std::memory_order_release);
        state_.store(EngineState::Stopped, std::memory_order_release);
        const ag_result thread_result = start_decode_thread();
        if (thread_result != AG_OK) {
            return enter_error(thread_result);
        }
        return state_.load(std::memory_order_acquire) == EngineState::Error
                   ? current_error()
                   : AG_OK;
    }

    ag_result seek(const std::int64_t position_ms) noexcept
    {
        if (state_.load(std::memory_order_acquire) == EngineState::Error) {
            return current_error();
        }
        const std::int64_t duration = duration_ms_.load(std::memory_order_acquire);
        if (!loaded_ || position_ms < 0 || position_ms > duration) {
            return AG_INVALID_ARGUMENT;
        }

        // Fast path: the decode thread is running. Hand the seek off to it
        // via atomic flags so we avoid stopping/restarting the output device
        // and decode thread (saves ~30ms of ma_device_start/stop + thread
        // join/create overhead per seek). The output device keeps running;
        // render() emits silence while seeking_ is set.
        if (decode_running_.load(std::memory_order_acquire)) {
            seek_done_.store(false, std::memory_order_release);
            seek_target_ms_.store(position_ms, std::memory_order_release);
            seeking_.store(true, std::memory_order_release);
            seek_requested_.store(true, std::memory_order_release);
            seek_cv_.notify_one();

            // Wait for the decode thread to complete the seek. Using a
            // condition_variable instead of sleep_for() because Windows
            // timer granularity is ~15ms, which would make the polling loop
            // far slower than the actual seek (~0.02ms). The CV wakes
            // immediately when the decode thread signals completion.
            {
                std::unique_lock<std::mutex> lock(seek_mutex_);
                seek_cv_.wait_for(lock,
                    std::chrono::seconds(2),
                    [this] {
                        return seek_done_.load(std::memory_order_acquire)
                               || !decode_running_.load(
                                      std::memory_order_acquire);
                    });
            }

            if (seek_done_.load(std::memory_order_acquire)) {
                seek_done_.store(false, std::memory_order_release);
                seeking_.store(false, std::memory_order_release);
                const ag_result result =
                    seek_result_.load(std::memory_order_acquire);
                if (result != AG_OK) {
                    return enter_error(result);
                }
                return state_.load(std::memory_order_acquire)
                                == EngineState::Error
                           ? current_error()
                           : AG_OK;
            }
            // Timed out or thread died -- fall through to the slow path.
            seeking_.store(false, std::memory_order_release);
            seek_requested_.store(false, std::memory_order_release);
        }

        // Slow path: stop the decode thread and perform the seek on the main
        // thread. Used when the decode thread is not running (e.g. EOF,
        // stopped state) or the fast path bailed out.
        const EngineState previous_state = state_.load(std::memory_order_acquire);
        const bool resume = previous_state == EngineState::Playing;
        if (stop_output() != AG_OK) {
            return enter_error(AG_DEVICE_ERROR);
        }

        stop_decode_thread();
        const ag_result restore_result = restore_published_decoder();
        if (restore_result != AG_OK) {
            return enter_error(restore_result);
        }
        const ag_result seek_result = decoder_.seek(position_ms);
        if (seek_result != AG_OK) {
            return enter_error(seek_result);
        }
        ring_buffer_->clear();
        const std::int64_t position_frames =
            (position_ms * sample_rate_ + 999) / 1'000;
        decode_track_index_ = session_.index();
        reset_timeline(position_frames);
        terminal_error_.store(AG_OK, std::memory_order_release);
        state_.store(previous_state, std::memory_order_release);
        const ag_result thread_result = start_decode_thread();
        if (thread_result != AG_OK) {
            return enter_error(thread_result);
        }

        if (state_.load(std::memory_order_acquire) == EngineState::Error) {
            return current_error();
        }
        if (resume && start_output() != AG_OK) {
            return enter_error(AG_DEVICE_ERROR);
        }
        if (state_.load(std::memory_order_acquire) == EngineState::Error) {
            const ag_result result = current_error();
            stop_output();
            stop_decode_thread();
            return result;
        }
        return AG_OK;
    }

    ag_result next() noexcept
    {
        if (state_.load(std::memory_order_acquire) == EngineState::Error) {
            return current_error();
        }
        if (!loaded_) {
            return AG_INVALID_ARGUMENT;
        }
        try {
            const std::size_t index = session_.next_index();
            return index == PlaybackSession::npos ? AG_INVALID_ARGUMENT
                                                  : switch_track(index);
        } catch (...) {
            return enter_error(AG_INTERNAL_ERROR);
        }
    }

    ag_result previous() noexcept
    {
        if (state_.load(std::memory_order_acquire) == EngineState::Error) {
            return current_error();
        }
        if (!loaded_) {
            return AG_INVALID_ARGUMENT;
        }
        const std::size_t index = session_.previous_index();
        return index == PlaybackSession::npos ? AG_INVALID_ARGUMENT
                                              : switch_track(index);
    }

    ag_result set_mode(const PlaybackMode mode) noexcept
    {
        if (mode != PlaybackMode::Sequential
            && mode != PlaybackMode::RepeatOne
            && mode != PlaybackMode::Shuffle) {
            return AG_INVALID_ARGUMENT;
        }
        session_.set_mode(mode);
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
        for (;;) {
            const std::uint64_t version =
                transition_version_.load(std::memory_order_acquire);
            if ((version & 1U) != 0U) {
                continue;
            }

            const std::int64_t rendered =
                rendered_frames_total_.load(std::memory_order_acquire);
            const std::int64_t track_start =
                track_start_frame_.load(std::memory_order_acquire);
            const std::int64_t track_frames = std::max<std::int64_t>(
                0, rendered - track_start);
            const EngineSnapshot result{
                state_.load(std::memory_order_acquire),
                sample_rate_ <= 0 ? 0 : track_frames * 1'000 / sample_rate_,
                duration_ms_.load(std::memory_order_acquire),
                volume_.load(std::memory_order_acquire),
                muted_.load(std::memory_order_acquire),
                session_.index(),
                session_.size(),
                session_.mode(),
            };
            if (transition_version_.load(std::memory_order_acquire) == version) {
                return result;
            }
        }
    }

    void render(float* output, const std::size_t requested_frames) noexcept
    {
        if (output == nullptr || requested_frames == 0U) {
            return;
        }
        const std::size_t channels = static_cast<std::size_t>(channels_);
        if (state_.load(std::memory_order_acquire) != EngineState::Playing
            || device_lost_.load(std::memory_order_acquire)
            || seeking_.load(std::memory_order_acquire)) {
            std::fill(output, output + requested_frames * channels, 0.0F);
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

        const std::int64_t rendered = rendered_frames_total_.fetch_add(
                                          static_cast<std::int64_t>(frames),
                                          std::memory_order_acq_rel)
                                      + static_cast<std::int64_t>(frames);
        publish_pending_transition(rendered);

        if (frames < requested_frames
            && decode_eof_.load(std::memory_order_acquire)
            && ring_buffer_ != nullptr
            && ring_buffer_->available_frames() == 0U) {
            EngineState expected = EngineState::Playing;
            state_.compare_exchange_strong(expected,
                                           EngineState::Stopped,
                                           std::memory_order_acq_rel,
                                           std::memory_order_acquire);
        }
    }

    [[nodiscard]] std::size_t buffered_frames() const noexcept
    {
        return ring_buffer_ == nullptr ? 0U : ring_buffer_->available_frames();
    }

    [[nodiscard]] bool device_lost() const noexcept
    {
        return device_lost_.load(std::memory_order_acquire);
    }

    ag_result retry_device() noexcept
    {
        if (!device_lost_.load(std::memory_order_acquire)) {
            return AG_OK;
        }
        if (!loaded_) {
            // No media loaded: nothing to reinitialize. Clear the stale flag
            // so callers (and tests) see the recovered state. The device will
            // be initialized fresh on the next load().
            device_lost_.store(false, std::memory_order_release);
            terminal_error_.store(AG_OK, std::memory_order_release);
            state_.store(EngineState::Stopped, std::memory_order_release);
            return AG_OK;
        }
        try {
            if (device_initialized_) {
                // Stop the device before uninit regardless of whether the loss
                // was real (notification_callback only flips device_lost_ and
                // state_; it does NOT stop the device) or simulated
                // (simulate_device_loss already calls stop_output). Calling
                // stop_output() is safe even if the device is already stopped.
                stop_output();
                ma_device_uninit(&device_);
                device_initialized_ = false;
            }
            const ag_result device_result = initialize_device();
            if (device_result != AG_OK) {
                return device_result;
            }
            device_lost_.store(false, std::memory_order_release);
            terminal_error_.store(AG_OK, std::memory_order_release);
            state_.store(EngineState::Paused, std::memory_order_release);
            return AG_OK;
        } catch (...) {
            return AG_INTERNAL_ERROR;
        }
    }

    void simulate_device_loss() noexcept
    {
        device_lost_.store(true, std::memory_order_release);
        stop_output();
        state_.store(EngineState::Paused, std::memory_order_release);
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

    static void notification_callback(const ma_device_notification* notification) noexcept
    {
        if (notification == nullptr) {
            return;
        }
        auto* const self = static_cast<Impl*>(notification->pDevice->pUserData);
        if (self == nullptr) {
            return;
        }
        // ma_device_notification_type_disconnected is not available in the
        // miniaudio version shipped via vcpkg. interruption_began is the
        // closest equivalent and fires when the audio session is interrupted
        // (device unplugged, exclusive-mode takeover, etc.).
        if (notification->type == ma_device_notification_type_interruption_began) {
            self->device_lost_.store(true, std::memory_order_release);
            EngineState expected = EngineState::Playing;
            self->state_.compare_exchange_strong(expected,
                                                 EngineState::Paused,
                                                 std::memory_order_acq_rel,
                                                 std::memory_order_acquire);
        }
    }

    [[nodiscard]] bool output_ready() const noexcept
    {
        return backend_ == AudioBackend::Manual || device_initialized_;
    }

    ag_result start_output() noexcept
    {
        if (backend_ == AudioBackend::Manual) {
            return AG_OK;
        }
        return device_initialized_ && ma_device_start(&device_) == MA_SUCCESS
                   ? AG_OK
                   : AG_DEVICE_ERROR;
    }

    ag_result stop_output() noexcept
    {
        if (backend_ == AudioBackend::Manual || !device_initialized_) {
            return AG_OK;
        }
        return ma_device_stop(&device_) == MA_SUCCESS ? AG_OK : AG_DEVICE_ERROR;
    }

    ag_result initialize_device() noexcept
    {
        if (backend_ == AudioBackend::Manual) {
            return AG_OK;
        }

        if (!context_initialized_) {
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
        }

        ma_device_config config = ma_device_config_init(ma_device_type_playback);
        config.playback.format = ma_format_f32;
        config.playback.channels = static_cast<ma_uint32>(channels_);
        config.sampleRate = static_cast<ma_uint32>(sample_rate_);
        config.dataCallback = data_callback;
        config.notificationCallback = notification_callback;
        config.pUserData = this;
        const ma_result result = ma_device_init(&context_, &config, &device_);
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
        seek_requested_.store(false, std::memory_order_release);
        seek_done_.store(false, std::memory_order_release);
        seeking_.store(false, std::memory_order_release);
        decode_running_.store(false, std::memory_order_release);
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
        // Clear any pending seek request so a restarting thread does not pick
        // up a stale target from a previous seek.
        seek_requested_.store(false, std::memory_order_release);
        seeking_.store(false, std::memory_order_release);
        // Wake the decode thread if it is blocked on the seek CV.
        seek_cv_.notify_all();
        if (decode_thread_.joinable()) {
            decode_thread_.join();
        }
        decode_running_.store(false, std::memory_order_release);
    }

    void decode_loop() noexcept
    {
        struct RunningGuard {
            std::atomic<bool>& flag;
            explicit RunningGuard(std::atomic<bool>& f) noexcept : flag(f)
            {
                flag.store(true, std::memory_order_release);
            }
            ~RunningGuard() noexcept
            {
                flag.store(false, std::memory_order_release);
            }
        } guard{decode_running_};
        try {
            DecodedAudioBlock block;
            std::size_t frame_offset = 0U;
            while (!stop_decode_.load(std::memory_order_acquire)) {
            handle_top_of_loop:
                if (device_lost_.load(std::memory_order_acquire)) {
                    return;
                }
                // Handle a pending seek request from the main thread. The
                // decode thread owns the decoder and timeline, so performing
                // the seek here avoids stopping the output device and
                // recreating the decode thread.
                if (seek_requested_.load(std::memory_order_acquire)) {
                    const std::int64_t target_ms =
                        seek_target_ms_.load(std::memory_order_acquire);
                    ag_result result = restore_published_decoder();
                    if (result == AG_OK) {
                        result = decoder_.seek(target_ms);
                    }
                    if (result == AG_OK) {
                        ring_buffer_->clear();
                        const std::int64_t position_frames =
                            (target_ms * sample_rate_ + 999) / 1'000;
                        decode_track_index_ = session_.index();
                        reset_timeline(position_frames);
                        terminal_error_.store(AG_OK,
                                              std::memory_order_release);
                    }
                    seek_result_.store(result, std::memory_order_release);
                    seek_requested_.store(false,
                                         std::memory_order_release);
                    seeking_.store(false, std::memory_order_release);
                    seek_done_.store(true, std::memory_order_release);
                    seek_cv_.notify_one();
                    // Discard any pre-seek decoded data so the next read
                    // fetches fresh samples from the new position.
                    block = {};
                    frame_offset = 0U;
                    if (result != AG_OK) {
                        // Seek failed: exit so the main thread can enter
                        // the error state and restart the decode thread.
                        return;
                    }
                    continue;
                }
                if (frame_offset < block.frames) {
                    const std::size_t written = ring_buffer_->write(
                        block.samples.data() + frame_offset
                            * static_cast<std::size_t>(channels_),
                        block.frames - frame_offset);
                    frame_offset += written;
                    produced_frames_total_ += static_cast<std::int64_t>(written);
                    if (written == 0U) {
                        // Ring buffer full. Wait on the seek CV so that a
                        // seek request can wake us immediately instead of
                        // suffering Windows timer granularity (~15ms).
                        std::unique_lock<std::mutex> lock(seek_mutex_);
                        seek_cv_.wait_for(lock, std::chrono::milliseconds(10),
                            [this] {
                                return seek_requested_.load(
                                           std::memory_order_acquire)
                                       || stop_decode_.load(
                                              std::memory_order_acquire);
                            });
                    }
                    continue;
                }

                const ag_result result = decoder_.read(block);
                frame_offset = 0U;
                if (result != AG_OK) {
                    set_decode_error(result);
                    return;
                }
                if (!block.end_of_stream) {
                    continue;
                }

                const std::size_t next_index =
                    session_.next_index_from(decode_track_index_);
                if (next_index == PlaybackSession::npos) {
                    decode_eof_.store(true, std::memory_order_release);
                    return;
                }

                while (pending_boundary_frame_.load(std::memory_order_acquire)
                           != no_pending_boundary
                       && !stop_decode_.load(std::memory_order_acquire)) {
                    if (seek_requested_.load(std::memory_order_acquire)) {
                        // A seek was requested while waiting for a pending
                        // track transition. Abandon the transition so the
                        // seek handler at the top of the loop can run. The
                        // seek's reset_timeline() will also clear the
                        // pending boundary.
                        pending_boundary_frame_.store(no_pending_boundary,
                                                      std::memory_order_release);
                        goto handle_top_of_loop;
                    }
                    {
                        std::unique_lock<std::mutex> lock(seek_mutex_);
                        seek_cv_.wait_for(lock, std::chrono::milliseconds(1),
                            [this] {
                                return seek_requested_.load(
                                           std::memory_order_acquire)
                                       || stop_decode_.load(
                                              std::memory_order_acquire);
                            });
                    }
                }
                if (stop_decode_.load(std::memory_order_acquire)) {
                    return;
                }

                ag_result transition_result = AG_OK;
                if (next_index == decode_track_index_
                    && session_.mode() == PlaybackMode::RepeatOne) {
                    transition_result = decoder_.seek(0);
                } else {
                    transition_result = decoder_.open(session_.path_at(next_index),
                                                      sample_rate_,
                                                      channels_);
                }
                if (transition_result != AG_OK) {
                    pending_transition_error_.store(transition_result,
                                                    std::memory_order_relaxed);
                    pending_boundary_frame_.store(produced_frames_total_,
                                                  std::memory_order_release);
                    publish_pending_transition(
                        rendered_frames_total_.load(std::memory_order_acquire));
                    decode_eof_.store(true, std::memory_order_release);
                    return;
                }

                pending_transition_error_.store(AG_OK,
                                                std::memory_order_relaxed);
                pending_track_index_.store(next_index, std::memory_order_relaxed);
                pending_duration_ms_.store(decoder_.metadata().duration_ms,
                                           std::memory_order_relaxed);
                pending_boundary_frame_.store(produced_frames_total_,
                                              std::memory_order_release);
                decode_track_index_ = next_index;
                decode_eof_.store(false, std::memory_order_release);
                block = {};
            }
        } catch (...) {
            set_decode_error(AG_INTERNAL_ERROR);
        }
    }

    void publish_pending_transition(const std::int64_t rendered) noexcept
    {
        std::int64_t boundary =
            pending_boundary_frame_.load(std::memory_order_acquire);
        if (boundary < 0 || rendered < boundary) {
            return;
        }
        if (!pending_boundary_frame_.compare_exchange_strong(
                boundary,
                publishing_boundary,
                std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            return;
        }

        const ag_result transition_error =
            pending_transition_error_.load(std::memory_order_relaxed);
        if (transition_error != AG_OK) {
            terminal_error_.store(transition_error, std::memory_order_release);
            state_.store(EngineState::Error, std::memory_order_release);
            pending_transition_error_.store(AG_OK, std::memory_order_relaxed);
            pending_boundary_frame_.store(no_pending_boundary,
                                          std::memory_order_release);
            return;
        }

        transition_version_.fetch_add(1U, std::memory_order_acq_rel);
        track_start_frame_.store(boundary, std::memory_order_release);
        duration_ms_.store(pending_duration_ms_.load(std::memory_order_relaxed),
                           std::memory_order_release);
        session_.set_index(pending_track_index_.load(std::memory_order_relaxed));
        transition_version_.fetch_add(1U, std::memory_order_release);
        pending_boundary_frame_.store(no_pending_boundary,
                                      std::memory_order_release);
    }

    ag_result restore_published_decoder() noexcept
    {
        const std::size_t published_index = session_.index();
        if (decode_track_index_ == published_index && decoder_.is_open()) {
            return AG_OK;
        }
        const ag_result result = decoder_.open(session_.path_at(published_index),
                                               sample_rate_,
                                               channels_);
        if (result == AG_OK) {
            decode_track_index_ = published_index;
        }
        return result;
    }

    ag_result switch_track(const std::size_t index) noexcept
    {
        if (index >= session_.size()) {
            return AG_INVALID_ARGUMENT;
        }

        const EngineState previous_state = state_.load(std::memory_order_acquire);
        const bool resume = previous_state == EngineState::Playing;
        if (stop_output() != AG_OK) {
            return enter_error(AG_DEVICE_ERROR);
        }
        stop_decode_thread();

        const ag_result open_result = decoder_.open(session_.path_at(index),
                                                    sample_rate_,
                                                    channels_);
        if (open_result != AG_OK) {
            return enter_error(open_result);
        }

        session_.set_index(index);
        decode_track_index_ = index;
        duration_ms_.store(decoder_.metadata().duration_ms,
                           std::memory_order_release);
        ring_buffer_->clear();
        reset_timeline(0);
        terminal_error_.store(AG_OK, std::memory_order_release);
        state_.store(previous_state, std::memory_order_release);
        const ag_result thread_result = start_decode_thread();
        if (thread_result != AG_OK) {
            return enter_error(thread_result);
        }
        if (state_.load(std::memory_order_acquire) == EngineState::Error) {
            return current_error();
        }
        if (resume && start_output() != AG_OK) {
            return enter_error(AG_DEVICE_ERROR);
        }
        return state_.load(std::memory_order_acquire) == EngineState::Error
                   ? current_error()
                   : AG_OK;
    }

    void reset_timeline(const std::int64_t position_frames) noexcept
    {
        rendered_frames_total_.store(position_frames, std::memory_order_release);
        track_start_frame_.store(0, std::memory_order_release);
        produced_frames_total_ = position_frames;
        pending_boundary_frame_.store(no_pending_boundary,
                                      std::memory_order_release);
        pending_transition_error_.store(AG_OK, std::memory_order_release);
        pending_track_index_.store(session_.index(), std::memory_order_release);
        pending_duration_ms_.store(duration_ms_.load(std::memory_order_acquire),
                                   std::memory_order_release);
        decode_eof_.store(false, std::memory_order_release);
    }

    void set_decode_error(const ag_result result) noexcept
    {
        terminal_error_.store(result, std::memory_order_release);
        decode_eof_.store(true, std::memory_order_release);
        state_.store(EngineState::Error, std::memory_order_release);
    }

    void shutdown_loaded_media() noexcept
    {
        state_.store(EngineState::Stopped, std::memory_order_release);
        stop_output();
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
        session_.clear();
        loaded_ = false;
        sample_rate_ = 0;
        channels_ = 0;
        duration_ms_.store(0, std::memory_order_release);
        decode_track_index_ = 0U;
        reset_timeline(0);
        terminal_error_.store(AG_OK, std::memory_order_release);
        device_lost_.store(false, std::memory_order_release);
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
        stop_output();
        stop_decode_thread();
        return result;
    }

    AudioBackend backend_;
    static constexpr std::int64_t no_pending_boundary = -1;
    static constexpr std::int64_t publishing_boundary = -2;
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
    std::atomic<std::int64_t> duration_ms_{0};
    std::thread decode_thread_;
    std::atomic<bool> stop_decode_{false};
    std::atomic<bool> decode_eof_{false};
    std::atomic<bool> device_lost_{false};
    std::atomic<bool> decode_running_{false};
    std::atomic<bool> seeking_{false};
    std::atomic<bool> seek_requested_{false};
    std::atomic<std::int64_t> seek_target_ms_{0};
    std::atomic<ag_result> seek_result_{AG_OK};
    std::atomic<bool> seek_done_{false};
    std::mutex seek_mutex_;
    std::condition_variable seek_cv_;
    std::atomic<EngineState> state_{EngineState::Stopped};
    std::atomic<ag_result> terminal_error_{AG_OK};
    std::atomic<std::int64_t> rendered_frames_total_{0};
    std::atomic<std::int64_t> track_start_frame_{0};
    std::int64_t produced_frames_total_ = 0;
    std::atomic<std::int64_t> pending_boundary_frame_{no_pending_boundary};
    std::atomic<ag_result> pending_transition_error_{AG_OK};
    std::atomic<std::size_t> pending_track_index_{0U};
    std::atomic<std::int64_t> pending_duration_ms_{0};
    std::atomic<float> volume_{1.0F};
    std::atomic<bool> muted_{false};
    std::atomic<std::uint64_t> transition_version_{0U};
    std::mutex random_mutex_;
    std::mt19937 random_{std::random_device{}()};
    PlaybackSession session_;
    std::size_t decode_track_index_ = 0U;
};

AudioEngine::AudioEngine(const AudioBackend backend,
                         const std::size_t buffer_frames)
    : impl_(std::make_unique<Impl>(backend, buffer_frames))
{
}

AudioEngine::~AudioEngine() = default;

ag_result AudioEngine::load(const std::string& utf8_path) noexcept
{
    return impl_->load(utf8_path);
}

ag_result AudioEngine::set_queue(std::vector<std::string> utf8_paths,
                                 const std::size_t start_index) noexcept
{
    return impl_->set_queue(std::move(utf8_paths), start_index);
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

ag_result AudioEngine::next() noexcept
{
    return impl_->next();
}

ag_result AudioEngine::previous() noexcept
{
    return impl_->previous();
}

ag_result AudioEngine::set_mode(const PlaybackMode mode) noexcept
{
    return impl_->set_mode(mode);
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

void AudioEngine::render(float* output,
                         const std::size_t requested_frames) noexcept
{
    impl_->render(output, requested_frames);
}

std::size_t AudioEngine::buffered_frames() const noexcept
{
    return impl_->buffered_frames();
}

bool AudioEngine::device_lost() const noexcept
{
    return impl_->device_lost();
}

ag_result AudioEngine::retry_device() noexcept
{
    return impl_->retry_device();
}

void AudioEngine::simulate_device_loss() noexcept
{
    impl_->simulate_device_loss();
}

} // namespace agplayer
