#include "transcoder.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

namespace agplayer {

namespace {

bool is_cancelled(const std::atomic_bool* cancelled) noexcept
{
    return cancelled != nullptr
           && cancelled->load(std::memory_order_relaxed);
}

// Pick the best sample format supported by the encoder. Falls back to FLTP
// (float planar) which is the most common lossy encoder format, or S16 for PCM.
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

// Map a channel count to a default mono/stereo layout. For >2 channels we
// let av_channel_layout_default build the layout.
void build_channel_layout(AVChannelLayout& layout, int channels)
{
    av_channel_layout_default(&layout, channels > 0 ? channels : 2);
}

struct DecoderState {
    AVFormatContext* fmt_ctx = nullptr;
    const AVCodec* codec = nullptr;
    AVCodecContext* ctx = nullptr;
    int stream_index = -1;

    ~DecoderState()
    {
        if (ctx != nullptr) {
            avcodec_free_context(&ctx);
        }
        if (fmt_ctx != nullptr) {
            avformat_close_input(&fmt_ctx);
        }
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
        if (swr != nullptr) {
            swr_free(&swr);
        }
        av_channel_layout_uninit(&out_ch_layout);
        if (ctx != nullptr) {
            avcodec_free_context(&ctx);
        }
        if (fmt_ctx != nullptr) {
            if (fmt_ctx->pb != nullptr) {
                avio_closep(&fmt_ctx->pb);
            }
            avformat_free_context(fmt_ctx);
        }
    }
};

ag_result open_decoder(const std::string& path, DecoderState& d, std::string& error)
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
    // Single-threaded decode: parallelism is at the file level (QtConcurrent).
    d.ctx->thread_count = 1;
    if (avcodec_open2(d.ctx, d.codec, nullptr) < 0) {
        error = "Failed to open decoder";
        return AG_DECODE_ERROR;
    }
    return AG_OK;
}

ag_result open_encoder(const std::string& output_path,
                       const std::string& codec_name,
                       const DecoderState& d,
                       const TranscodeConfig& config,
                       EncoderState& enc,
                       AVSampleFormat in_sample_fmt,
                       std::string& error)
{
    // Output format guessed from file extension.
    if (avformat_alloc_output_context2(&enc.fmt_ctx, nullptr, nullptr,
                                       output_path.c_str()) < 0
        || enc.fmt_ctx == nullptr) {
        error = "Failed to allocate output context";
        return AG_INTERNAL_ERROR;
    }

    // Find encoder: explicit name first, else default for the container.
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

    // Determine output parameters.
    const int out_channels = config.channels > 0
        ? config.channels : d.ctx->ch_layout.nb_channels;
    const int out_sample_rate = config.sample_rate > 0
        ? config.sample_rate : d.ctx->sample_rate;

    enc.ctx->sample_fmt = pick_sample_fmt(enc.codec);
    enc.ctx->sample_rate = out_sample_rate;
    enc.ctx->bit_rate = config.bit_rate > 0 ? config.bit_rate : 0;
    enc.ctx->thread_count = 1;
    build_channel_layout(enc.out_ch_layout, out_channels);
    enc.ctx->ch_layout = enc.out_ch_layout;

    // Some containers (mp3) need this flag set to allocate the stream
    // correctly when the global header is not present.
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

    // Set up SwrContext for format/rate/channel conversion.
    SwrContext* swr = swr_alloc();
    if (swr == nullptr) {
        error = "Failed to allocate SwrContext";
        return AG_INTERNAL_ERROR;
    }
    AVChannelLayout in_ch_layout{};
    av_channel_layout_copy(&in_ch_layout, &d.ctx->ch_layout);
    AVChannelLayout out_ch_layout_copy{};
    av_channel_layout_copy(&out_ch_layout_copy, &enc.ctx->ch_layout);

    av_opt_set_chlayout(swr, "in_chlayout", &in_ch_layout, 0);
    av_opt_set_chlayout(swr, "out_chlayout", &out_ch_layout_copy, 0);
    av_opt_set_int(swr, "in_sample_rate", d.ctx->sample_rate, 0);
    av_opt_set_int(swr, "out_sample_rate", enc.ctx->sample_rate, 0);
    av_opt_set_sample_fmt(swr, "in_sample_fmt", in_sample_fmt, 0);
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

// Encode one frame. Returns false on error.
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
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        }
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

// Decode the entire audio stream to float32 planar and return the maximum
// absolute sample value. Returns <= 0.0 on error or silence.
double scan_peak(const std::string& input_path,
                 const std::atomic_bool* cancelled,
                 std::string& error)
{
    DecoderState dec;
    ag_result r = open_decoder(input_path, dec, error);
    if (r != AG_OK) return -1.0;

    SwrContext* swr = swr_alloc();
    if (swr == nullptr) {
        error = "Failed to allocate SwrContext";
        return -1.0;
    }

    AVChannelLayout in_ch_layout{};
    av_channel_layout_copy(&in_ch_layout, &dec.ctx->ch_layout);
    AVChannelLayout out_ch_layout{};
    av_channel_layout_copy(&out_ch_layout, &dec.ctx->ch_layout);

    av_opt_set_chlayout(swr, "in_chlayout", &in_ch_layout, 0);
    av_opt_set_int(swr, "in_sample_rate", dec.ctx->sample_rate, 0);
    av_opt_set_sample_fmt(swr, "in_sample_fmt", dec.ctx->sample_fmt, 0);
    av_opt_set_chlayout(swr, "out_chlayout", &out_ch_layout, 0);
    av_opt_set_int(swr, "out_sample_rate", dec.ctx->sample_rate, 0);
    av_opt_set_sample_fmt(swr, "out_sample_fmt", AV_SAMPLE_FMT_FLTP, 0);

    av_channel_layout_uninit(&in_ch_layout);
    av_channel_layout_uninit(&out_ch_layout);

    if (swr_init(swr) < 0) {
        swr_free(&swr);
        error = "Failed to initialize SwrContext";
        return -1.0;
    }

    double peak = 0.0;
    AVPacket* in_pkt = av_packet_alloc();
    AVFrame* in_frame = av_frame_alloc();
    AVFrame* resampled = av_frame_alloc();

    bool sent_null = false;
    bool decoder_eof = false;
    while (!is_cancelled(cancelled) && !decoder_eof) {
        if (!sent_null) {
            const int read_ret = av_read_frame(dec.fmt_ctx, in_pkt);
            if (read_ret == AVERROR_EOF) {
                if (avcodec_send_packet(dec.ctx, nullptr) < 0) {
                    error = "Failed to flush decoder";
                    peak = -1.0;
                    break;
                }
                sent_null = true;
            } else if (read_ret < 0) {
                error = "Failed to read input packet";
                peak = -1.0;
                break;
            } else {
                if (in_pkt->stream_index != dec.stream_index) {
                    av_packet_unref(in_pkt);
                    continue;
                }

                if (avcodec_send_packet(dec.ctx, in_pkt) < 0) {
                    error = "Failed to send packet to decoder";
                    peak = -1.0;
                    av_packet_unref(in_pkt);
                    break;
                }
                av_packet_unref(in_pkt);
            }
        }

        while (true) {
            const int recv_ret = avcodec_receive_frame(dec.ctx, in_frame);
            if (recv_ret == AVERROR(EAGAIN)) break;
            if (recv_ret == AVERROR_EOF) {
                decoder_eof = true;
                break;
            }
            if (recv_ret < 0) {
                error = "Failed to decode frame";
                peak = -1.0;
                break;
            }

            av_frame_unref(resampled);
            resampled->format = AV_SAMPLE_FMT_FLTP;
            resampled->sample_rate = dec.ctx->sample_rate;
            av_channel_layout_copy(&resampled->ch_layout, &dec.ctx->ch_layout);
            resampled->nb_samples = in_frame->nb_samples * 2;
            if (av_frame_get_buffer(resampled, 0) < 0) {
                error = "Failed to allocate resampled frame";
                peak = -1.0;
                av_frame_unref(in_frame);
                break;
            }

            const int out_samples = swr_convert(swr,
                resampled->data, resampled->nb_samples,
                (const uint8_t**)in_frame->data, in_frame->nb_samples);
            if (out_samples < 0) {
                error = "swr_convert failed";
                peak = -1.0;
                av_frame_unref(in_frame);
                break;
            }

            const int channels = dec.ctx->ch_layout.nb_channels;
            for (int ch = 0; ch < channels; ++ch) {
                const float* src = reinterpret_cast<float*>(resampled->data[ch]);
                for (int i = 0; i < out_samples; ++i) {
                    const double abs_sample = std::abs(static_cast<double>(src[i]));
                    if (abs_sample > peak) peak = abs_sample;
                }
            }
            av_frame_unref(in_frame);
        }
        if (peak < 0.0) break;
    }

    av_frame_free(&resampled);
    av_packet_free(&in_pkt);
    av_frame_free(&in_frame);
    swr_free(&swr);

    if (is_cancelled(cancelled)) {
        error = "Transcode cancelled";
        return -1.0;
    }
    return peak;
}

ag_result run_transcode_pass(const std::string& input_path,
                             const TranscodeConfig& config,
                             const std::atomic_bool* cancelled,
                             std::function<void(float)> progress_callback,
                             std::string& error,
                             double gain = 1.0)
{
    if (config.output_path.empty()) {
        error = "Output path is empty";
        return AG_INVALID_ARGUMENT;
    }
    std::error_code path_ec;
    if (std::filesystem::equivalent(input_path, config.output_path, path_ec) ||
        std::filesystem::canonical(input_path, path_ec)
            == std::filesystem::canonical(config.output_path, path_ec)) {
        error = "Input and output path must be different";
        return AG_INVALID_ARGUMENT;
    }

    DecoderState dec;
    ag_result r = open_decoder(input_path, dec, error);
    if (r != AG_OK) return r;

    const bool apply_gain = (gain != 1.0);

    EncoderState enc;
    r = open_encoder(config.output_path, config.codec_name, dec, config, enc,
                     apply_gain ? AV_SAMPLE_FMT_FLTP : dec.ctx->sample_fmt,
                     error);
    if (r != AG_OK) return r;

    if (config.keep_metadata) {
        av_dict_copy(&enc.fmt_ctx->metadata, dec.fmt_ctx->metadata, 0);
    }

    // Open output file.
    if (!(enc.fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&enc.fmt_ctx->pb, config.output_path.c_str(),
                      AVIO_FLAG_WRITE) < 0) {
            error = "Failed to open output file";
            return AG_IO_ERROR;
        }
    }

