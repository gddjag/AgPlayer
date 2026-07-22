#include <agplayer/c_api.h>

#include "core_context.hpp"
#include "decoder.hpp"
#include "waveform_analyzer.hpp"

#include <atomic>
#include <cstring>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace {

template <typename Operation>
ag_result guard_result(Operation&& operation) noexcept
{
    try {
        return operation();
    } catch (...) {
        return AG_INTERNAL_ERROR;
    }
}

ag_playback_state to_c_state(const agplayer::EngineState state) noexcept
{
    switch (state) {
    case agplayer::EngineState::Stopped:
        return AG_STOPPED;
    case agplayer::EngineState::Loading:
        return AG_LOADING;
    case agplayer::EngineState::Playing:
        return AG_PLAYING;
    case agplayer::EngineState::Paused:
        return AG_PAUSED;
    case agplayer::EngineState::Error:
        return AG_ERROR;
    }
    return AG_ERROR;
}

ag_playback_mode to_c_mode(const agplayer::PlaybackMode mode) noexcept
{
    switch (mode) {
    case agplayer::PlaybackMode::Sequential:
        return AG_MODE_SEQUENTIAL;
    case agplayer::PlaybackMode::RepeatOne:
        return AG_MODE_REPEAT_ONE;
    case agplayer::PlaybackMode::Shuffle:
        return AG_MODE_SHUFFLE;
    }
    return AG_MODE_SEQUENTIAL;
}

} // namespace

struct ag_player {
    ag_player(const agplayer::AudioBackend backend,
              const std::size_t buffer_frames)
        : context(backend, buffer_frames)
    {
    }

    agplayer::CoreContext context;
};

struct ag_metadata {
    agplayer::MediaMetadata value;
};

struct ag_waveform {
    std::vector<float> peaks;
};

struct ag_cancel_token {
    std::atomic_bool cancelled{false};
};

ag_result ag_player_create(ag_player** out_player)
{
    const ag_player_config config{AG_AUDIO_BACKEND_DEFAULT, 0U};
    return ag_player_create_with_config(&config, out_player);
}

ag_result ag_player_create_with_config(const ag_player_config* config,
                                       ag_player** out_player)
{
    if (out_player == nullptr) {
        return AG_INVALID_ARGUMENT;
    }

    *out_player = nullptr;
    if (config == nullptr
        || (config->backend != AG_AUDIO_BACKEND_DEFAULT
            && config->backend != AG_AUDIO_BACKEND_NULL)) {
        return AG_INVALID_ARGUMENT;
    }

    try {
        const agplayer::AudioBackend backend =
            config->backend == AG_AUDIO_BACKEND_NULL
                ? agplayer::AudioBackend::Null
                : agplayer::AudioBackend::Default;
        *out_player = new (std::nothrow) ag_player(backend,
                                                  config->buffer_frames);
        return *out_player == nullptr ? AG_INTERNAL_ERROR : AG_OK;
    } catch (...) {
        return AG_INTERNAL_ERROR;
    }
}

void ag_player_destroy(ag_player* player)
{
    delete player;
}

ag_result ag_player_last_error(const ag_player* player,
                               char* buffer,
                               const size_t capacity,
                               size_t* required)
{
    if (player == nullptr || required == nullptr) {
        return AG_INVALID_ARGUMENT;
    }

    const std::string& message = player->context.last_error();
    *required = message.size() + 1U;

    if (capacity == 0U) {
        return AG_OK;
    }
    if (buffer == nullptr || capacity < *required) {
        return AG_INVALID_ARGUMENT;
    }

    std::memcpy(buffer, message.c_str(), *required);
    return AG_OK;
}

ag_result ag_player_load(ag_player* player, const char* utf8_path)
{
    if (player == nullptr || utf8_path == nullptr || utf8_path[0] == '\0') {
        return AG_INVALID_ARGUMENT;
    }
    return guard_result([&] { return player->context.load(utf8_path); });
}

ag_result ag_player_set_queue(ag_player* player,
                              const char* const* utf8_paths,
                              const size_t count,
                              const size_t start_index)
{
    if (player == nullptr || utf8_paths == nullptr || count == 0U
        || start_index >= count) {
        return AG_INVALID_ARGUMENT;
    }

    return guard_result([&] {
        std::vector<std::string> paths;
        paths.reserve(count);
        for (size_t index = 0U; index < count; ++index) {
            if (utf8_paths[index] == nullptr || utf8_paths[index][0] == '\0') {
                return AG_INVALID_ARGUMENT;
            }
            paths.emplace_back(utf8_paths[index]);
        }
        return player->context.set_queue(std::move(paths), start_index);
    });
}

