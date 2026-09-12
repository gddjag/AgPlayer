#include "core_context.hpp"
#include "playback_time_pitch_stage.hpp"

#include <utility>

namespace agplayer {

CoreContext::CoreContext(const AudioBackend backend,
                         const std::size_t buffer_frames)
    : audio_engine_(backend, buffer_frames)
{
}

const std::string& CoreContext::last_error() const noexcept
{
    return last_error_;
}

void CoreContext::set_error(std::string value) noexcept
{
    try {
        last_error_ = std::move(value);
    } catch (...) {
        last_error_.clear();
    }
}

ag_result CoreContext::load(const std::string& utf8_path) noexcept
{
    return record(audio_engine_.load(utf8_path), "load failed");
}

ag_result CoreContext::load_stream(
    std::shared_ptr<IAudioStreamSource> stream) noexcept
{
    return record(audio_engine_.load_stream(std::move(stream), true),
                  "editor stream load failed");
}

ag_result CoreContext::replace_stream(
    std::shared_ptr<IAudioStreamSource> stream) noexcept
{
    return record(audio_engine_.replace_stream(std::move(stream)),
                  "editor stream replacement failed");
}

ag_result CoreContext::set_queue(std::vector<std::string> utf8_paths,
                                 const std::size_t start_index) noexcept
{
    return record(audio_engine_.set_queue(std::move(utf8_paths), start_index),
                  "queue change failed");
}

ag_result CoreContext::set_scoped_queue(std::vector<std::string> utf8_paths,
                                        const std::size_t start_index,
                                        const std::size_t scope_size,
                                        const bool allow_fallback) noexcept
{
    return record(audio_engine_.set_scoped_queue(
                      std::move(utf8_paths), start_index, scope_size,
                      allow_fallback),
                  "scoped queue change failed");
}

ag_result CoreContext::queue_next(std::string utf8_path) noexcept
{
    return record(audio_engine_.queue_next(std::move(utf8_path)),
                  "queue next failed");
}

ag_result CoreContext::play() noexcept
{
    return record(audio_engine_.play(), "play failed");
}

ag_result CoreContext::pause() noexcept
{
    return record(audio_engine_.pause(), "pause failed");
}

ag_result CoreContext::stop() noexcept
{
    return record(audio_engine_.stop(), "stop failed");
}

ag_result CoreContext::seek(const std::int64_t position_ms) noexcept
{
    return record(audio_engine_.seek(position_ms), "seek failed");
}

ag_result CoreContext::next() noexcept
{
    return record(audio_engine_.next(), "next track failed");
}

ag_result CoreContext::previous() noexcept
{
    return record(audio_engine_.previous(), "previous track failed");
}

ag_result CoreContext::set_mode(const PlaybackMode mode) noexcept
{
    return record(audio_engine_.set_mode(mode), "playback mode change failed");
}

ag_result CoreContext::set_volume(const float volume) noexcept
{
    return record(audio_engine_.set_volume(volume), "volume change failed");
}

ag_result CoreContext::set_replay_gain(const float gain_db,
                                       const float peak,
                                       const bool clip_protection) noexcept
{
    return record(
        audio_engine_.set_replay_gain(gain_db, peak, clip_protection),
        "replay gain change failed");
}

ag_result CoreContext::set_time_pitch(
    const PlaybackTimePitchConfig& config) noexcept
{
    return record(audio_engine_.set_time_pitch(config),
                  "time/pitch change failed");
}

PlaybackTimePitchConfig CoreContext::time_pitch_config() const noexcept
{
    return audio_engine_.time_pitch_config();
}

ag_result CoreContext::begin_scratch() noexcept
{
    return record(audio_engine_.begin_scratch(), "scratch begin failed");
}

ag_result CoreContext::update_scratch(const float signed_rate) noexcept
{
    return record(audio_engine_.update_scratch(signed_rate),
                  "scratch update failed");
}

ag_result CoreContext::end_scratch() noexcept
{
    return record(audio_engine_.end_scratch(), "scratch end failed");
}

ag_result CoreContext::cancel_scratch() noexcept
{
    return record(audio_engine_.cancel_scratch(), "scratch cancel failed");
}

ScratchStatus CoreContext::scratch_status() const noexcept
{
    return audio_engine_.scratch_status();
}

OutputLevels CoreContext::output_levels() const noexcept
{
    return audio_engine_.output_levels();
}

ag_result CoreContext::set_equalizer(const GraphicEqSettings& settings,
                                     const std::uint64_t revision) noexcept
{
    return record(audio_engine_.set_equalizer(settings, revision),
                  "equalizer change failed");
}

EqualizerStatus CoreContext::equalizer_status() const noexcept
{
    return audio_engine_.equalizer_status();
}

void CoreContext::set_muted(const bool muted) noexcept
{
    audio_engine_.set_muted(muted);
    last_error_.clear();
}

EngineSnapshot CoreContext::snapshot() const noexcept
{
    return audio_engine_.snapshot();
}

void CoreContext::set_visual_pcm_enabled(bool enabled) noexcept
{
    audio_engine_.set_visual_pcm_enabled(enabled);
}

void CoreContext::read_visual_pcm(ag_visual_pcm_snapshot& snapshot) noexcept
{
    audio_engine_.read_visual_pcm(snapshot);
}

ag_result CoreContext::spectrum(float* bins,
                                const std::size_t bin_count) noexcept
{
    return record(audio_engine_.spectrum(bins, bin_count),
                  "spectrum snapshot failed");
}

bool CoreContext::device_lost() const noexcept
{
    return audio_engine_.device_lost();
}

ag_result CoreContext::retry_device() noexcept
{
    return record(audio_engine_.retry_device(), "device retry failed");
}

void CoreContext::simulate_device_loss() noexcept
{
    audio_engine_.simulate_device_loss();
    last_error_ = "device lost";
}

std::vector<OutputDevice> CoreContext::output_devices() noexcept
{
    return audio_engine_.output_devices();
}

ag_result CoreContext::set_output_device(std::string utf8_id,
                                         const bool exclusive) noexcept
{
    return record(
        audio_engine_.set_output_device(std::move(utf8_id), exclusive),
        "output device change failed");
}

bool CoreContext::exclusive_mode_active() const noexcept
{
    return audio_engine_.exclusive_mode_active();
}

ag_result CoreContext::set_transition_fade_ms(
    const int milliseconds) noexcept
{
    return record(audio_engine_.set_transition_fade_ms(milliseconds),
                  "transition fade change failed");
}

ag_result CoreContext::set_duration_ms(const std::int64_t duration_ms) noexcept
{
    return record(audio_engine_.set_duration_ms(duration_ms),
                  "duration synchronization failed");
}

ag_result CoreContext::set_match_track_sample_rate(
    const bool enabled) noexcept
{
    return record(audio_engine_.set_match_track_sample_rate(enabled),
                  "sample rate policy change failed");
}

ag_result CoreContext::record(const ag_result result,
                              const char* operation) noexcept
{
    try {
        last_error_ = result == AG_OK ? "" : operation;
    } catch (...) {
        last_error_.clear();
    }
    return result;
}

} // namespace agplayer
