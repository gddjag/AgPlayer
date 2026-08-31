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
    std::string custom_tag;
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
    std::string year;    // canonical year (for example Vorbis YEAR)
    std::string date;    // canonical full date (for example DATE/TDRC)
    std::string genre;   // from AV_DICT "genre"
    std::string lyrics;  // from AV_DICT "lyrics" / "LYRICS" / "USLT"
};

// Deterministic failure injection for the noexcept probe boundary. Production
// callers leave this null.
struct MediaMetadataProbeTestHooks final {
    bool throw_allocation_failure = false;
};

struct DecodedAudioBlock final {
    std::vector<float> samples;
    std::size_t frames = 0U;
    std::int64_t timestamp_frame = 0;
    std::int64_t timestamp_ms = 0;
    bool end_of_stream = false;
};

enum class DecoderDownmix {
    Preserve,
    AnalysisMono,
};

using DecoderInterruptCallback = bool (*)(void*) noexcept;

struct DecoderOpenOptions final {
    int output_sample_rate = 0;
    int output_channels = 0;
    DecoderDownmix downmix = DecoderDownmix::Preserve;
    DecoderInterruptCallback interrupt_callback = nullptr;
    void* interrupt_context = nullptr;
};

struct DecodedAudioFormat final {
    int sample_rate = 0;
    int channels = 0;
    std::uint64_t timeline_frames = 0;
    bool has_timeline = false;
};

// Reads container/stream metadata without allocating or opening a decoder.
[[nodiscard]] ag_result probe_media_metadata(const std::string& utf8_path,
                                             MediaMetadata& metadata,
                                             const MediaMetadataProbeTestHooks*
                                                 test_hooks = nullptr) noexcept;

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
    [[nodiscard]] ag_result open(const std::string& utf8_path,
                                 const DecoderOpenOptions& options) noexcept;
    void close() noexcept;
    [[nodiscard]] bool is_open() const noexcept;
    [[nodiscard]] ag_result read(DecodedAudioBlock& block) noexcept;
    [[nodiscard]] ag_result seek(std::int64_t target_ms) noexcept;
    [[nodiscard]] ag_result seekFrame(std::int64_t target_frame) noexcept;
    void clearInterruptCallback() noexcept;
    [[nodiscard]] const MediaMetadata& metadata() const noexcept;
    [[nodiscard]] const DecodedAudioFormat& output_format() const noexcept;
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace agplayer