ag_result ag_player_play(ag_player* player)
{
    return player == nullptr
               ? AG_INVALID_ARGUMENT
               : guard_result([&] { return player->context.play(); });
}

ag_result ag_player_pause(ag_player* player)
{
    return player == nullptr
               ? AG_INVALID_ARGUMENT
               : guard_result([&] { return player->context.pause(); });
}

ag_result ag_player_stop(ag_player* player)
{
    return player == nullptr
               ? AG_INVALID_ARGUMENT
               : guard_result([&] { return player->context.stop(); });
}

ag_result ag_player_seek(ag_player* player, const long long position_ms)
{
    return player == nullptr
               ? AG_INVALID_ARGUMENT
               : guard_result([&] { return player->context.seek(position_ms); });
}

ag_result ag_player_next(ag_player* player)
{
    return player == nullptr
               ? AG_INVALID_ARGUMENT
               : guard_result([&] { return player->context.next(); });
}

ag_result ag_player_previous(ag_player* player)
{
    return player == nullptr
               ? AG_INVALID_ARGUMENT
               : guard_result([&] { return player->context.previous(); });
}

ag_result ag_player_set_mode(ag_player* player, const ag_playback_mode mode)
{
    if (player == nullptr
        || (mode != AG_MODE_SEQUENTIAL && mode != AG_MODE_REPEAT_ONE
            && mode != AG_MODE_SHUFFLE)) {
        return AG_INVALID_ARGUMENT;
    }
    const agplayer::PlaybackMode value = mode == AG_MODE_REPEAT_ONE
                                             ? agplayer::PlaybackMode::RepeatOne
                                         : mode == AG_MODE_SHUFFLE
                                             ? agplayer::PlaybackMode::Shuffle
                                             : agplayer::PlaybackMode::Sequential;
    return guard_result([&] { return player->context.set_mode(value); });
}

ag_result ag_player_set_volume(ag_player* player, const float volume)
{
    return player == nullptr
               ? AG_INVALID_ARGUMENT
               : guard_result([&] { return player->context.set_volume(volume); });
}

ag_result ag_player_set_muted(ag_player* player, const int muted)
{
    if (player == nullptr || (muted != 0 && muted != 1)) {
        return AG_INVALID_ARGUMENT;
    }
    return guard_result([&] {
        player->context.set_muted(muted != 0);
        return AG_OK;
    });
}

ag_result ag_player_snapshot(const ag_player* player,
                             ag_playback_snapshot* snapshot)
{
    if (player == nullptr || snapshot == nullptr) {
        return AG_INVALID_ARGUMENT;
    }

    return guard_result([&] {
        const agplayer::EngineSnapshot value = player->context.snapshot();
        snapshot->state = to_c_state(value.state);
        snapshot->position_ms = value.position_ms;
        snapshot->duration_ms = value.duration_ms;
        snapshot->volume = value.volume;
        snapshot->muted = value.muted ? 1 : 0;
        snapshot->track_index = value.track_index;
        snapshot->track_count = value.track_count;
        snapshot->mode = to_c_mode(value.mode);
        return AG_OK;
    });
}

ag_result ag_player_retry_device(ag_player* player)
{
    return player == nullptr
               ? AG_INVALID_ARGUMENT
               : guard_result([&] { return player->context.retry_device(); });
}

int ag_player_device_lost(const ag_player* player)
{
    return player != nullptr && player->context.device_lost() ? 1 : 0;
}

ag_result ag_player_simulate_device_loss(ag_player* player)
{
    if (player == nullptr) {
        return AG_INVALID_ARGUMENT;
    }
    guard_result([&] { player->context.simulate_device_loss(); return AG_OK; });
    return AG_OK;
}