    if (avformat_write_header(enc.fmt_ctx, nullptr) < 0) {
        error = "Failed to write output header";
        return AG_INTERNAL_ERROR;
    }

    // Set up input -> FLTP conversion only when gain is actually applied.
    SwrContext* in_to_flt_swr = nullptr;
    if (apply_gain) {
        in_to_flt_swr = swr_alloc();
        if (in_to_flt_swr == nullptr) {
            error = "Failed to allocate input-to-FLTP SwrContext";
            av_write_trailer(enc.fmt_ctx);
            if (enc.fmt_ctx->pb != nullptr) {
                avio_closep(&enc.fmt_ctx->pb);
            }
            std::error_code ec;
            std::filesystem::remove(config.output_path, ec);
            return AG_INTERNAL_ERROR;
        }
        AVChannelLayout flt_ch_layout{};
        av_channel_layout_copy(&flt_ch_layout, &dec.ctx->ch_layout);
        av_opt_set_chlayout(in_to_flt_swr, "in_chlayout", &flt_ch_layout, 0);
        av_opt_set_chlayout(in_to_flt_swr, "out_chlayout", &flt_ch_layout, 0);
        av_opt_set_int(in_to_flt_swr, "in_sample_rate", dec.ctx->sample_rate, 0);
        av_opt_set_int(in_to_flt_swr, "out_sample_rate", dec.ctx->sample_rate, 0);
        av_opt_set_sample_fmt(in_to_flt_swr, "in_sample_fmt", dec.ctx->sample_fmt, 0);
        av_opt_set_sample_fmt(in_to_flt_swr, "out_sample_fmt", AV_SAMPLE_FMT_FLTP, 0);
        av_channel_layout_uninit(&flt_ch_layout);

        if (swr_init(in_to_flt_swr) < 0) {
            swr_free(&in_to_flt_swr);
            error = "Failed to initialize input-to-FLTP SwrContext";
            av_write_trailer(enc.fmt_ctx);
            if (enc.fmt_ctx->pb != nullptr) {
                avio_closep(&enc.fmt_ctx->pb);
            }
            std::error_code ec;
            std::filesystem::remove(config.output_path, ec);
            return AG_INTERNAL_ERROR;
        }
    }

