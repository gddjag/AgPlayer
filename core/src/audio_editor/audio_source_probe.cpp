#include "audio_source_probe.hpp"

extern "C" {
#include <libavformat/avformat.h>
}

#include <algorithm>
#include <chrono>
#include <string>

namespace agplayer::editor {
namespace {

struct ProbeDeadline final {
    std::chrono::steady_clock::time_point expires;
    const std::atomic_bool* cancelled{};
};

int interruptProbe(void* opaque)
{
    const auto* deadline = static_cast<const ProbeDeadline*>(opaque);
    return deadline != nullptr
        && ((deadline->cancelled
             && deadline->cancelled->load(std::memory_order_acquire))
            || std::chrono::steady_clock::now() >= deadline->expires);
}

bool deadlineExpired(const ProbeDeadline& deadline) noexcept
{
    return (deadline.cancelled
            && deadline.cancelled->load(std::memory_order_acquire))
        || std::chrono::steady_clock::now() >= deadline.expires;
}

} // namespace

bool AudioSourceProbeResult::matchesFormat(const AudioSource& expected) const noexcept
{
    if (!success || source.sample_rate != expected.sample_rate
        || source.channels != expected.channels) {
        return false;
    }
    const SampleFrame difference = source.total_frames >= expected.total_frames
        ? source.total_frames - expected.total_frames
        : expected.total_frames - source.total_frames;
    return difference <= frame_tolerance;
}

AudioSourceProbeResult AudioSourceProbe::probe(const std::filesystem::path& path) noexcept
{
    return probe(path, std::chrono::steady_clock::now() + std::chrono::seconds(2));
}

AudioSourceProbeResult AudioSourceProbe::probe(
    const std::filesystem::path& path,
    const std::chrono::steady_clock::time_point deadline,
    const std::atomic_bool* cancelled) noexcept
{
    AudioSourceProbeResult result;
    ProbeDeadline probeDeadline{deadline, cancelled};
    if (deadlineExpired(probeDeadline)) {
        result.timed_out = true;
        result.message = "audio source probe timed out";
        return result;
    }
    if (path.empty()) {
        result.message = "invalid source path";
        return result;
    }
    try {
        const std::string utf8Path = path.u8string();
        AVFormatContext* context = avformat_alloc_context();
        if (context == nullptr) {
            result.message = "cannot allocate audio probe";
            return result;
        }
        context->interrupt_callback = {interruptProbe, &probeDeadline};
        AVDictionary* options = nullptr;
        av_dict_set_int(&options, "probesize", 5 * 1024 * 1024, 0);
        av_dict_set_int(&options, "analyzeduration", 2'000'000, 0);
        const int openResult = avformat_open_input(
            &context, utf8Path.c_str(), nullptr, &options);
        av_dict_free(&options);
        if (openResult < 0 || context == nullptr) {
            avformat_close_input(&context);
            result.timed_out = deadlineExpired(probeDeadline);
            result.message = result.timed_out ? "audio source probe timed out"
                                              : "cannot open audio file";
            return result;
        }
        const auto close = [&context] { avformat_close_input(&context); };
        if (avformat_find_stream_info(context, nullptr) < 0) {
            close();
            result.timed_out = deadlineExpired(probeDeadline);
            result.message = result.timed_out ? "audio source probe timed out"
                                              : "cannot read audio stream information";
            return result;
        }
        if (deadlineExpired(probeDeadline)) {
            close();
            result.timed_out = true;
            result.message = "audio source probe timed out";
            return result;
        }
        const int streamIndex = av_find_best_stream(
            context, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
        if (streamIndex < 0) {
            close();
            result.message = "audio stream is missing";
            return result;
        }
        const AVStream* stream = context->streams[streamIndex];
        const AVCodecParameters* parameters = stream->codecpar;
        const int sampleRate = parameters->sample_rate;
        const int channels = parameters->ch_layout.nb_channels;
        if (sampleRate <= 0 || channels <= 0 || channels > 8) {
            close();
            result.message = "unsupported audio format";
            return result;
        }

        SampleFrame totalFrames{};
        if (stream->duration > 0 && stream->duration != AV_NOPTS_VALUE) {
            totalFrames = av_rescale_q(stream->duration, stream->time_base,
                                       AVRational{1, sampleRate});
        } else if (context->duration > 0 && context->duration != AV_NOPTS_VALUE) {
            totalFrames = av_rescale_q(context->duration,
                                       AVRational{1, AV_TIME_BASE},
                                       AVRational{1, sampleRate});
        }
        close();
        if (totalFrames <= 0) {
            result.message = "audio duration is unavailable";
            return result;
        }

        result.source = {path, static_cast<std::uint32_t>(sampleRate),
                         static_cast<std::uint32_t>(channels), totalFrames};
        result.frame_tolerance = std::max<SampleFrame>(1, sampleRate / 10);
        result.success = true;
        return result;
    } catch (...) {
        result.message = "audio source probe failed";
        return result;
    }
}

} // namespace agplayer::editor
