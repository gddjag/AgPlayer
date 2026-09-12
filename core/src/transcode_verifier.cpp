#include "transcode_verifier.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
}

#include <algorithm>
#include <cmath>

namespace agplayer {

namespace {

bool has_metadata(const AVDictionary* dictionary)
{
    return dictionary != nullptr && av_dict_count(dictionary) > 0;
}

} // namespace

ag_result verify_transcoded_output(
    const std::string_view utf8_path,
    const TranscodeVerificationPlan& plan,
    TranscodeVerificationResult& result,
    std::string& error)
{
    result = {};
    error.clear();
    const std::string path(utf8_path);
    AVFormatContext* format = nullptr;
    if (path.empty()
        || avformat_open_input(&format, path.c_str(), nullptr, nullptr) < 0
        || format == nullptr) {
        error = "Verification failed: output cannot be opened";
        return AG_IO_ERROR;
    }
    if (avformat_find_stream_info(format, nullptr) < 0) {
        avformat_close_input(&format);
        error = "Verification failed: output stream information is invalid";
        return AG_DECODE_ERROR;
    }

    int audio_streams = 0;
    int audio_index = -1;
    for (unsigned int index = 0; index < format->nb_streams; ++index) {
        const AVStream* stream = format->streams[index];
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            ++audio_streams;
            if (audio_index < 0) {
                audio_index = static_cast<int>(index);
            }
            result.metadata_present = result.metadata_present
                                      || has_metadata(stream->metadata);
        } else if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO
                   && (stream->disposition & AV_DISPOSITION_ATTACHED_PIC) != 0) {
            result.cover_present = true;
        }
    }
    result.metadata_present = result.metadata_present
                              || has_metadata(format->metadata);
    if (audio_index < 0 || audio_streams != plan.expected_audio_streams) {
        avformat_close_input(&format);
        error = "Verification failed: unexpected audio stream count";
        return AG_DECODE_ERROR;
    }
    if (plan.expect_metadata && !result.metadata_present) {
        avformat_close_input(&format);
        error = "Verification failed: metadata was not preserved";
        return AG_DECODE_ERROR;
    }
    if (plan.expect_cover && !result.cover_present) {
        avformat_close_input(&format);
        error = "Verification failed: cover art was not preserved";
        return AG_DECODE_ERROR;
    }

    AVStream* stream = format->streams[audio_index];
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    AVCodecContext* decoder = codec != nullptr
        ? avcodec_alloc_context3(codec) : nullptr;
    if (decoder == nullptr
        || avcodec_parameters_to_context(decoder, stream->codecpar) < 0
        || avcodec_open2(decoder, codec, nullptr) < 0) {
        avcodec_free_context(&decoder);
        avformat_close_input(&format);
        error = "Verification failed: output decoder cannot be opened";
        return AG_DECODE_ERROR;
    }

    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    if (packet == nullptr || frame == nullptr) {
        av_packet_free(&packet);
        av_frame_free(&frame);
        avcodec_free_context(&decoder);
        avformat_close_input(&format);
        error = "Verification failed: decoder allocation failed";
        return AG_INTERNAL_ERROR;
    }

    bool failed = false;
    const auto receive_frames = [&]() {
        while (true) {
            const int receive = avcodec_receive_frame(decoder, frame);
            if (receive == AVERROR(EAGAIN) || receive == AVERROR_EOF) {
                return true;
            }
            if (receive < 0) {
                return false;
            }
            result.decoded_samples += frame->nb_samples;
            av_frame_unref(frame);
        }
    };

    while (av_read_frame(format, packet) >= 0) {
        if (packet->stream_index == audio_index) {
            if (avcodec_send_packet(decoder, packet) < 0
                || !receive_frames()) {
                failed = true;
                av_packet_unref(packet);
                break;
            }
        }
        av_packet_unref(packet);
    }
    if (!failed
        && (avcodec_send_packet(decoder, nullptr) < 0 || !receive_frames())) {
        failed = true;
    }

    const int sample_rate = decoder->sample_rate;
    if (sample_rate > 0) {
        result.decoded_duration_ms =
            result.decoded_samples * 1000 / sample_rate;
    }
    av_packet_free(&packet);
    av_frame_free(&frame);
    avcodec_free_context(&decoder);
    avformat_close_input(&format);

    if (failed || result.decoded_samples <= 0) {
        error = "Verification failed: output could not be decoded through EOF";
        return AG_DECODE_ERROR;
    }
    if (plan.expected_duration_ms > 0) {
        const std::int64_t tolerance = plan.lossless ? 40 : 250;
        if (std::llabs(result.decoded_duration_ms
                       - plan.expected_duration_ms) > tolerance) {
            error = "Verification failed: decoded duration is incomplete"
                " (actual=" + std::to_string(result.decoded_duration_ms)
                + "ms, expected=" + std::to_string(plan.expected_duration_ms)
                + "ms)";
            return AG_DECODE_ERROR;
        }
    }
    return AG_OK;
}

} // namespace agplayer
