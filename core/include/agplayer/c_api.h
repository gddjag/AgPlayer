#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ag_player ag_player;
typedef struct ag_metadata ag_metadata;
typedef struct ag_waveform ag_waveform;
typedef struct ag_cancel_token ag_cancel_token;
typedef void (*ag_progress_callback)(float progress, void* user_data);

typedef enum ag_result {
    AG_OK = 0,
    AG_INVALID_ARGUMENT = 1,
    AG_IO_ERROR = 2,
    AG_UNSUPPORTED_FORMAT = 3,
    AG_DECODE_ERROR = 4,
    AG_DEVICE_ERROR = 5,
    AG_CANCELLED = 6,
    AG_INTERNAL_ERROR = 7
} ag_result;

typedef enum ag_audio_backend {
    AG_AUDIO_BACKEND_DEFAULT = 0,
    AG_AUDIO_BACKEND_NULL = 1
} ag_audio_backend;

typedef enum ag_playback_state {
    AG_STOPPED = 0,
    AG_LOADING = 1,
    AG_PLAYING = 2,
    AG_PAUSED = 3,
    AG_ERROR = 4
} ag_playback_state;

typedef enum ag_playback_mode {
    AG_MODE_SEQUENTIAL = 0,
    AG_MODE_REPEAT_ONE = 1,
    AG_MODE_SHUFFLE = 2
} ag_playback_mode;

typedef struct ag_player_config {
    ag_audio_backend backend;
    unsigned int buffer_frames;
} ag_player_config;

typedef struct ag_playback_snapshot {
    ag_playback_state state;
    long long position_ms;
    long long duration_ms;
    float volume;
    int muted;
    size_t track_index;
    size_t track_count;
    ag_playback_mode mode;
} ag_playback_snapshot;

ag_result ag_player_create(ag_player** out_player);
ag_result ag_player_create_with_config(const ag_player_config* config,
                                       ag_player** out_player);
void ag_player_destroy(ag_player* player);
ag_result ag_player_last_error(const ag_player* player,
                               char* buffer,
                               size_t capacity,
                               size_t* required);
ag_result ag_player_load(ag_player* player, const char* utf8_path);
ag_result ag_player_set_queue(ag_player* player,
                              const char* const* utf8_paths,
                              size_t count,
                              size_t start_index);
ag_result ag_player_play(ag_player* player);
ag_result ag_player_pause(ag_player* player);
ag_result ag_player_stop(ag_player* player);
ag_result ag_player_seek(ag_player* player, long long position_ms);
ag_result ag_player_next(ag_player* player);
ag_result ag_player_previous(ag_player* player);
ag_result ag_player_set_mode(ag_player* player, ag_playback_mode mode);
ag_result ag_player_set_volume(ag_player* player, float volume);
ag_result ag_player_set_muted(ag_player* player, int muted);
ag_result ag_player_snapshot(const ag_player* player,
                             ag_playback_snapshot* snapshot);
ag_result ag_player_retry_device(ag_player* player);
int ag_player_device_lost(const ag_player* player);
ag_result ag_player_simulate_device_loss(ag_player* player);

ag_result ag_metadata_open(const char* utf8_path, ag_metadata** out_metadata);
void ag_metadata_destroy(ag_metadata* metadata);
const char* ag_metadata_title(const ag_metadata* metadata);
const char* ag_metadata_artist(const ag_metadata* metadata);
const char* ag_metadata_album(const ag_metadata* metadata);
const char* ag_metadata_format(const ag_metadata* metadata);
int ag_metadata_sample_rate(const ag_metadata* metadata);
int ag_metadata_channels(const ag_metadata* metadata);
int ag_metadata_bits_per_sample(const ag_metadata* metadata);
long long ag_metadata_bit_rate(const ag_metadata* metadata);
long long ag_metadata_duration_ms(const ag_metadata* metadata);
const unsigned char* ag_metadata_cover(const ag_metadata* metadata,
                                       size_t* size,
                                       const char** mime_type);
const char* ag_metadata_year(const ag_metadata* metadata);
const char* ag_metadata_genre(const ag_metadata* metadata);

/* Write metadata to an audio file using FFmpeg stream copy (no re-encoding).
 * Fields set to NULL are preserved from the source. cover_data is applied
 * only if non-NULL and cover_size > 0. Writes to a temp file then atomically
 * replaces the original. Returns AG_OK on success, or an error code. */
