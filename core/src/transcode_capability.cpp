#include "transcode_capability.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
}

#include <algorithm>
#include <array>

namespace agplayer {

namespace {

struct FormatDefinition {
    const char* key;
    const char* label;
    const char* muxer;
    const char* codec;
    const char* extension;
    bool lossy;
    bool metadata;
    bool cover;
    bool supports_bitrate_modes;
};

constexpr std::array<FormatDefinition, 8> kFormats{{
    {"mp3", "MP3", "mp3", "libmp3lame", "mp3", true, true, true, true},
    {"flac", "FLAC", "flac", "flac", "flac", false, true, true, false},
    {"wav", "WAV", "wav", "pcm_s16le", "wav", false, true, false, false},
    {"aac", "AAC", "adts", "aac", "aac", true, false, false, true},
    {"opus", "Opus", "ogg", "libopus", "opus", true, true, false, true},
    {"ogg", "OGG", "ogg", "libvorbis", "ogg", true, true, false, true},
    {"alac", "ALAC", "ipod", "alac", "m4a", false, true, true, false},
    {"aiff", "AIFF", "aiff", "pcm_s16be", "aiff", false, false, false, false},
}};

const std::vector<int> kLossySampleRates{0, 44100, 48000};
const std::vector<int> kLosslessSampleRates{
    0, 44100, 48000, 88200, 96000, 176400, 192000};

void apply_recommended_parameters(TranscodeFormatCapability& result)
{
    if (result.key == "mp3") {
        result.sample_rate_choices = kLossySampleRates;
        result.bit_rate_choices = {128000, 192000, 256000, 320000};
        result.default_bit_rate = 320000;
    } else if (result.key == "aac") {
        result.sample_rate_choices = kLossySampleRates;
        result.bit_rate_choices = {96000, 128000, 192000, 256000, 320000};
        result.default_bit_rate = 256000;
    } else if (result.key == "opus") {
        result.sample_rate_choices = {48000};
        result.bit_rate_choices = {64000, 96000, 128000, 160000,
                                   192000, 256000, 320000};
        result.default_bit_rate = 320000;
    } else if (result.key == "ogg") {
        result.sample_rate_choices = kLossySampleRates;
    } else {
        result.sample_rate_choices = kLosslessSampleRates;
    }
}

void apply_friendly_parameters(TranscodeFormatCapability& result)
{
    if (result.key == "ogg") {
        result.parameter_kind = "quality";
        result.quality_choices = {0, 2, 4, 6, 8, 10};
        // Vorbis is quality-based. Q8 is the documented high-quality default,
        // rather than pretending it maps to a 320 kbps CBR setting.
        result.default_quality = 8;
        result.bitrate_modes.clear();
    } else if (result.key == "flac") {
        result.parameter_kind = "compression";
        result.quality_choices = {0, 3, 5, 8};
        result.default_quality = 5;
    } else if (result.lossy) {
        result.parameter_kind = "bitrate";
    } else {
        result.parameter_kind = "none";
    }

    if (result.key == "wav") {
        result.bit_depth_choices = {{"", "Source (Auto)", true},
                                    {"s16", "16-bit PCM", false},
                                    {"s24", "24-bit PCM", false},
                                    {"s32", "32-bit PCM", false},
                                    {"flt", "32-bit Float", false}};
    } else if (result.key == "flac" || result.key == "alac") {
        result.bit_depth_choices = {{"", "Source (Auto)", true},
                                    {"s16", "16-bit", false},
                                    {"s24", "24-bit", false}};
    } else if (result.key == "aiff") {
        result.bit_depth_choices = {{"", "Source (Auto)", true},
                                    {"s16", "16-bit PCM", false},
                                    {"s24", "24-bit PCM", false},
                                    {"s32", "32-bit PCM", false}};
    }
}

template <typename T>
std::vector<T> supported_config(const AVCodec* codec,
                                const AVCodecConfig config)
{
    const void* values = nullptr;
    int count = 0;
    if (avcodec_get_supported_config(nullptr, codec, config, 0, &values,
                                     &count) < 0
        || values == nullptr || count <= 0) {
        return {};
    }
    const auto* typed_values = static_cast<const T*>(values);
    return {typed_values, typed_values + count};
}

std::vector<std::string> sample_format_names(const AVCodec* codec)
{
    std::vector<std::string> result;
    for (const AVSampleFormat format :
         supported_config<AVSampleFormat>(codec,
                                          AV_CODEC_CONFIG_SAMPLE_FORMAT)) {
        const char* name = av_get_sample_fmt_name(format);
        if (name != nullptr) {
            result.emplace_back(name);
        }
    }
    return result;
}

std::vector<std::string> channel_layout_names(const AVCodec* codec)
{
    std::vector<std::string> result;
    for (const AVChannelLayout& layout :
         supported_config<AVChannelLayout>(codec,
                                           AV_CODEC_CONFIG_CHANNEL_LAYOUT)) {
        std::array<char, 128> description{};
        if (av_channel_layout_describe(&layout, description.data(),
                                       description.size()) >= 0
            && description[0] != '\0') {
            result.emplace_back(description.data());
        }
    }
    if (result.empty()) {
        result = {"mono", "stereo"};
    }
    return result;
}

std::vector<int> sample_rates(const AVCodec* codec)
{
    std::vector<int> result = supported_config<int>(
        codec, AV_CODEC_CONFIG_SAMPLE_RATE);
    if (result.empty()) {
        result = {44100, 48000, 96000};
    }
    return result;
}

bool can_open_default(const AVCodec* codec, const AVOutputFormat* muxer)
{
    AVCodecContext* context = avcodec_alloc_context3(codec);
    if (context == nullptr) {
        return false;
    }

    const auto formats = supported_config<AVSampleFormat>(
        codec, AV_CODEC_CONFIG_SAMPLE_FORMAT);
    context->sample_fmt = formats.empty() ? AV_SAMPLE_FMT_FLTP : formats.front();
    const auto rates = sample_rates(codec);
    context->sample_rate = rates.empty() ? 48000 : rates.front();
    av_channel_layout_default(&context->ch_layout, 2);
    context->time_base = AVRational{1, context->sample_rate};
    context->bit_rate = 192000;
    context->thread_count = 1;
    if ((codec->capabilities & AV_CODEC_CAP_EXPERIMENTAL) != 0) {
        context->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;
    }
    if ((muxer->flags & AVFMT_GLOBALHEADER) != 0) {
        context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
    const bool opened = avcodec_open2(context, codec, nullptr) >= 0;
    avcodec_free_context(&context);
    return opened;
}

TranscodeFormatCapability inspect(const FormatDefinition& definition)
{
    TranscodeFormatCapability result;
    result.key = definition.key;
    result.label = definition.label;
    result.muxer_name = definition.muxer;
    result.codec_name = definition.codec;
    result.output_extension = definition.extension;
    result.lossy = definition.lossy;
    result.supports_metadata = definition.metadata;
    result.supports_cover = definition.cover;
    if (definition.supports_bitrate_modes) {
        result.bitrate_modes = {{"cbr", "CBR", true},
                                {"vbr", "VBR", false}};
    }
    apply_friendly_parameters(result);
    apply_recommended_parameters(result);

    const AVCodec* codec = avcodec_find_encoder_by_name(definition.codec);
    if (codec == nullptr) {
        result.unavailable_reason = "Encoder is not included in this FFmpeg build";
        return result;
    }
    const AVOutputFormat* muxer = av_guess_format(definition.muxer, nullptr,
                                                   nullptr);
    if (muxer == nullptr) {
        result.unavailable_reason = "Muxer is not included in this FFmpeg build";
        return result;
    }
    // A negative result means the muxer cannot answer statically. The
    // following real avcodec_open2 probe remains authoritative in that case.
    if (avformat_query_codec(muxer, codec->id, FF_COMPLIANCE_NORMAL) == 0) {
        result.unavailable_reason = "Encoder and muxer are not compatible";
        return result;
    }
    if (!can_open_default(codec, muxer)) {
        result.unavailable_reason = "Encoder rejected its default audio profile";
        return result;
    }

    result.sample_rates = sample_rates(codec);
    result.sample_formats = sample_format_names(codec);
    if (result.sample_formats.empty()) {
        const char* name = av_get_sample_fmt_name(AV_SAMPLE_FMT_FLTP);
        result.sample_formats.emplace_back(name != nullptr ? name : "fltp");
    }
    result.channel_layouts = channel_layout_names(codec);
    result.available = true;
    return result;
}

} // namespace

std::vector<TranscodeFormatCapability> transcode_capabilities()
{
    std::vector<TranscodeFormatCapability> result;
    result.reserve(kFormats.size());
    for (const FormatDefinition& definition : kFormats) {
        result.push_back(inspect(definition));
    }
    return result;
}

const TranscodeFormatCapability* find_transcode_capability(
    const std::vector<TranscodeFormatCapability>& capabilities,
    const std::string_view key)
{
    const auto found = std::find_if(
        capabilities.begin(), capabilities.end(),
        [key](const TranscodeFormatCapability& capability) {
            return capability.key == key;
        });
    return found == capabilities.end() ? nullptr : &*found;
}

} // namespace agplayer
