#define _USE_MATH_DEFINES
#include "pitch_shifter.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

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

// Hann window coefficient
double hann(int n, int N) noexcept
{
    return 0.5 * (1.0 - std::cos(2.0 * 3.14159265358979323846 * n / (N - 1)));
}

// Simple first-order low-pass or high-pass filter for vocal formant compensation.
class FirstOrderFilter {
public:
    enum class Type { LowPass, HighPass };

    FirstOrderFilter(Type type, double cutoff_hz, double sample_rate) noexcept
    {
        const double omega = 2.0 * M_PI * cutoff_hz / sample_rate;
        const double cos_omega = std::cos(omega);
        const double sin_omega = std::sin(omega);
        const double alpha = sin_omega / (2.0 * 0.707); // Q = 0.707 for gentle slope

        if (type == Type::LowPass) {
            b0_ = (1.0 - cos_omega) / 2.0;
            b1_ = 1.0 - cos_omega;
            b2_ = (1.0 - cos_omega) / 2.0;
        } else {
            b0_ = (1.0 + cos_omega) / 2.0;
            b1_ = -(1.0 + cos_omega);
            b2_ = (1.0 + cos_omega) / 2.0;
        }
        a0_ = 1.0 + alpha;
        a1_ = -2.0 * cos_omega;
        a2_ = 1.0 - alpha;

        b0_ /= a0_; b1_ /= a0_; b2_ /= a0_;
        a1_ /= a0_; a2_ /= a0_; a0_ = 1.0;
    }

    float process(float input) noexcept
    {
        const double output = b0_ * input + b1_ * x1_ + b2_ * x2_
                              - a1_ * y1_ - a2_ * y2_;
        x2_ = x1_;
        x1_ = input;
        y2_ = y1_;
        y1_ = output;
        return static_cast<float>(output);
    }

private:
    double b0_ = 0.0, b1_ = 0.0, b2_ = 0.0;
    double a1_ = 0.0, a2_ = 0.0, a0_ = 1.0;
    double x1_ = 0.0, x2_ = 0.0;
    double y1_ = 0.0, y2_ = 0.0;
};

