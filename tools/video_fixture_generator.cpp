#include <cstdio>
#include <cstdint>
#include <cstring>
#include <filesystem>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/display.h>
#include <libavutil/error.h>
#include <libavutil/macros.h>
#include <libavutil/mem.h>
}

namespace {

constexpr int kWidth = 320;
constexpr int kHeight = 180;
constexpr int kFramesPerSecond = 30;
constexpr int kDurationSeconds = 2;
constexpr int kVideoFrames = kFramesPerSecond * kDurationSeconds;
constexpr int kAudioSampleRate = 48'000;
constexpr int kAudioFramesPerPacket = kAudioSampleRate / kFramesPerSecond;
struct VideoFormat {
    AVPixelFormat pixel_format;
    unsigned int codec_tag;
};

constexpr VideoFormat kAviVideoFormat{
    AV_PIX_FMT_YUV420P, MKTAG('I', '4', '2', '0')};
constexpr VideoFormat kMovVideoFormat{
    AV_PIX_FMT_RGB24, MKTAG('r', 'a', 'w', ' ')};

// A complete 1x1 transparent PNG. Keeping its bytes here avoids an encoder
// dependency for the attached-picture fixture.
constexpr std::uint8_t kPng[] = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00,
    0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
    0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4, 0x89,
    0x00, 0x00, 0x00, 0x0D, 0x49, 0x44, 0x41, 0x54, 0x08, 0xD7, 0x63,
    0xF8, 0xCF, 0xC0, 0xF0, 0x1F, 0x00, 0x05, 0x00, 0x01, 0xFF, 0x89,
    0x99, 0x3D, 0x1D, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44,
    0xAE, 0x42, 0x60, 0x82,
};

bool check(const int result)
{
    return result >= 0;
}

int video_packet_size(const VideoFormat& format, const int width,
                      const int height)
{
    const int pixels = width * height;
    return format.pixel_format == AV_PIX_FMT_YUV420P ? pixels * 3 / 2
                                                      : pixels * 3;
}

AVStream* add_video_stream(AVFormatContext* context, const VideoFormat& format,
                           const int width = kWidth,
                           const int height = kHeight,
                           const AVRational sar = AVRational{1, 1})
{
    AVStream* const stream = avformat_new_stream(context, nullptr);
    if (stream == nullptr) {
        return nullptr;
    }
    stream->time_base = AVRational{1, kFramesPerSecond};
    stream->avg_frame_rate = AVRational{kFramesPerSecond, 1};
    stream->sample_aspect_ratio = sar;
    AVCodecParameters* const parameters = stream->codecpar;
    parameters->codec_type = AVMEDIA_TYPE_VIDEO;
    parameters->codec_id = AV_CODEC_ID_RAWVIDEO;
    parameters->format = format.pixel_format;
    parameters->codec_tag = format.codec_tag;
    parameters->width = width;
    parameters->height = height;
    parameters->sample_aspect_ratio = sar;
    parameters->bit_rate = static_cast<std::int64_t>(
        video_packet_size(format, width, height))
        * kFramesPerSecond * 8;
    return stream;
}

AVStream* add_audio_stream(AVFormatContext* context)
{
    AVStream* const stream = avformat_new_stream(context, nullptr);
    if (stream == nullptr) {
        return nullptr;
    }
    stream->time_base = AVRational{1, kAudioSampleRate};
    AVCodecParameters* const parameters = stream->codecpar;
    parameters->codec_type = AVMEDIA_TYPE_AUDIO;
    parameters->codec_id = AV_CODEC_ID_PCM_S16LE;
    parameters->format = AV_SAMPLE_FMT_S16;
    parameters->sample_rate = kAudioSampleRate;
    av_channel_layout_default(&parameters->ch_layout, 2);
    parameters->bits_per_coded_sample = 16;
    parameters->bits_per_raw_sample = 16;
    parameters->bit_rate = static_cast<std::int64_t>(kAudioSampleRate) * 2 * 16;
    return stream;
}

bool write_video_packet(AVFormatContext* context, const AVStream* stream,
                        const VideoFormat& format, const int frame_index,
                        const int width = kWidth,
                        const int height = kHeight)
{
    AVPacket packet{};
    if (!check(av_new_packet(&packet,
                             video_packet_size(format, width, height)))) {
        return false;
    }
    if (format.pixel_format == AV_PIX_FMT_YUV420P) {
        for (int pixel = 0; pixel < width * height; ++pixel) {
            packet.data[pixel] = static_cast<std::uint8_t>((pixel + frame_index) & 0xFF);
        }
        const int chroma_offset = width * height;
        const int chroma_size = chroma_offset / 4;
        std::memset(packet.data + chroma_offset,
                    (frame_index * 3) & 0xFF, static_cast<std::size_t>(chroma_size));
        std::memset(packet.data + chroma_offset + chroma_size,
                    (frame_index * 7) & 0xFF, static_cast<std::size_t>(chroma_size));
    } else {
        for (int pixel = 0; pixel < width * height; ++pixel) {
            const int offset = pixel * 3;
            packet.data[offset] = static_cast<std::uint8_t>((pixel + frame_index) & 0xFF);
            packet.data[offset + 1] = static_cast<std::uint8_t>((frame_index * 3) & 0xFF);
            packet.data[offset + 2] = static_cast<std::uint8_t>((frame_index * 7) & 0xFF);
        }
    }
    packet.stream_index = stream->index;
    packet.pts = frame_index;
    packet.dts = frame_index;
    packet.duration = 1;
    packet.flags = AV_PKT_FLAG_KEY;
    const bool written = check(av_interleaved_write_frame(context, &packet));
    av_packet_unref(&packet);
    return written;
}