    // Estimate total duration for progress.
    const double duration_sec = (dec.fmt_ctx->duration != AV_NOPTS_VALUE
        && dec.fmt_ctx->duration > 0)
        ? dec.fmt_ctx->duration / static_cast<double>(AV_TIME_BASE)
        : 0.0;

    AVPacket* in_pkt = av_packet_alloc();
    AVFrame* in_frame = av_frame_alloc();
    AVFrame* flt_frame = av_frame_alloc();
    AVFrame* out_frame = av_frame_alloc();
    if (in_pkt == nullptr || in_frame == nullptr || flt_frame == nullptr
        || out_frame == nullptr) {
        if (in_pkt) av_packet_free(&in_pkt);
        if (in_frame) av_frame_free(&in_frame);
        if (flt_frame) av_frame_free(&flt_frame);
        if (out_frame) av_frame_free(&out_frame);
        swr_free(&in_to_flt_swr);
        av_write_trailer(enc.fmt_ctx);
        error = "Failed to allocate packet/frame";
        return AG_INTERNAL_ERROR;
    }
    AVAudioFifo* audio_fifo = av_audio_fifo_alloc(
        enc.ctx->sample_fmt, enc.ctx->ch_layout.nb_channels, 1);
    if (audio_fifo == nullptr) {
        av_packet_free(&in_pkt);
        av_frame_free(&in_frame);
        av_frame_free(&flt_frame);
        av_frame_free(&out_frame);
        av_audio_fifo_free(audio_fifo);
        swr_free(&in_to_flt_swr);
        error = "Failed to allocate audio FIFO";
        return AG_INTERNAL_ERROR;
    }

