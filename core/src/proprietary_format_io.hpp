#pragma once

#include "proprietary_audio_input.hpp"

extern "C" {
#include <libavformat/avformat.h>
}

#include <memory>
#include <string>

namespace agplayer {

struct ProprietaryFormatIo final {
    std::unique_ptr<ProprietaryAudioInput> input;
    AVIOContext* avio = nullptr;
};

// Preserve FFmpeg's direct file path for every ordinary media file.
int open_audio_format_input(const std::string& path, AVFormatContext** format,
                            ProprietaryFormatIo& io, std::string& error);
void close_audio_format_input(AVFormatContext** format,
                              ProprietaryFormatIo& io) noexcept;

} // namespace agplayer
