#include "video_decoder.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/display.h>
#include <libavutil/error.h>
#include <libavutil/mathematics.h>
#include <libswscale/swscale.h>
}

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <utility>
#include <vector>

namespace agplayer {
namespace {

#if defined(AGPLAYER_VIDEO_DECODER_TESTING)
std::atomic<VideoDecoderTestHook> video_decoder_test_hook{nullptr};
std::atomic<void*> video_decoder_test_hook_opaque{nullptr};
std::atomic_int video_decoder_forced_best_stream{-1};

bool invoke_test_hook(const VideoDecoderTestPoint point) noexcept
{
    const VideoDecoderTestHook hook = video_decoder_test_hook.load(
        std::memory_order_acquire);
    return hook != nullptr
        && hook(point, video_decoder_test_hook_opaque.load(
                           std::memory_order_acquire));
}
#endif

ag_result map_open_error(const int error) noexcept
{
    if (error == AVERROR(ENOMEM)) {
        return AG_INTERNAL_ERROR;
    }
    if (error == AVERROR_INVALIDDATA || error == AVERROR_DEMUXER_NOT_FOUND
        || error == AVERROR_PROTOCOL_NOT_FOUND) {
        return AG_UNSUPPORTED_FORMAT;
    }
    return AG_IO_ERROR;
}

int normalized_rotation(const AVStream* const stream) noexcept
{
    if (stream == nullptr || stream->codecpar == nullptr) {
        return 0;
    }
    const AVPacketSideData* const side_data = av_packet_side_data_get(
        stream->codecpar->coded_side_data,
        stream->codecpar->nb_coded_side_data,
        AV_PKT_DATA_DISPLAYMATRIX);
    if (side_data == nullptr || side_data->data == nullptr
        || side_data->size < 9U * sizeof(std::int32_t)) {
        return 0;
    }
    const double degrees = av_display_rotation_get(
        reinterpret_cast<const std::int32_t*>(side_data->data));
    if (!std::isfinite(degrees)) {
        return 0;
    }
    // FFmpeg's display matrix describes the inverse (counter-clockwise)
    // transform. The public API reports the clockwise display rotation.
    const long long quadrant = std::llround(-degrees / 90.0);
    const long long normalized = ((quadrant % 4LL) + 4LL) % 4LL;
    return static_cast<int>(normalized * 90LL);
}

bool is_playable_video_stream(const AVStream* const stream) noexcept
{
    return stream != nullptr && stream->codecpar != nullptr
        && stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO
        && (stream->disposition & AV_DISPOSITION_ATTACHED_PIC) == 0
        && stream->codecpar->width > 0 && stream->codecpar->height > 0
        && avcodec_find_decoder(stream->codecpar->codec_id) != nullptr;
}

bool is_better_video_stream(const AVStream* const candidate,
                            const AVStream* const current) noexcept
{
    const bool candidate_default =
        (candidate->disposition & AV_DISPOSITION_DEFAULT) != 0;
    const bool current_default =
        (current->disposition & AV_DISPOSITION_DEFAULT) != 0;
    if (candidate_default != current_default) {
        return candidate_default;
    }

    const std::int64_t candidate_pixels =
        static_cast<std::int64_t>(candidate->codecpar->width)
        * static_cast<std::int64_t>(candidate->codecpar->height);
    const std::int64_t current_pixels =
        static_cast<std::int64_t>(current->codecpar->width)
        * static_cast<std::int64_t>(current->codecpar->height);
    if (candidate_pixels != current_pixels) {
        return candidate_pixels > current_pixels;
    }
    if (candidate->codecpar->bit_rate != current->codecpar->bit_rate) {
        return candidate->codecpar->bit_rate > current->codecpar->bit_rate;
    }
    if (candidate->nb_frames != current->nb_frames) {
        return candidate->nb_frames > current->nb_frames;
    }
    return candidate->index < current->index;
}

int best_playable_video_stream(const AVFormatContext* const context) noexcept
{
    if (context == nullptr) {
        return -1;
    }
    const AVStream* best = nullptr;
    for (unsigned int index = 0; index < context->nb_streams; ++index) {
        const AVStream* const stream = context->streams[index];
        if (is_playable_video_stream(stream)
            && (best == nullptr || is_better_video_stream(stream, best))) {
            best = stream;
        }
    }
    return best == nullptr ? -1 : best->index;
}

} // namespace

#if defined(AGPLAYER_VIDEO_DECODER_TESTING)
void set_video_decoder_test_hook(const VideoDecoderTestHook hook,
                                 void* const opaque) noexcept
{
    video_decoder_test_hook_opaque.store(opaque, std::memory_order_release);
    video_decoder_test_hook.store(hook, std::memory_order_release);
}

void set_video_decoder_forced_best_stream(const int stream_index) noexcept
{
    video_decoder_forced_best_stream.store(stream_index,
                                           std::memory_order_release);
}
#endif

class VideoDecoder::Impl final {
public:
    ~Impl() { release_resources(); }

