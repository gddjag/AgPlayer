#undef NDEBUG

#include "waveform_analyzer.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <numeric>
#include <vector>

namespace {

constexpr double pi = 3.14159265358979323846;
constexpr float sample_rate = 48'000.0F;
constexpr std::size_t frames = 48'000U;

std::vector<float> tone(const double frequency_hz)
{
    std::vector<float> samples(frames);
    for (std::size_t frame = 0U; frame < frames; ++frame) {
        samples[frame] = static_cast<float>(
            0.6 * std::sin(2.0 * pi * frequency_hz
                           * static_cast<double>(frame) / sample_rate));
    }
    return samples;
}

std::vector<float> equal_amplitude_mix()
{
    constexpr double amplitude = 0.003;
    std::vector<float> samples(frames);
    for (std::size_t frame = 0U; frame < frames; ++frame) {
        const double time = static_cast<double>(frame) / sample_rate;
        double value = amplitude * (std::sin(2.0 * pi * 100.0 * time)
                                    + std::sin(2.0 * pi * 1'000.0 * time));
        if (frame >= 24'000U && frame < 24'050U) {
            value += amplitude * std::sin(2.0 * pi * 10'000.0 * time);
        }
        samples[frame] = static_cast<float>(value);
    }
    return samples;
}

std::vector<float> continuous_equal_amplitude_three_tone_mix()
{
    std::vector<float> samples(frames);
    for (std::size_t frame = 0U; frame < frames; ++frame) {
        const double time = static_cast<double>(frame) / sample_rate;
        samples[frame] = static_cast<float>(
            0.2 * (std::sin(2.0 * pi * 100.0 * time)
                   + std::sin(2.0 * pi * 1'000.0 * time)
                   + std::sin(2.0 * pi * 10'000.0 * time)));
    }
    return samples;
}

std::vector<float> low_mid_bed_with_short_high_burst()
{
    std::vector<float> samples(frames);
    for (std::size_t frame = 0U; frame < frames; ++frame) {
        const double time = static_cast<double>(frame) / sample_rate;
        double value = 0.3 * std::sin(2.0 * pi * 100.0 * time)
                       + 0.3 * std::sin(2.0 * pi * 1'000.0 * time);
        if (frame < 50U) {
            value += 0.3 * std::sin(2.0 * pi * 10'000.0 * time);
        }
        samples[frame] = static_cast<float>(value);
    }
    return samples;
}

void assert_finite_unit_interval(const std::vector<float>& values)
{
    for (const float value : values) {
        assert(std::isfinite(value));
        assert(value >= 0.0F);
        assert(value <= 1.0F);
    }
}

struct BandEnergy final {
    double bass = 0.0;
    double mid = 0.0;
    double high = 0.0;
};

double settled_mean(const std::vector<float>& values)
{
    assert(values.size() > 4U);
    return std::accumulate(values.begin() + 2, values.end() - 2, 0.0)
        / static_cast<double>(values.size() - 4U);
}

BandEnergy analyze_tone(const double frequency_hz)
{
    agplayer::WaveformBucketizer bucketizer(
        frames, 64U, 1U, sample_rate,
        agplayer::WaveformAggregation::AverageAbsolute);
    assert(bucketizer.add(tone(frequency_hz), frames) == AG_OK);

    std::vector<float> mix;
    std::vector<float> bass;
    std::vector<float> mid;
    std::vector<float> high;
    assert(bucketizer.finish(mix, bass, mid, high) == AG_OK);
    assert(mix.size() == 64U);
    assert(bass.size() == mix.size());
    assert(mid.size() == mix.size());
    assert(high.size() == mix.size());
    return {settled_mean(bass), settled_mean(mid), settled_mean(high)};
}

void test_low_mid_high_tones_are_classified_by_the_250_and_4000_hz_split()
{
    const BandEnergy low = analyze_tone(100.0);
    const BandEnergy mid = analyze_tone(1'000.0);
    const BandEnergy upper_mid = analyze_tone(3'000.0);
    const BandEnergy high = analyze_tone(8'000.0);

    assert(low.bass > low.mid);
    assert(low.bass > low.high);
    assert(mid.mid > mid.bass);
    assert(mid.mid > mid.high);
    // 3 kHz is in the smooth transition toward the 4 kHz high-pass cutoff.
    assert(upper_mid.high <= 1.5 * upper_mid.mid);
    assert(high.high > high.bass);
    assert(high.high > high.mid);
}

void test_silence_has_no_frequency_color_energy()
{
    const std::vector<float> silence(frames, 0.0F);
    agplayer::WaveformBucketizer bucketizer(
        frames, 64U, 1U, sample_rate,
        agplayer::WaveformAggregation::AverageAbsolute);
    assert(bucketizer.add(silence, frames) == AG_OK);

    std::vector<float> mix;
    std::vector<float> bass;
    std::vector<float> mid;
    std::vector<float> high;
    assert(bucketizer.finish(mix, bass, mid, high) == AG_OK);
    const auto is_zero = [](const float value) { return value == 0.0F; };
    assert(std::all_of(mix.begin(), mix.end(), is_zero));
    assert(std::all_of(bass.begin(), bass.end(), is_zero));
    assert(std::all_of(mid.begin(), mid.end(), is_zero));
    assert(std::all_of(high.begin(), high.end(), is_zero));
    assert_finite_unit_interval(mix);
    assert_finite_unit_interval(bass);
    assert_finite_unit_interval(mid);
    assert_finite_unit_interval(high);
}

void test_short_10khz_burst_retains_materially_visible_high_energy()
{
    agplayer::WaveformBucketizer bucketizer(
        frames, 64U, 1U, sample_rate,
        agplayer::WaveformAggregation::AverageAbsolute);
    assert(bucketizer.add(low_mid_bed_with_short_high_burst(), frames) == AG_OK);

    std::vector<float> mix;
    std::vector<float> bass;
    std::vector<float> mid;
    std::vector<float> high;
    assert(bucketizer.finish(mix, bass, mid, high) == AG_OK);
    // The 50-frame burst occupies 1/15 of a bucket. Its absolute RMS remains
    // measurable without per-track boosts; the raw peak layer carries crest.
    assert(*std::max_element(high.begin(), high.end()) >= 0.03F);
    assert_finite_unit_interval(mix);
    assert_finite_unit_interval(bass);
    assert_finite_unit_interval(mid);
    assert_finite_unit_interval(high);
}

void test_equal_amplitude_mix_keeps_high_above_twenty_percent_of_low_and_mid()
{
    agplayer::WaveformBucketizer bucketizer(
        frames, 64U, 1U, sample_rate,
        agplayer::WaveformAggregation::AverageAbsolute);
    assert(bucketizer.add(equal_amplitude_mix(), frames) == AG_OK);

    std::vector<float> mix;
    std::vector<float> bass;
    std::vector<float> mid;
    std::vector<float> high;
    assert(bucketizer.finish(mix, bass, mid, high) == AG_OK);
    const float high_peak = *std::max_element(high.begin(), high.end());
    assert(high_peak >= 0.20F * *std::max_element(bass.begin(), bass.end()));
    assert(high_peak >= 0.20F * *std::max_element(mid.begin(), mid.end()));
    assert_finite_unit_interval(mix);
    assert_finite_unit_interval(bass);
    assert_finite_unit_interval(mid);
    assert_finite_unit_interval(high);
}

void test_continuous_equal_amplitude_three_tone_mix_keeps_high_above_twenty_percent()
{
    agplayer::WaveformBucketizer bucketizer(
        frames, 64U, 1U, sample_rate,
        agplayer::WaveformAggregation::AverageAbsolute);
    assert(bucketizer.add(continuous_equal_amplitude_three_tone_mix(), frames)
           == AG_OK);

    std::vector<float> mix;
    std::vector<float> bass;
    std::vector<float> mid;
    std::vector<float> high;
    assert(bucketizer.finish(mix, bass, mid, high) == AG_OK);
    const float high_mean = static_cast<float>(settled_mean(high));
    assert(high_mean >= 0.20F * static_cast<float>(settled_mean(bass)));
    assert(high_mean >= 0.20F * static_cast<float>(settled_mean(mid)));
    assert_finite_unit_interval(mix);
    assert_finite_unit_interval(bass);
    assert_finite_unit_interval(mid);
    assert_finite_unit_interval(high);
}

} // namespace

int main()
{
    // A 0.6 sine has RMS 0.4243; independent per-band normalization or a
    // hardcoded high-band boost destroys amplitude ratios across tracks.
    const auto calibrated_low = analyze_tone(100.0);
    const auto calibrated_high = analyze_tone(8'000.0);
    assert(calibrated_low.bass > 0.39 && calibrated_low.bass < 0.44);
    assert(calibrated_high.high > 0.39 && calibrated_high.high < 0.44);
    test_low_mid_high_tones_are_classified_by_the_250_and_4000_hz_split();
    test_silence_has_no_frequency_color_energy();
    test_equal_amplitude_mix_keeps_high_above_twenty_percent_of_low_and_mid();
    test_continuous_equal_amplitude_three_tone_mix_keeps_high_above_twenty_percent();
    test_short_10khz_burst_retains_materially_visible_high_energy();
}
