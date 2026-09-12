#pragma once

#include <agplayer/c_api.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace agplayer {

struct AudioStreamProbe {
    int stream_index = -1;
    std::string codec;
    std::string language;
    std::string title;
    bool is_default = false;
    int sample_rate = 0;
    std::string sample_format;
    std::string channel_layout;
    std::int64_t bit_rate = 0;
    std::int64_t duration_ms = 0;
    int bits_per_sample = 0;
};

struct MediaProbe {
    std::string container;
    bool is_video = false;
    bool has_cover = false;
    std::vector<AudioStreamProbe> audio_streams;
};

ag_result probe_transcode_input(std::string_view utf8_path,
                                MediaProbe& probe,
                                std::string& error);

} // namespace agplayer
