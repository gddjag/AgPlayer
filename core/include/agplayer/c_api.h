#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ag_player ag_player;
/* Optional mono PCM tap after EQ/transition fade, before user volume, mute and
 * ReplayGain (also after EQ for scratch playback). Disabled by default. Enable and read
 * from one GUI thread only; do not race these calls with player destruction.
 * Reads consume this independent tap, never the legacy spectrum queue.
 * Returns oldest <=1024 queued samples, or sample_count=0 when none are fresh.
 * Consumers may drain multiple bounded reads per poll to preserve continuity.
 * Continuity requires equal generation AND first_sample_index equal to the
 * previous first_sample_index + sample_count. Reset/seek/disable/overflow
 * invalidate generation. Samples use the actual processing sample rate. */
typedef struct ag_visual_pcm_snapshot {
    float samples[1024];
    size_t sample_count;
    int sample_rate;
    uint64_t generation;
    uint64_t first_sample_index;
} ag_visual_pcm_snapshot;
typedef struct ag_metadata ag_metadata;
typedef struct ag_waveform ag_waveform;
typedef struct ag_cancel_token ag_cancel_token;
typedef struct ag_video_decoder ag_video_decoder;
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

typedef enum ag_video_pixel_format {
    AG_VIDEO_PIXEL_FORMAT_BGRA8 = 1
} ag_video_pixel_format;

typedef struct ag_video_frame {
    uint32_t struct_size;
    const unsigned char* data;
    size_t data_size;
    int width;
    int height;
    int stride;
    int pixel_format;
    int64_t pts_ms;
    int sar_num;
    int sar_den;
    int rotation_degrees;
    int end_of_stream;
} ag_video_frame;

typedef struct ag_video_media_info {
    uint32_t struct_size;
    int valid;
    int has_audio;
    int has_video;
} ag_video_media_info;

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
    AG_MODE_SHUFFLE = 2,
    AG_MODE_REPEAT_ALL = 3
} ag_playback_mode;

typedef struct ag_player_config {
    ag_audio_backend backend;
    unsigned int buffer_frames;
} ag_player_config;

typedef struct ag_playback_snapshot {
    ag_playback_state state;
    long long position_ms;
    long long duration_ms;
    int sample_rate;
    float volume;
    int muted;
    size_t track_index;
    size_t track_count;
    ag_playback_mode mode;
} ag_playback_snapshot;

typedef struct ag_playback_time_pitch_config {
    double speed_ratio;
    int keep_pitch;
} ag_playback_time_pitch_config;

typedef struct ag_scratch_status {
    int active;
    int ready;
    int buffering;
} ag_scratch_status;

typedef struct ag_output_levels {
    float left_peak;
    float right_peak;
    float left_rms;
    float right_rms;
} ag_output_levels;

#define AG_EQUALIZER_BAND_COUNT 18

typedef struct ag_equalizer_settings {
    unsigned long long revision;
    int enabled;
    int bypassed;
    int auto_clip_protection;
    double preamp_db;
    double band_gain_db[AG_EQUALIZER_BAND_COUNT];
    double q;
    double transition_ms;
} ag_equalizer_settings;

typedef struct ag_equalizer_status {
    unsigned long long revision;
    int enabled;
    int bypassed;
    int auto_clip_protection;
    int sample_rate;
    int active;
    double protection_db;
    double output_peak_db;
} ag_equalizer_status;

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
ag_result ag_player_set_scoped_queue(ag_player* player,
                                     const char* const* utf8_paths,
                                     size_t count,
                                     size_t start_index,
                                     size_t scope_size,
                                     int allow_fallback);
ag_result ag_player_queue_next(ag_player* player, const char* utf8_path);
ag_result ag_player_play(ag_player* player);
ag_result ag_player_pause(ag_player* player);
ag_result ag_player_stop(ag_player* player);
ag_result ag_player_seek(ag_player* player, long long position_ms);
ag_result ag_player_next(ag_player* player);
ag_result ag_player_previous(ag_player* player);
ag_result ag_player_set_mode(ag_player* player, ag_playback_mode mode);
ag_result ag_player_set_volume(ag_player* player, float volume);
ag_result ag_player_set_replay_gain(ag_player* player, float gain_db,
                                    float peak, int clip_protection);
ag_result ag_player_set_time_pitch(
    ag_player* player,
    const ag_playback_time_pitch_config* config);