ag_result ag_metadata_open(const char* utf8_path, ag_metadata** out_metadata)
{
    if (out_metadata == nullptr) {
        return AG_INVALID_ARGUMENT;
    }

    *out_metadata = nullptr;
    if (utf8_path == nullptr || utf8_path[0] == '\0') {
        return AG_INVALID_ARGUMENT;
    }

    try {
        agplayer::Decoder decoder;
        const ag_result result = decoder.open(utf8_path);
        if (result != AG_OK) {
            return result;
        }
        *out_metadata = new (std::nothrow) ag_metadata{decoder.metadata()};
        return *out_metadata == nullptr ? AG_INTERNAL_ERROR : AG_OK;
    } catch (...) {
        return AG_INTERNAL_ERROR;
    }
}

void ag_metadata_destroy(ag_metadata* metadata)
{
    delete metadata;
}

const char* ag_metadata_title(const ag_metadata* metadata)
{
    return metadata == nullptr ? "" : metadata->value.title.c_str();
}

const char* ag_metadata_artist(const ag_metadata* metadata)
{
    return metadata == nullptr ? "" : metadata->value.artist.c_str();
}

const char* ag_metadata_album(const ag_metadata* metadata)
{
    return metadata == nullptr ? "" : metadata->value.album.c_str();
}

const char* ag_metadata_format(const ag_metadata* metadata)
{
    return metadata == nullptr ? "" : metadata->value.format.c_str();
}

int ag_metadata_sample_rate(const ag_metadata* metadata)
{
    return metadata == nullptr ? 0 : metadata->value.sample_rate;
}

int ag_metadata_channels(const ag_metadata* metadata)
{
    return metadata == nullptr ? 0 : metadata->value.channels;
}

int ag_metadata_bits_per_sample(const ag_metadata* metadata)
{
    return metadata == nullptr ? 0 : metadata->value.bits_per_sample;
}

long long ag_metadata_bit_rate(const ag_metadata* metadata)
{
    return metadata == nullptr ? 0 : metadata->value.bit_rate;
}

long long ag_metadata_duration_ms(const ag_metadata* metadata)
{
    return metadata == nullptr ? 0 : metadata->value.duration_ms;
}

const unsigned char* ag_metadata_cover(const ag_metadata* metadata,
                                       size_t* size,
                                       const char** mime_type)
{
    if (size != nullptr) {
        *size = metadata == nullptr ? 0U : metadata->value.cover.size();
    }
    if (mime_type != nullptr) {
        *mime_type = metadata == nullptr
                         ? ""
                         : metadata->value.cover_mime_type.c_str();
    }
    return metadata == nullptr || metadata->value.cover.empty()
               ? nullptr
               : metadata->value.cover.data();
}

ag_cancel_token* ag_cancel_token_create(void)
{
    return new (std::nothrow) ag_cancel_token;
}

void ag_cancel_token_cancel(ag_cancel_token* token)
{
    if (token != nullptr) {
        token->cancelled.store(true, std::memory_order_relaxed);
    }
}

void ag_cancel_token_destroy(ag_cancel_token* token)
{
    delete token;
}

ag_result ag_waveform_analyze(const char* utf8_path,
                              const size_t target_points,
                              const ag_cancel_token* cancel_token,
                              const ag_progress_callback progress_callback,
                              void* const user_data,
                              ag_waveform** out_waveform)
{
    if (out_waveform == nullptr) {
        return AG_INVALID_ARGUMENT;
    }
    *out_waveform = nullptr;
    if (utf8_path == nullptr || utf8_path[0] == '\0'
        || target_points == 0U) {
        return AG_INVALID_ARGUMENT;
    }

    try {
        std::vector<float> peaks;
        const std::atomic_bool* cancelled =
            cancel_token == nullptr ? nullptr : &cancel_token->cancelled;
        const ag_result result = agplayer::WaveformAnalyzer::analyze(
            utf8_path, target_points, cancelled, progress_callback, user_data,
            peaks);
        if (result != AG_OK) {
            return result;
        }
        *out_waveform = new (std::nothrow) ag_waveform{std::move(peaks)};
        return *out_waveform == nullptr ? AG_INTERNAL_ERROR : AG_OK;
    } catch (...) {
        return AG_INTERNAL_ERROR;
    }
}

size_t ag_waveform_count(const ag_waveform* waveform)
{
    return waveform == nullptr ? 0U : waveform->peaks.size();
}

float ag_waveform_peak(const ag_waveform* waveform, const size_t index)
{
    return waveform == nullptr || index >= waveform->peaks.size()
               ? 0.0F
               : waveform->peaks[index];
}

void ag_waveform_destroy(ag_waveform* waveform)
{
    delete waveform;
}
