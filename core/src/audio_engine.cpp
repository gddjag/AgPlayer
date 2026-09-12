#include "audio_engine.hpp"

#include "audio_stream_source.hpp"
#include "decoder.hpp"
#include "pcm_ring_buffer.hpp"
#include "visual_pcm_tap.hpp"
#include "playback_time_pitch_stage.hpp"
#include "scratch_backfill_worker.hpp"
#include "scratch_command_mailbox.hpp"
#include "scratch_renderer.hpp"

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
#include <new>
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
static_assert(
    std::atomic<OutputDeviceSwitchTestBarrier*>::is_always_lock_free);
static_assert(
    std::atomic<OutputDeviceSwitchTestFailure>::is_always_lock_free);

namespace {

constexpr std::size_t spectrum_fft_size = 512U;
constexpr std::size_t spectrum_tap_capacity = 8'192U;
constexpr std::size_t spectrum_max_bins = spectrum_fft_size / 2U;
constexpr float spectrum_pi = 3.14159265358979323846F;
constexpr float output_meter_floor_linear = 1.0e-6F;
constexpr double output_meter_floor_db = -120.0;
constexpr float output_meter_release_db_per_second = 12.0F;

class OutputMeterAttemptGuard final {
public:
    explicit OutputMeterAttemptGuard(
        std::atomic<std::uint64_t>& generation) noexcept
        : generation_(generation)
    {
    }

    ~OutputMeterAttemptGuard()
    {
        if (!succeeded_) {
            generation_.fetch_add(1U, std::memory_order_acq_rel);
        }
    }

    void succeed() noexcept { succeeded_ = true; }

private:
    std::atomic<std::uint64_t>& generation_;
    bool succeeded_ = false;
};

float integer_power(float base, std::size_t exponent) noexcept
{
    float result = 1.0F;
    while (exponent > 0U) {
        if ((exponent & 1U) != 0U) {
            result *= base;
        }
        base *= base;
        exponent >>= 1U;
    }
    return result;
}

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

struct PlaybackDecodedBlock {
    std::vector<float> samples;
    std::vector<std::int64_t> source_frame_after;
    std::size_t frames{};
    std::int64_t source_end_frame{};
    std::uint64_t source_generation{};
    bool end_of_stream{};
};

class PlaybackDecoder final {
public:
    explicit PlaybackDecoder(
        const TimePitchEngineFactory factory = &create_time_pitch_engine)
        : factory_(factory)
        , file_(std::make_unique<Decoder>())
        , time_pitch_stage_(factory)
    {
    }

    PlaybackDecoder(PlaybackDecoder&&) noexcept = default;
    PlaybackDecoder& operator=(PlaybackDecoder&&) noexcept = default;

    void swap(PlaybackDecoder& other) noexcept
    {
        using std::swap;
        swap(factory_, other.factory_);
        swap(file_, other.file_);
        swap(stream_, other.stream_);
        swap(time_pitch_config_, other.time_pitch_config_);
        swap(time_pitch_stage_, other.time_pitch_stage_);
        swap(raw_block_, other.raw_block_);
        swap(processed_samples_, other.processed_samples_);
        swap(processed_source_frames_, other.processed_source_frames_);
        swap(source_end_frame_, other.source_end_frame_);
        swap(source_discard_until_frame_, other.source_discard_until_frame_);
        swap(processing_sample_rate_, other.processing_sample_rate_);
        swap(processing_channels_, other.processing_channels_);
        swap(processing_configured_, other.processing_configured_);
        swap(source_eof_, other.source_eof_);
        swap(flushed_, other.flushed_);
    }

    ag_result open(const std::string& path) noexcept
    {
        return open_interruptible(path, 0, 0, nullptr, nullptr);
    }

    ag_result open(const std::string& path, const int sampleRate,
                   const int channels) noexcept
    {
        return open_interruptible(path, sampleRate, channels, nullptr, nullptr);
    }

    ag_result open_interruptible(
        const std::string& path, const int sampleRate, const int channels,
        const DecoderInterruptCallback interruptCallback,
        void* const interruptContext) noexcept
    {
        stream_.reset();
        DecoderOpenOptions options;
        options.output_sample_rate = sampleRate;
        options.output_channels = channels;
        options.allow_silent_video_clock = true;
        options.interrupt_callback = interruptCallback;
        options.interrupt_context = interruptContext;
        const ag_result result = file_->open(path, options);
        const DecodedAudioFormat& output = file_->output_format();
        return result == AG_OK
            ? configure_processing(output.sample_rate, output.channels) : result;
    }

    void clear_interrupt_callback() noexcept
    {
        file_->clearInterruptCallback();
    }

    ag_result open(std::shared_ptr<IAudioStreamSource> stream) noexcept
    {
        if (!stream || stream->metadata().sample_rate <= 0
            || stream->metadata().channels <= 0
            || stream->metadata().channels > 2
            || stream->metadata().duration_ms <= 0) {
            return AG_INVALID_ARGUMENT;
        }
        file_->close();
        stream_ = std::move(stream);
        const MediaMetadata& value = metadata();
        return configure_processing(value.sample_rate, value.channels);
    }

    void close() noexcept
    {
        stream_.reset();
        file_->close();
        processing_configured_ = false;
        source_eof_ = false;
        flushed_ = false;
        raw_block_ = {};
        processed_samples_.clear();
        processed_source_frames_.clear();
        source_end_frame_ = 0;
        source_discard_until_frame_ = 0;
        processing_sample_rate_ = 0;
        processing_channels_ = 0;
    }

    [[nodiscard]] bool is_open() const noexcept
    {
        return stream_ != nullptr || file_->is_open();
    }

    [[nodiscard]] ag_result read(PlaybackDecodedBlock& block) noexcept
    {
        if (!processing_configured_) return AG_INVALID_ARGUMENT;
        try {
            processed_samples_.swap(block.samples);
            processed_source_frames_.swap(block.source_frame_after);
            processed_samples_.clear();
            processed_source_frames_.clear();
            block.frames = 0U;
            block.source_end_frame = source_end_frame_;
            block.end_of_stream = false;
            for (;;) {
                processed_samples_.clear();
                processed_source_frames_.clear();
                if (!source_eof_) {
                    const ag_result result = read_source(raw_block_);
                    if (result != AG_OK) return result;
                    source_eof_ = raw_block_.end_of_stream;
                    if (raw_block_.frames > 0U) {
                        source_end_frame_ = raw_block_.timestamp_frame
                            + static_cast<std::int64_t>(raw_block_.frames);
                        std::size_t source_offset = 0U;
                        std::int64_t source_start_frame =
                            raw_block_.timestamp_frame;
                        if (source_discard_until_frame_ > 0) {
                            if (source_start_frame
                                > source_discard_until_frame_) {
                                return AG_DECODE_ERROR;
                            }
                            if (source_end_frame_
                                <= source_discard_until_frame_) {
                                if (source_end_frame_
                                    == source_discard_until_frame_) {
                                    source_discard_until_frame_ = 0;
                                }
                                continue;
                            }
                            source_offset = static_cast<std::size_t>(
                                source_discard_until_frame_
                                - source_start_frame);
                            source_start_frame =
                                source_discard_until_frame_;
                            source_discard_until_frame_ = 0;
                        }
                        const std::size_t source_frames =
                            raw_block_.frames - source_offset;
                        const ag_result processResult = time_pitch_stage_.process(
                            raw_block_.samples.data()
                                + source_offset
                                    * static_cast<std::size_t>(
                                        processing_channels_),
                            source_frames, source_start_frame,
                            source_end_frame_,
                            processed_samples_, processed_source_frames_);
                        if (processResult != AG_OK) return processResult;
                    }
                }
                if (source_eof_ && !flushed_) {
                    const ag_result flushResult =
                        time_pitch_stage_.finish(processed_samples_,
                                                 processed_source_frames_);
                    if (flushResult != AG_OK) return flushResult;
                    flushed_ = true;
                }
                if (!processed_samples_.empty()) {
                    block.samples.swap(processed_samples_);
                    block.source_frame_after.swap(processed_source_frames_);
                    block.frames = block.samples.size()
                        / static_cast<std::size_t>(processing_channels_);
                    if (block.source_frame_after.size() != block.frames) {
                        return AG_INTERNAL_ERROR;
                    }
                    block.source_end_frame = source_end_frame_;
                    block.end_of_stream = false;
                    return AG_OK;
                }
                if (flushed_) {
                    block.source_end_frame = source_end_frame_;
                    block.end_of_stream = true;
                    return AG_OK;
                }
            }
        } catch (...) {
            block = {};
            return AG_INTERNAL_ERROR;
        }
    }

    [[nodiscard]] ag_result seek(const std::int64_t positionMs) noexcept
    {
        return reconfigure_and_seek(time_pitch_config_, positionMs);
    }

    [[nodiscard]] ag_result set_time_pitch(
        const PlaybackTimePitchConfig config) noexcept
    {
        if (is_open()) return AG_INVALID_ARGUMENT;
        time_pitch_config_ = config;
        return AG_OK;
    }

    void set_next_time_pitch(const PlaybackTimePitchConfig config) noexcept
    {
        time_pitch_config_ = config;
    }

    [[nodiscard]] ag_result reconfigure_and_seek(
        const PlaybackTimePitchConfig config,
        const std::int64_t positionMs) noexcept
    {
        if (!is_open()) return AG_INVALID_ARGUMENT;
        try {
            PlaybackTimePitchStage candidate(factory_);
            const ag_result configure_result = candidate.configure(
                processing_sample_rate_, processing_channels_, config);
            if (configure_result != AG_OK) return configure_result;
            const ag_result seek_result = stream_ ? stream_->seek(positionMs)
                                                  : file_->seek(positionMs);
            if (seek_result != AG_OK) return seek_result;
            time_pitch_stage_ = std::move(candidate);
            time_pitch_config_ = config;
            processing_configured_ = true;
            source_eof_ = false;
            flushed_ = false;
            raw_block_ = {};
            processed_samples_.clear();
            processed_source_frames_.clear();
            source_end_frame_ = positionMs * processing_sample_rate_ / 1'000;
            source_discard_until_frame_ = 0;
            return AG_OK;
        } catch (...) {
            return AG_INTERNAL_ERROR;
        }
    }

    [[nodiscard]] ag_result reconfigure_and_seek_frame(
        const PlaybackTimePitchConfig config,
        const std::int64_t targetFrame) noexcept
    {
        if (!is_open() || targetFrame < 0 || processing_sample_rate_ <= 0) {
            return AG_INVALID_ARGUMENT;
        }
        try {
            PlaybackTimePitchStage candidate(factory_);
            const ag_result configure_result = candidate.configure(
                processing_sample_rate_, processing_channels_, config);
            if (configure_result != AG_OK) return configure_result;
            std::int64_t discard_until = 0;
            ag_result seek_result = AG_OK;
            if (stream_) {
                const std::int64_t position_ms =
                    (targetFrame / processing_sample_rate_) * 1'000
                    + (targetFrame % processing_sample_rate_) * 1'000
                        / processing_sample_rate_;
                seek_result = stream_->seek(position_ms);
                discard_until = targetFrame;
            } else {
                seek_result = file_->seekFrame(targetFrame);
            }
            if (seek_result != AG_OK) return seek_result;
            time_pitch_stage_ = std::move(candidate);
            time_pitch_config_ = config;
            processing_configured_ = true;
            source_eof_ = false;
            flushed_ = false;
            raw_block_ = {};
            processed_samples_.clear();
            processed_source_frames_.clear();
            source_end_frame_ = targetFrame;
            source_discard_until_frame_ = discard_until;
            return AG_OK;
        } catch (...) {
            return AG_INTERNAL_ERROR;
        }
    }

    [[nodiscard]] const MediaMetadata& metadata() const noexcept
    {
        return stream_ ? stream_->metadata() : file_->metadata();
    }

    [[nodiscard]] bool uses_stream() const noexcept
    {
        return stream_ != nullptr;
    }

    [[nodiscard]] ag_result prepare_time_pitch_stage(
        const PlaybackTimePitchConfig config,
        PlaybackTimePitchStage& candidate) const noexcept
    {
        return candidate.configure(processing_sample_rate_,
                                   processing_channels_, config);
    }

    [[nodiscard]] ag_result commit_prepared_time_pitch(
        const PlaybackTimePitchConfig config,
        const std::int64_t positionMs,
        PlaybackTimePitchStage candidate) noexcept
    {
        if (!is_open()) return AG_INVALID_ARGUMENT;
        const ag_result seek_result = stream_ ? stream_->seek(positionMs)
                                              : file_->seek(positionMs);
        if (seek_result != AG_OK) return seek_result;
        time_pitch_stage_ = std::move(candidate);
        time_pitch_config_ = config;
        processing_configured_ = true;
        source_eof_ = false;
        flushed_ = false;
        raw_block_ = {};
        processed_samples_.clear();
        processed_source_frames_.clear();
        source_end_frame_ = positionMs * processing_sample_rate_ / 1'000;
        return AG_OK;
    }

private:
    [[nodiscard]] ag_result read_source(DecodedAudioBlock& block) noexcept
    {
        return stream_ ? stream_->read(block) : file_->read(block);
    }

    [[nodiscard]] ag_result configure_processing(
        const int sampleRate, const int channels) noexcept
    {
        PlaybackTimePitchStage candidate(factory_);
        const ag_result result = candidate.configure(
            sampleRate, channels, time_pitch_config_);
        if (result == AG_OK) {
            time_pitch_stage_ = std::move(candidate);
            processing_sample_rate_ = sampleRate;
            processing_channels_ = channels;
        }
        processing_configured_ = result == AG_OK;
        source_eof_ = false;
        flushed_ = false;
        raw_block_ = {};
        processed_samples_.clear();
        processed_source_frames_.clear();
        source_end_frame_ = 0;
        source_discard_until_frame_ = 0;
        return result;
    }

    TimePitchEngineFactory factory_;
    std::unique_ptr<Decoder> file_;
    std::shared_ptr<IAudioStreamSource> stream_;
    PlaybackTimePitchConfig time_pitch_config_{};
    PlaybackTimePitchStage time_pitch_stage_;
    DecodedAudioBlock raw_block_;
    std::vector<float> processed_samples_;
    std::vector<std::int64_t> processed_source_frames_;
    std::int64_t source_end_frame_{};
    std::int64_t source_discard_until_frame_{};
    int processing_sample_rate_{};
    int processing_channels_{};
    bool processing_configured_{};
    bool source_eof_{};
    bool flushed_{};
};

class AudioEngine::Impl final {
public:
    using TimelineTestHook = void (*)(void*, bool) noexcept;
    using TimePitchTestHook = void (*)(void*, std::uint64_t, int) noexcept;
    using ScratchCommitTestHook = void (*)(void*, int) noexcept;
    enum class TimePitchRequestState : int {
        Idle,
        Pending,
        Preparing,
        CommitReady,
        CommitExecuting,
        TimedOut,
    };
    enum class ScratchPhase : std::uint8_t {
        Idle,
        Active,
        Ending,
        CommitPending,
        CommitExecuting,
    };
    enum class ScratchCommitState : std::uint8_t {
        Idle,
        Pending,
        Executing,
        SeekStarted,
        Complete,
        Cancelled,
    };
    static_assert(std::atomic<TimelineTestHook>::is_always_lock_free);
    static_assert(std::atomic<TimePitchTestHook>::is_always_lock_free);
    static_assert(std::atomic<ScratchCommitTestHook>::is_always_lock_free);
    static_assert(std::atomic<void*>::is_always_lock_free);
    static_assert(std::atomic<TimePitchRequestState>::is_always_lock_free);
    static_assert(std::atomic<ScratchPhase>::is_always_lock_free);
    static_assert(std::atomic<ScratchCommitState>::is_always_lock_free);