bool write_audio_packet(AVFormatContext* context, const AVStream* stream,
                        const int packet_index)
{
    const int packet_size = kAudioFramesPerPacket * 2 * static_cast<int>(sizeof(std::int16_t));
    AVPacket packet{};
    if (!check(av_new_packet(&packet, packet_size))) {
        return false;
    }
    auto* const samples = reinterpret_cast<std::int16_t*>(packet.data);
    for (int frame = 0; frame < kAudioFramesPerPacket; ++frame) {
        const std::int16_t sample = static_cast<std::int16_t>(
            ((packet_index * kAudioFramesPerPacket + frame) % 200) * 200 - 20'000);
        samples[frame * 2] = sample;
        samples[frame * 2 + 1] = static_cast<std::int16_t>(-sample);
    }
    packet.stream_index = stream->index;
    packet.pts = static_cast<std::int64_t>(packet_index) * kAudioFramesPerPacket;
    packet.dts = packet.pts;
    packet.duration = kAudioFramesPerPacket;
    const bool written = check(av_interleaved_write_frame(context, &packet));
    av_packet_unref(&packet);
    return written;
}

bool open_output(const std::filesystem::path& path, AVFormatContext** context)
{
    if (!check(avformat_alloc_output_context2(context, nullptr, nullptr,
                                               path.string().c_str()))
        || *context == nullptr) {
        return false;
    }
    if (((*context)->oformat->flags & AVFMT_NOFILE) != 0) {
        return true;
    }
    return check(avio_open(&(*context)->pb, path.string().c_str(), AVIO_FLAG_WRITE));
}

bool finish_output(AVFormatContext* context)
{
    const bool finished = check(av_write_trailer(context));
    if ((context->oformat->flags & AVFMT_NOFILE) == 0 && context->pb != nullptr) {
        avio_closep(&context->pb);
    }
    avformat_free_context(context);
    return finished;
}

bool write_av_file(const std::filesystem::path& path, const bool with_audio,
                   const int rotation_degrees = 0,
                   const AVRational sar = AVRational{1, 1})
{
    AVFormatContext* context = nullptr;
    if (!open_output(path, &context)) {
        return false;
    }
    const VideoFormat& video_format = rotation_degrees != 0 || sar.num != sar.den
        ? kMovVideoFormat : kAviVideoFormat;
    AVStream* const video = add_video_stream(context, video_format, kWidth,
                                             kHeight, sar);
    AVStream* const audio = with_audio ? add_audio_stream(context) : nullptr;
    if (video == nullptr || (with_audio && audio == nullptr)) {
        avformat_free_context(context);
        return false;
    }
    if (rotation_degrees != 0) {
        AVPacketSideData* const side_data = av_packet_side_data_new(
            &video->codecpar->coded_side_data,
            &video->codecpar->nb_coded_side_data,
            AV_PKT_DATA_DISPLAYMATRIX, 9 * sizeof(std::int32_t), 0);
        if (side_data == nullptr) {
            avformat_free_context(context);
            return false;
        }
        av_display_rotation_set(reinterpret_cast<std::int32_t*>(side_data->data),
                                static_cast<double>(rotation_degrees));
    }
    if (!check(avformat_write_header(context, nullptr))) {
        if ((context->oformat->flags & AVFMT_NOFILE) == 0 && context->pb != nullptr) {
            avio_closep(&context->pb);
        }
        avformat_free_context(context);
        return false;
    }
    bool success = true;
    for (int index = 0; index < kVideoFrames && success; ++index) {
        success = write_video_packet(context, video, video_format, index);
        if (success && with_audio) {
            success = write_audio_packet(context, audio, index);
        }
    }
    return finish_output(context) && success;
}

AVStream* add_png_attachment_stream(AVFormatContext* context)
{
    AVStream* const image = avformat_new_stream(context, nullptr);
    if (image == nullptr) {
        return nullptr;
    }
    image->time_base = AVRational{1, 1};
    image->disposition = AV_DISPOSITION_ATTACHED_PIC | AV_DISPOSITION_DEFAULT;
    image->codecpar->codec_type = AVMEDIA_TYPE_ATTACHMENT;
    image->codecpar->codec_id = AV_CODEC_ID_PNG;
    image->codecpar->extradata = static_cast<std::uint8_t*>(
        av_mallocz(sizeof(kPng) + AV_INPUT_BUFFER_PADDING_SIZE));
    if (image->codecpar->extradata == nullptr) {
        return nullptr;
    }
    std::memcpy(image->codecpar->extradata, kPng, sizeof(kPng));
    image->codecpar->extradata_size = static_cast<int>(sizeof(kPng));
    av_dict_set(&image->metadata, "filename", "cover.png", 0);
    av_dict_set(&image->metadata, "mimetype", "image/png", 0);
    return image;
}

bool write_multi_video_file(const std::filesystem::path& path)
{
    AVFormatContext* context = nullptr;
    if (!open_output(path, &context)) {
        return false;
    }
    AVStream* const cover = add_png_attachment_stream(context);
    AVStream* const first_real = add_video_stream(context, kAviVideoFormat,
                                                  160, 90);
    AVStream* const best_real = add_video_stream(context, kAviVideoFormat,
                                                 kWidth, kHeight);
    if (cover == nullptr || first_real == nullptr || best_real == nullptr) {
        avformat_free_context(context);
        return false;
    }
    best_real->disposition |= AV_DISPOSITION_DEFAULT;
    AVDictionary* options = nullptr;
    av_dict_set(&options, "allow_raw_vfw", "1", 0);
    const int header_result = avformat_write_header(context, &options);
    av_dict_free(&options);
    if (!check(header_result)) {
        if ((context->oformat->flags & AVFMT_NOFILE) == 0 && context->pb != nullptr) {
            avio_closep(&context->pb);
        }
        avformat_free_context(context);
        return false;
    }
    bool success = true;
    for (int index = 0; index < kVideoFrames && success; ++index) {
        success = write_video_packet(context, first_real, kAviVideoFormat,
                                     index, 160, 90)
            && write_video_packet(context, best_real, kAviVideoFormat,
                                  index, kWidth, kHeight);
    }
    return finish_output(context) && success;
}

bool write_attached_picture_file(const std::filesystem::path& path)
{
    AVFormatContext* context = nullptr;
    if (!open_output(path, &context)) {
        return false;
    }
    AVStream* const audio = add_audio_stream(context);
    AVStream* const image = avformat_new_stream(context, nullptr);
    if (audio == nullptr || image == nullptr) {
        avformat_free_context(context);
        return false;
    }
    image->time_base = AVRational{1, 1};
    image->disposition |= AV_DISPOSITION_ATTACHED_PIC;
    // Matroska stores cover art as an attachment. Its demuxer recreates this
    // as an AVMEDIA_TYPE_VIDEO stream with AV_DISPOSITION_ATTACHED_PIC.
    image->codecpar->codec_type = AVMEDIA_TYPE_ATTACHMENT;
    image->codecpar->codec_id = AV_CODEC_ID_PNG;
    image->codecpar->extradata = static_cast<std::uint8_t*>(
        av_mallocz(sizeof(kPng) + AV_INPUT_BUFFER_PADDING_SIZE));
    if (image->codecpar->extradata == nullptr) {
        avformat_free_context(context);
        return false;
    }
    std::memcpy(image->codecpar->extradata, kPng, sizeof(kPng));
    image->codecpar->extradata_size = static_cast<int>(sizeof(kPng));
    av_dict_set(&image->metadata, "filename", "cover.png", 0);
    av_dict_set(&image->metadata, "mimetype", "image/png", 0);
    if (!check(avformat_write_header(context, nullptr))) {
        if ((context->oformat->flags & AVFMT_NOFILE) == 0 && context->pb != nullptr) {
            avio_closep(&context->pb);
        }
        avformat_free_context(context);
        return false;
    }
    bool success = true;
    for (int index = 0; index < kVideoFrames && success; ++index) {
        success = write_audio_packet(context, audio, index);
    }
    return finish_output(context) && success;
}

} // namespace

int main(const int argc, char** argv)
{
    if (argc != 9) {
        return 1;
    }
    const std::filesystem::path video_with_audio = argv[1];
    const std::filesystem::path video_only = argv[2];
    const std::filesystem::path audio_with_picture = argv[3];
    const std::filesystem::path rotated_90_video = argv[4];
    const std::filesystem::path rotated_180_video = argv[5];
    const std::filesystem::path rotated_270_video = argv[6];
    const std::filesystem::path sar_video = argv[7];
    const std::filesystem::path multi_video = argv[8];
    const bool success = write_av_file(video_with_audio, true)
        && write_av_file(video_only, false)
        && write_attached_picture_file(audio_with_picture)
        && write_av_file(rotated_90_video, false, 90)
        && write_av_file(rotated_180_video, false, 180)
        && write_av_file(rotated_270_video, false, 270)
        && write_av_file(sar_video, false, 0, AVRational{4, 3})
        && write_multi_video_file(multi_video);
    return success ? 0 : 2;
}
