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
    std::function<void(float)> progress,
    const EditorPlaybackParameters& parameters) const
{
    if (outputPath.empty()) return {false, 0, 0, 0, "invalid render request"};
    if (isCancelled(cancelled)) return {false, 0, 0, 0, "cancelled"};
    std::string error;
    auto playback = EditorPlaybackStream::create(snapshot, parameters, error,
        &agplayer::create_time_pitch_engine, range, cancelled);
    if (!playback) return {false, 0, 0, 0, error};
    const auto sampleRate = static_cast<std::uint32_t>(playback->metadata().sample_rate);
    const auto channels = static_cast<std::uint32_t>(playback->metadata().channels);
    const std::uint64_t bytesPerFrame = channels * sizeof(float);
    const SampleFrame inputFrames = range ? range->end - range->start : snapshot.totalFrames;
    const long double estimatedFrames = std::ceil(
        static_cast<long double>(inputFrames) / parameters.speed_ratio);
    constexpr auto maxDataBytes = std::numeric_limits<std::uint32_t>::max() - 36U;
    if (estimatedFrames > static_cast<long double>(maxDataBytes / bytesPerFrame))
        return {false, 0, 0, 0, "WAV render exceeds 4 GiB"};
    std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
    if (!output || !writeHeader(output, sampleRate,
        static_cast<std::uint16_t>(channels), 0))
        return {false, 0, 0, 0, "cannot create render file"};
    SampleFrame rendered = 0;
    const auto fail = [&](const std::string& message) {
        output.close();
        std::error_code ignored;
        std::filesystem::remove(outputPath, ignored);
        return RenderResult{false, rendered, sampleRate, channels, message};
    };
    agplayer::DecodedAudioBlock block;
    for (;;) {
        if (isCancelled(cancelled)) return fail("cancelled");
        const auto status = playback->read(block);
        if (status != AG_OK) return fail(status == AG_CANCELLED ? "cancelled"
            : "cannot render source event");
        if (block.frames > maxDataBytes / bytesPerFrame - static_cast<std::uint64_t>(rendered))
            return fail("WAV render exceeds 4 GiB");
        output.write(reinterpret_cast<const char*>(block.samples.data()),
            static_cast<std::streamsize>(block.samples.size() * sizeof(float)));
        if (!output) return fail("render write failed");
        rendered += static_cast<SampleFrame>(block.frames);
        if (progress) progress(static_cast<float>(std::min<long double>(
            0.999L, rendered / std::max(1.0L, estimatedFrames))));
        if (block.end_of_stream) break;
    }
    if (isCancelled(cancelled)) return fail("cancelled");
    output.seekp(0);
    if (!writeHeader(output, sampleRate, static_cast<std::uint16_t>(channels),
        static_cast<std::uint32_t>(static_cast<std::uint64_t>(rendered) * bytesPerFrame)))
        return fail("render write failed");
    output.flush();
    if (!output) return fail("render write failed");
    if (progress) progress(1.0F);
    return {true, rendered, sampleRate, channels, {}};
}

} // namespace agplayer::editor
