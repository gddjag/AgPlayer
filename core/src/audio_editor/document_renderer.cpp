#include "document_renderer.hpp"

#include "../decoder.hpp"

#include <agplayer/c_api.h>

#include <algorithm>
#include <array>
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

const AudioEvent* compatibleEvent(const TimelineSnapshot& snapshot) noexcept
{
    if (snapshot.events.size() != 1) return nullptr;
    const AudioEvent& event = snapshot.events.front();
    if (!isValid(event) || event.timelineStart != 0
        || snapshot.totalFrames != audibleFrames(event) || event.fadeIn != 0
        || event.fadeOut != 0 || event.speedRatio != 1.0
        || event.pitchSemitone != 0 || !event.envelope.empty()) return nullptr;
    return &event;
}

} // namespace

RenderResult DocumentRenderer::renderFloatWav(
    const TimelineSnapshot& snapshot, const std::optional<Selection>& range,
    const std::filesystem::path& outputPath, const std::atomic_bool* cancelled,
    std::function<void(float)> progress) const
{
    const AudioEvent* const event = compatibleEvent(snapshot);
    if (!event) return {false, 0, 0, 0,
                         "timeline render requires a single unmodified event"};
    if (outputPath.empty() || (range && (!range->valid()
        || range->end > snapshot.totalFrames))) {
        return {false, 0, 0, 0, "invalid render request"};
    }
    const AudioSource& source = *event->source;
    if (source.sample_rate == 0 || source.channels == 0 || source.channels > 8) {
        return {false, 0, 0, 0, "document has no valid audio format"};
    }
    const SampleFrame localStart = range ? range->start : 0;
    const SampleFrame frameCount = range ? range->end - range->start
                                         : audibleFrames(*event);
    const std::uint64_t bytes = static_cast<std::uint64_t>(frameCount)
        * source.channels * sizeof(float);
    if (frameCount <= 0 || bytes > std::numeric_limits<std::uint32_t>::max()) {
        return {false, 0, 0, 0, "WAV render exceeds 4 GiB"};
    }
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
    SampleFrame rendered{};
    if (event->mute) {
        std::vector<float> silence(4096U * source.channels, 0.0F);
        while (rendered < frameCount) {
            const auto count = static_cast<std::size_t>(std::min<SampleFrame>(
                frameCount - rendered, 4096));
            output.write(reinterpret_cast<const char*>(silence.data()),
                         static_cast<std::streamsize>(count * source.channels * sizeof(float)));
            rendered += static_cast<SampleFrame>(count);
        }
    } else {
        agplayer::Decoder decoder;
        if (decoder.open(source.path.u8string(), static_cast<int>(source.sample_rate),
                         static_cast<int>(source.channels)) != AG_OK) {
            return fail("cannot open source", rendered);
        }
        const SampleFrame sourceStart = event->sourceStart + localStart;
        const std::int64_t seekMs = sourceStart * 1'000 / source.sample_rate;
        if (decoder.seek(seekMs) != AG_OK) return fail("cannot seek source", rendered);
        const SampleFrame decoderStart = (seekMs * source.sample_rate + 999) / 1'000;
        SampleFrame discard = sourceStart - decoderStart;
        agplayer::DecodedAudioBlock block;
        while (rendered < frameCount) {
            if (cancelled && cancelled->load(std::memory_order_relaxed)) {
                return fail("cancelled", rendered);
            }
            if (decoder.read(block) != AG_OK || block.end_of_stream) {
                return fail("source ended early", rendered);
            }
            const auto begin = static_cast<std::size_t>(std::min<SampleFrame>(
                discard, static_cast<SampleFrame>(block.frames)));
            discard -= static_cast<SampleFrame>(begin);
            const auto available = static_cast<SampleFrame>(block.frames - begin);
            const auto take = static_cast<std::size_t>(std::min(
                frameCount - rendered, available));
            if (take == 0) continue;
            float* samples = block.samples.data() + begin * source.channels;
            if (event->gain != 1.0F) {
                for (std::size_t index = 0; index < take * source.channels; ++index) {
                    samples[index] *= event->gain;
                }
            }
            output.write(reinterpret_cast<const char*>(samples),
                         static_cast<std::streamsize>(take * source.channels * sizeof(float)));
            if (!output) return fail("render write failed", rendered);
            rendered += static_cast<SampleFrame>(take);
            if (progress) progress(static_cast<float>(rendered)
                                   / static_cast<float>(frameCount));
        }
    }
    if (!output) return fail("render write failed", rendered);
    if (progress) progress(1.0F);
    return {true, rendered, source.sample_rate, source.channels, {}};
}

} // namespace agplayer::editor