// OLA (Overlap-Add) time-stretch for a single channel of float samples.
// stretch_factor > 1: stretch (longer), < 1: compress (shorter).
// Returns the stretched samples in a new vector.
std::vector<float> ola_stretch(const float* input, int64_t input_samples,
                                double stretch_factor)
{
    if (input_samples <= 0 || stretch_factor <= 0.0) {
        return {};
    }

    const int frame_size = 1024;
    const int hop_analysis = frame_size / 2;
    const int hop_synthesis = static_cast<int>(std::round(
        static_cast<double>(hop_analysis) * stretch_factor));
    if (hop_synthesis < 1) return {};

    const int64_t out_samples = static_cast<int64_t>(
        std::round(static_cast<double>(input_samples) * stretch_factor));
    std::vector<float> output(out_samples + frame_size, 0.0f);

    // Precompute Hann window
    std::vector<float> window(frame_size);
    for (int i = 0; i < frame_size; ++i) {
        window[i] = static_cast<float>(hann(i, frame_size));
    }

    // Overlap-add: extract windowed frames from input, place at stretched
    // positions in output. The window normalization for 50% overlap is 2.0,
    // but since hop_synthesis != hop_analysis we use a normalization factor
    // based on the actual overlap.
    const double norm_factor = 2.0 / (1.0 + stretch_factor);

    int64_t in_pos = 0;
    int64_t out_pos = 0;
    while (in_pos + frame_size <= input_samples) {
        for (int i = 0; i < frame_size; ++i) {
            if (out_pos + i < static_cast<int64_t>(output.size())) {
                output[out_pos + i] += input[in_pos + i] * window[i]
                                       * static_cast<float>(norm_factor);
            }
        }
        in_pos += hop_analysis;
        out_pos += hop_synthesis;
    }

    output.resize(out_samples);
    return output;
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

ag_result pitch_shift(const std::string& input_path,
                      const PitchShiftConfig& config,
                      const std::atomic_bool* cancelled,
                      std::function<void(float)> progress_callback,
                      std::string& error)
{
    if (config.output_path.empty()) {
        error = "Output path is empty";
        return AG_INVALID_ARGUMENT;
    }
    if (config.pitch_cents < -1200 || config.pitch_cents > 1200) {
        error = "Pitch cents out of range (-1200..1200)";
        return AG_INVALID_ARGUMENT;
    }
    if (config.tempo_ratio < 0.5 || config.tempo_ratio > 2.0) {
        error = "Tempo ratio out of range (0.5..2.0)";
        return AG_INVALID_ARGUMENT;
    }
    if (config.output_sample_rate < 0) {
        error = "Output sample rate must be non-negative";
        return AG_INVALID_ARGUMENT;
    }

    // 1. Open decoder
    DecoderState dec;
    ag_result r = open_decoder(input_path, dec, error);
    if (r != AG_OK) return r;

    const double pitch_ratio = std::pow(2.0, config.pitch_cents / 1200.0);

    // 2. Set up swresample to convert to float32 planar for OLA processing.
    // For keep_tempo mode: resample to R/pitch_ratio first (changes sample
    // count, preserves audio content), then OLA-stretch by pitch_ratio*tempo.
    // For non-keep_tempo mode: no resampling, just OLA-stretch by tempo_ratio.
    SwrContext* swr = swr_alloc();
    if (swr == nullptr) {
        error = "Failed to allocate SwrContext";
        return AG_INTERNAL_ERROR;
    }

    AVChannelLayout in_ch_layout{};
    av_channel_layout_copy(&in_ch_layout, &dec.ctx->ch_layout);

    av_opt_set_chlayout(swr, "in_chlayout", &in_ch_layout, 0);
    av_opt_set_int(swr, "in_sample_rate", dec.ctx->sample_rate, 0);
    av_opt_set_sample_fmt(swr, "in_sample_fmt", dec.ctx->sample_fmt, 0);

    AVChannelLayout out_ch_layout{};
    av_channel_layout_copy(&out_ch_layout, &dec.ctx->ch_layout);

    av_opt_set_chlayout(swr, "out_chlayout", &out_ch_layout, 0);

    // For keep_tempo: resample to R/pitch_ratio so we get N/pitch_ratio samples
    // representing the same audio. Then OLA-stretch back to N*tempo samples.
    // For non-keep_tempo: keep original rate, just OLA-stretch by tempo_ratio.
    int out_sample_rate;
    double stretch_factor;
    if (config.keep_tempo) {
        out_sample_rate = static_cast<int>(
            std::round(dec.ctx->sample_rate / pitch_ratio));
        stretch_factor = pitch_ratio * config.tempo_ratio;
    } else {
        out_sample_rate = dec.ctx->sample_rate;
        stretch_factor = config.tempo_ratio;
    }

    // Avoid 0 sample rate
    if (out_sample_rate < 1) out_sample_rate = 1;

    av_opt_set_int(swr, "out_sample_rate", out_sample_rate, 0);
    av_opt_set_sample_fmt(swr, "out_sample_fmt", AV_SAMPLE_FMT_FLTP, 0);

    av_channel_layout_uninit(&in_ch_layout);
    av_channel_layout_uninit(&out_ch_layout);

    if (swr_init(swr) < 0) {
        swr_free(&swr);
        error = "Failed to initialize SwrContext";
        return AG_INTERNAL_ERROR;
    }

    // 3. Decode all frames and resample to float32 planar
    const int channels = dec.ctx->ch_layout.nb_channels;
    std::vector<std::vector<float>> channel_data(channels);

    AVPacket* in_pkt = av_packet_alloc();
    AVFrame* in_frame = av_frame_alloc();
    const double duration_sec = (dec.fmt_ctx->duration != AV_NOPTS_VALUE
        && dec.fmt_ctx->duration > 0)
        ? dec.fmt_ctx->duration / static_cast<double>(AV_TIME_BASE)
        : 0.0;

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
            float frac = static_cast<float>(processed / duration_sec) * 0.5f;
            if (frac < 0.0f) frac = 0.0f;
            if (frac > 0.5f) frac = 0.5f;
            progress_callback(frac);
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

            // Resample to float32 planar: allocate output frame and convert
            AVFrame* resampled = av_frame_alloc();
            resampled->format = AV_SAMPLE_FMT_FLTP;
            resampled->sample_rate = out_sample_rate;
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

            // Append to channel buffers
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
            resampled->sample_rate = out_sample_rate;
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

    // Flush swresample
    if (!failed && !is_cancelled(cancelled)) {
        AVFrame* flush_frame = av_frame_alloc();
        flush_frame->format = AV_SAMPLE_FMT_FLTP;
        flush_frame->sample_rate = out_sample_rate;
        av_channel_layout_copy(&flush_frame->ch_layout, &dec.ctx->ch_layout);
        flush_frame->nb_samples = 1024;
        while (av_frame_get_buffer(flush_frame, 0) == 0) {
            const int out_samples = swr_convert(swr,
                flush_frame->data, flush_frame->nb_samples,
                nullptr, 0);
            if (out_samples <= 0) break;
            for (int ch = 0; ch < channels; ++ch) {
                const float* src = reinterpret_cast<float*>(flush_frame->data[ch]);
                channel_data[ch].insert(channel_data[ch].end(),
                    src, src + out_samples);
            }
            av_frame_unref(flush_frame);
            flush_frame->nb_samples = 1024;
        }
        av_frame_free(&flush_frame);
    }

    swr_free(&swr);
    av_packet_free(&in_pkt);
    av_frame_free(&in_frame);

    if (failed) {
        return AG_INTERNAL_ERROR;
    }
    if (is_cancelled(cancelled)) {
        error = "Pitch shift cancelled";
        return AG_CANCELLED;
    }

    // 4. Apply OLA time-stretch to each channel
    if (progress_callback) progress_callback(0.5f);

    std::vector<std::vector<float>> stretched(channels);
    for (int ch = 0; ch < channels; ++ch) {
        if (is_cancelled(cancelled)) {
            error = "Pitch shift cancelled";
            return AG_CANCELLED;
        }
        stretched[ch] = ola_stretch(channel_data[ch].data(),
            static_cast<int64_t>(channel_data[ch].size()),
            stretch_factor);
    }

    // For keep_tempo: encode at original rate R (so pitch is shifted by
    // pitch_ratio, since the audio was resampled to R/pitch_ratio)
    // For non-keep_tempo: encode at R*pitch_ratio (pitch and tempo shift
    // together)
    int enc_sample_rate;
    if (config.keep_tempo) {
        enc_sample_rate = dec.ctx->sample_rate;
    } else {
        enc_sample_rate = static_cast<int>(
            std::round(dec.ctx->sample_rate * pitch_ratio));
        if (enc_sample_rate < 1) enc_sample_rate = 1;
    }

    if (config.vocal_protection) {
        if (pitch_ratio > 1.0) {
            // Pitch up: reduce excessive brightness with gentle low-pass.
            const double cutoff = 8000.0 / pitch_ratio;
            FirstOrderFilter lp(FirstOrderFilter::Type::LowPass, cutoff, enc_sample_rate);
            for (int ch = 0; ch < channels; ++ch) {
                for (float& sample : stretched[ch]) {
                    sample = lp.process(sample);
                }
            }
        } else if (pitch_ratio < 1.0) {
            // Pitch down: reduce muffled sound with gentle high-pass.
            const double cutoff = 80.0 * pitch_ratio;
            FirstOrderFilter hp(FirstOrderFilter::Type::HighPass, cutoff, enc_sample_rate);
            for (int ch = 0; ch < channels; ++ch) {
                for (float& sample : stretched[ch]) {
                    sample = hp.process(sample);
                }
            }
        }
    }

    if (progress_callback) progress_callback(0.7f);

    // 5. Set up encoder: same codec as input unless a specific codec is requested
    AVFormatContext* out_fmt = nullptr;
    if (avformat_alloc_output_context2(&out_fmt, nullptr, nullptr,
                                       config.output_path.c_str()) < 0
        || out_fmt == nullptr) {
        error = "Failed to allocate output context";
        return AG_INTERNAL_ERROR;
    }

    const AVCodec* enc_codec = dec.codec;
    if (!config.output_codec_name.empty()) {
        const AVCodec* requested = avcodec_find_encoder_by_name(
            config.output_codec_name.c_str());
        if (requested != nullptr) {
            enc_codec = requested;
        }
    }
    if (enc_codec == nullptr) {
        avformat_free_context(out_fmt);
        error = "Failed to find output encoder";
        return AG_INTERNAL_ERROR;
    }

    AVCodecContext* enc_ctx = avcodec_alloc_context3(enc_codec);
    if (enc_ctx == nullptr) {
        avformat_free_context(out_fmt);
        error = "Failed to allocate encoder context";
        return AG_INTERNAL_ERROR;
    }

    // If a specific output sample rate is requested, the stretched audio is
    // resampled to this rate before encoding.
    const int final_sample_rate = config.output_sample_rate > 0
        ? config.output_sample_rate
        : enc_sample_rate;

    // Use a sample format the encoder supports (prefer original, fallback to FLTP)
    AVSampleFormat enc_sample_fmt = dec.ctx->sample_fmt;
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4996)
#endif
    if (enc_codec->sample_fmts != nullptr) {
        // Check if decoder's format is supported by encoder
        bool supported = false;
        for (int i = 0; enc_codec->sample_fmts[i] != AV_SAMPLE_FMT_NONE; ++i) {
            if (enc_codec->sample_fmts[i] == enc_sample_fmt) {
                supported = true;
                break;
            }
        }
        if (!supported) {
            enc_sample_fmt = enc_codec->sample_fmts[0];
        }
    }
#ifdef _MSC_VER
#pragma warning(pop)
#endif

    enc_ctx->sample_fmt = enc_sample_fmt;
    enc_ctx->sample_rate = final_sample_rate;
    av_channel_layout_copy(&enc_ctx->ch_layout, &dec.ctx->ch_layout);
    enc_ctx->bit_rate = dec.ctx->bit_rate;
    enc_ctx->thread_count = 1;
    if (out_fmt->oformat->flags & AVFMT_GLOBALHEADER) {
        enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    if (avcodec_open2(enc_ctx, enc_codec, nullptr) < 0) {
        avcodec_free_context(&enc_ctx);
        avformat_free_context(out_fmt);
        error = "Failed to open encoder";
        return AG_INTERNAL_ERROR;
    }

    AVStream* out_stream = avformat_new_stream(out_fmt, enc_codec);
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

    // 6. Convert stretched float32 planar back to encoder format and encode
    // Set up a second swresample for format conversion
    SwrContext* fmt_swr = swr_alloc();
    AVChannelLayout fmt_in_ch{};
    av_channel_layout_copy(&fmt_in_ch, &enc_ctx->ch_layout);
    AVChannelLayout fmt_out_ch{};
    av_channel_layout_copy(&fmt_out_ch, &enc_ctx->ch_layout);

    av_opt_set_chlayout(fmt_swr, "in_chlayout", &fmt_in_ch, 0);
    av_opt_set_chlayout(fmt_swr, "out_chlayout", &fmt_out_ch, 0);
    av_opt_set_int(fmt_swr, "in_sample_rate", enc_sample_rate, 0);
    av_opt_set_int(fmt_swr, "out_sample_rate", final_sample_rate, 0);
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

    // Encode in frames of 1024 samples
    const int frame_size = 1024;
    const int64_t total_samples = stretched[0].size();
    int64_t encoded_samples = 0;
    AVFrame* out_frame = av_frame_alloc();

    int64_t pts = 0;
    while (encoded_samples < total_samples && !is_cancelled(cancelled)) {
        const int samples_this_frame = static_cast<int>(
            std::min<int64_t>(frame_size, total_samples - encoded_samples));

        // Prepare float32 planar input
        out_frame->format = AV_SAMPLE_FMT_FLTP;
        out_frame->sample_rate = enc_sample_rate;
        av_channel_layout_copy(&out_frame->ch_layout, &enc_ctx->ch_layout);
        out_frame->nb_samples = samples_this_frame;
        if (av_frame_get_buffer(out_frame, 0) < 0) {
            error = "Failed to allocate output frame buffer";
            failed = true;
            break;
        }

        for (int ch = 0; ch < channels; ++ch) {
            float* dst = reinterpret_cast<float*>(out_frame->data[ch]);
            const float* src = stretched[ch].data() + encoded_samples;
            std::memcpy(dst, src, samples_this_frame * sizeof(float));
        }

        // Convert to encoder format: allocate encoder frame and convert
        AVFrame* enc_frame = av_frame_alloc();
        enc_frame->format = enc_sample_fmt;
        enc_frame->sample_rate = final_sample_rate;
        av_channel_layout_copy(&enc_frame->ch_layout, &enc_ctx->ch_layout);
        enc_frame->nb_samples = samples_this_frame;
        enc_frame->pts = pts;
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
        pts += conv_samples;

        if (!encode_frame(enc_ctx, out_fmt, enc_frame, error)) {
            av_frame_free(&enc_frame);
            failed = true;
            break;
        }
        av_frame_free(&enc_frame);

        encoded_samples += samples_this_frame;

        if (progress_callback) {
            float frac = 0.7f + 0.3f * static_cast<float>(encoded_samples)
                                       / static_cast<float>(total_samples);
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
        error = "Pitch shift cancelled";
        return AG_CANCELLED;
    }

    return AG_OK;
}

} // namespace agplayer
