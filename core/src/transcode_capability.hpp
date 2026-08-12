#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace agplayer {

struct TranscodeOptionChoice {
    std::string key;
    std::string label;
    bool is_default = false;
};

struct TranscodeFormatCapability {
    std::string key;
    std::string label;
    std::string muxer_name;
    std::string codec_name;
    bool available = false;
    std::string unavailable_reason;
    bool lossy = false;
    bool supports_metadata = false;
    bool supports_cover = false;
    std::vector<int> sample_rates;
    std::vector<std::string> sample_formats;
    std::vector<std::string> channel_layouts;
    std::vector<TranscodeOptionChoice> bitrate_modes;
};

std::vector<TranscodeFormatCapability> transcode_capabilities();

const TranscodeFormatCapability* find_transcode_capability(
    const std::vector<TranscodeFormatCapability>& capabilities,
    std::string_view key);

} // namespace agplayer