    Impl(const AudioBackend backend, const std::size_t buffer_frames,
         const TimePitchEngineFactory factory)
        : backend_(backend)
        , buffer_frames_(buffer_frames == 0U ? 32'768U : buffer_frames)
        , factory_(factory)
        , decoder_(factory)
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

    void set_timeline_test_hook(const TimelineTestHook hook,
                                void* const context) noexcept
    {
        timeline_test_context_.store(context, std::memory_order_relaxed);
        timeline_test_hook_.store(hook, std::memory_order_release);
    }

    void set_time_pitch_test_hook(const TimePitchTestHook hook,
                                  void* const context) noexcept
    {
        time_pitch_test_context_.store(context, std::memory_order_relaxed);
        time_pitch_test_hook_.store(hook, std::memory_order_release);
    }

    void set_scratch_commit_test_hook(const ScratchCommitTestHook hook,
                                      void* const context) noexcept
    {
        scratch_commit_test_context_.store(context,
                                           std::memory_order_relaxed);
        scratch_commit_test_hook_.store(hook, std::memory_order_release);
    }

    [[nodiscard]] std::int64_t pending_boundary_for_testing() const noexcept
    {
        return pending_boundary_frame_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::uint64_t
    published_mapper_generation_for_testing() const noexcept
    {
        return published_mapper_generation_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::uint64_t
    pending_mapper_generation_for_testing() const noexcept
    {
        return pending_mapper_generation_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::int64_t
    consumed_source_frame_for_testing() const noexcept
    {
        return consumed_source_frame_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::int64_t
    scratch_source_frame_for_testing() const noexcept
    {
        return scratch_source_frame_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::uint64_t
    scratch_physical_seek_count_for_testing() const noexcept
    {
        return scratch_physical_seek_count_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool decode_running_for_testing() const noexcept
    {
        return decode_running_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool time_pitch_cancel_requested_for_testing() const noexcept
    {
        const TimePitchRequestState state =
            time_pitch_request_state_.load(std::memory_order_acquire);
        return state == TimePitchRequestState::TimedOut;
    }

    [[nodiscard]] bool time_pitch_request_idle_for_testing() const noexcept
    {
        return time_pitch_request_state_.load(std::memory_order_acquire)
            == TimePitchRequestState::Idle;
    }

    [[nodiscard]] std::uint64_t
    requested_time_pitch_generation_for_testing() const noexcept
    {
        return requested_time_pitch_generation_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::uint64_t
    completed_time_pitch_generation_for_testing() const noexcept
    {
        return completed_time_pitch_generation_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool
    has_retired_time_pitch_decoder_for_testing() const noexcept
    {
        return has_retired_time_pitch_decoder_.load(
            std::memory_order_acquire);
    }

    [[nodiscard]] bool time_pitch_mailbox_clear_for_testing() const noexcept
    {
        return !seek_requested_.load(std::memory_order_acquire)
            && !time_pitch_change_requested_.load(std::memory_order_acquire)
            && !seeking_.load(std::memory_order_acquire)
            && time_pitch_request_state_.load(std::memory_order_acquire)
                == TimePitchRequestState::Idle;
    }

    void stop_decode_thread_for_testing() noexcept
    {
        stop_decode_thread();
    }

    void request_decode_exit_for_testing() noexcept
    {
        stop_decode_.store(true, std::memory_order_release);
        seek_cv_.notify_all();
    }

    void mark_device_lost_for_testing() noexcept
    {
        invalidate_scratch_from_device_callback();
        device_lost_.store(true, std::memory_order_release);
        terminal_error_.store(AG_DEVICE_ERROR, std::memory_order_release);
        state_.store(EngineState::Error, std::memory_order_release);
    }

    void notify_device_lost_from_backend_for_testing() noexcept
    {
        publish_device_loss_from_backend_callback();
    }

    ag_result enter_error_for_testing(const ag_result result) noexcept
    {
        return enter_error(result);
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

    ag_result load_stream(std::shared_ptr<IAudioStreamSource> stream,
                          const bool bypassTimePitch) noexcept
    {
        if (!stream) return AG_INVALID_ARGUMENT;
        const std::lock_guard<std::recursive_mutex> controlLock(control_mutex_);
        try {
            shutdown_loaded_media();
            state_.store(EngineState::Loading, std::memory_order_release);
            session_.set_queue({"agplayer://editor-stream"}, 0U);
            session_.set_mode(PlaybackMode::Sequential);
            const bool editorKeepPitch =
                keep_pitch_.load(std::memory_order_acquire);
            const PlaybackTimePitchConfig streamConfig = bypassTimePitch
                ? PlaybackTimePitchConfig{1.0, editorKeepPitch}
                : current_time_pitch_config();
            const ag_result bypassResult =
                decoder_.set_time_pitch(streamConfig);
            if (bypassResult != AG_OK) return fail_load(bypassResult);
            const ag_result decodeResult = decoder_.open(std::move(stream));
            if (decodeResult != AG_OK) return fail_load(decodeResult);
            stream_source_active_ = true;
            editor_stream_ = bypassTimePitch;
            sample_rate_.store(decoder_.metadata().sample_rate,
                               std::memory_order_release);
            publish_equalizer_for_rate(decoder_.metadata().sample_rate);
            channels_ = decoder_.metadata().channels;
            duration_ms_.store(decoder_.metadata().duration_ms,
                               std::memory_order_release);
            ring_buffer_ = std::make_unique<PcmRingBuffer>(
                buffer_frames_, static_cast<std::size_t>(channels_));
            const ag_result deviceResult = initialize_device();
            if (deviceResult != AG_OK) return fail_load(deviceResult);
            const std::uint64_t epoch = begin_timeline_write();
            time_pitch_ratio_.store(
                static_cast<float>(streamConfig.speed_ratio),
                std::memory_order_release);
            keep_pitch_.store(editorKeepPitch, std::memory_order_release);
            publish_session_state_locked();
            reset_timeline_locked(0);
            end_timeline_write(epoch);
            loaded_ = true;
            terminal_error_.store(AG_OK, std::memory_order_release);
            state_.store(EngineState::Stopped, std::memory_order_release);
            const ag_result threadResult = start_decode_thread();
            if (threadResult != AG_OK) return fail_load(threadResult);
            return AG_OK;
        } catch (...) {
            return fail_load(AG_INTERNAL_ERROR);
        }
    }

    ag_result replace_stream(std::shared_ptr<IAudioStreamSource> stream) noexcept
    {
        if (!stream) return AG_INVALID_ARGUMENT;
        const std::lock_guard<std::recursive_mutex> controlLock(control_mutex_);
        const MediaMetadata& metadata = stream->metadata();
        if (!loaded_ || !editor_stream_
            || metadata.sample_rate != sample_rate_.load(std::memory_order_acquire)
            || metadata.channels != channels_) {
            return load_stream(std::move(stream), true);
        }
        try {
            state_.store(EngineState::Stopped, std::memory_order_release);
            if (stop_output() != AG_OK) return enter_error(AG_DEVICE_ERROR);
            stop_decode_thread();
            const ag_result decodeResult = decoder_.open(std::move(stream));
            if (decodeResult != AG_OK) return enter_error(decodeResult);
            duration_ms_.store(decoder_.metadata().duration_ms,
                               std::memory_order_release);
            const std::uint64_t epoch = begin_timeline_write();
            ring_buffer_->clear();
            reset_timeline_locked(0);
            end_timeline_write(epoch);
            terminal_error_.store(AG_OK, std::memory_order_release);
            state_.store(EngineState::Stopped, std::memory_order_release);
            const ag_result threadResult = start_decode_thread();
            return threadResult == AG_OK ? AG_OK : enter_error(threadResult);
        } catch (...) {
            return enter_error(AG_INTERNAL_ERROR);
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
            const bool resetTempoAtBoundary = loaded_;
            const bool boundaryKeepPitch =
                keep_pitch_.load(std::memory_order_acquire);
            shutdown_loaded_media();
            editor_stream_ = false;
            state_.store(EngineState::Loading, std::memory_order_release);
            if (scope_size > 0U) {
                session_.set_scoped_queue(std::move(paths), start_index,
                                          scope_size, allow_fallback);
            } else {
                session_.set_queue(std::move(paths), start_index);
            }
            decode_track_index_ = start_index;

            const PlaybackTimePitchConfig initialConfig =
                resetTempoAtBoundary
                ? PlaybackTimePitchConfig{1.0, boundaryKeepPitch}
                : current_time_pitch_config();
            const ag_result config_result =
                decoder_.set_time_pitch(initialConfig);
            if (config_result != AG_OK) return fail_load(config_result);

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

            const std::uint64_t epoch = begin_timeline_write();
            if (resetTempoAtBoundary) {
                time_pitch_ratio_.store(1.0F, std::memory_order_release);
                keep_pitch_.store(boundaryKeepPitch,
                                  std::memory_order_release);
            }
            publish_session_state_locked();
            reset_timeline_locked(0);
            end_timeline_write(epoch);
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
        if (scratch_phase_.load(std::memory_order_acquire)
            != ScratchPhase::Idle) {
            return AG_INVALID_ARGUMENT;
        }
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
        if (state == EngineState::Paused) invalidate_output_level_meter();
        else invalidate_output_meter();
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
        if (scratch_phase_.load(std::memory_order_acquire)
            != ScratchPhase::Idle) {
            const ag_result scratch_result = end_scratch();
            if (scratch_result != AG_OK) return scratch_result;
        }
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
        // Pause keeps the visual PCM timeline continuous so the immersive
        // renderer can perform the original slow visual release and resume
        // without a black reset. Only the audible level meter is stale here;
        // seek, track and device boundaries still invalidate the visual tap.
        invalidate_output_level_meter();
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
        hard_invalidate_scratch();
        invalidate_output_meter();
        state_.store(EngineState::Stopped, std::memory_order_release);
        stop_decode_thread();
        if (stop_output() != AG_OK) {
            return enter_error(AG_DEVICE_ERROR);
        }
        destroy_scratch_resources_after_output_stopped();

        const ag_result restore_result = restore_published_decoder();
        if (restore_result != AG_OK) {
            return enter_error(restore_result);
        }
        const ag_result seek_result = decoder_.seek(0);
        if (seek_result != AG_OK) {
            return enter_error(seek_result);
        }
        const std::size_t publishedIndex =
            published_track_index_.load(std::memory_order_acquire);
        const std::uint64_t epoch = begin_timeline_write();
        ring_buffer_->clear();
        decode_track_index_ = publishedIndex;
        reset_timeline_locked(0);
        end_timeline_write(epoch);
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
        invalidate_output_meter();
        if (time_pitch_request_state_.load(std::memory_order_acquire)
            != TimePitchRequestState::Idle) {
            return AG_CANCELLED;
        }

        if (scratch_rendering()) {
            hard_invalidate_scratch();
            if (stop_output() != AG_OK) {
                return enter_error(AG_DEVICE_ERROR);
            }
            stop_decode_thread();
            destroy_scratch_resources_after_output_stopped();
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
        const ag_result seek_result = decoder_.reconfigure_and_seek(
            current_time_pitch_config(), position_ms);
        if (seek_result != AG_OK) {
            return enter_error(seek_result);
        }
        const std::int64_t position_frames =
            (position_ms
                 * sample_rate_.load(std::memory_order_acquire)
             + 999)
            / 1'000;
        const std::uint64_t epoch = begin_timeline_write();
        ring_buffer_->clear();
        decode_track_index_ =
            published_track_index_.load(std::memory_order_acquire);
        reset_timeline_locked(position_frames);
        end_timeline_write(epoch);
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
            sync_session_index_to_published();
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
        sync_session_index_to_published();
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
        const std::uint64_t epoch = begin_timeline_write();
        session_.set_mode(mode);
        publish_session_state_locked();
        end_timeline_write(epoch);
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

    ag_result set_replay_gain(const float gain_db, const float peak,
                              const bool clip_protection) noexcept
    {
        if (!std::isfinite(gain_db) || !std::isfinite(peak) || peak < 0.0F) {
            return AG_INVALID_ARGUMENT;
        }
        float linear = std::pow(10.0F, gain_db / 20.0F);
        if (clip_protection && peak > 0.0F && linear * peak > 1.0F) {
            linear = 1.0F / peak;
        }
        replay_gain_linear_.store(linear, std::memory_order_release);
        return AG_OK;
    }

    ag_result set_time_pitch(const PlaybackTimePitchConfig config) noexcept
    {
        if (!std::isfinite(config.speed_ratio)
            || config.speed_ratio < 0.75 || config.speed_ratio > 1.50) {
            return AG_INVALID_ARGUMENT;
        }
        const std::lock_guard<std::recursive_mutex> control_lock(
            control_mutex_);
        if (scratch_rendering()) return AG_CANCELLED;
        if (device_lost_.load(std::memory_order_acquire)
            || state_.load(std::memory_order_acquire) == EngineState::Error) {
            return current_error();
        }
        // IAudioStreamSource::seek() is an internal, synchronous contract and
        // cannot be force-cancelled safely. Dynamic time/pitch transactions
        // therefore remain file-backed; editor streams were already subject
        // to this boundary, and direct internal streams follow it as well.
        if (loaded_ && stream_source_active_) return AG_INVALID_ARGUMENT;
        const float nextRatio = static_cast<float>(config.speed_ratio);
        if (std::abs(time_pitch_ratio_.load(std::memory_order_acquire)
                     - nextRatio) < 0.000001F
            && keep_pitch_.load(std::memory_order_acquire)
                == config.keep_pitch) {
            return AG_OK;
        }
        if (time_pitch_request_state_.load(std::memory_order_acquire)
            != TimePitchRequestState::Idle) {
            return AG_CANCELLED;
        }
        if (!loaded_) {
            const ag_result result = decoder_.set_time_pitch(config);
            if (result == AG_OK) {
                const std::uint64_t epoch = begin_timeline_write();
                time_pitch_ratio_.store(nextRatio, std::memory_order_release);
                keep_pitch_.store(config.keep_pitch,
                                  std::memory_order_release);
                end_timeline_write(epoch);
            }
            return result;
        }
        if (!decode_running_.load(std::memory_order_acquire)) {
            stop_decode_thread();
            const ag_result start_result = start_decode_thread();
            if (start_result != AG_OK) return start_result;
            if (!decode_running_.load(std::memory_order_acquire)) {
                const ag_result terminal =
                    terminal_error_.load(std::memory_order_acquire);
                return terminal == AG_OK ? AG_INTERNAL_ERROR : terminal;
            }
        }
        const std::int64_t sourcePosition = snapshot().position_ms;
        seek_target_ms_.store(sourcePosition, std::memory_order_release);
        requested_time_pitch_ratio_.store(nextRatio,
                                          std::memory_order_release);
        requested_keep_pitch_.store(config.keep_pitch,
                                    std::memory_order_release);
        time_pitch_change_requested_.store(true,
                                           std::memory_order_release);
        const std::uint64_t request_generation =
            time_pitch_generation_counter_.fetch_add(
                1U, std::memory_order_relaxed) + 1U;
        requested_time_pitch_generation_.store(request_generation,
                                               std::memory_order_release);
        time_pitch_request_state_.store(TimePitchRequestState::Pending,
                                        std::memory_order_release);
        notify_time_pitch_test_hook(request_generation, 0);
        if (!decode_running_.load(std::memory_order_acquire)
            || time_pitch_request_state_.load(std::memory_order_acquire)
                != TimePitchRequestState::Pending) {
            if (!decode_running_.load(std::memory_order_acquire)) {
                abandon_time_pitch_request_after_decode_stop();
            }
            return completed_time_pitch_generation_.load(
                       std::memory_order_acquire) == request_generation
                ? completed_time_pitch_result_.load(std::memory_order_acquire)
                : AG_CANCELLED;
        }
        seeking_.store(true, std::memory_order_release);
        seek_requested_.store(true, std::memory_order_release);
        seek_cv_.notify_one();
        const auto request_completed = [this, request_generation] {
            return completed_time_pitch_generation_.load(
                       std::memory_order_acquire) == request_generation
                && time_pitch_request_state_.load(std::memory_order_acquire)
                    == TimePitchRequestState::Idle;
        };
        bool completed = false;
        {
            std::unique_lock<std::mutex> lock(seek_mutex_);
            completed = seek_cv_.wait_for(
                lock, std::chrono::seconds(2),
                [this, &request_completed] {
                    return request_completed()
                        || !decode_running_.load(std::memory_order_acquire);
                });
        }
        if (!completed) {
            TimePitchRequestState state =
                time_pitch_request_state_.load(std::memory_order_acquire);
            while (state == TimePitchRequestState::Pending
                   || state == TimePitchRequestState::Preparing
                   || state == TimePitchRequestState::CommitReady) {
                if (time_pitch_request_state_.compare_exchange_weak(
                        state, TimePitchRequestState::TimedOut,
                        std::memory_order_acq_rel,
                        std::memory_order_acquire)) {
                    seeking_.store(false, std::memory_order_release);
                    seek_cv_.notify_all();
                    return AG_CANCELLED;
                }
            }
            if (state == TimePitchRequestState::TimedOut) {
                seeking_.store(false, std::memory_order_release);
                return AG_CANCELLED;
            }
            if (state == TimePitchRequestState::CommitExecuting) {
                // The worker crossed the point of no return. From here to
                // publication it performs fixed-cost swaps, ring reset, and
                // atomics only: no file I/O, locks, or retired destruction.
                // Wait for this generation's real result rather than guessing
                // success or returning a failure that could apply later.
                std::unique_lock<std::mutex> lock(seek_mutex_);
                seek_cv_.wait(lock, [this, &request_completed] {
                    return request_completed()
                        || !decode_running_.load(std::memory_order_acquire);
                });
                completed = request_completed();
            } else {
                completed = request_completed();
            }
        }
        if (!completed || !request_completed()) {
            if (!decode_running_.load(std::memory_order_acquire)) {
                abandon_time_pitch_request_after_decode_stop();
                if (request_completed()) {
                    return completed_time_pitch_result_.load(
                        std::memory_order_acquire);
                }
            }
            seeking_.store(false, std::memory_order_release);
            TimePitchRequestState state =
                time_pitch_request_state_.load(std::memory_order_acquire);
            if (state != TimePitchRequestState::TimedOut
                && state != TimePitchRequestState::CommitExecuting) {
                time_pitch_request_state_.store(TimePitchRequestState::Idle,
                                                std::memory_order_release);
            }
            const ag_result terminal =
                terminal_error_.load(std::memory_order_acquire);
            return terminal == AG_OK ? AG_INTERNAL_ERROR : terminal;
        }
        seeking_.store(false, std::memory_order_release);
        return completed_time_pitch_result_.load(std::memory_order_acquire);
    }

    [[nodiscard]] PlaybackTimePitchConfig time_pitch_config() const noexcept
    {
        for (;;) {
            const std::uint64_t epoch =
                transition_version_.load(std::memory_order_acquire);
            if ((epoch & 1U) != 0U) continue;
            const PlaybackTimePitchConfig result = current_time_pitch_config();
            if (transition_version_.load(std::memory_order_acquire) == epoch) {
                return result;
            }
        }
    }

    ag_result begin_scratch() noexcept
    {
        const std::lock_guard<std::recursive_mutex> control_lock(
            control_mutex_);
        try {
            const EngineState state = state_.load(std::memory_order_acquire);
            if (!loaded_ || stream_source_active_
                || (state != EngineState::Playing
                    && state != EngineState::Paused)
                || device_lost_.load(std::memory_order_acquire)
                || seeking_.load(std::memory_order_acquire)
                || seek_requested_.load(std::memory_order_acquire)
                || time_pitch_request_state_.load(std::memory_order_acquire)
                    != TimePitchRequestState::Idle
                || pending_boundary_frame_.load(std::memory_order_acquire)
                    != no_pending_boundary) {
                return AG_INVALID_ARGUMENT;
            }
            if (scratch_phase_.load(std::memory_order_acquire)
                != ScratchPhase::Idle) {
                return AG_OK;
            }

            const int sample_rate =
                sample_rate_.load(std::memory_order_acquire);
            const std::int64_t source_frame_count =
                duration_ms_.load(std::memory_order_acquire)
                * static_cast<std::int64_t>(sample_rate) / 1'000;
            if (sample_rate <= 0 || channels_ <= 0
                || source_frame_count <= 0 || session_.size() == 0U) {
                return AG_INVALID_ARGUMENT;
            }

            if (!scratch_worker_) {
                scratch_worker_ = std::make_unique<ScratchBackfillWorker>(
                    static_cast<std::uint32_t>(sample_rate),
                    static_cast<std::uint32_t>(channels_));
                scratch_renderer_ = std::make_unique<ScratchRenderer>(
                    static_cast<std::uint32_t>(sample_rate),
                    static_cast<std::uint32_t>(channels_));
            }

            const std::uint64_t generation =
                scratch_generation_counter_.fetch_add(
                    1U, std::memory_order_relaxed) + 1U;
            const std::uint64_t epoch =
                scratch_media_epoch_.load(std::memory_order_acquire);
            const std::int64_t source_frame = std::clamp(
                consumed_source_frame_.load(std::memory_order_acquire),
                std::int64_t{0}, source_frame_count - 1);
            scratch_media_path_ = session_.current_path();
            scratch_source_frame_count_ = source_frame_count;
            scratch_window_generation_.store(generation,
                                               std::memory_order_release);
            scratch_backfill_pending_generation_.store(
                generation, std::memory_order_release);
            scratch_source_frame_.store(source_frame,
                                        std::memory_order_release);
            scratch_ready_.store(false, std::memory_order_release);
            scratch_buffering_.store(true, std::memory_order_release);
            scratch_exit_complete_.store(false, std::memory_order_release);
            scratch_rebase_requested_.store(false,
                                             std::memory_order_release);
            scratch_requested_rate_.store(0.0F,
                                          std::memory_order_release);
            scratch_was_playing_ = state == EngineState::Playing;

            if (!scratch_renderer_->begin(
                    static_cast<double>(source_frame), source_frame_count,
                    generation, epoch)) {
                return AG_INTERNAL_ERROR;
            }
            const ScratchCommand command{generation, epoch, true, 0.0F};
            // begin/end are serialized by control_mutex_, preserving the
            // mailbox's single-producer contract.
            scratch_render_mailbox_.publish(command);
            const ScratchBackfillRequest request{
                scratch_media_path_,
                static_cast<std::uint32_t>(sample_rate),
                static_cast<std::uint32_t>(channels_),
                source_frame_count,
                source_frame,
                generation,
                epoch,
            };
            if (!scratch_worker_->request(request)) {
                scratch_renderer_->cancel_immediately();
                scratch_buffering_.store(false, std::memory_order_release);
                return AG_INTERNAL_ERROR;
            }

            scratch_phase_.store(ScratchPhase::Active,
                                 std::memory_order_release);
            seek_cv_.notify_all();
            if (!scratch_was_playing_ && start_output() != AG_OK) {
                hard_invalidate_scratch();
                return AG_DEVICE_ERROR;
            }
            return AG_OK;
        } catch (...) {
            hard_invalidate_scratch();
            return AG_INTERNAL_ERROR;
        }
    }

    ag_result update_scratch(const float signed_rate) noexcept
    {
        if (!std::isfinite(signed_rate)) return AG_INVALID_ARGUMENT;
        if (scratch_phase_.load(std::memory_order_acquire)
            != ScratchPhase::Active) {
            return AG_INVALID_ARGUMENT;
        }
        scratch_requested_rate_.store(
            std::clamp(signed_rate, -3.0F, 3.0F),
            std::memory_order_release);
        seek_cv_.notify_one();
        return AG_OK;
    }

    ag_result end_scratch() noexcept
    {
        const std::lock_guard<std::recursive_mutex> control_lock(
            control_mutex_);
        ScratchPhase phase =
            scratch_phase_.load(std::memory_order_acquire);
        if (phase == ScratchPhase::Idle) return AG_OK;
        if (phase != ScratchPhase::Active
            && phase != ScratchPhase::Ending) {
            return AG_CANCELLED;
        }

        const std::uint64_t generation =
            scratch_window_generation_.load(std::memory_order_acquire);
        const std::uint64_t epoch =
            scratch_media_epoch_.load(std::memory_order_acquire);
        scratch_phase_.store(ScratchPhase::Ending,
                             std::memory_order_release);
        scratch_requested_rate_.store(0.0F,
                                      std::memory_order_release);
        const ScratchCommand exit_command{
            generation, epoch, false, 0.0F};
        scratch_render_mailbox_.publish(exit_command);
        seek_cv_.notify_all();

        if (backend_ == AudioBackend::Manual) {
            std::array<float, 1'024U * ScratchRenderer::kMaximumChannels>
                scratch_output{};
            while (!scratch_exit_complete_.load(std::memory_order_acquire)) {
                render(scratch_output.data(), 1'024U);
            }
        } else {
            while (!scratch_exit_complete_.load(std::memory_order_acquire)) {
                if (device_lost_.load(std::memory_order_acquire)
                    || state_.load(std::memory_order_acquire)
                        == EngineState::Error) {
                    hard_invalidate_scratch();
                    return current_error();
                }
                std::this_thread::yield();
            }
        }

        notify_scratch_commit_test_hook(1);
        const bool lost_before_commit =
            device_lost_.load(std::memory_order_acquire);
        if (lost_before_commit
            || scratch_media_epoch_.load(std::memory_order_acquire) != epoch
            || scratch_phase_.load(std::memory_order_acquire)
                != ScratchPhase::Ending) {
            hard_invalidate_scratch();
            return lost_before_commit ? current_error() : AG_CANCELLED;
        }
        const std::uint64_t commit_generation =
            scratch_commit_generation_counter_.fetch_add(
                1U, std::memory_order_relaxed) + 1U;
        scratch_commit_target_frame_.store(
            scratch_source_frame_.load(std::memory_order_acquire),
            std::memory_order_release);
        scratch_commit_epoch_.store(epoch, std::memory_order_release);
        scratch_requested_commit_generation_.store(
            commit_generation, std::memory_order_release);
        scratch_commit_state_.store(ScratchCommitState::Pending,
                                    std::memory_order_release);
        ScratchPhase expected_phase = ScratchPhase::Ending;
        if (!scratch_phase_.compare_exchange_strong(
                expected_phase, ScratchPhase::CommitPending,
                std::memory_order_acq_rel, std::memory_order_acquire)) {
            hard_invalidate_scratch();
            return device_lost_.load(std::memory_order_acquire)
                ? current_error() : AG_CANCELLED;
        }
        notify_scratch_commit_test_hook(3);
        seek_cv_.notify_all();

        {
            std::unique_lock<std::mutex> lock(seek_mutex_);
            while (scratch_completed_commit_generation_.load(
                       std::memory_order_acquire) != commit_generation
                   && decode_running_.load(std::memory_order_acquire)
                   && !device_lost_.load(std::memory_order_acquire)
                   && scratch_media_epoch_.load(std::memory_order_acquire)
                       == epoch) {
                seek_cv_.wait_for(lock, std::chrono::milliseconds(1));
            }
        }
        if (scratch_completed_commit_generation_.load(
                std::memory_order_acquire) != commit_generation) {
            hard_invalidate_scratch();
            return AG_INTERNAL_ERROR;
        }
        const ag_result result =
            scratch_commit_result_.load(std::memory_order_acquire);
        finish_scratch_session();
        return result;
    }

    ag_result cancel_scratch() noexcept { return end_scratch(); }

    [[nodiscard]] ScratchStatus scratch_status() const noexcept
    {
        const bool active = scratch_phase_.load(std::memory_order_acquire)
            != ScratchPhase::Idle;
        return {
            active,
            active && scratch_ready_.load(std::memory_order_acquire),
            active && scratch_buffering_.load(std::memory_order_acquire),
        };
    }

    [[nodiscard]] OutputLevels output_levels() const noexcept
    {
        if (muted_.load(std::memory_order_relaxed)
            || device_lost_.load(std::memory_order_relaxed)
            || seeking_.load(std::memory_order_relaxed)
            || (state_.load(std::memory_order_relaxed)
                    != EngineState::Playing
                && !scratch_rendering())) {
            return {};
        }
        return {
            output_left_peak_.load(std::memory_order_relaxed),
            output_right_peak_.load(std::memory_order_relaxed),
            output_left_rms_.load(std::memory_order_relaxed),
            output_right_rms_.load(std::memory_order_relaxed),
        };
    }

    void publish_output_levels_for_testing(
        const float* output, const std::size_t frames,
        const std::size_t channels) noexcept
    {
        publish_output_levels(output, frames, channels);
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
        if (is_graphic_eq_sample_rate_supported(current_rate)) {
            if (fail_next_equalizer_submit_for_test_.exchange(
                    false, std::memory_order_acq_rel)
                || !equalizer_.submit(*program)) {
                return AG_INTERNAL_ERROR;
            }
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
        return AG_OK;
    }

    [[nodiscard]] EqualizerStatus equalizer_status() const noexcept
    {
        double output_peak_db = output_meter_floor_db;
        const std::uint64_t meter_generation =
            output_meter_generation_.load(std::memory_order_acquire);
        if (output_meter_valid_generation_.load(std::memory_order_acquire)
                == meter_generation
            && state_.load(std::memory_order_acquire) == EngineState::Playing
            && !muted_.load(std::memory_order_acquire)
            && !device_lost_.load(std::memory_order_acquire)
            && !seeking_.load(std::memory_order_acquire)) {
            const float output_peak =
                output_peak_linear_.load(std::memory_order_acquire);
            if (output_meter_generation_.load(std::memory_order_acquire)
                    == meter_generation
                && std::isfinite(output_peak)
                && output_peak > output_meter_floor_linear) {
                output_peak_db = (std::max)(
                    output_meter_floor_db,
                    20.0 * std::log10(static_cast<double>(output_peak)));
            }
        }
        return {
            equalizer_revision_status_.load(std::memory_order_acquire),
            equalizer_enabled_status_.load(std::memory_order_acquire),
            equalizer_bypassed_status_.load(std::memory_order_acquire),
            equalizer_auto_protection_status_.load(std::memory_order_acquire),
            equalizer_sample_rate_status_.load(std::memory_order_acquire),
            equalizer_active_status_.load(std::memory_order_acquire),
            equalizer_protection_status_.load(std::memory_order_acquire),
            output_peak_db};
    }

    void set_muted(const bool muted) noexcept
    {
        if (muted_.load(std::memory_order_acquire) == muted) {
            return;
        }
        invalidate_output_level_meter();
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

            const int sample_rate =
                sample_rate_.load(std::memory_order_acquire);
            const std::int64_t source_frame =
                consumed_source_frame_.load(std::memory_order_acquire);
            const EngineSnapshot result{
                state_.load(std::memory_order_acquire),
                sample_rate <= 0 ? 0 : source_frame * 1'000 / sample_rate,
                duration_ms_.load(std::memory_order_acquire),
                sample_rate,
                channels_.load(std::memory_order_acquire),
                volume_.load(std::memory_order_acquire),
                muted_.load(std::memory_order_acquire),
                published_track_index_.load(std::memory_order_acquire),
                published_track_count_.load(std::memory_order_acquire),
                static_cast<PlaybackMode>(
                    published_mode_.load(std::memory_order_acquire)),
            };
            if (transition_version_.load(std::memory_order_acquire) == version) {
                return result;
            }
        }
    }

    [[nodiscard]] bool scratch_rendering() const noexcept
    {
        return scratch_phase_.load(std::memory_order_acquire)
            != ScratchPhase::Idle;
    }

    void render_scratch(float* const output,
                        const std::size_t requested_frames) noexcept
    {
        ScratchCommand command = scratch_rt_command_;
        if (scratch_render_mailbox_.try_read(command)) {
            scratch_rt_command_ = command;
        }
        const ScratchPhase phase =
            scratch_phase_.load(std::memory_order_acquire);
        command.generation = scratch_window_generation_.load(
            std::memory_order_acquire);
        command.epoch = scratch_media_epoch_.load(
            std::memory_order_acquire);
        command.active = phase == ScratchPhase::Active;
        command.signed_rate = scratch_requested_rate_.load(
            std::memory_order_acquire);
        if (!scratch_renderer_ || !scratch_worker_) {
            std::fill_n(output,
                        requested_frames * static_cast<std::size_t>(channels_),
                        0.0F);
            scratch_ready_.store(false, std::memory_order_release);
            scratch_buffering_.store(true, std::memory_order_release);
            return;
        }

        if (scratch_lease_
            && (scratch_lease_.window->generation() != command.generation
                || scratch_lease_.window->epoch() != command.epoch)) {
            (void)scratch_worker_->release(scratch_lease_);
        }
        if (!scratch_lease_) {
            if (scratch_worker_->try_acquire(scratch_lease_)
                && scratch_lease_.window->generation()
                    == command.generation
                && scratch_lease_.window->epoch() == command.epoch) {
                scratch_backfill_pending_generation_.store(
                    0U, std::memory_order_release);
            }
        }
        const ScratchRenderStatus status = scratch_renderer_->render(
            scratch_lease_ ? scratch_lease_.window : nullptr,
            command, output, requested_frames);
        scratch_source_frame_.store(
            static_cast<std::int64_t>(std::llround(status.source_frame)),
            std::memory_order_release);
        scratch_ready_.store(status.ready, std::memory_order_release);
        scratch_buffering_.store(status.buffering,
                                 std::memory_order_release);
        if (status.rebase_requested
            && scratch_backfill_pending_generation_.load(
                   std::memory_order_acquire) == 0U) {
            scratch_rebase_anchor_.store(
                static_cast<std::int64_t>(std::llround(status.source_frame)),
                std::memory_order_relaxed);
            scratch_rebase_generation_.store(command.generation,
                                              std::memory_order_relaxed);
            scratch_rebase_epoch_.store(command.epoch,
                                        std::memory_order_relaxed);
            scratch_rebase_requested_.store(true,
                                             std::memory_order_release);
        }
        if (status.exit_complete) {
            if (scratch_lease_) {
                (void)scratch_worker_->release(scratch_lease_);
            }
            scratch_ready_.store(false, std::memory_order_release);
            scratch_buffering_.store(false, std::memory_order_release);
            scratch_exit_complete_.store(true, std::memory_order_release);
        }
    }

    void render(float* output, const std::size_t requested_frames) noexcept
    {
        const std::uint64_t meter_generation =
            output_meter_generation_.load(std::memory_order_acquire);
        wait_at_output_device_switch_test_barrier();
        if (output == nullptr || requested_frames == 0U) {
            output_peak_linear_.store(0.0F, std::memory_order_release);
            output_meter_valid_generation_.store(0U,
                                                  std::memory_order_release);
            publish_output_levels(nullptr, 0U, 0U);
            return;
        }
        const std::size_t channels = static_cast<std::size_t>(channels_);
        const bool scratch = scratch_rendering();
        if ((state_.load(std::memory_order_acquire) != EngineState::Playing
             && !scratch)
            || device_lost_.load(std::memory_order_acquire)
            || seeking_.load(std::memory_order_acquire)) {
            output_peak_linear_.store(0.0F, std::memory_order_release);
            output_meter_valid_generation_.store(0U,
                                                  std::memory_order_release);
            std::fill(output, output + requested_frames * channels, 0.0F);
            publish_output_levels(output, requested_frames, channels);
            return;
        }

        if (scratch) {
            render_scratch(output, requested_frames);
            equalizer_.process(output, requested_frames, channels);
            if (visual_pcm_enabled_.load(std::memory_order_relaxed))
                tap_spectrum(output, requested_frames, channels, true);
            const float gain = muted_.load(std::memory_order_relaxed)
                ? 0.0F
                : volume_.load(std::memory_order_relaxed)
                    * replay_gain_linear_.load(std::memory_order_relaxed);
            for (std::size_t sample = 0U;
                 sample < requested_frames * channels; ++sample) {
                output[sample] *= gain;
            }
            publish_output_levels(output, requested_frames, channels);
            tap_spectrum(output, requested_frames, channels);
            return;
        }

        std::uint64_t epoch =
            transition_version_.load(std::memory_order_acquire);
        if ((epoch & 1U) != 0U
            || !transition_version_.compare_exchange_strong(
                epoch, epoch + 1U, std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            output_peak_linear_.store(0.0F, std::memory_order_release);
            output_meter_valid_generation_.store(0U,
                                                  std::memory_order_release);
            std::fill(output, output + requested_frames * channels, 0.0F);
            publish_output_levels(output, requested_frames, channels);
            return;
        }
        notify_timeline_test_hook(true);
        const std::int64_t render_start =
            rendered_frames_total_.load(std::memory_order_acquire);
        std::int64_t source_frame_after =
            consumed_source_frame_.load(std::memory_order_relaxed);
        std::uint64_t source_generation = 0U;
        const std::size_t frames = ring_buffer_ == nullptr
            ? 0U
            : ring_buffer_->read(output, requested_frames,
                                 &source_frame_after, &source_generation);
        const std::int64_t rendered =
            render_start + static_cast<std::int64_t>(frames);
        const std::int64_t pending_boundary =
            pending_boundary_frame_.load(std::memory_order_acquire);
        if (pending_boundary >= 0 && pending_boundary <= rendered) {
            const std::size_t old_track_frames = pending_boundary <= render_start
                ? 0U
                : static_cast<std::size_t>(pending_boundary - render_start);
            equalizer_.process(output, old_track_frames, channels);
            publish_pending_transition_locked(pending_boundary);
            equalizer_.process(output + old_track_frames * channels,
                               frames - old_track_frames, channels);
        } else {
            equalizer_.process(output, frames, channels);
        }
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
        float block_peak = 0.0F;
        const bool visual_enabled = visual_pcm_enabled_.load(std::memory_order_relaxed);
        std::array<float, spectrum_fft_size> visual_mono;
        std::size_t visual_count = 0U;
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
            float visual_sum = 0.0F;
            for (std::size_t channel = 0U; channel < channels; ++channel) {
                float& sample = output[frame * channels + channel];
                if (visual_enabled) visual_sum += sample * transition_gain;
                sample *= gain * transition_gain;
                block_peak = (std::max)(block_peak, std::abs(sample));
            }
            if (visual_enabled) {
                visual_mono[visual_count++] = visual_sum / static_cast<float>(channels);
                if (visual_count == visual_mono.size() || frame + 1U == frames) {
                    visual_pcm_tap_.write(visual_mono.data(), visual_count, sample_rate);
                    visual_count = 0U;
                }
            }
        }
        if (frames == 0U || gain <= 0.0F) {
            output_peak_linear_.store(0.0F, std::memory_order_release);
            output_meter_valid_generation_.store(0U,
                                                  std::memory_order_release);
        } else {
            const float previous_peak =
                output_meter_valid_generation_.load(
                    std::memory_order_relaxed) == meter_generation
                ? output_peak_linear_.load(std::memory_order_relaxed)
                : 0.0F;
            const float release_gain = integer_power(
                output_meter_release_per_frame_.load(
                    std::memory_order_relaxed),
                requested_frames);
            if (output_meter_generation_.load(std::memory_order_acquire)
                    == meter_generation
                && state_.load(std::memory_order_acquire)
                       == EngineState::Playing
                && !muted_.load(std::memory_order_acquire)
                && !device_lost_.load(std::memory_order_acquire)
                && !seeking_.load(std::memory_order_acquire)) {
                output_peak_linear_.store(
                    (std::max)(block_peak, previous_peak * release_gain),
                    std::memory_order_release);
                output_meter_valid_generation_.store(
                    meter_generation, std::memory_order_release);
            } else {
                output_meter_valid_generation_.store(
                    0U, std::memory_order_release);
            }
        }
        tap_spectrum(output, frames, channels);
        if (frames < requested_frames) visual_pcm_tap_.invalidate();
        std::fill(output + frames * channels,
                  output + requested_frames * channels,
                  0.0F);
        publish_output_levels(output, requested_frames, channels);

        rendered_frames_total_.store(rendered, std::memory_order_release);
        publish_pending_transition_locked(rendered);
        if (frames > 0U
            && source_generation
                == published_mapper_generation_.load(
                    std::memory_order_relaxed)) {
            consumed_source_frame_.store(source_frame_after,
                                         std::memory_order_release);
        }
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
            if (state_.compare_exchange_strong(
                    expected, EngineState::Stopped,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire)) {
                published_mapper_generation_.store(
                    next_mapper_generation(), std::memory_order_release);
            }
        }
        transition_version_.store(epoch + 2U, std::memory_order_release);
    }

    [[nodiscard]] std::size_t buffered_frames() const noexcept
    {
        return ring_buffer_ == nullptr ? 0U : ring_buffer_->available_frames();
    }

    [[nodiscard]] bool end_of_stream() const noexcept
    {
        return decode_eof_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool device_lost() const noexcept
    {
        return device_lost_.load(std::memory_order_acquire);
    }

    ag_result retry_device() noexcept
    {
        const std::lock_guard<std::recursive_mutex> control_lock(
            control_mutex_);
        if (!device_lost_.load(std::memory_order_acquire)) {
            return AG_OK;
        }
        invalidate_output_meter();
        if (!loaded_) {
            // No media loaded: nothing to reinitialize. Clear the stale flag
            // so callers (and tests) see the recovered state. The device will
            // be initialized fresh on the next load().
            device_lost_.store(false, std::memory_order_release);
            terminal_error_.store(AG_OK, std::memory_order_release);
            state_.store(EngineState::Stopped, std::memory_order_release);
            return AG_OK;
        }
        hard_invalidate_scratch();
        stop_decode_thread();
        {
            const std::lock_guard<std::recursive_mutex> device_lock(
                device_mutex_);
            // A real notification does not stop the backend. Quiesce it
            // before releasing the RT-held Scratch bank.
            if (device_initialized_) {
                stop_output();
                ma_device_uninit(&device_);
                device_initialized_ = false;
            }
        }
        destroy_scratch_resources_after_output_stopped();
        std::int64_t recovery_frame = 0;
        const ag_result reposition_result =
            reposition_decoder_for_recovery(recovery_frame);
        if (reposition_result != AG_OK) return reposition_result;
        ag_result device_result = AG_OK;
        {
            const std::lock_guard<std::recursive_mutex> device_lock(
                device_mutex_);
            device_result = initialize_device();
        }
        if (device_result != AG_OK) {
            return device_result;
        }
        const std::uint64_t epoch = begin_timeline_write();
        if (ring_buffer_ != nullptr) ring_buffer_->clear();
        reset_timeline_locked(recovery_frame);
        device_lost_.store(false, std::memory_order_release);
        terminal_error_.store(AG_OK, std::memory_order_release);
        state_.store(EngineState::Paused, std::memory_order_release);
        end_timeline_write(epoch);
        const ag_result thread_result = start_decode_thread();
        if (thread_result != AG_OK) return enter_error(thread_result);
        seek_cv_.notify_all();
        return AG_OK;
    }

    void simulate_device_loss() noexcept
    {
        const std::lock_guard<std::recursive_mutex> control_lock(
            control_mutex_);
        hard_invalidate_scratch();
        invalidate_output_meter();
        device_lost_.store(true, std::memory_order_release);
        stop_output();
        stop_decode_thread();
        destroy_scratch_resources_after_output_stopped();
        const std::uint64_t epoch = begin_timeline_write();
        decode_mapper_generation_ = next_mapper_generation();
        published_mapper_generation_.store(decode_mapper_generation_,
                                           std::memory_order_release);
        terminal_error_.store(AG_DEVICE_ERROR,
                              std::memory_order_release);
        state_.store(EngineState::Error, std::memory_order_release);
        end_timeline_write(epoch);
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
            const bool scratch_active = scratch_rendering();
            const bool resume =
                state_.load(std::memory_order_acquire)
                == EngineState::Playing;
            if (scratch_active) {
                hard_invalidate_scratch();
                if (stop_output() != AG_OK) {
                    return enter_error(AG_DEVICE_ERROR);
                }
            }
            stop_decode_thread();
            if (scratch_active) {
                destroy_scratch_resources_after_output_stopped();
            }
            const auto restart = [this, scratch_active, resume]() noexcept {
                const ag_result result = start_decode_thread();
                if (result != AG_OK) return result;
                if (scratch_active && resume && start_output() != AG_OK) {
                    return enter_error(AG_DEVICE_ERROR);
                }
                return AG_OK;
            };
            sync_session_index_to_published();
            if (!session_.queue_next(std::move(path))) {
                const ag_result restart_result = restart();
                return restart_result == AG_OK
                    ? AG_INVALID_ARGUMENT : restart_result;
            }
            const std::uint64_t epoch = begin_timeline_write();
            publish_session_state_locked();
            end_timeline_write(epoch);
            return restart();
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

    void set_visual_pcm_enabled(bool enabled) noexcept
    {
        visual_pcm_tap_.set_enabled(enabled);
        visual_pcm_enabled_.store(enabled, std::memory_order_relaxed);
    }

    void read_visual_pcm(ag_visual_pcm_snapshot& snapshot) noexcept
    {
        visual_pcm_tap_.read(snapshot);
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
            if (!utf8_id.empty() || exclusive) return AG_INVALID_ARGUMENT;
            if (!device_lost_.load(std::memory_order_acquire)) return AG_OK;
        }
        const std::lock_guard<std::recursive_mutex> control_lock(
            control_mutex_);
        const bool recovering =
            device_lost_.load(std::memory_order_acquire);
        const bool scratch_resources = scratch_worker_ != nullptr;
        if (recovering && scratch_resources) {
            hard_invalidate_scratch();
        }
        std::int64_t recovery_frame = 0;
        if (recovering) {
            stop_decode_thread();
        }
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
            if (!recovering && scratch_resources) {
                hard_invalidate_scratch();
            }
            invalidate_output_meter();
            OutputMeterAttemptGuard meter_attempt(output_meter_generation_);

            const EngineState previous_state =
                state_.load(std::memory_order_acquire);
            const bool resume = previous_state == EngineState::Playing;
            if (resume) {
                arm_output_device_switch_test_barrier(1, true);
            }
            if (output_device_switch_test_failure()
                == OutputDeviceSwitchTestFailure::BeforeStateSnapshot) {
                throw std::bad_alloc{};
            }
            const std::string previous_id = selected_device_id_;
            const bool previous_exclusive = exclusive_mode_;

            if (device_initialized_) {
                if (stop_output() != AG_OK && !recovering) {
                    return AG_DEVICE_ERROR;
                }
                ma_device_uninit(&device_);
                device_initialized_ = false;
            }
            if (scratch_resources) {
                destroy_scratch_resources_after_output_stopped();
            }
            invalidate_output_meter();
            if (resume) {
                arm_output_device_switch_test_barrier(2, false);
            }
            if (recovering) {
                const ag_result recovery_result =
                    reposition_decoder_for_recovery(recovery_frame);
                if (recovery_result != AG_OK) return recovery_result;
            }

            selected_device_id_ = std::move(utf8_id);
            exclusive_mode_ = exclusive;
            if (!loaded_) {
                meter_attempt.succeed();
                return AG_OK;
            }

            const ag_result result = initialize_device();
            if (result == AG_OK) {
                if (recovering) {
                    const std::uint64_t epoch = begin_timeline_write();
                    if (ring_buffer_ != nullptr) ring_buffer_->clear();
                    reset_timeline_locked(recovery_frame);
                    terminal_error_.store(AG_OK, std::memory_order_release);
                    state_.store(EngineState::Paused,
                                 std::memory_order_release);
                    end_timeline_write(epoch);
                    const ag_result thread_result = start_decode_thread();
                    if (thread_result != AG_OK) {
                        return enter_error(thread_result);
                    }
                } else if (previous_state != EngineState::Error) {
                    terminal_error_.store(AG_OK,
                                          std::memory_order_release);
                }
                device_lost_.store(false, std::memory_order_release);
                seek_cv_.notify_all();
                if (!resume || start_output() == AG_OK) {
                    meter_attempt.succeed();
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
                if (recovering) {
                    const std::uint64_t epoch = begin_timeline_write();
                    if (ring_buffer_ != nullptr) ring_buffer_->clear();
                    reset_timeline_locked(recovery_frame);
                    terminal_error_.store(AG_OK, std::memory_order_release);
                    state_.store(EngineState::Paused,
                                 std::memory_order_release);
                    end_timeline_write(epoch);
                    const ag_result thread_result = start_decode_thread();
                    if (thread_result != AG_OK) {
                        return enter_error(thread_result);
                    }
                }
                device_lost_.store(false, std::memory_order_release);
                seek_cv_.notify_all();
                if (!resume || start_output() == AG_OK) {
                    return AG_DEVICE_ERROR;
                }
            }
            device_lost_.store(true, std::memory_order_release);
            const std::uint64_t epoch = begin_timeline_write();
            if (ring_buffer_ != nullptr) ring_buffer_->clear();
            published_mapper_generation_.store(
                next_mapper_generation(), std::memory_order_release);
            terminal_error_.store(AG_DEVICE_ERROR,
                                  std::memory_order_release);
            state_.store(EngineState::Error, std::memory_order_release);
            end_timeline_write(epoch);
            return AG_DEVICE_ERROR;
        } catch (...) {
            return AG_INTERNAL_ERROR;
        }
    }

    [[nodiscard]] bool exclusive_mode_active() const noexcept
    {
        return device_initialized_ && active_exclusive_mode_;
    }

    void set_output_device_switch_test_barrier(
        OutputDeviceSwitchTestBarrier* const barrier) noexcept
    {
        output_device_switch_test_barrier_.store(barrier,
                                                 std::memory_order_release);
    }

    void fail_next_equalizer_submit_for_test() noexcept
    {
        fail_next_equalizer_submit_for_test_.store(true,
                                                   std::memory_order_release);
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
            self->invalidate_output_meter();
            self->publish_device_loss_from_backend_callback();
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
        if (output_device_switch_test_failure()
            == OutputDeviceSwitchTestFailure::StopOutput) {
            return AG_DEVICE_ERROR;
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
        // Publish the mailbox as reachable before thread construction. This
        // lets immediate startup commands queue without running decoder work
        // on the caller, while thread-construction failure rolls the promise
        // back below.
        decode_running_.store(true, std::memory_order_release);
        try {
            decode_thread_ = std::thread([this] { decode_loop(); });
            return AG_OK;
        } catch (...) {
            decode_running_.store(false, std::memory_order_release);
            stop_decode_.store(true, std::memory_order_release);
            return AG_INTERNAL_ERROR;
        }
    }

    void abandon_time_pitch_request_after_decode_stop() noexcept
    {
        const TimePitchRequestState state =
            time_pitch_request_state_.load(std::memory_order_acquire);
        if (state == TimePitchRequestState::Idle) return;
        const std::uint64_t generation =
            requested_time_pitch_generation_.load(std::memory_order_acquire);
        seek_requested_.store(false, std::memory_order_release);
        seek_done_.store(false, std::memory_order_release);
        time_pitch_change_requested_.store(false, std::memory_order_release);
        seeking_.store(false, std::memory_order_release);
        completed_time_pitch_result_.store(AG_CANCELLED,
                                           std::memory_order_relaxed);
        completed_time_pitch_generation_.store(generation,
                                                std::memory_order_release);
        time_pitch_request_state_.store(TimePitchRequestState::Idle,
                                        std::memory_order_release);
        seek_cv_.notify_all();
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
        abandon_time_pitch_request_after_decode_stop();
    }

    [[nodiscard]] bool service_scratch_decode(
        PlaybackDecodedBlock& block,
        std::size_t& frame_offset) noexcept
    {
        ScratchCommitState commit =
            scratch_commit_state_.load(std::memory_order_acquire);
        if (commit == ScratchCommitState::Pending) {
            if (scratch_phase_.load(std::memory_order_acquire)
                != ScratchPhase::CommitPending) {
                return true;
            }
            ScratchCommitState expected = ScratchCommitState::Pending;
            if (scratch_commit_state_.compare_exchange_strong(
                    expected, ScratchCommitState::Executing,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire)) {
                const std::uint64_t generation =
                    scratch_requested_commit_generation_.load(
                        std::memory_order_acquire);
                const std::int64_t target = scratch_commit_target_frame_.load(
                    std::memory_order_acquire);
                ScratchPhase expected_phase = ScratchPhase::CommitPending;
                if (!scratch_phase_.compare_exchange_strong(
                        expected_phase, ScratchPhase::CommitExecuting,
                        std::memory_order_acq_rel,
                        std::memory_order_acquire)) {
                    ScratchCommitState executing =
                        ScratchCommitState::Executing;
                    if (scratch_commit_state_.compare_exchange_strong(
                            executing, ScratchCommitState::Cancelled,
                            std::memory_order_acq_rel,
                            std::memory_order_acquire)) {
                        scratch_commit_result_.store(
                            AG_CANCELLED, std::memory_order_relaxed);
                        scratch_completed_commit_generation_.store(
                            generation, std::memory_order_release);
                        seek_cv_.notify_all();
                    }
                    return true;
                }
                notify_scratch_commit_test_hook(2);
                const std::uint64_t commit_epoch =
                    scratch_commit_epoch_.load(std::memory_order_acquire);
                if (device_lost_.load(std::memory_order_acquire)
                    || scratch_media_epoch_.load(std::memory_order_acquire)
                        != commit_epoch
                    || scratch_phase_.load(std::memory_order_acquire)
                        != ScratchPhase::CommitExecuting) {
                    ScratchCommitState executing =
                        ScratchCommitState::Executing;
                    if (scratch_commit_state_.compare_exchange_strong(
                            executing, ScratchCommitState::Cancelled,
                            std::memory_order_acq_rel,
                            std::memory_order_acquire)) {
                        scratch_commit_result_.store(
                            AG_CANCELLED, std::memory_order_relaxed);
                        scratch_completed_commit_generation_.store(
                            generation, std::memory_order_release);
                        seek_cv_.notify_all();
                    }
                    return true;
                }
                ScratchCommitState executing = ScratchCommitState::Executing;
                if (!scratch_commit_state_.compare_exchange_strong(
                        executing, ScratchCommitState::SeekStarted,
                        std::memory_order_acq_rel,
                        std::memory_order_acquire)) {
                    return true;
                }
                scratch_physical_seek_count_.fetch_add(
                    1U, std::memory_order_relaxed);
                const ag_result result = decoder_.reconfigure_and_seek_frame(
                    current_time_pitch_config(), target);
                if (result == AG_OK) {
                    const std::uint64_t timeline = begin_timeline_write();
                    ring_buffer_->clear();
                    decode_track_index_ = published_track_index_.load(
                        std::memory_order_acquire);
                    reset_timeline_locked(target);
                    end_timeline_write(timeline);
                    block = {};
                    frame_offset = 0U;
                }
                scratch_commit_result_.store(result,
                                             std::memory_order_relaxed);
                scratch_completed_commit_generation_.store(
                    generation, std::memory_order_release);
                scratch_commit_state_.store(ScratchCommitState::Complete,
                                            std::memory_order_release);
                seek_cv_.notify_all();
                return true;
            }
            commit = expected;
        }

        const ScratchPhase phase =
            scratch_phase_.load(std::memory_order_acquire);
        if (phase != ScratchPhase::Active
            && phase != ScratchPhase::Ending) {
            return phase == ScratchPhase::CommitPending
                || phase == ScratchPhase::CommitExecuting;
        }

        if (phase == ScratchPhase::Active
            && scratch_rebase_requested_.exchange(
                false, std::memory_order_acq_rel)) {
            const std::uint64_t request_epoch =
                scratch_rebase_epoch_.load(std::memory_order_acquire);
            const std::uint64_t request_generation =
                scratch_rebase_generation_.load(std::memory_order_acquire);
            if (request_epoch
                    == scratch_media_epoch_.load(std::memory_order_acquire)
                && request_generation
                    == scratch_window_generation_.load(
                        std::memory_order_acquire)
                && scratch_worker_) {
                const std::uint64_t next_generation =
                    scratch_generation_counter_.fetch_add(
                        1U, std::memory_order_relaxed) + 1U;
                scratch_window_generation_.store(next_generation,
                                                  std::memory_order_release);
                scratch_backfill_pending_generation_.store(
                    next_generation, std::memory_order_release);
                const ScratchBackfillRequest request{
                    scratch_media_path_,
                    static_cast<std::uint32_t>(
                        sample_rate_.load(std::memory_order_acquire)),
                    static_cast<std::uint32_t>(channels_),
                    scratch_source_frame_count_,
                    scratch_rebase_anchor_.load(std::memory_order_acquire),
                    next_generation,
                    request_epoch,
                };
                if (!scratch_worker_->request(request)) {
                    scratch_backfill_pending_generation_.store(
                        0U, std::memory_order_release);
                    scratch_buffering_.store(true,
                                             std::memory_order_release);
                }
            }
        }

        std::unique_lock<std::mutex> lock(seek_mutex_);
        seek_cv_.wait_for(lock, std::chrono::milliseconds(1));
        return true;
    }

    void decode_loop() noexcept
    {
        struct RunningGuard {
            std::atomic<bool>& flag;
            std::condition_variable& cv;
            RunningGuard(std::atomic<bool>& f,
                         std::condition_variable& condition) noexcept
                : flag(f), cv(condition) {}
            ~RunningGuard() noexcept
            {
                flag.store(false, std::memory_order_release);
                cv.notify_all();
            }
        } guard{decode_running_, seek_cv_};
        try {
            PlaybackDecodedBlock block;
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
                    const bool time_pitch_change =
                        time_pitch_change_requested_.load(
                            std::memory_order_acquire);
                    const std::uint64_t time_pitch_generation =
                        requested_time_pitch_generation_.load(
                            std::memory_order_acquire);
                    const PlaybackTimePitchConfig requested_config =
                        time_pitch_change
                        ? PlaybackTimePitchConfig{
                            static_cast<double>(requested_time_pitch_ratio_.load(
                                std::memory_order_acquire)),
                            requested_keep_pitch_.load(
                                std::memory_order_acquire)}
                        : current_time_pitch_config();
                    if (time_pitch_change) {
                        notify_time_pitch_test_hook(time_pitch_generation, 3);
                    }
                    ag_result result = AG_OK;
                    std::uint64_t time_pitch_timeline_epoch = 0U;
                    bool time_pitch_timeline_active = false;
                    if (time_pitch_change) {
                        std::unique_ptr<PlaybackDecoder> replacement;
                        PlaybackTimePitchStage prepared_stage(factory_);
                        TimePitchRequestState expected =
                            TimePitchRequestState::Pending;
                        if (!time_pitch_request_state_.compare_exchange_strong(
                                expected, TimePitchRequestState::Preparing,
                                std::memory_order_acq_rel,
                                std::memory_order_acquire)) {
                            result = expected == TimePitchRequestState::TimedOut
                                ? AG_CANCELLED : AG_INTERNAL_ERROR;
                        } else {
                            result = prepare_time_pitch_decoder(
                                requested_config, target_ms, replacement,
                                prepared_stage);
                            const TimePitchRequestState prepared_state =
                                time_pitch_request_state_.load(
                                    std::memory_order_acquire);
                            if (prepared_state
                                == TimePitchRequestState::TimedOut) {
                                result = AG_CANCELLED;
                            } else if (result == AG_OK) {
                                expected = TimePitchRequestState::Preparing;
                                if (time_pitch_request_state_
                                        .compare_exchange_strong(
                                            expected,
                                            TimePitchRequestState::CommitReady,
                                            std::memory_order_acq_rel,
                                            std::memory_order_acquire)) {
                                    notify_time_pitch_test_hook(
                                        time_pitch_generation, 4);
                                    if (begin_time_pitch_timeline_write(
                                            time_pitch_timeline_epoch)) {
                                        time_pitch_timeline_active = true;
                                        notify_time_pitch_test_hook(
                                            time_pitch_generation, 5);
                                        expected =
                                            TimePitchRequestState::CommitReady;
                                        if (time_pitch_request_state_
                                                .compare_exchange_strong(
                                                    expected,
                                                    TimePitchRequestState::
                                                        CommitExecuting,
                                                    std::memory_order_acq_rel,
                                                    std::memory_order_acquire)) {
                                            result =
                                                commit_prepared_time_pitch_decoder(
                                                    requested_config, target_ms,
                                                    replacement,
                                                    std::move(prepared_stage));
                                        } else if (expected
                                                   == TimePitchRequestState::
                                                       TimedOut) {
                                            result = AG_CANCELLED;
                                        } else {
                                            result = AG_INTERNAL_ERROR;
                                        }
                                    } else {
                                        result =
                                            time_pitch_request_state_.load(
                                                std::memory_order_acquire)
                                                    == TimePitchRequestState::
                                                        TimedOut
                                                ? AG_CANCELLED
                                                : AG_INTERNAL_ERROR;
                                    }
                                } else if (expected
                                           == TimePitchRequestState::TimedOut) {
                                    result = AG_CANCELLED;
                                } else {
                                    result = AG_INTERNAL_ERROR;
                                }
                            }
                            if (result != AG_OK && replacement) {
                                if (!retired_time_pitch_decoder_) {
                                    retired_time_pitch_decoder_ =
                                        std::move(replacement);
                                    has_retired_time_pitch_decoder_.store(
                                        true, std::memory_order_release);
                                }
                            }
                        }
                        if (result != AG_OK && time_pitch_timeline_active) {
                            end_timeline_write(time_pitch_timeline_epoch);
                            time_pitch_timeline_active = false;
                        }
                    } else {
                        result = restore_published_decoder();
                    }
                    if (result == AG_OK && !time_pitch_change) {
                        result = decoder_.reconfigure_and_seek(
                            requested_config, target_ms);
                    }
                    if (result == AG_OK) {
                        const std::int64_t position_frames =
                            (target_ms
                                 * sample_rate_.load(
                                     std::memory_order_acquire)
                             + 999)
                            / 1'000;
                        decode_track_index_ = published_track_index_.load(
                            std::memory_order_acquire);
                        const std::uint64_t epoch = time_pitch_change
                            ? time_pitch_timeline_epoch
                            : begin_timeline_write();
                        ring_buffer_->clear();
                        if (time_pitch_change) {
                            time_pitch_ratio_.store(
                                static_cast<float>(requested_config.speed_ratio),
                                std::memory_order_release);
                            keep_pitch_.store(requested_config.keep_pitch,
                                              std::memory_order_release);
                        }
                        reset_timeline_locked(position_frames);
                        end_timeline_write(epoch);
                        time_pitch_timeline_active = false;
                        terminal_error_.store(AG_OK,
                                              std::memory_order_release);
                    }
                    seek_requested_.store(false,
                                         std::memory_order_release);
                    time_pitch_change_requested_.store(
                        false, std::memory_order_release);
                    seeking_.store(false, std::memory_order_release);
                    if (time_pitch_change) {
                        completed_time_pitch_result_.store(
                            result, std::memory_order_relaxed);
                        completed_time_pitch_generation_.store(
                            time_pitch_generation, std::memory_order_release);
                        time_pitch_request_state_.store(
                            TimePitchRequestState::Idle,
                            std::memory_order_release);
                        notify_time_pitch_test_hook(time_pitch_generation, 1);
                        notify_time_pitch_test_hook(time_pitch_generation, 2);
                    } else {
                        seek_result_.store(result, std::memory_order_release);
                        seek_done_.store(true, std::memory_order_release);
                    }
                    seek_cv_.notify_all();
                    // Discard any pre-seek decoded data so the next read
                    // fetches fresh samples from the new position.
                    if (result == AG_OK) {
                        block = {};
                        frame_offset = 0U;
                    }
                    if (result != AG_OK) {
                        if (time_pitch_change) {
                            continue;
                        }
                        // Seek failed: exit so the main thread can enter
                        // the error state and restart the decode thread.
                        return;
                    }
                    continue;
                }
                if (service_scratch_decode(block, frame_offset)) {
                    continue;
                }
                if (frame_offset < block.frames) {
                    const std::size_t written = ring_buffer_->write(
                        block.samples.data() + frame_offset
                            * static_cast<std::size_t>(channels_),
                        block.source_frame_after.data() + frame_offset,
                        block.frames - frame_offset,
                        block.source_generation);
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
                block.source_generation = decode_mapper_generation_;
                if (!block.end_of_stream) {
                    continue;
                }

                const std::size_t next_index =
                    session_.next_index_from(decode_track_index_);
                if (next_index == PlaybackSession::npos) {
                    // Container metadata can overstate the duration of VBR
                    // streams. The last raw source timestamp plus decoded
                    // frames is authoritative; stretched output frames are
                    // deliberately not used for source-media duration.
                    const int sample_rate =
                        sample_rate_.load(std::memory_order_acquire);
                    const std::int64_t pending_boundary =
                        pending_boundary_frame_.load(std::memory_order_acquire);
                    if (sample_rate > 0) {
                        if (pending_boundary >= 0) {
                            // The next track may already be decoded while
                            // the render thread still publishes the previous
                            // track. Preserve its exact duration for that
                            // pending hand-off rather than overwriting the
                            // current row's timeline.
                            pending_duration_ms_.store(
                                block.source_end_frame * 1'000 / sample_rate,
                                std::memory_order_release);
                        } else {
                            duration_ms_.store(
                                block.source_end_frame * 1'000 / sample_rate,
                                std::memory_order_release);
                        }
                    }
                    decode_eof_.store(true, std::memory_order_release);
                    // Keep the decoder-owning thread parked at EOF. A later
                    // seek can then wake it and reuse the open decoder instead
                    // of paying for a thread join/restart on every scrub near
                    // the end of a short track. The wait is dormant (zero
                    // polling CPU) and stop/unload wakes it through seek_cv_.
                    {
                        std::unique_lock<std::mutex> lock(seek_mutex_);
                        seek_cv_.wait(lock, [this] {
                            return seek_requested_.load(
                                       std::memory_order_acquire)
                                   || scratch_rendering()
                                   || stop_decode_.load(
                                          std::memory_order_acquire);
                        });
                    }
                    if (stop_decode_.load(std::memory_order_acquire)) {
                        return;
                    }
                    continue;
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
                drain_retired_time_pitch_decoder();
                const int current_sample_rate =
                    sample_rate_.load(std::memory_order_acquire);
                const PlaybackTimePitchConfig boundary_config{
                    1.0, keep_pitch_.load(std::memory_order_acquire)};
                int next_sample_rate = current_sample_rate;
                bool needs_device_reconfigure = false;
                if (next_index == decode_track_index_
                    && session_.mode() == PlaybackMode::RepeatOne) {
                    transition_result = decoder_.reconfigure_and_seek(
                        boundary_config, 0);
                } else if (match_track_sample_rate_.load(
                               std::memory_order_acquire)) {
                    decoder_.set_next_time_pitch(boundary_config);
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
                    decoder_.set_next_time_pitch(boundary_config);
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

                    const std::uint64_t epoch = begin_timeline_write();
                    sample_rate_.store(next_sample_rate,
                                       std::memory_order_release);
                    publish_equalizer_for_rate(next_sample_rate);
                    session_.set_index(next_index);
                    decode_track_index_ = next_index;
                    duration_ms_.store(
                        decoder_.metadata().duration_ms,
                        std::memory_order_release);
                    ring_buffer_->clear();
                    time_pitch_ratio_.store(1.0F,
                                            std::memory_order_release);
                    keep_pitch_.store(boundary_config.keep_pitch,
                                      std::memory_order_release);
                    publish_session_state_locked();
                    reset_timeline_locked(0);
                    if (transition_fade_ms_.load(
                            std::memory_order_acquire)
                        > 0) {
                        fade_boundary_frame_.store(
                            0, std::memory_order_release);
                    }
                    end_timeline_write(epoch);
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
                pending_keep_pitch_.store(boundary_config.keep_pitch,
                                          std::memory_order_relaxed);
                pending_time_pitch_reset_.store(true,
                                                std::memory_order_relaxed);
                decode_mapper_generation_ = next_mapper_generation();
                pending_mapper_generation_.store(
                    decode_mapper_generation_, std::memory_order_relaxed);
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
        const std::uint64_t epoch = begin_timeline_write();
        publish_pending_transition_locked(rendered);
        end_timeline_write(epoch);
    }

    void publish_pending_transition_locked(
        const std::int64_t rendered) noexcept
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

        visual_pcm_tap_.invalidate();
        const ag_result transition_error =
            pending_transition_error_.load(std::memory_order_relaxed);
        if (transition_error != AG_OK) {
            if (ring_buffer_ != nullptr) ring_buffer_->clear();
            consumed_source_frame_.store(0, std::memory_order_release);
            published_mapper_generation_.store(
                next_mapper_generation(), std::memory_order_release);
            terminal_error_.store(transition_error, std::memory_order_release);
            state_.store(EngineState::Error, std::memory_order_release);
            pending_transition_error_.store(AG_OK, std::memory_order_relaxed);
            pending_boundary_frame_.store(no_pending_boundary,
                                          std::memory_order_release);
            return;
        }

        track_start_frame_.store(boundary, std::memory_order_release);
        consumed_source_frame_.store(0, std::memory_order_release);
        published_mapper_generation_.store(
            pending_mapper_generation_.load(std::memory_order_relaxed),
            std::memory_order_release);
        duration_ms_.store(pending_duration_ms_.load(std::memory_order_relaxed),
                           std::memory_order_release);
        published_track_index_.store(
            pending_track_index_.load(std::memory_order_relaxed),
            std::memory_order_release);
        if (pending_time_pitch_reset_.load(std::memory_order_relaxed)) {
            time_pitch_ratio_.store(1.0F, std::memory_order_release);
            keep_pitch_.store(
                pending_keep_pitch_.load(std::memory_order_relaxed),
                std::memory_order_release);
            pending_time_pitch_reset_.store(false,
                                            std::memory_order_relaxed);
        }
        equalizer_.reset();
        pending_boundary_frame_.store(no_pending_boundary,
                                      std::memory_order_release);
    }

    ag_result restore_published_decoder() noexcept
    {
        drain_retired_time_pitch_decoder();
        if (editor_stream_) {
            return decoder_.is_open() ? AG_OK : AG_INVALID_ARGUMENT;
        }
        const std::size_t published_index =
            published_track_index_.load(std::memory_order_acquire);
        if (decode_track_index_ == published_index && decoder_.is_open()) {
            return AG_OK;
        }
        decoder_.set_next_time_pitch(current_time_pitch_config());
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
        hard_invalidate_scratch();
        stop_decode_thread();
        if (stop_output() != AG_OK) {
            return enter_error(AG_DEVICE_ERROR);
        }
        destroy_scratch_resources_after_output_stopped();
        drain_retired_time_pitch_decoder();

        const int previous_sample_rate =
            sample_rate_.load(std::memory_order_acquire);
        int next_sample_rate = previous_sample_rate;
        ag_result open_result = AG_OK;
        if (match_track_sample_rate_.load(std::memory_order_acquire)) {
            decoder_.set_next_time_pitch(
                {1.0, keep_pitch_.load(std::memory_order_acquire)});
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
            decoder_.set_next_time_pitch(
                {1.0, keep_pitch_.load(std::memory_order_acquire)});
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

        const bool boundaryKeepPitch =
            keep_pitch_.load(std::memory_order_acquire);
        const std::uint64_t epoch = begin_timeline_write();
        session_.set_index(index);
        decode_track_index_ = index;
        duration_ms_.store(decoder_.metadata().duration_ms,
                           std::memory_order_release);
        ring_buffer_->clear();
        time_pitch_ratio_.store(1.0F, std::memory_order_release);
        keep_pitch_.store(boundaryKeepPitch, std::memory_order_release);
        publish_session_state_locked();
        reset_timeline_locked(0);
        if (transition_fade_ms_.load(std::memory_order_acquire) > 0) {
            fade_boundary_frame_.store(0, std::memory_order_release);
        }
        end_timeline_write(epoch);
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

    void reset_timeline_locked(const std::int64_t position_frames) noexcept
    {
        visual_pcm_tap_.invalidate();
        invalidate_output_meter();
        decode_mapper_generation_ = next_mapper_generation();
        published_mapper_generation_.store(decode_mapper_generation_,
                                           std::memory_order_release);
        equalizer_.reset();
        rendered_frames_total_.store(0, std::memory_order_release);
        track_start_frame_.store(0, std::memory_order_release);
        produced_frames_total_.store(0, std::memory_order_release);
        consumed_source_frame_.store(position_frames,
                                     std::memory_order_release);
        pending_boundary_frame_.store(no_pending_boundary,
                                      std::memory_order_release);
        fade_boundary_frame_.store(no_pending_boundary,
                                   std::memory_order_release);
        pending_transition_error_.store(AG_OK, std::memory_order_release);
        pending_time_pitch_reset_.store(false, std::memory_order_release);
        pending_mapper_generation_.store(decode_mapper_generation_,
                                         std::memory_order_release);
        pending_track_index_.store(
            published_track_index_.load(std::memory_order_acquire),
            std::memory_order_release);
        pending_duration_ms_.store(duration_ms_.load(std::memory_order_acquire),
                                   std::memory_order_release);
        decode_eof_.store(false, std::memory_order_release);
    }

    [[nodiscard]] bool begin_time_pitch_timeline_write(
        std::uint64_t& epoch) noexcept
    {
        while (time_pitch_request_state_.load(std::memory_order_acquire)
               == TimePitchRequestState::CommitReady) {
            std::uint64_t candidate =
                transition_version_.load(std::memory_order_acquire);
            if ((candidate & 1U) == 0U
                && transition_version_.compare_exchange_weak(
                    candidate, candidate + 1U, std::memory_order_acq_rel,
                    std::memory_order_acquire)) {
                notify_timeline_test_hook(false);
                epoch = candidate;
                return true;
            }
            std::this_thread::yield();
        }
        return false;
    }

    [[nodiscard]] std::uint64_t begin_timeline_write() noexcept
    {
        for (;;) {
            std::uint64_t epoch =
                transition_version_.load(std::memory_order_acquire);
            if ((epoch & 1U) == 0U
                && transition_version_.compare_exchange_weak(
                    epoch, epoch + 1U, std::memory_order_acq_rel,
                    std::memory_order_acquire)) {
                notify_timeline_test_hook(false);
                return epoch;
            }
            std::this_thread::yield();
        }
    }

    void end_timeline_write(const std::uint64_t epoch) noexcept
    {
        transition_version_.store(epoch + 2U, std::memory_order_release);
    }

    void notify_timeline_test_hook(const bool realtime) noexcept
    {
        const TimelineTestHook hook =
            timeline_test_hook_.load(std::memory_order_acquire);
        if (hook != nullptr) {
            hook(timeline_test_context_.load(std::memory_order_relaxed),
                 realtime);
        }
    }

    void notify_time_pitch_test_hook(const std::uint64_t generation,
                                     const int phase) noexcept
    {
        const TimePitchTestHook hook =
            time_pitch_test_hook_.load(std::memory_order_acquire);
        if (hook != nullptr) {
            hook(time_pitch_test_context_.load(std::memory_order_relaxed),
                 generation, phase);
        }
    }

    void notify_scratch_commit_test_hook(const int phase) noexcept
    {
        const ScratchCommitTestHook hook =
            scratch_commit_test_hook_.load(std::memory_order_acquire);
        if (hook != nullptr) {
            hook(scratch_commit_test_context_.load(
                     std::memory_order_relaxed),
                 phase);
        }
    }

    void publish_session_state_locked() noexcept
    {
        published_track_index_.store(session_.index(),
                                     std::memory_order_release);
        published_track_count_.store(session_.size(),
                                     std::memory_order_release);
        published_mode_.store(static_cast<int>(session_.mode()),
                              std::memory_order_release);
    }

    void sync_session_index_to_published() noexcept
    {
        session_.set_index(
            published_track_index_.load(std::memory_order_acquire));
    }

    void set_decode_error(const ag_result result) noexcept
    {
        const std::uint64_t epoch = begin_timeline_write();
        if (ring_buffer_ != nullptr) ring_buffer_->clear();
        decode_mapper_generation_ = next_mapper_generation();
        published_mapper_generation_.store(decode_mapper_generation_,
                                           std::memory_order_release);
        terminal_error_.store(result, std::memory_order_release);
        decode_eof_.store(true, std::memory_order_release);
        state_.store(EngineState::Error, std::memory_order_release);
        end_timeline_write(epoch);
    }

    [[nodiscard]] std::uint64_t next_mapper_generation() noexcept
    {
        return mapper_generation_counter_.fetch_add(
                   1U, std::memory_order_relaxed) + 1U;
    }

    [[nodiscard]] PlaybackTimePitchConfig
    current_time_pitch_config() const noexcept
    {
        return {
            static_cast<double>(
                time_pitch_ratio_.load(std::memory_order_acquire)),
            keep_pitch_.load(std::memory_order_acquire)};
    }

    void drain_retired_time_pitch_decoder() noexcept
    {
        retired_time_pitch_decoder_.reset();
        has_retired_time_pitch_decoder_.store(false,
                                               std::memory_order_release);
    }

    void finish_scratch_session() noexcept
    {
        if (scratch_worker_) scratch_worker_->cancel();
        scratch_ready_.store(false, std::memory_order_release);
        scratch_buffering_.store(false, std::memory_order_release);
        scratch_exit_complete_.store(true, std::memory_order_release);
        scratch_rebase_requested_.store(false, std::memory_order_release);
        scratch_backfill_pending_generation_.store(
            0U, std::memory_order_release);
        scratch_commit_state_.store(ScratchCommitState::Idle,
                                    std::memory_order_release);
        scratch_requested_rate_.store(0.0F, std::memory_order_release);
        if (!scratch_was_playing_) {
            (void)stop_output();
        }
        scratch_phase_.store(ScratchPhase::Idle,
                             std::memory_order_release);
    }

    // Device notifications run on a backend callback thread. Keep this path
    // strictly atomic: no worker, lease, mailbox, lock, wait, allocation, I/O,
    // or condition-variable operation is permitted here.
    void invalidate_scratch_from_device_callback() noexcept
    {
        (void)scratch_media_epoch_.fetch_add(
            1U, std::memory_order_acq_rel);
        scratch_phase_.store(ScratchPhase::Idle,
                             std::memory_order_release);
        scratch_requested_rate_.store(0.0F, std::memory_order_release);
        scratch_ready_.store(false, std::memory_order_release);
        scratch_buffering_.store(false, std::memory_order_release);
        scratch_exit_complete_.store(true, std::memory_order_release);
        scratch_rebase_requested_.store(false, std::memory_order_release);
        scratch_backfill_pending_generation_.store(
            0U, std::memory_order_release);
        for (int attempt = 0; attempt < 2; ++attempt) {
            ScratchCommitState expected =
                scratch_commit_state_.load(std::memory_order_acquire);
            if (expected != ScratchCommitState::Pending
                && expected != ScratchCommitState::Executing) {
                break;
            }
            if (scratch_commit_state_.compare_exchange_strong(
                    expected, ScratchCommitState::Cancelled,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire)) {
                scratch_commit_result_.store(AG_CANCELLED,
                                             std::memory_order_release);
                scratch_completed_commit_generation_.store(
                    scratch_requested_commit_generation_.load(
                        std::memory_order_acquire),
                    std::memory_order_release);
                break;
            }
        }
    }

    void publish_device_loss_from_backend_callback() noexcept
    {
        invalidate_scratch_from_device_callback();
        device_lost_.store(true, std::memory_order_release);
        published_mapper_generation_.store(
            next_mapper_generation(), std::memory_order_release);
        terminal_error_.store(AG_DEVICE_ERROR, std::memory_order_release);
        state_.store(EngineState::Error, std::memory_order_release);
    }

    void hard_invalidate_scratch() noexcept
    {
        invalidate_scratch_from_device_callback();
        if (scratch_worker_) scratch_worker_->cancel();
        seek_cv_.notify_all();
    }

    void destroy_scratch_resources_after_output_stopped() noexcept
    {
        if (scratch_worker_ && scratch_lease_) {
            (void)scratch_worker_->release(scratch_lease_);
        }
        if (scratch_renderer_) scratch_renderer_->cancel_immediately();
        if (scratch_worker_) scratch_worker_->shutdown();
        scratch_renderer_.reset();
        scratch_worker_.reset();
        scratch_lease_ = {};
        scratch_media_path_.clear();
        scratch_source_frame_count_ = 0;
        scratch_commit_state_.store(ScratchCommitState::Idle,
                                    std::memory_order_release);
    }

    static bool interrupt_time_pitch_candidate(void* const context) noexcept
    {
        auto* const self = static_cast<Impl*>(context);
        if (self == nullptr) return true;
        if (self->stop_decode_.load(std::memory_order_acquire)) return true;
        const TimePitchRequestState state =
            self->time_pitch_request_state_.load(std::memory_order_acquire);
        return state == TimePitchRequestState::TimedOut;
    }

    void shutdown_loaded_media() noexcept
    {
        hard_invalidate_scratch();
        state_.store(EngineState::Stopped, std::memory_order_release);
        stop_output();
        stop_decode_thread();
        destroy_scratch_resources_after_output_stopped();
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
        drain_retired_time_pitch_decoder();
        stream_source_active_ = false;
        editor_stream_ = false;
        ring_buffer_.reset();
        session_.clear();
        loaded_ = false;
        sample_rate_.store(0, std::memory_order_release);
        channels_ = 0;
        duration_ms_.store(0, std::memory_order_release);
        decode_track_index_ = 0U;
        const std::uint64_t epoch = begin_timeline_write();
        publish_session_state_locked();
        reset_timeline_locked(0);
        end_timeline_write(epoch);
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
        hard_invalidate_scratch();
        stop_output();
        stop_decode_thread();
        destroy_scratch_resources_after_output_stopped();
        drain_retired_time_pitch_decoder();
        const std::uint64_t epoch = begin_timeline_write();
        if (ring_buffer_ != nullptr) ring_buffer_->clear();
        decode_mapper_generation_ = next_mapper_generation();
        published_mapper_generation_.store(decode_mapper_generation_,
                                           std::memory_order_release);
        terminal_error_.store(result, std::memory_order_release);
        decode_eof_.store(true, std::memory_order_release);
        state_.store(EngineState::Error, std::memory_order_release);
        end_timeline_write(epoch);
        return result;
    }

    ag_result prepare_time_pitch_decoder(
        const PlaybackTimePitchConfig config,
        const std::int64_t target_ms,
        std::unique_ptr<PlaybackDecoder>& replacement,
        PlaybackTimePitchStage& prepared_stage) noexcept
    {
        if (!decoder_.uses_stream() && session_.size() > 0U) {
            const std::size_t published_index =
                published_track_index_.load(std::memory_order_acquire);
            try {
                // The previous live decoder is retained across commit so the
                // point-of-no-return never closes FFmpeg input. Reclaim it in
                // this cancellable preparation phase; track/recovery/error
                // paths also drain it after the decode thread is safe.
                drain_retired_time_pitch_decoder();
                const TimePitchRequestState request_state =
                    time_pitch_request_state_.load(std::memory_order_acquire);
                if (request_state == TimePitchRequestState::TimedOut) {
                    return AG_CANCELLED;
                }
                auto candidate = std::make_unique<PlaybackDecoder>(factory_);
                ag_result result = candidate->set_time_pitch(config);
                if (result == AG_OK) {
                    result = candidate->open_interruptible(
                        session_.path_at(published_index),
                        sample_rate_.load(std::memory_order_acquire),
                        channels_, &Impl::interrupt_time_pitch_candidate,
                        this);
                }
                if (result == AG_OK && target_ms > 0) {
                    result = candidate->reconfigure_and_seek(config,
                                                             target_ms);
                }
                if (result == AG_OK) {
                    candidate->clear_interrupt_callback();
                }
                replacement = std::move(candidate);
                return result;
            } catch (...) {
                return AG_INTERNAL_ERROR;
            }
        }

        return decoder_.prepare_time_pitch_stage(config, prepared_stage);
    }

    ag_result commit_prepared_time_pitch_decoder(
        const PlaybackTimePitchConfig config,
        const std::int64_t target_ms,
        std::unique_ptr<PlaybackDecoder>& replacement,
        PlaybackTimePitchStage prepared_stage) noexcept
    {
        if (replacement) {
            if (retired_time_pitch_decoder_) return AG_INTERNAL_ERROR;
            decoder_.swap(*replacement);
            retired_time_pitch_decoder_ = std::move(replacement);
            has_retired_time_pitch_decoder_.store(
                true, std::memory_order_release);
            decode_track_index_ =
                published_track_index_.load(std::memory_order_acquire);
            return AG_OK;
        }
        return decoder_.commit_prepared_time_pitch(
            config, target_ms, std::move(prepared_stage));
    }

    ag_result reposition_decoder_for_recovery(
        std::int64_t& recovery_frame) noexcept
    {
        const int sample_rate =
            sample_rate_.load(std::memory_order_acquire);
        if (sample_rate <= 0) return AG_INTERNAL_ERROR;
        const std::int64_t consumed_frame =
            consumed_source_frame_.load(std::memory_order_acquire);
        ag_result result = restore_published_decoder();
        if (result == AG_OK) {
            result = decoder_.reconfigure_and_seek_frame(
                current_time_pitch_config(), consumed_frame);
        }
        if (result == AG_OK) {
            recovery_frame = consumed_frame;
        }
        return result;
    }

    void tap_spectrum(const float* output,
                      const std::size_t frames,
                      const std::size_t channels,
                      const bool visual_only = false) noexcept
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
            if (visual_only) {
                visual_pcm_tap_.write(mono.data(), count,
                                      sample_rate_.load(std::memory_order_acquire));
            } else {
                (void)spectrum_tap_.write(mono.data(), count);
            }
            offset += count;
        }
    }

    void publish_output_levels(const float* output,
                               const std::size_t frames,
                               const std::size_t channels) noexcept
    {
        float left_peak = 0.0F;
        float right_peak = 0.0F;
        double left_square_sum = 0.0;
        double right_square_sum = 0.0;

        if (output != nullptr && frames > 0U
            && (channels == 1U || channels == 2U)) {
            for (std::size_t frame = 0U; frame < frames; ++frame) {
                const float left = output[frame * channels];
                const float right = channels == 1U
                    ? left : output[frame * channels + 1U];
                if (std::isfinite(left)) {
                    left_peak = (std::max)(left_peak, std::abs(left));
                    left_square_sum += static_cast<double>(left)
                        * static_cast<double>(left);
                }
                if (std::isfinite(right)) {
                    right_peak = (std::max)(right_peak, std::abs(right));
                    right_square_sum += static_cast<double>(right)
                        * static_cast<double>(right);
                }
            }
            const double denominator = static_cast<double>(frames);
            output_left_peak_.store(
                std::clamp(left_peak, 0.0F, 1.0F),
                std::memory_order_relaxed);
            output_right_peak_.store(
                std::clamp(right_peak, 0.0F, 1.0F),
                std::memory_order_relaxed);
            output_left_rms_.store(
                std::clamp(static_cast<float>(
                    std::sqrt(left_square_sum / denominator)),
                    0.0F, 1.0F),
                std::memory_order_relaxed);
            output_right_rms_.store(
                std::clamp(static_cast<float>(
                    std::sqrt(right_square_sum / denominator)),
                    0.0F, 1.0F),
                std::memory_order_relaxed);
            return;
        }

        output_left_peak_.store(0.0F, std::memory_order_relaxed);
        output_right_peak_.store(0.0F, std::memory_order_relaxed);
        output_left_rms_.store(0.0F, std::memory_order_relaxed);
        output_right_rms_.store(0.0F, std::memory_order_relaxed);
    }

    void publish_equalizer_for_rate(const int sample_rate) noexcept
    {
        const float release_per_frame = sample_rate > 0
            ? std::pow(10.0F,
                       -output_meter_release_db_per_second
                           / (20.0F * static_cast<float>(sample_rate)))
            : 0.0F;
        output_meter_release_per_frame_.store(release_per_frame,
                                              std::memory_order_release);
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

    void invalidate_output_meter() noexcept
    {
        visual_pcm_tap_.invalidate();
        invalidate_output_level_meter();
    }

    void invalidate_output_level_meter() noexcept
    {
        output_meter_generation_.fetch_add(1U, std::memory_order_acq_rel);
    }

    void wait_at_output_device_switch_test_barrier() noexcept
    {
        OutputDeviceSwitchTestBarrier* const barrier =
            output_device_switch_test_barrier_.load(std::memory_order_acquire);
        if (barrier == nullptr) {
            return;
        }
        int phase = barrier->armed_phase.load(std::memory_order_acquire);
        if (phase == 0
            || !barrier->armed_phase.compare_exchange_strong(
                phase, 0, std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            return;
        }
        barrier->entered_phase.store(phase, std::memory_order_release);
        while (barrier->release_phase.load(std::memory_order_acquire) < phase
               && !barrier->cancelled.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
    }

    void arm_output_device_switch_test_barrier(const int phase,
                                                const bool wait) noexcept
    {
        OutputDeviceSwitchTestBarrier* const barrier =
            output_device_switch_test_barrier_.load(std::memory_order_acquire);
        if (barrier == nullptr) {
            return;
        }
        barrier->armed_phase.store(phase, std::memory_order_release);
        while (wait
               && barrier->entered_phase.load(std::memory_order_acquire)
                      < phase
               && !barrier->cancelled.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
    }

    [[nodiscard]] OutputDeviceSwitchTestFailure
    output_device_switch_test_failure() const noexcept
    {
        OutputDeviceSwitchTestBarrier* const barrier =
            output_device_switch_test_barrier_.load(std::memory_order_acquire);
        return barrier == nullptr
            ? OutputDeviceSwitchTestFailure::None
            : barrier->failure.load(std::memory_order_acquire);
    }

    AudioBackend backend_;
    static constexpr std::int64_t no_pending_boundary = -1;
    static constexpr std::int64_t publishing_boundary = -2;
    std::size_t buffer_frames_;
    TimePitchEngineFactory factory_;
    PlaybackDecoder decoder_;
    std::unique_ptr<PlaybackDecoder> retired_time_pitch_decoder_;
    std::atomic<bool> has_retired_time_pitch_decoder_{false};
    std::unique_ptr<PcmRingBuffer> ring_buffer_;
    std::unique_ptr<ScratchBackfillWorker> scratch_worker_;
    std::unique_ptr<ScratchRenderer> scratch_renderer_;
    ScratchBackfillLease scratch_lease_{};
    // Only control_mutex_-serialized begin/end lifecycle transitions publish.
    // Rate updates and decode-thread generation changes use lock-free atomics.
    ScratchCommandMailbox scratch_render_mailbox_;
    ScratchCommand scratch_rt_command_{};
    std::string scratch_media_path_;
    std::int64_t scratch_source_frame_count_{};
    bool scratch_was_playing_{};
    std::atomic<ScratchPhase> scratch_phase_{ScratchPhase::Idle};
    std::atomic<ScratchCommitState> scratch_commit_state_{
        ScratchCommitState::Idle};
    std::atomic<std::uint64_t> scratch_media_epoch_{1U};
    std::atomic<std::uint64_t> scratch_generation_counter_{0U};
    std::atomic<std::uint64_t> scratch_window_generation_{0U};
    std::atomic<std::uint64_t> scratch_backfill_pending_generation_{0U};
    std::atomic<std::int64_t> scratch_source_frame_{0};
    static_assert(std::atomic<float>::is_always_lock_free);
    std::atomic<float> scratch_requested_rate_{0.0F};
    std::atomic<bool> scratch_ready_{false};
    std::atomic<bool> scratch_buffering_{false};
    std::atomic<bool> scratch_exit_complete_{true};
    std::atomic<bool> scratch_rebase_requested_{false};
    std::atomic<std::int64_t> scratch_rebase_anchor_{0};
    std::atomic<std::uint64_t> scratch_rebase_generation_{0U};
    std::atomic<std::uint64_t> scratch_rebase_epoch_{0U};
    std::atomic<std::uint64_t> scratch_commit_generation_counter_{0U};
    std::atomic<std::uint64_t> scratch_requested_commit_generation_{0U};
    std::atomic<std::uint64_t> scratch_completed_commit_generation_{0U};
    std::atomic<std::int64_t> scratch_commit_target_frame_{0};
    std::atomic<std::uint64_t> scratch_commit_epoch_{0U};
    std::atomic<ag_result> scratch_commit_result_{AG_OK};
    std::atomic<std::uint64_t> scratch_physical_seek_count_{0U};
    std::atomic<float> output_left_peak_{0.0F};
    std::atomic<float> output_right_peak_{0.0F};
    std::atomic<float> output_left_rms_{0.0F};
    std::atomic<float> output_right_rms_{0.0F};
    PcmRingBuffer spectrum_tap_{spectrum_tap_capacity, 1U};
    VisualPcmTap visual_pcm_tap_;
    std::atomic<bool> visual_pcm_enabled_{false};
    std::array<float, spectrum_fft_size> spectrum_history_{};
    std::array<float, spectrum_max_bins> spectrum_smoothed_{};
    std::size_t spectrum_history_write_ = 0U;
    std::size_t spectrum_history_filled_ = 0U;
    ma_context context_{};
    ma_device device_{};
    std::atomic<bool> context_initialized_{false};
    std::atomic<bool> device_initialized_{false};
    bool loaded_ = false;
    bool stream_source_active_ = false;
    bool editor_stream_ = false;
    std::string selected_device_id_;
    bool exclusive_mode_ = false;
    std::atomic<bool> active_exclusive_mode_{false};
    std::atomic<int> sample_rate_{0};
    std::atomic<int> channels_{0};
    std::atomic<std::int64_t> duration_ms_{0};
    std::thread decode_thread_;
    std::atomic<bool> stop_decode_{false};
    std::atomic<bool> decode_eof_{false};
    std::atomic<bool> device_lost_{false};
    std::atomic<bool> decode_running_{false};
    std::atomic<bool> seeking_{false};
    std::atomic<bool> seek_requested_{false};
    std::atomic<bool> time_pitch_change_requested_{false};
    std::atomic<TimePitchRequestState> time_pitch_request_state_{
        TimePitchRequestState::Idle};
    std::atomic<float> requested_time_pitch_ratio_{1.0F};
    std::atomic<bool> requested_keep_pitch_{true};
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
    std::atomic<std::int64_t> consumed_source_frame_{0};
    std::atomic<std::int64_t> pending_boundary_frame_{no_pending_boundary};
    std::atomic<std::int64_t> fade_boundary_frame_{no_pending_boundary};
    std::atomic<ag_result> pending_transition_error_{AG_OK};
    std::atomic<std::size_t> pending_track_index_{0U};
    std::atomic<std::int64_t> pending_duration_ms_{0};
    std::atomic<bool> pending_time_pitch_reset_{false};
    std::atomic<bool> pending_keep_pitch_{true};
    std::atomic<std::uint64_t> pending_mapper_generation_{0U};
    std::atomic<float> volume_{1.0F};
    std::atomic<float> replay_gain_linear_{1.0F};
    std::atomic<float> time_pitch_ratio_{1.0F};
    std::atomic<bool> keep_pitch_{true};
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
    std::atomic<float> output_peak_linear_{0.0F};
    std::atomic<float> output_meter_release_per_frame_{0.0F};
    std::atomic<std::uint64_t> output_meter_generation_{1U};
    std::atomic<std::uint64_t> output_meter_valid_generation_{0U};
    std::atomic<OutputDeviceSwitchTestBarrier*>
        output_device_switch_test_barrier_{nullptr};
    std::atomic<bool> fail_next_equalizer_submit_for_test_{false};
    std::atomic<bool> muted_{false};
    std::atomic<int> transition_fade_ms_{0};
    std::atomic<bool> match_track_sample_rate_{false};
    std::atomic<std::uint64_t> transition_version_{0U};
    std::atomic<TimelineTestHook> timeline_test_hook_{nullptr};
    std::atomic<void*> timeline_test_context_{nullptr};
    std::atomic<TimePitchTestHook> time_pitch_test_hook_{nullptr};
    std::atomic<void*> time_pitch_test_context_{nullptr};
    std::atomic<ScratchCommitTestHook> scratch_commit_test_hook_{nullptr};
    std::atomic<void*> scratch_commit_test_context_{nullptr};
    std::atomic<std::uint64_t> time_pitch_generation_counter_{0U};
    std::atomic<std::uint64_t> requested_time_pitch_generation_{0U};
    std::atomic<std::uint64_t> completed_time_pitch_generation_{0U};
    std::atomic<ag_result> completed_time_pitch_result_{AG_OK};
    std::atomic<std::uint64_t> mapper_generation_counter_{0U};
    std::atomic<std::uint64_t> published_mapper_generation_{0U};
    std::atomic<std::size_t> published_track_index_{0U};
    std::atomic<std::size_t> published_track_count_{0U};
    std::atomic<int> published_mode_{
        static_cast<int>(PlaybackMode::Sequential)};
    std::mutex random_mutex_;
    std::mt19937 random_{std::random_device{}()};
    PlaybackSession session_;
    std::size_t decode_track_index_ = 0U;
    std::uint64_t decode_mapper_generation_ = 0U;
};

AudioEngine::AudioEngine(const AudioBackend backend,
                         const std::size_t buffer_frames,
                         const TimePitchEngineFactory factory)
    : impl_(std::make_unique<Impl>(backend, buffer_frames, factory))
{
}

AudioEngine::~AudioEngine() = default;

void AudioEngine::set_timeline_test_hook(const TimelineTestHook hook,
                                         void* const context) noexcept
{
    impl_->set_timeline_test_hook(hook, context);
}

void AudioEngine::set_time_pitch_test_hook(const TimePitchTestHook hook,
                                           void* const context) noexcept
{
    impl_->set_time_pitch_test_hook(hook, context);
}

void AudioEngine::set_scratch_commit_test_hook(
    const ScratchCommitTestHook hook, void* const context) noexcept
{
    impl_->set_scratch_commit_test_hook(hook, context);
}

std::int64_t AudioEngine::pending_boundary_for_testing() const noexcept
{
    return impl_->pending_boundary_for_testing();
}

std::uint64_t
AudioEngine::published_mapper_generation_for_testing() const noexcept
{
    return impl_->published_mapper_generation_for_testing();
}

std::uint64_t
AudioEngine::pending_mapper_generation_for_testing() const noexcept
{
    return impl_->pending_mapper_generation_for_testing();
}

std::int64_t AudioEngine::consumed_source_frame_for_testing() const noexcept
{
    return impl_->consumed_source_frame_for_testing();
}

std::int64_t AudioEngine::scratch_source_frame_for_testing() const noexcept
{
    return impl_->scratch_source_frame_for_testing();
}

std::uint64_t
AudioEngine::scratch_physical_seek_count_for_testing() const noexcept
{
    return impl_->scratch_physical_seek_count_for_testing();
}

bool AudioEngine::decode_running_for_testing() const noexcept
{
    return impl_->decode_running_for_testing();
}

bool AudioEngine::time_pitch_cancel_requested_for_testing() const noexcept
{
    return impl_->time_pitch_cancel_requested_for_testing();
}

bool AudioEngine::time_pitch_request_idle_for_testing() const noexcept
{
    return impl_->time_pitch_request_idle_for_testing();
}

std::uint64_t
AudioEngine::requested_time_pitch_generation_for_testing() const noexcept
{
    return impl_->requested_time_pitch_generation_for_testing();
}

std::uint64_t
AudioEngine::completed_time_pitch_generation_for_testing() const noexcept
{
    return impl_->completed_time_pitch_generation_for_testing();
}

bool AudioEngine::has_retired_time_pitch_decoder_for_testing() const noexcept
{
    return impl_->has_retired_time_pitch_decoder_for_testing();
}

bool AudioEngine::time_pitch_mailbox_clear_for_testing() const noexcept
{
    return impl_->time_pitch_mailbox_clear_for_testing();
}

void AudioEngine::stop_decode_thread_for_testing() noexcept
{
    impl_->stop_decode_thread_for_testing();
}

void AudioEngine::request_decode_exit_for_testing() noexcept
{
    impl_->request_decode_exit_for_testing();
}

void AudioEngine::mark_device_lost_for_testing() noexcept
{
    impl_->mark_device_lost_for_testing();
}

void AudioEngine::notify_device_lost_from_backend_for_testing() noexcept
{
    impl_->notify_device_lost_from_backend_for_testing();
}

ag_result AudioEngine::enter_error_for_testing(
    const ag_result result) noexcept
{
    return impl_->enter_error_for_testing(result);
}

ag_result AudioEngine::load(const std::string& utf8_path) noexcept
{
    return impl_->load(utf8_path);
}

ag_result AudioEngine::load_stream(
    std::shared_ptr<IAudioStreamSource> stream,
    const bool bypass_time_pitch) noexcept
{
    return impl_->load_stream(std::move(stream), bypass_time_pitch);
}

ag_result AudioEngine::replace_stream(
    std::shared_ptr<IAudioStreamSource> stream) noexcept
{
    return impl_->replace_stream(std::move(stream));
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

ag_result AudioEngine::set_replay_gain(const float gain_db, const float peak,
                                       const bool clip_protection) noexcept
{
    return impl_->set_replay_gain(gain_db, peak, clip_protection);
}

ag_result AudioEngine::set_time_pitch(
    const PlaybackTimePitchConfig& config) noexcept
{
    return impl_->set_time_pitch(config);
}

PlaybackTimePitchConfig AudioEngine::time_pitch_config() const noexcept
{
    return impl_->time_pitch_config();
}

ag_result AudioEngine::begin_scratch() noexcept
{
    return impl_->begin_scratch();
}

ag_result AudioEngine::update_scratch(const float signed_rate) noexcept
{
    return impl_->update_scratch(signed_rate);
}

ag_result AudioEngine::end_scratch() noexcept
{
    return impl_->end_scratch();
}

ag_result AudioEngine::cancel_scratch() noexcept
{
    return impl_->cancel_scratch();
}

ScratchStatus AudioEngine::scratch_status() const noexcept
{
    return impl_->scratch_status();
}

OutputLevels AudioEngine::output_levels() const noexcept
{
    return impl_->output_levels();
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

void AudioEngine::publish_output_levels_for_testing(
    const float* output, const std::size_t frames,
    const std::size_t channels) noexcept
{
    impl_->publish_output_levels_for_testing(output, frames, channels);
}

void AudioEngine::set_visual_pcm_enabled(bool enabled) noexcept
{
    impl_->set_visual_pcm_enabled(enabled);
}

void AudioEngine::read_visual_pcm(ag_visual_pcm_snapshot& snapshot) noexcept
{
    impl_->read_visual_pcm(snapshot);
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

bool AudioEngine::end_of_stream() const noexcept
{
    return impl_->end_of_stream();
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

void AudioEngine::set_output_device_switch_test_barrier(
    OutputDeviceSwitchTestBarrier* const barrier) noexcept
{
    impl_->set_output_device_switch_test_barrier(barrier);
}

void AudioEngine::fail_next_equalizer_submit_for_test() noexcept
{
    impl_->fail_next_equalizer_submit_for_test();
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
