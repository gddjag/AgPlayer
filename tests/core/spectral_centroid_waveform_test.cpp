#undef NDEBUG

#include "waveform_analyzer.hpp"

#include <agplayer/c_api.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
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

std::uint8_t median_index_for(const std::vector<float>& samples)
{
    agplayer::WaveformBucketizer bucketizer(
        frames, 64U, 1U, sample_rate,
        agplayer::WaveformAggregation::Peak, true);
    assert(bucketizer.add(samples, frames) == AG_OK);

    std::vector<float> mix;
    std::vector<float> bass;
    std::vector<float> mid;
    std::vector<float> high;
    std::vector<std::uint8_t> spectral_index;
    assert(bucketizer.finish(mix, bass, mid, high, &spectral_index) == AG_OK);
    assert(spectral_index.size() == mix.size());
    assert(spectral_index.size() == 64U);

    std::vector<std::uint8_t> settled(
        spectral_index.begin() + 2, spectral_index.end() - 2);
    std::sort(settled.begin(), settled.end());
    return settled[settled.size() / 2U];
}

void test_centroid_index_tracks_frequency_monotonically()
{
    const std::array<double, 5> frequencies{100.0, 300.0, 1'000.0,
                                             3'000.0, 8'000.0};
    std::array<std::uint8_t, frequencies.size()> indices{};
    for (std::size_t i = 0U; i < frequencies.size(); ++i) {
        indices[i] = median_index_for(tone(frequencies[i]));
    }

    for (std::size_t i = 1U; i < indices.size(); ++i) {
        assert(indices[i] > indices[i - 1U]);
    }
    assert(indices.front() < 64U);
    assert(indices.back() > 192U);
}

void test_silence_is_stable_and_colorless()
{
    const std::vector<float> silence(frames, 0.0F);
    agplayer::WaveformBucketizer bucketizer(
        frames, 64U, 1U, sample_rate,
        agplayer::WaveformAggregation::Peak, true);
    assert(bucketizer.add(silence, frames) == AG_OK);

    std::vector<float> mix;
    std::vector<float> bass;
    std::vector<float> mid;
    std::vector<float> high;
    std::vector<std::uint8_t> spectral_index;
    assert(bucketizer.finish(mix, bass, mid, high, &spectral_index) == AG_OK);
    assert(spectral_index.size() == mix.size());
    assert(std::all_of(spectral_index.begin(), spectral_index.end(),
                       [](const std::uint8_t value) { return value == 0U; }));
}

void test_plain_waveform_skips_spectral_work()
{
    agplayer::WaveformBucketizer bucketizer(
        frames, 64U, 1U, sample_rate,
        agplayer::WaveformAggregation::Peak, false);
    assert(bucketizer.add(tone(1'000.0), frames) == AG_OK);

    std::vector<float> mix;
    std::vector<float> bass;
    std::vector<float> mid;
    std::vector<float> high;
    std::vector<std::uint8_t> spectral_index{255U};
    assert(bucketizer.finish(mix, bass, mid, high, &spectral_index) == AG_OK);
    assert(spectral_index.empty());
}

void test_c_api_exposes_spectral_index_from_unified_analyzer(
    const char* source_path)
{
    ag_waveform* waveform = nullptr;
    assert(ag_waveform_analyze_with_spectral_index(
               source_path, 64U, AG_WAVEFORM_AGGREGATION_AVERAGE_ABSOLUTE,
               nullptr, nullptr, nullptr, &waveform)
           == AG_OK);
    assert(waveform != nullptr);
    assert(ag_waveform_count(waveform) == 64U);
    assert(ag_waveform_spectral_index_count(waveform) == 64U);
    for (std::size_t i = 0U; i < 64U; ++i) {
        assert(ag_waveform_spectral_index(waveform, i) <= 255U);
    }
    ag_waveform_destroy(waveform);
}

} // namespace

int main(const int argc, char** argv)
{
    assert(argc == 2);
    test_centroid_index_tracks_frequency_monotonically();
    test_silence_is_stable_and_colorless();
    test_plain_waveform_skips_spectral_work();
    test_c_api_exposes_spectral_index_from_unified_analyzer(argv[1]);
}
