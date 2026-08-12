#pragma once

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace agplayer {

inline AVSampleFormat pick_supported_sample_format(
    const AVCodec* codec,
    const AVSampleFormat preferred,
    const AVSampleFormat fallback = AV_SAMPLE_FMT_FLTP)
{
    const void* configs = nullptr;
    int count = 0;
    if (avcodec_get_supported_config(nullptr, codec,
                                     AV_CODEC_CONFIG_SAMPLE_FORMAT, 0,
                                     &configs, &count) < 0
        || configs == nullptr || count <= 0) {
        return preferred != AV_SAMPLE_FMT_NONE ? preferred : fallback;
    }

    const auto* formats = static_cast<const AVSampleFormat*>(configs);
    for (int i = 0; i < count; ++i) {
        if (formats[i] == preferred) {
            return preferred;
        }
    }
    return formats[0];
}

} // namespace agplayer
