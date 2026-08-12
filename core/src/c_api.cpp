#include <agplayer/c_api.h>

#include "core_context.hpp"
#include "decoder.hpp"
#include "metadata_writer.hpp"
#include "pitch_shifter.hpp"
#include "light_editor.hpp"
#include "multitrack_editor.hpp"
#include "transcoder.hpp"

#include <algorithm>
#include "bpm_analyzer.hpp"
#include "waveform_analyzer.hpp"

#include <atomic>
#include <cstring>
#include <functional>
#include <mutex>
#include <new>
#include <string>
#include <utility>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
}

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
    case agplayer::PlaybackMode::RepeatAll:
        return AG_MODE_REPEAT_ALL;
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
    std::mutex output_device_mutex;
    std::vector<agplayer::OutputDevice> output_device_snapshot;
};

struct ag_metadata {
    agplayer::MediaMetadata value;
};

struct ag_waveform {
    std::vector<float> peaks;
    std::vector<float> bass_;
    std::vector<float> mid_;
    std::vector<float> high_;
    double bpm_ = 0.0;
    std::uint64_t duration_ms_ = 0U;
    std::uint64_t total_samples_ = 0U;
    int sample_rate_ = 0;
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

ag_result ag_player_queue_next(ag_player* player, const char* utf8_path)
{
    if (player == nullptr || utf8_path == nullptr || utf8_path[0] == '\0') {
        return AG_INVALID_ARGUMENT;
    }
    return guard_result([&] { return player->context.queue_next(utf8_path); });
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
            && mode != AG_MODE_SHUFFLE && mode != AG_MODE_REPEAT_ALL)) {
        return AG_INVALID_ARGUMENT;
    }
    const agplayer::PlaybackMode value = mode == AG_MODE_REPEAT_ONE
                                             ? agplayer::PlaybackMode::RepeatOne
                                         : mode == AG_MODE_SHUFFLE
                                             ? agplayer::PlaybackMode::Shuffle
                                         : mode == AG_MODE_REPEAT_ALL
                                             ? agplayer::PlaybackMode::RepeatAll
                                             : agplayer::PlaybackMode::Sequential;
    return guard_result([&] { return player->context.set_mode(value); });
}

ag_result ag_player_set_volume(ag_player* player, const float volume)
{
    return player == nullptr
               ? AG_INVALID_ARGUMENT
               : guard_result([&] { return player->context.set_volume(volume); });
}

ag_result ag_player_set_replay_gain(ag_player* player, const float gain_db,
                                    const float peak,
                                    const int clip_protection)
{
    if (player == nullptr || (clip_protection != 0 && clip_protection != 1)) {
        return AG_INVALID_ARGUMENT;
    }
    return guard_result([&] {
        return player->context.set_replay_gain(gain_db, peak,
                                               clip_protection != 0);
    });
}

ag_result ag_player_set_equalizer(
    ag_player* player,
    const ag_equalizer_settings* settings)
{
    if (player == nullptr || settings == nullptr
        || (settings->enabled != 0 && settings->enabled != 1)
        || (settings->bypassed != 0 && settings->bypassed != 1)
        || (settings->auto_clip_protection != 0
            && settings->auto_clip_protection != 1)) {
        return AG_INVALID_ARGUMENT;
    }
    return guard_result([&] {
        agplayer::GraphicEqSettings value;
        value.enabled = settings->enabled != 0;
        value.bypassed = settings->bypassed != 0;
        value.auto_clip_protection = settings->auto_clip_protection != 0;
        value.preamp_db = settings->preamp_db;
        std::copy(std::begin(settings->band_gain_db),
                  std::end(settings->band_gain_db),
                  value.band_gain_db.begin());
        value.q = settings->q;
        value.transition_ms = settings->transition_ms;
        return player->context.set_equalizer(value, settings->revision);
    });
}

ag_result ag_player_equalizer_status(const ag_player* player,
                                     ag_equalizer_status* status)
{
    if (player == nullptr || status == nullptr) {
        return AG_INVALID_ARGUMENT;
    }
    return guard_result([&] {
        const agplayer::EqualizerStatus value =
            player->context.equalizer_status();
        status->revision = value.revision;
        status->enabled = value.enabled ? 1 : 0;
        status->bypassed = value.bypassed ? 1 : 0;
        status->auto_clip_protection = value.auto_clip_protection ? 1 : 0;
        status->sample_rate = value.sample_rate;
        status->active = value.active ? 1 : 0;
        status->protection_db = value.protection_db;
        return AG_OK;
    });
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
        snapshot->sample_rate = value.sample_rate;
        snapshot->volume = value.volume;
        snapshot->muted = value.muted ? 1 : 0;
        snapshot->track_index = value.track_index;
        snapshot->track_count = value.track_count;
        snapshot->mode = to_c_mode(value.mode);
        return AG_OK;
    });
}