ag_result ag_player_get_time_pitch(
    const ag_player* player,
    ag_playback_time_pitch_config* config);
ag_result ag_player_begin_scratch(ag_player* player);
ag_result ag_player_update_scratch(ag_player* player, float signed_rate);
ag_result ag_player_end_scratch(ag_player* player);
ag_result ag_player_cancel_scratch(ag_player* player);
ag_result ag_player_scratch_status(const ag_player* player,
                                   ag_scratch_status* status);
ag_result ag_player_output_levels(const ag_player* player,
                                  ag_output_levels* levels);
ag_result ag_player_set_equalizer(ag_player* player,
                                  const ag_equalizer_settings* settings);
ag_result ag_player_equalizer_status(const ag_player* player,
                                     ag_equalizer_status* status);
ag_result ag_player_set_muted(ag_player* player, int muted);
ag_result ag_player_snapshot(const ag_player* player,
                             ag_playback_snapshot* snapshot);
ag_result ag_player_set_visual_pcm_enabled(ag_player* player, int enabled);
ag_result ag_player_read_visual_pcm(ag_player* player, ag_visual_pcm_snapshot* snapshot);
/* Copies a real-time FFT snapshot from the audible PCM stream.
 * bin_count must be between 1 and 256. Bins are normalized to 0..1. */
ag_result ag_player_spectrum(ag_player* player,
                             float* bins,
                             size_t bin_count);
ag_result ag_player_retry_device(ag_player* player);
int ag_player_device_lost(const ag_player* player);
ag_result ag_player_simulate_device_loss(ag_player* player);
/* Refreshes the output-device snapshot used by the info/id/name accessors. */
ag_result ag_player_output_device_count(ag_player* player, size_t* count);
ag_result ag_player_output_device_info(ag_player* player,
                                       size_t index,
                                       char* id_buffer,
                                       size_t id_capacity,
                                       size_t* id_required,
                                       char* name_buffer,
                                       size_t name_capacity,
                                       size_t* name_required);
ag_result ag_player_output_device_id(ag_player* player,
                                     size_t index,
                                     char* buffer,
                                     size_t capacity,
                                     size_t* required);
ag_result ag_player_output_device_name(ag_player* player,
                                       size_t index,
                                       char* buffer,
                                       size_t capacity,
                                       size_t* required);
ag_result ag_player_set_output_device(ag_player* player,
                                      const char* utf8_id,
                                      int exclusive);
int ag_player_exclusive_mode_active(const ag_player* player);
ag_result ag_player_set_transition_fade_ms(ag_player* player,
                                           int milliseconds);
ag_result ag_player_set_duration_ms(ag_player* player, long long duration_ms);
ag_result ag_player_set_match_track_sample_rate(ag_player* player,
                                                 int enabled);

/* Synchronous software video decoding with opaque handle ownership.
 *
 * Null contract:
 * - create/open/read/seek return AG_INVALID_ARGUMENT for required null inputs.
 * - cancel/close/destroy are no-ops for a null handle.
 *
 * Frame ABI contract:
 * - Set struct_size to sizeof(ag_video_frame) before read. A smaller value is
 *   rejected; a larger, future-sized value is accepted and bytes beyond the
 *   current structure are left untouched.
 * - Successful non-EOS frames are always BGRA8. The data pointer remains valid
 *   only until the next open/read/seek/close/destroy call on the same handle,
 *   whether that call succeeds or fails. Copy the bytes before that boundary.
 *
 * Lifecycle and concurrency contract:
 * - open on an already-open handle returns AG_INVALID_ARGUMENT and leaves the
 *   decode position intact (while invalidating any previously returned data).
 * - Serialize open/read/seek/close calls for a handle. cancel and destroy may
 *   instead be called from another thread while one of those calls is active.
 * - destroy marks the handle as shutting down, requests cancellation, waits for
 *   entered calls to return, then frees it. New result-returning calls admitted
 *   during shutdown return AG_CANCELLED. Do not use a handle after destroy
 *   returns.
 *
 * Media-info ABI contract:
 * - open_with_media_info uses the same cancellable container open as decoding;
 *   it does not perform a second probe. Set struct_size to
 *   sizeof(ag_video_media_info). Smaller values are rejected; future-sized
 *   values are accepted without changing bytes beyond the current structure.
 * - valid is set after container stream discovery succeeds. When valid is 1,
 *   has_audio/has_video describe real container tracks independently of the
 *   function result. Attached pictures are not video tracks. The function may
 *   still return AG_UNSUPPORTED_FORMAT when no video codec can be opened.
 * - The original open entry point retains its decoder-ready return contract.
 */
