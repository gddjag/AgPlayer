#undef NDEBUG

#include "graphic_equalizer.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace {

constexpr double kToleranceDb = 0.3;

bool near(double actual, double expected, double tolerance)
{
    return std::abs(actual - expected) <= tolerance;
}

double measured_sine_gain_db(std::size_t band, double gain_db,
                             int sample_rate = 48'000)
{
    agplayer::GraphicEqSettings settings;
    settings.band_gain_db[band] = gain_db;
    settings.auto_clip_protection = false;
    settings.transition_ms = 1.0;
    const auto program = agplayer::prepare_graphic_eq(
        settings, sample_rate, static_cast<std::uint64_t>(band + 1));
    assert(program.has_value());

    agplayer::GraphicEqualizerProcessor processor;
    assert(processor.submit(*program));
    const std::size_t frame_count = static_cast<std::size_t>(sample_rate) * 2;
    const std::size_t measurement_start = frame_count / 2;
    std::vector<float> samples(frame_count);
    const double frequency = agplayer::kGraphicEqBandFrequenciesHz[band];
    constexpr double amplitude = 0.1;
    constexpr double pi = 3.14159265358979323846;
    for (std::size_t frame = 0; frame < frame_count; ++frame) {
        samples[frame] = static_cast<float>(
            amplitude * std::sin(2.0 * pi * frequency
                                 * static_cast<double>(frame)
                                 / static_cast<double>(sample_rate)));
    }

    processor.process(samples.data(), frame_count, 1);
    double input_energy = 0.0;
    double output_energy = 0.0;
    for (std::size_t frame = measurement_start; frame < frame_count; ++frame) {
        const double input = amplitude * std::sin(
            2.0 * pi * frequency * static_cast<double>(frame)
            / static_cast<double>(sample_rate));
        input_energy += input * input;
        output_energy += static_cast<double>(samples[frame]) * samples[frame];
    }
    return 10.0 * std::log10(output_energy / input_energy);
}

void test_fixed_band_frequencies()
{
    constexpr std::array<double, 18> expected{
        20.0, 31.5, 50.0, 80.0, 125.0, 200.0, 315.0, 500.0, 800.0,
        1'250.0, 2'000.0, 3'150.0, 5'000.0, 8'000.0, 10'000.0, 12'500.0,
        16'000.0, 20'000.0};
    static_assert(agplayer::kGraphicEqBandCount == expected.size());
    for (std::size_t index = 0; index < expected.size(); ++index) {
        assert(agplayer::kGraphicEqBandFrequenciesHz[index] == expected[index]);
    }

    agplayer::GraphicEqSettings defaults;
    assert(near(defaults.q, agplayer::kGraphicEqDefaultQ, 1.0e-12));
    assert(near(agplayer::kGraphicEqDefaultQ, 2.145, 1.0e-12));
}

void test_supported_sample_rates_and_boundaries()
{
    for (const int sample_rate : {44'100, 48'000, 88'200, 96'000, 192'000}) {
        agplayer::GraphicEqSettings settings;
        const auto program = agplayer::prepare_graphic_eq(settings, sample_rate, 1);
        assert(program.has_value());
        assert(program->sample_rate == sample_rate);
    }

    agplayer::GraphicEqSettings preamp_boundary;
    preamp_boundary.preamp_db = -18.0;
    assert(agplayer::prepare_graphic_eq(preamp_boundary, 48'000, 1).has_value());
    preamp_boundary.preamp_db = 18.0;
    assert(agplayer::prepare_graphic_eq(preamp_boundary, 48'000, 1).has_value());

    agplayer::GraphicEqSettings invalid;
    invalid.preamp_db = -18.1;
    assert(!agplayer::prepare_graphic_eq(invalid, 48'000, 1).has_value());
    invalid.preamp_db = 18.1;
    assert(!agplayer::prepare_graphic_eq(invalid, 48'000, 1).has_value());

    invalid = {};
    invalid.band_gain_db[3] = -18.1;
    assert(!agplayer::prepare_graphic_eq(invalid, 48'000, 1).has_value());

    invalid = {};
    invalid.band_gain_db[3] = 18.1;
    assert(!agplayer::prepare_graphic_eq(invalid, 48'000, 1).has_value());

    agplayer::GraphicEqSettings boundary;
    boundary.band_gain_db[3] = -18.0;
    assert(agplayer::prepare_graphic_eq(boundary, 48'000, 1).has_value());
    boundary.band_gain_db[3] = 18.0;
    assert(agplayer::prepare_graphic_eq(boundary, 48'000, 1).has_value());

    invalid = {};
    invalid.q = std::numeric_limits<double>::quiet_NaN();
    assert(!agplayer::prepare_graphic_eq(invalid, 48'000, 1).has_value());

    assert(!agplayer::prepare_graphic_eq({}, 32'000, 1).has_value());
}

void test_center_frequency_response()
{
    for (const int sample_rate : {44'100, 48'000, 88'200, 96'000, 192'000}) {
        for (std::size_t band = 0; band < agplayer::kGraphicEqBandCount; ++band) {
            for (const double gain_db : {-6.0, 6.0}) {
                agplayer::GraphicEqSettings settings;
                settings.band_gain_db[band] = gain_db;
                const auto program = agplayer::prepare_graphic_eq(
                    settings, sample_rate, static_cast<std::uint64_t>(band + 1));
                assert(program.has_value());
                const double response = agplayer::graphic_eq_response_db(
                    *program, agplayer::kGraphicEqBandFrequenciesHz[band]);
                assert(near(response, gain_db, kToleranceDb));
            }
        }
    }
}

void test_twenty_kilohertz_band_at_cd_sample_rate()
{
    agplayer::GraphicEqSettings settings;
    settings.auto_clip_protection = false;
    settings.band_gain_db.back() = 6.0;
    const auto program = agplayer::prepare_graphic_eq(settings, 44'100, 17);
    assert(program.has_value());
    const double response = agplayer::graphic_eq_response_db(
        *program, 20'000.0);
    assert(std::isfinite(response));
    assert(near(response, 6.0, kToleranceDb));
}

void test_pcm_sine_response_matches_every_band()
{
    for (std::size_t band = 0; band < agplayer::kGraphicEqBandCount; ++band) {
        for (const double gain_db : {-6.0, 6.0}) {
            assert(near(measured_sine_gain_db(band, gain_db),
                        gain_db, kToleranceDb));
        }
    }
}

void test_ten_kilohertz_band_changes_steady_state_sine_rms()
{
    constexpr std::size_t ten_kilohertz_band = 14;
    assert(agplayer::kGraphicEqBandFrequenciesHz[ten_kilohertz_band]
           == 10'000.0);
    assert(measured_sine_gain_db(ten_kilohertz_band, 6.0) > 0.0);
}

void write_measurement_evidence_if_requested()
{
#ifdef _WIN32
    char* environment_value = nullptr;
    std::size_t environment_size = 0;
    if (_dupenv_s(&environment_value, &environment_size,
                  "AGPLAYER_EQ_MEASUREMENT_CSV") != 0
        || environment_value == nullptr) {
        return;
    }
    const std::string path(environment_value);
    std::free(environment_value);
#else
    const char* environment_value = std::getenv("AGPLAYER_EQ_MEASUREMENT_CSV");
    if (environment_value == nullptr || *environment_value == '\0') {
        return;
    }
    const std::string path(environment_value);
#endif
    std::ofstream output(path, std::ios::trunc);
    assert(output.is_open());
    output << "sample_rate_hz,band_hz,target_db,analytic_db,pcm_measured_db\n";
    for (std::size_t band = 0; band < agplayer::kGraphicEqBandCount; ++band) {
        for (const double gain_db : {-6.0, 6.0}) {
            agplayer::GraphicEqSettings settings;
            settings.band_gain_db[band] = gain_db;
            settings.auto_clip_protection = false;
            const auto program = agplayer::prepare_graphic_eq(
                settings, 48'000, static_cast<std::uint64_t>(band + 1));
            assert(program.has_value());
            output << 48'000 << ','
                   << agplayer::kGraphicEqBandFrequenciesHz[band] << ','
                   << gain_db << ','
                   << agplayer::graphic_eq_response_db(
                          *program, agplayer::kGraphicEqBandFrequenciesHz[band])
                   << ',' << measured_sine_gain_db(band, gain_db) << '\n';
        }
    }
}

void test_flat_and_automatic_protection()
{
    agplayer::GraphicEqSettings flat;
    const auto flat_program = agplayer::prepare_graphic_eq(flat, 48'000, 1);
    assert(flat_program.has_value());
    assert(flat_program->flat);
    assert(near(flat_program->protection_db, 0.0, 1e-9));
    assert(near(flat_program->output_gain, 1.0, 1e-12));
    assert(near(agplayer::graphic_eq_response_db(*flat_program, 1'000.0),
                0.0, 1e-9));

    agplayer::GraphicEqSettings boosted;
    boosted.band_gain_db.fill(12.0);
    boosted.preamp_db = 6.0;
    const auto protected_program =
        agplayer::prepare_graphic_eq(boosted, 48'000, 2);
    assert(protected_program.has_value());
    assert(protected_program->protection_db < 0.0);
    assert(near(protected_program->output_gain,
                std::pow(10.0, (boosted.preamp_db
                                 + protected_program->protection_db) / 20.0),
                1e-12));

    double protected_peak = -1'000.0;
    for (std::size_t index = 0; index < 2'048; ++index) {
        const double ratio = static_cast<double>(index) / 2'047.0;
        const double frequency = 20.0 * std::pow(1'000.0, ratio);
        protected_peak = std::max(
            protected_peak,
            agplayer::graphic_eq_response_db(*protected_program, frequency)
                + protected_program->protection_db);
    }
    assert(protected_peak <= -0.49);
}

void test_flat_processing_is_transparent()
{
    const auto program = agplayer::prepare_graphic_eq({}, 48'000, 1);
    assert(program.has_value());
    agplayer::GraphicEqualizerProcessor processor;
    assert(processor.submit(*program));

    std::array<float, 12> samples{
        -1.0F, 1.0F, -0.75F, 0.75F, -0.25F, 0.25F,
        0.0F, 0.0F, 0.25F, -0.25F, 0.75F, -0.75F};
    const auto original = samples;
    processor.process(samples.data(), samples.size() / 2, 2);
    assert(samples == original);
}

void test_channel_state_is_independent()
{
    agplayer::GraphicEqSettings settings;
    settings.band_gain_db[5] = 12.0;
    settings.auto_clip_protection = false;
    settings.transition_ms = 1.0;
    const auto program = agplayer::prepare_graphic_eq(settings, 48'000, 1);
    assert(program.has_value());

    agplayer::GraphicEqualizerProcessor processor;
    assert(processor.submit(*program));
    std::vector<float> samples(4'096 * 2, 0.0F);
    for (std::size_t frame = 0; frame < 4'096; ++frame) {
        samples[frame * 2] = static_cast<float>(
            0.25 * std::sin(2.0 * 3.14159265358979323846 * 1'000.0
                            * static_cast<double>(frame) / 48'000.0));
    }
    processor.process(samples.data(), 4'096, 2);

    double right_energy = 0.0;
    double left_energy = 0.0;
    for (std::size_t frame = 0; frame < 4'096; ++frame) {
        left_energy += std::abs(samples[frame * 2]);
        right_energy += std::abs(samples[frame * 2 + 1]);
    }
    assert(left_energy > 1.0);
    assert(right_energy == 0.0);
}

void test_updates_and_bypass_are_smoothed_and_finite()
{
    agplayer::GraphicEqSettings flat;
    flat.transition_ms = 25.0;
    const auto first = agplayer::prepare_graphic_eq(flat, 48'000, 1);
    assert(first.has_value());

    agplayer::GraphicEqSettings boosted = flat;
    boosted.preamp_db = 12.0;
    boosted.auto_clip_protection = false;
    const auto second = agplayer::prepare_graphic_eq(boosted, 48'000, 2);
    assert(second.has_value());

    agplayer::GraphicEqualizerProcessor processor;
    assert(processor.submit(*first));
    std::vector<float> samples(4'096, 0.25F);
    processor.process(samples.data(), 1'024, 2);
    assert(processor.submit(*second));
    processor.process(samples.data() + 2'048, 1'024, 2);

    const float first_changed = samples[2'048];
    const float last_changed = samples.back();
    assert(first_changed >= 0.24F && first_changed < 0.35F);
    assert(last_changed > first_changed);
    for (const float sample : samples) {
        assert(std::isfinite(sample));
    }

    boosted.bypassed = true;
    const auto bypassed = agplayer::prepare_graphic_eq(boosted, 48'000, 3);
    assert(bypassed.has_value());
    assert(processor.submit(*bypassed));
    std::array<float, 4> bypass_samples{0.25F, 0.25F, 0.25F, 0.25F};
    processor.process(bypass_samples.data(), 2, 2);
    assert(bypass_samples.front() > 0.24F);
    assert(bypass_samples.front() < last_changed);

    std::array<float, 4> invalid{
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(), 0.5F};
    processor.process(invalid.data(), 2, 2);
    for (const float sample : invalid) {
        assert(std::isfinite(sample));
    }
}

void test_full_band_eight_channel_192khz_transition_stays_finite()
{
    agplayer::GraphicEqSettings firstSettings;
    firstSettings.auto_clip_protection = false;
    firstSettings.transition_ms = 1.0;
    for (std::size_t band = 0; band < firstSettings.band_gain_db.size(); ++band)
        firstSettings.band_gain_db[band] = band % 2 == 0 ? 12.0 : -12.0;

    auto secondSettings = firstSettings;
    for (double& gain : secondSettings.band_gain_db)
        gain = -gain;
    const auto first = agplayer::prepare_graphic_eq(firstSettings, 192'000, 1);
    const auto second = agplayer::prepare_graphic_eq(secondSettings, 192'000, 2);
    assert(first.has_value());
    assert(second.has_value());

    agplayer::GraphicEqualizerProcessor processor;
    assert(processor.submit(*first));
    constexpr std::size_t channels = agplayer::kGraphicEqMaxChannels;
    constexpr std::size_t frames = 4'096;
    std::vector<float> samples(frames * channels, 0.125F);
    processor.process(samples.data(), frames / 2, channels);
    assert(processor.submit(*second));
    processor.process(samples.data() + frames / 2 * channels,
                      frames / 2, channels);
    for (const float sample : samples)
        assert(std::isfinite(sample));
}

void test_reset_clears_filter_memory()
{
    agplayer::GraphicEqSettings settings;
    settings.band_gain_db[0] = 12.0;
    settings.auto_clip_protection = false;
    const auto program = agplayer::prepare_graphic_eq(settings, 48'000, 1);
    assert(program.has_value());

    agplayer::GraphicEqualizerProcessor processor;
    assert(processor.submit(*program));
    std::array<float, 512> impulse{};
    impulse[0] = 1.0F;
    processor.process(impulse.data(), impulse.size(), 1);
    processor.reset();

    std::array<float, 64> silence{};
    processor.process(silence.data(), silence.size(), 1);
    for (const float sample : silence) {
        assert(sample == 0.0F);
    }
}

void test_ui_updates_are_coalesced_without_losing_the_latest_program()
{
    agplayer::GraphicEqualizerProcessor processor;
    for (std::uint64_t version = 1; version <= 1'000; ++version) {
        agplayer::GraphicEqSettings settings;
        settings.auto_clip_protection = false;
        settings.transition_ms = 1.0;
        settings.preamp_db = version == 1'000 ? 6.0 : -6.0;
        const auto program = agplayer::prepare_graphic_eq(
            settings, 48'000, version);
        assert(program.has_value());
        assert(processor.submit(*program));
    }

    std::vector<float> samples(4'096, 0.25F);
    processor.process(samples.data(), samples.size(), 1);
    assert(samples.back() > 0.48F);
}

} // namespace

int main()
{
    test_fixed_band_frequencies();
    test_supported_sample_rates_and_boundaries();
    test_center_frequency_response();
    test_twenty_kilohertz_band_at_cd_sample_rate();
    test_pcm_sine_response_matches_every_band();
    test_ten_kilohertz_band_changes_steady_state_sine_rms();
    test_flat_and_automatic_protection();
    test_flat_processing_is_transparent();
    test_channel_state_is_independent();
    test_updates_and_bypass_are_smoothed_and_finite();
    test_full_band_eight_channel_192khz_transition_stays_finite();
    test_reset_clears_filter_memory();
    test_ui_updates_are_coalesced_without_losing_the_latest_program();
    write_measurement_evidence_if_requested();
    return 0;
}
