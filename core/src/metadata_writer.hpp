#pragma once

#include <agplayer/c_api.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace agplayer {

// Fields set to empty string are preserved from the source file.
// cover_data is applied only if cover_size > 0.
struct MetadataUpdate {
    std::string title;
    std::string artist;
    std::string album;
    std::string year;
    std::string genre;
    std::string lyrics;
    const unsigned char* cover_data = nullptr;
    std::size_t cover_size = 0;
    std::string cover_mime_type;
};

// Write metadata to an audio file using FFmpeg stream copy (no re-encoding).
// Writes to a temp file then atomically replaces the original.
// Returns AG_OK on success, or an error code. On failure, error is set.
ag_result write_metadata(const std::string& utf8_path,
                         const MetadataUpdate& update,
                         std::string& error);

} // namespace agplayer
