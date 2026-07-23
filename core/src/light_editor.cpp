#include "light_editor.hpp"

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
#include <vector>

namespace agplayer {

namespace {

bool is_cancelled(const std::atomic_bool* cancelled) noexcept
{
    return cancelled != nullptr
           && cancelled->load(std::memory_order_relaxed);
}

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

} // namespace

ag_result light_edit(const std::string& input_path,
                     const LightEditConfig& config,
                     const std::atomic_bool* cancelled,
                     std::function<void(float)> progress_callback,
                     std::string& error)
{
    if (config.output_path.empty()) {
        error = "Output path is empty";
        return AG_INVALID_ARGUMENT;
    }
    if (config.gain < 0.0 || config.gain > 4.0) {
        error = "Gain out of range (0.0..4.0)";
        return AG_INVALID_ARGUMENT;
    }
    if (config.fade_in_ms < 0 || config.fade_out_ms < 0) {
        error = "Fade duration cannot be negative";
        return AG_INVALID_ARGUMENT;
    }

    // 1. Open decoder
    DecoderState dec;
    ag_result r = open_decoder(input_path, dec, error);
    if (r != AG_OK) return r;

    const int channels = dec.ctx->ch_layout.nb_channels;
    const int sample_rate = dec.ctx->sample_rate;
    const double duration_sec = (dec.fmt_ctx->duration != AV_NOPTS_VALUE
        && dec.fmt_ctx->duration > 0)
        ? dec.fmt_ctx->duration / static_cast<double>(AV_TIME_BASE)
        : 0.0;

    // 2. Set up swresample to float32 planar
    SwrContext* swr = swr_alloc();
    if (swr == nullptr) {
        error = "Failed to allocate SwrContext";
        return AG_INTERNAL_ERROR;
    }

    AVChannelLayout in_ch_layout{};
    av_channel_layout_copy(&in_ch_layout, &dec.ctx->ch_layout);
    AVChannelLayout out_ch_layout{};
    av_channel_layout_copy(&out_ch_layout, &dec.ctx->ch_layout);

    av_opt_set_chlayout(swr, "in_chlayout", &in_ch_layout, 0);
    av_opt_set_int(swr, "in_sample_rate", sample_rate, 0);
    av_opt_set_sample_fmt(swr, "in_sample_fmt", dec.ctx->sample_fmt, 0);
    av_opt_set_chlayout(swr, "out_chlayout", &out_ch_layout, 0);
    av_opt_set_int(swr, "out_sample_rate", sample_rate, 0);
    av_opt_set_sample_fmt(swr, "out_sample_fmt", AV_SAMPLE_FMT_FLTP, 0);

    av_channel_layout_uninit(&in_ch_layout);
    av_channel_layout_uninit(&out_ch_layout);

    if (swr_init(swr) < 0) {
        swr_free(&swr);
        error = "Failed to initialize SwrContext";
        return AG_INTERNAL_ERROR;
    }

    // 3. Decode all frames to float32 planar
    std::vector<std::vector<float>> channel_data(channels);

    AVPacket* in_pkt = av_packet_alloc();
    AVFrame* in_frame = av_frame_alloc();

    bool failed = false;
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

        if (progress_callback && duration_sec > 0.0) {
            const double processed = in_pkt->pts != AV_NOPTS_VALUE
                ? in_pkt->pts * av_q2d(dec.fmt_ctx->streams[dec.stream_index]->time_base)
                : 0.0;
            float frac = static_cast<float>(processed / duration_sec) * 0.4f;
            if (frac < 0.0f) frac = 0.0f;
            if (frac > 0.4f) frac = 0.4f;
            progress_callback(frac);
        }

        if (avcodec_send_packet(dec.ctx, in_pkt) < 0) {
            av_packet_unref(in_pkt);
            continue;
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

            AVFrame* resampled = av_frame_alloc();
            resampled->format = AV_SAMPLE_FMT_FLTP;
            resampled->sample_rate = sample_rate;
            av_channel_layout_copy(&resampled->ch_layout, &dec.ctx->ch_layout);
            resampled->nb_samples = in_frame->nb_samples * 2;
            if (av_frame_get_buffer(resampled, 0) < 0) {
                av_frame_unref(resampled);
                av_frame_unref(in_frame);
                error = "Failed to allocate resampled frame";
                failed = true;
                break;
            }
            const int out_samples = swr_convert(swr,
                resampled->data, resampled->nb_samples,
                (const uint8_t**)in_frame->data, in_frame->nb_samples);
            if (out_samples < 0) {
                av_frame_unref(resampled);
                av_frame_unref(in_frame);
                error = "swr_convert failed";
                failed = true;
                break;
            }

            for (int ch = 0; ch < channels; ++ch) {
                const float* src = reinterpret_cast<float*>(resampled->data[ch]);
                channel_data[ch].insert(channel_data[ch].end(),
                    src, src + out_samples);
            }
            av_frame_unref(resampled);
            av_frame_unref(in_frame);
        }
        if (failed) break;
    }

