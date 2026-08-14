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
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <complex>
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
static_assert(std::atomic<int>::is_always_lock_free);
static_assert(std::atomic<EngineState>::is_always_lock_free);
static_assert(std::atomic<PlaybackMode>::is_always_lock_free);
static_assert(std::atomic<ag_result>::is_always_lock_free);

namespace {

constexpr std::size_t spectrum_fft_size = 512U;
constexpr std::size_t spectrum_tap_capacity = 8'192U;
constexpr std::size_t spectrum_max_bins = spectrum_fft_size / 2U;
constexpr float spectrum_pi = 3.14159265358979323846F;

void fft(std::array<std::complex<float>, spectrum_fft_size>& values) noexcept
{
    for (std::size_t index = 1U, reversed = 0U;
         index < spectrum_fft_size; ++index) {
        std::size_t bit = spectrum_fft_size >> 1U;
        while ((reversed & bit) != 0U) {
            reversed ^= bit;
            bit >>= 1U;
        }
        reversed ^= bit;
        if (index < reversed) {
            std::swap(values[index], values[reversed]);
        }
    }

    for (std::size_t length = 2U; length <= spectrum_fft_size;
         length <<= 1U) {
        const float angle =
            -2.0F * spectrum_pi / static_cast<float>(length);
        const std::complex<float> step(std::cos(angle), std::sin(angle));
        for (std::size_t offset = 0U; offset < spectrum_fft_size;
             offset += length) {
            std::complex<float> phase(1.0F, 0.0F);
            const std::size_t half = length / 2U;
            for (std::size_t index = 0U; index < half; ++index) {
                const std::complex<float> even = values[offset + index];
                const std::complex<float> odd =
                    values[offset + index + half] * phase;
                values[offset + index] = even + odd;
                values[offset + index + half] = even - odd;
                phase *= step;
            }
        }
    }
}

std::string encode_device_id(std::string prefix,
                             const void* data,
                             const std::size_t size)
{
    static constexpr char hex[] = "0123456789abcdef";
    const auto* bytes = static_cast<const unsigned char*>(data);
    std::string result(std::move(prefix));
    result.reserve(result.size() + size * 2U);
    for (std::size_t index = 0U; index < size; ++index) {
        result.push_back(hex[bytes[index] >> 4U]);
        result.push_back(hex[bytes[index] & 0x0FU]);
    }
    return result;
}

std::string device_id_token(const ma_backend backend,
                            const ma_device_id& id)
{
    if (backend == ma_backend_wasapi) {
        std::size_t length = 0U;
        while (length < sizeof(id.wasapi) / sizeof(id.wasapi[0])
               && id.wasapi[length] != 0) {
            ++length;
        }
        return encode_device_id(
            "wasapi:", id.wasapi,
            length * sizeof(id.wasapi[0]));
    }
    if (backend == ma_backend_dsound) {
        return encode_device_id("dsound:", id.dsound, sizeof(id.dsound));
    }
    if (backend == ma_backend_winmm) {
        return encode_device_id("winmm:", &id.winmm, sizeof(id.winmm));
    }
    if (backend == ma_backend_null) {
        return encode_device_id(
            "null:", &id.nullbackend, sizeof(id.nullbackend));
    }
    if (backend == ma_backend_alsa) {
        return std::string("alsa:") + id.alsa;
    }
    if (backend == ma_backend_pulseaudio) {
        return std::string("pulse:") + id.pulse;
    }
    if (backend == ma_backend_coreaudio) {
        return std::string("coreaudio:") + id.coreaudio;
    }
    return encode_device_id(
        "backend:" + std::to_string(static_cast<int>(backend)) + ":",
        &id, sizeof(id));
}

} // namespace

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
        const std::lock_guard<std::recursive_mutex> control_lock(
            control_mutex_);
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
        return set_queue_impl(std::move(paths), start_index, 0U, false);
    }

    ag_result set_scoped_queue(std::vector<std::string> paths,
                               const std::size_t start_index,
                               const std::size_t scope_size,
                               const bool allow_fallback) noexcept
    {
        if (scope_size == 0U || scope_size > paths.size()
            || start_index >= scope_size) {
            return AG_INVALID_ARGUMENT;
        }
        return set_queue_impl(std::move(paths), start_index, scope_size,
                              allow_fallback);
    }

    ag_result set_queue_impl(std::vector<std::string> paths,
                             const std::size_t start_index,
                             const std::size_t scope_size,
                             const bool allow_fallback) noexcept
    {
        if (paths.empty() || start_index >= paths.size()
            || std::any_of(paths.begin(), paths.end(), [](const std::string& path) {
                   return path.empty();
               })) {
            return AG_INVALID_ARGUMENT;
        }

        const std::lock_guard<std::recursive_mutex> control_lock(
            control_mutex_);
        try {
            shutdown_loaded_media();
            state_.store(EngineState::Loading, std::memory_order_release);
            if (scope_size > 0U) {
                session_.set_scoped_queue(std::move(paths), start_index,
                                          scope_size, allow_fallback);
            } else {
                session_.set_queue(std::move(paths), start_index);
            }
            decode_track_index_ = start_index;

            const ag_result decode_result = decoder_.open(session_.current_path());
            if (decode_result != AG_OK) {
                return fail_load(decode_result);
            }

            sample_rate_.store(decoder_.metadata().sample_rate,
                               std::memory_order_release);
            publish_equalizer_for_rate(decoder_.metadata().sample_rate);
            channels_ = decoder_.metadata().channels;
            duration_ms_.store(decoder_.metadata().duration_ms,
                               std::memory_order_release);
            constexpr std::size_t transition_capacity = 96'001U;
            ring_buffer_ = std::make_unique<PcmRingBuffer>(
                (std::max)(buffer_frames_, transition_capacity),
                static_cast<std::size_t>(channels_));

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
        const std::lock_guard<std::recursive_mutex> control_lock(
            control_mutex_);
        std::unique_lock<std::recursive_mutex> device_lock(device_mutex_);
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
            device_lock.unlock();
            return enter_error(AG_DEVICE_ERROR);
        }
        if (state_.load(std::memory_order_acquire) == EngineState::Error) {
            const ag_result result = current_error();
            stop_output();
            device_lock.unlock();
            stop_decode_thread();
            return result;
        }
        return AG_OK;
    }

    ag_result pause() noexcept
    {
        const std::lock_guard<std::recursive_mutex> control_lock(
            control_mutex_);
        std::unique_lock<std::recursive_mutex> device_lock(device_mutex_);
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
            device_lock.unlock();
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
        const std::lock_guard<std::recursive_mutex> control_lock(
            control_mutex_);
        if (state_.load(std::memory_order_acquire) == EngineState::Error) {
            return current_error();
        }
        if (!loaded_) {
            return AG_INVALID_ARGUMENT;
        }
        state_.store(EngineState::Stopped, std::memory_order_release);
        stop_decode_thread();
        if (stop_output() != AG_OK) {
            return enter_error(AG_DEVICE_ERROR);
        }

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
        const std::lock_guard<std::recursive_mutex> control_lock(
            control_mutex_);
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
            (position_ms
                 * sample_rate_.load(std::memory_order_acquire)
             + 999)
            / 1'000;
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
        const std::lock_guard<std::recursive_mutex> control_lock(
            control_mutex_);
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
        const std::lock_guard<std::recursive_mutex> control_lock(
            control_mutex_);
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
            && mode != PlaybackMode::Shuffle
            && mode != PlaybackMode::RepeatAll) {
            return AG_INVALID_ARGUMENT;
        }
        const std::lock_guard<std::recursive_mutex> control_lock(
            control_mutex_);
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

    ag_result set_equalizer(const GraphicEqSettings& settings,
                            const std::uint64_t revision) noexcept
    {
        const int current_rate = sample_rate_.load(std::memory_order_acquire);
        const int validation_rate =
            is_graphic_eq_sample_rate_supported(current_rate)
                ? current_rate
                : 48'000;
        const auto program =
            prepare_graphic_eq(settings, validation_rate, revision);
        if (!program.has_value()) {
            return AG_INVALID_ARGUMENT;
        }
        {
            const std::lock_guard<std::mutex> lock(equalizer_settings_mutex_);
            equalizer_settings_ = settings;
            equalizer_revision_ = revision;
        }
        equalizer_revision_status_.store(revision, std::memory_order_release);
        equalizer_enabled_status_.store(settings.enabled,
                                        std::memory_order_release);
        equalizer_bypassed_status_.store(settings.bypassed,
                                         std::memory_order_release);
        equalizer_auto_protection_status_.store(
            settings.auto_clip_protection, std::memory_order_release);
        equalizer_protection_status_.store(program->protection_db,
                                           std::memory_order_release);
        equalizer_active_status_.store(
            settings.enabled && !settings.bypassed
                && is_graphic_eq_sample_rate_supported(current_rate),
            std::memory_order_release);
        if (is_graphic_eq_sample_rate_supported(current_rate)
            && !equalizer_.submit(*program)) {
            return AG_INTERNAL_ERROR;
        }
        return AG_OK;
    }

    [[nodiscard]] EqualizerStatus equalizer_status() const noexcept
    {
        return {
            equalizer_revision_status_.load(std::memory_order_acquire),
            equalizer_enabled_status_.load(std::memory_order_acquire),
            equalizer_bypassed_status_.load(std::memory_order_acquire),
            equalizer_auto_protection_status_.load(std::memory_order_acquire),
            equalizer_sample_rate_status_.load(std::memory_order_acquire),
            equalizer_active_status_.load(std::memory_order_acquire),
            equalizer_protection_status_.load(std::memory_order_acquire)};
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
            std::int64_t track_frames = std::max<std::int64_t>(
                0, rendered - track_start);
            const int sample_rate =
                sample_rate_.load(std::memory_order_acquire);
            const EngineSnapshot result{
                state_.load(std::memory_order_acquire),
                sample_rate <= 0 ? 0 : track_frames * 1'000 / sample_rate,
                duration_ms_.load(std::memory_order_acquire),
                sample_rate,
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

        const std::int64_t render_start =
            rendered_frames_total_.load(std::memory_order_acquire);
        const std::size_t frames = ring_buffer_ == nullptr
                                       ? 0U
                                       : ring_buffer_->read(output, requested_frames);
        equalizer_.process(output, frames, channels_);
        const float gain = muted_.load(std::memory_order_relaxed)
                               ? 0.0F
                               : volume_.load(std::memory_order_relaxed)
                                     * replay_gain_linear_.load(
                                         std::memory_order_relaxed);
        const int fade_ms =
            transition_fade_ms_.load(std::memory_order_acquire);
        const int sample_rate =
            sample_rate_.load(std::memory_order_acquire);
        const std::int64_t fade_frames =
            fade_ms > 0 ? static_cast<std::int64_t>(sample_rate) * fade_ms
                              / 1'000
                        : 0;
        const std::int64_t fade_boundary =
            fade_boundary_frame_.load(std::memory_order_acquire);
        for (std::size_t frame = 0U; frame < frames; ++frame) {
            float transition_gain = 1.0F;
            if (fade_frames > 0 && fade_boundary >= 0) {
                const std::int64_t absolute_frame =
                    render_start + static_cast<std::int64_t>(frame);
                if (absolute_frame < fade_boundary
                    && absolute_frame >= fade_boundary - fade_frames) {
                    transition_gain = static_cast<float>(
                        fade_boundary - absolute_frame)
                                      / static_cast<float>(fade_frames);
                } else if (absolute_frame >= fade_boundary
                           && absolute_frame
                                  < fade_boundary + fade_frames) {
                    transition_gain = static_cast<float>(
                        absolute_frame - fade_boundary + 1)
                                      / static_cast<float>(fade_frames);
                }
            }
            for (std::size_t channel = 0U; channel < channels; ++channel) {
                output[frame * channels + channel] *=
                    gain * transition_gain;
            }
        }
        tap_spectrum(output, frames, channels);
        std::fill(output + frames * channels,
                  output + requested_frames * channels,
                  0.0F);

        const std::int64_t rendered = rendered_frames_total_.fetch_add(
                                          static_cast<std::int64_t>(frames),
                                          std::memory_order_acq_rel)
                                      + static_cast<std::int64_t>(frames);
        publish_pending_transition(rendered);
        if (fade_frames > 0 && fade_boundary >= 0
            && rendered >= fade_boundary + fade_frames) {
            std::int64_t expected = fade_boundary;
            fade_boundary_frame_.compare_exchange_strong(
                expected, no_pending_boundary,
                std::memory_order_acq_rel,
                std::memory_order_acquire);
        }

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
        const std::lock_guard<std::recursive_mutex> control_lock(
            control_mutex_);
        const std::lock_guard<std::recursive_mutex> device_lock(
            device_mutex_);
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
        // No try/catch needed: stop_output, ma_device_uninit and
        // initialize_device are all noexcept (C functions + noexcept method),
        // and atomic stores cannot throw.
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
        seek_cv_.notify_all();
        return AG_OK;
    }

    void simulate_device_loss() noexcept
    {
        const std::lock_guard<std::recursive_mutex> control_lock(
            control_mutex_);
        device_lost_.store(true, std::memory_order_release);
        terminal_error_.store(AG_DEVICE_ERROR,
                              std::memory_order_release);
        stop_output();
        state_.store(EngineState::Error, std::memory_order_release);
    }

    ag_result queue_next(std::string path) noexcept
    {
        if (path.empty()) {
            return AG_INVALID_ARGUMENT;
        }
        const std::lock_guard<std::recursive_mutex> control_lock(
            control_mutex_);
        if (!loaded_) {
            return AG_INVALID_ARGUMENT;
        }
        try {
            stop_decode_thread();
            if (!session_.queue_next(std::move(path))) {
                const ag_result restart = start_decode_thread();
                return restart == AG_OK ? AG_INVALID_ARGUMENT : restart;
            }
            return start_decode_thread();
        } catch (...) {
            return enter_error(AG_INTERNAL_ERROR);
        }
    }

    [[nodiscard]] std::vector<OutputDevice> output_devices() noexcept
    {
        const std::lock_guard<std::recursive_mutex> device_lock(
            device_mutex_);
        if (backend_ == AudioBackend::Manual) {
            return {};
        }
        try {
            if (initialize_context() != AG_OK) {
                return {};
            }
            ma_device_info* devices = nullptr;
            ma_uint32 count = 0U;
            if (ma_context_get_devices(
                    &context_, &devices, &count, nullptr, nullptr)
                != MA_SUCCESS) {
                return {};
            }
            std::vector<OutputDevice> result;
            result.reserve(static_cast<std::size_t>(count));
            for (ma_uint32 index = 0U; index < count; ++index) {
                if (devices[index].name[0] != '\0') {
                    result.push_back(
                        {device_id_token(context_.backend,
                                         devices[index].id),
                         devices[index].name});
                }
            }
            return result;
        } catch (...) {
            return {};
        }
    }

    ag_result spectrum(float* bins, const std::size_t bin_count) noexcept
    {
        if (bins == nullptr || bin_count == 0U
            || bin_count > spectrum_max_bins) {
            return AG_INVALID_ARGUMENT;
        }

        std::array<float, spectrum_fft_size> drained{};
        for (;;) {
            const std::size_t frames =
                spectrum_tap_.read(drained.data(), drained.size());
            if (frames == 0U) {
                break;
            }
            for (std::size_t index = 0U; index < frames; ++index) {
                spectrum_history_[spectrum_history_write_] = drained[index];
                spectrum_history_write_ =
                    (spectrum_history_write_ + 1U) % spectrum_fft_size;
                spectrum_history_filled_ =
                    (std::min)(spectrum_history_filled_ + 1U,
                               spectrum_fft_size);
            }
        }

        std::array<std::complex<float>, spectrum_fft_size> values{};
        const std::size_t missing =
            spectrum_fft_size - spectrum_history_filled_;
        for (std::size_t index = 0U; index < spectrum_history_filled_;
             ++index) {
            const std::size_t history_index =
                (spectrum_history_write_ + missing + index)
                % spectrum_fft_size;
            const float phase =
                2.0F * spectrum_pi * static_cast<float>(missing + index)
                / static_cast<float>(spectrum_fft_size - 1U);
            const float window = 0.5F - 0.5F * std::cos(phase);
            values[missing + index] =
                std::complex<float>(spectrum_history_[history_index] * window,
                                    0.0F);
        }
        fft(values);

        const bool playing =
            state_.load(std::memory_order_acquire) == EngineState::Playing;
        for (std::size_t index = 0U; index < bin_count; ++index) {
            const float magnitude =
                4.0F * std::abs(values[index + 1U])
                / static_cast<float>(spectrum_fft_size);
            const float normalized = std::clamp(
                std::log1p(magnitude * 12.0F) / std::log(13.0F),
                0.0F, 1.0F);
            const float target = playing ? normalized : 0.0F;
            const float factor =
                target > spectrum_smoothed_[index] ? 0.25F : 0.82F;
            spectrum_smoothed_[index] =
                target + (spectrum_smoothed_[index] - target) * factor;
            bins[index] = spectrum_smoothed_[index];
        }
        return AG_OK;
    }

    ag_result set_output_device(std::string utf8_id,
                                const bool exclusive) noexcept
    {
        if (backend_ == AudioBackend::Manual) {
            return utf8_id.empty() && !exclusive ? AG_OK
                                                  : AG_INVALID_ARGUMENT;
        }
        const std::lock_guard<std::recursive_mutex> control_lock(
            control_mutex_);
        const std::lock_guard<std::recursive_mutex> device_lock(
            device_mutex_);
        try {
            if (!utf8_id.empty()) {
                const std::vector<OutputDevice> devices = output_devices();
                const auto found = std::find_if(
                    devices.begin(), devices.end(),
                    [&utf8_id](const OutputDevice& device) {
                        return device.id == utf8_id;
                    });
                if (found == devices.end()) {
                    return AG_INVALID_ARGUMENT;
                }
            }
            if (selected_device_id_ == utf8_id
                && exclusive_mode_ == exclusive
                && (!loaded_ || device_initialized_)
                && !device_lost_.load(std::memory_order_acquire)) {
                return AG_OK;
            }

            const std::string previous_id = selected_device_id_;
            const bool previous_exclusive = exclusive_mode_;
            const EngineState previous_state =
                state_.load(std::memory_order_acquire);
            const bool resume = previous_state == EngineState::Playing;
            const bool recovering =
                device_lost_.load(std::memory_order_acquire);

            if (device_initialized_) {
                if (stop_output() != AG_OK && !recovering) {
                    return AG_DEVICE_ERROR;
                }
                ma_device_uninit(&device_);
                device_initialized_ = false;
            }

            selected_device_id_ = std::move(utf8_id);
            exclusive_mode_ = exclusive;
            if (!loaded_) {
                return AG_OK;
            }

            const ag_result result = initialize_device();
            if (result == AG_OK) {
                device_lost_.store(false, std::memory_order_release);
                if (previous_state != EngineState::Error || recovering) {
                    terminal_error_.store(AG_OK,
                                          std::memory_order_release);
                }
                if (previous_state == EngineState::Error && recovering) {
                    state_.store(loaded_ ? EngineState::Paused
                                         : EngineState::Stopped,
                                 std::memory_order_release);
                }
                seek_cv_.notify_all();
                if (!resume || start_output() == AG_OK) {
                    return AG_OK;
                }
            }

            if (device_initialized_) {
                ma_device_uninit(&device_);
                device_initialized_ = false;
            }
            selected_device_id_ = previous_id;
            exclusive_mode_ = previous_exclusive;
            if (initialize_device() == AG_OK) {
                device_lost_.store(false, std::memory_order_release);
                seek_cv_.notify_all();
                if (!resume || start_output() == AG_OK) {
                    return AG_DEVICE_ERROR;
                }
            }
            device_lost_.store(true, std::memory_order_release);
            terminal_error_.store(AG_DEVICE_ERROR,
                                  std::memory_order_release);
            state_.store(EngineState::Error, std::memory_order_release);
            return AG_DEVICE_ERROR;
        } catch (...) {
            return AG_INTERNAL_ERROR;
        }
    }

    [[nodiscard]] bool exclusive_mode_active() const noexcept
    {
        return device_initialized_ && active_exclusive_mode_;
    }

    ag_result set_transition_fade_ms(const int milliseconds) noexcept
    {
        if (milliseconds != 0 && milliseconds != 200
            && milliseconds != 500) {
            return AG_INVALID_ARGUMENT;
        }
        transition_fade_ms_.store(milliseconds,
                                  std::memory_order_release);
        if (milliseconds == 0) {
            fade_boundary_frame_.store(no_pending_boundary,
                                       std::memory_order_release);
        }
        return AG_OK;
    }

    ag_result set_duration_ms(const std::int64_t duration_ms) noexcept
    {
        if (!loaded_ || duration_ms <= 0) {
            return AG_INVALID_ARGUMENT;
        }
        duration_ms_.store(duration_ms, std::memory_order_release);
        return AG_OK;
    }

    ag_result set_match_track_sample_rate(const bool enabled) noexcept
    {
        match_track_sample_rate_.store(enabled, std::memory_order_release);
        return AG_OK;
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
            self->terminal_error_.store(AG_DEVICE_ERROR,
                                        std::memory_order_release);
            self->state_.store(EngineState::Error,
                               std::memory_order_release);
        }
    }

    [[nodiscard]] bool output_ready() const noexcept
    {
        return backend_ == AudioBackend::Manual || device_initialized_;
    }

    ag_result start_output() noexcept
    {
        const std::lock_guard<std::recursive_mutex> lock(device_mutex_);
        if (backend_ == AudioBackend::Manual) {
            return AG_OK;
        }
        return device_initialized_ && ma_device_start(&device_) == MA_SUCCESS
                   ? AG_OK
                   : AG_DEVICE_ERROR;
    }

    ag_result stop_output() noexcept
    {
        const std::lock_guard<std::recursive_mutex> lock(device_mutex_);
        if (backend_ == AudioBackend::Manual || !device_initialized_) {
            return AG_OK;
        }
        return ma_device_stop(&device_) == MA_SUCCESS ? AG_OK : AG_DEVICE_ERROR;
    }

    ag_result initialize_context() noexcept
    {
        const std::lock_guard<std::recursive_mutex> lock(device_mutex_);
        if (backend_ == AudioBackend::Manual) {
            return AG_OK;
        }
        if (context_initialized_) {
            return AG_OK;
        }
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
        return AG_OK;
    }

    ag_result initialize_device() noexcept
    {
        const std::lock_guard<std::recursive_mutex> lock(device_mutex_);
        if (backend_ == AudioBackend::Manual) {
            return AG_OK;
        }
        if (initialize_context() != AG_OK) {
            return AG_DEVICE_ERROR;
        }

        ma_device_id selected_id{};
        const ma_device_id* selected_id_ptr = nullptr;
        if (!selected_device_id_.empty()) {
            ma_device_info* devices = nullptr;
            ma_uint32 count = 0U;
            if (ma_context_get_devices(
                    &context_, &devices, &count, nullptr, nullptr)
                != MA_SUCCESS) {
                return AG_DEVICE_ERROR;
            }
            for (ma_uint32 index = 0U; index < count; ++index) {
                if (selected_device_id_
                    == device_id_token(context_.backend,
                                       devices[index].id)) {
                    selected_id = devices[index].id;
                    selected_id_ptr = &selected_id;
                    break;
                }
            }
            if (selected_id_ptr == nullptr) {
                return AG_DEVICE_ERROR;
            }
        }
        ma_device_config config = ma_device_config_init(ma_device_type_playback);
        config.playback.pDeviceID = selected_id_ptr;
        config.playback.format = ma_format_f32;
        config.playback.channels = static_cast<ma_uint32>(channels_);
        config.sampleRate = static_cast<ma_uint32>(
            sample_rate_.load(std::memory_order_acquire));
        config.dataCallback = data_callback;
        config.notificationCallback = notification_callback;
        config.pUserData = this;
        config.playback.shareMode =
            exclusive_mode_ ? ma_share_mode_exclusive : ma_share_mode_shared;
        ma_result result = ma_device_init(&context_, &config, &device_);
        active_exclusive_mode_ = exclusive_mode_ && result == MA_SUCCESS;
        if (result != MA_SUCCESS && exclusive_mode_) {
            config.playback.shareMode = ma_share_mode_shared;
            result = ma_device_init(&context_, &config, &device_);
            active_exclusive_mode_ = false;
        }
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
                if (device_lost_.load(std::memory_order_acquire)) {
                    std::unique_lock<std::mutex> lock(seek_mutex_);
                    seek_cv_.wait(lock, [this] {
                        return !device_lost_.load(
                                   std::memory_order_acquire)
                               || stop_decode_.load(
                                   std::memory_order_acquire);
                    });
                    continue;
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
                            (target_ms
                                 * sample_rate_.load(
                                     std::memory_order_acquire)
                             + 999)
                            / 1'000;
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
                    produced_frames_total_.fetch_add(static_cast<std::int64_t>(written),
                                                     std::memory_order_relaxed);
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
                    // Container metadata can overstate the duration of VBR
                    // streams. At end of decode, the emitted PCM frame count
                    // is authoritative and must drive the player timeline,
                    // waveform clipping and seek mapping.
                    const int sample_rate =
                        sample_rate_.load(std::memory_order_acquire);
                    const std::int64_t produced_frames =
                        produced_frames_total_.load(std::memory_order_acquire);
                    const std::int64_t pending_boundary =
                        pending_boundary_frame_.load(std::memory_order_acquire);
                    if (sample_rate > 0) {
                        if (pending_boundary >= 0) {
                            // The next track may already be decoded while
                            // the render thread still publishes the previous
                            // track. Preserve its exact duration for that
                            // pending hand-off rather than overwriting the
                            // current row's timeline.
                            const std::int64_t pending_frames =
                                std::max<std::int64_t>(
                                    0, produced_frames - pending_boundary);
                            pending_duration_ms_.store(
                                pending_frames * 1'000 / sample_rate,
                                std::memory_order_release);
                        } else {
                            const std::int64_t track_start =
                                track_start_frame_.load(
                                    std::memory_order_acquire);
                            const std::int64_t track_frames =
                                std::max<std::int64_t>(
                                    0, produced_frames - track_start);
                            duration_ms_.store(
                                track_frames * 1'000 / sample_rate,
                                std::memory_order_release);
                        }
                    }
                    decode_eof_.store(true, std::memory_order_release);
                    return;
                }

                bool seek_preempted = false;
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
                        seek_preempted = true;
                        break;
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
                if (seek_preempted) {
                    continue;
                }
                if (stop_decode_.load(std::memory_order_acquire)) {
                    return;
                }

                ag_result transition_result = AG_OK;
                const int current_sample_rate =
                    sample_rate_.load(std::memory_order_acquire);
                int next_sample_rate = current_sample_rate;
                bool needs_device_reconfigure = false;
                if (next_index == decode_track_index_
                    && session_.mode() == PlaybackMode::RepeatOne) {
                    transition_result = decoder_.seek(0);
                } else if (match_track_sample_rate_.load(
                               std::memory_order_acquire)) {
                    transition_result =
                        decoder_.open(session_.path_at(next_index));
                    if (transition_result == AG_OK) {
                        next_sample_rate = decoder_.metadata().sample_rate;
                        needs_device_reconfigure =
                            next_sample_rate != current_sample_rate;
                        if (decoder_.metadata().channels != channels_) {
                            transition_result = decoder_.open(
                                session_.path_at(next_index),
                                next_sample_rate,
                                channels_);
                        }
                        if (transition_result == AG_OK
                            && needs_device_reconfigure) {
                            decode_track_index_ = next_index;
                        }
                    }
                } else {
                    transition_result = decoder_.open(session_.path_at(next_index),
                                                      current_sample_rate,
                                                      channels_);
                }
                if (transition_result != AG_OK) {
                    pending_transition_error_.store(transition_result,
                                                    std::memory_order_relaxed);
                    const std::int64_t boundary =
                        produced_frames_total_.load(
                            std::memory_order_relaxed);
                    fade_boundary_frame_.store(boundary,
                                               std::memory_order_release);
                    pending_boundary_frame_.store(boundary,
                                                  std::memory_order_release);
                    publish_pending_transition(
                        rendered_frames_total_.load(std::memory_order_acquire));
                    decode_eof_.store(true, std::memory_order_release);
                    return;
                }

                if (needs_device_reconfigure) {
                    const std::int64_t boundary =
                        produced_frames_total_.load(
                            std::memory_order_acquire);
                    fade_boundary_frame_.store(boundary,
                                               std::memory_order_release);
                    while (rendered_frames_total_.load(
                               std::memory_order_acquire)
                               < boundary
                           && !stop_decode_.load(
                               std::memory_order_acquire)
                           && !seek_requested_.load(
                               std::memory_order_acquire)) {
                        std::unique_lock<std::mutex> lock(seek_mutex_);
                        seek_cv_.wait_for(lock, std::chrono::milliseconds(1));
                    }
                    if (seek_requested_.load(std::memory_order_acquire)) {
                        continue;
                    }
                    if (stop_decode_.load(std::memory_order_acquire)) {
                        return;
                    }

                    const std::lock_guard<std::recursive_mutex> device_lock(
                        device_mutex_);
                    if (seek_requested_.load(std::memory_order_acquire)) {
                        continue;
                    }
                    if (stop_decode_.load(std::memory_order_acquire)) {
                        return;
                    }
                    const bool resume =
                        state_.load(std::memory_order_acquire)
                        == EngineState::Playing;
                    if (stop_output() != AG_OK) {
                        set_decode_error(AG_DEVICE_ERROR);
                        return;
                    }
                    if (device_initialized_) {
                        ma_device_uninit(&device_);
                        device_initialized_ = false;
                    }

                    transition_version_.fetch_add(
                        1U, std::memory_order_acq_rel);
                    sample_rate_.store(next_sample_rate,
                                       std::memory_order_release);
                    publish_equalizer_for_rate(next_sample_rate);
                    session_.set_index(next_index);
                    decode_track_index_ = next_index;
                    duration_ms_.store(
                        decoder_.metadata().duration_ms,
                        std::memory_order_release);
                    ring_buffer_->clear();
                    reset_timeline(0);
                    if (transition_fade_ms_.load(
                            std::memory_order_acquire)
                        > 0) {
                        fade_boundary_frame_.store(
                            0, std::memory_order_release);
                    }
                    transition_version_.fetch_add(
                        1U, std::memory_order_release);

                    if (initialize_device() != AG_OK) {
                        set_decode_error(AG_DEVICE_ERROR);
                        return;
                    }
                    if (resume && start_output() != AG_OK) {
                        set_decode_error(AG_DEVICE_ERROR);
                        return;
                    }
                    block = {};
                    frame_offset = 0U;
                    continue;
                }

                pending_transition_error_.store(AG_OK,
                                                std::memory_order_relaxed);
                pending_track_index_.store(next_index, std::memory_order_relaxed);
                pending_duration_ms_.store(decoder_.metadata().duration_ms,
                                           std::memory_order_relaxed);
                const std::int64_t boundary =
                    produced_frames_total_.load(std::memory_order_relaxed);
                fade_boundary_frame_.store(boundary,
                                           std::memory_order_release);
                pending_boundary_frame_.store(boundary,
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
        equalizer_.reset();
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
        const ag_result result = decoder_.open(
            session_.path_at(published_index),
            sample_rate_.load(std::memory_order_acquire),
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
        stop_decode_thread();
        if (stop_output() != AG_OK) {
            return enter_error(AG_DEVICE_ERROR);
        }

        const int previous_sample_rate =
            sample_rate_.load(std::memory_order_acquire);
        int next_sample_rate = previous_sample_rate;
        ag_result open_result = AG_OK;
        if (match_track_sample_rate_.load(std::memory_order_acquire)) {
            open_result = decoder_.open(session_.path_at(index));
            if (open_result == AG_OK) {
                next_sample_rate = decoder_.metadata().sample_rate;
                if (decoder_.metadata().channels != channels_) {
                    open_result = decoder_.open(session_.path_at(index),
                                                next_sample_rate,
                                                channels_);
                }
            }
        } else {
            open_result = decoder_.open(session_.path_at(index),
                                        previous_sample_rate,
                                        channels_);
        }
        if (open_result != AG_OK) {
            return enter_error(open_result);
        }
        if (next_sample_rate != previous_sample_rate) {
            const std::lock_guard<std::recursive_mutex> device_lock(
                device_mutex_);
            if (device_initialized_) {
                ma_device_uninit(&device_);
                device_initialized_ = false;
            }
            sample_rate_.store(next_sample_rate, std::memory_order_release);
            publish_equalizer_for_rate(next_sample_rate);
            if (initialize_device() != AG_OK) {
                return enter_error(AG_DEVICE_ERROR);
            }
        }

        session_.set_index(index);
        decode_track_index_ = index;
        duration_ms_.store(decoder_.metadata().duration_ms,
                           std::memory_order_release);
        ring_buffer_->clear();
        reset_timeline(0);
        if (transition_fade_ms_.load(std::memory_order_acquire) > 0) {
            fade_boundary_frame_.store(0, std::memory_order_release);
        }
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
        equalizer_.reset();
        rendered_frames_total_.store(position_frames, std::memory_order_release);
        track_start_frame_.store(0, std::memory_order_release);
        produced_frames_total_.store(position_frames, std::memory_order_release);
        pending_boundary_frame_.store(no_pending_boundary,
                                      std::memory_order_release);
        fade_boundary_frame_.store(no_pending_boundary,
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
        {
            const std::lock_guard<std::recursive_mutex> device_lock(
                device_mutex_);
            if (device_initialized_) {
                ma_device_uninit(&device_);
                device_initialized_ = false;
            }
            if (context_initialized_) {
                ma_context_uninit(&context_);
                context_initialized_ = false;
            }
        }
        decoder_.close();
        ring_buffer_.reset();
        session_.clear();
        loaded_ = false;
        sample_rate_.store(0, std::memory_order_release);
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

    void tap_spectrum(const float* output,
                      const std::size_t frames,
                      const std::size_t channels) noexcept
    {
        if (output == nullptr || frames == 0U || channels == 0U) {
            return;
        }
        std::array<float, spectrum_fft_size> mono{};
        std::size_t offset = 0U;
        while (offset < frames) {
            const std::size_t count =
                (std::min)(mono.size(), frames - offset);
            for (std::size_t frame = 0U; frame < count; ++frame) {
                float sum = 0.0F;
                for (std::size_t channel = 0U; channel < channels;
                     ++channel) {
                    sum += output[(offset + frame) * channels + channel];
                }
                mono[frame] = sum / static_cast<float>(channels);
            }
            (void)spectrum_tap_.write(mono.data(), count);
            offset += count;
        }
    }

    void publish_equalizer_for_rate(const int sample_rate) noexcept
    {
        GraphicEqSettings settings;
        std::uint64_t revision = 0;
        {
            const std::lock_guard<std::mutex> lock(equalizer_settings_mutex_);
            settings = equalizer_settings_;
            revision = equalizer_revision_;
        }
        equalizer_sample_rate_status_.store(sample_rate,
                                            std::memory_order_release);
        if (!is_graphic_eq_sample_rate_supported(sample_rate)) {
            GraphicEqSettings disabled;
            disabled.enabled = false;
            const auto dry = prepare_graphic_eq(disabled, 48'000, revision);
            if (dry.has_value()) {
                (void)equalizer_.submit(*dry);
            }
            equalizer_active_status_.store(false, std::memory_order_release);
            equalizer_protection_status_.store(0.0,
                                               std::memory_order_release);
            return;
        }
        const auto program = prepare_graphic_eq(settings, sample_rate, revision);
        if (!program.has_value()) {
            equalizer_active_status_.store(false, std::memory_order_release);
            return;
        }
        (void)equalizer_.submit(*program);
        equalizer_protection_status_.store(program->protection_db,
                                           std::memory_order_release);
        equalizer_active_status_.store(settings.enabled && !settings.bypassed,
                                       std::memory_order_release);
    }

    AudioBackend backend_;
    static constexpr std::int64_t no_pending_boundary = -1;
    static constexpr std::int64_t publishing_boundary = -2;
    std::size_t buffer_frames_;
    Decoder decoder_;
    std::unique_ptr<PcmRingBuffer> ring_buffer_;
    PcmRingBuffer spectrum_tap_{spectrum_tap_capacity, 1U};
    std::array<float, spectrum_fft_size> spectrum_history_{};
    std::array<float, spectrum_max_bins> spectrum_smoothed_{};
    std::size_t spectrum_history_write_ = 0U;
    std::size_t spectrum_history_filled_ = 0U;
    ma_context context_{};
    ma_device device_{};
    std::atomic<bool> context_initialized_{false};
    std::atomic<bool> device_initialized_{false};
    bool loaded_ = false;
    std::string selected_device_id_;
    bool exclusive_mode_ = false;
    std::atomic<bool> active_exclusive_mode_{false};
    std::atomic<int> sample_rate_{0};
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
    mutable std::recursive_mutex control_mutex_;
    mutable std::recursive_mutex device_mutex_;
    std::atomic<EngineState> state_{EngineState::Stopped};
    std::atomic<ag_result> terminal_error_{AG_OK};
    std::atomic<std::int64_t> rendered_frames_total_{0};
    std::atomic<std::int64_t> track_start_frame_{0};
    std::atomic<std::int64_t> produced_frames_total_{0};
    std::atomic<std::int64_t> pending_boundary_frame_{no_pending_boundary};
    std::atomic<std::int64_t> fade_boundary_frame_{no_pending_boundary};
    std::atomic<ag_result> pending_transition_error_{AG_OK};
    std::atomic<std::size_t> pending_track_index_{0U};
    std::atomic<std::int64_t> pending_duration_ms_{0};
    std::atomic<float> volume_{1.0F};
    GraphicEqualizerProcessor equalizer_;
    mutable std::mutex equalizer_settings_mutex_;
    GraphicEqSettings equalizer_settings_{};
    std::uint64_t equalizer_revision_ = 0;
    std::atomic<std::uint64_t> equalizer_revision_status_{0};
    std::atomic<bool> equalizer_enabled_status_{true};
    std::atomic<bool> equalizer_bypassed_status_{false};
    std::atomic<bool> equalizer_auto_protection_status_{true};
    std::atomic<int> equalizer_sample_rate_status_{0};
    std::atomic<bool> equalizer_active_status_{false};
    std::atomic<double> equalizer_protection_status_{0.0};
    std::atomic<bool> muted_{false};
    std::atomic<int> transition_fade_ms_{0};
    std::atomic<bool> match_track_sample_rate_{false};
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

ag_result AudioEngine::set_scoped_queue(std::vector<std::string> utf8_paths,
                                        const std::size_t start_index,
                                        const std::size_t scope_size,
                                        const bool allow_fallback) noexcept
{
    return impl_->set_scoped_queue(std::move(utf8_paths), start_index,
                                   scope_size, allow_fallback);
}

ag_result AudioEngine::queue_next(std::string utf8_path) noexcept
{
    return impl_->queue_next(std::move(utf8_path));
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

ag_result AudioEngine::set_equalizer(const GraphicEqSettings& settings,
                                     const std::uint64_t revision) noexcept
{
    return impl_->set_equalizer(settings, revision);
}

EqualizerStatus AudioEngine::equalizer_status() const noexcept
{
    return impl_->equalizer_status();
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

ag_result AudioEngine::spectrum(float* bins,
                                const std::size_t bin_count) noexcept
{
    return impl_->spectrum(bins, bin_count);
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

std::vector<OutputDevice> AudioEngine::output_devices() noexcept
{
    return impl_->output_devices();
}

ag_result AudioEngine::set_output_device(std::string utf8_id,
                                         const bool exclusive) noexcept
{
    return impl_->set_output_device(std::move(utf8_id), exclusive);
}

bool AudioEngine::exclusive_mode_active() const noexcept
{
    return impl_->exclusive_mode_active();
}

ag_result AudioEngine::set_transition_fade_ms(
    const int milliseconds) noexcept
{
    return impl_->set_transition_fade_ms(milliseconds);
}

ag_result AudioEngine::set_duration_ms(const std::int64_t duration_ms) noexcept
{
    return impl_->set_duration_ms(duration_ms);
}

ag_result AudioEngine::set_match_track_sample_rate(
    const bool enabled) noexcept
{
    return impl_->set_match_track_sample_rate(enabled);
}

} // namespace agplayer
