#pragma once

#include <agplayer/c_api.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace agplayer {

struct MediaMetadata final {
    std::string title;
    std::string artist;
    std::string album;
    std::string album_artist;
    std::string track;
    std::string disc;
    std::string composer;
    std::string comment;
    std::string bpm;
    std::string copyright;
    std::string encoder;
    std::string format;
    int sample_rate = 0;
    int channels = 0;
    int bits_per_sample = 0;
    std::int64_t bit_rate = 0;
    std::int64_t duration_ms = 0;
    std::vector<unsigned char> cover;
    std::string cover_mime_type;
    std::string year;    // from AV_DICT "date"
    std::string genre;   // from AV_DICT "genre"
    std::string lyrics;  // from AV_DICT "lyrics" / "LYRICS" / "USLT"
};

struct DecodedAudioBlock final {
    std::vector<float> samples;
    std::size_t frames = 0U;
    std::int64_t timestamp_frame = 0;
    std::int64_t timestamp_ms = 0;
    bool end_of_stream = false;
};

// Reads container/stream metadata without allocating or opening a decoder.
[[nodiscard]] ag_result probe_media_metadata(const std::string& utf8_path,
                                             MediaMetadata& metadata) noexcept;

class Decoder final {
public:
    Decoder();
    ~Decoder();

    Decoder(const Decoder&) = delete;
    Decoder& operator=(const Decoder&) = delete;

    [[nodiscard]] ag_result open(const std::string& utf8_path) noexcept;
    [[nodiscard]] ag_result open(const std::string& utf8_path,
                                 int output_sample_rate,
                                 int output_channels) noexcept;
    void close() noexcept;
    [[nodiscard]] bool is_open() const noexcept;
    [[nodiscard]] ag_result read(DecodedAudioBlock& block) noexcept;
    [[nodiscard]] ag_result seek(std::int64_t target_ms) noexcept;
    [[nodiscard]] ag_result seekFrame(std::int64_t target_frame) noexcept;
    [[nodiscard]] const MediaMetadata& metadata() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace agplayer