ag_result ag_metadata_write(const char* utf8_path,
                            const char* title,
                            const char* artist,
                            const char* album,
                            const char* year,
                            const char* genre,
                            const unsigned char* cover_data,
                            size_t cover_size,
                            const char* cover_mime_type);

ag_cancel_token* ag_cancel_token_create(void);
void ag_cancel_token_cancel(ag_cancel_token* token);
void ag_cancel_token_destroy(ag_cancel_token* token);

/* Transcode an audio file to a new format/path. The output container is
 * determined by the output_path file extension (e.g. .mp3, .wav, .flac,
 * .m4a, .ogg). codec_name NULL = auto-select encoder for the container.
 * bit_rate 0 = codec default. sample_rate 0 = keep source. channels 0 = keep
 * source. cancel_token NULL = not cancellable. progress_callback NULL = no
 * progress reporting. Returns AG_OK on success, AG_CANCELLED if cancelled. */
ag_result ag_transcode(const char* input_path,
                       const char* output_path,
                       const char* codec_name,
                       long long bit_rate,
                       int sample_rate,
                       int channels,
                       const ag_cancel_token* cancel_token,
                       ag_progress_callback progress_callback,
                       void* user_data);

/* Extended options for ag_pitch_shift_ex. Set unused fields to 0/NULL. */
typedef struct ag_pitch_shift_options {
    int vocal_protection;    /* 1 = attempt to preserve vocal formants (experimental) */
    int smooth_transition;   /* 1 = use smooth transition blending (experimental) */
    int output_sample_rate;  /* 0 = auto (follow pitch/tempo), else target Hz */
} ag_pitch_shift_options;

/* Pitch-shift an audio file using FFmpeg asetrate + atempo filters.
 * pitch_cents: pitch shift in cents (1 semitone = 100 cents), range -1200..1200.
 * keep_tempo: 1 = preserve original tempo/duration (pitch only), 0 = pitch and
 *   tempo change together.
 * tempo_ratio: additional tempo multiplier (1.0 = no change), range 0.5..2.0.
 * output_codec_name: NULL = same codec as input, else FFmpeg codec name
 *   (e.g. "libmp3lame", "flac", "libopus"). Output container is inferred from
 *   output_path extension.
 * options: additional options (may be NULL for defaults).
 * Returns AG_OK on success, AG_CANCELLED if cancelled. */
ag_result ag_pitch_shift_ex(const char* input_path,
                            const char* output_path,
                            int pitch_cents,
                            int keep_tempo,
                            double tempo_ratio,
                            const char* output_codec_name,
                            const ag_pitch_shift_options* options,
                            const ag_cancel_token* cancel_token,
                            ag_progress_callback progress_callback,
                            void* user_data);

/* Backwards-compatible pitch-shift wrapper. Calls ag_pitch_shift_ex with
 * output_codec_name = NULL and default options. */
ag_result ag_pitch_shift(const char* input_path,
                         const char* output_path,
                         int pitch_cents,
                         int keep_tempo,
                         double tempo_ratio,
                         const ag_cancel_token* cancel_token,
                         ag_progress_callback progress_callback,
                         void* user_data);

/* Light edit: trim + fade in/out + gain. Decodes to float32, applies edits
 * in-memory, re-encodes with the same codec as input.
 * trim_start_ms: 0 = start of file. trim_end_ms: 0 = end of file.
 * fade_in_ms / fade_out_ms: 0 = no fade. Linear fade.
 * gain: linear amplitude factor (1.0 = no change, 0.5 = -6dB, 2.0 = +6dB).
 * output_path: destination file path (same codec as input).
 * Returns AG_OK on success, AG_CANCELLED if cancelled. */
ag_result ag_light_edit(const char* input_path,
                        const char* output_path,
                        long long trim_start_ms,
                        long long trim_end_ms,
                        int fade_in_ms,
                        int fade_out_ms,
                        double gain,
                        const ag_cancel_token* cancel_token,
                        ag_progress_callback progress_callback,
                        void* user_data);

ag_result ag_waveform_analyze(const char* utf8_path,
                              size_t target_points,
                              const ag_cancel_token* cancel_token,
                              ag_progress_callback progress_callback,
                              void* user_data,
                              ag_waveform** out_waveform);
size_t ag_waveform_count(const ag_waveform* waveform);
float ag_waveform_peak(const ag_waveform* waveform, size_t index);
void ag_waveform_destroy(ag_waveform* waveform);

#ifdef __cplusplus
}
#endif