    bool failed = false;
    int64_t out_pts = 0;

    auto encode_fifo = [&](bool drain) -> bool {
        const int frame_size = enc.ctx->frame_size;
        while (true) {
            const int available = av_audio_fifo_size(audio_fifo);
            if (available <= 0) {
                return true;
            }
            if (frame_size > 0 && available < frame_size && !drain) {
                return true;
            }

            int frame_samples = frame_size > 0 ? frame_size : available;
            if (drain && available < frame_samples
                && (enc.codec->capabilities & AV_CODEC_CAP_SMALL_LAST_FRAME)) {
                frame_samples = available;
            }
            const int read_samples = std::min(available, frame_samples);

            av_frame_unref(out_frame);
            out_frame->format = enc.ctx->sample_fmt;
            out_frame->sample_rate = enc.ctx->sample_rate;
            av_channel_layout_copy(&out_frame->ch_layout, &enc.ctx->ch_layout);
            out_frame->nb_samples = frame_samples;
            out_frame->pts = out_pts;
            if (av_frame_get_buffer(out_frame, 0) < 0) {
                error = "Failed to allocate encoder frame buffer";
                return false;
            }
            if (av_audio_fifo_read(
                    audio_fifo,
                    reinterpret_cast<void**>(out_frame->extended_data),
                    read_samples)
                != read_samples) {
                error = "Failed to read encoder audio FIFO";
                return false;
            }
            if (read_samples < frame_samples
                && av_samples_set_silence(
                       out_frame->extended_data, read_samples,
                       frame_samples - read_samples,
                       enc.ctx->ch_layout.nb_channels,
                       enc.ctx->sample_fmt)
                    < 0) {
                error = "Failed to pad final encoder frame";
                return false;
            }
            out_pts += frame_samples;
            if (!encode_frame(enc.ctx, enc.fmt_ctx, out_frame, error)) {
                return false;
            }
        }
    };

