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
    std::string output_extension;
    std::string parameter_kind;
    bool available = false;
    std::string unavailable_reason;
    bool lossy = false;
    bool supports_metadata = false;
    bool supports_cover = false;
    std::vector<int> quality_choices;
    int default_quality = 0;
    std::vector<TranscodeOptionChoice> bit_depth_choices;
    std::vector<int> sample_rates;
    std::vector<std::string> sample_formats;
    std::vector<std::string> channel_layouts;
    std::vector<TranscodeOptionChoice> bitrate_modes;
    std::vector<int> sample_rate_choices;
    std::vector<int> bit_rate_choices;
    int default_bit_rate = 0;
};

std::vector<TranscodeFormatCapability> transcode_capabilities();

const TranscodeFormatCapability* find_transcode_capability(
    const std::vector<TranscodeFormatCapability>& capabilities,
    std::string_view key);

} // namespace agplayer