ag_result ag_player_spectrum(ag_player* player,
                             float* bins,
                             const size_t bin_count)
{
    if (player == nullptr || bins == nullptr || bin_count == 0U) {
        return AG_INVALID_ARGUMENT;
    }
    return guard_result(
        [&] { return player->context.spectrum(bins, bin_count); });
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
    return guard_result([&] { player->context.simulate_device_loss(); return AG_OK; });
}

ag_result ag_player_output_device_count(ag_player* player, size_t* count)
{
    if (player == nullptr || count == nullptr) {
        return AG_INVALID_ARGUMENT;
    }
    return guard_result([&] {
        const std::lock_guard<std::mutex> lock(
            player->output_device_mutex);
        player->output_device_snapshot =
            player->context.output_devices();
        *count = player->output_device_snapshot.size();
        return AG_OK;
    });
}

ag_result ag_player_output_device_info(ag_player* player,
                                       const size_t index,
                                       char* id_buffer,
                                       const size_t id_capacity,
                                       size_t* id_required,
                                       char* name_buffer,
                                       const size_t name_capacity,
                                       size_t* name_required)
{
    if (player == nullptr || id_required == nullptr
        || name_required == nullptr) {
        return AG_INVALID_ARGUMENT;
    }
    return guard_result([&] {
        const std::lock_guard<std::mutex> lock(
            player->output_device_mutex);
        if (index >= player->output_device_snapshot.size()) {
            return AG_INVALID_ARGUMENT;
        }
        const agplayer::OutputDevice& device =
            player->output_device_snapshot[index];
        *id_required = device.id.size() + 1U;
        *name_required = device.name.size() + 1U;
        if (id_capacity == 0U && name_capacity == 0U) {
            return AG_OK;
        }
        if (id_buffer == nullptr || name_buffer == nullptr
            || id_capacity < *id_required
            || name_capacity < *name_required) {
            return AG_INVALID_ARGUMENT;
        }
        std::memcpy(id_buffer, device.id.c_str(), *id_required);
        std::memcpy(name_buffer, device.name.c_str(), *name_required);
        return AG_OK;
    });
}

ag_result ag_player_output_device_id(ag_player* player,
                                     const size_t index,
                                     char* buffer,
                                     const size_t capacity,
                                     size_t* required)
{
    if (player == nullptr || required == nullptr) {
        return AG_INVALID_ARGUMENT;
    }
    return guard_result([&] {
        const std::lock_guard<std::mutex> lock(
            player->output_device_mutex);
        if (index >= player->output_device_snapshot.size()) {
            return AG_INVALID_ARGUMENT;
        }
        const std::string& id =
            player->output_device_snapshot[index].id;
        *required = id.size() + 1U;
        if (capacity == 0U) {
            return AG_OK;
        }
        if (buffer == nullptr || capacity < *required) {
            return AG_INVALID_ARGUMENT;
        }
        std::memcpy(buffer, id.c_str(), *required);
        return AG_OK;
    });
}

ag_result ag_player_output_device_name(ag_player* player,
                                       const size_t index,
                                       char* buffer,
                                       const size_t capacity,
                                       size_t* required)
{
    if (player == nullptr || required == nullptr) {
        return AG_INVALID_ARGUMENT;
    }
    return guard_result([&] {
        const std::lock_guard<std::mutex> lock(
            player->output_device_mutex);
        if (index >= player->output_device_snapshot.size()) {
            return AG_INVALID_ARGUMENT;
        }
        const std::string& name =
            player->output_device_snapshot[index].name;
        *required = name.size() + 1U;
        if (capacity == 0U) {
            return AG_OK;
        }
        if (buffer == nullptr || capacity < *required) {
            return AG_INVALID_ARGUMENT;
        }
        std::memcpy(buffer, name.c_str(), *required);
        return AG_OK;
    });
}

ag_result ag_player_set_output_device(ag_player* player,
                                      const char* utf8_id,
                                      const int exclusive)
{
    if (player == nullptr || utf8_id == nullptr
        || (exclusive != 0 && exclusive != 1)) {
        return AG_INVALID_ARGUMENT;
    }
    return guard_result([&] {
        return player->context.set_output_device(
            std::string(utf8_id), exclusive != 0);
    });
}

int ag_player_exclusive_mode_active(const ag_player* player)
{
    return player != nullptr && player->context.exclusive_mode_active()
               ? 1
               : 0;
}

ag_result ag_player_set_transition_fade_ms(ag_player* player,
                                           const int milliseconds)
{
    return player == nullptr
               ? AG_INVALID_ARGUMENT
               : guard_result([&] {
                     return player->context.set_transition_fade_ms(
                         milliseconds);
                 });
}