    auto queue_converted = [&](AVFrame* frame, int samples) -> bool {
        if (samples <= 0) {
            return true;
        }
        const int required = av_audio_fifo_size(audio_fifo) + samples;
        if (av_audio_fifo_realloc(audio_fifo, required) < 0) {
            error = "Failed to grow encoder audio FIFO";
            return false;
        }
        if (av_audio_fifo_write(
                audio_fifo, reinterpret_cast<void**>(frame->extended_data),
                samples)
            != samples) {
            error = "Failed to write encoder audio FIFO";
            return false;
        }
        return encode_fifo(false);
    };

    // Convert a decoded frame to the encoder's input format and encode it.
    // In the gain path this means input -> FLTP -> gain -> output.
    // In the no-gain path this is a single-step input -> output conversion.
    auto convert_and_encode = [&](AVFrame* frame) -> bool {
        AVFrame* src_for_enc = frame;
        int src_samples = frame->nb_samples;

        if (apply_gain) {
            const int flt_samples = swr_get_out_samples(in_to_flt_swr,
                frame->nb_samples);
            if (flt_samples < 0) {
                error = "swr_get_out_samples (input->FLTP) failed";
                return false;
            }

            av_frame_unref(flt_frame);
            flt_frame->format = AV_SAMPLE_FMT_FLTP;
            flt_frame->sample_rate = dec.ctx->sample_rate;
            av_channel_layout_copy(&flt_frame->ch_layout, &dec.ctx->ch_layout);
            flt_frame->nb_samples = flt_samples;
            if (av_frame_get_buffer(flt_frame, 0) < 0) {
                error = "Failed to allocate FLTP frame buffer";
                return false;
            }

            const int converted_flt = swr_convert(in_to_flt_swr,
                flt_frame->data, flt_samples,
                (const uint8_t**)frame->data, frame->nb_samples);
            if (converted_flt < 0) {
                error = "swr_convert (input->FLTP) failed";
                return false;
            }
            flt_frame->nb_samples = converted_flt;

            // Apply volume normalization gain.
            const int channels = dec.ctx->ch_layout.nb_channels;
            for (int ch = 0; ch < channels; ++ch) {
                float* src = reinterpret_cast<float*>(flt_frame->data[ch]);
                for (int i = 0; i < converted_flt; ++i) {
                    src[i] = static_cast<float>(
                        static_cast<double>(src[i]) * gain);
                }
            }

            src_for_enc = flt_frame;
            src_samples = converted_flt;
        }

        // Convert src_for_enc to output format.
        const int out_samples = swr_get_out_samples(enc.swr, src_samples);
        if (out_samples < 0) {
            error = "swr_get_out_samples failed";
            return false;
        }

        av_frame_unref(out_frame);
        out_frame->format = enc.ctx->sample_fmt;
        out_frame->sample_rate = enc.ctx->sample_rate;
        av_channel_layout_copy(&out_frame->ch_layout, &enc.ctx->ch_layout);
        out_frame->nb_samples = out_samples;
        if (av_frame_get_buffer(out_frame, 0) < 0) {
            error = "Failed to allocate output frame buffer";
            return false;
        }

        const int converted = swr_convert(enc.swr,
            out_frame->data, out_samples,
            (const uint8_t**)src_for_enc->data, src_samples);
        if (converted < 0) {
            av_frame_unref(out_frame);
            error = "swr_convert failed";
            return false;
        }
        out_frame->nb_samples = converted;
        if (!queue_converted(out_frame, converted)) {
            av_frame_unref(out_frame);
            return false;
        }

        av_frame_unref(out_frame);
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

        // Report progress based on input position.
        if (progress_callback && duration_sec > 0.0) {
            const double processed = in_pkt->pts != AV_NOPTS_VALUE
                ? in_pkt->pts * av_q2d(dec.fmt_ctx->streams[dec.stream_index]->time_base)
                : 0.0;
            float frac = static_cast<float>(processed / duration_sec);
            if (frac < 0.0f) frac = 0.0f;
            if (frac > 1.0f) frac = 1.0f;
            progress_callback(frac);
        }

        // Send packet to decoder.
        if (avcodec_send_packet(dec.ctx, in_pkt) < 0) {
            error = "Failed to send packet to decoder";
            failed = true;
            av_packet_unref(in_pkt);
            break;
        }
        av_packet_unref(in_pkt);

        // Receive decoded frames and encode.
        while (true) {
            const int recv_ret = avcodec_receive_frame(dec.ctx, in_frame);
            if (recv_ret == AVERROR(EAGAIN) || recv_ret == AVERROR_EOF) break;
            if (recv_ret < 0) {
                error = "Failed to decode frame";
                failed = true;
                break;
            }

            if (!convert_and_encode(in_frame)) {
                av_frame_unref(in_frame);
                failed = true;
                break;
            }
            av_frame_unref(in_frame);
        }
        if (failed) break;
    }

