// Keep assertions active in Release because they execute the behavior under test.
#undef NDEBUG

#include <agplayer/c_api.h>

#include "decoder.hpp"
#include "frequency_color_waveform_analyzer.hpp"
#include "waveform_analyzer_filters.hpp"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <cstring>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

constexpr double kPi = 3.14159265358979323846;

void write_u16(std::ofstream& output, const std::uint16_t value)
{
    const char bytes[] = {
        static_cast<char>(value & 0xffU),
        static_cast<char>((value >> 8U) & 0xffU),
    };
    output.write(bytes, sizeof(bytes));
}

void write_u24(std::ofstream& output, const std::int32_t value)
{
    const auto bits = static_cast<std::uint32_t>(value);
    const char bytes[] = {
        static_cast<char>(bits & 0xffU),
        static_cast<char>((bits >> 8U) & 0xffU),
        static_cast<char>((bits >> 16U) & 0xffU),
    };
    output.write(bytes, sizeof(bytes));
}

void write_u32(std::ofstream& output, const std::uint32_t value)
{
    const char bytes[] = {
        static_cast<char>(value & 0xffU),
        static_cast<char>((value >> 8U) & 0xffU),
        static_cast<char>((value >> 16U) & 0xffU),
        static_cast<char>((value >> 24U) & 0xffU),
    };
    output.write(bytes, sizeof(bytes));
}

enum class WaveEncoding { Pcm16, Pcm24, Float32 };

