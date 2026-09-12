#include "stem_preview_mixer.hpp"

#include "decoder.hpp"

#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#include <miniaudio.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <limits>
#include <mutex>
#include <thread>
#include <utility>

namespace agplayer {
namespace {

constexpr std::size_t preview_channels = 2U;
constexpr std::int64_t preview_sample_rate = 44'100;
constexpr std::size_t warmup_frames = 4'096U;
constexpr auto warmup_timeout = std::chrono::seconds(2);

[[nodiscard]] std::int64_t milliseconds_to_frames(
    const std::int64_t milliseconds) noexcept
{
    if (milliseconds <= 0) {
        return 0;
    }
    constexpr std::int64_t milliseconds_per_second = 1'000;
    const std::int64_t seconds = milliseconds / milliseconds_per_second;
    const std::int64_t remainder = milliseconds % milliseconds_per_second;
    if (seconds
        > (std::numeric_limits<std::int64_t>::max)()
              / preview_sample_rate) {
        return (std::numeric_limits<std::int64_t>::max)();
    }
    return seconds * preview_sample_rate
           + remainder * preview_sample_rate / milliseconds_per_second;
}

[[nodiscard]] std::int64_t frames_to_milliseconds(
    const std::int64_t frames) noexcept
{
    if (frames <= 0) {
        return 0;
    }
    return frames / preview_sample_rate * 1'000
           + (frames % preview_sample_rate) * 1'000 / preview_sample_rate;
}

[[nodiscard]] bool valid_gain(const float gain) noexcept
{
    return std::isfinite(gain) && gain >= 0.0F && gain <= 1.0F;
}

} // namespace

class StemPreviewMixer::Impl final {
public:
    Impl(const AudioBackend backend, const std::size_t buffer_frames)
        : backend_(backend)
        , buffer_frames_(buffer_frames == 0U ? 32'768U : buffer_frames)
        , scratch_frames_((std::max)(std::size_t{1U}, buffer_frames_))
        , scratch_(scratch_frames_ * preview_channels, 0.0F)
    {
    }

    ~Impl()
    {
        const std::lock_guard<std::recursive_mutex> lock(control_mutex_);
        (void)stop_output();
        uninitialize_device();
        sources_.clear();
    }

    ag_result load(std::vector<StemPreviewSource> sources,
                   const std::int64_t position_ms) noexcept
    {
        if (sources.empty() || position_ms < 0) {
            return AG_INVALID_ARGUMENT;
        }
        for (std::size_t index = 0U; index < sources.size(); ++index) {
            if (sources[index].id.empty() || sources[index].path.empty()
                || !valid_gain(sources[index].gain)) {
                return AG_INVALID_ARGUMENT;
            }
            for (std::size_t other = index + 1U; other < sources.size();
                 ++other) {
                if (sources[index].id == sources[other].id) {
                    return AG_INVALID_ARGUMENT;
                }
            }
        }

        const std::lock_guard<std::recursive_mutex> lock(control_mutex_);
        try {
            (void)stop_output();
            sources_.clear();
            state_.store(EngineState::Loading, std::memory_order_release);
            position_frames_.store(0, std::memory_order_release);
            duration_ms_.store(0, std::memory_order_release);

            std::vector<SourceState> loaded;
            loaded.reserve(sources.size());
            std::int64_t duration_ms = 0;
            for (StemPreviewSource& source : sources) {
                MediaMetadata metadata;
                const ag_result probe_result =
                    probe_media_metadata(source.path, metadata);
                if (probe_result != AG_OK) {
                    reset_after_load_failure();
                    return probe_result;
                }
                if (metadata.sample_rate
                        != static_cast<int>(preview_sample_rate)
                    || metadata.channels
                           != static_cast<int>(preview_channels)
                    || metadata.duration_ms <= 0) {
                    reset_after_load_failure();
                    return AG_UNSUPPORTED_FORMAT;
                }

                SourceState state;
                state.id = std::move(source.id);
                state.engine = std::make_unique<AudioEngine>(
                    AudioBackend::Manual, buffer_frames_);
                ag_result result = state.engine->load(source.path);
                if (result != AG_OK) {
                    reset_after_load_failure();
                    return result;
                }
                const EngineSnapshot engine_snapshot =
                    state.engine->snapshot();
                if (engine_snapshot.sample_rate
                        != static_cast<int>(preview_sample_rate)
                    || engine_snapshot.channels
                           != static_cast<int>(preview_channels)
                    || engine_snapshot.duration_ms <= 0) {
                    reset_after_load_failure();
                    return AG_UNSUPPORTED_FORMAT;
                }
                state.duration_ms = engine_snapshot.duration_ms;
                result = state.engine->set_volume(source.gain);
                if (result == AG_OK && position_ms > 0
                    && position_ms < state.duration_ms) {
                    result = state.engine->seek(position_ms);
                }
                if (result != AG_OK) {
                    reset_after_load_failure();
                    return result;
                }
                duration_ms = (std::max)(duration_ms, state.duration_ms);
                loaded.push_back(std::move(state));
            }

            if (position_ms > duration_ms) {
                reset_after_load_failure();
                return AG_INVALID_ARGUMENT;
            }

            sources_ = std::move(loaded);
            duration_ms_.store(duration_ms, std::memory_order_release);
            position_frames_.store(milliseconds_to_frames(position_ms),
                                   std::memory_order_release);
            const ag_result device_result = initialize_device();
            if (device_result != AG_OK) {
                reset_after_load_failure();
                return device_result;
            }
            state_.store(EngineState::Stopped, std::memory_order_release);
            return AG_OK;
        } catch (...) {
            reset_after_load_failure();
            return AG_INTERNAL_ERROR;
        }
    }

    ag_result play() noexcept
    {
        const std::lock_guard<std::recursive_mutex> lock(control_mutex_);
        if (sources_.empty()) {
            return AG_INVALID_ARGUMENT;
        }
        const EngineState current = state_.load(std::memory_order_acquire);
        if (current == EngineState::Playing) {
            return AG_OK;
        }
        if (current == EngineState::Error
            || current == EngineState::Loading) {
            return AG_INVALID_ARGUMENT;
        }
        if (position_frames_.load(std::memory_order_acquire)
            >= milliseconds_to_frames(
                duration_ms_.load(std::memory_order_acquire))) {
            const ag_result seek_result = seek(0);
            if (seek_result != AG_OK) {
                return seek_result;
            }
        }

        const std::int64_t position_ms = frames_to_milliseconds(
            position_frames_.load(std::memory_order_acquire));
        for (SourceState& source : sources_) {
            if (position_ms >= source.duration_ms) {
                continue;
            }
            const ag_result result = source.engine->play();
            if (result != AG_OK) {
                pause_sources();
                state_.store(EngineState::Error, std::memory_order_release);
                return result;
            }
        }

        if (backend_ == AudioBackend::Manual && !wait_until_ready()) {
            pause_sources();
            state_.store(EngineState::Error, std::memory_order_release);
            return AG_DECODE_ERROR;
        }

        state_.store(EngineState::Playing, std::memory_order_release);
        const ag_result result = start_output();
        if (result != AG_OK) {
            state_.store(EngineState::Error, std::memory_order_release);
            pause_sources();
            return result;
        }
        return AG_OK;
    }

    ag_result pause() noexcept
    {
        const std::lock_guard<std::recursive_mutex> lock(control_mutex_);
        if (sources_.empty()) {
            return AG_INVALID_ARGUMENT;
        }
        const EngineState current = state_.load(std::memory_order_acquire);
        if (current == EngineState::Paused) {
            return AG_OK;
        }
        if (current != EngineState::Playing) {
            return AG_INVALID_ARGUMENT;
        }
        const ag_result output_result = stop_output();
        const ag_result source_result = pause_sources();
        if (output_result != AG_OK || source_result != AG_OK) {
            state_.store(EngineState::Error, std::memory_order_release);
            return output_result != AG_OK ? output_result : source_result;
        }
        state_.store(EngineState::Paused, std::memory_order_release);
        return AG_OK;
    }

    ag_result seek(const std::int64_t position_ms) noexcept
    {
        if (position_ms < 0) {
            return AG_INVALID_ARGUMENT;
        }
        const std::lock_guard<std::recursive_mutex> lock(control_mutex_);
        if (sources_.empty()
            || position_ms > duration_ms_.load(std::memory_order_acquire)) {
            return AG_INVALID_ARGUMENT;
        }
        const EngineState previous = state_.load(std::memory_order_acquire);
        const bool resume = previous == EngineState::Playing;
        if (resume) {
            const ag_result output_result = stop_output();
            if (output_result != AG_OK) {
                state_.store(EngineState::Error, std::memory_order_release);
                return output_result;
            }
        }
        for (SourceState& source : sources_) {
            ag_result result = AG_OK;
            if (position_ms < source.duration_ms) {
                result = source.engine->seek(position_ms);
                if (result == AG_OK && resume
                    && source.engine->snapshot().state
                           != EngineState::Playing) {
                    result = source.engine->play();
                }
            } else {
                result = source.engine->stop();
            }
            if (result != AG_OK) {
                state_.store(EngineState::Error, std::memory_order_release);
                return result;
            }
        }
        position_frames_.store(milliseconds_to_frames(position_ms),
                               std::memory_order_release);
        if (!resume) {
            return AG_OK;
        }
        if (backend_ == AudioBackend::Manual && !wait_until_ready()) {
            pause_sources();
            state_.store(EngineState::Error, std::memory_order_release);
            return AG_DECODE_ERROR;
        }
        state_.store(EngineState::Playing, std::memory_order_release);
        const ag_result output_result = start_output();
        if (output_result != AG_OK) {
            pause_sources();
            state_.store(EngineState::Error, std::memory_order_release);
        }
        return output_result;
    }

    ag_result stop() noexcept
    {
        const std::lock_guard<std::recursive_mutex> lock(control_mutex_);
        if (sources_.empty()) {
            state_.store(EngineState::Stopped, std::memory_order_release);
            position_frames_.store(0, std::memory_order_release);
            return AG_OK;
        }
        const ag_result output_result = stop_output();
        ag_result source_result = AG_OK;
        for (SourceState& source : sources_) {
            const ag_result result = source.engine->stop();
            if (source_result == AG_OK && result != AG_OK) {
                source_result = result;
            }
        }
        position_frames_.store(0, std::memory_order_release);
        if (output_result != AG_OK || source_result != AG_OK) {
            state_.store(EngineState::Error, std::memory_order_release);
            return output_result != AG_OK ? output_result : source_result;
        }
        state_.store(EngineState::Stopped, std::memory_order_release);
        return AG_OK;
    }

    ag_result set_gain(const std::string_view id, const float gain) noexcept
    {
        if (id.empty() || !valid_gain(gain)) {
            return AG_INVALID_ARGUMENT;
        }
        const std::lock_guard<std::recursive_mutex> lock(control_mutex_);
        const auto found = std::find_if(
            sources_.begin(), sources_.end(),
            [id](const SourceState& source) { return source.id == id; });
        return found == sources_.end() ? AG_INVALID_ARGUMENT
                                       : found->engine->set_volume(gain);
    }

    [[nodiscard]] StemPreviewSnapshot snapshot() const noexcept
    {
        return {
            state_.load(std::memory_order_acquire),
            frames_to_milliseconds(
                position_frames_.load(std::memory_order_acquire)),
            duration_ms_.load(std::memory_order_acquire),
        };
    }

    void render(float* output, const std::size_t requested_frames) noexcept
    {
        if (output == nullptr || requested_frames == 0U) {
            return;
        }
        std::fill(output,
                  output + requested_frames * preview_channels, 0.0F);
        if (state_.load(std::memory_order_acquire)
            != EngineState::Playing) {
            return;
        }

        std::size_t rendered_frames = 0U;
        while (rendered_frames < requested_frames) {
            const std::int64_t position =
                position_frames_.load(std::memory_order_acquire);
            const std::int64_t duration = milliseconds_to_frames(
                duration_ms_.load(std::memory_order_acquire));
            if (position >= duration) {
                state_.store(EngineState::Stopped, std::memory_order_release);
                return;
            }
            const std::size_t remaining_request =
                requested_frames - rendered_frames;
            const std::size_t remaining_timeline =
                static_cast<std::size_t>(duration - position);
            const std::size_t chunk = (std::min)(
                scratch_frames_,
                (std::min)(remaining_request, remaining_timeline));
            if (!sources_ready(position, chunk)) {
                return;
            }

            float* const mixed =
                output + rendered_frames * preview_channels;
            for (SourceState& source : sources_) {
                const std::int64_t source_duration =
                    milliseconds_to_frames(source.duration_ms);
                if (position >= source_duration) {
                    continue;
                }
                const std::size_t samples = chunk * preview_channels;
                std::fill(scratch_.begin(), scratch_.begin() + samples,
                          0.0F);
                source.engine->render(scratch_.data(), chunk);
                for (std::size_t sample = 0U; sample < samples; ++sample) {
                    mixed[sample] += scratch_[sample];
                }
            }
            const std::size_t samples = chunk * preview_channels;
            for (std::size_t sample = 0U; sample < samples; ++sample) {
                mixed[sample] = std::clamp(mixed[sample], -1.0F, 1.0F);
            }
            position_frames_.fetch_add(static_cast<std::int64_t>(chunk),
                                       std::memory_order_acq_rel);
            rendered_frames += chunk;
        }

        if (position_frames_.load(std::memory_order_acquire)
            >= milliseconds_to_frames(
                duration_ms_.load(std::memory_order_acquire))) {
            state_.store(EngineState::Stopped, std::memory_order_release);
        }
    }

private:
    struct SourceState {
        std::string id;
        std::unique_ptr<AudioEngine> engine;
        std::int64_t duration_ms = 0;
    };

    static void data_callback(ma_device* device,
                              void* output,
                              const void*,
                              const ma_uint32 frame_count) noexcept
    {
        auto* const self = static_cast<Impl*>(device->pUserData);
        self->render(static_cast<float*>(output),
                     static_cast<std::size_t>(frame_count));
    }

    [[nodiscard]] bool sources_ready(const std::int64_t position,
                                     const std::size_t frames) noexcept
    {
        for (const SourceState& source : sources_) {
            const std::int64_t source_duration =
                milliseconds_to_frames(source.duration_ms);
            if (position >= source_duration) {
                continue;
            }
            const EngineSnapshot source_snapshot = source.engine->snapshot();
            if (source_snapshot.state == EngineState::Error) {
                state_.store(EngineState::Error, std::memory_order_release);
                return false;
            }
            const bool end_of_stream = source.engine->end_of_stream();
            const std::size_t buffered = source.engine->buffered_frames();
            if (source_snapshot.state == EngineState::Stopped
                && end_of_stream && buffered == 0U) {
                continue;
            }
            const std::size_t needed = (std::min)(
                frames,
                static_cast<std::size_t>(source_duration - position));
            if (source_snapshot.state != EngineState::Playing
                || (buffered < needed && !end_of_stream)) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool wait_until_ready() noexcept
    {
        const auto deadline = std::chrono::steady_clock::now()
                              + warmup_timeout;
        const std::size_t requested =
            (std::min)(scratch_frames_, warmup_frames);
        while (!sources_ready(
            position_frames_.load(std::memory_order_acquire), requested)) {
            if (state_.load(std::memory_order_acquire)
                    == EngineState::Error
                || std::chrono::steady_clock::now() >= deadline) {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return true;
    }

    ag_result pause_sources() noexcept
    {
        ag_result first_error = AG_OK;
        for (SourceState& source : sources_) {
            const EngineState state = source.engine->snapshot().state;
            if (state != EngineState::Playing) {
                continue;
            }
            const ag_result result = source.engine->pause();
            if (first_error == AG_OK && result != AG_OK) {
                first_error = result;
            }
        }
        return first_error;
    }

    ag_result initialize_device() noexcept
    {
        if (backend_ == AudioBackend::Manual || device_initialized_) {
            return AG_OK;
        }
        ma_result result = MA_SUCCESS;
        if (!context_initialized_) {
            if (backend_ == AudioBackend::Null) {
                const ma_backend null_backend = ma_backend_null;
                result = ma_context_init(&null_backend, 1U, nullptr,
                                         &context_);
            } else {
                result = ma_context_init(nullptr, 0U, nullptr, &context_);
            }
            if (result != MA_SUCCESS) {
                return AG_DEVICE_ERROR;
            }
            context_initialized_ = true;
        }

        ma_device_config config =
            ma_device_config_init(ma_device_type_playback);
        config.playback.format = ma_format_f32;
        config.playback.channels =
            static_cast<ma_uint32>(preview_channels);
        config.sampleRate = static_cast<ma_uint32>(preview_sample_rate);
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

    ag_result start_output() noexcept
    {
        if (backend_ == AudioBackend::Manual || device_started_) {
            return AG_OK;
        }
        if (!device_initialized_
            || ma_device_start(&device_) != MA_SUCCESS) {
            return AG_DEVICE_ERROR;
        }
        device_started_ = true;
        return AG_OK;
    }

    ag_result stop_output() noexcept
    {
        if (backend_ == AudioBackend::Manual || !device_started_) {
            return AG_OK;
        }
        if (ma_device_stop(&device_) != MA_SUCCESS) {
            return AG_DEVICE_ERROR;
        }
        device_started_ = false;
        return AG_OK;
    }

    void uninitialize_device() noexcept
    {
        if (device_initialized_) {
            ma_device_uninit(&device_);
            device_initialized_ = false;
            device_started_ = false;
        }
        if (context_initialized_) {
            ma_context_uninit(&context_);
            context_initialized_ = false;
        }
    }

    void reset_after_load_failure() noexcept
    {
        (void)stop_output();
        sources_.clear();
        position_frames_.store(0, std::memory_order_release);
        duration_ms_.store(0, std::memory_order_release);
        state_.store(EngineState::Error, std::memory_order_release);
    }

    AudioBackend backend_;
    std::size_t buffer_frames_;
    std::size_t scratch_frames_;
    std::vector<float> scratch_;
    std::vector<SourceState> sources_;
    mutable std::recursive_mutex control_mutex_;
    std::atomic<EngineState> state_{EngineState::Stopped};
    std::atomic<std::int64_t> position_frames_{0};
    std::atomic<std::int64_t> duration_ms_{0};
    ma_context context_{};
    ma_device device_{};
    bool context_initialized_ = false;
    bool device_initialized_ = false;
    bool device_started_ = false;
};

StemPreviewMixer::StemPreviewMixer(const AudioBackend backend,
                                   const std::size_t buffer_frames)
    : impl_(std::make_unique<Impl>(backend, buffer_frames))
{
}

StemPreviewMixer::~StemPreviewMixer() = default;

ag_result StemPreviewMixer::load(std::vector<StemPreviewSource> sources,
                                 const std::int64_t position_ms) noexcept
{
    return impl_->load(std::move(sources), position_ms);
}

ag_result StemPreviewMixer::play() noexcept
{
    return impl_->play();
}

ag_result StemPreviewMixer::pause() noexcept
{
    return impl_->pause();
}

ag_result StemPreviewMixer::seek(const std::int64_t position_ms) noexcept
{
    return impl_->seek(position_ms);
}

ag_result StemPreviewMixer::stop() noexcept
{
    return impl_->stop();
}

ag_result StemPreviewMixer::set_gain(const std::string_view id,
                                     const float gain) noexcept
{
    return impl_->set_gain(id, gain);
}

StemPreviewSnapshot StemPreviewMixer::snapshot() const noexcept
{
    return impl_->snapshot();
}

void StemPreviewMixer::render(float* output,
                              const std::size_t requested_frames) noexcept
{
    impl_->render(output, requested_frames);
}

} // namespace agplayer
