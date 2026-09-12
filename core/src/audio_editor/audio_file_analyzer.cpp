#include "audio_file_analyzer.hpp"

#include "../decoder.hpp"

#include <agplayer/c_api.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace agplayer::editor {
namespace {

bool cancelled(const std::atomic_bool* token) noexcept
{
    return token != nullptr && token->load(std::memory_order_relaxed);
}

} // namespace

AudioFileAnalysis AudioFileAnalyzer::analyze(
    const std::filesystem::path& path,
    const std::size_t target_points,
    const std::atomic_bool* cancel_token) noexcept
{
    AudioFileAnalysis result;
    if (path.empty() || target_points == 0 || cancelled(cancel_token)) {
        result.message = cancelled(cancel_token) ? "cancelled"
                                                  : "invalid analysis request";
        return result;
    }
    try {
        agplayer::Decoder counting_decoder;
        if (counting_decoder.open(path.u8string()) != AG_OK) {
            result.message = "cannot decode audio file";
            return result;
        }
        const agplayer::MediaMetadata metadata = counting_decoder.metadata();
        if (metadata.sample_rate <= 0 || metadata.channels <= 0
            || metadata.channels > 8) {
            result.message = "unsupported audio format";
            return result;
        }
        SampleFrame total_frames = 0;
        agplayer::DecodedAudioBlock block;
        do {
            if (cancelled(cancel_token)) {
                result.message = "cancelled";
                return result;
            }
            if (counting_decoder.read(block) != AG_OK
                || block.frames > static_cast<std::size_t>(
                    std::numeric_limits<SampleFrame>::max() - total_frames)) {
                result.message = "cannot count decoded frames";
                return result;
            }
            total_frames += static_cast<SampleFrame>(block.frames);
        } while (!block.end_of_stream);
        counting_decoder.close();
        if (total_frames <= 0) {
            result.message = "audio file is empty";
            return result;
        }

        const auto points = static_cast<std::size_t>(std::min<SampleFrame>(
            total_frames, static_cast<SampleFrame>(target_points)));
        result.channel_peaks.assign(
            static_cast<std::size_t>(metadata.channels),
            std::vector<float>(points * 2U, 0.0F));
        for (auto& channel : result.channel_peaks) {
            for (std::size_t point = 0; point < points; ++point) {
                channel[point * 2U] = 1.0F;
                channel[point * 2U + 1U] = -1.0F;
            }
        }
        result.visual_mix_peaks.assign(points * 2U, 0.0F);
        for (std::size_t point = 0; point < points; ++point) {
            result.visual_mix_peaks[point * 2U] = 1.0F;
            result.visual_mix_peaks[point * 2U + 1U] = -1.0F;
        }

        agplayer::Decoder decoder;
        if (decoder.open(path.u8string()) != AG_OK) {
            result.message = "cannot reopen audio file";
            return result;
        }
        SampleFrame absolute_frame = 0;
        do {
            if (cancelled(cancel_token)) {
                result.message = "cancelled";
                return result;
            }
            if (decoder.read(block) != AG_OK) {
                result.message = "cannot analyze audio samples";
                return result;
            }
            for (std::size_t frame = 0; frame < block.frames; ++frame) {
                const std::size_t bucket = static_cast<std::size_t>(
                    std::min<SampleFrame>(points - 1U,
                        (absolute_frame + static_cast<SampleFrame>(frame))
                            * static_cast<SampleFrame>(points) / total_frames));
                double mixedSample = 0.0;
                for (int channel = 0; channel < metadata.channels; ++channel) {
                    const float sample = block.samples[
                        frame * static_cast<std::size_t>(metadata.channels)
                        + static_cast<std::size_t>(channel)];
                    if (!std::isfinite(sample)) {
                        result.message = "audio contains invalid samples";
                        return result;
                    }
                    mixedSample += sample;
                    auto& peaks = result.channel_peaks[
                        static_cast<std::size_t>(channel)];
                    peaks[bucket * 2U] = std::min(peaks[bucket * 2U], sample);
                    peaks[bucket * 2U + 1U] = std::max(
                        peaks[bucket * 2U + 1U], sample);
                }
                const float visualSample = static_cast<float>(
                    mixedSample / metadata.channels);
                result.visual_mix_peaks[bucket * 2U] = std::min(
                    result.visual_mix_peaks[bucket * 2U], visualSample);
                result.visual_mix_peaks[bucket * 2U + 1U] = std::max(
                    result.visual_mix_peaks[bucket * 2U + 1U], visualSample);
            }
            absolute_frame += static_cast<SampleFrame>(block.frames);
        } while (!block.end_of_stream);

        for (auto& channel : result.channel_peaks) {
            for (std::size_t point = 0; point < points; ++point) {
                if (channel[point * 2U] > channel[point * 2U + 1U]) {
                    channel[point * 2U] = 0.0F;
                    channel[point * 2U + 1U] = 0.0F;
                }
            }
        }
        for (std::size_t point = 0; point < points; ++point) {
            if (result.visual_mix_peaks[point * 2U]
                > result.visual_mix_peaks[point * 2U + 1U]) {
                result.visual_mix_peaks[point * 2U] = 0.0F;
                result.visual_mix_peaks[point * 2U + 1U] = 0.0F;
            }
        }
        result.source = AudioSource{
            path, static_cast<std::uint32_t>(metadata.sample_rate),
            static_cast<std::uint32_t>(metadata.channels), total_frames};
        result.format = metadata.format;
        result.bits_per_sample = metadata.bits_per_sample;
        result.bit_rate = metadata.bit_rate;
        result.success = true;
        return result;
    } catch (...) {
        result.message = "audio analysis failed";
        return result;
    }
}

} // namespace agplayer::editor
