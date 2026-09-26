#include "proprietary_format_io.hpp"

extern "C" {
#include <libavutil/mem.h>
}

#include <cerrno>

namespace agplayer {

int open_audio_format_input(const std::string& path, AVFormatContext** format,
                            ProprietaryFormatIo& io, std::string& error)
{
    if (!ProprietaryAudioInput::recognizes_path(path)) {
        return avformat_open_input(format, path.c_str(), nullptr, nullptr);
    }
    io.input = ProprietaryAudioInput::open(path, error);
    if (!io.input) return AVERROR_INVALIDDATA;
    *format = avformat_alloc_context();
    if (*format == nullptr) return AVERROR(ENOMEM);
    constexpr int buffer_size = 64 * 1024;
    auto* buffer = static_cast<unsigned char*>(av_malloc(buffer_size));
    if (buffer == nullptr) return AVERROR(ENOMEM);
    io.avio = avio_alloc_context(buffer, buffer_size, 0, io.input.get(),
                                 &ProprietaryAudioInput::read_callback, nullptr,
                                 &ProprietaryAudioInput::seek_callback);
    if (io.avio == nullptr) {
        av_free(buffer);
        return AVERROR(ENOMEM);
    }
    (*format)->pb = io.avio;
    (*format)->flags |= AVFMT_FLAG_CUSTOM_IO;
    return avformat_open_input(format, nullptr, nullptr, nullptr);
}

void close_audio_format_input(AVFormatContext** format,
                              ProprietaryFormatIo& io) noexcept
{
    if (*format != nullptr && io.avio != nullptr) (*format)->pb = nullptr;
    avformat_close_input(format);
    if (io.avio != nullptr) av_freep(&io.avio->buffer);
    avio_context_free(&io.avio);
    io.input.reset();
}

} // namespace agplayer