    if (!failed && is_cancelled(cancelled)) {
        av_packet_free(&in_pkt);
        av_frame_free(&in_frame);
        av_frame_free(&flt_frame);
        av_frame_free(&out_frame);
        swr_free(&in_to_flt_swr);
        av_write_trailer(enc.fmt_ctx);
        if (enc.fmt_ctx->pb != nullptr) {
            avio_closep(&enc.fmt_ctx->pb);
        }
        std::error_code ec;
        std::filesystem::remove(config.output_path, ec);
        error = "Transcode cancelled";
        return AG_CANCELLED;
    }

    // Flush decoder.
    if (!failed) {
        avcodec_send_packet(dec.ctx, nullptr);
        while (true) {
            const int recv_ret = avcodec_receive_frame(dec.ctx, in_frame);
            if (recv_ret == AVERROR(EAGAIN) || recv_ret == AVERROR_EOF) break;
            if (recv_ret < 0) {
                error = "Failed to flush decoder";
                failed = true;
                break;
            }
            if (!convert_and_encode(in_frame)) {
                av_frame_unref(in_frame);
                failed = true;
                break;
            }
            av_frame_unref(in_frame);
        }
    }

    // In the FLTP path, drain the resamplers so no trailing samples are lost.
    if (!failed && apply_gain) {
        // Drain input -> FLTP resampler.
        while (true) {
            const int pending = swr_get_out_samples(in_to_flt_swr, 0);
            if (pending < 0) {
                error = "swr_get_out_samples (drain input->FLTP) failed";
                failed = true;
                break;
            }
            if (pending == 0) break;

            av_frame_unref(flt_frame);
            flt_frame->format = AV_SAMPLE_FMT_FLTP;
            flt_frame->sample_rate = dec.ctx->sample_rate;
            av_channel_layout_copy(&flt_frame->ch_layout, &dec.ctx->ch_layout);
            flt_frame->nb_samples = pending;
            if (av_frame_get_buffer(flt_frame, 0) < 0) {
                error = "Failed to allocate FLTP drain frame buffer";
                failed = true;
                break;
            }

            const int got = swr_convert(in_to_flt_swr,
                flt_frame->data, pending, nullptr, 0);
            if (got < 0) {
                error = "swr_convert (drain input->FLTP) failed";
                failed = true;
                break;
            }
            if (got == 0) break;
            flt_frame->nb_samples = got;

            // Apply gain to drained FLTP samples.
            const int channels = dec.ctx->ch_layout.nb_channels;
            for (int ch = 0; ch < channels; ++ch) {
                float* src = reinterpret_cast<float*>(flt_frame->data[ch]);
                for (int i = 0; i < got; ++i) {
                    src[i] = static_cast<float>(
                        static_cast<double>(src[i]) * gain);
                }
            }

            // Feed drained FLTP through the encoder resampler and encode.
            const int out_samples = swr_get_out_samples(enc.swr, got);
            if (out_samples < 0) {
                error = "swr_get_out_samples failed";
                failed = true;
                break;
            }

            av_frame_unref(out_frame);
            out_frame->format = enc.ctx->sample_fmt;
            out_frame->sample_rate = enc.ctx->sample_rate;
            av_channel_layout_copy(&out_frame->ch_layout, &enc.ctx->ch_layout);
            out_frame->nb_samples = out_samples;
            out_frame->pts = out_pts;
            if (av_frame_get_buffer(out_frame, 0) < 0) {
                error = "Failed to allocate output drain frame buffer";
                failed = true;
                break;
            }

            const int converted = swr_convert(enc.swr,
                out_frame->data, out_samples,
                (const uint8_t**)flt_frame->data, got);
            if (converted < 0) {
                av_frame_unref(out_frame);
                error = "swr_convert failed";
                failed = true;
                break;
            }
            out_frame->nb_samples = converted;
            if (!queue_converted(out_frame, converted)) {
                av_frame_unref(out_frame);
                failed = true;
                break;
            }
            av_frame_unref(out_frame);
        }

    }

