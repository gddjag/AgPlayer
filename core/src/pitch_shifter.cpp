#define _USE_MATH_DEFINES
#include "pitch_shifter.hpp"
#include "ffmpeg_codec_support.hpp"
#include "time_pitch_engine.hpp"

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

    // 2. Decode to the source sample rate. SoundTouch performs pitch and
    // tempo processing on interleaved float samples without changing the
    // encoder clock.
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

    const int out_sample_rate = dec.ctx->sample_rate;

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

    // 4. Apply production-grade interleaved pitch/tempo processing.
    if (progress_callback) progress_callback(0.5f);

    const std::size_t input_frames = channel_data.empty()
        ? 0U : channel_data.front().size();
    if (input_frames == 0U) {
        error = "Input contains no decodable audio samples";
        return AG_INTERNAL_ERROR;
    }

    std::vector<float> interleaved(input_frames * static_cast<std::size_t>(channels));
    for (std::size_t frame = 0; frame < input_frames; ++frame) {
        for (int ch = 0; ch < channels; ++ch) {
            interleaved[frame * static_cast<std::size_t>(channels)
                        + static_cast<std::size_t>(ch)] = channel_data[ch][frame];
        }
    }

    auto processor = create_time_pitch_engine();
    if (processor == nullptr || !processor->configure(out_sample_rate, channels)
        || !processor->setTempoRatio(config.tempo_ratio)
        || (config.keep_tempo
                ? !processor->setPitchCents(config.pitch_cents)
                : !processor->setRateRatio(pitch_ratio))
        || !processor->setFormantPreservation(config.vocal_protection)) {
        error = "Invalid time/pitch processor configuration";
        return AG_INVALID_ARGUMENT;
    }

    std::vector<float> processed;
    processed.reserve(static_cast<std::size_t>(std::ceil(
        static_cast<long double>(interleaved.size()) / config.tempo_ratio)));
    constexpr unsigned int receive_frames = 4096;
    std::vector<float> receive_buffer(
        static_cast<std::size_t>(receive_frames)
        * static_cast<std::size_t>(channels));
    const auto drain_processor = [&] {
        while (!is_cancelled(cancelled)) {
            const std::size_t received = processor->receive(
                receive_buffer.data(), receive_frames);
            if (processor->failed()) return false;
            if (received == 0U) return true;
            processed.insert(processed.end(), receive_buffer.begin(),
                receive_buffer.begin()
                    + static_cast<std::ptrdiff_t>(received)
                        * static_cast<std::ptrdiff_t>(channels));
        }
        return !processor->failed();
    };
    constexpr std::size_t put_frames = 4'096U;
    for (std::size_t offset = 0U; offset < input_frames
         && !is_cancelled(cancelled); offset += put_frames) {
        const std::size_t count = std::min(put_frames, input_frames - offset);
        processor->put(interleaved.data()
                           + offset * static_cast<std::size_t>(channels),
                       count);
        if (processor->failed() || !drain_processor()) {
            error = "Time/pitch processor failed while accepting audio";
            return AG_INTERNAL_ERROR;
        }
    }
    processor->flush();
    if (processor->failed() || !drain_processor()) {
        error = "Time/pitch processor failed while flushing audio";
        return AG_INTERNAL_ERROR;
    }
    if (is_cancelled(cancelled)) {
        error = "Pitch shift cancelled";
        return AG_CANCELLED;
    }

    const std::size_t processed_frames =
        processed.size() / static_cast<std::size_t>(channels);
    std::vector<std::vector<float>> stretched(
        static_cast<std::size_t>(channels),
        std::vector<float>(processed_frames));
    for (std::size_t frame = 0; frame < processed_frames; ++frame) {
        for (int ch = 0; ch < channels; ++ch) {
            stretched[ch][frame] =
                processed[frame * static_cast<std::size_t>(channels)
                          + static_cast<std::size_t>(ch)];
        }
    }

    const int enc_sample_rate = out_sample_rate;

    if (config.smooth_transition) {
        const std::size_t fade_samples = std::min<std::size_t>(
            static_cast<std::size_t>(std::max(1, enc_sample_rate / 50)),
            stretched.empty() ? 0U : stretched.front().size() / 2U);
        const double half_pi = std::acos(-1.0) / 2.0;
        for (auto& channel : stretched) {
            for (std::size_t i = 0; i < fade_samples; ++i) {
                const double phase =
                    static_cast<double>(i + 1U)
                    / static_cast<double>(fade_samples);
                const float gain =
                    static_cast<float>(std::sin(phase * half_pi));
                channel[i] *= gain;
                channel[channel.size() - 1U - i] *= gain;
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
    const AVSampleFormat enc_sample_fmt = pick_supported_sample_format(
        enc_codec, dec.ctx->sample_fmt);

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
