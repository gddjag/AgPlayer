#include "multitrack_editor.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace agplayer {

namespace {

bool is_cancelled(const std::atomic_bool* cancelled) noexcept
{
    return cancelled != nullptr
           && cancelled->load(std::memory_order_relaxed);
}

struct LayoutGuard {
    AVChannelLayout layout{};
    ~LayoutGuard() { av_channel_layout_uninit(&layout); }
};

struct DecoderState {
    AVFormatContext* fmt_ctx = nullptr;
    const AVCodec* codec = nullptr;
    AVCodecContext* ctx = nullptr;
    int stream_index = -1;

    ~DecoderState()
    {
        if (ctx != nullptr) avcodec_free_context(&ctx);
        if (fmt_ctx != nullptr) avformat_close_input(&fmt_ctx);
    }
};

struct EncoderState {
    AVFormatContext* fmt_ctx = nullptr;
    const AVCodec* codec = nullptr;
    AVCodecContext* ctx = nullptr;
    AVStream* stream = nullptr;
    SwrContext* swr = nullptr;
    AVChannelLayout out_ch_layout{};

    ~EncoderState()
    {
        if (swr != nullptr) swr_free(&swr);
        av_channel_layout_uninit(&out_ch_layout);
        if (ctx != nullptr) avcodec_free_context(&ctx);
        if (fmt_ctx != nullptr) {
            if (fmt_ctx->pb != nullptr) avio_closep(&fmt_ctx->pb);
            avformat_free_context(fmt_ctx);
        }
    }
};

AVSampleFormat pick_sample_fmt(const AVCodec* codec)
{
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4996)
#endif
    if (codec->sample_fmts != nullptr) {
        return codec->sample_fmts[0];
    }
#ifdef _MSC_VER
#pragma warning(pop)
#endif
    return AV_SAMPLE_FMT_FLTP;
}

ag_result open_decoder(const std::string& path, DecoderState& d,
                       std::string& error)
{
    if (avformat_open_input(&d.fmt_ctx, path.c_str(), nullptr, nullptr) < 0) {
        error = "Failed to open input file";
        return AG_IO_ERROR;
    }
    if (avformat_find_stream_info(d.fmt_ctx, nullptr) < 0) {
        error = "Failed to find stream info";
        return AG_DECODE_ERROR;
    }
    d.stream_index = av_find_best_stream(d.fmt_ctx, AVMEDIA_TYPE_AUDIO,
                                         -1, -1, &d.codec, 0);
    if (d.stream_index < 0 || d.codec == nullptr) {
        error = "No audio stream found";
        return AG_UNSUPPORTED_FORMAT;
    }
    d.ctx = avcodec_alloc_context3(d.codec);
    if (d.ctx == nullptr) {
        error = "Failed to allocate decoder context";
        return AG_INTERNAL_ERROR;
    }
    if (avcodec_parameters_to_context(d.ctx,
            d.fmt_ctx->streams[d.stream_index]->codecpar) < 0) {
        error = "Failed to copy decoder parameters";
        return AG_DECODE_ERROR;
    }
    d.ctx->thread_count = 1;
    if (avcodec_open2(d.ctx, d.codec, nullptr) < 0) {
        error = "Failed to open decoder";
        return AG_DECODE_ERROR;
    }
    return AG_OK;
}