ag_result ag_player_set_duration_ms(ag_player* player,
                                    const long long duration_ms)
{
    return player == nullptr
               ? AG_INVALID_ARGUMENT
               : guard_result([&] {
                     return player->context.set_duration_ms(duration_ms);
                 });
}

ag_result ag_player_set_match_track_sample_rate(ag_player* player,
                                                 const int enabled)
{
    if (player == nullptr || (enabled != 0 && enabled != 1)) {
        return AG_INVALID_ARGUMENT;
    }
    return guard_result([&] {
        return player->context.set_match_track_sample_rate(enabled != 0);
    });
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

const char* ag_metadata_album_artist(const ag_metadata* metadata)
{
    return metadata == nullptr ? "" : metadata->value.album_artist.c_str();
}

const char* ag_metadata_track(const ag_metadata* metadata)
{
    return metadata == nullptr ? "" : metadata->value.track.c_str();
}

const char* ag_metadata_disc(const ag_metadata* metadata)
{
    return metadata == nullptr ? "" : metadata->value.disc.c_str();
}

const char* ag_metadata_composer(const ag_metadata* metadata)
{
    return metadata == nullptr ? "" : metadata->value.composer.c_str();
}

const char* ag_metadata_comment(const ag_metadata* metadata)
{
    return metadata == nullptr ? "" : metadata->value.comment.c_str();
}

const char* ag_metadata_bpm_tag(const ag_metadata* metadata)
{
    return metadata == nullptr ? "" : metadata->value.bpm.c_str();
}

const char* ag_metadata_copyright(const ag_metadata* metadata)
{
    return metadata == nullptr ? "" : metadata->value.copyright.c_str();
}

const char* ag_metadata_encoder(const ag_metadata* metadata)
{
    return metadata == nullptr ? "" : metadata->value.encoder.c_str();
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

const char* ag_metadata_year(const ag_metadata* metadata)
{
    return metadata == nullptr ? "" : metadata->value.year.c_str();
}

const char* ag_metadata_genre(const ag_metadata* metadata)
{
    return metadata == nullptr ? "" : metadata->value.genre.c_str();
}

const char* ag_metadata_lyrics(const ag_metadata* metadata)
{
    return metadata == nullptr ? "" : metadata->value.lyrics.c_str();
}

ag_result ag_metadata_write(const char* utf8_path,
                            const char* title,
                            const char* artist,
                            const char* album,
                            const char* year,
                            const char* genre,
                            const char* lyrics,
                            const unsigned char* cover_data,
                            const size_t cover_size,
                            const char* cover_mime_type)
{
    return ag_metadata_write_extended(utf8_path,
                                      title,
                                      artist,
                                      album,
                                      nullptr,
                                      year,
                                      genre,
                                      nullptr,
                                      nullptr,
                                      nullptr,
                                      nullptr,
                                      nullptr,
                                      nullptr,
                                      nullptr,
                                      lyrics,
                                      cover_data,
                                      cover_size,
                                      cover_mime_type);
}

ag_result ag_metadata_write_extended(const char* utf8_path,
                                     const char* title,
                                     const char* artist,
                                     const char* album,
                                     const char* album_artist,
                                     const char* date,
                                     const char* genre,
                                     const char* track,
                                     const char* disc,
                                     const char* composer,
                                     const char* comment,
                                     const char* bpm,
                                     const char* copyright,
                                     const char* encoder,
                                     const char* lyrics,
                                     const unsigned char* cover_data,
                                     const size_t cover_size,
                                     const char* cover_mime_type)
{
    if (utf8_path == nullptr || utf8_path[0] == '\0') {
        return AG_INVALID_ARGUMENT;
    }

    try {
        agplayer::MetadataUpdate update;
        if (title != nullptr) update.title = title;
        if (artist != nullptr) update.artist = artist;
        if (album != nullptr) update.album = album;
        if (album_artist != nullptr) update.album_artist = album_artist;
        if (date != nullptr) update.year = date;
        if (genre != nullptr) update.genre = genre;
        if (track != nullptr) update.track = track;
        if (disc != nullptr) update.disc = disc;
        if (composer != nullptr) update.composer = composer;
        if (comment != nullptr) update.comment = comment;
        if (bpm != nullptr) update.bpm = bpm;
        if (copyright != nullptr) update.copyright = copyright;
        if (encoder != nullptr) update.encoder = encoder;
        if (lyrics != nullptr) update.lyrics = lyrics;
        if (cover_data != nullptr) {
            update.cover_action = cover_size > 0U
                ? agplayer::CoverAction::Set
                : agplayer::CoverAction::Clear;
            update.cover_data = cover_data;
            update.cover_size = cover_size;
            if (cover_mime_type != nullptr) {
                update.cover_mime_type = cover_mime_type;
            }
        }
        std::string error;
        return agplayer::write_metadata(utf8_path, update, error);
    } catch (...) {
        return AG_INTERNAL_ERROR;
    }
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

ag_result ag_transcode_ex(const char* input_path,
                          const char* output_path,
                          const char* codec_name,
                          const long long bit_rate,
                          const int sample_rate,
                          const int channels,
                          const ag_transcode_options* options,
                          const ag_cancel_token* cancel_token,
                          const ag_progress_callback progress_callback,
                          void* const user_data)
{
    if (input_path == nullptr || input_path[0] == '\0'
        || output_path == nullptr || output_path[0] == '\0') {
        return AG_INVALID_ARGUMENT;
    }

    try {
        agplayer::TranscodeConfig config;
        config.output_path = output_path;
        if (codec_name != nullptr) config.codec_name = codec_name;
        config.bit_rate = bit_rate;
        config.sample_rate = sample_rate;
        config.channels = channels;
        if (options != nullptr) {
            config.volume_normalize = options->volume_normalize != 0;
            config.keep_metadata = options->keep_metadata != 0;
            config.variable_bit_rate = options->bitrate_mode == 1;
            config.quality = std::clamp(options->quality, 0, 100);
        }

        const std::atomic_bool* cancelled =
            cancel_token == nullptr ? nullptr : &cancel_token->cancelled;

        std::function<void(float)> cb;
        if (progress_callback != nullptr) {
            cb = [progress_callback, user_data](float frac) {
                progress_callback(frac, user_data);
            };
        }

        std::string error;
        return agplayer::transcode(input_path, config, cancelled,
                                   std::move(cb), error);
    } catch (...) {
        return AG_INTERNAL_ERROR;
    }
}

ag_result ag_transcode(const char* input_path,
                       const char* output_path,
                       const char* codec_name,
                       const long long bit_rate,
                       const int sample_rate,
                       const int channels,
                       const ag_cancel_token* cancel_token,
                       const ag_progress_callback progress_callback,
                       void* const user_data)
{
    return ag_transcode_ex(input_path, output_path, codec_name, bit_rate,
                           sample_rate, channels, nullptr, cancel_token,
                           progress_callback, user_data);
}

int ag_encoder_available(const char* codec_name)
{
    return codec_name != nullptr && codec_name[0] != '\0'
            && avcodec_find_encoder_by_name(codec_name) != nullptr
        ? 1 : 0;
}

ag_result ag_pitch_shift_ex(const char* input_path,
                            const char* output_path,
                            const int pitch_cents,
                            const int keep_tempo,
                            const double tempo_ratio,
                            const char* output_codec_name,
                            const ag_pitch_shift_options* options,
                            const ag_cancel_token* cancel_token,
                            const ag_progress_callback progress_callback,
                            void* const user_data)
{
    if (input_path == nullptr || input_path[0] == '\0'
        || output_path == nullptr || output_path[0] == '\0') {
        return AG_INVALID_ARGUMENT;
    }

    try {
        agplayer::PitchShiftConfig config;
        config.output_path = output_path;
        config.pitch_cents = pitch_cents;
        config.keep_tempo = keep_tempo != 0;
        config.tempo_ratio = tempo_ratio;
        if (output_codec_name != nullptr) {
            config.output_codec_name = output_codec_name;
        }
        if (options != nullptr) {
            config.vocal_protection = options->vocal_protection != 0;
            config.smooth_transition = options->smooth_transition != 0;
            config.output_sample_rate = options->output_sample_rate;
        }

        const std::atomic_bool* cancelled =
            cancel_token == nullptr ? nullptr : &cancel_token->cancelled;

ag_result ag_transcode_v2(const char* input_path,
                          const ag_transcode_request_v2* request,
                          const ag_cancel_token* cancel_token,
                          const ag_progress_callback progress_callback,
                          void* const user_data)
{
    last_error.clear();
    if (input_path == nullptr || input_path[0] == '\0'
        || request == nullptr
        || request->struct_size < sizeof(ag_transcode_request_v2)
        || request->api_version != AG_TRANSCODE_REQUEST_V2_VERSION
        || request->output_path == nullptr
        || request->output_path[0] == '\0') {
        last_error = "invalid v2 transcode request";
        return AG_INVALID_ARGUMENT;
    }

    try {
        agplayer::TranscodeConfig config;
        config.output_path = request->output_path;
        if (request->muxer_name != nullptr) {
            config.container_name = request->muxer_name;
        }
        if (request->codec_name != nullptr) {
            config.codec_name = request->codec_name;
        }
        config.bit_rate = request->bit_rate;
        config.sample_rate = request->sample_rate;
        if (request->channel_layout != nullptr) {
            config.channel_layout = request->channel_layout;
        }
        if (request->sample_format != nullptr) {
            config.sample_format = request->sample_format;
        }
        config.audio_stream_index = request->audio_stream_index;
        config.keep_metadata = request->keep_metadata != 0;
        config.keep_cover = request->keep_cover != 0;
        config.variable_bit_rate = request->bitrate_mode == 1;
        config.quality = std::clamp(request->quality, 0, 100);

        const std::atomic_bool* cancelled = cancel_token == nullptr
            ? nullptr : &cancel_token->cancelled;
        std::function<void(float)> callback;
        if (progress_callback != nullptr) {
            callback = [progress_callback, user_data](const float progress) {
                progress_callback(progress, user_data);
            };
        }
        std::string error;
        const ag_result result = agplayer::transcode(
            input_path, config, cancelled, std::move(callback), error);
        if (result != AG_OK) {
            last_error = std::move(error);
        }
        return result;
    } catch (...) {
        last_error = "unexpected exception in v2 transcoder";
        return AG_INTERNAL_ERROR;
    }
}

        std::function<void(float)> cb;
        if (progress_callback != nullptr) {
            cb = [progress_callback, user_data](float frac) {
                progress_callback(frac, user_data);
            };
        }

        std::string error;
        return agplayer::pitch_shift(input_path, config, cancelled,
                                     std::move(cb), error);
    } catch (...) {
        return AG_INTERNAL_ERROR;
    }
}

ag_result ag_pitch_shift(const char* input_path,
                         const char* output_path,
                         const int pitch_cents,
                         const int keep_tempo,
                         const double tempo_ratio,
                         const ag_cancel_token* cancel_token,
                         const ag_progress_callback progress_callback,
                         void* const user_data)
{
    return ag_pitch_shift_ex(input_path, output_path, pitch_cents, keep_tempo,
                             tempo_ratio, nullptr, nullptr, cancel_token,
                             progress_callback, user_data);
}

ag_result ag_light_edit(const char* input_path,
                        const char* output_path,
                        const long long trim_start_ms,
                        const long long trim_end_ms,
                        const int fade_in_ms,
                        const int fade_out_ms,
                        const double gain,
                        const ag_cancel_token* cancel_token,
                        const ag_progress_callback progress_callback,
                        void* const user_data)
{
    if (input_path == nullptr || input_path[0] == '\0'
        || output_path == nullptr || output_path[0] == '\0') {
        return AG_INVALID_ARGUMENT;
    }

    try {
        agplayer::LightEditConfig config;
        config.output_path = output_path;
        config.trim_start_ms = trim_start_ms;
        config.trim_end_ms = trim_end_ms;
        config.fade_in_ms = fade_in_ms;
        config.fade_out_ms = fade_out_ms;
        config.gain = gain;

        const std::atomic_bool* cancelled =
            cancel_token == nullptr ? nullptr : &cancel_token->cancelled;

        std::function<void(float)> cb;
        if (progress_callback != nullptr) {
            cb = [progress_callback, user_data](float frac) {
                progress_callback(frac, user_data);
            };
        }

        std::string error;
        return agplayer::light_edit(input_path, config, cancelled,
                                     std::move(cb), error);
    } catch (...) {
        return AG_INTERNAL_ERROR;
    }
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
        std::vector<float> bass;
        std::vector<float> mid;
        std::vector<float> high;
        const std::atomic_bool* cancelled =
            cancel_token == nullptr ? nullptr : &cancel_token->cancelled;
        std::uint64_t duration_ms = 0U;
        std::uint64_t total_samples = 0U;
        int sample_rate = 0;
        const ag_result result = agplayer::WaveformAnalyzer::analyze(
            utf8_path, target_points, cancelled, progress_callback, user_data,
            peaks, bass, mid, high, agplayer::WaveformAggregation::Peak,
            &duration_ms, &total_samples, &sample_rate);
        if (result != AG_OK) {
            return result;
        }
        ag_waveform* waveform = new (std::nothrow) ag_waveform{};
        if (waveform == nullptr) {
            return AG_INTERNAL_ERROR;
        }
        waveform->peaks = std::move(peaks);
        waveform->bass_ = std::move(bass);
        waveform->mid_ = std::move(mid);
        waveform->high_ = std::move(high);
        waveform->duration_ms_ = duration_ms;
        waveform->total_samples_ = total_samples;
        waveform->sample_rate_ = sample_rate;
        *out_waveform = waveform;
        return AG_OK;
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

size_t ag_waveform_layer_count(const ag_waveform* waveform,
                               const ag_waveform_layer layer)
{
    if (waveform == nullptr) {
        return 0U;
    }
    switch (layer) {
    case AG_WAVEFORM_LAYER_MIX:
        return waveform->peaks.size();
    case AG_WAVEFORM_LAYER_BASS:
        return waveform->bass_.size();
    case AG_WAVEFORM_LAYER_MID:
        return waveform->mid_.size();
    case AG_WAVEFORM_LAYER_HIGH:
        return waveform->high_.size();
    }
    return 0U;
}

float ag_waveform_layer_peak(const ag_waveform* waveform,
                             const ag_waveform_layer layer,
                             const size_t index)
{
    if (waveform == nullptr) {
        return 0.0F;
    }
    const std::vector<float>* layer_peaks = nullptr;
    switch (layer) {
    case AG_WAVEFORM_LAYER_MIX:
        layer_peaks = &waveform->peaks;
        break;
    case AG_WAVEFORM_LAYER_BASS:
        layer_peaks = &waveform->bass_;
        break;
    case AG_WAVEFORM_LAYER_MID:
        layer_peaks = &waveform->mid_;
        break;
    case AG_WAVEFORM_LAYER_HIGH:
        layer_peaks = &waveform->high_;
        break;
    default:
        return 0.0F;
    }
    return index >= layer_peaks->size() ? 0.0F : (*layer_peaks)[index];
}

double ag_waveform_bpm(const ag_waveform* waveform)
{
    return waveform == nullptr ? 0.0 : waveform->bpm_;
}

ag_result ag_track_analysis(const char* utf8_path,
                            const size_t target_points,
                            const ag_cancel_token* cancel_token,
                            const ag_progress_callback progress_callback,
                            void* const user_data,
                            ag_waveform** out_waveform,
                            double* const out_bpm)
{
    return ag_track_analysis_with_aggregation(
        utf8_path, target_points, AG_WAVEFORM_AGGREGATION_PEAK,
        cancel_token, progress_callback, user_data, out_waveform, out_bpm);
}

uint64_t ag_waveform_duration_ms(const ag_waveform* waveform)
{
    return waveform == nullptr ? 0U : waveform->duration_ms_;
}

uint64_t ag_waveform_total_samples(const ag_waveform* waveform)
{
    return waveform == nullptr ? 0U : waveform->total_samples_;
}

int ag_waveform_sample_rate(const ag_waveform* waveform)
{
    return waveform == nullptr ? 0 : waveform->sample_rate_;
}

ag_result ag_track_analysis_with_aggregation(
    const char* utf8_path,
    const size_t target_points,
    const ag_waveform_aggregation aggregation,
    const ag_cancel_token* cancel_token,
    const ag_progress_callback progress_callback,
    void* const user_data,
    ag_waveform** out_waveform,
    double* const out_bpm)
{
    if (out_waveform == nullptr) {
        return AG_INVALID_ARGUMENT;
    }

    *out_waveform = nullptr;
    if (out_bpm != nullptr) {
        *out_bpm = 0.0;
    }

    if (utf8_path == nullptr || utf8_path[0] == '\0' || target_points == 0U
        || aggregation < AG_WAVEFORM_AGGREGATION_PEAK
        || aggregation > AG_WAVEFORM_AGGREGATION_RMS) {
        return AG_INVALID_ARGUMENT;
    }

    return guard_result([&] {
        const std::atomic_bool* cancelled =
            cancel_token == nullptr ? nullptr : &cancel_token->cancelled;

        std::vector<float> peaks;
        std::vector<float> bass;
        std::vector<float> mid;
        std::vector<float> high;
        const auto internal_aggregation =
            static_cast<agplayer::WaveformAggregation>(aggregation);
        std::uint64_t duration_ms = 0U;
        std::uint64_t total_samples = 0U;
        int sample_rate = 0;
        ag_result waveform_result = agplayer::WaveformAnalyzer::analyze(
            utf8_path, target_points, cancelled, progress_callback, user_data,
            peaks, bass, mid, high, internal_aggregation, &duration_ms,
            &total_samples, &sample_rate);
        if (waveform_result != AG_OK) {
            return waveform_result;
        }

        ag_waveform* waveform = new (std::nothrow) ag_waveform{};
        if (waveform == nullptr) {
            return AG_INTERNAL_ERROR;
        }
        waveform->peaks = std::move(peaks);
        waveform->bass_ = std::move(bass);
        waveform->mid_ = std::move(mid);
        waveform->high_ = std::move(high);
        waveform->duration_ms_ = duration_ms;
        waveform->total_samples_ = total_samples;
        waveform->sample_rate_ = sample_rate;

        agplayer::BpmAnalyzeInput bpm_input;
        bpm_input.file_path = utf8_path;
        bpm_input.cancelled = cancelled;
        agplayer::BpmAnalyzeOutput bpm_output;
        const ag_result bpm_result = agplayer::analyze_bpm(bpm_input, &bpm_output);
        if (bpm_result == AG_OK) {
            waveform->bpm_ = bpm_output.bpm;
            if (out_bpm != nullptr) {
                *out_bpm = bpm_output.bpm;
            }
        } else if (bpm_result == AG_CANCELLED) {
            delete waveform;
            return AG_CANCELLED;
        }
        // Non-fatal BPM failures (e.g. file too short) keep the waveform and
        // leave *out_bpm at 0.

        *out_waveform = waveform;
        return AG_OK;
    });
}

ag_result ag_bpm_analyze(const char* file_path, ag_bpm_result* out)
{
    if (out == nullptr) {
        return AG_INVALID_ARGUMENT;
    }

    *out = {0.0, 0.0};

    if (file_path == nullptr || file_path[0] == '\0') {
        return AG_INVALID_ARGUMENT;
    }

    return guard_result([&] {
        agplayer::BpmAnalyzeInput input;
        input.file_path = file_path;
        agplayer::BpmAnalyzeOutput output;
        const ag_result result = agplayer::analyze_bpm(input, &output);
        if (result == AG_OK) {
            out->bpm = output.bpm;
            out->confidence = output.confidence;
        }
        return result;
    });
}

ag_result ag_multitrack_edit_ex2(const size_t track_count,
                                 const char* const* input_paths,
                                 const long long* timeline_start_ms,
                                 const long long* trim_start_ms,
                                 const long long* trim_end_ms,
                                 const int* fade_in_ms,
                                 const int* fade_out_ms,
                                 const double* gain,
                                 const double* pan,
                                 const char* output_path,
                                 const ag_cancel_token* cancel_token,
                                 const ag_progress_callback progress_callback,
                                 void* const user_data)
{
    if (track_count == 0 || output_path == nullptr || output_path[0] == '\0') {
        return AG_INVALID_ARGUMENT;
    }

    try {
        agplayer::MultiTrackEditConfig config;
        config.output_path = output_path;
        config.tracks.reserve(track_count);
        for (size_t i = 0; i < track_count; ++i) {
            agplayer::MultiTrackEditConfig::Track track;
            if (input_paths != nullptr && input_paths[i] != nullptr) {
                track.input_path = input_paths[i];
            }
            if (timeline_start_ms != nullptr) {
                track.timeline_start_ms = timeline_start_ms[i];
            }
            if (trim_start_ms != nullptr) track.trim_start_ms = trim_start_ms[i];
            if (trim_end_ms != nullptr) track.trim_end_ms = trim_end_ms[i];
            if (fade_in_ms != nullptr) track.fade_in_ms = fade_in_ms[i];
            if (fade_out_ms != nullptr) track.fade_out_ms = fade_out_ms[i];
            if (gain != nullptr) track.gain = gain[i];
            if (pan != nullptr) track.pan = pan[i];
            config.tracks.push_back(std::move(track));
        }

        const std::atomic_bool* cancelled =
            cancel_token == nullptr ? nullptr : &cancel_token->cancelled;

        std::function<void(float)> cb;
        if (progress_callback != nullptr) {
            cb = [progress_callback, user_data](float frac) {
                progress_callback(frac, user_data);
            };
        }

        std::string error;
        return agplayer::multitrack_edit(config, cancelled, std::move(cb),
                                         error);
    } catch (...) {
        return AG_INTERNAL_ERROR;
    }
}

ag_result ag_multitrack_edit_ex(const size_t track_count,
                                const char* const* input_paths,
                                const long long* timeline_start_ms,
                                const long long* trim_start_ms,
                                const long long* trim_end_ms,
                                const int* fade_in_ms,
                                const int* fade_out_ms,
                                const double* gain,
                                const char* output_path,
                                const ag_cancel_token* cancel_token,
                                const ag_progress_callback progress_callback,
                                void* const user_data)
{
    return ag_multitrack_edit_ex2(
        track_count, input_paths, timeline_start_ms, trim_start_ms,
        trim_end_ms, fade_in_ms, fade_out_ms, gain, nullptr, output_path,
        cancel_token, progress_callback, user_data);
}

ag_result ag_multitrack_edit_v2(
    const size_t track_count,
    const ag_multitrack_track_v2* const tracks,
    const char* const output_path,
    const ag_cancel_token* const cancel_token,
    const ag_progress_callback progress_callback,
    void* const user_data)
{
    if (track_count == 0 || tracks == nullptr || output_path == nullptr
        || output_path[0] == '\0') {
        return AG_INVALID_ARGUMENT;
    }
    try {
        agplayer::MultiTrackEditConfig config;
        config.output_path = output_path;
        config.tracks.reserve(track_count);
        for (size_t index = 0; index < track_count; ++index) {
            const ag_multitrack_track_v2& source = tracks[index];
            if (source.struct_size < sizeof(ag_multitrack_track_v2)
                || source.api_version != 1U) {
                return AG_INVALID_ARGUMENT;
            }
            agplayer::MultiTrackEditConfig::Track track;
            if (source.input_path != nullptr) {
                track.input_path = source.input_path;
            }
            track.timeline_start_ms = source.timeline_start_ms;
            track.trim_start_ms = source.trim_start_ms;
            track.trim_end_ms = source.trim_end_ms;
            track.fade_in_ms = source.fade_in_ms;
            track.fade_out_ms = source.fade_out_ms;
            track.gain = source.gain;
            track.pan = source.pan;
            track.timeline_duration_ms = source.timeline_duration_ms;
            track.loop = source.loop != 0;
            config.tracks.push_back(std::move(track));
        }
        const std::atomic_bool* cancelled = cancel_token == nullptr
            ? nullptr : &cancel_token->cancelled;
        std::function<void(float)> callback;
        if (progress_callback != nullptr) {
            callback = [progress_callback, user_data](float value) {
                progress_callback(value, user_data);
            };
        }
        std::string error;
        return agplayer::multitrack_edit(config, cancelled,
                                         std::move(callback), error);
    } catch (...) {
        return AG_INTERNAL_ERROR;
    }
}

ag_result ag_multitrack_edit_v3(
    const size_t track_count,
    const ag_multitrack_track_v3* const tracks,
    const char* const output_path,
    const ag_cancel_token* const cancel_token,
    const ag_progress_callback progress_callback,
    void* const user_data)
{
    if (track_count == 0 || tracks == nullptr || output_path == nullptr
        || output_path[0] == '\0') {
        return AG_INVALID_ARGUMENT;
    }
    try {
        agplayer::MultiTrackEditConfig config;
        config.output_path = output_path;
        config.tracks.reserve(track_count);
        for (size_t index = 0; index < track_count; ++index) {
            const ag_multitrack_track_v3& source = tracks[index];
            constexpr size_t v3_base_size =
                offsetof(ag_multitrack_track_v3, fade_in_curve);
            if (source.struct_size < v3_base_size
                || source.api_version != 1U) {
                return AG_INVALID_ARGUMENT;
            }
            agplayer::MultiTrackEditConfig::Track track;
            if (source.input_path != nullptr) {
                track.input_path = source.input_path;
            }
            track.timeline_start_sample = source.timeline_start_sample;
            track.trim_start_sample = source.trim_start_sample;
            track.trim_end_sample = source.trim_end_sample;
            track.fade_in_samples = source.fade_in_samples;
            track.fade_out_samples = source.fade_out_samples;
            if (source.struct_size >= sizeof(ag_multitrack_track_v3)) {
                const auto fade_curve = [](int value) {
                    if (value == 0) return agplayer::FadeCurve::Linear;
                    if (value == 2) return agplayer::FadeCurve::Smooth;
                    return agplayer::FadeCurve::EqualPower;
                };
                track.fade_in_curve = fade_curve(source.fade_in_curve);
                track.fade_out_curve = fade_curve(source.fade_out_curve);
            }
            track.gain = source.gain;
            track.pan = source.pan;
            track.timeline_duration_samples =
                source.timeline_duration_samples;
            track.loop = source.loop != 0;
            config.tracks.push_back(std::move(track));
        }
        const std::atomic_bool* cancelled = cancel_token == nullptr
            ? nullptr : &cancel_token->cancelled;
        std::function<void(float)> callback;
        if (progress_callback != nullptr) {
            callback = [progress_callback, user_data](float value) {
                progress_callback(value, user_data);
            };
        }
        std::string error;
        return agplayer::multitrack_edit(config, cancelled,
                                         std::move(callback), error);
    } catch (...) {
        return AG_INTERNAL_ERROR;
    }
}

ag_result ag_multitrack_edit(const size_t track_count,
                             const char* const* input_paths,
                             const long long* trim_start_ms,
                             const long long* trim_end_ms,
                             const int* fade_in_ms,
                             const int* fade_out_ms,
                             const double* gain,
                             const char* output_path,
                             const ag_cancel_token* cancel_token,
                             const ag_progress_callback progress_callback,
                             void* const user_data)
{
    return ag_multitrack_edit_ex(
        track_count, input_paths, nullptr, trim_start_ms, trim_end_ms,
        fade_in_ms, fade_out_ms, gain, output_path, cancel_token,
        progress_callback, user_data);
}