    // Flush decoder
    if (!failed && !is_cancelled(cancelled)) {
        avcodec_send_packet(dec.ctx, nullptr);
        while (true) {
            const int recv_ret = avcodec_receive_frame(dec.ctx, in_frame);
            if (recv_ret == AVERROR(EAGAIN) || recv_ret == AVERROR_EOF) break;
            if (recv_ret < 0) break;

            AVFrame* resampled = av_frame_alloc();
            resampled->format = AV_SAMPLE_FMT_FLTP;
            resampled->sample_rate = sample_rate;
            av_channel_layout_copy(&resampled->ch_layout, &dec.ctx->ch_layout);
            resampled->nb_samples = in_frame->nb_samples * 2;
            if (av_frame_get_buffer(resampled, 0) == 0) {
                const int out_samples = swr_convert(swr,
                    resampled->data, resampled->nb_samples,
                    (const uint8_t**)in_frame->data, in_frame->nb_samples);
                if (out_samples > 0) {
                    for (int ch = 0; ch < channels; ++ch) {
                        const float* src = reinterpret_cast<float*>(resampled->data[ch]);
                        channel_data[ch].insert(channel_data[ch].end(),
                            src, src + out_samples);
                    }
                }
            }
            av_frame_unref(resampled);
            av_frame_unref(in_frame);
        }
    }

    swr_free(&swr);
    av_packet_free(&in_pkt);
    av_frame_free(&in_frame);

    if (failed) return AG_INTERNAL_ERROR;
    if (is_cancelled(cancelled)) {
        error = "Light edit cancelled";
        return AG_CANCELLED;
    }

    // 4. Apply trim
    if (progress_callback) progress_callback(0.4f);

    const int64_t decoded_samples = channel_data[0].size();
    const int64_t trim_start_sample = (config.trim_start_ms > 0)
        ? std::min<int64_t>(config.trim_start_ms * sample_rate / 1000,
                            decoded_samples)
        : 0;
    const int64_t trim_end_sample = (config.trim_end_ms > 0)
        ? std::min<int64_t>(config.trim_end_ms * sample_rate / 1000,
                            decoded_samples)
        : decoded_samples;

    if (trim_start_sample >= trim_end_sample) {
        error = "Trim start must be before trim end";
        return AG_INVALID_ARGUMENT;
    }

    const int64_t edited_samples = trim_end_sample - trim_start_sample;

    // Apply trim + gain + fade in one pass
    const int64_t fade_in_samples = static_cast<int64_t>(config.fade_in_ms)
                                    * sample_rate / 1000;
    const int64_t fade_out_samples = static_cast<int64_t>(config.fade_out_ms)
                                     * sample_rate / 1000;
    const float gain = static_cast<float>(config.gain);

    for (int ch = 0; ch < channels; ++ch) {
        if (is_cancelled(cancelled)) {
            error = "Light edit cancelled";
            return AG_CANCELLED;
        }

        std::vector<float>& buf = channel_data[ch];
        // Trim: move trimmed region to front, then resize
        if (trim_start_sample > 0) {
            std::memmove(buf.data(), buf.data() + trim_start_sample,
                         edited_samples * sizeof(float));
        }
        buf.resize(edited_samples);

        // Apply gain + fades
        for (int64_t i = 0; i < edited_samples; ++i) {
            float sample = buf[i] * gain;

            // Fade in
            if (config.fade_in_ms > 0 && i < fade_in_samples) {
                const float fade = static_cast<float>(i)
                                   / static_cast<float>(fade_in_samples);
                sample *= fade;
            }

            // Fade out
            if (config.fade_out_ms > 0
                && i >= edited_samples - fade_out_samples) {
                const int64_t fade_pos = i - (edited_samples - fade_out_samples);
                const float fade = 1.0f - static_cast<float>(fade_pos)
                                          / static_cast<float>(fade_out_samples);
                sample *= fade;
            }

            buf[i] = sample;
        }
    }

