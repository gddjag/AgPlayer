#include "document_renderer.hpp"

#include "../decoder.hpp"

#include <agplayer/c_api.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <vector>

namespace agplayer::editor {
namespace {

void write_u16(std::ostream& stream, const std::uint16_t value)
{
    const std::array<char, 2> bytes{
        static_cast<char>(value & 0xffU),
        static_cast<char>((value >> 8U) & 0xffU)};
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void write_u32(std::ostream& stream, const std::uint32_t value)
{
    const std::array<char, 4> bytes{
        static_cast<char>(value & 0xffU),
        static_cast<char>((value >> 8U) & 0xffU),
        static_cast<char>((value >> 16U) & 0xffU),
        static_cast<char>((value >> 24U) & 0xffU)};
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

bool write_header(std::ostream& stream, const std::uint32_t sample_rate,
                  const std::uint16_t channels, const std::uint32_t data_bytes)
{
    if (data_bytes > std::numeric_limits<std::uint32_t>::max() - 36U) {
        return false;
    }
    stream.write("RIFF", 4);
    write_u32(stream, 36U + data_bytes);
    stream.write("WAVEfmt ", 8);
    write_u32(stream, 16U);
    write_u16(stream, 3U);
    write_u16(stream, channels);
    write_u32(stream, sample_rate);
    write_u32(stream, sample_rate * channels * 4U);
    write_u16(stream, static_cast<std::uint16_t>(channels * 4U));
    write_u16(stream, 32U);
    stream.write("data", 4);
    write_u32(stream, data_bytes);
    return stream.good();
}

SampleFrame total_frames(const DocumentSnapshot& snapshot)
{
    SampleFrame result = 0;
    for (const auto& span : snapshot.spans) {
        if (span.frame_count <= 0
            || span.frame_count > std::numeric_limits<SampleFrame>::max() - result) {
            return -1;
        }
        result += span.frame_count;
    }
    return result;
}

std::vector<AudioSpan> spans_for_range(
    const DocumentSnapshot& snapshot,
    const std::optional<Selection>& range)
{
    if (!range) {
        return snapshot.spans;
    }
    std::vector<AudioSpan> result;
    SampleFrame cursor = 0;
    for (const AudioSpan& original : snapshot.spans) {
        const SampleFrame span_end = cursor + original.frame_count;
        const SampleFrame begin = std::max(cursor, range->start);
        const SampleFrame end = std::min(span_end, range->end);
        if (end > begin) {
            AudioSpan sliced = original;
            const SampleFrame local_begin = begin - cursor;
            const SampleFrame local_end = end - cursor;
            const float delta = original.gain_end - original.gain_start;
            sliced.source_start += local_begin;
            sliced.frame_count = local_end - local_begin;
            sliced.gain_start = original.gain_start + delta
                * static_cast<float>(local_begin)
                / static_cast<float>(original.frame_count);
            sliced.gain_end = original.gain_start + delta
                * static_cast<float>(local_end)
                / static_cast<float>(original.frame_count);
            result.push_back(std::move(sliced));
        }
        cursor = span_end;
    }
    return result;
}

} // namespace

RenderResult DocumentRenderer::renderFloatWav(
    const TimelineSnapshot& snapshot,
    const std::optional<Selection>& range,
    const std::filesystem::path& output_path,
    const std::atomic_bool* cancelled,
    std::function<void(float)> progress) const
{
    const auto legacy = singleEventDocumentSnapshot(snapshot);
    if (!legacy) {
        return {false, 0, 0, 0,
                "timeline render requires a single unmodified event"};
    }
    return renderFloatWav(*legacy, range, output_path, cancelled,
                          std::move(progress));
}

RenderResult DocumentRenderer::renderFloatWav(
    const DocumentSnapshot& snapshot,
    const std::optional<Selection>& range,
    const std::filesystem::path& output_path,
    const std::atomic_bool* cancelled,
    std::function<void(float)> progress) const
{
    const SampleFrame document_frames = total_frames(snapshot);
    if (document_frames <= 0 || output_path.empty()
        || (range && (!range->valid() || range->end > document_frames))) {
        return {false, 0, 0, 0, "invalid render request"};
    }

    const AudioSource* format_source = nullptr;
    for (const auto& span : snapshot.spans) {
        if (span.source) {
            format_source = span.source.get();
            break;
        }
    }
    if (!format_source || format_source->sample_rate == 0
        || format_source->channels == 0 || format_source->channels > 8) {
        return {false, 0, 0, 0, "document has no valid audio format"};
    }
    for (const auto& span : snapshot.spans) {
        if (span.source
            && (span.source->sample_rate != format_source->sample_rate
                || span.source->channels != format_source->channels)) {
            return {false, 0, 0, 0, "mixed source formats are unsupported"};
        }
    }

    const auto spans = spans_for_range(snapshot, range);
    const SampleFrame expected_frames = range
        ? range->end - range->start : document_frames;
    const std::uint64_t byte_count = static_cast<std::uint64_t>(expected_frames)
        * format_source->channels * sizeof(float);
    if (expected_frames <= 0
        || byte_count > std::numeric_limits<std::uint32_t>::max()) {
        return {false, 0, 0, 0, "WAV render exceeds 4 GiB"};
    }

    std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
    if (!output || !write_header(output, format_source->sample_rate,
                                 static_cast<std::uint16_t>(format_source->channels),
                                 static_cast<std::uint32_t>(byte_count))) {
        return {false, 0, 0, 0, "cannot create render file"};
    }

    SampleFrame rendered = 0;
    std::vector<float> silence(4096U * format_source->channels, 0.0F);
    for (const AudioSpan& span : spans) {
        if (cancelled && cancelled->load(std::memory_order_relaxed)) {
            output.close();
            std::filesystem::remove(output_path);
            return {false, rendered, format_source->sample_rate,
                    format_source->channels, "cancelled"};
        }
        SampleFrame remaining = span.frame_count;
        SampleFrame span_offset = 0;
        if (span.silent || !span.source) {
            while (remaining > 0) {
                const auto count = static_cast<std::size_t>(
                    std::min<SampleFrame>(remaining, 4096));
                output.write(reinterpret_cast<const char*>(silence.data()),
                             static_cast<std::streamsize>(count
                                 * format_source->channels * sizeof(float)));
                remaining -= static_cast<SampleFrame>(count);
                rendered += static_cast<SampleFrame>(count);
            }
        } else {
            agplayer::Decoder decoder;
            if (decoder.open(span.source->path.u8string(),
                             static_cast<int>(format_source->sample_rate),
                             static_cast<int>(format_source->channels)) != AG_OK) {
                output.close();
                std::filesystem::remove(output_path);
                return {false, rendered, format_source->sample_rate,
                        format_source->channels, "cannot open source"};
            }
            const std::int64_t seek_ms = span.source_start * 1'000
                / static_cast<SampleFrame>(format_source->sample_rate);
            if (decoder.seek(seek_ms) != AG_OK) {
                output.close();
                std::filesystem::remove(output_path);
                return {false, rendered, format_source->sample_rate,
                        format_source->channels, "cannot seek source"};
            }
            const SampleFrame decoder_start = (seek_ms
                * static_cast<SampleFrame>(format_source->sample_rate) + 999) / 1'000;
            SampleFrame discard = span.source_start - decoder_start;
            agplayer::DecodedAudioBlock block;
            while (remaining > 0) {
                if (decoder.read(block) != AG_OK || block.end_of_stream) {
                    output.close();
                    std::filesystem::remove(output_path);
                    return {false, rendered, format_source->sample_rate,
                            format_source->channels, "source ended early"};
                }
                std::size_t begin = static_cast<std::size_t>(
                    std::min<SampleFrame>(discard,
                                         static_cast<SampleFrame>(block.frames)));
                discard -= static_cast<SampleFrame>(begin);
                const auto available = static_cast<SampleFrame>(block.frames - begin);
                const auto take = static_cast<std::size_t>(
                    std::min(remaining, available));
                if (take == 0) {
                    continue;
                }
                float* samples = block.samples.data()
                    + begin * format_source->channels;
                for (std::size_t frame = 0; frame < take; ++frame) {
                    const float ratio = span.frame_count <= 1 ? 0.0F
                        : static_cast<float>(span_offset
                            + static_cast<SampleFrame>(frame))
                            / static_cast<float>(span.frame_count - 1);
                    const float gain = span.gain_start
                        + (span.gain_end - span.gain_start) * ratio;
                    for (std::uint32_t channel = 0;
                         channel < format_source->channels; ++channel) {
                        samples[frame * format_source->channels + channel] *= gain;
                    }
                }
                output.write(reinterpret_cast<const char*>(samples),
                    static_cast<std::streamsize>(take
                        * format_source->channels * sizeof(float)));
                remaining -= static_cast<SampleFrame>(take);
                span_offset += static_cast<SampleFrame>(take);
                rendered += static_cast<SampleFrame>(take);
            }
        }
        if (!output.good()) {
            output.close();
            std::filesystem::remove(output_path);
            return {false, rendered, format_source->sample_rate,
                    format_source->channels, "render write failed"};
        }
        if (progress) {
            progress(0.65F * static_cast<float>(rendered)
                     / static_cast<float>(expected_frames));
        }
    }
    output.flush();
    output.close();
    if (!output) {
        std::filesystem::remove(output_path);
        return {false, rendered, format_source->sample_rate,
                format_source->channels, "render flush failed"};
    }
    return {true, rendered, format_source->sample_rate,
            format_source->channels, {}};
}

} // namespace agplayer::editor
