#include "metadata_writer.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
#include <libavutil/opt.h>
}

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace agplayer {

namespace {

bool atomic_replace(const std::string& temp_path, const std::string& target_path)
{
#ifdef _WIN32
    const std::filesystem::path temp_w(temp_path);
    const std::filesystem::path target_w(target_path);
    return MoveFileExW(temp_w.wstring().c_str(), target_w.wstring().c_str(),
                       MOVEFILE_REPLACE_EXISTING) != 0;
#else
    return std::rename(temp_path.c_str(), target_path.c_str()) == 0;
#endif
}

void cleanup_output(AVFormatContext*& out_ctx)
{
    if (out_ctx != nullptr) {
        if (out_ctx->pb != nullptr) {
            avio_closep(&out_ctx->pb);
        }
        avformat_free_context(out_ctx);
        out_ctx = nullptr;
    }
}

} // namespace

ag_result write_metadata(const std::string& utf8_path,
                         const MetadataUpdate& update,
                         std::string& error)
{
    AVFormatContext* in_ctx = nullptr;
    if (avformat_open_input(&in_ctx, utf8_path.c_str(), nullptr, nullptr) < 0) {
        error = "Failed to open input file";
        return AG_IO_ERROR;
    }
    if (avformat_find_stream_info(in_ctx, nullptr) < 0) {
        avformat_close_input(&in_ctx);
        error = "Failed to find stream info";
        return AG_DECODE_ERROR;
    }

    AVFormatContext* out_ctx = nullptr;
    // Use the original path so FFmpeg guesses the output format from extension.
    if (avformat_alloc_output_context2(&out_ctx, nullptr, nullptr,
                                       utf8_path.c_str()) < 0
        || out_ctx == nullptr) {
        avformat_close_input(&in_ctx);
        error = "Failed to allocate output context";
        return AG_INTERNAL_ERROR;
    }

    // Copy all streams without re-encoding.
    for (unsigned int i = 0; i < in_ctx->nb_streams; ++i) {
        AVStream* in_stream = in_ctx->streams[i];
        AVStream* out_stream = avformat_new_stream(out_ctx, nullptr);
        if (out_stream == nullptr) {
            avformat_close_input(&in_ctx);
            cleanup_output(out_ctx);
            error = "Failed to create output stream";
            return AG_INTERNAL_ERROR;
        }
        if (avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar) < 0) {
            avformat_close_input(&in_ctx);
            cleanup_output(out_ctx);
            error = "Failed to copy codec parameters";
            return AG_INTERNAL_ERROR;
        }
        out_stream->time_base = in_stream->time_base;
    }

    // Copy format-level metadata, then override with user-specified fields.
    av_dict_copy(&out_ctx->metadata, in_ctx->metadata, 0);
    if (!update.title.empty()) {
        av_dict_set(&out_ctx->metadata, "title", update.title.c_str(), 0);
    }
    if (!update.artist.empty()) {
        av_dict_set(&out_ctx->metadata, "artist", update.artist.c_str(), 0);
    }
    if (!update.album.empty()) {
        av_dict_set(&out_ctx->metadata, "album", update.album.c_str(), 0);
    }
    if (!update.year.empty()) {
        av_dict_set(&out_ctx->metadata, "date", update.year.c_str(), 0);
    }
    if (!update.genre.empty()) {
        av_dict_set(&out_ctx->metadata, "genre", update.genre.c_str(), 0);
    }
    if (!update.lyrics.empty()) {
        av_dict_set(&out_ctx->metadata, "lyrics", update.lyrics.c_str(), 0);
    }

    // Build temp file path next to the original.
    const std::filesystem::path p(utf8_path);
    const auto pid = static_cast<std::uint32_t>(
#ifdef _WIN32
        GetCurrentProcessId()
#else
        getpid()
#endif
    );
    const std::string temp_path =
        (p.parent_path()
         / (p.filename().string()
            + ".agtmp-"
            + std::to_string(pid)
            + "-"
            + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())))
            .string();

    if (!(out_ctx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&out_ctx->pb, temp_path.c_str(), AVIO_FLAG_WRITE) < 0) {
            avformat_close_input(&in_ctx);
            cleanup_output(out_ctx);
            std::error_code ec;
            std::filesystem::remove(temp_path, ec);
            error = "Failed to open temp output file";
            return AG_IO_ERROR;
        }
    }

    if (avformat_write_header(out_ctx, nullptr) < 0) {
        avformat_close_input(&in_ctx);
        cleanup_output(out_ctx);
        std::error_code ec;
        std::filesystem::remove(temp_path, ec);
        error = "Failed to write header";
        return AG_INTERNAL_ERROR;
    }

    AVPacket* pkt = av_packet_alloc();
    if (pkt == nullptr) {
        av_write_trailer(out_ctx);
        avformat_close_input(&in_ctx);
        cleanup_output(out_ctx);
        std::error_code ec;
        std::filesystem::remove(temp_path, ec);
        error = "Failed to allocate packet";
        return AG_INTERNAL_ERROR;
    }

    bool failed = false;
    while (av_read_frame(in_ctx, pkt) >= 0) {
        if (pkt->stream_index >= 0
            && pkt->stream_index < static_cast<int>(out_ctx->nb_streams)) {
            AVStream* in_stream = in_ctx->streams[pkt->stream_index];
            AVStream* out_stream = out_ctx->streams[pkt->stream_index];
            av_packet_rescale_ts(pkt, in_stream->time_base, out_stream->time_base);
            pkt->pos = -1;
            if (av_interleaved_write_frame(out_ctx, pkt) < 0) {
                error = "Failed to write packet";
                failed = true;
                av_packet_unref(pkt);
                break;
            }
        }
        av_packet_unref(pkt);
    }

    if (av_write_trailer(out_ctx) < 0) {
        failed = true;
        error = "Failed to write trailer";
    }
    av_packet_free(&pkt);

    if (failed) {
        avformat_close_input(&in_ctx);
        cleanup_output(out_ctx);
        std::error_code ec;
        std::filesystem::remove(temp_path, ec);
        if (error.empty()) {
            error = "Metadata write failed";
        }
        return AG_IO_ERROR;
    }

    avformat_close_input(&in_ctx);
    cleanup_output(out_ctx);

    // Backup original file before replacing.
    const std::filesystem::path original_path(utf8_path);
    const std::string backup_path = (original_path.parent_path()
                                     / (original_path.filename().string() + ".agbak"))
                                        .string();
    {
        std::error_code ec;
        std::filesystem::remove(backup_path, ec);
        std::filesystem::copy_file(original_path, backup_path,
                                   std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            std::error_code remove_ec;
            std::filesystem::remove(temp_path, remove_ec);
            error = "Failed to create backup";
            return AG_IO_ERROR;
        }
    }

    if (!atomic_replace(temp_path, utf8_path)) {
        std::error_code ec;
        std::filesystem::remove(temp_path, ec);
        error = "Failed to replace original file";
        return AG_IO_ERROR;
    }

    return AG_OK;
}

} // namespace agplayer