void write_tone_wave(const std::filesystem::path& path,
                     const WaveEncoding encoding,
                     const std::uint16_t channels,
                     const double frequency,
                     const std::uint32_t sample_rate = 48'000U)
{
    const std::uint32_t frame_count = sample_rate;
    const std::uint16_t bits = encoding == WaveEncoding::Pcm16 ? 16U
        : encoding == WaveEncoding::Pcm24 ? 24U : 32U;
    const std::uint16_t bytes_per_sample = bits / 8U;
    const std::uint16_t block_align = channels * bytes_per_sample;
    const std::uint32_t data_size = frame_count * block_align;
    const bool extensible = channels > 2U;
    const std::uint32_t fmt_size = extensible ? 40U : 16U;
    const std::uint16_t format_tag = encoding == WaveEncoding::Float32 ? 3U : 1U;

    std::ofstream output(path, std::ios::binary);
    assert(output);
    output.write("RIFF", 4);
    write_u32(output, 4U + 8U + fmt_size + 8U + data_size);
    output.write("WAVEfmt ", 8);
    write_u32(output, fmt_size);
    write_u16(output, extensible ? 0xfffeU : format_tag);
    write_u16(output, channels);
    write_u32(output, sample_rate);
    write_u32(output, sample_rate * block_align);
    write_u16(output, block_align);
    write_u16(output, bits);
    if (extensible) {
        write_u16(output, 22U);
        write_u16(output, bits);
        write_u32(output, 0x3fU);
        write_u32(output, format_tag);
        write_u16(output, 0U);
        write_u16(output, 0x0010U);
        output.put(static_cast<char>(0x80));
        output.put(static_cast<char>(0x00));
        output.put(static_cast<char>(0x00));
        output.put(static_cast<char>(0xaa));
        output.put(static_cast<char>(0x00));
        output.put(static_cast<char>(0x38));
        output.put(static_cast<char>(0x9b));
        output.put(static_cast<char>(0x71));
    }
    output.write("data", 4);
    write_u32(output, data_size);

    for (std::uint32_t frame = 0U; frame < frame_count; ++frame) {
        const float sample = static_cast<float>(
            0.5 * std::sin(2.0 * kPi * frequency * frame / sample_rate));
        for (std::uint16_t channel = 0U; channel < channels; ++channel) {
            if (encoding == WaveEncoding::Pcm16) {
                const auto value = static_cast<std::int16_t>(
                    std::lround(sample * 32'767.0F));
                write_u16(output, static_cast<std::uint16_t>(value));
            } else if (encoding == WaveEncoding::Pcm24) {
                const auto value = static_cast<std::int32_t>(
                    std::lround(sample * 8'388'607.0F));
                write_u24(output, value);
            } else {
                std::uint32_t value = 0U;
                static_assert(sizeof(value) == sizeof(sample));
                std::memcpy(&value, &sample, sizeof(value));
                write_u32(output, value);
            }
        }
    }
    output.flush();
    assert(output);
}

void write_program_mixture_wave(const std::filesystem::path& path)
{
    constexpr std::uint32_t sample_rate = 48'000U;
    constexpr std::uint16_t channels = 2U;
    constexpr std::uint32_t segment_frames = sample_rate / 2U;
    constexpr std::uint32_t frame_count = segment_frames * 5U;
    constexpr std::uint16_t block_align = channels * 2U;
    constexpr std::uint32_t data_size = frame_count * block_align;

    std::ofstream output(path, std::ios::binary);
    assert(output);
    output.write("RIFF", 4);
    write_u32(output, 36U + data_size);
    output.write("WAVEfmt ", 8);
    write_u32(output, 16U);
    write_u16(output, 1U);
    write_u16(output, channels);
    write_u32(output, sample_rate);
    write_u32(output, sample_rate * block_align);
    write_u16(output, block_align);
    write_u16(output, 16U);
    output.write("data", 4);
    write_u32(output, data_size);

    for (std::uint32_t frame = 0U; frame < frame_count; ++frame) {
        const std::uint32_t segment = frame / segment_frames;
        const std::uint32_t local_frame = frame % segment_frames;
        const double time = static_cast<double>(frame) / sample_rate;
        float sample = 0.0F;
        if (segment == 0U) {
            sample = static_cast<float>(
                0.75 * std::sin(2.0 * kPi * 100.0 * time));
        } else if (segment == 1U) {
            sample = static_cast<float>(
                0.65 * std::sin(2.0 * kPi * 900.0 * time)
                + 0.10 * std::sin(2.0 * kPi * 200.0 * time));
        } else if (segment == 2U) {
            const bool transient = local_frame % (sample_rate / 10U)
                < sample_rate / 100U;
            if (transient) {
                sample = static_cast<float>(
                    0.55 * std::sin(2.0 * kPi * 1'600.0 * time)
                    + 0.65 * std::sin(2.0 * kPi * 8'000.0 * time));
            }
        } else if (segment == 3U) {
            const double fade = 1.0
                - static_cast<double>(local_frame) / segment_frames;
            sample = static_cast<float>(fade * (
                0.25 * std::sin(2.0 * kPi * 100.0 * time)
                + 0.55 * std::sin(2.0 * kPi * 1'000.0 * time)
                + 0.25 * std::sin(2.0 * kPi * 7'000.0 * time)));
        } else if (local_frame < segment_frames / 2U) {
            sample = std::clamp(static_cast<float>(
                1.8 * std::sin(2.0 * kPi * 1'000.0 * time)), -1.0F, 1.0F);
        }

        const float channel_samples[] = {sample, sample * 0.8F};
        for (const float channel_sample : channel_samples) {
            const auto value = static_cast<std::int16_t>(std::lround(
                std::clamp(channel_sample, -1.0F, 1.0F) * 32'767.0F));
            write_u16(output, static_cast<std::uint16_t>(value));
        }
    }
    output.flush();
    assert(output);
}

float maximum(const std::vector<float>& values)
{
    return values.empty() ? 0.0F
                          : *std::max_element(values.begin(), values.end());
}

float range_maximum(const std::vector<float>& values,
                    const std::size_t begin,
                    const std::size_t end)
{
    assert(begin < end && end <= values.size());
    return *std::max_element(values.begin() + static_cast<std::ptrdiff_t>(begin),
                             values.begin() + static_cast<std::ptrdiff_t>(end));
}

std::vector<float> tone(const std::uint32_t sample_rate,
                        const double frequency,
                        const double seconds)
{
    const std::size_t frames = static_cast<std::size_t>(
        static_cast<double>(sample_rate) * seconds);
    std::vector<float> samples(frames);
    for (std::size_t frame = 0U; frame < frames; ++frame) {
        samples[frame] = static_cast<float>(
            std::sin(2.0 * kPi * frequency * static_cast<double>(frame)
                     / static_cast<double>(sample_rate)));
    }
    return samples;
}

double settled_rms(std::vector<float> samples,
                   agplayer::detail::LinkwitzRiley4& filter,
                   const std::size_t settle_frames)
{
    long double sum_squares = 0.0L;
    std::size_t count = 0U;
    for (std::size_t index = 0U; index < samples.size(); ++index) {
        const float output = filter.process(samples[index]);
        assert(std::isfinite(output));
        if (index >= settle_frames) {
            sum_squares += static_cast<long double>(output) * output;
            ++count;
        }
    }
    return std::sqrt(static_cast<double>(sum_squares
                                         / static_cast<long double>(count)));
}

void test_lr4_is_finite_and_minus_six_db_at_crossover()
{
    // Catches a single-biquad implementation or a wrong-Q LR4 cascade.
    for (const std::uint32_t sample_rate : {44'100U, 48'000U, 88'200U,
                                            96'000U, 192'000U}) {
        for (const float cutoff : {180.0F, 2'800.0F}) {
            const auto input = tone(sample_rate, cutoff, 1.0);
            const std::size_t settle = sample_rate / 5U;
            agplayer::detail::LinkwitzRiley4 low(
                agplayer::detail::LinkwitzRiley4::Type::LowPass,
                static_cast<float>(sample_rate), cutoff);
            agplayer::detail::LinkwitzRiley4 high(
                agplayer::detail::LinkwitzRiley4::Type::HighPass,
                static_cast<float>(sample_rate), cutoff);
            const double input_rms = std::sqrt(0.5);
            const double low_db = 20.0 * std::log10(
                settled_rms(input, low, settle) / input_rms);
            const double high_db = 20.0 * std::log10(
                settled_rms(input, high, settle) / input_rms);
            assert(std::abs(low_db + 6.0206) <= 0.8);
            assert(std::abs(high_db + 6.0206) <= 0.8);
        }
    }
}

struct SplitOutput final {
    std::vector<float> low;
    std::vector<float> mid;
    std::vector<float> high;
};

SplitOutput split(const std::uint32_t sample_rate,
                  const std::vector<float>& samples,
                  const std::vector<std::size_t>& block_sizes)
{
    agplayer::detail::FrequencyBandSplitter splitter(
        static_cast<float>(sample_rate));
    SplitOutput output;
    output.low.reserve(samples.size());
    output.mid.reserve(samples.size());
    output.high.reserve(samples.size());
    std::size_t offset = 0U;
    std::size_t block_index = 0U;
    while (offset < samples.size()) {
        const std::size_t requested = block_sizes[block_index % block_sizes.size()];
        const std::size_t count = std::min(requested, samples.size() - offset);
        for (std::size_t index = 0U; index < count; ++index) {
            const auto bands = splitter.process(samples[offset + index]);
            assert(std::isfinite(bands.low));
            assert(std::isfinite(bands.mid));
            assert(std::isfinite(bands.high));
            output.low.push_back(bands.low);
            output.mid.push_back(bands.mid);
            output.high.push_back(bands.high);
        }
        offset += count;
        ++block_index;
    }
    return output;
}

double tail_rms(const std::vector<float>& values, const std::size_t skip)
{
    long double sum = 0.0L;
    for (std::size_t index = skip; index < values.size(); ++index) {
        sum += static_cast<long double>(values[index]) * values[index];
    }
    return std::sqrt(static_cast<double>(
        sum / static_cast<long double>(values.size() - skip)));
}

void test_band_splitter_preserves_state_across_decoder_blocks()
{
    // Catches filters being reconstructed for every decoded block.
    const auto input = tone(48'000U, 1'000.0, 1.0);
    const SplitOutput single = split(48'000U, input, {input.size()});
    const SplitOutput blocked = split(48'000U, input, {1U, 17U, 257U, 4'096U});
    assert(single.low == blocked.low);
    assert(single.mid == blocked.mid);
    assert(single.high == blocked.high);
}

void test_frequency_bands_have_expected_tone_dominance()
{
    // Catches swapped branches and wrong 180/2800 Hz crossover constants.
    for (const std::uint32_t sample_rate : {44'100U, 48'000U, 88'200U,
                                            96'000U, 192'000U}) {
        const std::size_t settle = sample_rate / 5U;
        const SplitOutput low = split(sample_rate,
                                      tone(sample_rate, 100.0, 1.0), {997U});
        assert(tail_rms(low.low, settle) > 2.0 * tail_rms(low.mid, settle));
        assert(tail_rms(low.low, settle) > 4.0 * tail_rms(low.high, settle));

        const SplitOutput mid = split(sample_rate,
                                      tone(sample_rate, 1'000.0, 1.0), {997U});
        assert(tail_rms(mid.mid, settle) > 2.0 * tail_rms(mid.low, settle));
        assert(tail_rms(mid.mid, settle) > 2.0 * tail_rms(mid.high, settle));

        const SplitOutput high = split(sample_rate,
                                       tone(sample_rate, 8'000.0, 1.0), {997U});
        assert(tail_rms(high.high, settle) > 2.0 * tail_rms(high.mid, settle));
        assert(tail_rms(high.high, settle) > 4.0 * tail_rms(high.low, settle));
    }
}

agplayer::detail::FrequencyFrameValues values(const float mix,
                                               const float low,
                                               const float mid,
                                               const float high)
{
    return {mix, low, mid, high};
}

void test_fixed_timeline_bucket_mapping_and_gaps()
{
    // Catches rebucketing to decoded EOF and filling timestamp gaps.
    agplayer::detail::FrequencyColorAccumulator three(3U, 2'000U);
    assert(three.addBlock(0, {values(0.25F, 0.25F, 0.0F, 0.0F),
                              values(0.50F, 0.0F, 0.50F, 0.0F),
                              values(1.00F, 0.0F, 0.0F, 1.00F)}) == AG_OK);
    agplayer::FrequencyColorWaveformData result;
    assert(three.finish(result) == AG_OK);
    assert(result.mix.size() == 2'000U);
    assert(result.mix[0] > 0.0F);
    assert(result.mix[666] > 0.0F);
    assert(result.mix[1'333] > 0.0F);
    assert(result.mix[1] == 0.0F);
    assert(result.mix[1'999] == 0.0F);

    agplayer::detail::FrequencyColorAccumulator gap(3U, 2'000U);
    assert(gap.addBlock(0, {values(1.0F, 1.0F, 0.0F, 0.0F)}) == AG_OK);
    assert(gap.addBlock(2, {values(1.0F, 0.0F, 0.0F, 1.0F)}) == AG_OK);
    assert(gap.finish(result) == AG_OK);
    assert(result.mix[0] > 0.0F);
    assert(result.mix[666] == 0.0F);
    assert(result.mix[1'333] > 0.0F);
}

void test_2001_frames_map_to_exactly_2000_buckets()
{
    // Catches using ceil-sized buckets or emitting timeline-dependent counts.
    agplayer::detail::FrequencyColorAccumulator accumulator(2'001U, 2'000U);
    std::vector<agplayer::detail::FrequencyFrameValues> frames(
        2'001U, values(0.5F, 0.5F, 0.5F, 0.5F));
    frames[1] = values(1.0F, 1.0F, 1.0F, 1.0F);
    assert(accumulator.addBlock(0, frames) == AG_OK);
    agplayer::FrequencyColorWaveformData result;
    assert(accumulator.finish(result) == AG_OK);
    assert(result.mix.size() == 2'000U);
    assert(std::all_of(result.mix.begin(), result.mix.end(),
                       [](const float value) { return value > 0.0F; }));
}

void test_overlapping_pts_and_nonfinite_samples_are_rejected()
{
    // Catches accepting a repeated frame or poisoning normalization with NaN.
    agplayer::detail::FrequencyColorAccumulator overlap(10U, 2'000U);
    assert(overlap.addBlock(0, {values(1.0F, 1.0F, 1.0F, 1.0F),
                                values(1.0F, 1.0F, 1.0F, 1.0F)}) == AG_OK);
    assert(overlap.addBlock(1, {values(1.0F, 1.0F, 1.0F, 1.0F)})
           == AG_DECODE_ERROR);
    agplayer::FrequencyColorWaveformData result;
    assert(overlap.finish(result) == AG_DECODE_ERROR);
    assert(result.mix.empty());

    agplayer::detail::FrequencyColorAccumulator invalid(1U, 2'000U);
    assert(invalid.addBlock(0, {values(
        std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F, 0.0F)})
           == AG_DECODE_ERROR);
}

void test_timeline_policy_uses_declared_timestamp_sources()
{
    // Catches tolerance-only snapping, swallowed one-frame gaps, and generic
    // acceptance of negative timestamps without declared leading padding.
    agplayer::detail::FrequencyTimelineBlock mapped;
    agplayer::detail::FrequencyTimelineCursor exact(1U, 0U);
    assert(exact.mapBlock(0, 10U, mapped) == AG_OK);
    assert(mapped.begin_frame == 0U && mapped.frame_count == 10U);
    assert(exact.mapBlock(11, 10U, mapped) == AG_OK);
    assert(mapped.begin_frame == 11U);

    agplayer::detail::FrequencyTimelineCursor overlap(1U, 0U);
    assert(overlap.mapBlock(0, 10U, mapped) == AG_OK);
    assert(overlap.mapBlock(9, 10U, mapped) == AG_DECODE_ERROR);

    agplayer::detail::FrequencyTimelineCursor non_priming_negative(1U, 0U);
    assert(non_priming_negative.mapBlock(-1, 10U, mapped)
           == AG_DECODE_ERROR);

    agplayer::detail::FrequencyTimelineCursor declared_preroll(1U, 1'105U);
    assert(declared_preroll.mapBlock(-1'105, 47U, mapped) == AG_OK);
    assert(mapped.skip_frames == 47U && mapped.frame_count == 0U);
    assert(declared_preroll.mapBlock(47, 1'152U, mapped) == AG_OK);
    assert(mapped.begin_frame == 47U && mapped.frame_count == 1'152U);

    agplayer::detail::FrequencyTimelineCursor coarse(45U, 0U);
    assert(coarse.mapBlock(4'057, 2'048U, mapped) == AG_OK);
    assert(coarse.mapBlock(6'130, 2'048U, mapped) == AG_OK);
    assert(mapped.begin_frame == 6'105U);
}

void test_negative_preroll_tracks_signed_source_continuity()
{
    // Catches fully-negative blocks returning before source PTS state advances.
    agplayer::detail::FrequencyTimelineBlock mapped;

    agplayer::detail::FrequencyTimelineCursor continuous(1U, 12U);
    assert(continuous.mapBlock(-12, 4U, mapped) == AG_OK);
    assert(mapped.skip_frames == 4U && mapped.frame_count == 0U);
    assert(continuous.mapBlock(-8, 5U, mapped) == AG_OK);
    assert(mapped.skip_frames == 5U && mapped.frame_count == 0U);
    assert(continuous.mapBlock(-3, 3U, mapped) == AG_OK);
    assert(mapped.skip_frames == 3U && mapped.frame_count == 0U);
    assert(continuous.mapBlock(0, 2U, mapped) == AG_OK);
    assert(mapped.begin_frame == 0U && mapped.frame_count == 2U);

    agplayer::detail::FrequencyTimelineCursor repeated(1U, 12U);
    assert(repeated.mapBlock(-12, 4U, mapped) == AG_OK);
    assert(repeated.mapBlock(-12, 4U, mapped) == AG_DECODE_ERROR);

    agplayer::detail::FrequencyTimelineCursor overlapping(1U, 12U);
    assert(overlapping.mapBlock(-12, 6U, mapped) == AG_OK);
    assert(overlapping.mapBlock(-8, 4U, mapped) == AG_DECODE_ERROR);

    agplayer::detail::FrequencyTimelineCursor crossing_zero(1U, 5U);
    assert(crossing_zero.mapBlock(-5, 8U, mapped) == AG_OK);
    assert(mapped.skip_frames == 5U);
    assert(mapped.begin_frame == 0U && mapped.frame_count == 3U);
    assert(crossing_zero.mapBlock(3, 2U, mapped) == AG_OK);
    assert(mapped.begin_frame == 3U && mapped.frame_count == 2U);

    agplayer::detail::FrequencyTimelineCursor at_padding_limit(1U, 12U);
    assert(at_padding_limit.mapBlock(-12, 1U, mapped) == AG_OK);
    agplayer::detail::FrequencyTimelineCursor past_padding_limit(1U, 12U);
    assert(past_padding_limit.mapBlock(-13, 1U, mapped) == AG_DECODE_ERROR);

    agplayer::detail::FrequencyTimelineCursor overflow(1U, 0U);
    assert(overflow.mapBlock(std::numeric_limits<std::int64_t>::max(), 2U,
                             mapped) == AG_DECODE_ERROR);
}

void test_all_layers_share_one_normalization_reference_and_gate()
{
    // Catches per-layer normalization, missing gain staging, and a missing gate.
    agplayer::detail::FrequencyColorAccumulator accumulator(4U, 4U);
    assert(accumulator.addBlock(0, {
        values(1.0F, 0.5F, 0.5F, 0.5F),
        values(1.0F, 0.5F, 0.5F, 0.5F),
        values(1.0F, 0.5F, 0.5F, 0.5F),
        values(0.000'1F, 0.000'1F, 0.000'1F, 0.000'1F),
    }) == AG_OK);
    agplayer::FrequencyColorWaveformData result;
    assert(accumulator.finish(result) == AG_OK);
    assert(result.mix[0] > result.low[0]);
    assert(result.mid[0] > result.low[0]);
    assert(result.high[0] > result.mid[0]);
    for (const auto* layer : {&result.mix, &result.low, &result.mid,
                              &result.high}) {
        assert(std::all_of(layer->begin(), layer->end(), [](const float value) {
            return std::isfinite(value) && value >= 0.0F && value <= 0.98F;
        }));
    }

    agplayer::detail::FrequencyColorAccumulator gated(1U, 1U);
    assert(gated.addBlock(0, {
        values(0.000'1F, 0.000'1F, 0.000'1F, 0.000'1F),
    }) == AG_OK);
    assert(gated.finish(result) == AG_OK);
    assert(result.mix[0] == 0.0F);
    assert(result.low[0] == 0.0F);
    assert(result.mid[0] == 0.0F);
    assert(result.high[0] == 0.0F);
}

void test_peak_rms_weights_shared_percentile_and_exact_cap()
{
    // Catches changing 0.7 peak + 0.3 RMS, using maximum normalization,
    // or changing the display exponent/cap.
    agplayer::detail::FrequencyColorAccumulator envelope(4U, 2U);
    assert(envelope.addBlock(0, {
        values(1.0F, 0.0F, 0.0F, 0.0F),
        values(0.0F, 0.0F, 0.0F, 0.0F),
        values(1.0F, 0.0F, 0.0F, 0.0F),
        values(1.0F, 0.0F, 0.0F, 0.0F),
    }) == AG_OK);
    agplayer::FrequencyColorWaveformData result;
    assert(envelope.finish(result) == AG_OK);
    assert(std::abs(result.mix[0] - 0.94457355F) < 0.000'01F);
    assert(std::abs(result.mix[1] - 0.98F) < 0.000'001F);

    agplayer::detail::FrequencyColorAccumulator percentile(200U, 200U);
    std::vector<agplayer::detail::FrequencyFrameValues> distribution(
        200U, values(0.5F, 0.0F, 0.0F, 0.0F));
    distribution.back() = values(1.0F, 0.0F, 0.0F, 0.0F);
    assert(percentile.addBlock(0, distribution) == AG_OK);
    assert(percentile.finish(result) == AG_OK);
    assert(std::abs(result.mix.front() - 0.98F) < 0.000'001F);
    assert(std::abs(result.mix.back() - 0.98F) < 0.000'001F);
}

void test_exact_band_gains_and_smoothing_windows()
{
    // Catches changing low/mid/high gains or making 7/5/3 windows equal.
    agplayer::detail::FrequencyColorAccumulator gains(7U, 7U);
    std::vector<agplayer::detail::FrequencyFrameValues> constant(
        7U, values(1.0F, 0.5F, 0.5F, 0.5F));
    assert(gains.addBlock(0, constant) == AG_OK);
    agplayer::FrequencyColorWaveformData result;
    assert(gains.finish(result) == AG_OK);
    for (std::size_t index = 0U; index < 7U; ++index) {
        assert(std::abs(result.low[index] - 0.65067093F) < 0.000'01F);
        assert(std::abs(result.mid[index] - 0.60584483F) < 0.000'01F);
        assert(std::abs(result.high[index] - 0.80604893F) < 0.000'01F);
    }

    agplayer::detail::FrequencyColorAccumulator impulse(9U, 9U);
    std::vector<agplayer::detail::FrequencyFrameValues> transient(
        9U, values(1.0F, 0.0F, 0.0F, 0.0F));
    transient[4] = values(1.0F, 1.0F, 1.0F, 1.0F);
    assert(impulse.addBlock(0, transient) == AG_OK);
    assert(impulse.finish(result) == AG_OK);
    const auto nonzero_count = [](const std::vector<float>& layer) {
        return static_cast<std::size_t>(std::count_if(
            layer.begin(), layer.end(), [](const float value) {
                return value > 0.0F;
            }));
    };
    assert(nonzero_count(result.low) == 7U);
    assert(nonzero_count(result.mid) == 5U);
    assert(nonzero_count(result.high) == 3U);
    assert(std::abs(result.low[4] - 0.29925349F) < 0.000'01F);
    assert(std::abs(result.mid[4] - 0.34327218F) < 0.000'01F);
    assert(std::abs(result.high[4] - 0.62688059F) < 0.000'01F);
}

void test_smoothing_precedes_shared_reference_and_gate_is_exact()
{
    // Catches computing the percentile before band smoothing or changing the
    // -72 dBFS gate.
    agplayer::detail::FrequencyColorAccumulator order(200U, 200U);
    std::vector<agplayer::detail::FrequencyFrameValues> impulse(
        200U, values(0.0F, 0.0F, 0.0F, 0.0F));
    impulse[100] = values(0.0F, 1.0F, 0.0F, 0.0F);
    assert(order.addBlock(0, impulse) == AG_OK);
    agplayer::FrequencyColorWaveformData result;
    assert(order.finish(result) == AG_OK);
    assert(std::abs(result.low[100] - 0.98F) < 0.000'001F);

    agplayer::detail::FrequencyColorAccumulator gate(3U, 3U);
    assert(gate.addBlock(0, {
        values(0.000'2F, 0.0F, 0.0F, 0.0F),
        values(0.000'3F, 0.0F, 0.0F, 0.0F),
        values(1.0F, 0.0F, 0.0F, 0.0F),
    }) == AG_OK);
    assert(gate.finish(result) == AG_OK);
    assert(result.mix[0] == 0.0F);
    assert(result.mix[1] > 0.0F);
    assert(std::abs(result.mix[2] - 0.98F) < 0.000'001F);
}

void test_deterministic_program_material_profiles()
{
    // Deterministic synthetic fade/classical/clip/silence profiles replace
    // copyrighted program material while exercising the same envelopes.
    std::vector<agplayer::detail::FrequencyFrameValues> profile;
    profile.reserve(20U);
    for (std::size_t index = 0U; index < 20U; ++index) {
        const float fade = 1.0F - static_cast<float>(index) / 20.0F;
        profile.push_back(values(fade,
                                 0.40F * fade,
                                 0.30F * fade,
                                 0.20F * fade));
    }
    profile[4] = values(1.0F, 1.0F, 0.7F, 0.9F); // clipped transient
    for (std::size_t index = 16U; index < profile.size(); ++index) {
        profile[index] = values(0.0F, 0.0F, 0.0F, 0.0F); // silence tail
    }

    agplayer::detail::FrequencyColorAccumulator first(20U, 20U);
    agplayer::detail::FrequencyColorAccumulator second(20U, 20U);
    assert(first.addBlock(0, profile) == AG_OK);
    assert(second.addBlock(0, profile) == AG_OK);
    agplayer::FrequencyColorWaveformData first_result;
    agplayer::FrequencyColorWaveformData second_result;
    assert(first.finish(first_result) == AG_OK);
    assert(second.finish(second_result) == AG_OK);
    assert(first_result.mix == second_result.mix);
    assert(first_result.low == second_result.low);
    assert(first_result.mid == second_result.mid);
    assert(first_result.high == second_result.high);
    const auto exact = [](const float actual, const float expected) {
        assert(std::abs(actual - expected) < 0.000'001F);
    };
    exact(first_result.mix[0], 0.980000019F);
    exact(first_result.low[0], 0.539864898F);
    exact(first_result.mid[0], 0.427566618F);
    exact(first_result.high[0], 0.449594975F);
    exact(first_result.mix[4], 0.980000019F);
    exact(first_result.low[4], 0.581535995F);
    exact(first_result.mid[4], 0.470008373F);
    exact(first_result.high[4], 0.709133327F);
    exact(first_result.mix[10], 0.650670946F);
    exact(first_result.low[10], 0.368670672F);
    exact(first_result.mid[10], 0.287194818F);
    exact(first_result.high[10], 0.297166586F);
    exact(first_result.mix[14], 0.474040210F);
    exact(first_result.low[14], 0.239883289F);
    exact(first_result.mid[14], 0.191469088F);
    exact(first_result.high[14], 0.216497943F);
    assert(std::abs(first_result.mix[4] - 0.98F) < 0.000'001F);
    assert(first_result.mix[0] > first_result.mix[14]);
    assert(first_result.mix[19] == 0.0F);
    assert(first_result.low[19] == 0.0F);
    assert(first_result.mid[19] == 0.0F);
    assert(first_result.high[19] == 0.0F);
}

void test_symmetric_smoothing_preserves_a_constant_band_at_edges()
{
    // Catches dividing edge windows by unavailable neighbors.
    agplayer::detail::FrequencyColorAccumulator accumulator(7U, 7U);
    std::vector<agplayer::detail::FrequencyFrameValues> frames(
        7U, values(0.0F, 1.0F, 0.0F, 0.0F));
    assert(accumulator.addBlock(0, frames) == AG_OK);
    agplayer::FrequencyColorWaveformData result;
    assert(accumulator.finish(result) == AG_OK);
    assert(result.low.size() == 7U);
    for (const float value : result.low) {
        assert(std::abs(value - result.low[3]) < 0.000'001F);
    }
}

struct CancellationState final {
    std::atomic_bool* cancelled = nullptr;
    std::vector<float> progress;
};

void cancel_after_first_block(const float value, void* const user_data)
{
    auto& state = *static_cast<CancellationState*>(user_data);
    state.progress.push_back(value);
    if (value > 0.0F) state.cancelled->store(true, std::memory_order_relaxed);
}

void test_analyzer_is_one_pass_bounded_and_clears_cancelled_output(
    const std::string& source_path)
{
    // Catches reopening for BPM/counting and retaining PCM proportional to duration.
    agplayer::FrequencyColorWaveformData output;
    agplayer::FrequencyColorAnalysisDiagnostics diagnostics;
    const std::uint64_t opens_before = agplayer::Decoder::threadOpenCount();
    assert(agplayer::FrequencyColorWaveformAnalyzer::analyze(
               source_path, 2'000U, nullptr, nullptr, nullptr,
               output, &diagnostics) == AG_OK);
    const std::uint64_t actual_opens =
        agplayer::Decoder::threadOpenCount() - opens_before;
    assert(output.mix.size() == 2'000U);
    assert(output.low.size() == 2'000U);
    assert(output.mid.size() == 2'000U);
    assert(output.high.size() == 2'000U);
    assert(output.timeline_frames > 0U);
    assert(output.decoded_frames > 0U);
    assert(output.sample_rate > 0U);
    assert(actual_opens == 1U);
    assert(diagnostics.decoder_open_count == actual_opens);
    assert(diagnostics.decoded_block_count > 0U);
    assert(diagnostics.accumulator_bytes
           <= 2'000U * 4U * sizeof(agplayer::detail::BucketStats) + 4'096U);

    std::atomic_bool cancelled{false};
    CancellationState state{&cancelled, {}};
    output.mix = {1.0F};
    output.low = {1.0F};
    output.mid = {1.0F};
    output.high = {1.0F};
    assert(agplayer::FrequencyColorWaveformAnalyzer::analyze(
               source_path, 2'000U, &cancelled, cancel_after_first_block,
               &state, output, nullptr) == AG_CANCELLED);
    assert(output.mix.empty());
    assert(output.low.empty());
    assert(output.mid.empty());
    assert(output.high.empty());
    assert(!state.progress.empty());
    assert(state.progress.back() < 1.0F);
}

void test_public_analyzer_tracks_real_pcm_program_material(
    const std::filesystem::path& source)
{
    // Catches tests bypassing decoder/downmix/LR4 with pre-separated helper data.
    const std::filesystem::path path = source.parent_path()
        / "frequency-program-mixture.wav";
    write_program_mixture_wave(path);

    agplayer::FrequencyColorWaveformData output;
    assert(agplayer::FrequencyColorWaveformAnalyzer::analyze(
               path.string(), 2'000U, nullptr, nullptr, nullptr,
               output, nullptr) == AG_OK);
    assert(output.timeline_frames == 120'000U);
    assert(output.sample_rate == 48'000U);
    for (const auto* layer : {&output.mix, &output.low, &output.mid,
                              &output.high}) {
        assert(layer->size() == 2'000U);
        assert(std::all_of(layer->begin(), layer->end(), [](const float value) {
            return std::isfinite(value) && value >= 0.0F && value <= 0.98F;
        }));
    }

    const float bass_low = range_maximum(output.low, 40U, 360U);
    assert(bass_low > range_maximum(output.mid, 40U, 360U));
    assert(bass_low > range_maximum(output.high, 40U, 360U));

    const float vocal_mid = range_maximum(output.mid, 440U, 760U);
    assert(vocal_mid > range_maximum(output.low, 440U, 760U));
    assert(vocal_mid > range_maximum(output.high, 440U, 760U));

    assert(range_maximum(output.mid, 800U, 1'200U)
           > range_maximum(output.low, 800U, 1'200U));
    assert(range_maximum(output.high, 800U, 1'200U)
           > range_maximum(output.low, 800U, 1'200U));

    assert(range_maximum(output.mix, 1'200U, 1'300U)
           > range_maximum(output.mix, 1'500U, 1'600U));
    assert(std::abs(range_maximum(output.mix, 1'600U, 1'800U) - 0.98F)
           < 0.000'001F);
    for (std::size_t index = 1'870U; index < 2'000U; ++index) {
        assert(output.mix[index] == 0.0F);
        assert(output.low[index] == 0.0F);
        assert(output.mid[index] == 0.0F);
        assert(output.high[index] == 0.0F);
    }
    std::filesystem::remove(path);
}

void test_decoder_pcm_matrix_preserves_band_dominance(
    const std::filesystem::path& source)
{
    // Catches bypassing AnalysisMono or assuming only 16-bit stereo input.
    const struct ToneCase final {
        double frequency;
        int dominant_band;
    } tone_cases[] = {
        {100.0, 0},
        {1'000.0, 1},
        {8'000.0, 2},
    };
    for (const WaveEncoding encoding : {WaveEncoding::Pcm16,
                                        WaveEncoding::Pcm24,
                                        WaveEncoding::Float32}) {
        for (const std::uint16_t channels : {
                 std::uint16_t{1U}, std::uint16_t{2U}, std::uint16_t{6U}}) {
            for (const ToneCase tone_case : tone_cases) {
                const std::filesystem::path path = source.parent_path()
                    / ("frequency-pcm-" + std::to_string(static_cast<int>(encoding))
                       + "-" + std::to_string(channels) + "-"
                       + std::to_string(tone_case.dominant_band) + ".wav");
                write_tone_wave(path, encoding, channels, tone_case.frequency);
                agplayer::FrequencyColorWaveformData output;
                assert(agplayer::FrequencyColorWaveformAnalyzer::analyze(
                           path.string(), 2'000U, nullptr, nullptr, nullptr,
                           output, nullptr) == AG_OK);
                const float bands[] = {maximum(output.low), maximum(output.mid),
                                       maximum(output.high)};
                assert(bands[tone_case.dominant_band] > 0.0F);
                for (int band = 0; band < 3; ++band) {
                    if (band != tone_case.dominant_band) {
                        assert(bands[tone_case.dominant_band] > bands[band]);
                    }
                }
                for (const auto* layer : {&output.mix, &output.low, &output.mid,
                                          &output.high}) {
                    assert(layer->size() == 2'000U);
                    assert(std::all_of(layer->begin(), layer->end(),
                                       [](const float value) {
                        return std::isfinite(value) && value >= 0.0F
                               && value <= 0.98F;
                    }));
                }
                std::filesystem::remove(path);
            }
        }
    }
}

void test_low_sample_rates_use_safe_fallback(const std::filesystem::path& source)
{
    // Catches constructing a 2.8 kHz crossover at/beyond the high-band ceiling.
    for (const std::uint32_t sample_rate : {4'000U, 6'000U}) {
        const std::filesystem::path path = source.parent_path()
            / ("frequency-low-rate-" + std::to_string(sample_rate) + ".wav");
        write_tone_wave(path, WaveEncoding::Pcm16, 1U, 1'000.0, sample_rate);
        agplayer::FrequencyColorWaveformData output;
        output.mix = {1.0F};
        output.low = {1.0F};
        output.mid = {1.0F};
        output.high = {1.0F};
        assert(agplayer::FrequencyColorWaveformAnalyzer::analyze(
                   path.string(), 2'000U, nullptr, nullptr, nullptr,
                   output, nullptr) == AG_UNSUPPORTED_FORMAT);
        assert(output.mix.empty());
        assert(output.low.empty());
        assert(output.mid.empty());
        assert(output.high.empty());
        std::filesystem::remove(path);
    }

    const std::filesystem::path supported = source.parent_path()
        / "frequency-low-rate-8000.wav";
    write_tone_wave(supported, WaveEncoding::Pcm16, 1U, 1'000.0, 8'000U);
    agplayer::FrequencyColorWaveformData output;
    assert(agplayer::FrequencyColorWaveformAnalyzer::analyze(
               supported.string(), 2'000U, nullptr, nullptr, nullptr,
               output, nullptr) == AG_OK);
    assert(output.mix.size() == 2'000U);
    assert(output.sample_rate == 8'000U);
    std::filesystem::remove(supported);
}

void test_repository_lossless_lossy_vbr_formats_preserve_band_dominance(
    const std::filesystem::path& source)
{
    // Catches coupling to PCM/WAV and compressed decoders that blur or swap
    // low/mid/high energy.
    const struct FormatCase final {
        const char* extension;
        const char* codec;
    } formats[] = {
        {"mp3", "libmp3lame"},
        {"flac", "flac"},
        {"aac", "aac"},
        {"m4a", "aac"},
        {"ogg", "vorbis"},
        {"opus", "opus"},
        {"wma", "wmav2"},
    };
    const struct ToneCase final {
        double frequency;
        int dominant_band;
    } tones[] = {{100.0, 0}, {1'000.0, 1}, {8'000.0, 2}};

    for (const ToneCase& tone_case : tones) {
        const std::filesystem::path tone_source = source.parent_path()
            / ("frequency-compressed-source-"
               + std::to_string(tone_case.dominant_band) + ".wav");
        write_tone_wave(tone_source, WaveEncoding::Pcm16, 2U,
                        tone_case.frequency, 44'100U);

        for (const FormatCase& format : formats) {
            const std::filesystem::path path = source.parent_path()
                / ("frequency-format-"
                   + std::to_string(tone_case.dominant_band) + "."
                   + format.extension);
            std::filesystem::remove(path);
            assert(ag_transcode(tone_source.string().c_str(),
                                path.string().c_str(), format.codec,
                                192'000, 44'100, 2,
                                nullptr, nullptr, nullptr) == AG_OK);
            agplayer::FrequencyColorWaveformData output;
            assert(agplayer::FrequencyColorWaveformAnalyzer::analyze(
                       path.string(), 2'000U, nullptr, nullptr, nullptr,
                       output, nullptr) == AG_OK);
            const float bands[] = {maximum(output.low), maximum(output.mid),
                                   maximum(output.high)};
            for (int band = 0; band < 3; ++band) {
                if (band != tone_case.dominant_band) {
                    assert(bands[tone_case.dominant_band] > bands[band]);
                }
            }

            agplayer::Decoder decoder;
            agplayer::DecoderOpenOptions decoder_options;
            decoder_options.downmix = agplayer::DecoderDownmix::AnalysisMono;
            assert(decoder.open(path.string(), decoder_options) == AG_OK);
            if (std::string(format.extension) == "mp3") {
                assert(decoder.output_format().leading_padding_frames > 0U);
            }
            if (std::string(format.extension) == "wma") {
                assert(decoder.output_format().timestamp_quantization_frames
                       > 1U);
            }
            decoder.close();
            std::filesystem::remove(path);
        }

        const std::filesystem::path vbr_path = source.parent_path()
            / ("frequency-format-vbr-"
               + std::to_string(tone_case.dominant_band) + ".mp3");
        const std::string vbr_path_utf8 = vbr_path.string();
        ag_transcode_request_v2 request{};
        request.struct_size = sizeof(request);
        request.api_version = AG_TRANSCODE_REQUEST_V2_VERSION;
        request.output_path = vbr_path_utf8.c_str();
        request.muxer_name = "mp3";
        request.codec_name = "libmp3lame";
        request.sample_rate = 44'100;
        request.channel_layout = "stereo";
        request.audio_stream_index = 0;
        request.bitrate_mode = 1;
        request.quality = 75;
        std::filesystem::remove(vbr_path);
        assert(ag_transcode_v2(tone_source.string().c_str(), &request, nullptr,
                               nullptr, nullptr) == AG_OK);
        agplayer::FrequencyColorWaveformData vbr_output;
        assert(agplayer::FrequencyColorWaveformAnalyzer::analyze(
                   vbr_path.string(), 2'000U, nullptr, nullptr, nullptr,
                   vbr_output, nullptr) == AG_OK);
        const float vbr_bands[] = {maximum(vbr_output.low),
                                   maximum(vbr_output.mid),
                                   maximum(vbr_output.high)};
        for (int band = 0; band < 3; ++band) {
            if (band != tone_case.dominant_band) {
                assert(vbr_bands[tone_case.dominant_band] > vbr_bands[band]);
            }
        }
        std::filesystem::remove(vbr_path);
        std::filesystem::remove(tone_source);
    }
}

void test_missing_timeline_returns_unsupported(
    const std::filesystem::path& source,
    const std::filesystem::path& test_executable)
{
    // Catches silently sizing buckets from decoded EOF when metadata has no timeline.
    const std::filesystem::path durationless = source.parent_path()
        / "frequency-durationless.wav";
    const std::filesystem::path executable =
        std::filesystem::canonical(test_executable);
    const std::filesystem::path generator = executable.parent_path()
        / ("fixture_generator" + executable.extension().string());
    const std::string generator_name = generator.string();
    const std::string output_name = durationless.string();
#if defined(_WIN32)
    const char* arguments[] = {generator_name.c_str(), output_name.c_str(),
                               "durationless", nullptr};
    assert(_spawnv(_P_WAIT, generator_name.c_str(), arguments) == 0);
#else
    const pid_t child = fork();
    assert(child >= 0);
    if (child == 0) {
        execl(generator_name.c_str(), generator_name.c_str(),
              output_name.c_str(), "durationless", nullptr);
        _exit(127);
    }
    int status = 0;
    assert(waitpid(child, &status, 0) >= 0);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
#endif

    agplayer::FrequencyColorWaveformData output;
    const ag_result result = agplayer::FrequencyColorWaveformAnalyzer::analyze(
        durationless.string(), 2'000U, nullptr, nullptr, nullptr, output, nullptr);
    assert(result == AG_UNSUPPORTED_FORMAT);
    assert(output.mix.empty());
    assert(output.low.empty());
    assert(output.mid.empty());
    assert(output.high.empty());
    std::filesystem::remove(durationless);
}

} // namespace

int main(const int argc, char** argv)
{
    assert(argc == 2);
    test_lr4_is_finite_and_minus_six_db_at_crossover();
    test_band_splitter_preserves_state_across_decoder_blocks();
    test_frequency_bands_have_expected_tone_dominance();
    test_fixed_timeline_bucket_mapping_and_gaps();
    test_2001_frames_map_to_exactly_2000_buckets();
    test_overlapping_pts_and_nonfinite_samples_are_rejected();
    test_timeline_policy_uses_declared_timestamp_sources();
    test_negative_preroll_tracks_signed_source_continuity();
    test_all_layers_share_one_normalization_reference_and_gate();
    test_peak_rms_weights_shared_percentile_and_exact_cap();
    test_exact_band_gains_and_smoothing_windows();
    test_smoothing_precedes_shared_reference_and_gate_is_exact();
    test_deterministic_program_material_profiles();
    test_symmetric_smoothing_preserves_a_constant_band_at_edges();
    test_analyzer_is_one_pass_bounded_and_clears_cancelled_output(argv[1]);
    test_public_analyzer_tracks_real_pcm_program_material(argv[1]);
    test_low_sample_rates_use_safe_fallback(argv[1]);
    test_decoder_pcm_matrix_preserves_band_dominance(argv[1]);
    test_repository_lossless_lossy_vbr_formats_preserve_band_dominance(
        argv[1]);
    test_missing_timeline_returns_unsupported(argv[1], argv[0]);
}
