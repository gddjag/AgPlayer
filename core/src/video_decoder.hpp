#pragma once

#include <agplayer/c_api.h>

#include <cstdint>
#include <memory>

namespace agplayer {

class VideoDecoder final {
public:
    VideoDecoder();
    ~VideoDecoder();

    VideoDecoder(const VideoDecoder&) = delete;
    VideoDecoder& operator=(const VideoDecoder&) = delete;

    [[nodiscard]] ag_result open(const char* utf8_path) noexcept;
    [[nodiscard]] ag_result read(ag_video_frame& frame) noexcept;
    [[nodiscard]] ag_result seek(std::int64_t position_ms) noexcept;
    void cancel() noexcept;
    void close() noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace agplayer
