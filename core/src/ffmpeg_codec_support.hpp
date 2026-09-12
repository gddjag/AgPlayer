#pragma once

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace agplayer {

inline bool codec_supports_sample_format(const AVCodec* codec,
                                         const AVSampleFormat requested)
{
    const void* configs = nullptr;
    int count = 0;
    if (avcodec_get_supported_config(nullptr, codec,
                                     AV_CODEC_CONFIG_SAMPLE_FORMAT, 0,
                                     &configs, &count) < 0
        || configs == nullptr || count <= 0) {
        return true;
    }
    const auto* formats = static_cast<const AVSampleFormat*>(configs);
    for (int index = 0; index < count; ++index) {
        if (formats[index] == requested) {
            return true;
        }
    }
    return false;
}

inline AVSampleFormat pick_supported_sample_format(
    const AVCodec* codec,
    const AVSampleFormat preferred,
    const AVSampleFormat fallback = AV_SAMPLE_FMT_FLTP)
{
    if (codec_supports_sample_format(codec, preferred)) {
        return preferred;
    }
    const void* configs = nullptr;
    int count = 0;
    if (avcodec_get_supported_config(nullptr, codec,
                                     AV_CODEC_CONFIG_SAMPLE_FORMAT, 0,
                                     &configs, &count) >= 0
        && configs != nullptr && count > 0) {
        return static_cast<const AVSampleFormat*>(configs)[0];
    }
    return fallback;
}

} // namespace agplayer