    ag_result open(const char* const utf8_path,
                   VideoMediaInfo& media_info) noexcept
    {
        media_info = {};
        invalidate_pixels();
        if (utf8_path == nullptr || utf8_path[0] == '\0') {
            return AG_INVALID_ARGUMENT;
        }
        if (open_) {
            return AG_INVALID_ARGUMENT;
        }
        release_resources();
        if (cancelled_.load(std::memory_order_acquire)) {
            return AG_CANCELLED;
        }

        format_context_ = avformat_alloc_context();
        if (format_context_ == nullptr) {
            return AG_INTERNAL_ERROR;
        }
        format_context_->interrupt_callback.callback = &interrupt_callback;
        format_context_->interrupt_callback.opaque = this;

#if defined(AGPLAYER_VIDEO_DECODER_TESTING)
        invoke_test_hook(VideoDecoderTestPoint::open_entered);
#endif
        int result = avformat_open_input(&format_context_, utf8_path, nullptr,
                                         nullptr);
        if (result < 0) {
            const ag_result mapped = cancelled_or(result, map_open_error(result));
            release_resources();
            return mapped;
        }
        result = avformat_find_stream_info(format_context_, nullptr);
        if (result < 0) {
            const ag_result mapped = cancelled_or(result,
                                                   AG_UNSUPPORTED_FORMAT);
            release_resources();
            return mapped;
        }

        media_info.valid = true;
        for (unsigned int index = 0; index < format_context_->nb_streams;
             ++index) {
            const AVStream* const candidate = format_context_->streams[index];
            if (candidate == nullptr || candidate->codecpar == nullptr) {
                continue;
            }
            if (candidate->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
                media_info.has_audio = true;
            } else if (candidate->codecpar->codec_type == AVMEDIA_TYPE_VIDEO
                       && (candidate->disposition
                           & AV_DISPOSITION_ATTACHED_PIC) == 0) {
                media_info.has_video = true;
            }
        }

        int selected = av_find_best_stream(format_context_, AVMEDIA_TYPE_VIDEO,
                                           -1, -1, nullptr, 0);
#if defined(AGPLAYER_VIDEO_DECODER_TESTING)
        const int forced_best = video_decoder_forced_best_stream.load(
            std::memory_order_acquire);
        if (forced_best >= 0) {
            selected = forced_best;
        }
#endif
        if (selected < 0
            || static_cast<unsigned int>(selected)
                   >= format_context_->nb_streams
            || !is_playable_video_stream(format_context_->streams[selected])) {
            selected = best_playable_video_stream(format_context_);
        }
        if (selected < 0) {
            release_resources();
            return AG_UNSUPPORTED_FORMAT;
        }
        AVStream* const stream = format_context_->streams[selected];
        if (stream == nullptr || stream->codecpar == nullptr
            || stream->codecpar->width <= 0 || stream->codecpar->height <= 0) {
            release_resources();
            return AG_UNSUPPORTED_FORMAT;
        }
        const AVCodec* const codec = avcodec_find_decoder(
            stream->codecpar->codec_id);
        if (codec == nullptr) {
            release_resources();
            return AG_UNSUPPORTED_FORMAT;
        }
        codec_context_ = avcodec_alloc_context3(codec);
        if (codec_context_ == nullptr) {
            release_resources();
            return AG_INTERNAL_ERROR;
        }
        result = avcodec_parameters_to_context(codec_context_, stream->codecpar);
        if (result < 0) {
            release_resources();
            return result == AVERROR(ENOMEM) ? AG_INTERNAL_ERROR
                                             : AG_UNSUPPORTED_FORMAT;
        }
#if defined(AGPLAYER_VIDEO_DECODER_TESTING)
        if (invoke_test_hook(VideoDecoderTestPoint::codec_open_entered)) {
            release_resources();
            return AG_UNSUPPORTED_FORMAT;
        }
#endif
        result = avcodec_open2(codec_context_, codec, nullptr);
        if (result < 0) {
            const ag_result mapped = cancelled_or(
                result, result == AVERROR(ENOMEM) ? AG_INTERNAL_ERROR
                                                  : AG_UNSUPPORTED_FORMAT);
            release_resources();
            return mapped;
        }
        frame_ = av_frame_alloc();
        packet_ = av_packet_alloc();
        if (frame_ == nullptr || packet_ == nullptr) {
            release_resources();
            return AG_INTERNAL_ERROR;
        }

        stream_index_ = selected;
        time_base_ = stream->time_base;
        stream_start_time_ = stream->start_time;
        stream_sar_ = stream->sample_aspect_ratio;
        rotation_degrees_ = normalized_rotation(stream);
        packet_pending_ = false;
        drain_pending_ = false;
        draining_ = false;
        end_of_stream_ = false;
        open_ = true;
        if (cancelled_.load(std::memory_order_acquire)) {
            release_resources();
            return AG_CANCELLED;
        }
        return AG_OK;
    }

