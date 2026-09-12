#include "decoder.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/dict.h>
#include <libavutil/error.h>
#include <libavutil/mathematics.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

#include <algorithm>
#include <array>
#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <utility>

#include "metadata_writer.hpp"

namespace agplayer {
namespace {

constexpr std::size_t kMaxProbeTagBytes = 4U * 1024U * 1024U;
constexpr std::size_t kMaxProbeCoverBytes = 32U * 1024U * 1024U;
constexpr int kMaxMetadataProbePackets = 32;

bool format_uses_shared_year_date(const AVFormatContext* context) noexcept
{
    const char* name = context != nullptr && context->iformat != nullptr
        ? context->iformat->name : nullptr;
    if (name == nullptr) return false;
    const std::string formats(name);
    return formats.find("mp3") != std::string::npos
        || formats.find("wav") != std::string::npos
        || formats.find("mov") != std::string::npos
        || formats.find("mp4") != std::string::npos
        || formats.find("m4a") != std::string::npos;
}

int first_audio_stream(const AVFormatContext* context) noexcept
{
    if (context == nullptr) return -1;
    for (unsigned int index = 0; index < context->nb_streams; ++index) {
        if (context->streams[index]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

int first_playable_video_stream(const AVFormatContext* context) noexcept
{
    if (context == nullptr) return -1;
    for (unsigned int index = 0; index < context->nb_streams; ++index) {
        const AVStream* const stream = context->streams[index];
        const AVCodecParameters* const parameters = stream->codecpar;
        if (parameters->codec_type == AVMEDIA_TYPE_VIDEO
            && (stream->disposition & AV_DISPOSITION_ATTACHED_PIC) == 0
            && parameters->width > 0 && parameters->height > 0) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

void recover_flac_stream_info(AVCodecParameters* parameters) noexcept
{
    if (parameters == nullptr || parameters->codec_id != AV_CODEC_ID_FLAC
        || parameters->extradata == nullptr || parameters->extradata_size < 34) {
        return;
    }
    const unsigned char* stream_info = parameters->extradata;
    int remaining = parameters->extradata_size;
    if (remaining >= 42 && std::memcmp(stream_info, "fLaC", 4) == 0) {
        stream_info += 8;
        remaining -= 8;
    } else if (remaining >= 38 && (stream_info[0] & 0x7fU) == 0
               && stream_info[1] == 0 && stream_info[2] == 0
               && stream_info[3] == 34) {
        stream_info += 4;
        remaining -= 4;
    }
    if (remaining < 34) return;
    const int sample_rate = (static_cast<int>(stream_info[10]) << 12)
        | (static_cast<int>(stream_info[11]) << 4)
        | (static_cast<int>(stream_info[12]) >> 4);
    const int channels = ((stream_info[12] >> 1) & 0x07) + 1;
    const int bits_per_sample = ((stream_info[12] & 0x01) << 4)
        | (stream_info[13] >> 4);
    if (sample_rate > 0 && parameters->sample_rate <= 0) {
        parameters->sample_rate = sample_rate;
    }
    if (channels > 0 && parameters->ch_layout.nb_channels <= 0) {
        av_channel_layout_default(&parameters->ch_layout, channels);
    }
    if (parameters->bits_per_raw_sample <= 0) {
        parameters->bits_per_raw_sample = bits_per_sample + 1;
    }
}

void recover_adts_stream_info(AVCodecParameters* parameters,
                              const AVPacket* packet) noexcept
{
    static constexpr int sample_rates[] = {
        96'000, 88'200, 64'000, 48'000, 44'100, 32'000, 24'000,
        22'050, 16'000, 12'000, 11'025, 8'000, 7'350,
    };
    if (parameters == nullptr || packet == nullptr || packet->data == nullptr
        || packet->size < 7 || parameters->codec_id != AV_CODEC_ID_AAC
        || packet->data[0] != 0xff || (packet->data[1] & 0xf6U) != 0xf0U) {
        return;
    }
    const int rate_index = (packet->data[2] >> 2) & 0x0f;
    const int channels = ((packet->data[2] & 0x01) << 2)
        | ((packet->data[3] >> 6) & 0x03);
    if (rate_index < static_cast<int>(std::size(sample_rates))
        && parameters->sample_rate <= 0) {
        parameters->sample_rate = sample_rates[rate_index];
    }
    if (channels > 0 && parameters->ch_layout.nb_channels <= 0) {
        av_channel_layout_default(&parameters->ch_layout, channels);
    }
}

int discover_audio_stream_without_codec(AVFormatContext* context,
                                        std::int64_t& observed_duration_ms) noexcept
{
    observed_duration_ms = 0;
    int audio_index = first_audio_stream(context);
    if (audio_index >= 0) {
        recover_flac_stream_info(context->streams[audio_index]->codecpar);
    }
    const auto ready = [context](const int index) noexcept {
        if (index < 0) return false;
        const AVCodecParameters* parameters = context->streams[index]->codecpar;
        return parameters->codec_id != AV_CODEC_ID_NONE
            && parameters->sample_rate > 0
            && parameters->ch_layout.nb_channels > 0;
    };
    const auto duration_ready = [context](const int index) noexcept {
        if (index >= 0) {
            const AVStream* stream = context->streams[index];
            if (stream->duration > 0 && stream->duration != AV_NOPTS_VALUE) {
                return true;
            }
        }
        return context->duration > 0 && context->duration != AV_NOPTS_VALUE;
    };
    if (ready(audio_index) && duration_ready(audio_index)) return audio_index;
    AVPacket* packet = av_packet_alloc();
    if (packet == nullptr) return AVERROR(ENOMEM);
    int read_result = 0;
    std::int64_t accumulated_duration_ms = 0;
    std::int64_t observed_packet_bytes = 0;
    for (int count = 0; count < kMaxMetadataProbePackets
         && !(ready(audio_index) && duration_ready(audio_index));
         ++count) {
        read_result = av_read_frame(context, packet);
        if (read_result < 0) break;
        if (audio_index < 0) audio_index = first_audio_stream(context);
        if (audio_index >= 0) {
            AVStream* stream = context->streams[audio_index];
            recover_flac_stream_info(stream->codecpar);
            if (packet->stream_index == audio_index) {
                recover_adts_stream_info(stream->codecpar, packet);
                std::int64_t packet_duration_ms = packet->duration > 0
                    ? av_rescale_q(packet->duration, stream->time_base,
                                   AVRational{1, 1'000})
                    : 0;
                if (packet_duration_ms <= 0
                    && stream->codecpar->codec_id == AV_CODEC_ID_AAC
                    && stream->codecpar->sample_rate > 0) {
                    packet_duration_ms = std::max<std::int64_t>(
                        1, av_rescale(1'024, 1'000,
                                     stream->codecpar->sample_rate));
                }
                accumulated_duration_ms += packet_duration_ms;
                observed_packet_bytes += packet->size;
                const std::int64_t timestamp = packet->pts != AV_NOPTS_VALUE
                    ? packet->pts : packet->dts;
                if (timestamp != AV_NOPTS_VALUE) {
                    const std::int64_t start = stream->start_time != AV_NOPTS_VALUE
                        ? stream->start_time : 0;
                    observed_duration_ms = std::max(
                        observed_duration_ms,
                        av_rescale_q(timestamp - start + packet->duration,
                                     stream->time_base, AVRational{1, 1'000}));
                } else {
                    observed_duration_ms = std::max(observed_duration_ms,
                                                    accumulated_duration_ms);
                }
            }
        }
        av_packet_unref(packet);
    }
    av_packet_free(&packet);
    if (accumulated_duration_ms > 0) {
        observed_duration_ms = std::max(observed_duration_ms,
                                        accumulated_duration_ms);
        const std::int64_t file_size = context->pb != nullptr
            ? avio_size(context->pb) : 0;
        if (file_size > observed_packet_bytes && observed_packet_bytes > 0) {
            observed_duration_ms = std::max(
                observed_duration_ms,
                av_rescale(file_size, accumulated_duration_ms,
                           observed_packet_bytes));
        }
    }
    return audio_index;
}

ag_result map_open_error(const int error) noexcept
{
    if (error == AVERROR(ENOMEM)) {
        return AG_INTERNAL_ERROR;
    }
    if (error == AVERROR_INVALIDDATA || error == AVERROR_DEMUXER_NOT_FOUND
        || error == AVERROR_PROTOCOL_NOT_FOUND) {
        return AG_UNSUPPORTED_FORMAT;
    }
    return AG_IO_ERROR;
}

std::string read_tag(AVDictionary* preferred,
                     AVDictionary* fallback,
                     const char* key)
{
    const AVDictionaryEntry* entry = av_dict_get(preferred, key, nullptr, 0);
    if (entry == nullptr) {
        entry = av_dict_get(fallback, key, nullptr, 0);
    }
    return entry != nullptr && entry->value != nullptr ? entry->value : "";
}

std::string read_lyrics(AVDictionary* preferred, AVDictionary* fallback)
{
    static constexpr const char* keys[] = {"lyrics", "LYRICS", "USLT"};
    for (const char* key : keys) {
        const std::string value = read_tag(preferred, fallback, key);
        if (!value.empty()) {
            return value;
        }
    }
    return "";
}

std::string read_tag_limited(AVDictionary* preferred,
                             AVDictionary* fallback,
                             const char* key,
                             std::size_t& remaining,
                             bool& limit_exceeded)
{
    const AVDictionaryEntry* entry = av_dict_get(preferred, key, nullptr, 0);
    if (entry == nullptr) {
        entry = av_dict_get(fallback, key, nullptr, 0);
    }
    if (entry == nullptr || entry->value == nullptr) return {};
    std::size_t length = 0;
    while (length <= remaining && entry->value[length] != '\0') ++length;
    if (length > remaining) {
        limit_exceeded = true;
        return {};
    }
    remaining -= length;
    return std::string(entry->value, length);
}

std::string read_lyrics_limited(AVDictionary* preferred,
                                AVDictionary* fallback,
                                std::size_t& remaining,
                                bool& limit_exceeded)
{
    static constexpr const char* keys[] = {"lyrics", "LYRICS", "USLT"};
    for (const char* key : keys) {
        const std::string value = read_tag_limited(
            preferred, fallback, key, remaining, limit_exceeded);
        if (limit_exceeded || !value.empty()) return value;
    }
    return {};
}

const char* image_mime_type(const AVCodecID codec_id) noexcept
{
    switch (codec_id) {
    case AV_CODEC_ID_MJPEG:
        return "image/jpeg";
    case AV_CODEC_ID_PNG:
        return "image/png";
    case AV_CODEC_ID_WEBP:
        return "image/webp";
    default:
        return "application/octet-stream";
    }
}

} // namespace

ag_result probe_media_metadata(const std::string& utf8_path,
                               MediaMetadata& metadata,
                               const MediaMetadataProbeTestHooks* test_hooks) noexcept
{
    metadata = {};
    AVFormatContext* context = nullptr;
    try {
        if (test_hooks != nullptr && test_hooks->throw_allocation_failure) {
            throw std::bad_alloc{};
        }
        if (utf8_path.empty()) return AG_INVALID_ARGUMENT;
        const int open_result = avformat_open_input(&context, utf8_path.c_str(),
                                                    nullptr, nullptr);
        if (open_result < 0) return map_open_error(open_result);
        const auto finish = [&context](const ag_result result) noexcept {
            avformat_close_input(&context);
            return result;
        };
        if (avformat_find_stream_info(context, nullptr) < 0) {
            return finish(AG_UNSUPPORTED_FORMAT);
        }

        int audio_index = -1;
        const AVStream* video_stream = nullptr;
        for (unsigned int index = 0; index < context->nb_streams; ++index) {
            const AVStream* const stream = context->streams[index];
            if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
                metadata.has_audio = true;
                if (audio_index < 0) {
                    audio_index = static_cast<int>(index);
                }
            } else if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO
                       && (stream->disposition & AV_DISPOSITION_ATTACHED_PIC) == 0) {
                metadata.has_video = true;
                if (video_stream == nullptr) {
                    video_stream = stream;
                    metadata.video_width = stream->codecpar->width;
                    metadata.video_height = stream->codecpar->height;
                }
            }
        }
        if (!metadata.has_audio && !metadata.has_video) {
            return finish(AG_UNSUPPORTED_FORMAT);
        }

        std::int64_t observed_duration_ms = 0;
        if (audio_index >= 0) {
            const int discovered_audio_index = discover_audio_stream_without_codec(
                context, observed_duration_ms);
            if (discovered_audio_index >= 0) {
                audio_index = discovered_audio_index;
            }
        }
        const AVStream* const audio_stream = audio_index >= 0
            ? context->streams[audio_index] : nullptr;
        const AVStream* const metadata_stream = audio_stream != nullptr
            ? audio_stream : video_stream;
        const AVCodecParameters* const parameters = audio_stream != nullptr
            ? audio_stream->codecpar : nullptr;
        std::size_t tag_budget = kMaxProbeTagBytes;
        bool tag_limit_exceeded = false;
        const auto read_limited = [metadata_stream, context, &tag_budget,
                                   &tag_limit_exceeded](const char* key) {
            return read_tag_limited(metadata_stream->metadata, context->metadata,
                                    key, tag_budget, tag_limit_exceeded);
        };
        const auto read_canonical = [&read_limited, &tag_limit_exceeded](
                                        const CanonicalField field) {
            for (const char* alias : known_metadata_aliases(field)) {
                const std::string value = read_limited(alias);
                if (tag_limit_exceeded || !value.empty()) return value;
            }
            return std::string{};
        };
        metadata.title = read_canonical(CanonicalField::Title);
        metadata.artist = read_canonical(CanonicalField::Artist);
        metadata.album = read_canonical(CanonicalField::Album);
        metadata.album_artist = read_canonical(CanonicalField::AlbumArtist);
        metadata.track = read_limited("track");
        metadata.disc = read_limited("disc");
        metadata.composer = read_canonical(CanonicalField::Composer);
        metadata.comment = read_limited("comment");
        metadata.bpm = read_canonical(CanonicalField::Bpm);
        metadata.custom_tag = read_canonical(CanonicalField::CustomTag);
        metadata.copyright = read_limited("copyright");
        metadata.encoder = read_limited("encoded_by");
        if (metadata.encoder.empty() && !tag_limit_exceeded) {
            metadata.encoder = read_limited("encoder");
        }
        metadata.year = read_canonical(CanonicalField::Year);
        metadata.date = read_canonical(CanonicalField::Date);
        if (format_uses_shared_year_date(context)) {
            const std::string shared = !metadata.date.empty()
                ? metadata.date : metadata.year;
            metadata.year = shared;
            metadata.date = shared;
        }
        metadata.genre = read_canonical(CanonicalField::Genre);
        metadata.lyrics = read_lyrics_limited(metadata_stream->metadata,
                                              context->metadata, tag_budget,
                                              tag_limit_exceeded);
        if (tag_limit_exceeded) return finish(AG_UNSUPPORTED_FORMAT);
        metadata.format = context->iformat != nullptr
            && context->iformat->name != nullptr ? context->iformat->name : "";
        if (parameters != nullptr) {
            metadata.sample_rate = parameters->sample_rate;
            metadata.channels = parameters->ch_layout.nb_channels;
            metadata.bits_per_sample = parameters->bits_per_raw_sample > 0
                ? parameters->bits_per_raw_sample : parameters->bits_per_coded_sample;
            metadata.bit_rate = parameters->bit_rate > 0
                ? parameters->bit_rate : context->bit_rate;
        }
        const AVStream* const duration_stream = audio_stream != nullptr
            ? audio_stream : video_stream;
        if (duration_stream != nullptr && duration_stream->duration > 0
            && duration_stream->duration != AV_NOPTS_VALUE) {
            metadata.duration_ms = av_rescale_q(duration_stream->duration,
                duration_stream->time_base, AVRational{1, 1'000});
        } else if (context->duration > 0
                   && context->duration != AV_NOPTS_VALUE) {
            metadata.duration_ms = av_rescale_q(context->duration,
                AVRational{1, AV_TIME_BASE}, AVRational{1, 1'000});
        } else {
            metadata.duration_ms = observed_duration_ms;
        }
        for (unsigned int index = 0; index < context->nb_streams; ++index) {
            const AVStream* stream = context->streams[index];
            if ((stream->disposition & AV_DISPOSITION_ATTACHED_PIC) == 0
                || stream->attached_pic.data == nullptr
                || stream->attached_pic.size <= 0) {
                continue;
            }
            if (static_cast<std::size_t>(stream->attached_pic.size)
                > kMaxProbeCoverBytes) {
                return finish(AG_UNSUPPORTED_FORMAT);
            }
            metadata.cover.assign(
                stream->attached_pic.data,
                stream->attached_pic.data + stream->attached_pic.size);
            metadata.cover_mime_type = image_mime_type(stream->codecpar->codec_id);
            break;
        }
        return finish(AG_OK);
    } catch (...) {
        avformat_close_input(&context);
        metadata = {};
        return AG_INTERNAL_ERROR;
    }
}

class Decoder::Impl final {
public:
    ~Impl()
    {
        reset();
    }

    ag_result open(const std::string& utf8_path,
                   const DecoderOpenOptions& options)
    {
        reset();
        if (utf8_path.empty()
            || (options.custom_read != nullptr
                && options.custom_io_context == nullptr)) {
            return AG_INVALID_ARGUMENT;
        }

        interrupt_handler_ = options.interrupt_callback;
        interrupt_context_ = options.interrupt_context;
        custom_read_ = options.custom_read;
        custom_seek_ = options.custom_seek;
        custom_io_context_ = options.custom_io_context;
        packet_callback_ = options.packet_callback;
        packet_context_ = options.packet_context;
        interrupt_triggered_ = false;
        if (interrupt_handler_ != nullptr || custom_read_ != nullptr) {
            format_context_ = avformat_alloc_context();
            if (format_context_ == nullptr) {
                reset();
                return AG_INTERNAL_ERROR;
            }
        }
        if (interrupt_handler_ != nullptr) {
            format_context_->interrupt_callback.callback =
                &Impl::ffmpeg_interrupt_callback;
            format_context_->interrupt_callback.opaque = this;
        }

        const AVInputFormat* input_format = nullptr;
        if (!options.input_format_hint.empty()) {
            input_format = av_find_input_format(options.input_format_hint.c_str());
            // FFmpeg exposes DSDIFF/DSDIFF as the generic IFF demuxer. Keep
            // the analysis-facing hint explicit while adapting it here.
            if (input_format == nullptr
                && options.input_format_hint == "dsdiff") {
                input_format = av_find_input_format("iff");
            }
            if (input_format == nullptr) {
                reset();
                return AG_UNSUPPORTED_FORMAT;
            }
        }
        if (custom_read_ != nullptr) {
            constexpr int custom_io_buffer_size = 64 * 1024;
            auto* const buffer = static_cast<unsigned char*>(
                av_malloc(custom_io_buffer_size));
            if (buffer == nullptr) {
                reset();
                return AG_INTERNAL_ERROR;
            }
            custom_io_ = avio_alloc_context(
                buffer, custom_io_buffer_size, 0, this,
                &Impl::ffmpeg_read_callback, nullptr,
                custom_seek_ != nullptr ? &Impl::ffmpeg_seek_callback : nullptr);
            if (custom_io_ == nullptr) {
                av_free(buffer);
                reset();
                return AG_INTERNAL_ERROR;
            }
            format_context_->pb = custom_io_;
            format_context_->flags |= AVFMT_FLAG_CUSTOM_IO;
        }

        int result = avformat_open_input(&format_context_, utf8_path.c_str(),
                                         input_format, nullptr);
        if (result < 0) {
            const bool interrupted = interrupt_triggered_;
            reset();
            return interrupted ? AG_CANCELLED : map_open_error(result);
        }
        result = avformat_find_stream_info(format_context_, nullptr);
        if (result < 0) {
            const bool interrupted = interrupt_triggered_;
            reset();
            return interrupted ? AG_CANCELLED : AG_UNSUPPORTED_FORMAT;
        }

        const AVCodec* codec = nullptr;
        audio_stream_index_ = av_find_best_stream(
            format_context_, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
        if (audio_stream_index_ < 0 || codec == nullptr) {
            // A present-but-undecodable audio stream is never a reason to
            // replace real media with silence. This path is exclusively for
            // containers with no audio stream at all.
            if (first_audio_stream(format_context_) >= 0
                || !options.allow_silent_video_clock) {
                reset();
                return AG_UNSUPPORTED_FORMAT;
            }
            const int video_stream_index = first_playable_video_stream(format_context_);
            if (video_stream_index < 0
                || !initialize_silent_video_clock(
                    *format_context_->streams[video_stream_index])) {
                reset();
                return AG_UNSUPPORTED_FORMAT;
            }
            return AG_OK;
        }

        codec_context_ = avcodec_alloc_context3(codec);
        if (codec_context_ == nullptr) {
            reset();
            return AG_INTERNAL_ERROR;
        }
        AVStream* const stream = format_context_->streams[audio_stream_index_];
        result = avcodec_parameters_to_context(codec_context_, stream->codecpar);
        if (result < 0 || avcodec_open2(codec_context_, codec, nullptr) < 0) {
            reset();
            return AG_DECODE_ERROR;
        }
        if (codec_context_->sample_rate <= 0
            || codec_context_->ch_layout.nb_channels <= 0) {
            reset();
            return AG_UNSUPPORTED_FORMAT;
        }

        output_sample_rate_ = options.output_sample_rate > 0
                                  ? options.output_sample_rate
                                  : codec_context_->sample_rate;
        output_channels_ = options.output_channels > 0
                               ? options.output_channels
                               : codec_context_->ch_layout.nb_channels;

        result = initialize_resampler();
        if (result < 0) {
            reset();
            return AG_DECODE_ERROR;
        }
        output_format_.sample_rate = output_sample_rate_;
        output_format_.channels = output_channels_;

        packet_ = av_packet_alloc();
        frame_ = av_frame_alloc();
        if (packet_ == nullptr || frame_ == nullptr) {
            reset();
            return AG_INTERNAL_ERROR;
        }

        populate_metadata(*stream);
        populate_native_format(*stream);
        return AG_OK;
    }

    [[nodiscard]] bool is_open() const noexcept
    {
        if (silent_video_clock_) return format_context_ != nullptr;
        return format_context_ != nullptr && codec_context_ != nullptr
               && swr_context_ != nullptr && packet_ != nullptr
               && frame_ != nullptr;
    }

    ag_result read(DecodedAudioBlock& block)
    {
        block.samples.clear();
        block.frames = 0U;
        block.timestamp_frame = 0;
        block.timestamp_ms = 0;
        block.end_of_stream = false;
        if (silent_video_clock_) {
            return read_silent_video_clock(block);
        }
        if (codec_context_ == nullptr || frame_ == nullptr || packet_ == nullptr) {
            return AG_INVALID_ARGUMENT;
        }

        for (;;) {
            const int receive_result = avcodec_receive_frame(codec_context_, frame_);
            if (receive_result == 0) {
                const ag_result convert_result = convert_frame(block);
                av_frame_unref(frame_);
                if (convert_result != AG_OK) {
                    return convert_result;
                }
                if (!trim_to_seek_target(block)) {
                    continue;
                }
                return AG_OK;
            }
            if (receive_result == AVERROR_EOF) {
                if (!resampler_drained_) {
                    const ag_result drain_result = drain_resampler(block);
                    if (drain_result != AG_OK) {
                        return drain_result;
                    }
                    if (block.frames > 0U) {
                        return AG_OK;
                    }
                    resampler_drained_ = true;
                }
                block.end_of_stream = true;
                return AG_OK;
            }
            if (receive_result != AVERROR(EAGAIN)) {
                return AG_DECODE_ERROR;
            }

            if (input_eof_) {
                if (drain_sent_) {
                    block.end_of_stream = true;
                    return AG_OK;
                }
                const int send_result = avcodec_send_packet(codec_context_, nullptr);
                if (send_result < 0 && send_result != AVERROR_EOF) {
                    return AG_DECODE_ERROR;
                }
                drain_sent_ = true;
                continue;
            }

            int read_result = 0;
            do {
                av_packet_unref(packet_);
                read_result = av_read_frame(format_context_, packet_);
                if (read_result == AVERROR_EOF) {
                    input_eof_ = true;
                    break;
                }
                if (read_result < 0) {
                    return AG_DECODE_ERROR;
                }
            } while (packet_->stream_index != audio_stream_index_);

            if (input_eof_) {
                continue;
            }

            notify_packet();
            const int send_result = avcodec_send_packet(codec_context_, packet_);
            av_packet_unref(packet_);
            if (send_result < 0) {
                return AG_DECODE_ERROR;
            }
        }
    }

    ag_result read_analysis(DecodedAnalysisBlock& block)
    {
        block = {};
        if (silent_video_clock_ || codec_context_ == nullptr || frame_ == nullptr
            || packet_ == nullptr) {
            return AG_INVALID_ARGUMENT;
        }

        for (;;) {
            const int receive_result = avcodec_receive_frame(codec_context_, frame_);
            if (receive_result == 0) {
                const ag_result convert_result = convert_analysis_frame(block);
                av_frame_unref(frame_);
                return convert_result;
            }
            if (receive_result == AVERROR_EOF) {
                block.end_of_stream = true;
                return AG_OK;
            }
            if (receive_result != AVERROR(EAGAIN)) {
                return interrupt_triggered_ ? AG_CANCELLED : AG_DECODE_ERROR;
            }

            if (input_eof_) {
                if (drain_sent_) {
                    block.end_of_stream = true;
                    return AG_OK;
                }
                const int send_result = avcodec_send_packet(codec_context_, nullptr);
                if (send_result < 0 && send_result != AVERROR_EOF) {
                    return interrupt_triggered_ ? AG_CANCELLED : AG_DECODE_ERROR;
                }
                drain_sent_ = true;
                continue;
            }

            int read_result = 0;
            do {
                av_packet_unref(packet_);
                read_result = av_read_frame(format_context_, packet_);
                if (read_result == AVERROR_EOF) {
                    input_eof_ = true;
                    break;
                }
                if (read_result < 0) {
                    return interrupt_triggered_ ? AG_CANCELLED : AG_DECODE_ERROR;
                }
            } while (packet_->stream_index != audio_stream_index_);

            if (input_eof_) continue;
            notify_packet();
            const int send_result = avcodec_send_packet(codec_context_, packet_);
            av_packet_unref(packet_);
            if (send_result < 0) {
                return interrupt_triggered_ ? AG_CANCELLED : AG_DECODE_ERROR;
            }
        }
    }

    ag_result seek(const std::int64_t target_ms)
    {
        if (silent_video_clock_) {
            if (target_ms < 0) return AG_INVALID_ARGUMENT;
            const std::int64_t clamped_ms = (std::min)(
                target_ms, silent_duration_ms_);
            const std::int64_t target_frame = av_rescale_rnd(
                clamped_ms, output_sample_rate_, 1'000, AV_ROUND_UP);
            silent_cursor_frame_ = (std::min)(target_frame,
                                               silent_total_frames_);
            return AG_OK;
        }
        if (target_ms < 0 || format_context_ == nullptr || codec_context_ == nullptr
            || swr_context_ == nullptr) {
            return AG_INVALID_ARGUMENT;
        }

        const AVStream* const stream = format_context_->streams[audio_stream_index_];
        const std::int64_t stream_origin =
            stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time;
        const std::int64_t target_timestamp = stream_origin + av_rescale_q(
            target_ms, AVRational{1, 1'000}, stream->time_base);
        const std::int64_t target_frame = av_rescale_rnd(
            target_ms, output_sample_rate_, 1'000, AV_ROUND_UP);
        return seek_to(target_timestamp, target_frame, target_ms);
    }

    ag_result seek_frame(const std::int64_t target_frame)
    {
        if (silent_video_clock_) {
            if (target_frame < 0) return AG_INVALID_ARGUMENT;
            silent_cursor_frame_ = (std::min)(target_frame,
                                               silent_total_frames_);
            return AG_OK;
        }
        if (target_frame < 0 || format_context_ == nullptr
            || codec_context_ == nullptr || swr_context_ == nullptr
            || output_sample_rate_ <= 0) {
            return AG_INVALID_ARGUMENT;
        }
        const AVStream* const stream = format_context_->streams[audio_stream_index_];
        const std::int64_t stream_origin =
            stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time;
        const std::int64_t target_timestamp = stream_origin + av_rescale_q_rnd(
            target_frame, AVRational{1, output_sample_rate_},
            stream->time_base, AV_ROUND_DOWN);
        const std::int64_t target_ms = av_rescale_q(
            target_frame, AVRational{1, output_sample_rate_},
            AVRational{1, 1'000});
        return seek_to(target_timestamp, target_frame, target_ms);
    }

    [[nodiscard]] const MediaMetadata& metadata() const noexcept
    {
        return metadata_;
    }

    [[nodiscard]] const DecodedAudioFormat& output_format() const noexcept
    {
        return output_format_;
    }

    [[nodiscard]] const DecodedNativeFormat& native_format() const noexcept
    {
        return native_format_;
    }

    void clear_interrupt_callback() noexcept
    {
        if (format_context_ != nullptr) {
            format_context_->interrupt_callback = {};
        }
        interrupt_handler_ = nullptr;
        interrupt_context_ = nullptr;
        interrupt_triggered_ = false;
    }

    void reset() noexcept
    {
        swr_free(&swr_context_);
        av_channel_layout_uninit(&input_layout_);
        av_channel_layout_uninit(&output_layout_);
        av_frame_free(&frame_);
        av_packet_free(&packet_);
        avcodec_free_context(&codec_context_);
        if (format_context_ != nullptr && custom_io_ != nullptr) {
            format_context_->pb = nullptr;
        }
        avformat_close_input(&format_context_);
        avio_context_free(&custom_io_);
        interrupt_handler_ = nullptr;
        interrupt_context_ = nullptr;
        custom_read_ = nullptr;
        custom_seek_ = nullptr;
        custom_io_context_ = nullptr;
        packet_callback_ = nullptr;
        packet_context_ = nullptr;
        interrupt_triggered_ = false;
        audio_stream_index_ = -1;
        output_sample_rate_ = 0;
        output_channels_ = 0;
        input_eof_ = false;
        drain_sent_ = false;
        resampler_drained_ = false;
        seek_target_ms_ = -1;
        seek_target_frame_ = -1;
        block_start_frame_ = 0;
        fallback_frame_ = 0;
        fallback_frame_valid_ = true;
        silent_video_clock_ = false;
        silent_duration_ms_ = 0;
        silent_total_frames_ = 0;
        silent_cursor_frame_ = 0;
        metadata_ = {};
        output_format_ = {};
        native_format_ = {};
    }

private:
    static int ffmpeg_interrupt_callback(void* const opaque) noexcept
    {
        auto* const self = static_cast<Impl*>(opaque);
        if (self == nullptr || self->interrupt_handler_ == nullptr) return 0;
        if (!self->interrupt_handler_(self->interrupt_context_)) return 0;
        self->interrupt_triggered_ = true;
        return 1;
    }

    static int ffmpeg_read_callback(void* const opaque,
                                    std::uint8_t* const buffer,
                                    const int size) noexcept
    {
        auto* const self = static_cast<Impl*>(opaque);
        if (self == nullptr || self->custom_read_ == nullptr
            || buffer == nullptr || size <= 0) {
            return AVERROR(EINVAL);
        }
        const int result = self->custom_read_(self->custom_io_context_, buffer,
                                              size);
        if (result > 0) return result;
        if (result == 0) return AVERROR_EOF;
        return result;
    }

    static std::int64_t ffmpeg_seek_callback(void* const opaque,
                                             const std::int64_t offset,
                                             const int whence) noexcept
    {
        auto* const self = static_cast<Impl*>(opaque);
        if (self == nullptr || self->custom_seek_ == nullptr) {
            return AVERROR(ENOSYS);
        }
        const std::int64_t result = self->custom_seek_(
            self->custom_io_context_, offset, whence);
        return result;
    }

    void notify_packet() const noexcept
    {
        if (packet_callback_ != nullptr && packet_ != nullptr
            && packet_->data != nullptr && packet_->size > 0) {
            packet_callback_(packet_context_, packet_->data, packet_->size);
        }
    }

    ag_result seek_to(const std::int64_t target_timestamp,
                      const std::int64_t target_frame,
                      const std::int64_t target_ms)
    {
        interrupt_triggered_ = false;
        const int result = avformat_seek_file(format_context_,
                                              audio_stream_index_,
                                              std::numeric_limits<std::int64_t>::min(),
                                              target_timestamp,
                                              target_timestamp,
                                              AVSEEK_FLAG_BACKWARD);
        if (result < 0) {
            return interrupt_triggered_ ? AG_CANCELLED : AG_DECODE_ERROR;
        }

        avcodec_flush_buffers(codec_context_);
        av_packet_unref(packet_);
        av_frame_unref(frame_);
        swr_close(swr_context_);
        if (swr_init(swr_context_) < 0) return AG_DECODE_ERROR;

        input_eof_ = false;
        drain_sent_ = false;
        resampler_drained_ = false;
        seek_target_ms_ = target_ms;
        seek_target_frame_ = target_frame;
        fallback_frame_valid_ = false;
        return AG_OK;
    }

    int initialize_resampler()
    {
        av_channel_layout_uninit(&input_layout_);
        int result = 0;
        if (codec_context_->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC) {
            av_channel_layout_default(
                &input_layout_, codec_context_->ch_layout.nb_channels);
        } else {
            result = av_channel_layout_copy(&input_layout_,
                                            &codec_context_->ch_layout);
            if (result < 0) {
                return result;
            }
        }

        av_channel_layout_uninit(&output_layout_);
        av_channel_layout_default(&output_layout_, output_channels_);

        result = swr_alloc_set_opts2(&swr_context_,
                                     &output_layout_,
                                     AV_SAMPLE_FMT_FLT,
                                     output_sample_rate_,
                                     &input_layout_,
                                     codec_context_->sample_fmt,
                                     codec_context_->sample_rate,
                                     0,
                                     nullptr);
        if (result < 0) {
            return result;
        }
        return swr_init(swr_context_);
    }

    [[nodiscard]] bool initialize_silent_video_clock(const AVStream& video_stream)
    {
        constexpr int silent_sample_rate = 48'000;
        constexpr int silent_channels = 2;
        std::int64_t duration_ms = 0;
        if (video_stream.duration > 0
            && video_stream.duration != AV_NOPTS_VALUE) {
            duration_ms = av_rescale_q(video_stream.duration,
                                       video_stream.time_base,
                                       AVRational{1, 1'000});
        } else if (format_context_->duration > 0
                   && format_context_->duration != AV_NOPTS_VALUE) {
            duration_ms = av_rescale_q(format_context_->duration,
                                       AVRational{1, AV_TIME_BASE},
                                       AVRational{1, 1'000});
        }
        if (duration_ms <= 0) return false;

        const std::int64_t total_frames = av_rescale_rnd(
            duration_ms, silent_sample_rate, 1'000, AV_ROUND_UP);
        if (total_frames <= 0
            || total_frames == (std::numeric_limits<std::int64_t>::max)()) {
            return false;
        }

        output_sample_rate_ = silent_sample_rate;
        output_channels_ = silent_channels;
        output_format_.sample_rate = silent_sample_rate;
        output_format_.channels = silent_channels;
        silent_video_clock_ = true;
        silent_duration_ms_ = duration_ms;
        silent_total_frames_ = total_frames;
        silent_cursor_frame_ = 0;
        metadata_.format = format_context_->iformat != nullptr
                               && format_context_->iformat->name != nullptr
                           ? format_context_->iformat->name
                           : "";
        metadata_.sample_rate = silent_sample_rate;
        metadata_.channels = silent_channels;
        metadata_.duration_ms = duration_ms;
        metadata_.has_video = true;
        metadata_.video_width = video_stream.codecpar->width;
        metadata_.video_height = video_stream.codecpar->height;
        metadata_.bit_rate = format_context_->bit_rate;
        return true;
    }

    ag_result read_silent_video_clock(DecodedAudioBlock& block) noexcept
    {
        constexpr std::size_t silent_block_frames = 1'024U;
        if (silent_cursor_frame_ >= silent_total_frames_) {
            block.timestamp_frame = silent_total_frames_;
            block.timestamp_ms = av_rescale_q(
                silent_total_frames_, AVRational{1, output_sample_rate_},
                AVRational{1, 1'000});
            block.end_of_stream = true;
            return AG_OK;
        }

        const std::int64_t remaining = silent_total_frames_ - silent_cursor_frame_;
        const std::size_t frames = static_cast<std::size_t>((std::min)(
            remaining, static_cast<std::int64_t>(silent_block_frames)));
        block.frames = frames;
        block.timestamp_frame = silent_cursor_frame_;
        block.timestamp_ms = av_rescale_q(
            silent_cursor_frame_, AVRational{1, output_sample_rate_},
            AVRational{1, 1'000});
        block.samples.assign(frames * static_cast<std::size_t>(output_channels_),
                             0.0F);
        silent_cursor_frame_ += static_cast<std::int64_t>(frames);
        block.end_of_stream = silent_cursor_frame_ == silent_total_frames_;
        return AG_OK;
    }

    void populate_metadata(const AVStream& audio_stream)
    {
        const AVCodecParameters* const parameters = audio_stream.codecpar;
        metadata_.title = read_tag(audio_stream.metadata,
                                   format_context_->metadata,
                                   "title");
        metadata_.artist = read_tag(audio_stream.metadata,
                                    format_context_->metadata,
                                    "artist");
        metadata_.album = read_tag(audio_stream.metadata,
                                   format_context_->metadata,
                                   "album");
        metadata_.album_artist = read_tag(audio_stream.metadata,
                                          format_context_->metadata,
                                          "album_artist");
        metadata_.track = read_tag(audio_stream.metadata,
                                   format_context_->metadata,
                                   "track");
        metadata_.disc = read_tag(audio_stream.metadata,
                                  format_context_->metadata,
                                  "disc");
        metadata_.composer = read_tag(audio_stream.metadata,
                                      format_context_->metadata,
                                      "composer");
        metadata_.comment = read_tag(audio_stream.metadata,
                                     format_context_->metadata,
                                     "comment");
        metadata_.bpm = read_tag(audio_stream.metadata,
                                 format_context_->metadata,
                                 "bpm");
        metadata_.custom_tag = read_tag(audio_stream.metadata,
                                        format_context_->metadata,
                                        "AGPLAYER_TAG");
        metadata_.copyright = read_tag(audio_stream.metadata,
                                       format_context_->metadata,
                                       "copyright");
        metadata_.encoder = read_tag(audio_stream.metadata,
                                     format_context_->metadata,
                                     "encoded_by");
        if (metadata_.encoder.empty()) {
            metadata_.encoder = read_tag(audio_stream.metadata,
                                         format_context_->metadata,
                                         "encoder");
        }
        metadata_.year = read_tag(audio_stream.metadata,
                                   format_context_->metadata,
                                   "year");
        metadata_.date = read_tag(audio_stream.metadata,
                                   format_context_->metadata,
                                   "date");
        if (format_uses_shared_year_date(format_context_)) {
            const std::string shared = !metadata_.date.empty()
                ? metadata_.date : metadata_.year;
            metadata_.year = shared;
            metadata_.date = shared;
        }
        metadata_.genre = read_tag(audio_stream.metadata,
                                   format_context_->metadata,
                                   "genre");
        metadata_.lyrics = read_lyrics(audio_stream.metadata,
                                       format_context_->metadata);
        metadata_.format = format_context_->iformat != nullptr
                               && format_context_->iformat->name != nullptr
                           ? format_context_->iformat->name
                           : "";
        metadata_.codec = avcodec_get_name(codec_context_->codec_id);
        metadata_.sample_rate = codec_context_->sample_rate;
        metadata_.channels = codec_context_->ch_layout.nb_channels;
        metadata_.bits_per_sample = parameters->bits_per_raw_sample > 0
                                        ? parameters->bits_per_raw_sample
                                    : parameters->bits_per_coded_sample > 0
                                        ? parameters->bits_per_coded_sample
                                        : 0;
        metadata_.bit_rate = parameters->bit_rate > 0
                                 ? parameters->bit_rate
                                 : format_context_->bit_rate;
        if (audio_stream.duration > 0 && audio_stream.duration != AV_NOPTS_VALUE) {
            metadata_.duration_ms = av_rescale_q(
                audio_stream.duration, audio_stream.time_base, AVRational{1, 1'000});
        } else if (format_context_->duration > 0
                   && format_context_->duration != AV_NOPTS_VALUE) {
            metadata_.duration_ms = av_rescale_q(
                format_context_->duration,
                AVRational{1, AV_TIME_BASE},
                AVRational{1, 1'000});
        }

        for (unsigned int index = 0; index < format_context_->nb_streams; ++index) {
            const AVStream* const stream = format_context_->streams[index];
            if ((stream->disposition & AV_DISPOSITION_ATTACHED_PIC) == 0
                || stream->attached_pic.data == nullptr
                || stream->attached_pic.size <= 0) {
                continue;
            }
            const unsigned char* const begin = stream->attached_pic.data;
            metadata_.cover.assign(begin, begin + stream->attached_pic.size);
            metadata_.cover_mime_type = image_mime_type(stream->codecpar->codec_id);
            break;
        }
    }

    void populate_native_format(const AVStream& audio_stream)
    {
        const AVCodecParameters* const parameters = audio_stream.codecpar;
        const AVSampleFormat packed = av_get_packed_sample_fmt(
            codec_context_->sample_fmt);
        native_format_.sample_rate = codec_context_->sample_rate;
        native_format_.channels = codec_context_->ch_layout.nb_channels;
        std::array<char, 256> channel_layout{};
        if (av_channel_layout_describe(&codec_context_->ch_layout,
                                       channel_layout.data(),
                                       channel_layout.size()) >= 0) {
            native_format_.channel_layout = channel_layout.data();
        }
        native_format_.storage_bits = av_get_bytes_per_sample(packed) * 8;
        native_format_.valid_bits = parameters->bits_per_raw_sample > 0
            ? parameters->bits_per_raw_sample
            : parameters->bits_per_coded_sample > 0
                ? parameters->bits_per_coded_sample
                : native_format_.storage_bits;
        switch (packed) {
        case AV_SAMPLE_FMT_U8:
        case AV_SAMPLE_FMT_S16:
        case AV_SAMPLE_FMT_S32:
        case AV_SAMPLE_FMT_S64:
            native_format_.representation = NativeSampleRepresentation::SignedInteger;
            break;
        case AV_SAMPLE_FMT_FLT:
        case AV_SAMPLE_FMT_DBL:
            native_format_.representation = NativeSampleRepresentation::FloatingPoint;
            break;
        default:
            native_format_.representation = NativeSampleRepresentation::Unknown;
            break;
        }
        const std::string codec_name = avcodec_get_name(codec_context_->codec_id);
        native_format_.is_dsd = codec_name.rfind("dsd_", 0) == 0;
        native_format_.is_dst = codec_name == "dst";
        if (native_format_.is_dsd || native_format_.is_dst) {
            constexpr int dsd_bits_per_decoded_sample = 8;
            if (codec_context_->sample_rate
                <= (std::numeric_limits<int>::max)()
                       / dsd_bits_per_decoded_sample) {
                native_format_.raw_dsd_sample_rate =
                    codec_context_->sample_rate * dsd_bits_per_decoded_sample;
            }
        }
        const AVCodecDescriptor* const descriptor =
            avcodec_descriptor_get(codec_context_->codec_id);
        native_format_.codec_is_lossless = descriptor != nullptr
            && (descriptor->props & AV_CODEC_PROP_LOSSLESS) != 0;
    }

    ag_result convert_analysis_frame(DecodedAnalysisBlock& block)
    {
        const int frame_sample_rate = frame_->sample_rate > 0
            ? frame_->sample_rate : codec_context_->sample_rate;
        if (frame_->format != codec_context_->sample_fmt
            || frame_sample_rate != codec_context_->sample_rate
            || !frame_layout_matches() || frame_->nb_samples < 0) {
            return AG_DECODE_ERROR;
        }
        const std::size_t frames = static_cast<std::size_t>(frame_->nb_samples);
        const std::size_t channels = static_cast<std::size_t>(
            codec_context_->ch_layout.nb_channels);
        if (channels == 0U || frames > (std::numeric_limits<std::size_t>::max)()
                                         / channels) {
            return AG_INTERNAL_ERROR;
        }
        const std::size_t sample_count = frames * channels;
        block.samples.resize(sample_count);
        block.representation = native_format_.representation;
        block.storage_bits = native_format_.storage_bits;
        block.valid_bits = native_format_.valid_bits;
        if (block.representation == NativeSampleRepresentation::SignedInteger) {
            block.integer_samples.resize(sample_count);
        }

        const AVSampleFormat packed = av_get_packed_sample_fmt(
            codec_context_->sample_fmt);
        const int bytes_per_sample = av_get_bytes_per_sample(packed);
        const bool planar = av_sample_fmt_is_planar(codec_context_->sample_fmt) != 0;
        if (bytes_per_sample <= 0 || frame_->extended_data == nullptr) {
            return AG_UNSUPPORTED_FORMAT;
        }
        const auto sample_pointer = [&](const std::size_t frame_index,
                                        const std::size_t channel) {
            const std::size_t plane = planar ? channel : 0U;
            const std::size_t sample_index = planar
                ? frame_index : frame_index * channels + channel;
            return frame_->extended_data[plane]
                + sample_index * static_cast<std::size_t>(bytes_per_sample);
        };
        for (std::size_t frame_index = 0; frame_index < frames; ++frame_index) {
            for (std::size_t channel = 0; channel < channels; ++channel) {
                const std::size_t index = frame_index * channels + channel;
                const std::uint8_t* const source = sample_pointer(frame_index,
                                                                  channel);
                std::int64_t integer = 0;
                double normalized = 0.0;
                switch (packed) {
                case AV_SAMPLE_FMT_U8: {
                    integer = static_cast<std::int64_t>(*source) - 128;
                    normalized = static_cast<double>(integer) / 128.0;
                    break;
                }
                case AV_SAMPLE_FMT_S16: {
                    std::int16_t value = 0;
                    std::memcpy(&value, source, sizeof(value));
                    integer = value;
                    normalized = static_cast<double>(value) / 32'768.0;
                    break;
                }
                case AV_SAMPLE_FMT_S32: {
                    std::int32_t value = 0;
                    std::memcpy(&value, source, sizeof(value));
                    integer = value;
                    normalized = static_cast<double>(value) / 2'147'483'648.0;
                    break;
                }
                case AV_SAMPLE_FMT_S64: {
                    std::int64_t value = 0;
                    std::memcpy(&value, source, sizeof(value));
                    integer = value;
                    normalized = std::ldexp(static_cast<double>(value), -63);
                    break;
                }
                case AV_SAMPLE_FMT_FLT: {
                    float value = 0.0F;
                    std::memcpy(&value, source, sizeof(value));
                    normalized = static_cast<double>(value);
                    break;
                }
                case AV_SAMPLE_FMT_DBL:
                    std::memcpy(&normalized, source, sizeof(normalized));
                    break;
                default:
                    return AG_UNSUPPORTED_FORMAT;
                }
                if (std::isfinite(normalized)) {
                    block.samples[index] = normalized;
                } else {
                    block.samples[index] = 0.0;
                    ++block.invalid_samples;
                }
                if (!block.integer_samples.empty()) {
                    block.integer_samples[index] = integer;
                }
            }
        }

        const AVStream* const stream = format_context_->streams[audio_stream_index_];
        const std::int64_t timestamp = frame_->best_effort_timestamp != AV_NOPTS_VALUE
            ? frame_->best_effort_timestamp
            : frame_->pts != AV_NOPTS_VALUE ? frame_->pts : AV_NOPTS_VALUE;
        if (timestamp != AV_NOPTS_VALUE) {
            const std::int64_t origin = stream->start_time == AV_NOPTS_VALUE
                ? 0 : stream->start_time;
            block.timestamp_frame = av_rescale_q(
                timestamp - origin, stream->time_base,
                AVRational{1, codec_context_->sample_rate});
            fallback_frame_valid_ = true;
        } else if (fallback_frame_valid_) {
            block.timestamp_frame = fallback_frame_;
        } else {
            return AG_DECODE_ERROR;
        }
        block.timestamp_ms = av_rescale_q(
            block.timestamp_frame, AVRational{1, codec_context_->sample_rate},
            AVRational{1, 1'000});
        block.frames = frames;
        fallback_frame_ = block.timestamp_frame
            + static_cast<std::int64_t>(block.frames);
        return AG_OK;
    }

    ag_result convert_frame(DecodedAudioBlock& block)
    {
        const int frame_sample_rate = frame_->sample_rate > 0
                                          ? frame_->sample_rate
                                          : codec_context_->sample_rate;
        if (frame_->format != codec_context_->sample_fmt
            || frame_sample_rate != codec_context_->sample_rate
            || !frame_layout_matches()) {
            return AG_DECODE_ERROR;
        }

        const std::int64_t output_capacity = av_rescale_rnd(
            swr_get_delay(swr_context_, codec_context_->sample_rate)
                + frame_->nb_samples,
            output_sample_rate_,
            codec_context_->sample_rate,
            AV_ROUND_UP);
        if (output_capacity <= 0 || output_capacity > INT_MAX) {
            return AG_DECODE_ERROR;
        }

        const std::size_t channels =
            static_cast<std::size_t>(output_channels_);
        if (static_cast<std::uint64_t>(output_capacity)
            > std::numeric_limits<std::size_t>::max() / channels) {
            return AG_INTERNAL_ERROR;
        }
        block.samples.resize(static_cast<std::size_t>(output_capacity) * channels);
        uint8_t* output_data[] = {
            reinterpret_cast<uint8_t*>(block.samples.data()),
        };
        const auto* input_data =
            reinterpret_cast<const uint8_t* const*>(frame_->extended_data);
        const int converted = swr_convert(swr_context_,
                                          output_data,
                                          static_cast<int>(output_capacity),
                                          input_data,
                                          frame_->nb_samples);
        if (converted < 0) {
            return AG_DECODE_ERROR;
        }

        block.frames = static_cast<std::size_t>(converted);
        block.samples.resize(block.frames * channels);
        const AVStream* const stream = format_context_->streams[audio_stream_index_];
        const std::int64_t timestamp = frame_->best_effort_timestamp != AV_NOPTS_VALUE
                                           ? frame_->best_effort_timestamp
                                       : frame_->pts != AV_NOPTS_VALUE
                                           ? frame_->pts
                                           : AV_NOPTS_VALUE;
        if (timestamp != AV_NOPTS_VALUE) {
            const std::int64_t stream_origin =
                stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time;
            block_start_frame_ = av_rescale_q(timestamp - stream_origin,
                                              stream->time_base,
                                              AVRational{1, output_sample_rate_});
            fallback_frame_valid_ = true;
        } else if (fallback_frame_valid_) {
            block_start_frame_ = fallback_frame_;
        } else {
            return AG_DECODE_ERROR;
        }
        block.timestamp_frame = block_start_frame_;
        block.timestamp_ms = av_rescale_q(block_start_frame_,
                                          AVRational{1, output_sample_rate_},
                                          AVRational{1, 1'000});
        fallback_frame_ = block_start_frame_
                          + static_cast<std::int64_t>(block.frames);
        return AG_OK;
    }

    ag_result drain_resampler(DecodedAudioBlock& block)
    {
        const std::int64_t output_capacity = av_rescale_rnd(
            swr_get_delay(swr_context_, codec_context_->sample_rate),
            output_sample_rate_,
            codec_context_->sample_rate,
            AV_ROUND_UP);
        if (output_capacity <= 0) {
            return AG_OK;
        }
        if (output_capacity > INT_MAX) {
            return AG_DECODE_ERROR;
        }

        const std::size_t channels = static_cast<std::size_t>(output_channels_);
        if (static_cast<std::uint64_t>(output_capacity)
            > std::numeric_limits<std::size_t>::max() / channels) {
            return AG_INTERNAL_ERROR;
        }
        block.samples.resize(static_cast<std::size_t>(output_capacity) * channels);
        uint8_t* output_data[] = {
            reinterpret_cast<uint8_t*>(block.samples.data()),
        };
        const int converted = swr_convert(swr_context_,
                                          output_data,
                                          static_cast<int>(output_capacity),
                                          nullptr,
                                          0);
        if (converted < 0) {
            return AG_DECODE_ERROR;
        }

        block.frames = static_cast<std::size_t>(converted);
        block.samples.resize(block.frames * channels);
        block.timestamp_frame = fallback_frame_;
        block.timestamp_ms = av_rescale_q(fallback_frame_,
                                          AVRational{1, output_sample_rate_},
                                          AVRational{1, 1'000});
        fallback_frame_ += static_cast<std::int64_t>(block.frames);
        return AG_OK;
    }

    bool frame_layout_matches() const noexcept
    {
        if (frame_->ch_layout.nb_channels <= 0) {
            return false;
        }

        AVChannelLayout frame_layout{};
        int result = 0;
        if (frame_->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC) {
            av_channel_layout_default(&frame_layout,
                                      frame_->ch_layout.nb_channels);
        } else {
            result = av_channel_layout_copy(&frame_layout,
                                            &frame_->ch_layout);
        }
        const bool matches = result >= 0
                             && av_channel_layout_compare(&frame_layout,
                                                          &input_layout_) == 0;
        av_channel_layout_uninit(&frame_layout);
        return matches;
    }

    bool trim_to_seek_target(DecodedAudioBlock& block)
    {
        if (seek_target_frame_ < 0) {
            return true;
        }

        const std::int64_t block_end_frame =
            block_start_frame_ + static_cast<std::int64_t>(block.frames);
        if (block_end_frame <= seek_target_frame_) {
            return false;
        }

        if (block_start_frame_ < seek_target_frame_) {
            const std::int64_t frames_to_skip =
                seek_target_frame_ - block_start_frame_;
            if (frames_to_skip >= static_cast<std::int64_t>(block.frames)) {
                return false;
            }

            const std::size_t channels =
                static_cast<std::size_t>(output_channels_);
            const std::size_t samples_to_skip =
                static_cast<std::size_t>(frames_to_skip) * channels;
            block.samples.erase(
                block.samples.begin(),
                block.samples.begin() + static_cast<std::ptrdiff_t>(samples_to_skip));
            block.frames -= static_cast<std::size_t>(frames_to_skip);
            block.timestamp_frame = seek_target_frame_;
            block.timestamp_ms = seek_target_ms_;
            block_start_frame_ = seek_target_frame_;
        }

        seek_target_ms_ = -1;
        seek_target_frame_ = -1;
        return true;
    }

    AVFormatContext* format_context_ = nullptr;
    AVIOContext* custom_io_ = nullptr;
    AVCodecContext* codec_context_ = nullptr;
    SwrContext* swr_context_ = nullptr;
    AVChannelLayout input_layout_{};
    AVChannelLayout output_layout_{};
    AVPacket* packet_ = nullptr;
    AVFrame* frame_ = nullptr;
    int audio_stream_index_ = -1;
    int output_sample_rate_ = 0;
    int output_channels_ = 0;
    bool input_eof_ = false;
    bool drain_sent_ = false;
    bool resampler_drained_ = false;
    std::int64_t seek_target_ms_ = -1;
    std::int64_t seek_target_frame_ = -1;
    std::int64_t block_start_frame_ = 0;
    std::int64_t fallback_frame_ = 0;
    bool fallback_frame_valid_ = true;
    bool silent_video_clock_ = false;
    std::int64_t silent_duration_ms_ = 0;
    std::int64_t silent_total_frames_ = 0;
    std::int64_t silent_cursor_frame_ = 0;
    DecoderInterruptCallback interrupt_handler_ = nullptr;
    void* interrupt_context_ = nullptr;
    DecoderReadCallback custom_read_ = nullptr;
    DecoderSeekCallback custom_seek_ = nullptr;
    void* custom_io_context_ = nullptr;
    DecoderPacketCallback packet_callback_ = nullptr;
    void* packet_context_ = nullptr;
    bool interrupt_triggered_ = false;
    MediaMetadata metadata_;
    DecodedAudioFormat output_format_;
    DecodedNativeFormat native_format_;
};

Decoder::Decoder()
    : impl_(std::make_unique<Impl>())
{
}

Decoder::~Decoder() = default;

ag_result Decoder::open(const std::string& utf8_path) noexcept
{
    return open(utf8_path, DecoderOpenOptions{});
}

ag_result Decoder::open(const std::string& utf8_path,
                        const int output_sample_rate,
                        const int output_channels) noexcept
{
    DecoderOpenOptions options;
    options.output_sample_rate = output_sample_rate;
    options.output_channels = output_channels;
    return open(utf8_path, options);
}

ag_result Decoder::open(const std::string& utf8_path,
                        const DecoderOpenOptions& options) noexcept
{
    try {
        return impl_->open(utf8_path, options);
    } catch (...) {
        impl_->reset();
        return AG_INTERNAL_ERROR;
    }
}

void Decoder::close() noexcept
{
    impl_->reset();
}

bool Decoder::is_open() const noexcept
{
    return impl_->is_open();
}

ag_result Decoder::read(DecodedAudioBlock& block) noexcept
{
    try {
        return impl_->read(block);
    } catch (...) {
        block = {};
        return AG_INTERNAL_ERROR;
    }
}

ag_result Decoder::readAnalysis(DecodedAnalysisBlock& block) noexcept
{
    try {
        return impl_->read_analysis(block);
    } catch (...) {
        block = {};
        return AG_INTERNAL_ERROR;
    }
}

ag_result Decoder::seek(const std::int64_t target_ms) noexcept
{
    return impl_->seek(target_ms);
}

ag_result Decoder::seekFrame(const std::int64_t target_frame) noexcept
{
    return impl_->seek_frame(target_frame);
}

const MediaMetadata& Decoder::metadata() const noexcept
{
    return impl_->metadata();
}

void Decoder::clearInterruptCallback() noexcept
{
    impl_->clear_interrupt_callback();
}

const DecodedAudioFormat& Decoder::output_format() const noexcept
{
    return impl_->output_format();
}

const DecodedNativeFormat& Decoder::native_format() const noexcept
{
    return impl_->native_format();
}

} // namespace agplayer
