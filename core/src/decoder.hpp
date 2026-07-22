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
    std::string format;
    int sample_rate = 0;
    int channels = 0;
    int bits_per_sample = 0;
    std::int64_t bit_rate = 0;
    std::int64_t duration_ms = 0;
    std::vector<unsigned char> cover;
    std::string cover_mime_type;
};

struct DecodedAudioBlock final {
    std::vector<float> samples;
    std::size_t frames = 0U;
    std::int64_t timestamp_ms = 0;
    bool end_of_stream = false;
};

class Decoder final {
public:
    Decoder();
    ~Decoder();

    Decoder(const Decoder&) = delete;
    Decoder& operator=(const Decoder&) = delete;

    [[nodiscard]] ag_result open(const std::string& utf8_path) noexcept;
    void close() noexcept;
    [[nodiscard]] ag_result read(DecodedAudioBlock& block) noexcept;
    [[nodiscard]] ag_result seek(std::int64_t target_ms) noexcept;
    [[nodiscard]] const MediaMetadata& metadata() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace agplayer
