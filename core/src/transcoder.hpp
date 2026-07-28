#pragma once

#include <agplayer/c_api.h>

#include <atomic>
#include <functional>
#include <string>

namespace agplayer {

// Transcode configuration. output_path determines the output container format
// via its file extension (e.g. .mp3, .wav, .flac, .m4a, .ogg, .opus).
// codec_name empty = auto-select encoder for the container.
// bit_rate 0 = codec default. sample_rate 0 = keep source. channels 0 = keep source.
// volume_normalize enables a two-pass peak scan that scales the output so the
// loudest sample reaches -1 dBFS (0.8913). Already-quiet files are not amplified.
struct TranscodeConfig {
    std::string output_path;
    std::string codec_name;
    long long bit_rate = 0;
    int sample_rate = 0;
    int channels = 0;
    bool volume_normalize = false;
    bool keep_metadata = false;
};

// Transcode a single audio file. progress_callback receives a fraction in
// [0.0, 1.0] based on processed duration. cancelled (may be null) is polled
// between packets; if cancelled, the partial output is deleted and
// AG_CANCELLED is returned. Returns AG_OK on success, error set on failure.
ag_result transcode(const std::string& input_path,
                    const TranscodeConfig& config,
                    const std::atomic_bool* cancelled,
                    std::function<void(float)> progress_callback,
                    std::string& error);

} // namespace agplayer
