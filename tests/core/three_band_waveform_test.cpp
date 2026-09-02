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
    const BandEnergy mid = analyze_tone(3'000.0);
    const BandEnergy high = analyze_tone(8'000.0);

    assert(low.bass > low.mid);
    assert(low.bass > low.high);
    assert(mid.mid > mid.bass);
    assert(mid.mid > mid.high);
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
    assert(std::all_of(bass.begin(), bass.end(), is_zero));
    assert(std::all_of(mid.begin(), mid.end(), is_zero));
    assert(std::all_of(high.begin(), high.end(), is_zero));
}

} // namespace

int main()
{
    test_low_mid_high_tones_are_classified_by_the_250_and_4000_hz_split();
    test_silence_has_no_frequency_color_energy();
}