    ag_result read(ag_video_frame& output) noexcept
    {
        const std::uint32_t supplied_size = output.struct_size;
        invalidate_pixels();
        if (supplied_size < sizeof(ag_video_frame)) {
            return AG_INVALID_ARGUMENT;
        }
        if (!open_) {
            return AG_INVALID_ARGUMENT;
        }
        if (cancelled_.load(std::memory_order_acquire)) {
            return cancel_read(output, supplied_size);
        }
        if (end_of_stream_) {
            set_end_of_stream(output, supplied_size);
            return AG_OK;
        }

        try {
            for (;;) {
                if (cancelled_.load(std::memory_order_acquire)) {
                    return cancel_read(output, supplied_size);
                }
                int result = avcodec_receive_frame(codec_context_, frame_);
                if (result >= 0) {
#if defined(AGPLAYER_VIDEO_DECODER_TESTING)
                    invoke_test_hook(VideoDecoderTestPoint::frame_received);
#endif
                    if (cancelled_.load(std::memory_order_acquire)) {
                        return cancel_read(output, supplied_size);
                    }
                    return copy_frame(output, supplied_size);
                }
                if (result == AVERROR_EOF) {
                    if (cancelled_.load(std::memory_order_acquire)) {
                        return cancel_read(output, supplied_size);
                    }
                    end_of_stream_ = true;
                    set_end_of_stream(output, supplied_size);
                    return AG_OK;
                }
                if (result != AVERROR(EAGAIN)) {
                    return read_error(result, AG_DECODE_ERROR, output,
                                      supplied_size);
                }

                if (packet_pending_) {
#if defined(AGPLAYER_VIDEO_DECODER_TESTING)
                    result = invoke_test_hook(VideoDecoderTestPoint::send_packet)
                        ? AVERROR(EAGAIN)
                        : avcodec_send_packet(codec_context_, packet_);
#else
                    result = avcodec_send_packet(codec_context_, packet_);
#endif
                    if (result == AVERROR(EAGAIN)) {
                        continue;
                    }
                    av_packet_unref(packet_);
                    packet_pending_ = false;
                    if (result < 0) {
                        return read_error(result, AG_DECODE_ERROR, output,
                                          supplied_size);
                    }
                    continue;
                }

                if (drain_pending_) {
#if defined(AGPLAYER_VIDEO_DECODER_TESTING)
                    result = invoke_test_hook(VideoDecoderTestPoint::send_drain)
                        ? AVERROR(EAGAIN)
                        : avcodec_send_packet(codec_context_, nullptr);
#else
                    result = avcodec_send_packet(codec_context_, nullptr);
#endif
                    if (result == AVERROR(EAGAIN)) {
                        continue;
                    }
                    drain_pending_ = false;
                    if (result < 0 && result != AVERROR_EOF) {
                        return read_error(result, AG_DECODE_ERROR, output,
                                          supplied_size);
                    }
                    draining_ = true;
                    continue;
                }

                if (draining_) {
                    return AG_DECODE_ERROR;
                }

                result = av_read_frame(format_context_, packet_);
                if (result == AVERROR_EOF) {
                    drain_pending_ = true;
                    continue;
                }
                if (result < 0) {
                    return read_error(result, AG_IO_ERROR, output,
                                      supplied_size);
                }
                if (packet_->stream_index != stream_index_) {
                    av_packet_unref(packet_);
                    continue;
                }
                packet_pending_ = true;
            }
        } catch (const std::bad_alloc&) {
            invalidate_pixels();
            return AG_INTERNAL_ERROR;
        } catch (...) {
            invalidate_pixels();
            return AG_INTERNAL_ERROR;
        }
    }

