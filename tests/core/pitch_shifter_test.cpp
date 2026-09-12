#undef NDEBUG

#include "pitch_shifter.hpp"

#include "decoder.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

void write_u16(std::ostream& stream, const std::uint16_t value)
{
    const std::array<char, 2> bytes{static_cast<char>(value & 0xffU),
        static_cast<char>((value >> 8U) & 0xffU)};
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void write_u32(std::ostream& stream, const std::uint32_t value)
{
    const std::array<char, 4> bytes{static_cast<char>(value & 0xffU),
        static_cast<char>((value >> 8U) & 0xffU),
        static_cast<char>((value >> 16U) & 0xffU),
        static_cast<char>((value >> 24U) & 0xffU)};
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::filesystem::path make_long_sine_fixture()
{
    constexpr std::uint32_t sample_rate = 48'000U;
    constexpr std::uint32_t frames = sample_rate * 10U;
    const auto path = std::filesystem::temp_directory_path()
        / "agplayer-pitch-shifter-long.wav";
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write("RIFF", 4);
    write_u32(stream, 36U + frames * 2U);
    stream.write("WAVEfmt ", 8);
    write_u32(stream, 16U);
    write_u16(stream, 1U);
    write_u16(stream, 1U);
    write_u32(stream, sample_rate);
    write_u32(stream, sample_rate * 2U);
    write_u16(stream, 2U);
    write_u16(stream, 16U);
    stream.write("data", 4);
    write_u32(stream, frames * 2U);
    const double step = 2.0 * std::acos(-1.0) * 440.0 / sample_rate;
    for (std::uint32_t frame = 0U; frame < frames; ++frame) {
        const auto sample = static_cast<std::int16_t>(std::lround(
            16'000.0 * std::sin(step * frame)));
        write_u16(stream, static_cast<std::uint16_t>(sample));
    }
    assert(stream.good());
    return path;
}

std::size_t decoded_frames(const std::filesystem::path& path)
{
    agplayer::Decoder decoder;
    assert(decoder.open(path.string()) == AG_OK);
    agplayer::DecodedAudioBlock block;
    std::size_t frames = 0U;
    do {
        assert(decoder.read(block) == AG_OK);
        frames += block.frames;
    } while (!block.end_of_stream);
    return frames;
}

std::filesystem::path make_corrupt_adpcm_wav(const std::filesystem::path& source)
{
    const std::filesystem::path dest =
        source.parent_path() / "corrupt-adpcm.wav";
    std::filesystem::remove(dest);
    std::filesystem::copy_file(
        source, dest, std::filesystem::copy_options::overwrite_existing);

    std::fstream file(dest, std::ios::in | std::ios::out | std::ios::binary);
    assert(file);
    file.seekp(20, std::ios::beg);
    const char tag[] = {static_cast<char>(0x02), static_cast<char>(0x00)};
    file.write(tag, sizeof(tag));
    assert(file);
    return dest;
}

// Simple second-order high-pass filter for measuring high-frequency energy.
class HighPassFilter {
public:
    HighPassFilter(double cutoff_hz, double sample_rate) noexcept
    {
        const double pi = std::acos(-1.0);
        const double omega = 2.0 * pi * cutoff_hz / sample_rate;
        const double cos_omega = std::cos(omega);
        const double sin_omega = std::sin(omega);
        const double alpha = sin_omega / (2.0 * 0.707);

        b0_ = (1.0 + cos_omega) / 2.0;
        b1_ = -(1.0 + cos_omega);
        b2_ = (1.0 + cos_omega) / 2.0;
        a0_ = 1.0 + alpha;
        a1_ = -2.0 * cos_omega;
        a2_ = 1.0 - alpha;

        b0_ /= a0_; b1_ /= a0_; b2_ /= a0_;
        a1_ /= a0_; a2_ /= a0_;
    }

    float process(float input) noexcept
    {
        const double output = b0_ * input + b1_ * x1_ + b2_ * x2_
                              - a1_ * y1_ - a2_ * y2_;
        x2_ = x1_;
        x1_ = input;
        y2_ = y1_;
        y1_ = output;
        return static_cast<float>(output);
    }

private:
    double b0_ = 0.0, b1_ = 0.0, b2_ = 0.0;
    double a1_ = 0.0, a2_ = 0.0, a0_ = 1.0;
    double x1_ = 0.0, x2_ = 0.0;
    double y1_ = 0.0, y2_ = 0.0;
};

float high_frequency_energy(const std::filesystem::path& path,
                            double cutoff_hz)
{
    agplayer::Decoder decoder;
    if (decoder.open(path.string()) != AG_OK) {
        return -1.0f;
    }
    const int sample_rate = decoder.metadata().sample_rate;
    if (sample_rate <= 0) {
        return -1.0f;
    }
    HighPassFilter hp(cutoff_hz, sample_rate);
    agplayer::DecodedAudioBlock block;
    double sum = 0.0;
    std::size_t count = 0;
    do {
        if (decoder.read(block) != AG_OK) {
            return -1.0f;
        }
        for (float sample : block.samples) {
            const float filtered = hp.process(sample);
            sum += static_cast<double>(filtered) * filtered;
            ++count;
        }
    } while (!block.end_of_stream);
    if (count == 0) {
        return 0.0f;
    }
    return static_cast<float>(sum / static_cast<double>(count));
}

std::pair<double, double> edge_energy(const std::filesystem::path& path)
{
    agplayer::Decoder decoder;
    assert(decoder.open(path.string()) == AG_OK);
    agplayer::DecodedAudioBlock block;
    std::vector<float> samples;
    do {
        assert(decoder.read(block) == AG_OK);
        samples.insert(samples.end(), block.samples.begin(), block.samples.end());
    } while (!block.end_of_stream);
    assert(samples.size() > 2'000U);

    constexpr std::size_t window = 500U;
    const auto average = [](const auto begin, const auto end) {
        double sum = 0.0;
        for (auto it = begin; it != end; ++it) {
            sum += std::abs(static_cast<double>(*it));
        }
        return sum / static_cast<double>(std::distance(begin, end));
    };
    return {
        average(samples.begin(), samples.begin() + window),
        average(samples.end() - window, samples.end())
    };
}

} // namespace

int main(const int argc, char** argv)
{
    assert(argc == 2);
    const std::filesystem::path input_path = argv[1];

    // Happy path: valid sine wave pitch-shifts successfully.
    const std::filesystem::path happy_output =
        input_path.parent_path() / "pitch-shifter-out.wav";
    std::filesystem::remove(happy_output);

    agplayer::PitchShiftConfig happy_config;
    happy_config.pitch_cents = 100;
    happy_config.keep_tempo = true;
    happy_config.tempo_ratio = 1.0;
    happy_config.output_path = happy_output.string();
    happy_config.output_codec_name = "pcm_s16le";
    std::string error;
    ag_result result =
        agplayer::pitch_shift(input_path.string(), happy_config,
                              nullptr, nullptr, error);
    if (result != AG_OK) {
        std::cerr << "pitch_shift happy path failed: " << static_cast<int>(result)
                  << " " << error << "\n";
    }
    assert(result == AG_OK);
    assert(std::filesystem::exists(happy_output));

    // A real long offline render must drain the streaming processor while it
    // feeds it.  This is longer than the engine's fixed output FIFO.
    const auto long_input = make_long_sine_fixture();
    const auto long_output = long_input.parent_path()
        / "agplayer-pitch-shifter-long-output.wav";
    std::filesystem::remove(long_output);
    agplayer::PitchShiftConfig long_config;
    long_config.output_path = long_output.string();
    long_config.output_codec_name = "pcm_s16le";
    long_config.keep_tempo = true;
    long_config.tempo_ratio = 0.5;
    error.clear();
    result = agplayer::pitch_shift(long_input.string(), long_config,
                                   nullptr, nullptr, error);
    if (result != AG_OK) {
        std::cerr << "long pitch_shift failed: " << static_cast<int>(result)
                  << " " << error << "\n";
    }
    assert(result == AG_OK);
    assert(std::filesystem::exists(long_output));
    const auto long_frames = decoded_frames(long_output);
    assert(std::llabs(static_cast<long long>(long_frames) - 960'000LL)
           < 4'096LL);

    // Failure path: corrupted ADPCM-tagged WAV triggers avcodec_send_packet
    // failure. The tool must return a non-AG_OK status and must not leave a
    // complete/successful output file.
    const std::filesystem::path corrupt_path = make_corrupt_adpcm_wav(input_path);
    const std::filesystem::path failure_output =
        corrupt_path.parent_path() / "pitch-shifter-out.wav";
    std::filesystem::remove(failure_output);

    agplayer::PitchShiftConfig failure_config;
    failure_config.pitch_cents = 100;
    failure_config.keep_tempo = true;
    failure_config.tempo_ratio = 1.0;
    failure_config.output_path = failure_output.string();
    failure_config.output_codec_name = "pcm_s16le";
    error.clear();
    result = agplayer::pitch_shift(corrupt_path.string(), failure_config,
                                   nullptr, nullptr, error);
    if (result == AG_OK) {
        std::cerr << "expected pitch_shift failure for corrupted input\n";
    }
    assert(result != AG_OK);
    assert(!std::filesystem::exists(failure_output));

    // Vocal protection comparison. Spectral-envelope preservation is not a
    // low-pass brightness control; the controlled formant-centroid contract
    // is covered by editor_playback_stream_test. This regression verifies
    // that the offline export path applies the same non-hollow processing.
    const std::filesystem::path protected_output =
        input_path.parent_path() / "pitch-shifter-protected.wav";
    const std::filesystem::path unprotected_output =
        input_path.parent_path() / "pitch-shifter-unprotected.wav";
    std::filesystem::remove(protected_output);
    std::filesystem::remove(unprotected_output);

    agplayer::PitchShiftConfig protected_config;
    protected_config.pitch_cents = 400;
    protected_config.keep_tempo = true;
    protected_config.tempo_ratio = 1.0;
    protected_config.output_path = protected_output.string();
    protected_config.output_codec_name = "pcm_s16le";
    protected_config.vocal_protection = true;

    agplayer::PitchShiftConfig unprotected_config = protected_config;
    unprotected_config.output_path = unprotected_output.string();
    unprotected_config.vocal_protection = false;

    error.clear();
    result = agplayer::pitch_shift(input_path.string(), protected_config,
                                   nullptr, nullptr, error);
    if (result != AG_OK) {
        std::cerr << "pitch_shift vocal protection (protected) failed: "
                  << static_cast<int>(result) << " " << error << "\n";
    }
    assert(result == AG_OK);
    assert(std::filesystem::exists(protected_output));

    error.clear();
    result = agplayer::pitch_shift(input_path.string(), unprotected_config,
                                   nullptr, nullptr, error);
    if (result != AG_OK) {
        std::cerr << "pitch_shift vocal protection (unprotected) failed: "
                  << static_cast<int>(result) << " " << error << "\n";
    }
    assert(result == AG_OK);
    assert(std::filesystem::exists(unprotected_output));

    constexpr double high_freq_cutoff = 6000.0;
    const float protected_energy =
        high_frequency_energy(protected_output, high_freq_cutoff);
    const float unprotected_energy =
        high_frequency_energy(unprotected_output, high_freq_cutoff);
    assert(protected_energy >= 0.0f);
    assert(unprotected_energy >= 0.0f);
    assert(std::abs(protected_energy - unprotected_energy) > 0.000001F);

    // Smooth transition applies a short boundary envelope without changing
    // the pitch/tempo settings.
    const std::filesystem::path smooth_output =
        input_path.parent_path() / "pitch-shifter-smooth.wav";
    const std::filesystem::path abrupt_output =
        input_path.parent_path() / "pitch-shifter-abrupt.wav";
    std::filesystem::remove(smooth_output);
    std::filesystem::remove(abrupt_output);

    agplayer::PitchShiftConfig smooth_config;
    smooth_config.output_path = smooth_output.string();
    smooth_config.output_codec_name = "pcm_s16le";
    smooth_config.pitch_cents = 0;
    smooth_config.keep_tempo = true;
    smooth_config.tempo_ratio = 1.0;
    smooth_config.smooth_transition = true;

    agplayer::PitchShiftConfig abrupt_config = smooth_config;
    abrupt_config.output_path = abrupt_output.string();
    abrupt_config.smooth_transition = false;

    error.clear();
    assert(agplayer::pitch_shift(input_path.string(), smooth_config,
                                 nullptr, nullptr, error) == AG_OK);
    error.clear();
    assert(agplayer::pitch_shift(input_path.string(), abrupt_config,
                                 nullptr, nullptr, error) == AG_OK);

    const auto smooth_edges = edge_energy(smooth_output);
    const auto abrupt_edges = edge_energy(abrupt_output);
    assert(smooth_edges.first < abrupt_edges.first * 0.75);
    assert(smooth_edges.second < abrupt_edges.second * 0.75);

    std::filesystem::remove(happy_output);
    std::filesystem::remove(long_input);
    std::filesystem::remove(long_output);
    std::filesystem::remove(corrupt_path);
    std::filesystem::remove(protected_output);
    std::filesystem::remove(unprotected_output);
    std::filesystem::remove(smooth_output);
    std::filesystem::remove(abrupt_output);
    return 0;
}