    // Drain the final resampler for both gain and no-gain paths.
    while (!failed) {
        const int pending = swr_get_out_samples(enc.swr, 0);
        if (pending < 0) {
            error = "swr_get_out_samples (drain encoder) failed";
            failed = true;
            break;
        }
        if (pending == 0) break;

        av_frame_unref(out_frame);
        out_frame->format = enc.ctx->sample_fmt;
        out_frame->sample_rate = enc.ctx->sample_rate;
        av_channel_layout_copy(&out_frame->ch_layout, &enc.ctx->ch_layout);
        out_frame->nb_samples = pending;
        if (av_frame_get_buffer(out_frame, 0) < 0) {
            error = "Failed to allocate output drain frame buffer";
            failed = true;
            break;
        }

        const int got = swr_convert(
            enc.swr, out_frame->data, pending, nullptr, 0);
        if (got < 0) {
            av_frame_unref(out_frame);
            error = "swr_convert (drain encoder) failed";
            failed = true;
            break;
        }
        if (got == 0) break;
        out_frame->nb_samples = got;
        if (!queue_converted(out_frame, got)) {
            av_frame_unref(out_frame);
            failed = true;
            break;
        }
        av_frame_unref(out_frame);
    }

    // Flush encoder.
    if (!failed) {
        if (!encode_fifo(true)
            || !encode_frame(enc.ctx, enc.fmt_ctx, nullptr, error)) {
            failed = true;
        }
    }

    av_packet_free(&in_pkt);
    av_frame_free(&in_frame);
    av_frame_free(&flt_frame);
    av_frame_free(&out_frame);
    av_audio_fifo_free(audio_fifo);
    swr_free(&in_to_flt_swr);

    if (progress_callback && !failed) {
        progress_callback(1.0f);
    }

    av_write_trailer(enc.fmt_ctx);

    if (failed) {
        if (enc.fmt_ctx->pb != nullptr) {
            avio_closep(&enc.fmt_ctx->pb);
        }
        std::error_code ec;
        std::filesystem::remove(config.output_path, ec);
        return AG_INTERNAL_ERROR;
    }

    return AG_OK;
}

} // namespace

ag_result transcode(const std::string& input_path,
                    const TranscodeConfig& config,
                    const std::atomic_bool* cancelled,
                    std::function<void(float)> progress_callback,
                    std::string& error)
{
    double gain = 1.0;
    if (config.volume_normalize) {
        const double peak = scan_peak(input_path, cancelled, error);
        if (peak < 0.0) {
            if (error == "Transcode cancelled" || is_cancelled(cancelled)) {
                return AG_CANCELLED;
            }
            return AG_DECODE_ERROR;
        }
        if (peak > 0.0) {
            constexpr double target_peak = 0.8913; // -1 dBFS
            gain = target_peak / peak;
            if (gain > 1.0) gain = 1.0; // do not amplify if already at/above target
        }
    }
    return run_transcode_pass(input_path, config, cancelled,
                              progress_callback, error, gain);
}

} // namespace agplayer