    ag_result seek(const std::int64_t position_ms) noexcept
    {
        invalidate_pixels();
        if (!open_ || position_ms < 0) {
            return AG_INVALID_ARGUMENT;
        }
#if defined(AGPLAYER_VIDEO_DECODER_TESTING)
        invoke_test_hook(VideoDecoderTestPoint::seek_entered);
#endif
        if (cancelled_.load(std::memory_order_acquire)) {
            return AG_CANCELLED;
        }
        if (time_base_.num <= 0 || time_base_.den <= 0) {
            return AG_DECODE_ERROR;
        }
        std::int64_t target = av_rescale_q_rnd(
            position_ms, AVRational{1, 1'000}, time_base_,
            static_cast<AVRounding>(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
        if (target == std::numeric_limits<std::int64_t>::min()
            || target == std::numeric_limits<std::int64_t>::max()) {
            return AG_INVALID_ARGUMENT;
        }
        if (stream_start_time_ != AV_NOPTS_VALUE) {
            if (stream_start_time_ > 0
                && target > std::numeric_limits<std::int64_t>::max()
                    - stream_start_time_) {
                return AG_INVALID_ARGUMENT;
            }
            if (stream_start_time_ < 0
                && target < std::numeric_limits<std::int64_t>::min()
                    - stream_start_time_) {
                return AG_INVALID_ARGUMENT;
            }
            target += stream_start_time_;
        }
        const int result = av_seek_frame(format_context_, stream_index_, target,
                                         AVSEEK_FLAG_BACKWARD);
        if (result < 0) {
            return cancelled_or(result, AG_IO_ERROR);
        }
        avcodec_flush_buffers(codec_context_);
        av_packet_unref(packet_);
        av_frame_unref(frame_);
        packet_pending_ = false;
        drain_pending_ = false;
        draining_ = false;
        end_of_stream_ = false;
        return cancelled_.load(std::memory_order_acquire) ? AG_CANCELLED : AG_OK;
    }

    void cancel() noexcept
    {
        cancelled_.store(true, std::memory_order_release);
    }

    void close() noexcept
    {
        release_resources();
        cancelled_.store(false, std::memory_order_release);
    }

private:
    static int interrupt_callback(void* const opaque) noexcept
    {
        const auto* const self = static_cast<const Impl*>(opaque);
        return self != nullptr
                && self->cancelled_.load(std::memory_order_acquire)
            ? 1 : 0;
    }

    ag_result cancelled_or(const int ffmpeg_result,
                           const ag_result fallback) const noexcept
    {
        return cancelled_.load(std::memory_order_acquire)
                || ffmpeg_result == AVERROR_EXIT
            ? AG_CANCELLED : fallback;
    }

    ag_result copy_frame(ag_video_frame& output,
                         const std::uint32_t supplied_size)
    {
        const int width = frame_->width;
        const int height = frame_->height;
        if (width <= 0 || height <= 0
            || width > std::numeric_limits<int>::max() / 4) {
            return AG_DECODE_ERROR;
        }
        const int stride = width * 4;
        const std::size_t unsigned_stride = static_cast<std::size_t>(stride);
        if (static_cast<std::size_t>(height)
            > std::numeric_limits<std::size_t>::max() / unsigned_stride) {
            return AG_DECODE_ERROR;
        }
        const std::size_t data_size = unsigned_stride
            * static_cast<std::size_t>(height);
        if (data_size > pixels_.max_size()) {
            return AG_DECODE_ERROR;
        }
        pixels_.resize(data_size);

        sws_context_ = sws_getCachedContext(
            sws_context_, width, height,
            static_cast<AVPixelFormat>(frame_->format),
            width, height, AV_PIX_FMT_BGRA, SWS_BILINEAR,
            nullptr, nullptr, nullptr);
        if (sws_context_ == nullptr) {
            invalidate_pixels();
            return AG_UNSUPPORTED_FORMAT;
        }
        std::uint8_t* destination_data[4] = {pixels_.data(), nullptr, nullptr,
                                             nullptr};
        const int destination_linesize[4] = {stride, 0, 0, 0};
        const int scaled_height = sws_scale(
            sws_context_, frame_->data, frame_->linesize, 0, height,
            destination_data, destination_linesize);
        if (scaled_height != height) {
            invalidate_pixels();
            return AG_DECODE_ERROR;
        }
#if defined(AGPLAYER_VIDEO_DECODER_TESTING)
        invoke_test_hook(VideoDecoderTestPoint::frame_converted);
#endif
        if (cancelled_.load(std::memory_order_acquire)) {
            return cancel_read(output, supplied_size);
        }

        AVRational sar = frame_->sample_aspect_ratio;
        if (sar.num <= 0 || sar.den <= 0) {
            sar = stream_sar_;
        }
        if (sar.num <= 0 || sar.den <= 0) {
            sar = AVRational{1, 1};
        }
        std::int64_t pts_ms = 0;
        if (frame_->best_effort_timestamp != AV_NOPTS_VALUE) {
            pts_ms = av_rescale_q(frame_->best_effort_timestamp, time_base_,
                                  AVRational{1, 1'000});
        }

        ag_video_frame result{};
        result.struct_size = supplied_size;
        result.data = pixels_.data();
        result.data_size = data_size;
        result.width = width;
        result.height = height;
        result.stride = stride;
        result.pixel_format = AG_VIDEO_PIXEL_FORMAT_BGRA8;
        result.pts_ms = pts_ms;
        result.sar_num = sar.num;
        result.sar_den = sar.den;
        result.rotation_degrees = rotation_degrees_;
        result.end_of_stream = 0;
        output = result;
        av_frame_unref(frame_);
        return AG_OK;
    }

    static void set_end_of_stream(ag_video_frame& output,
                                  const std::uint32_t supplied_size) noexcept
    {
        ag_video_frame result{};
        result.struct_size = supplied_size;
        result.pixel_format = AG_VIDEO_PIXEL_FORMAT_BGRA8;
        result.sar_num = 1;
        result.sar_den = 1;
        result.end_of_stream = 1;
        output = result;
    }

    static void clear_output(ag_video_frame& output,
                             const std::uint32_t supplied_size) noexcept
    {
        ag_video_frame result{};
        result.struct_size = supplied_size;
        output = result;
    }

    ag_result cancel_read(ag_video_frame& output,
                          const std::uint32_t supplied_size) noexcept
    {
        av_frame_unref(frame_);
        invalidate_pixels();
        clear_output(output, supplied_size);
        return AG_CANCELLED;
    }

    ag_result read_error(const int ffmpeg_result, const ag_result fallback,
                         ag_video_frame& output,
                         const std::uint32_t supplied_size) noexcept
    {
        return cancelled_or(ffmpeg_result, fallback) == AG_CANCELLED
            ? cancel_read(output, supplied_size) : fallback;
    }

    void invalidate_pixels() noexcept
    {
        pixels_.clear();
    }

    void release_pixels() noexcept
    {
        std::vector<unsigned char>().swap(pixels_);
    }

    void release_resources() noexcept
    {
        release_pixels();
        if (sws_context_ != nullptr) {
            sws_freeContext(sws_context_);
            sws_context_ = nullptr;
        }
        av_packet_free(&packet_);
        av_frame_free(&frame_);
        avcodec_free_context(&codec_context_);
        avformat_close_input(&format_context_);
        stream_index_ = -1;
        time_base_ = AVRational{0, 1};
        stream_start_time_ = AV_NOPTS_VALUE;
        stream_sar_ = AVRational{0, 1};
        rotation_degrees_ = 0;
        packet_pending_ = false;
        drain_pending_ = false;
        draining_ = false;
        end_of_stream_ = false;
        open_ = false;
    }

    std::atomic_bool cancelled_{false};
    AVFormatContext* format_context_ = nullptr;
    AVCodecContext* codec_context_ = nullptr;
    AVFrame* frame_ = nullptr;
    AVPacket* packet_ = nullptr;
    SwsContext* sws_context_ = nullptr;
    std::vector<unsigned char> pixels_;
    int stream_index_ = -1;
    AVRational time_base_{0, 1};
    std::int64_t stream_start_time_ = AV_NOPTS_VALUE;
    AVRational stream_sar_{0, 1};
    int rotation_degrees_ = 0;
    bool packet_pending_ = false;
    bool drain_pending_ = false;
    bool draining_ = false;
    bool end_of_stream_ = false;
    bool open_ = false;
};

VideoDecoder::VideoDecoder() : impl_(std::make_unique<Impl>()) {}
VideoDecoder::~VideoDecoder() = default;

ag_result VideoDecoder::open(const char* const utf8_path,
                             VideoMediaInfo& media_info) noexcept
{
    return impl_->open(utf8_path, media_info);
}

ag_result VideoDecoder::read(ag_video_frame& frame) noexcept
{
    return impl_->read(frame);
}

ag_result VideoDecoder::seek(const std::int64_t position_ms) noexcept
{
    return impl_->seek(position_ms);
}

void VideoDecoder::cancel() noexcept { impl_->cancel(); }
void VideoDecoder::close() noexcept { impl_->close(); }

} // namespace agplayer