ag_result open_encoder(const std::string& output_path,
                       const std::string& codec_name,
                       int master_sample_rate,
                       const AVChannelLayout& master_layout,
                       EncoderState& enc,
                       std::string& error)
{
    if (avformat_alloc_output_context2(&enc.fmt_ctx, nullptr, nullptr,
                                       output_path.c_str()) < 0
        || enc.fmt_ctx == nullptr) {
        error = "Failed to allocate output context";
        return AG_INTERNAL_ERROR;
    }

    if (!codec_name.empty()) {
        enc.codec = avcodec_find_encoder_by_name(codec_name.c_str());
    } else {
        enc.codec = avcodec_find_encoder(enc.fmt_ctx->oformat->audio_codec);
    }
    if (enc.codec == nullptr) {
        error = "Encoder not found";
        return AG_UNSUPPORTED_FORMAT;
    }

    enc.stream = avformat_new_stream(enc.fmt_ctx, enc.codec);
    if (enc.stream == nullptr) {
        error = "Failed to create output stream";
        return AG_INTERNAL_ERROR;
    }

    enc.ctx = avcodec_alloc_context3(enc.codec);
    if (enc.ctx == nullptr) {
        error = "Failed to allocate encoder context";
        return AG_INTERNAL_ERROR;
    }

    enc.ctx->sample_fmt = pick_sample_fmt(enc.codec);
    enc.ctx->sample_rate = master_sample_rate;
    enc.ctx->bit_rate = 0;
    enc.ctx->thread_count = 1;
    av_channel_layout_copy(&enc.out_ch_layout, &master_layout);
    av_channel_layout_copy(&enc.ctx->ch_layout, &enc.out_ch_layout);

    if (enc.fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
        enc.ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    if (avcodec_open2(enc.ctx, enc.codec, nullptr) < 0) {
        error = "Failed to open encoder";
        return AG_INTERNAL_ERROR;
    }
    if (avcodec_parameters_from_context(enc.stream->codecpar, enc.ctx) < 0) {
        error = "Failed to copy encoder parameters to stream";
        return AG_INTERNAL_ERROR;
    }
    enc.stream->time_base = enc.ctx->time_base;

    SwrContext* swr = swr_alloc();
    if (swr == nullptr) {
        error = "Failed to allocate SwrContext";
        return AG_INTERNAL_ERROR;
    }
    AVChannelLayout in_ch_layout{};
    av_channel_layout_copy(&in_ch_layout, &master_layout);
    AVChannelLayout out_ch_layout_copy{};
    av_channel_layout_copy(&out_ch_layout_copy, &enc.ctx->ch_layout);

    av_opt_set_chlayout(swr, "in_chlayout", &in_ch_layout, 0);
    av_opt_set_chlayout(swr, "out_chlayout", &out_ch_layout_copy, 0);
    av_opt_set_int(swr, "in_sample_rate", master_sample_rate, 0);
    av_opt_set_int(swr, "out_sample_rate", enc.ctx->sample_rate, 0);
    av_opt_set_sample_fmt(swr, "in_sample_fmt", AV_SAMPLE_FMT_FLTP, 0);
    av_opt_set_sample_fmt(swr, "out_sample_fmt", enc.ctx->sample_fmt, 0);

    av_channel_layout_uninit(&in_ch_layout);
    av_channel_layout_uninit(&out_ch_layout_copy);

    if (swr_init(swr) < 0) {
        swr_free(&swr);
        error = "Failed to initialize SwrContext";
        return AG_INTERNAL_ERROR;
    }
    enc.swr = swr;
    return AG_OK;
}

bool encode_frame(AVCodecContext* enc_ctx, AVFormatContext* fmt_ctx,
                  AVFrame* frame, std::string& error)
{
    if (avcodec_send_frame(enc_ctx, frame) < 0) {
        error = "Failed to send frame to encoder";
        return false;
    }
    AVPacket* pkt = av_packet_alloc();
    if (pkt == nullptr) {
        error = "Failed to allocate packet";
        return false;
    }
    bool ok = true;
    while (true) {
        const int ret = avcodec_receive_packet(enc_ctx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0) {
            error = "Failed to receive packet from encoder";
            ok = false;
            break;
        }
        av_packet_rescale_ts(pkt, enc_ctx->time_base,
                             fmt_ctx->streams[0]->time_base);
        pkt->stream_index = 0;
        if (av_interleaved_write_frame(fmt_ctx, pkt) < 0) {
            error = "Failed to write output packet";
            ok = false;
            break;
        }
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
    return ok;
}

ag_result render_track_to_temp(const std::string& input_path,
                               int master_sample_rate,
                               const AVChannelLayout& master_layout,
                               const MultiTrackEditConfig::Track& track_config,
                               const std::atomic_bool* cancelled,
                               const std::filesystem::path& temp_path,
                               int64_t& out_sample_count,
                               std::string& error)
{
    DecoderState dec;
    ag_result r = open_decoder(input_path, dec, error);
    if (r != AG_OK) return r;

    const int channels = master_layout.nb_channels;
    const int64_t trim_start_req = (track_config.trim_start_ms > 0)
        ? track_config.trim_start_ms * master_sample_rate / 1000
        : 0;
    const int64_t trim_end_req = (track_config.trim_end_ms > 0)
        ? track_config.trim_end_ms * master_sample_rate / 1000
        : std::numeric_limits<int64_t>::max();
    const int64_t target_written = (track_config.trim_end_ms > 0)
        ? std::max<int64_t>(0, trim_end_req - trim_start_req)
        : std::numeric_limits<int64_t>::max();

    SwrContext* swr = swr_alloc();
    if (swr == nullptr) {
        error = "Failed to allocate SwrContext";
        return AG_INTERNAL_ERROR;
    }

    AVChannelLayout in_ch_layout{};
    av_channel_layout_copy(&in_ch_layout, &dec.ctx->ch_layout);
    AVChannelLayout out_ch_layout{};
    av_channel_layout_copy(&out_ch_layout, &master_layout);

    av_opt_set_chlayout(swr, "in_chlayout", &in_ch_layout, 0);
    av_opt_set_int(swr, "in_sample_rate", dec.ctx->sample_rate, 0);
    av_opt_set_sample_fmt(swr, "in_sample_fmt", dec.ctx->sample_fmt, 0);
    av_opt_set_chlayout(swr, "out_chlayout", &out_ch_layout, 0);
    av_opt_set_int(swr, "out_sample_rate", master_sample_rate, 0);
    av_opt_set_sample_fmt(swr, "out_sample_fmt", AV_SAMPLE_FMT_FLTP, 0);

    av_channel_layout_uninit(&in_ch_layout);
    av_channel_layout_uninit(&out_ch_layout);

    if (swr_init(swr) < 0) {
        swr_free(&swr);
        error = "Failed to initialize SwrContext";
        return AG_INTERNAL_ERROR;
    }

    std::ofstream out_file(temp_path, std::ios::binary | std::ios::trunc);
    if (!out_file) {
        swr_free(&swr);
        error = "Failed to create temporary track file";
        return AG_IO_ERROR;
    }

    const float gain = static_cast<float>(track_config.gain);
    AVPacket* in_pkt = av_packet_alloc();
    AVFrame* in_frame = av_frame_alloc();
    AVFrame* resampled = av_frame_alloc();

    int64_t decoded_samples = 0;
    int64_t written_samples = 0;
    bool failed = false;
    bool trim_end_reached = false;

    auto write_resampled_frame = [&](AVFrame* frame) -> bool {
        if (frame == nullptr || frame->nb_samples <= 0) return true;

        av_frame_unref(resampled);
        resampled->format = AV_SAMPLE_FMT_FLTP;
        resampled->sample_rate = master_sample_rate;
        av_channel_layout_copy(&resampled->ch_layout, &master_layout);
        const int estimated = swr_get_out_samples(swr, frame->nb_samples);
        resampled->nb_samples = estimated + 32;
        if (av_frame_get_buffer(resampled, 0) < 0) {
            error = "Failed to allocate resampled frame";
            return false;
        }

        const int out_samples = swr_convert(swr,
            resampled->data, resampled->nb_samples,
            (const uint8_t**)frame->data, frame->nb_samples);
        if (out_samples < 0) {
            error = "swr_convert failed";
            return false;
        }
        if (out_samples == 0) return true;

        std::vector<float> interleaved(
            static_cast<size_t>(out_samples) * channels);
        for (int i = 0; i < out_samples; ++i) {
            for (int ch = 0; ch < channels; ++ch) {
                interleaved[static_cast<size_t>(i) * channels + ch] =
                    reinterpret_cast<float*>(resampled->data[ch])[i] * gain;
            }
        }

        if (decoded_samples + out_samples <= trim_start_req) {
            decoded_samples += out_samples;
            return true;
        }

        int64_t write_start_in_frame = 0;
        if (decoded_samples < trim_start_req) {
            write_start_in_frame = trim_start_req - decoded_samples;
        }
        int64_t write_count =
            static_cast<int64_t>(out_samples) - write_start_in_frame;

        if (track_config.trim_end_ms > 0) {
            const int64_t remaining = target_written - written_samples;
            if (remaining <= 0) {
                trim_end_reached = true;
                decoded_samples += out_samples;
                return true;
            }
            if (write_count > remaining) {
                write_count = remaining;
            }
        }

        if (write_count > 0) {
            const float* src = interleaved.data()
                + static_cast<size_t>(write_start_in_frame) * channels;
            out_file.write(
                reinterpret_cast<const char*>(src),
                static_cast<std::streamsize>(
                    write_count * channels * sizeof(float)));
            if (!out_file) {
                error = "Failed to write temporary track data";
                return false;
            }
            written_samples += write_count;
            if (track_config.trim_end_ms > 0
                && written_samples >= target_written) {
                trim_end_reached = true;
            }
        }

        decoded_samples += out_samples;
        return true;
    };

    while (!is_cancelled(cancelled)) {
        const int read_ret = av_read_frame(dec.fmt_ctx, in_pkt);
        if (read_ret == AVERROR_EOF) break;
        if (read_ret < 0) {
            error = "Failed to read input packet";
            failed = true;
            break;
        }
        if (in_pkt->stream_index != dec.stream_index) {
            av_packet_unref(in_pkt);
            continue;
        }

        if (avcodec_send_packet(dec.ctx, in_pkt) < 0) {
            error = "Failed to send packet to decoder";
            failed = true;
            av_packet_unref(in_pkt);
            break;
        }
        av_packet_unref(in_pkt);

        while (true) {
            const int recv_ret = avcodec_receive_frame(dec.ctx, in_frame);
            if (recv_ret == AVERROR(EAGAIN) || recv_ret == AVERROR_EOF) break;
            if (recv_ret < 0) {
                error = "Failed to decode frame";
                failed = true;
                break;
            }
            if (!write_resampled_frame(in_frame)) {
                failed = true;
                av_frame_unref(in_frame);
                break;
            }
            av_frame_unref(in_frame);
        }
        if (failed) break;
        if (trim_end_reached) break;
    }

    if (!failed && !is_cancelled(cancelled) && !trim_end_reached) {
        avcodec_send_packet(dec.ctx, nullptr);
        while (true) {
            const int recv_ret = avcodec_receive_frame(dec.ctx, in_frame);
            if (recv_ret == AVERROR(EAGAIN) || recv_ret == AVERROR_EOF) break;
            if (recv_ret < 0) break;
            if (!write_resampled_frame(in_frame)) {
                failed = true;
                av_frame_unref(in_frame);
                break;
            }
            av_frame_unref(in_frame);
        }
    }

    if (!failed && !is_cancelled(cancelled) && !trim_end_reached) {
        while (true) {
            const int pending = swr_get_out_samples(swr, 0);
            if (pending <= 0) break;

            av_frame_unref(resampled);
            resampled->format = AV_SAMPLE_FMT_FLTP;
            resampled->sample_rate = master_sample_rate;
            av_channel_layout_copy(&resampled->ch_layout, &master_layout);
            resampled->nb_samples = pending;
            if (av_frame_get_buffer(resampled, 0) < 0) break;

            const int got = swr_convert(swr,
                resampled->data, pending, nullptr, 0);
            if (got <= 0) break;
            if (!write_resampled_frame(resampled)) {
                failed = true;
                break;
            }
            av_frame_unref(resampled);
        }
    }

    swr_free(&swr);
    av_packet_free(&in_pkt);
    av_frame_free(&in_frame);
    av_frame_free(&resampled);
    out_file.close();

    if (failed) return AG_INTERNAL_ERROR;
    if (is_cancelled(cancelled)) {
        error = "Multitrack edit cancelled";
        return AG_CANCELLED;
    }

    const int64_t trim_start_sample =
        std::min(trim_start_req, decoded_samples);
    const int64_t trim_end_sample = (track_config.trim_end_ms > 0)
        ? std::min(trim_end_req, decoded_samples)
        : decoded_samples;
    if (trim_start_sample >= trim_end_sample) {
        error = "Trim start must be before trim end";
        return AG_INVALID_ARGUMENT;
    }
    const int64_t edited_samples = trim_end_sample - trim_start_sample;

    const int64_t fade_in_req = static_cast<int64_t>(track_config.fade_in_ms)
                                * master_sample_rate / 1000;
    const int64_t fade_out_req = static_cast<int64_t>(track_config.fade_out_ms)
                                 * master_sample_rate / 1000;
    int64_t fade_in_samples = std::min(fade_in_req, edited_samples);
    int64_t fade_out_samples = std::min(fade_out_req, edited_samples);
    const bool whole_faded = (fade_in_samples == edited_samples
                              && fade_out_samples == edited_samples);

    std::fstream fs(temp_path, std::ios::in | std::ios::out | std::ios::binary);
    if (!fs) {
        error = "Failed to open temporary track file for fade";
        return AG_IO_ERROR;
    }

    constexpr int64_t k_fade_block = 4096;
    std::vector<float> buf(static_cast<size_t>(k_fade_block) * channels);
    for (int64_t offset = 0; offset < edited_samples; offset += k_fade_block) {
        if (is_cancelled(cancelled)) {
            error = "Multitrack edit cancelled";
            return AG_CANCELLED;
        }
        const int64_t chunk = std::min(k_fade_block, edited_samples - offset);
        const std::streamsize bytes = static_cast<std::streamsize>(
            chunk * channels * sizeof(float));
        fs.seekg(static_cast<std::streamoff>(
            offset * channels * sizeof(float)));
        fs.read(reinterpret_cast<char*>(buf.data()), bytes);
        if (fs.gcount() != bytes) {
            error = "Failed to read temporary track data for fade";
            failed = true;
            break;
        }
        if (whole_faded) {
            std::fill(buf.begin(),
                      buf.begin() + static_cast<size_t>(chunk) * channels,
                      0.0f);
        } else {
            for (int64_t i = 0; i < chunk; ++i) {
                const int64_t idx = offset + i;
                float env = 1.0f;
                if (fade_in_samples > 0 && idx < fade_in_samples) {
                    env *= static_cast<float>(idx)
                           / static_cast<float>(fade_in_samples);
                }
                if (fade_out_samples > 0
                    && idx >= edited_samples - fade_out_samples) {
                    const int64_t pos =
                        idx - (edited_samples - fade_out_samples);
                    env *= 1.0f - static_cast<float>(pos)
                                / static_cast<float>(fade_out_samples);
                }
                for (int ch = 0; ch < channels; ++ch) {
                    buf[static_cast<size_t>(i) * channels + ch] *= env;
                }
            }
        }
        fs.seekp(static_cast<std::streamoff>(
            offset * channels * sizeof(float)));
        fs.write(reinterpret_cast<const char*>(buf.data()), bytes);
        if (!fs) {
            error = "Failed to write temporary track data after fade";
            failed = true;
            break;
        }
    }

    out_sample_count = edited_samples;
    return failed ? AG_INTERNAL_ERROR : AG_OK;
}

struct TempTrackInfo {
    std::filesystem::path path;
    int64_t sample_count = 0;
};

struct TempFileReader {
    std::ifstream file;
    int64_t sample_count = 0;
    int channels = 0;
};

} // namespace

ag_result multitrack_edit(const MultiTrackEditConfig& config,
                          const std::atomic_bool* cancelled,
                          std::function<void(float)> progress_callback,
                          std::string& error)
{
    if (config.output_path.empty()) {
        error = "Output path is empty";
        return AG_INVALID_ARGUMENT;
    }
    if (config.tracks.empty()) {
        error = "No tracks";
        return AG_INVALID_ARGUMENT;
    }

    std::error_code path_ec;
    const auto output_canonical = std::filesystem::weakly_canonical(
        std::filesystem::path(config.output_path), path_ec);
    for (const auto& track : config.tracks) {
        if (track.input_path.empty()) continue;
        const auto input_canonical = std::filesystem::weakly_canonical(
            std::filesystem::path(track.input_path), path_ec);
        if (!path_ec && input_canonical == output_canonical) {
            error = "Input and output path must be different";
            return AG_INVALID_ARGUMENT;
        }
    }

    for (const auto& track : config.tracks) {
        if (track.fade_in_ms < 0 || track.fade_out_ms < 0) {
            error = "Fade duration cannot be negative";
            return AG_INVALID_ARGUMENT;
        }
        if (track.trim_start_ms < 0 || track.trim_end_ms < 0) {
            error = "Trim duration cannot be negative";
            return AG_INVALID_ARGUMENT;
        }
    }

    LayoutGuard master_guard;
    int master_sample_rate = 0;
    bool found_master = false;

    for (const auto& track : config.tracks) {
        if (track.input_path.empty()) continue;
        DecoderState probe;
        ag_result r = open_decoder(track.input_path, probe, error);
        if (r != AG_OK) return r;
        master_sample_rate = probe.ctx->sample_rate;
        av_channel_layout_copy(&master_guard.layout, &probe.ctx->ch_layout);
        found_master = true;
        break;
    }

    if (!found_master) {
        error = "No valid input tracks";
        return AG_INVALID_ARGUMENT;
    }

    const AVChannelLayout& master_layout = master_guard.layout;
    const int channels = master_layout.nb_channels;

    std::vector<TempTrackInfo> track_infos;
    track_infos.reserve(config.tracks.size());
    std::vector<std::filesystem::path> temp_files;

    const size_t valid_track_count = std::count_if(
        config.tracks.begin(), config.tracks.end(),
        [](const MultiTrackEditConfig::Track& t) {
            return !t.input_path.empty();
        });

    size_t decoded_count = 0;
    for (size_t i = 0; i < config.tracks.size(); ++i) {
        if (is_cancelled(cancelled)) {
            error = "Multitrack edit cancelled";
            return AG_CANCELLED;
        }

        if (config.tracks[i].input_path.empty()) {
            track_infos.push_back(TempTrackInfo{});
            continue;
        }

        std::filesystem::path temp_path = output_canonical;
        temp_path += ".agtmp" + std::to_string(i) + ".pcm";
        temp_files.push_back(temp_path);

        TempTrackInfo info;
        info.path = temp_path;
        ag_result r = render_track_to_temp(
            config.tracks[i].input_path,
            master_sample_rate,
            master_layout,
            config.tracks[i],
            cancelled,
            temp_path,
            info.sample_count,
            error);
        if (r != AG_OK) {
            std::error_code ec;
            for (const auto& p : temp_files) std::filesystem::remove(p, ec);
            std::filesystem::remove(config.output_path, ec);
            return r;
        }

        track_infos.push_back(std::move(info));
        ++decoded_count;

        if (progress_callback && valid_track_count > 0) {
            float frac = 0.5f * static_cast<float>(decoded_count)
                               / static_cast<float>(valid_track_count);
            if (frac > 0.5f) frac = 0.5f;
            progress_callback(frac);
        }
    }

    int64_t max_samples = 0;
    for (const auto& info : track_infos) {
        max_samples = std::max(max_samples, info.sample_count);
    }

    if (max_samples == 0) {
        error = "No audio data";
        std::error_code ec;
        for (const auto& p : temp_files) std::filesystem::remove(p, ec);
        std::filesystem::remove(config.output_path, ec);
        return AG_INVALID_ARGUMENT;
    }

    ag_result result = AG_OK;
    bool header_written = false;

    {
        EncoderState enc;
        ag_result r = open_encoder(config.output_path, "", master_sample_rate,
                                   master_layout, enc, error);
        if (r != AG_OK) {
            result = r;
        } else {
            if (!(enc.fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
                if (avio_open(&enc.fmt_ctx->pb, config.output_path.c_str(),
                              AVIO_FLAG_WRITE) < 0) {
                    error = "Failed to open output file";
                    result = AG_IO_ERROR;
                }
            }

            if (result == AG_OK) {
                if (avformat_write_header(enc.fmt_ctx, nullptr) < 0) {
                    error = "Failed to write output header";
                    result = AG_INTERNAL_ERROR;
                } else {
                    header_written = true;
                }
            }

            if (result == AG_OK) {
                constexpr int frame_size = 1024;
                int64_t encoded_samples = 0;
                int64_t out_pts = 0;

                AVFrame* flt_frame = av_frame_alloc();
                AVFrame* out_frame = av_frame_alloc();
                if (flt_frame == nullptr || out_frame == nullptr) {
                    error = "Failed to allocate frame";
                    result = AG_INTERNAL_ERROR;
                } else {
                    std::vector<TempFileReader> readers;
                    readers.reserve(track_infos.size());
                    for (const auto& info : track_infos) {
                        TempFileReader reader;
                        reader.sample_count = info.sample_count;
                        reader.channels = channels;
                        if (info.sample_count > 0 && !info.path.empty()) {
                            reader.file.open(info.path, std::ios::binary);
                        }
                        readers.push_back(std::move(reader));
                    }

                    std::vector<float> interleaved(
                        static_cast<size_t>(frame_size) * channels);

                    while (result == AG_OK && encoded_samples < max_samples
                           && !is_cancelled(cancelled)) {
                        const int samples_this_frame = static_cast<int>(
                            std::min<int64_t>(
                                static_cast<int64_t>(frame_size),
                                max_samples - encoded_samples));

                        av_frame_unref(flt_frame);
                        flt_frame->format = AV_SAMPLE_FMT_FLTP;
                        flt_frame->sample_rate = master_sample_rate;
                        av_channel_layout_copy(&flt_frame->ch_layout,
                                               &master_layout);
                        flt_frame->nb_samples = samples_this_frame;
                        if (av_frame_get_buffer(flt_frame, 0) < 0) {
                            error = "Failed to allocate mix frame buffer";
                            result = AG_INTERNAL_ERROR;
                            break;
                        }

                        for (int ch = 0; ch < channels; ++ch) {
                            std::memset(flt_frame->data[ch], 0,
                                        samples_this_frame * sizeof(float));
                        }

                        for (auto& reader : readers) {
                            if (!reader.file.is_open()
                                || reader.sample_count <= encoded_samples) {
                                continue;
                            }
                            const int64_t readable = std::min<int64_t>(
                                samples_this_frame,
                                reader.sample_count - encoded_samples);
                            const std::streamsize bytes =
                                static_cast<std::streamsize>(
                                    readable * reader.channels * sizeof(float));
                            reader.file.seekg(static_cast<std::streamoff>(
                                encoded_samples * reader.channels
                                * sizeof(float)));
                            reader.file.read(
                                reinterpret_cast<char*>(interleaved.data()),
                                bytes);
                            if (reader.file.gcount() != bytes) {
                                error = "Failed to read temporary track data";
                                result = AG_INTERNAL_ERROR;
                                break;
                            }
                            for (int ch = 0; ch < channels; ++ch) {
                                float* dst = reinterpret_cast<float*>(
                                    flt_frame->data[ch]);
                                for (int i = 0; i < readable; ++i) {
                                    dst[i] += interleaved[
                                        static_cast<size_t>(i) * channels + ch];
                                }
                            }
                        }
                        if (result != AG_OK) break;

                        for (int ch = 0; ch < channels; ++ch) {
                            float* dst = reinterpret_cast<float*>(
                                flt_frame->data[ch]);
                            for (int i = 0; i < samples_this_frame; ++i) {
                                if (dst[i] > 1.0f) dst[i] = 1.0f;
                                else if (dst[i] < -1.0f) dst[i] = -1.0f;
                            }
                        }

                        const int out_samples = swr_get_out_samples(
                            enc.swr, samples_this_frame);
                        if (out_samples < 0) {
                            error = "swr_get_out_samples failed";
                            result = AG_INTERNAL_ERROR;
                            break;
                        }

                        av_frame_unref(out_frame);
                        out_frame->format = enc.ctx->sample_fmt;
                        out_frame->sample_rate = enc.ctx->sample_rate;
                        av_channel_layout_copy(&out_frame->ch_layout,
                                               &enc.ctx->ch_layout);
                        out_frame->nb_samples = out_samples;
                        out_frame->pts = out_pts;
                        if (av_frame_get_buffer(out_frame, 0) < 0) {
                            error = "Failed to allocate output frame buffer";
                            result = AG_INTERNAL_ERROR;
                            break;
                        }

                        const int converted = swr_convert(
                            enc.swr,
                            out_frame->data, out_samples,
                            (const uint8_t**)flt_frame->data,
                            samples_this_frame);
                        if (converted < 0) {
                            av_frame_unref(out_frame);
                            error = "swr_convert failed";
                            result = AG_INTERNAL_ERROR;
                            break;
                        }
                        out_frame->nb_samples = converted;
                        out_pts += converted;

                        if (!encode_frame(enc.ctx, enc.fmt_ctx, out_frame,
                                          error)) {
                            av_frame_unref(out_frame);
                            result = AG_INTERNAL_ERROR;
                            break;
                        }
                        av_frame_unref(out_frame);

                        encoded_samples += samples_this_frame;

                        if (progress_callback) {
                            float frac = 0.5f + 0.5f
                                * static_cast<float>(encoded_samples)
                                / static_cast<float>(max_samples);
                            if (frac > 1.0f) frac = 1.0f;
                            progress_callback(frac);
                        }
                    }

                    if (result == AG_OK && !is_cancelled(cancelled)) {
                        while (true) {
                            const int pending = swr_get_out_samples(enc.swr, 0);
                            if (pending <= 0) break;

                            av_frame_unref(out_frame);
                            out_frame->format = enc.ctx->sample_fmt;
                            out_frame->sample_rate = enc.ctx->sample_rate;
                            av_channel_layout_copy(&out_frame->ch_layout,
                                                   &enc.ctx->ch_layout);
                            out_frame->nb_samples = pending;
                            out_frame->pts = out_pts;
                            if (av_frame_get_buffer(out_frame, 0) < 0) {
                                error = "Failed to allocate output drain frame "
                                        "buffer";
                                result = AG_INTERNAL_ERROR;
                                break;
                            }

                            const int got = swr_convert(enc.swr,
                                out_frame->data, pending, nullptr, 0);
                            if (got <= 0) break;
                            out_frame->nb_samples = got;
                            out_pts += got;

                            if (!encode_frame(enc.ctx, enc.fmt_ctx, out_frame,
                                              error)) {
                                av_frame_unref(out_frame);
                                result = AG_INTERNAL_ERROR;
                                break;
                            }
                            av_frame_unref(out_frame);
                        }
                    }

                    if (result == AG_OK && !is_cancelled(cancelled)) {
                        if (!encode_frame(enc.ctx, enc.fmt_ctx, nullptr,
                                          error)) {
                            result = AG_INTERNAL_ERROR;
                        }
                    }

                    if (progress_callback && result == AG_OK
                        && !is_cancelled(cancelled)) {
                        progress_callback(1.0f);
                    }

                    av_frame_free(&flt_frame);
                    av_frame_free(&out_frame);
                }
            }
        }

        if (header_written) {
            av_write_trailer(enc.fmt_ctx);
        }
    }

    std::error_code ec;
    for (const auto& p : temp_files) std::filesystem::remove(p, ec);

    if (result != AG_OK || is_cancelled(cancelled)) {
        std::filesystem::remove(config.output_path, ec);
        if (result == AG_OK && is_cancelled(cancelled)) {
            error = "Multitrack edit cancelled";
            return AG_CANCELLED;
        }
        return result;
    }

    return AG_OK;
}

} // namespace agplayer
