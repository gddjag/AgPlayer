#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

struct AVFormatContext;
namespace agplayer {
struct MediaMetadata;

// A seekable view of the audio payload inside a supported local container.
// Ordinary audio files never construct this object.
class ProprietaryAudioInput final {
public:
    static bool recognizes_path(const std::string& utf8_path);
    static std::unique_ptr<ProprietaryAudioInput> open(
        const std::string& utf8_path, std::string& error);

    int read(std::uint8_t* buffer, int capacity) noexcept;
    std::int64_t seek(std::int64_t offset, int whence) noexcept;
    static int read_callback(void* opaque, std::uint8_t* buffer,
                             int capacity) noexcept;
    static std::int64_t seek_callback(void* opaque, std::int64_t offset,
                                      int whence) noexcept;
    void apply_container_metadata(MediaMetadata& metadata) const;
    void apply_format_tags(AVFormatContext* format) const;

private:
    enum class Cipher { Passthrough, QmcStatic, QmcMap, QmcRc4,
                        Kwm, Ncm, Kgm, Vpr };
    explicit ProprietaryAudioInput(std::ifstream file) : file_(std::move(file)) {}
    bool initialize(const std::string& extension, std::string& error);
    std::uint64_t qmc_segment_key(std::uint64_t id) const noexcept;
    std::ifstream file_;
    Cipher cipher_ = Cipher::QmcStatic;
    std::uint64_t payload_offset_ = 0;
    std::uint64_t payload_size_ = 0;
    std::uint64_t position_ = 0;
    std::array<std::uint8_t, 32> kwm_mask_{};
    std::array<std::uint8_t, 256> ncm_box_{};
    std::array<std::uint8_t, 17> kgm_key_{};
    std::vector<std::uint8_t> qmc_key_;
    std::vector<std::uint8_t> qmc_keystream_;
    std::uint32_t qmc_hash_ = 1;
    std::string container_title_;
    std::string container_artist_;
    std::string container_album_;
    std::vector<std::uint8_t> container_cover_;
    std::string container_cover_mime_;
    int container_cover_width_ = 0;
    int container_cover_height_ = 0;
};

} // namespace agplayer