ag_result ag_video_decoder_create(ag_video_decoder** out_decoder);
ag_result ag_video_decoder_open(ag_video_decoder* decoder,
                                const char* utf8_path);
ag_result ag_video_decoder_open_with_media_info(
    ag_video_decoder* decoder,
    const char* utf8_path,
    ag_video_media_info* out_media_info);
ag_result ag_video_decoder_read(ag_video_decoder* decoder,
                                ag_video_frame* out_frame);
ag_result ag_video_decoder_seek(ag_video_decoder* decoder,
                                int64_t position_ms);
/* May be called from another thread to interrupt open/read/seek. */
void ag_video_decoder_cancel(ag_video_decoder* decoder);
/* Idempotent. Also clears cancellation so the handle can be reopened. */
void ag_video_decoder_close(ag_video_decoder* decoder);
void ag_video_decoder_destroy(ag_video_decoder* decoder);

ag_result ag_metadata_open(const char* utf8_path, ag_metadata** out_metadata);
void ag_metadata_destroy(ag_metadata* metadata);
const char* ag_metadata_title(const ag_metadata* metadata);
const char* ag_metadata_artist(const ag_metadata* metadata);
const char* ag_metadata_album(const ag_metadata* metadata);
const char* ag_metadata_album_artist(const ag_metadata* metadata);
const char* ag_metadata_track(const ag_metadata* metadata);
const char* ag_metadata_disc(const ag_metadata* metadata);
const char* ag_metadata_composer(const ag_metadata* metadata);
const char* ag_metadata_comment(const ag_metadata* metadata);
const char* ag_metadata_bpm_tag(const ag_metadata* metadata);
const char* ag_metadata_custom_tag(const ag_metadata* metadata);
const char* ag_metadata_copyright(const ag_metadata* metadata);
const char* ag_metadata_encoder(const ag_metadata* metadata);
const char* ag_metadata_format(const ag_metadata* metadata);
int ag_metadata_sample_rate(const ag_metadata* metadata);
int ag_metadata_channels(const ag_metadata* metadata);
int ag_metadata_bits_per_sample(const ag_metadata* metadata);
long long ag_metadata_bit_rate(const ag_metadata* metadata);
long long ag_metadata_duration_ms(const ag_metadata* metadata);
int ag_metadata_has_audio(const ag_metadata* metadata);
int ag_metadata_has_video(const ag_metadata* metadata);
int ag_metadata_video_width(const ag_metadata* metadata);
int ag_metadata_video_height(const ag_metadata* metadata);
const unsigned char* ag_metadata_cover(const ag_metadata* metadata,
                                       size_t* size,
                                       const char** mime_type);
const char* ag_metadata_year(const ag_metadata* metadata);
const char* ag_metadata_date(const ag_metadata* metadata);
const char* ag_metadata_genre(const ag_metadata* metadata);
const char* ag_metadata_lyrics(const ag_metadata* metadata);

/* Write metadata to an audio file using FFmpeg stream copy (no re-encoding).
 * Fields set to NULL are preserved from the source. For cover_data, NULL keeps
 * the current cover, non-NULL with cover_size 0 clears it, and non-NULL with a
 * positive size replaces it. lyrics set to NULL is preserved from the source;
 * pass an empty string to clear existing lyrics. Writes to a temp file then
 * atomically replaces the original. Returns AG_OK on success, or an error code. */
ag_result ag_metadata_write(const char* utf8_path,
                            const char* title,
                            const char* artist,
                            const char* album,
                            const char* year,
                            const char* genre,
                            const char* lyrics,
                            const unsigned char* cover_data,
                            size_t cover_size,
                            const char* cover_mime_type);
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
                                     size_t cover_size,
                                     const char* cover_mime_type);

ag_cancel_token* ag_cancel_token_create(void);
void ag_cancel_token_cancel(ag_cancel_token* token);
void ag_cancel_token_set_paused(ag_cancel_token* token, int paused);
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

/* Returns 1 when the linked FFmpeg build contains the named encoder. */
int ag_encoder_available(const char* codec_name);

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

