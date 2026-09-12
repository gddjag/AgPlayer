#include "document_renderer.hpp"

#include "../decoder.hpp"

#include <agplayer/c_api.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <system_error>
#include <vector>

namespace agplayer::editor {
namespace {

void writeU16(std::ostream& stream, const std::uint16_t value)
{
    const std::array<char, 2> bytes{static_cast<char>(value & 0xffU),
                                    static_cast<char>((value >> 8U) & 0xffU)};
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void writeU32(std::ostream& stream, const std::uint32_t value)
{
    const std::array<char, 4> bytes{static_cast<char>(value & 0xffU),
        static_cast<char>((value >> 8U) & 0xffU),
        static_cast<char>((value >> 16U) & 0xffU),
        static_cast<char>((value >> 24U) & 0xffU)};
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

bool writeHeader(std::ostream& stream, const std::uint32_t sampleRate,
                 const std::uint16_t channels, const std::uint32_t dataBytes)
{
    if (dataBytes > std::numeric_limits<std::uint32_t>::max() - 36U) return false;
    stream.write("RIFF", 4); writeU32(stream, 36U + dataBytes);
    stream.write("WAVEfmt ", 8); writeU32(stream, 16U); writeU16(stream, 3U);
    writeU16(stream, channels); writeU32(stream, sampleRate);
    writeU32(stream, sampleRate * channels * 4U);
    writeU16(stream, static_cast<std::uint16_t>(channels * 4U));
    writeU16(stream, 32U); stream.write("data", 4); writeU32(stream, dataBytes);
    return stream.good();
}

bool isCancelled(const std::atomic_bool* cancelled) noexcept
{
    return cancelled != nullptr
        && cancelled->load(std::memory_order_relaxed);
}

} // namespace

RenderResult DocumentRenderer::renderFloatWav(
    const TimelineSnapshot& snapshot, const std::optional<Selection>& range,
    const std::filesystem::path& outputPath, const std::atomic_bool* cancelled,
    std::function<void(float)> progress) const
{
    if (outputPath.empty() || snapshot.events.empty() || snapshot.totalFrames <= 0
        || (range && (!range->valid() || range->end > snapshot.totalFrames))) {
        return {false, 0, 0, 0, "invalid render request"};
    }
    if (isCancelled(cancelled)) return {false, 0, 0, 0, "cancelled"};
    if (!isValid(snapshot.events.front())) {
        return {false, 0, 0, 0, "invalid or unsupported timeline event"};
    }
    const AudioSource& source = *snapshot.events.front().source;
    if (source.sample_rate == 0 || source.channels == 0 || source.channels > 8) {
        return {false, 0, 0, 0, "document has no valid audio format"};
    }
    SampleFrame previousEnd = 0;
    for (const AudioEvent& event : snapshot.events) {
        if (!isValid(event) || event.source->sample_rate != source.sample_rate
            || event.source->channels != source.channels
            || event.speedRatio != 1.0 || event.pitchSemitone != 0
            || event.timelineStart < previousEnd
            || audibleFrames(event) > snapshot.totalFrames - event.timelineStart) {
            return {false, 0, 0, 0, "invalid or unsupported timeline event"};
        }
        previousEnd = event.timelineStart + audibleFrames(event);
    }
    const SampleFrame renderStart = range ? range->start : 0;
    const SampleFrame renderEnd = range ? range->end : snapshot.totalFrames;
    const SampleFrame frameCount = range ? range->end - range->start
                                         : snapshot.totalFrames;
    const std::uint64_t bytesPerFrame = static_cast<std::uint64_t>(source.channels)
        * sizeof(float);
    if (frameCount <= 0
        || static_cast<std::uint64_t>(frameCount)
            > std::numeric_limits<std::uint32_t>::max() / bytesPerFrame
        || source.sample_rate > std::numeric_limits<std::uint32_t>::max()
            / source.channels / sizeof(float)) {
        return {false, 0, 0, 0, "WAV render exceeds 4 GiB"};
    }
    const std::uint64_t bytes = static_cast<std::uint64_t>(frameCount)
        * bytesPerFrame;
    std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
    if (!output || !writeHeader(output, source.sample_rate,
                                static_cast<std::uint16_t>(source.channels),
                                static_cast<std::uint32_t>(bytes))) {
        return {false, 0, 0, 0, "cannot create render file"};
    }
    const auto fail = [&](const std::string& message, const SampleFrame rendered) {
        output.close(); std::error_code ignored; std::filesystem::remove(outputPath, ignored);
        return RenderResult{false, rendered, source.sample_rate, source.channels, message};
    };
    constexpr SampleFrame kBlockFrames = 4'096;
    SampleFrame rendered = 0;
    std::vector<float> silence(static_cast<std::size_t>(kBlockFrames)
                               * source.channels, 0.0F);
    const auto reportProgress = [&] {
        if (progress) {
            progress(static_cast<float>(rendered)
                     / static_cast<float>(frameCount));
        }
    };
    const auto writeSilence = [&](SampleFrame frames) -> bool {
        while (frames > 0) {
            if (isCancelled(cancelled)) return false;
            const auto count = static_cast<std::size_t>(
                std::min(frames, kBlockFrames));
            output.write(reinterpret_cast<const char*>(silence.data()),
                         static_cast<std::streamsize>(count * bytesPerFrame));
            if (!output) return false;
            rendered += static_cast<SampleFrame>(count);
            frames -= static_cast<SampleFrame>(count);
            reportProgress();
        }
        return true;
    };
    const auto writeEvent = [&](const AudioEvent& event,
                                const SampleFrame timelineStart,
                                const SampleFrame timelineEnd) -> bool {
        const SampleFrame frames = timelineEnd - timelineStart;
        if (event.mute) return writeSilence(frames);
        agplayer::Decoder decoder;
        if (decoder.open(event.source->path.u8string(),
                         static_cast<int>(source.sample_rate),
                         static_cast<int>(source.channels)) != AG_OK) {
            return false;
        }
        const SampleFrame eventOffset = timelineStart - event.timelineStart;
        const SampleFrame sourceStart = event.sourceStart + eventOffset;
        if (decoder.seekFrame(sourceStart) != AG_OK) {
            return false;
        }
        SampleFrame eventRendered = 0;
        agplayer::DecodedAudioBlock block;
        while (eventRendered < frames) {
            if (isCancelled(cancelled) || decoder.read(block) != AG_OK) return false;
            if (block.frames == 0) {
                if (block.end_of_stream) return false;
                continue;
            }
            constexpr std::size_t begin = 0;
            const auto available = static_cast<SampleFrame>(block.frames - begin);
            const auto take = static_cast<std::size_t>(std::min(
                frames - eventRendered, available));
            if (take == 0) continue;
            float* samples = block.samples.data() + begin * source.channels;
            for (std::size_t frame = 0; frame < take; ++frame) {
                const SampleFrame localOffset = eventOffset + eventRendered
                    + static_cast<SampleFrame>(frame);
                const float gain = eventAmplitudeGainAt(event, localOffset);
                if (gain != 1.0F) {
                    for (std::uint32_t channel = 0; channel < source.channels;
                         ++channel) {
                        samples[frame * source.channels + channel] *= gain;
                    }
                }
            }
            output.write(reinterpret_cast<const char*>(samples),
                         static_cast<std::streamsize>(take * bytesPerFrame));
            if (!output) return false;
            eventRendered += static_cast<SampleFrame>(take);
            rendered += static_cast<SampleFrame>(take);
            reportProgress();
        }
        return true;
    };

    SampleFrame cursor = renderStart;
    for (const AudioEvent& event : snapshot.events) {
        const SampleFrame eventEnd = event.timelineStart + audibleFrames(event);
        if (eventEnd <= renderStart) continue;
        if (event.timelineStart >= renderEnd) break;
        const SampleFrame eventStart = std::max(event.timelineStart, renderStart);
        if (cursor < eventStart && !writeSilence(eventStart - cursor)) {
            return fail(isCancelled(cancelled) ? "cancelled" : "render write failed",
                        rendered);
        }
        const SampleFrame intersectionStart = std::max(cursor, eventStart);
        const SampleFrame intersectionEnd = std::min(eventEnd, renderEnd);
        if (intersectionStart < intersectionEnd
            && !writeEvent(event, intersectionStart, intersectionEnd)) {
            return fail(isCancelled(cancelled) ? "cancelled"
                                               : "cannot render source event",
                        rendered);
        }
        cursor = intersectionEnd;
    }
    if (cursor < renderEnd && !writeSilence(renderEnd - cursor)) {
        return fail(isCancelled(cancelled) ? "cancelled" : "render write failed",
                    rendered);
    }
    if (!output) return fail("render write failed", rendered);
    if (progress) progress(1.0F);
    return {true, rendered, source.sample_rate, source.channels, {}};
}

} // namespace agplayer::editor