    if (progress_callback) progress_callback(0.6f);

    // 5. Set up encoder (same codec as input)
    AVFormatContext* out_fmt = nullptr;
    if (avformat_alloc_output_context2(&out_fmt, nullptr, nullptr,
                                       config.output_path.c_str()) < 0
        || out_fmt == nullptr) {
        error = "Failed to allocate output context";
        return AG_INTERNAL_ERROR;
    }

    AVCodecContext* enc_ctx = avcodec_alloc_context3(dec.codec);
    if (enc_ctx == nullptr) {
        avformat_free_context(out_fmt);
        error = "Failed to allocate encoder context";
        return AG_INTERNAL_ERROR;
    }

    // Use a sample format the encoder supports
    AVSampleFormat enc_sample_fmt = dec.ctx->sample_fmt;
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4996)
#endif
    if (dec.codec->sample_fmts != nullptr) {
        bool supported = false;
        for (int i = 0; dec.codec->sample_fmts[i] != AV_SAMPLE_FMT_NONE; ++i) {
            if (dec.codec->sample_fmts[i] == enc_sample_fmt) {
                supported = true;
                break;
            }
        }
        if (!supported) {
            enc_sample_fmt = dec.codec->sample_fmts[0];
        }
    }
#ifdef _MSC_VER
#pragma warning(pop)
#endif

    enc_ctx->sample_fmt = enc_sample_fmt;
    enc_ctx->sample_rate = sample_rate;
    av_channel_layout_copy(&enc_ctx->ch_layout, &dec.ctx->ch_layout);
    enc_ctx->bit_rate = dec.ctx->bit_rate;
    enc_ctx->thread_count = 1;
    if (out_fmt->oformat->flags & AVFMT_GLOBALHEADER) {
        enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    if (avcodec_open2(enc_ctx, dec.codec, nullptr) < 0) {
        avcodec_free_context(&enc_ctx);
        avformat_free_context(out_fmt);
        error = "Failed to open encoder";
        return AG_INTERNAL_ERROR;
    }

    AVStream* out_stream = avformat_new_stream(out_fmt, dec.codec);
    if (out_stream == nullptr
        || avcodec_parameters_from_context(out_stream->codecpar, enc_ctx) < 0) {
        avcodec_free_context(&enc_ctx);
        avformat_free_context(out_fmt);
        error = "Failed to create output stream";
        return AG_INTERNAL_ERROR;
    }
    out_stream->time_base = enc_ctx->time_base;

    if (!(out_fmt->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&out_fmt->pb, config.output_path.c_str(),
                      AVIO_FLAG_WRITE) < 0) {
            avcodec_free_context(&enc_ctx);
            avformat_free_context(out_fmt);
            error = "Failed to open output file";
            return AG_IO_ERROR;
        }
    }
    if (avformat_write_header(out_fmt, nullptr) < 0) {
        if (out_fmt->pb) avio_closep(&out_fmt->pb);
        avcodec_free_context(&enc_ctx);
        avformat_free_context(out_fmt);
        error = "Failed to write output header";
        return AG_INTERNAL_ERROR;
    }

    // 6. Convert float32 planar to encoder format and encode
    SwrContext* fmt_swr = swr_alloc();
    AVChannelLayout fmt_in_ch{};
    av_channel_layout_copy(&fmt_in_ch, &enc_ctx->ch_layout);
    AVChannelLayout fmt_out_ch{};
    av_channel_layout_copy(&fmt_out_ch, &enc_ctx->ch_layout);

    av_opt_set_chlayout(fmt_swr, "in_chlayout", &fmt_in_ch, 0);
    av_opt_set_chlayout(fmt_swr, "out_chlayout", &fmt_out_ch, 0);
    av_opt_set_int(fmt_swr, "in_sample_rate", sample_rate, 0);
    av_opt_set_int(fmt_swr, "out_sample_rate", sample_rate, 0);
    av_opt_set_sample_fmt(fmt_swr, "in_sample_fmt", AV_SAMPLE_FMT_FLTP, 0);
    av_opt_set_sample_fmt(fmt_swr, "out_sample_fmt", enc_sample_fmt, 0);

    av_channel_layout_uninit(&fmt_in_ch);
    av_channel_layout_uninit(&fmt_out_ch);

    if (swr_init(fmt_swr) < 0) {
        swr_free(&fmt_swr);
        if (out_fmt->pb) avio_closep(&out_fmt->pb);
        avcodec_free_context(&enc_ctx);
        avformat_free_context(out_fmt);
        error = "Failed to initialize format SwrContext";
        return AG_INTERNAL_ERROR;
    }

    const int frame_size = 1024;
    int64_t encoded_samples = 0;
    AVFrame* out_frame = av_frame_alloc();
    out_frame->format = AV_SAMPLE_FMT_FLTP;
    out_frame->sample_rate = sample_rate;
    av_channel_layout_copy(&out_frame->ch_layout, &enc_ctx->ch_layout);
    out_frame->nb_samples = frame_size;

    int64_t pts = 0;
    while (encoded_samples < edited_samples && !is_cancelled(cancelled)) {
        const int samples_this_frame = static_cast<int>(
            std::min<int64_t>(frame_size, edited_samples - encoded_samples));

        out_frame->nb_samples = samples_this_frame;
        if (av_frame_get_buffer(out_frame, 0) < 0) {
            error = "Failed to allocate output frame buffer";
            failed = true;
            break;
        }

        for (int ch = 0; ch < channels; ++ch) {
            float* dst = reinterpret_cast<float*>(out_frame->data[ch]);
            const float* src = channel_data[ch].data() + encoded_samples;
            std::memcpy(dst, src, samples_this_frame * sizeof(float));
        }

        AVFrame* enc_frame = av_frame_alloc();
        enc_frame->format = enc_sample_fmt;
        enc_frame->sample_rate = sample_rate;
        av_channel_layout_copy(&enc_frame->ch_layout, &enc_ctx->ch_layout);
        enc_frame->nb_samples = samples_this_frame;
        enc_frame->pts = pts;
        pts += samples_this_frame;
        if (av_frame_get_buffer(enc_frame, 0) < 0) {
            av_frame_free(&enc_frame);
            av_frame_unref(out_frame);
            error = "Failed to allocate encoder frame";
            failed = true;
            break;
        }

        const int conv_samples = swr_convert(fmt_swr,
            enc_frame->data, enc_frame->nb_samples,
            (const uint8_t**)out_frame->data, samples_this_frame);
        av_frame_unref(out_frame);

        if (conv_samples <= 0) {
            av_frame_free(&enc_frame);
            continue;
        }
        enc_frame->nb_samples = conv_samples;

        if (!encode_frame(enc_ctx, out_fmt, enc_frame, error)) {
            av_frame_free(&enc_frame);
            failed = true;
            break;
        }
        av_frame_free(&enc_frame);

        encoded_samples += samples_this_frame;

        if (progress_callback) {
            float frac = 0.6f + 0.4f * static_cast<float>(encoded_samples)
                                       / static_cast<float>(edited_samples);
            if (frac > 1.0f) frac = 1.0f;
            progress_callback(frac);
        }
    }

    // Flush encoder
    if (!failed && !is_cancelled(cancelled)) {
        if (!encode_frame(enc_ctx, out_fmt, nullptr, error)) {
            failed = true;
        }
    }

    av_frame_free(&out_frame);
    swr_free(&fmt_swr);

    if (progress_callback && !failed) {
        progress_callback(1.0f);
    }

    av_write_trailer(out_fmt);

    if (out_fmt->pb) avio_closep(&out_fmt->pb);
    avcodec_free_context(&enc_ctx);
    avformat_free_context(out_fmt);

    if (failed) {
        std::error_code ec;
        std::filesystem::remove(config.output_path, ec);
        return AG_INTERNAL_ERROR;
    }
    if (is_cancelled(cancelled)) {
        std::error_code ec;
        std::filesystem::remove(config.output_path, ec);
        error = "Light edit cancelled";
        return AG_CANCELLED;
    }

    return AG_OK;
}

} // namespace agplayer