/* Extended options for ag_transcode_ex. Set unused fields to 0/NULL. */
typedef struct ag_transcode_options {
    int volume_normalize;    /* 1 = normalize peak to -1 dBFS before encoding */
    int keep_metadata;       /* 1 = copy input container and audio-stream tags */
    int bitrate_mode;        /* 0 = constant bitrate, 1 = variable bitrate */
    int quality;             /* VBR quality 0..100; 75 is the default */
} ag_transcode_options;

/* Transcode with extended options. Behaves like ag_transcode when options is
 * NULL or all fields are zero. */
ag_result ag_transcode_ex(const char* input_path,
                          const char* output_path,
                          const char* codec_name,
                          long long bit_rate,
                          int sample_rate,
                          int channels,
                          const ag_transcode_options* options,
                          const ag_cancel_token* cancel_token,
                          ag_progress_callback progress_callback,
                          void* user_data);

#define AG_TRANSCODE_REQUEST_V2_VERSION 2U

typedef enum ag_metadata_edit_action {
    AG_METADATA_EDIT_KEEP = 0,
    AG_METADATA_EDIT_SET = 1,
    AG_METADATA_EDIT_CLEAR = 2
} ag_metadata_edit_action;

typedef enum ag_metadata_field {
    AG_METADATA_FIELD_TITLE = 0,
    AG_METADATA_FIELD_ARTIST = 1,
    AG_METADATA_FIELD_ALBUM = 2,
    AG_METADATA_FIELD_ALBUM_ARTIST = 3,
    AG_METADATA_FIELD_GENRE = 4,
    AG_METADATA_FIELD_YEAR = 5,
    AG_METADATA_FIELD_DATE = 6,
    AG_METADATA_FIELD_COMPOSER = 7,
    AG_METADATA_FIELD_BPM = 8,
    AG_METADATA_FIELD_CUSTOM_TAG = 9
} ag_metadata_field;

typedef struct ag_metadata_field_edit {
    ag_metadata_field field;
    ag_metadata_edit_action action;
    const char* value; /* Required only for AG_METADATA_EDIT_SET. */
} ag_metadata_field_edit;

typedef enum ag_metadata_cover_action {
    AG_METADATA_COVER_KEEP = 0,
    AG_METADATA_COVER_SET = 1,
    AG_METADATA_COVER_CLEAR = 2
} ag_metadata_cover_action;

/* Versioned conversion request. struct_size and api_version must be set so
 * future fields can be appended without changing the older transcode ABI. */
typedef struct ag_transcode_request_v2 {
    size_t struct_size;
    uint32_t api_version;
    const char* output_path;
    const char* muxer_name;
    const char* codec_name;
    long long bit_rate;
    int sample_rate;
    const char* channel_layout;
    const char* sample_format;
    int audio_stream_index;
    int keep_metadata;
    int keep_cover;
    int bitrate_mode;
    int quality;
    const ag_metadata_field_edit* metadata_fields;
    size_t metadata_field_count;
    ag_metadata_cover_action metadata_cover_action;
    const unsigned char* metadata_cover_data;
    size_t metadata_cover_size;
    const char* metadata_cover_mime_type;
} ag_transcode_request_v2;

ag_result ag_transcode_v2(const char* input_path,
                          const ag_transcode_request_v2* request,
                          const ag_cancel_token* cancel_token,
                          ag_progress_callback progress_callback,
                          void* user_data);

/* Returns a thread-local UTF-8 diagnostic for the most recent failed core
 * operation on this thread; returns an empty string when no detail is known. */
const char* ag_last_error(void);

ag_result ag_waveform_analyze(const char* utf8_path,
                              size_t target_points,
                              const ag_cancel_token* cancel_token,
                              ag_progress_callback progress_callback,
                              void* user_data,
                              ag_waveform** out_waveform);
size_t ag_waveform_count(const ag_waveform* waveform);
float ag_waveform_peak(const ag_waveform* waveform, size_t index);
void ag_waveform_destroy(ag_waveform* waveform);

typedef enum ag_waveform_layer {
    AG_WAVEFORM_LAYER_MIX = 0,
    AG_WAVEFORM_LAYER_BASS = 1,
    AG_WAVEFORM_LAYER_MID = 2,
    AG_WAVEFORM_LAYER_HIGH = 3,
    AG_WAVEFORM_LAYER_PEAK = 4,
    AG_WAVEFORM_LAYER_RMS = 5
} ag_waveform_layer;

