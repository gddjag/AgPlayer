#include "document_render_pipeline.hpp"

#include "../decoder.hpp"
#include "automation_time_mapper.hpp"
#include "document_renderer.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <system_error>
#include <vector>

namespace agplayer::editor {
namespace {

bool needs_time_pitch(const TimePitchSession& session) noexcept
{
    return std::abs(session.speedPercent() - 100.0) > 0.001
        || session.pitchCents() != 0
        || session.formantPreservation();
}

std::filesystem::path intermediate_path_for(
    const std::filesystem::path& output, const char* stage)
{
    const auto ticks = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    return output.parent_path() / std::filesystem::u8path(
        output.filename().u8string() + ".agplayer-" + stage + "-"
        + std::to_string(ticks) + ".wav");
}

void write_u16(std::ostream& stream, const std::uint16_t value)
{
    const std::array<char, 2> bytes{static_cast<char>(value & 0xffU),
        static_cast<char>((value >> 8U) & 0xffU)};
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void write_u32(std::ostream& stream, const std::uint32_t value)
{
    const std::array<char, 4> bytes{static_cast<char>(value & 0xffU),
        static_cast<char>((value >> 8U) & 0xffU),
        static_cast<char>((value >> 16U) & 0xffU),
        static_cast<char>((value >> 24U) & 0xffU)};
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

bool write_float_header(std::ostream& stream, const std::uint32_t sampleRate,
                        const std::uint16_t channels,
                        const std::uint32_t dataBytes)
{
    stream.write("RIFF", 4); write_u32(stream, 36U + dataBytes);
    stream.write("WAVEfmt ", 8); write_u32(stream, 16U); write_u16(stream, 3U);
    write_u16(stream, channels); write_u32(stream, sampleRate);
    write_u32(stream, sampleRate * channels * 4U);
    write_u16(stream, static_cast<std::uint16_t>(channels * 4U));
    write_u16(stream, 32U); stream.write("data", 4);
    write_u32(stream, dataBytes);
    return stream.good();
}

TimelineSnapshot without_automation(TimelineSnapshot snapshot)
{
    for (AudioEvent& event : snapshot.events) {
        event.gain = 1.0F;
        event.fadeIn = 0;
        event.fadeOut = 0;
        event.envelope.clear();
    }
    return snapshot;
}

RenderResult apply_automation_after_time_pitch(
    const AudioSource& processed, const TimelineSnapshot& original,
    const std::optional<Selection>& range,
    const std::filesystem::path& output,
    const double speedRatio,
    const std::atomic_bool* cancelled,
    const std::function<void(float)>& progress)
{
    const SampleFrame renderStart = range ? range->start : 0;
    const SampleFrame renderFrames = range ? range->end - range->start
                                            : original.totalFrames;
    const std::uint64_t bytesPerFrame = static_cast<std::uint64_t>(
        processed.channels) * sizeof(float);
    if (processed.total_frames <= 0 || renderFrames <= 0
        || processed.channels == 0 || processed.channels > 8
        || static_cast<std::uint64_t>(processed.total_frames)
            > (std::numeric_limits<std::uint32_t>::max() - 36U)
                / bytesPerFrame) {
        return {false, 0, 0, 0, "invalid automation render request"};
    }
    const auto dataBytes = static_cast<std::uint32_t>(
        static_cast<std::uint64_t>(processed.total_frames) * bytesPerFrame);
    std::ofstream stream(output, std::ios::binary | std::ios::trunc);
    if (!stream || !write_float_header(
            stream, processed.sample_rate,
            static_cast<std::uint16_t>(processed.channels), dataBytes)) {
        return {false, 0, 0, 0, "cannot create automation render file"};
    }
    const auto fail = [&](const char* message, const SampleFrame frames) {
        stream.close();
        std::error_code ignored;
        std::filesystem::remove(output, ignored);
        return RenderResult{false, frames, processed.sample_rate,
                            processed.channels, message};
    };
    agplayer::Decoder decoder;
    if (decoder.open(processed.path.u8string(),
                     static_cast<int>(processed.sample_rate),
                     static_cast<int>(processed.channels)) != AG_OK) {
        return fail("cannot decode time/pitch output", 0);
    }
    SampleFrame written = 0;
    std::size_t eventIndex = 0;
    const AutomationTimeMapper automationTime(
        renderStart, renderFrames, speedRatio);
    agplayer::DecodedAudioBlock block;
    while (written < processed.total_frames) {
        if (cancelled && cancelled->load(std::memory_order_acquire)) {
            return fail("cancelled", written);
        }
        if (decoder.read(block) != AG_OK) {
            return fail("cannot decode time/pitch output", written);
        }
        if (block.frames == 0) {
            if (block.end_of_stream) break;
            continue;
        }
        const auto take = static_cast<std::size_t>(std::min<SampleFrame>(
            processed.total_frames - written,
            static_cast<SampleFrame>(block.frames)));
        for (std::size_t frame = 0; frame < take; ++frame) {
            const SampleFrame outputFrame = written
                + static_cast<SampleFrame>(frame);
            const SampleFrame timelineFrame = automationTime.map(outputFrame);
            while (eventIndex < original.events.size()
                   && original.events[eventIndex].timelineStart
                        + audibleFrames(original.events[eventIndex])
                        <= timelineFrame) {
                ++eventIndex;
            }
            float gain = 1.0F;
            if (eventIndex < original.events.size()) {
                const AudioEvent& event = original.events[eventIndex];
                if (timelineFrame >= event.timelineStart
                    && timelineFrame < event.timelineStart
                        + audibleFrames(event)) {
                    gain = eventAmplitudeGainAt(
                        event, timelineFrame - event.timelineStart);
                }
            }
            if (gain != 1.0F) {
                for (std::uint32_t channel = 0; channel < processed.channels;
                     ++channel) {
                    block.samples[frame * processed.channels + channel] *= gain;
                }
            }
        }
        stream.write(reinterpret_cast<const char*>(block.samples.data()),
                     static_cast<std::streamsize>(take * bytesPerFrame));
        if (!stream) return fail("automation render write failed", written);
        written += static_cast<SampleFrame>(take);
        if (progress) progress(static_cast<float>(written)
                               / processed.total_frames);
    }
    if (written != processed.total_frames || !stream) {
        return fail("automation render was truncated", written);
    }
    if (progress) progress(1.0F);
    return {true, written, processed.sample_rate, processed.channels, {}};
}

WriteResult verify_decodable(const WriteRequest& request, WriteResult result)
{
    if (!result.ok()) return result;
    agplayer::Decoder decoder;
    agplayer::DecodedAudioBlock block;
    if (decoder.open(request.output_path.u8string()) != AG_OK
        || decoder.read(block) != AG_OK || block.frames == 0) {
        return {WriteError::VerificationFailed,
                "final export is not decodable", result.frames};
    }
    return result;
}

} // namespace

WriteResult DocumentRenderPipeline::write(
    const WriteRequest& request, const TimePitchSession& time_pitch,
    const std::atomic_bool* cancelled,
    std::function<void(float)> progress) const
{
    if (!needs_time_pitch(time_pitch)) {
        return verify_decodable(
            request, DocumentWriter{}.write(request, cancelled, progress));
    }

    const std::filesystem::path intermediate = intermediate_path_for(
        request.output_path, "time-pitch");
    const std::filesystem::path automated = intermediate_path_for(
        request.output_path, "automation");
    const auto processed = time_pitch.process(
        without_automation(request.snapshot), intermediate, request.range,
        cancelled,
        progress ? [progress](const float value) { progress(value * 0.60F); }
                 : std::function<void(float)>{});
    if (!processed.success) {
        return {cancelled && cancelled->load(std::memory_order_acquire)
                    ? WriteError::Cancelled : WriteError::RenderFailed,
                processed.message, 0};
    }

    const RenderResult automation = apply_automation_after_time_pitch(
        processed.source, request.snapshot, request.range, automated,
        time_pitch.speedPercent() / 100.0,
        cancelled,
        progress ? [progress](const float value) {
            progress(0.60F + value * 0.20F);
        } : std::function<void(float)>{});
    if (!automation.success) {
        std::error_code ignored;
        std::filesystem::remove(intermediate, ignored);
        return {cancelled && cancelled->load(std::memory_order_acquire)
                    ? WriteError::Cancelled : WriteError::RenderFailed,
                automation.message, 0};
    }
    WriteRequest processed_request = request;
    processed_request.snapshot = AudioDocument::fromSource(
        AudioSource{automated, processed.source.sample_rate,
                    processed.source.channels,
                    processed.source.total_frames}).timelineSnapshot();
    processed_request.range.reset();
    const WriteResult result = DocumentWriter{}.write(
        processed_request, cancelled,
        progress ? [progress](const float value) {
            progress(0.80F + value * 0.20F);
        } : std::function<void(float)>{});
    std::error_code ignored;
    std::filesystem::remove(intermediate, ignored);
    std::filesystem::remove(automated, ignored);
    return verify_decodable(request, result);
}

} // namespace agplayer::editor
