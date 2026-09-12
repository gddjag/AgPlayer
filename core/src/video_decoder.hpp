#pragma once

#include <agplayer/c_api.h>

#include <cstdint>
#include <memory>

namespace agplayer {

struct VideoMediaInfo final {
    bool valid = false;
    bool has_audio = false;
    bool has_video = false;
};

#if defined(AGPLAYER_VIDEO_DECODER_TESTING)
enum class VideoDecoderTestPoint {
    open_entered,
    codec_open_entered,
    frame_received,
    frame_converted,
    seek_entered,
    send_packet,
    send_drain,
};

using VideoDecoderTestHook = bool (*)(VideoDecoderTestPoint point,
                                      void* opaque) noexcept;

void set_video_decoder_test_hook(VideoDecoderTestHook hook,
                                 void* opaque) noexcept;
void set_video_decoder_forced_best_stream(int stream_index) noexcept;
bool video_decoder_is_shutting_down_for_test(
    const ag_video_decoder* decoder) noexcept;
#endif

class VideoDecoder final {
public:
    VideoDecoder();
    ~VideoDecoder();

    VideoDecoder(const VideoDecoder&) = delete;
    VideoDecoder& operator=(const VideoDecoder&) = delete;

    [[nodiscard]] ag_result open(const char* utf8_path,
                                 VideoMediaInfo& media_info) noexcept;
    [[nodiscard]] ag_result read(ag_video_frame& frame) noexcept;
    [[nodiscard]] ag_result seek(std::int64_t position_ms) noexcept;
    void cancel() noexcept;
    void close() noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace agplayer
