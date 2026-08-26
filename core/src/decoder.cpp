#include "decoder.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/dict.h>
#include <libavutil/error.h>
#include <libavutil/mathematics.h>
#include <libswresample/swresample.h>
}

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <limits>
#include <new>
#include <utility>

#include "metadata_writer.hpp"

namespace agplayer {
namespace {

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
                               MediaMetadata& metadata) noexcept
{
    metadata = {};
    if (utf8_path.empty()) return AG_INVALID_ARGUMENT;
    AVFormatContext* context = nullptr;
    const int open_result = avformat_open_input(&context, utf8_path.c_str(),
                                                nullptr, nullptr);
    if (open_result < 0) return map_open_error(open_result);
    const auto close = [&context]() { avformat_close_input(&context); };
    if (avformat_find_stream_info(context, nullptr) < 0) {
        close();
        return AG_UNSUPPORTED_FORMAT;
    }
    const int audio_index = av_find_best_stream(context, AVMEDIA_TYPE_AUDIO,
                                                -1, -1, nullptr, 0);
    if (audio_index < 0) {
        close();
        return AG_UNSUPPORTED_FORMAT;
    }
    const AVStream* audio_stream = context->streams[audio_index];
    const AVCodecParameters* parameters = audio_stream->codecpar;
    const auto read_canonical = [audio_stream, context](
                                    const CanonicalField field) {
        for (const char* alias : known_metadata_aliases(field)) {
            const std::string value = read_tag(audio_stream->metadata,
                                               context->metadata, alias);
            if (!value.empty()) return value;
        }
        return std::string{};
    };
    metadata.title = read_canonical(CanonicalField::Title);
    metadata.artist = read_canonical(CanonicalField::Artist);
    metadata.album = read_canonical(CanonicalField::Album);
    metadata.album_artist = read_canonical(CanonicalField::AlbumArtist);
    metadata.track = read_tag(audio_stream->metadata, context->metadata, "track");
    metadata.disc = read_tag(audio_stream->metadata, context->metadata, "disc");
    metadata.composer = read_canonical(CanonicalField::Composer);
    metadata.comment = read_tag(audio_stream->metadata, context->metadata, "comment");
    metadata.bpm = read_canonical(CanonicalField::Bpm);
    metadata.copyright = read_tag(audio_stream->metadata, context->metadata,
                                  "copyright");
    metadata.encoder = read_tag(audio_stream->metadata, context->metadata,
                                "encoded_by");
    if (metadata.encoder.empty()) {
        metadata.encoder = read_tag(audio_stream->metadata, context->metadata,
                                    "encoder");
    }
    metadata.year = read_canonical(CanonicalField::Year);
    metadata.genre = read_canonical(CanonicalField::Genre);
    metadata.lyrics = read_lyrics(audio_stream->metadata, context->metadata);
    metadata.format = context->iformat != nullptr && context->iformat->name != nullptr
        ? context->iformat->name : "";
    metadata.sample_rate = parameters->sample_rate;
    metadata.channels = parameters->ch_layout.nb_channels;
    metadata.bits_per_sample = parameters->bits_per_raw_sample > 0
        ? parameters->bits_per_raw_sample : parameters->bits_per_coded_sample;
    metadata.bit_rate = parameters->bit_rate > 0
        ? parameters->bit_rate : context->bit_rate;
    if (audio_stream->duration > 0 && audio_stream->duration != AV_NOPTS_VALUE) {
        metadata.duration_ms = av_rescale_q(audio_stream->duration,
            audio_stream->time_base, AVRational{1, 1'000});
    } else if (context->duration > 0 && context->duration != AV_NOPTS_VALUE) {
        metadata.duration_ms = av_rescale_q(context->duration,
            AVRational{1, AV_TIME_BASE}, AVRational{1, 1'000});
    }
    for (unsigned int index = 0; index < context->nb_streams; ++index) {
        const AVStream* stream = context->streams[index];
        if ((stream->disposition & AV_DISPOSITION_ATTACHED_PIC) == 0
            || stream->attached_pic.data == nullptr || stream->attached_pic.size <= 0) {
            continue;
        }
        metadata.cover.assign(stream->attached_pic.data,
                              stream->attached_pic.data + stream->attached_pic.size);
        metadata.cover_mime_type = image_mime_type(stream->codecpar->codec_id);
        break;
    }
    close();
    return AG_OK;
}

class Decoder::Impl final {
public:
    ~Impl()
    {
        reset();
    }

    ag_result open(const std::string& utf8_path,
                   const int output_sample_rate,
                   const int output_channels)
    {
        reset();
        if (utf8_path.empty()) {
            return AG_INVALID_ARGUMENT;
        }

        int result = avformat_open_input(&format_context_, utf8_path.c_str(), nullptr, nullptr);
        if (result < 0) {
            reset();
            return map_open_error(result);
        }
        result = avformat_find_stream_info(format_context_, nullptr);
        if (result < 0) {
            reset();
            return AG_UNSUPPORTED_FORMAT;
        }

        const AVCodec* codec = nullptr;
        audio_stream_index_ = av_find_best_stream(
            format_context_, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
        if (audio_stream_index_ < 0 || codec == nullptr) {
            reset();
            return AG_UNSUPPORTED_FORMAT;
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

        output_sample_rate_ = output_sample_rate > 0
                                  ? output_sample_rate
                                  : codec_context_->sample_rate;
        output_channels_ = output_channels > 0
                               ? output_channels
                               : codec_context_->ch_layout.nb_channels;

        result = initialize_resampler();
        if (result < 0) {
            reset();
            return AG_DECODE_ERROR;
        }

        packet_ = av_packet_alloc();
        frame_ = av_frame_alloc();
        if (packet_ == nullptr || frame_ == nullptr) {
            reset();
            return AG_INTERNAL_ERROR;
        }

        populate_metadata(*stream);
        return AG_OK;
    }

    [[nodiscard]] bool is_open() const noexcept
    {
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

            const int send_result = avcodec_send_packet(codec_context_, packet_);
            av_packet_unref(packet_);
            if (send_result < 0) {
                return AG_DECODE_ERROR;
            }
        }
    }

    ag_result seek(const std::int64_t target_ms)
    {
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

    void reset() noexcept
    {
        swr_free(&swr_context_);
        av_channel_layout_uninit(&input_layout_);
        av_channel_layout_uninit(&output_layout_);
        av_frame_free(&frame_);
        av_packet_free(&packet_);
        avcodec_free_context(&codec_context_);
        avformat_close_input(&format_context_);
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
        metadata_ = {};
    }

private:
    ag_result seek_to(const std::int64_t target_timestamp,
                      const std::int64_t target_frame,
                      const std::int64_t target_ms)
    {
        const int result = avformat_seek_file(format_context_,
                                              audio_stream_index_,
                                              std::numeric_limits<std::int64_t>::min(),
                                              target_timestamp,
                                              target_timestamp,
                                              AVSEEK_FLAG_BACKWARD);
        if (result < 0) return AG_DECODE_ERROR;

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
                                  "date");
        metadata_.genre = read_tag(audio_stream.metadata,
                                   format_context_->metadata,
                                   "genre");
        metadata_.lyrics = read_lyrics(audio_stream.metadata,
                                       format_context_->metadata);
        metadata_.format = format_context_->iformat != nullptr
                               && format_context_->iformat->name != nullptr
                           ? format_context_->iformat->name
                           : "";
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
    MediaMetadata metadata_;
};

Decoder::Decoder()
    : impl_(std::make_unique<Impl>())
{
}

Decoder::~Decoder() = default;

ag_result Decoder::open(const std::string& utf8_path) noexcept
{
    return open(utf8_path, 0, 0);
}

ag_result Decoder::open(const std::string& utf8_path,
                        const int output_sample_rate,
                        const int output_channels) noexcept
{
    try {
        return impl_->open(utf8_path, output_sample_rate, output_channels);
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

} // namespace agplayer
