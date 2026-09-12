#pragma once

#include <agplayer/c_api.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace agplayer {

struct TranscodeVerificationPlan {
    std::int64_t expected_duration_ms = 0;
    bool lossless = false;
    bool expect_metadata = false;
    bool expect_cover = false;
    int expected_audio_streams = 1;
};

struct TranscodeVerificationResult {
    std::int64_t decoded_samples = 0;
    std::int64_t decoded_duration_ms = 0;
    bool metadata_present = false;
    bool cover_present = false;
};

ag_result verify_transcoded_output(
    std::string_view utf8_path,
    const TranscodeVerificationPlan& plan,
    TranscodeVerificationResult& result,
    std::string& error);

} // namespace agplayer
