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
#include <string>
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

void build_channel_layout(AVChannelLayout& layout, int channels)
{
    av_channel_layout_default(&layout, channels > 0 ? channels : 2);
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
    build_channel_layout(enc.out_ch_layout, master_layout.nb_channels);
    enc.ctx->ch_layout = enc.out_ch_layout;

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

ag_result decode_track(const std::string& input_path,
                       int master_sample_rate,
                       const AVChannelLayout& master_layout,
                       const MultiTrackEditConfig::Track& track_config,
                       const std::atomic_bool* cancelled,
                       std::vector<std::vector<float>>& channel_data,
                       std::string& error)
{
    DecoderState dec;
    ag_result r = open_decoder(input_path, dec, error);
    if (r != AG_OK) return r;

    const int channels = master_layout.nb_channels;
    channel_data.assign(channels, {});

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

    AVPacket* in_pkt = av_packet_alloc();
    AVFrame* in_frame = av_frame_alloc();
    AVFrame* resampled = av_frame_alloc();

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

            av_frame_unref(resampled);
            resampled->format = AV_SAMPLE_FMT_FLTP;
            resampled->sample_rate = master_sample_rate;
            av_channel_layout_copy(&resampled->ch_layout, &master_layout);
            const int estimated = swr_get_out_samples(swr, in_frame->nb_samples);
            resampled->nb_samples = estimated + 32;
            if (av_frame_get_buffer(resampled, 0) < 0) {
                error = "Failed to allocate resampled frame";
                failed = true;
                av_frame_unref(in_frame);
                break;
            }

            const int out_samples = swr_convert(swr,
                resampled->data, resampled->nb_samples,
                (const uint8_t**)in_frame->data, in_frame->nb_samples);
            if (out_samples < 0) {
                error = "swr_convert failed";
                failed = true;
                av_frame_unref(in_frame);
                break;
            }

            for (int ch = 0; ch < channels; ++ch) {
                const float* src = reinterpret_cast<float*>(resampled->data[ch]);
                channel_data[ch].insert(channel_data[ch].end(),
                    src, src + out_samples);
            }
            av_frame_unref(in_frame);
        }
        if (failed) break;
    }

    if (!failed && !is_cancelled(cancelled)) {
        avcodec_send_packet(dec.ctx, nullptr);
        while (true) {
            const int recv_ret = avcodec_receive_frame(dec.ctx, in_frame);
            if (recv_ret == AVERROR(EAGAIN) || recv_ret == AVERROR_EOF) break;
            if (recv_ret < 0) break;

            av_frame_unref(resampled);
            resampled->format = AV_SAMPLE_FMT_FLTP;
            resampled->sample_rate = master_sample_rate;
            av_channel_layout_copy(&resampled->ch_layout, &master_layout);
            const int estimated = swr_get_out_samples(swr, in_frame->nb_samples);
            resampled->nb_samples = estimated + 32;
            if (av_frame_get_buffer(resampled, 0) < 0) {
                av_frame_unref(resampled);
                av_frame_unref(in_frame);
                continue;
            }

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
            av_frame_unref(resampled);
            av_frame_unref(in_frame);
        }
    }

    if (!failed && !is_cancelled(cancelled)) {
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

            for (int ch = 0; ch < channels; ++ch) {
                const float* src = reinterpret_cast<float*>(resampled->data[ch]);
                channel_data[ch].insert(channel_data[ch].end(),
                    src, src + got);
            }
            av_frame_unref(resampled);
        }
    }

    swr_free(&swr);
    av_packet_free(&in_pkt);
    av_frame_free(&in_frame);
    av_frame_free(&resampled);

    if (failed) return AG_INTERNAL_ERROR;
    if (is_cancelled(cancelled)) {
        error = "Multitrack edit cancelled";
        return AG_CANCELLED;
    }

    const int64_t decoded_samples = channel_data.empty()
        ? 0
        : static_cast<int64_t>(channel_data[0].size());
    const int64_t trim_start_sample = (track_config.trim_start_ms > 0)
        ? std::min<int64_t>(track_config.trim_start_ms * master_sample_rate / 1000,
                            decoded_samples)
        : 0;
    const int64_t trim_end_sample = (track_config.trim_end_ms > 0)
        ? std::min<int64_t>(track_config.trim_end_ms * master_sample_rate / 1000,
                            decoded_samples)
        : decoded_samples;

    if (trim_start_sample >= trim_end_sample) {
        error = "Trim start must be before trim end";
        return AG_INVALID_ARGUMENT;
    }

    const int64_t edited_samples = trim_end_sample - trim_start_sample;
    const int64_t fade_in_samples = static_cast<int64_t>(track_config.fade_in_ms)
                                    * master_sample_rate / 1000;
    const int64_t fade_out_samples = static_cast<int64_t>(track_config.fade_out_ms)
                                     * master_sample_rate / 1000;
    const float gain = static_cast<float>(track_config.gain);

    for (int ch = 0; ch < channels; ++ch) {
        if (is_cancelled(cancelled)) {
            error = "Multitrack edit cancelled";
            return AG_CANCELLED;
        }

        std::vector<float>& buf = channel_data[ch];
        if (trim_start_sample > 0) {
            std::memmove(buf.data(), buf.data() + trim_start_sample,
                         edited_samples * sizeof(float));
        }
        buf.resize(edited_samples);

        for (int64_t i = 0; i < edited_samples; ++i) {
            float sample = buf[i] * gain;

            if (track_config.fade_in_ms > 0 && i < fade_in_samples) {
                const float fade = static_cast<float>(i)
                                   / static_cast<float>(fade_in_samples);
                sample *= fade;
            }

            if (track_config.fade_out_ms > 0
                && i >= edited_samples - fade_out_samples) {
                const int64_t fade_pos = i - (edited_samples - fade_out_samples);
                const float fade = 1.0f - static_cast<float>(fade_pos)
                                          / static_cast<float>(fade_out_samples);
                sample *= fade;
            }

            buf[i] = sample;
        }
    }

    return AG_OK;
}

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

    int master_sample_rate = 0;
    AVChannelLayout master_layout{};
    bool found_master = false;

    for (const auto& track : config.tracks) {
        if (track.input_path.empty()) continue;
        DecoderState probe;
        ag_result r = open_decoder(track.input_path, probe, error);
        if (r != AG_OK) return r;
        master_sample_rate = probe.ctx->sample_rate;
        av_channel_layout_copy(&master_layout, &probe.ctx->ch_layout);
        found_master = true;
        break;
    }

    if (!found_master) {
        error = "No valid input tracks";
        return AG_INVALID_ARGUMENT;
    }

    std::vector<std::vector<std::vector<float>>> tracks_data;
    tracks_data.reserve(config.tracks.size());

    const size_t valid_track_count = std::count_if(
        config.tracks.begin(), config.tracks.end(),
        [](const MultiTrackEditConfig::Track& t) {
            return !t.input_path.empty();
        });

    size_t decoded_count = 0;
    for (size_t i = 0; i < config.tracks.size(); ++i) {
        if (is_cancelled(cancelled)) {
            av_channel_layout_uninit(&master_layout);
            error = "Multitrack edit cancelled";
            return AG_CANCELLED;
        }

        if (config.tracks[i].input_path.empty()) {
            tracks_data.emplace_back();
            continue;
        }

        std::vector<std::vector<float>> channel_data;
        ag_result r = decode_track(config.tracks[i].input_path,
                                   master_sample_rate, master_layout,
                                   config.tracks[i], cancelled,
                                   channel_data, error);
        if (r != AG_OK) {
            av_channel_layout_uninit(&master_layout);
            return r;
        }

        tracks_data.push_back(std::move(channel_data));
        ++decoded_count;

        if (progress_callback && valid_track_count > 0) {
            float frac = 0.4f * static_cast<float>(decoded_count)
                              / static_cast<float>(valid_track_count);
            if (frac > 0.4f) frac = 0.4f;
            progress_callback(frac);
        }
    }

    int64_t max_samples = 0;
    for (const auto& track : tracks_data) {
        if (!track.empty() && !track[0].empty()) {
            max_samples = std::max(max_samples,
                                   static_cast<int64_t>(track[0].size()));
        }
    }

    if (max_samples == 0) {
        av_channel_layout_uninit(&master_layout);
        error = "No audio data";
        return AG_INVALID_ARGUMENT;
    }

    if (progress_callback) progress_callback(0.5f);

    EncoderState enc;
    ag_result r = open_encoder(config.output_path, "", master_sample_rate,
                               master_layout, enc, error);
    if (r != AG_OK) {
        av_channel_layout_uninit(&master_layout);
        return r;
    }

    if (!(enc.fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&enc.fmt_ctx->pb, config.output_path.c_str(),
                      AVIO_FLAG_WRITE) < 0) {
            error = "Failed to open output file";
            av_channel_layout_uninit(&master_layout);
            return AG_IO_ERROR;
        }
    }

    if (avformat_write_header(enc.fmt_ctx, nullptr) < 0) {
        error = "Failed to write output header";
        av_channel_layout_uninit(&master_layout);
        return AG_INTERNAL_ERROR;
    }

    const int channels = master_layout.nb_channels;
    constexpr int frame_size = 1024;
    int64_t encoded_samples = 0;
    int64_t out_pts = 0;

    AVFrame* flt_frame = av_frame_alloc();
    AVFrame* out_frame = av_frame_alloc();
    if (flt_frame == nullptr || out_frame == nullptr) {
        error = "Failed to allocate frame";
        av_frame_free(&flt_frame);
        av_frame_free(&out_frame);
        av_write_trailer(enc.fmt_ctx);
        std::error_code ec;
        std::filesystem::remove(config.output_path, ec);
        av_channel_layout_uninit(&master_layout);
        return AG_INTERNAL_ERROR;
    }

    bool failed = false;
    while (encoded_samples < max_samples && !is_cancelled(cancelled)) {
        const int samples_this_frame = static_cast<int>(
            std::min<int64_t>(frame_size, max_samples - encoded_samples));

        av_frame_unref(flt_frame);
        flt_frame->format = AV_SAMPLE_FMT_FLTP;
        flt_frame->sample_rate = master_sample_rate;
        av_channel_layout_copy(&flt_frame->ch_layout, &master_layout);
        flt_frame->nb_samples = samples_this_frame;
        if (av_frame_get_buffer(flt_frame, 0) < 0) {
            error = "Failed to allocate mix frame buffer";
            failed = true;
            break;
        }

        for (int ch = 0; ch < channels; ++ch) {
            float* dst = reinterpret_cast<float*>(flt_frame->data[ch]);
            for (int i = 0; i < samples_this_frame; ++i) {
                double sum = 0.0;
                for (const auto& track : tracks_data) {
                    if (track.empty() || static_cast<size_t>(ch) >= track.size()
                        || track[ch].empty()) {
                        continue;
                    }
                    const int64_t idx = encoded_samples + i;
                    if (idx < static_cast<int64_t>(track[ch].size())) {
                        sum += static_cast<double>(track[ch][idx]);
                    }
                }
                if (sum > 1.0) sum = 1.0;
                if (sum < -1.0) sum = -1.0;
                dst[i] = static_cast<float>(sum);
            }
        }

        const int out_samples = swr_get_out_samples(enc.swr, samples_this_frame);
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
            error = "Failed to allocate output frame buffer";
            failed = true;
            break;
        }

        const int converted = swr_convert(enc.swr,
            out_frame->data, out_samples,
            (const uint8_t**)flt_frame->data, samples_this_frame);
        if (converted < 0) {
            av_frame_unref(out_frame);
            error = "swr_convert failed";
            failed = true;
            break;
        }
        out_frame->nb_samples = converted;
        out_pts += converted;

        if (!encode_frame(enc.ctx, enc.fmt_ctx, out_frame, error)) {
            av_frame_unref(out_frame);
            failed = true;
            break;
        }
        av_frame_unref(out_frame);

        encoded_samples += samples_this_frame;

        if (progress_callback) {
            float frac = 0.5f + 0.5f * static_cast<float>(encoded_samples)
                                   / static_cast<float>(max_samples);
            if (frac > 1.0f) frac = 1.0f;
            progress_callback(frac);
        }
    }

    if (!failed && !is_cancelled(cancelled)) {
        while (true) {
            const int pending = swr_get_out_samples(enc.swr, 0);
            if (pending <= 0) break;

            av_frame_unref(out_frame);
            out_frame->format = enc.ctx->sample_fmt;
            out_frame->sample_rate = enc.ctx->sample_rate;
            av_channel_layout_copy(&out_frame->ch_layout, &enc.ctx->ch_layout);
            out_frame->nb_samples = pending;
            out_frame->pts = out_pts;
            if (av_frame_get_buffer(out_frame, 0) < 0) {
                error = "Failed to allocate output drain frame buffer";
                failed = true;
                break;
            }

            const int got = swr_convert(enc.swr,
                out_frame->data, pending, nullptr, 0);
            if (got <= 0) break;
            out_frame->nb_samples = got;
            out_pts += got;

            if (!encode_frame(enc.ctx, enc.fmt_ctx, out_frame, error)) {
                av_frame_unref(out_frame);
                failed = true;
                break;
            }
            av_frame_unref(out_frame);
        }
    }

    if (!failed && !is_cancelled(cancelled)) {
        if (!encode_frame(enc.ctx, enc.fmt_ctx, nullptr, error)) {
            failed = true;
        }
    }

    av_frame_free(&flt_frame);
    av_frame_free(&out_frame);

    if (progress_callback && !failed && !is_cancelled(cancelled)) {
        progress_callback(1.0f);
    }

    av_write_trailer(enc.fmt_ctx);

    av_channel_layout_uninit(&master_layout);

    if (failed) {
        std::error_code ec;
        std::filesystem::remove(config.output_path, ec);
        return AG_INTERNAL_ERROR;
    }
    if (is_cancelled(cancelled)) {
        std::error_code ec;
        std::filesystem::remove(config.output_path, ec);
        error = "Multitrack edit cancelled";
        return AG_CANCELLED;
    }

    return AG_OK;
}

} // namespace agplayer
