#include "transcode_probe.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/dict.h>
#include <libavutil/samplefmt.h>
}

#include <array>
#include <utility>

namespace agplayer {

namespace {

std::string dictionary_value(const AVDictionary* dictionary, const char* key)
{
    const AVDictionaryEntry* entry = av_dict_get(dictionary, key, nullptr, 0);
    return entry != nullptr && entry->value != nullptr ? entry->value : "";
}

std::int64_t stream_duration_ms(const AVFormatContext* format,
                                const AVStream* stream)
{
    if (stream->duration != AV_NOPTS_VALUE && stream->duration > 0) {
        return av_rescale_q(stream->duration, stream->time_base,
                            AVRational{1, 1000});
    }
    if (format->duration != AV_NOPTS_VALUE && format->duration > 0) {
        return format->duration / (AV_TIME_BASE / 1000);
    }
    return 0;
}

std::string channel_layout_name(const AVCodecParameters* parameters)
{
    AVChannelLayout layout = parameters->ch_layout;
    AVChannelLayout inferred_layout{};
    if (layout.order == AV_CHANNEL_ORDER_UNSPEC
        && layout.nb_channels > 0) {
        av_channel_layout_default(&inferred_layout, layout.nb_channels);
        layout = inferred_layout;
    }
    std::array<char, 128> description{};
    if (layout.nb_channels > 0
        && av_channel_layout_describe(&layout,
                                      description.data(),
                                      description.size()) >= 0) {
        return description.data();
    }
    return {};
}

} // namespace

ag_result probe_transcode_input(const std::string_view utf8_path,
                                MediaProbe& probe,
                                std::string& error)
{
    probe = {};
    error.clear();
    if (utf8_path.empty()) {
        error = "Input path is empty";
        return AG_INVALID_ARGUMENT;
    }

    const std::string path(utf8_path);
    AVFormatContext* format = nullptr;
    int result = avformat_open_input(&format, path.c_str(), nullptr, nullptr);
    if (result < 0 || format == nullptr) {
        error = "Failed to open input for probing";
        return AG_IO_ERROR;
    }
    result = avformat_find_stream_info(format, nullptr);
    if (result < 0) {
        avformat_close_input(&format);
        error = "Failed to read input stream information";
        return AG_DECODE_ERROR;
    }

    if (format->iformat != nullptr && format->iformat->name != nullptr) {
        probe.container = format->iformat->name;
    }
    probe.audio_streams.reserve(format->nb_streams);
    for (unsigned int index = 0; index < format->nb_streams; ++index) {
        const AVStream* stream = format->streams[index];
        const AVCodecParameters* parameters = stream->codecpar;
        if (parameters->codec_type == AVMEDIA_TYPE_VIDEO) {
            if ((stream->disposition & AV_DISPOSITION_ATTACHED_PIC) != 0) {
                probe.has_cover = true;
            } else {
                probe.is_video = true;
            }
            continue;
        }
        if (parameters->codec_type != AVMEDIA_TYPE_AUDIO) {
            continue;
        }

        AudioStreamProbe audio;
        audio.stream_index = static_cast<int>(index);
        const AVCodecDescriptor* descriptor =
            avcodec_descriptor_get(parameters->codec_id);
        if (descriptor != nullptr && descriptor->name != nullptr) {
            audio.codec = descriptor->name;
        }
        audio.language = dictionary_value(stream->metadata, "language");
        audio.title = dictionary_value(stream->metadata, "title");
        audio.is_default = (stream->disposition & AV_DISPOSITION_DEFAULT) != 0;
        audio.sample_rate = parameters->sample_rate;
        const char* sample_format = av_get_sample_fmt_name(
            static_cast<AVSampleFormat>(parameters->format));
        if (sample_format != nullptr) {
            audio.sample_format = sample_format;
        }
        audio.channel_layout = channel_layout_name(parameters);
        audio.bit_rate = parameters->bit_rate;
        audio.duration_ms = stream_duration_ms(format, stream);
        audio.bits_per_sample = parameters->bits_per_raw_sample > 0
            ? parameters->bits_per_raw_sample
            : parameters->bits_per_coded_sample;
        const bool lossless_codec = descriptor != nullptr
            && (descriptor->props & AV_CODEC_PROP_LOSSLESS) != 0;
        if (audio.bits_per_sample <= 0 && lossless_codec
            && parameters->format >= 0) {
            const int bytes = av_get_bytes_per_sample(
                static_cast<AVSampleFormat>(parameters->format));
            if (bytes > 0) {
                audio.bits_per_sample = bytes * 8;
            }
        }
        probe.audio_streams.push_back(std::move(audio));
    }
    avformat_close_input(&format);

    if (probe.audio_streams.empty()) {
        error = "Input contains no audio stream";
        return AG_UNSUPPORTED_FORMAT;
    }
    return AG_OK;
}

} // namespace agplayer
