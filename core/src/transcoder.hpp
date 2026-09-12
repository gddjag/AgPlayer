#pragma once

#include <agplayer/c_api.h>

#include "metadata_writer.hpp"

#include <atomic>
#include <functional>
#include <string>
#include <string_view>

namespace agplayer {

// Transcode configuration. output_path determines the output container format
// via its file extension (e.g. .mp3, .wav, .flac, .m4a, .ogg, .opus).
// codec_name empty = auto-select encoder for the container.
// bit_rate 0 = codec default. sample_rate 0 = keep source. channels 0 = keep source.
// volume_normalize enables a two-pass peak scan that scales the output so the
// loudest sample reaches -1 dBFS (0.8913). Already-quiet files are not amplified.
struct TranscodeConfig {
    std::string output_path;
    std::string container_name;
    std::string codec_name;
    long long bit_rate = 0;
    int sample_rate = 0;
    int channels = 0;
    std::string channel_layout;
    std::string sample_format;
    int audio_stream_index = -1;
    bool volume_normalize = false;
    bool keep_metadata = false;
    bool keep_cover = false;
    bool variable_bit_rate = false;
    int quality = 75;
    MetadataEditPlan metadata_edit_plan;
    std::function<void(std::string_view)> stage_callback;
};

// Validates canonical edits against the explicitly selected output muxer.
// This does not create or open an output file and must run before encoding.
ag_result preflight_transcode_metadata(const TranscodeConfig& config,
                                       std::string& error);

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