typedef enum ag_waveform_aggregation {
    AG_WAVEFORM_AGGREGATION_PEAK = 0,
    AG_WAVEFORM_AGGREGATION_AVERAGE_ABSOLUTE = 1,
    AG_WAVEFORM_AGGREGATION_RMS = 2
} ag_waveform_aggregation;

/* Borrowed immutable arrays, valid only during the callback. Undecoded buckets
 * are zero padded; total_samples is the current estimated full timeline. The
 * final ag_waveform uses the actual decoded sample count. Callback runs on the
 * calling thread and may cancel via cancel_token. */
typedef struct ag_waveform_snapshot {
    const float* mix;
    const float* bass;
    const float* mid;
    const float* high;
    const float* peak;
    const float* rms;
    size_t count;
    uint64_t total_samples;
    int sample_rate;
} ag_waveform_snapshot;
typedef void (*ag_waveform_snapshot_callback)(
    const ag_waveform_snapshot* snapshot, void* user_data);

ag_result ag_waveform_analyze_progressive(
    const char* utf8_path, size_t target_points,
    ag_waveform_aggregation aggregation,
    const ag_cancel_token* cancel_token,
    ag_progress_callback progress_callback, void* user_data,
    ag_waveform_snapshot_callback snapshot_callback, void* snapshot_user_data,
    ag_waveform** out_waveform);

ag_result ag_waveform_analyze_with_aggregation(
    const char* utf8_path,
    size_t target_points,
    ag_waveform_aggregation aggregation,
    const ag_cancel_token* cancel_token,
    ag_progress_callback progress_callback,
    void* user_data,
    ag_waveform** out_waveform);

/* Frequency-color entry point using average-absolute bucket aggregation. */
ag_result ag_track_frequency_color_analysis(
    const char* utf8_path,
    size_t target_points,
    const ag_cancel_token* cancel_token,
    ag_progress_callback progress_callback,
    void* user_data,
    ag_waveform** out_waveform);

size_t ag_waveform_layer_count(const ag_waveform* waveform,
                               ag_waveform_layer layer);
float ag_waveform_layer_peak(const ag_waveform* waveform,
                             ag_waveform_layer layer,
                             size_t index);
double ag_waveform_bpm(const ag_waveform* waveform);
uint64_t ag_waveform_duration_ms(const ag_waveform* waveform);
uint64_t ag_waveform_total_samples(const ag_waveform* waveform);
int ag_waveform_sample_rate(const ag_waveform* waveform);

/* Analyze the waveform and BPM of an audio file offline.
 * Returns AG_OK on success and fills *out_waveform. *out_bpm is set to the
 * detected BPM when BPM analysis succeeds; when BPM analysis fails for a
 * non-fatal reason (e.g. file too short) the function still returns AG_OK,
 * returns the waveform, and sets *out_bpm to 0.
 * Returns AG_CANCELLED if cancellation is requested during either phase.
 * Returns AG_INVALID_ARGUMENT if utf8_path is NULL/empty, target_points is 0,
 * or out_waveform is NULL. */
ag_result ag_track_analysis(const char* utf8_path,
                            size_t target_points,
                            const ag_cancel_token* cancel_token,
                            ag_progress_callback progress_callback,
                            void* user_data,
                            ag_waveform** out_waveform,
                            double* out_bpm);

/* Variant of ag_track_analysis with selectable bucket aggregation. */
ag_result ag_track_analysis_with_aggregation(
    const char* utf8_path,
    size_t target_points,
    ag_waveform_aggregation aggregation,
    const ag_cancel_token* cancel_token,
    ag_progress_callback progress_callback,
    void* user_data,
    ag_waveform** out_waveform,
    double* out_bpm);

typedef struct ag_bpm_result {
    double bpm;
    double confidence;
} ag_bpm_result;

/* Analyze the BPM of an audio file offline.
 * Returns AG_OK on success and fills `out`. The caller does not free `out`.
 * Returns AG_INVALID_ARGUMENT if file_path or out is NULL.
 * Returns AG_DECODE_ERROR if the file cannot be decoded or is too short. */
ag_result ag_bpm_analyze(const char* file_path, ag_bpm_result* out);

#ifdef __cplusplus
}
#endif
