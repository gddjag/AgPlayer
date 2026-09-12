#undef NDEBUG

#include "lossless/lossless_analyzer.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <random>
#include <string>
#include <vector>

namespace {

void write_le16(std::ofstream& output, const std::uint16_t value)
{
    const std::array<char, 2> bytes{
        static_cast<char>(value & 0xffU),
        static_cast<char>((value >> 8U) & 0xffU),
    };
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void write_le32(std::ofstream& output, const std::uint32_t value)
{
    const std::array<char, 4> bytes{
        static_cast<char>(value & 0xffU),
        static_cast<char>((value >> 8U) & 0xffU),
        static_cast<char>((value >> 16U) & 0xffU),
        static_cast<char>((value >> 24U) & 0xffU),
    };
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void write_le24(std::ofstream& output, const std::int32_t value)
{
    const auto bits = static_cast<std::uint32_t>(value);
    const std::array<char, 3> bytes{
        static_cast<char>(bits & 0xffU),
        static_cast<char>((bits >> 8U) & 0xffU),
        static_cast<char>((bits >> 16U) & 0xffU),
    };
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

template <typename SampleWriter>
void write_pcm_wav(const std::filesystem::path& path,
                   const int sample_rate,
                   const int bits_per_sample,
                   const std::size_t frames,
                   SampleWriter write_sample,
                   const std::uint16_t audio_format = 1U,
                   const std::uint16_t channels = 2U)
{
    const auto bytes_per_sample = static_cast<std::uint16_t>(bits_per_sample / 8);
    const std::uint32_t data_bytes = static_cast<std::uint32_t>(
        frames * channels * bytes_per_sample);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    assert(output);
    output.write("RIFF", 4);
    write_le32(output, 36U + data_bytes);
    output.write("WAVEfmt ", 8);
    write_le32(output, 16U);
    write_le16(output, audio_format);
    write_le16(output, channels);
    write_le32(output, static_cast<std::uint32_t>(sample_rate));
    write_le32(output, static_cast<std::uint32_t>(sample_rate) * channels
                           * bytes_per_sample);
    write_le16(output, static_cast<std::uint16_t>(channels * bytes_per_sample));
    write_le16(output, static_cast<std::uint16_t>(bits_per_sample));
    output.write("data", 4);
    write_le32(output, data_bytes);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        for (std::uint16_t channel = 0; channel < channels; ++channel) {
            write_sample(output, frame, channel);
        }
    }
    assert(output.good());
}

void write_nonfinite_float(const std::filesystem::path& path)
{
    write_pcm_wav(path, 48'000, 32, 48'000U,
                  [](std::ofstream& output, const std::size_t frame,
                     const std::uint16_t channel) {
                      float value = 0.1F;
                      if (frame == 12U && channel == 0U) {
                          value = (std::numeric_limits<float>::quiet_NaN)();
                      } else if (frame == 24U && channel == 1U) {
                          value = (std::numeric_limits<float>::infinity)();
                      }
                      std::uint32_t bits = 0U;
                      std::memcpy(&bits, &value, sizeof(bits));
                      write_le32(output, bits);
                  }, 3U);
}

void write_silence(const std::filesystem::path& path, const int sample_rate)
{
    write_pcm_wav(path, sample_rate, 16, static_cast<std::size_t>(sample_rate),
                  [](std::ofstream& output, std::size_t, std::uint16_t) {
                      write_le16(output, 0U);
                  });
}

void write_low_band_pcm16(const std::filesystem::path& path,
                          const int sample_rate)
{
    const double pi = std::acos(-1.0);
    write_pcm_wav(path, sample_rate, 16,
                  static_cast<std::size_t>(sample_rate) * 2U,
                  [=](std::ofstream& output, const std::size_t frame,
                      const std::uint16_t channel) {
                      const double time = static_cast<double>(frame)
                                          / static_cast<double>(sample_rate);
                      const double frequency = channel == 0U ? 3'000.0 : 7'000.0;
                      const auto value = static_cast<std::int16_t>(std::lround(
                          std::sin(2.0 * pi * frequency * time) * 12'000.0));
                      write_le16(output, static_cast<std::uint16_t>(value));
                  });
}

void write_precise_pcm32(const std::filesystem::path& path,
                         const int sample_rate,
                         const double frequency)
{
    const double pi = std::acos(-1.0);
    write_pcm_wav(path, sample_rate, 32,
                  static_cast<std::size_t>(sample_rate),
                  [=](std::ofstream& output, const std::size_t frame,
                      const std::uint16_t channel) {
                      const double phase = 2.0 * pi * frequency
                          * static_cast<double>(frame)
                          / static_cast<double>(sample_rate);
                      std::int64_t value = static_cast<std::int64_t>(
                          std::llround(std::sin(phase) * 1'200'000'000.0));
                      // Exercise bits below float32's exact integer range.
                      value = (value & ~std::int64_t{0xff})
                              | static_cast<std::int64_t>(
                                  (frame * 73U + channel * 29U) & 0xffU);
                      write_le32(output, static_cast<std::uint32_t>(
                                             static_cast<std::int32_t>(value)));
                  });
}

void write_spectrogram_tone(const std::filesystem::path& path,
                            const double frequency,
                            const std::size_t frames,
                            const std::size_t first_active_frame = 0U,
                            const std::size_t last_active_frame =
                                (std::numeric_limits<std::size_t>::max)())
{
    constexpr int sample_rate = 48'000;
    const double pi = std::acos(-1.0);
    write_pcm_wav(path, sample_rate, 16, frames,
                  [=](std::ofstream& output, const std::size_t frame,
                      const std::uint16_t) {
                      const bool active = frame >= first_active_frame
                          && frame < last_active_frame;
                      const double sample = active
                          ? std::sin(2.0 * pi * frequency
                                     * static_cast<double>(frame)
                                     / static_cast<double>(sample_rate))
                                * 20'000.0
                          : 0.0;
                      write_le16(output, static_cast<std::uint16_t>(
                          static_cast<std::int16_t>(std::lround(sample))));
                  });
}

double spectrogram_bin(const agplayer::lossless::SpectrogramSummary& summary,
                       const std::size_t time_bin,
                       const std::size_t frequency_bin)
{
    assert(time_bin < summary.timeBins);
    assert(frequency_bin < summary.frequencyBins);
    return summary.db[time_bin * summary.frequencyBins + frequency_bin];
}

double spectrogram_row_peak(
    const agplayer::lossless::SpectrogramSummary& summary,
    const std::size_t time_bin)
{
    assert(time_bin < summary.timeBins);
    const auto first = summary.db.begin()
        + static_cast<std::ptrdiff_t>(time_bin * summary.frequencyBins);
    return *std::max_element(
        first, first + static_cast<std::ptrdiff_t>(summary.frequencyBins));
}

void assert_same_classification(
    const agplayer::lossless::AnalysisResult& expected,
    const agplayer::lossless::AnalysisResult& actual)
{
    assert(actual.verdict == expected.verdict);
    assert(actual.confidence == expected.confidence);
    assert(actual.evidence.size() == expected.evidence.size());
    for (std::size_t index = 0U; index < expected.evidence.size(); ++index) {
        assert(actual.evidence[index].code == expected.evidence[index].code);
        assert(actual.evidence[index].family == expected.evidence[index].family);
        assert(actual.evidence[index].direction
               == expected.evidence[index].direction);
        assert(actual.evidence[index].severity
               == expected.evidence[index].severity);
    }
    assert(actual.candidates.size() == expected.candidates.size());
    for (std::size_t index = 0U; index < expected.candidates.size(); ++index) {
        assert(actual.candidates[index].format
               == expected.candidates[index].format);
        assert(actual.candidates[index].confidence
               == expected.candidates[index].confidence);
    }
    assert(actual.chain == expected.chain);
}

void write_expanded_narrowband_pcm24(const std::filesystem::path& path)
{
    constexpr int sample_rate = 96'000;
    const double pi = std::acos(-1.0);
    write_pcm_wav(path, sample_rate, 24,
                  static_cast<std::size_t>(sample_rate),
                  [=](std::ofstream& output, const std::size_t frame,
                      const std::uint16_t) {
                      const auto pcm16 = static_cast<std::int32_t>(std::lround(
                          std::sin(2.0 * pi * 1'000.0
                                   * static_cast<double>(frame)
                                   / static_cast<double>(sample_rate))
                          * 12'000.0));
                      write_le24(output, pcm16 << 8);
                  });
}

std::vector<double> deterministic_noise(const std::size_t frames)
{
    std::vector<double> samples(frames);
    std::uint32_t state = 0x6d2b79f5U;
    for (double& sample : samples) {
        state ^= state << 13U;
        state ^= state >> 17U;
        state ^= state << 5U;
        sample = (static_cast<double>(state) / 4'294'967'295.0 - 0.5) * 0.4;
    }
    return samples;
}

void write_filtered_noise_pcm24(const std::filesystem::path& path,
                                const int sample_rate,
                                const double cutoff_hz,
                                const bool sharp_edge,
                                const double noise_gain = 1.0,
                                const double modulation_hz = 0.0,
                                const std::size_t duration_seconds = 2U)
{
    const std::size_t frames = static_cast<std::size_t>(sample_rate) * duration_seconds;
    auto samples = deterministic_noise(frames);
    if (sharp_edge) {
        // Windowed-sinc anti-imaging filters used by high-quality resamplers
        // leave a steep, persistent transition followed by a deep stopband.
        constexpr std::size_t taps = 257U;
        const double pi = std::acos(-1.0);
        std::array<double, taps> coefficients{};
        double coefficient_sum = 0.0;
        for (std::size_t index = 0U; index < taps; ++index) {
            const double centered = static_cast<double>(index)
                - static_cast<double>(taps - 1U) / 2.0;
            const double normalized_cutoff = cutoff_hz / sample_rate;
            const double sinc = centered == 0.0
                ? 2.0 * normalized_cutoff
                : std::sin(2.0 * pi * normalized_cutoff * centered)
                    / (pi * centered);
            const double blackman = 0.42
                - 0.5 * std::cos(2.0 * pi * static_cast<double>(index)
                                 / static_cast<double>(taps - 1U))
                + 0.08 * std::cos(4.0 * pi * static_cast<double>(index)
                                  / static_cast<double>(taps - 1U));
            coefficients[index] = sinc * blackman;
            coefficient_sum += coefficients[index];
        }
        std::vector<double> filtered(frames, 0.0);
        for (std::size_t frame = 0U; frame < frames; ++frame) {
            double sum = 0.0;
            const std::size_t count = std::min(taps, frame + 1U);
            for (std::size_t tap = 0U; tap < count; ++tap) {
                sum += samples[frame - tap] * coefficients[tap];
            }
            filtered[frame] = sum / coefficient_sum;
        }
        samples = std::move(filtered);
    } else {
        // A gradual analogue-style roll-off is a counterexample: reduced
        // bandwidth alone must remain inconclusive.
        const double alpha = 1.0 - std::exp(
            -2.0 * std::acos(-1.0) * cutoff_hz / sample_rate);
        for (int pass = 0; pass < 12; ++pass) {
            double state = 0.0;
            for (double& sample : samples) {
                state += alpha * (sample - state);
                sample = state;
            }
        }
    }
    write_pcm_wav(path, sample_rate, 24, frames,
                  [&samples, noise_gain, sample_rate, modulation_hz](std::ofstream& output, const std::size_t frame,
                             const std::uint16_t) {
                      double signal = samples[frame] * noise_gain
                          + (noise_gain < 1.0 ? 0.1 * std::sin(2.0 * std::acos(-1.0)
                              * 997.0 * frame / sample_rate) : 0.0);
                      if (modulation_hz > 0.0) signal *= 1.0 + 0.5 * std::cos(
                          2.0 * std::acos(-1.0) * modulation_hz * frame / sample_rate);
                      const auto value = static_cast<std::int32_t>(std::lround(
                          std::clamp(signal, -1.0, 1.0) * 7'500'000.0));
                      write_le24(output, value);
                  });
}

bool contains_window(const std::vector<std::size_t>& values,
                     const std::size_t expected)
{
    return std::find(values.begin(), values.end(), expected) != values.end();
}

void write_actual_resampled_noise(const std::filesystem::path& path, std::size_t trim, double gain)
{
    // Real rational interpolation of an independently generated 44.1 kHz
    // sequence; unlike filtering at the output rate this has a sampling grid.
    constexpr std::size_t phases = 320U, taps = 32U, output_frames = 384'000U;
    auto input = deterministic_noise(176'500U);
    for (double& sample : input) sample = std::round(sample * 32767.0) / 32767.0;
    std::array<std::array<double, taps>, phases> coefficients{};
    const double pi = std::acos(-1.0);
    for (std::size_t phase = 0; phase < phases; ++phase) {
        double sum = 0.0;
        for (std::size_t tap = 0; tap < taps; ++tap) {
            const double distance = static_cast<double>(tap) - 15.0 - static_cast<double>(phase) / phases;
            const double sinc = std::abs(distance) < 1e-12 ? 1.0 : std::sin(pi * distance) / (pi * distance);
            const double window = 0.42 + 0.5 * std::cos(pi * distance / 16.0)
                + 0.08 * std::cos(2.0 * pi * distance / 16.0);
            coefficients[phase][tap] = sinc * window;
            sum += coefficients[phase][tap];
        }
        for (double& coefficient : coefficients[phase]) coefficient /= sum;
    }
    write_pcm_wav(path, 96'000, 24, output_frames - trim,
        [&](std::ofstream& output, std::size_t frame, std::uint16_t) {
            const auto numerator = (frame + trim) * 147U;
            const auto center = static_cast<std::int64_t>(numerator / phases);
            const auto phase = numerator % phases;
            double value = 0.0;
            for (std::size_t tap = 0; tap < taps; ++tap) {
                const auto index = center + static_cast<std::int64_t>(tap) - 15;
                if (index >= 0 && static_cast<std::size_t>(index) < input.size())
                    value += input[static_cast<std::size_t>(index)] * coefficients[phase][tap];
            }
            write_le24(output, static_cast<std::int32_t>(std::lround(value * gain * 7'500'000.0)));
        });
}

struct MemoryInput final {
    std::vector<std::uint8_t> bytes;
    std::int64_t cursor = 0;
    std::uint64_t packetBytes = 0;
};

int read_memory(void* context, std::uint8_t* buffer, const int size) noexcept
{
    auto& input = *static_cast<MemoryInput*>(context);
    if (size <= 0 || input.cursor < 0) return -1;
    const auto remaining = static_cast<std::int64_t>(input.bytes.size())
                           - input.cursor;
    // FFmpeg AVERROR_EOF. Decoder must preserve negative callback status.
    if (remaining <= 0) return -541'478'725;
    const auto count = static_cast<std::size_t>(std::min<std::int64_t>(
        remaining, size));
    std::copy_n(input.bytes.data() + input.cursor, count, buffer);
    input.cursor += static_cast<std::int64_t>(count);
    return static_cast<int>(count);
}

void observe_packet(void* context,
                    const std::uint8_t*,
                    const int size) noexcept
{
    auto& input = *static_cast<MemoryInput*>(context);
    if (size > 0) input.packetBytes += static_cast<std::uint64_t>(size);
}

std::int64_t seek_memory(void* context,
                         const std::int64_t offset,
                         const int whence) noexcept
{
    auto& input = *static_cast<MemoryInput*>(context);
    constexpr int seek_set = 0;
    constexpr int seek_cur = 1;
    constexpr int seek_end = 2;
    constexpr int avseek_size = 0x10000;
    if ((whence & avseek_size) != 0) {
        return static_cast<std::int64_t>(input.bytes.size());
    }
    std::int64_t base = 0;
    switch (whence & 0x3) {
    case seek_set: base = 0; break;
    case seek_cur: base = input.cursor; break;
    case seek_end: base = static_cast<std::int64_t>(input.bytes.size()); break;
    default: return -1;
    }
    if (offset < -base) return -1;
    const std::int64_t target = base + offset;
    if (target < 0 || target > static_cast<std::int64_t>(input.bytes.size())) {
        return -1;
    }
    input.cursor = target;
    return target;
}

} // namespace

int main(const int argc, char** argv)
{
    namespace lossless = agplayer::lossless;
    std::random_device random;
    const std::uint64_t run_id =
        (static_cast<std::uint64_t>(random()) << 32U) | random();
    const auto directory = std::filesystem::temp_directory_path()
        / ("agplayer-lossless-core-test-" + std::to_string(run_id));
    std::filesystem::create_directories(directory);

    std::atomic_bool cancelled{false};
    lossless::AnalysisOptions options;
    assert(options.isoTrackIndex == -1);
    options.spectrumBins = 256U;

    lossless::AnalysisResult channel_reference;
    for (int variant = 0; variant < 4; ++variant) {
        const auto channel_file = directory / ("channels-" + std::to_string(variant) + ".wav");
        const std::uint16_t channel_count = variant == 0 ? 1U : variant == 3 ? 6U : 2U;
        write_pcm_wav(channel_file, 48'000, 16, 48'000U,
            [variant](std::ofstream& out, std::size_t frame, std::uint16_t channel) {
                double amplitude = 12000.0 * std::sin(2.0 * std::acos(-1.0) * 7000.0 * frame / 48000.0);
                if (variant == 2 && channel == 1U) amplitude = -amplitude;
                if (variant == 3 && channel != 5U) amplitude = 0.0;
                write_le16(out, static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lround(amplitude))));
            }, 1U, channel_count);
        const auto result = lossless::analyzeFile(channel_file.u8string(), options, cancelled, {});
        if (variant == 0) { channel_reference = result; continue; }
        assert(result.coverage.activeWindows == channel_reference.coverage.activeWindows);
        assert(result.measurements.cutoffHz == channel_reference.measurements.cutoffHz);
        assert(result.measurements.dominantFrequencyHz == channel_reference.measurements.dominantFrequencyHz);
        assert(result.spectrum.db == channel_reference.spectrum.db);
    }

    // Cached Hann windows must preserve strong and silent spectra. The two
    // tones are separated by complete silent windows, including when the active
    // channel moves. Reordering the separated tones preserves their spectrum.
    lossless::AnalysisResult window_reference;
    constexpr std::size_t segment_frames = 16'384U;
    for (int variant = 0; variant < 3; ++variant) {
        const auto file = directory / ("window-history-" + std::to_string(variant) + ".wav");
        write_pcm_wav(file, 32'000, 16, segment_frames * 5U,
            [variant](std::ofstream& out, std::size_t frame, std::uint16_t channel) {
                const auto segment = frame / segment_frames;
                double value = 0.0;
                if (segment == 1U || segment == 3U) {
                    const bool high = (segment == 1U) != (variant == 2);
                    const bool active_channel = variant != 1
                        || channel == (segment == 1U ? 0U : 5U);
                    if (active_channel) value = (high ? 12000.0 : 750.0)
                        * std::sin(2.0 * std::acos(-1.0) * (high ? 4000.0 : 1000.0)
                            * static_cast<double>(frame % segment_frames) / 32000.0);
                }
                write_le16(out, static_cast<std::uint16_t>(
                    static_cast<std::int16_t>(std::lround(value))));
            }, 1U, variant == 1 ? 6U : 1U);
        const auto result = lossless::analyzeFile(file.u8string(), options, cancelled, {});
        assert(result.error.empty());
        assert(result.coverage.decodedFrames == segment_frames * 5U);
        assert(result.coverage.analyzedWindows == 9U);
        assert(result.coverage.activeWindows == 6U);
        if (variant == 0) { window_reference = result; continue; }
        assert(result.spectrum.db.size() == window_reference.spectrum.db.size());
        for (std::size_t i = 0; i < result.spectrum.db.size(); ++i)
            assert(std::abs(result.spectrum.db[i] - window_reference.spectrum.db[i]) < 1.0e-9);
    }

    const auto silence = directory / "silence-44100.wav";
    write_silence(silence, 44'100);
    const auto silence_result = lossless::analyzeFile(
        silence.u8string(), options, cancelled, {});
    assert(silence_result.verdict == lossless::Verdict::Inconclusive);
    assert(silence_result.confidence <= 25);
    assert(silence_result.coverage.activeWindowRatio == 0.0);
    assert(silence_result.spectrum.nyquistHz == 22'050.0);
    assert(!silence_result.measurements.transientPreEchoMeasured);
    const auto broadband = directory / "broadband-does-not-prove-history.wav";
    const auto broadband_samples = deterministic_noise(96'000U);
    write_pcm_wav(broadband, 96'000, 24, broadband_samples.size(),
        [&](std::ofstream& out, std::size_t frame, std::uint16_t) {
            write_le24(out, static_cast<std::int32_t>(std::lround(broadband_samples[frame] * 7'500'000.0)));
        });
    const auto broadband_result = lossless::analyzeFile(broadband.u8string(), options, cancelled, {});
    assert(broadband_result.measurements.cutoffHz > 45'000.0);
    // The same wideband spectrum can be produced by adding noise to a lossy
    // decode. Container and bandwidth alone do not certify recording history.
    assert(broadband_result.verdict == lossless::Verdict::Inconclusive);

    // Controlled precursor structure is measurable, but does not establish
    // codec history: an acoustic lead-in or editing can produce it too.
    for (const bool precursor : {false, true}) {
        const auto attack = directory / (precursor ? "precursor.wav" : "clean-attack.wav");
        write_pcm_wav(attack, 48'000, 16, 48'000U,
            [precursor](std::ofstream& out, std::size_t frame, std::uint16_t channel) {
                const auto phase = frame % 12'000U;
                double amplitude = phase >= 4'800U && phase < 4'848U ? 0.4 : 0.0;
                if (precursor && phase >= 3'840U && phase < 4'656U) amplitude = 0.03;
                const auto sample = static_cast<std::int16_t>(amplitude * 32767.0
                    * ((frame + channel) % 2U ? -1.0 : 1.0));
                write_le16(out, static_cast<std::uint16_t>(sample));
            });
        const auto measured = lossless::analyzeFile(attack.u8string(), options, cancelled, {});
        assert(measured.measurements.transientPreEchoMeasured);
        assert(measured.measurements.transientCount == 4U);
        assert(precursor ? measured.measurements.transientPreEchoScore > 0.001
                         : measured.measurements.transientPreEchoScore == 0.0);
        assert(measured.verdict != lossless::Verdict::SuspectedLossyTranscode);
    }
    // A steady background before an acoustic attack is not excess precursor
    // energy; subtraction must not turn ordinary room noise into pre-echo.
    const auto background_attack = directory / "background-attack.wav";
    write_pcm_wav(background_attack, 44'100, 16, 44'100U,
        [](std::ofstream& out, std::size_t frame, std::uint16_t) {
            const auto phase = frame % 11'025U;
            const double amplitude = phase >= 4'410U && phase < 4'455U ? 0.4 : 0.03;
            write_le16(out, static_cast<std::uint16_t>(static_cast<std::int16_t>(
                amplitude * 32767.0 * (frame % 2U ? -1.0 : 1.0))));
        });
    const auto background_result = lossless::analyzeFile(
        background_attack.u8string(), options, cancelled, {});
    assert(background_result.measurements.transientPreEchoMeasured);
    assert(background_result.measurements.transientPreEchoScore < 1.0e-12);
    assert(background_result.verdict != lossless::Verdict::SuspectedLossyTranscode);

    const auto precise = directory / "precise-32bit-96000.wav";
    write_precise_pcm32(precise, 96'000, 12'000.0);
    // Compare file creation time, not the clock after a potentially slow scan.
    const auto unix_now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::vector<float> progress_values;
    const auto precise_result = lossless::analyzeFile(
        precise.u8string(), options, cancelled,
        [&progress_values](const float value) { progress_values.push_back(value); });
    assert(precise_result.verdict != lossless::Verdict::AnalysisFailed);
    assert(precise_result.source.bitsPerSample == 32);
    assert(std::llabs(precise_result.file.modifiedUnixMs - unix_now_ms) < 2'000);
    assert(!precise_result.source.channelLayout.empty());
    assert(precise_result.measurements.lowBitUsageRatio > 0.90);
    assert(precise_result.measurements.effectiveBits > 24.0);
    assert(std::abs(precise_result.measurements.dominantFrequencyHz - 12'000.0)
           < 80.0);
    assert(precise_result.spectrum.nyquistHz == 48'000.0);
    assert(precise_result.spectrum.db.size() == 256U);
    assert(contains_window(precise_result.measurements.stftWindowSizes, 4'096U));
    assert(contains_window(precise_result.measurements.stftWindowSizes, 8'192U));
    assert(contains_window(precise_result.measurements.stftWindowSizes, 16'384U));
    assert(!precise_result.measurements.transientPreEchoMeasured);
    for (const std::size_t bins : {16U, 64U, 1024U, 2048U}) {
        auto display_options = options;
        display_options.spectrumBins = bins;
        const auto display = lossless::analyzeFile(precise.u8string(), display_options, cancelled, {});
        assert(display.spectrum.db.size() == bins);
        assert(display.verdict == precise_result.verdict);
        assert(display.confidence == precise_result.confidence);
        assert(display.measurements.cutoffHz == precise_result.measurements.cutoffHz);
        assert(display.measurements.dominantFrequencyHz == precise_result.measurements.dominantFrequencyHz);
        assert(display.measurements.spectralEntropy == precise_result.measurements.spectralEntropy);
    }

    // Catches reducing each displayed frequency band to one representative
    // FFT bin. Bin 1016 is deliberately more than one Hann lobe away from
    // every representative used by the old 128-bin point sampler.
    constexpr double off_grid_frequency = 1'016.0 * 48'000.0 / 8'192.0;
    const auto off_grid_tone = directory / "spectrogram-off-grid-tone.wav";
    write_spectrogram_tone(off_grid_tone, off_grid_frequency, 96'000U);
    const auto off_grid_baseline = lossless::analyzeFile(
        off_grid_tone.u8string(), options, cancelled, {});
    auto coarse_spectrogram_options = options;
    coarse_spectrogram_options.includeSpectrogram = true;
    coarse_spectrogram_options.maxSpectrogramTimeBins = 4U;
    coarse_spectrogram_options.maxSpectrogramFrequencyBins = 128U;
    const auto coarse_spectrogram = lossless::analyzeFile(
        off_grid_tone.u8string(), coarse_spectrogram_options, cancelled, {});
    assert(coarse_spectrogram.spectrogram.timeBins == 4U);
    assert(coarse_spectrogram.spectrogram.frequencyBins == 128U);
    bool off_grid_peak_preserved = true;
    for (std::size_t time_bin = 0U;
         time_bin < coarse_spectrogram.spectrogram.timeBins; ++time_bin) {
        off_grid_peak_preserved = off_grid_peak_preserved
            && spectrogram_bin(coarse_spectrogram.spectrogram,
                               time_bin, 31U) > -20.0
            && spectrogram_bin(coarse_spectrogram.spectrogram,
                               time_bin, 30U) < -70.0
            && spectrogram_bin(coarse_spectrogram.spectrogram,
                               time_bin, 32U) < -70.0;
    }

    // Catches decimating time to one STFT frame per stride and then deleting
    // silent rows. Both short bursts sit between the old sampled frames; the
    // two silent middle buckets must remain present on the real time axis.
    const auto intermittent = directory / "spectrogram-intermittent.wav";
    constexpr std::size_t intermittent_frames = 48'000U * 8U;
    write_pcm_wav(intermittent, 48'000, 16, intermittent_frames,
        [](std::ofstream& output, const std::size_t frame,
           const std::uint16_t) {
            const bool active = (frame >= 31'200U && frame < 33'600U)
                || (frame >= 312'000U && frame < 314'400U);
            const double phase = 2.0 * std::acos(-1.0) * 4'000.0
                * static_cast<double>(frame) / 48'000.0;
            const auto sample = static_cast<std::int16_t>(std::lround(
                active ? std::sin(phase) * 20'000.0 : 0.0));
            write_le16(output, static_cast<std::uint16_t>(sample));
        });
    auto intermittent_options = options;
    intermittent_options.includeSpectrogram = true;
    intermittent_options.maxSpectrogramTimeBins = 4U;
    intermittent_options.maxSpectrogramFrequencyBins = 64U;
    const auto intermittent_result = lossless::analyzeFile(
        intermittent.u8string(), intermittent_options, cancelled, {});
    assert(intermittent_result.spectrogram.timeBins == 4U);
    assert(intermittent_result.spectrogram.frequencyBins == 64U);
    assert(spectrogram_row_peak(intermittent_result.spectrogram, 0U) > -20.0);
    assert(spectrogram_row_peak(intermittent_result.spectrogram, 1U) < -100.0);
    assert(spectrogram_row_peak(intermittent_result.spectrogram, 2U) < -100.0);
    assert(spectrogram_row_peak(intermittent_result.spectrogram, 3U) > -20.0);

    // Spectrogram detail is display evidence only. Requesting either preview
    // or full bounded detail must not alter any classification output.
    auto fine_spectrogram_options = options;
    fine_spectrogram_options.includeSpectrogram = true;
    const auto fine_spectrogram = lossless::analyzeFile(
        off_grid_tone.u8string(), fine_spectrogram_options, cancelled, {});
    assert(fine_spectrogram.spectrogram.frequencyBins == 256U);
    assert(fine_spectrogram.spectrogram.timeBins > 0U);
    assert(fine_spectrogram.spectrogram.timeBins <= 256U);
    assert(off_grid_peak_preserved);
    assert_same_classification(off_grid_baseline, coarse_spectrogram);
    assert_same_classification(off_grid_baseline, fine_spectrogram);
    assert(!progress_values.empty());
    assert(progress_values.front() >= 0.0F);
    assert(progress_values.back() == 1.0F);
    assert(std::is_sorted(progress_values.begin(), progress_values.end()));

    // SACD tracks are exposed as a virtual DFF byte stream. The Decoder must
    // feed such a seekable stream through the same demux/decode path.
    std::ifstream precise_stream(precise, std::ios::binary);
    MemoryInput memory_input;
    memory_input.bytes.assign(std::istreambuf_iterator<char>(precise_stream),
                              std::istreambuf_iterator<char>());
    precise_stream.close();
    lossless::AnalysisOptions virtual_options = options;
    virtual_options.decoderOpenOptions.custom_read = &read_memory;
    virtual_options.decoderOpenOptions.custom_seek = &seek_memory;
    virtual_options.decoderOpenOptions.custom_io_context = &memory_input;
    virtual_options.decoderOpenOptions.input_format_hint = "wav";
    virtual_options.decoderOpenOptions.packet_callback = &observe_packet;
    virtual_options.decoderOpenOptions.packet_context = &memory_input;
    const auto virtual_result = lossless::analyzeFile(
        "virtual-sacd-track.dff", virtual_options, cancelled, {});
    assert(virtual_result.verdict != lossless::Verdict::AnalysisFailed);
    assert(virtual_result.source.sampleRate == 96'000);
    assert(memory_input.cursor > 0);
    assert(memory_input.packetBytes > 0U);

    // A stable low-pass spectrum alone is compatible with naturally
    // bandwidth-limited material and must not become a lossy verdict.
    const auto low_band = directory / "natural-low-band-96000.wav";
    write_low_band_pcm16(low_band, 96'000);
    const auto low_band_result = lossless::analyzeFile(
        low_band.u8string(), options, cancelled, {});
    assert(low_band_result.verdict == lossless::Verdict::Inconclusive);
    assert(low_band_result.confidence <= 45);
    assert(low_band_result.measurements.codecHoleScore <= 0.10);

    const auto sharp_edge = directory / "unseen-sharp-edge-96000.wav";
    write_filtered_noise_pcm24(sharp_edge, 96'000, 22'050.0, true);
    const auto sharp_edge_result = lossless::analyzeFile(
        sharp_edge.u8string(), options, cancelled, {});
    assert(sharp_edge_result.measurements.spectralEdgeDepthDb >= 24.0);
    assert(sharp_edge_result.measurements.spectralEdgeStability >= 0.80);
    // This generator filters directly at 96 kHz; labelling it as known
    // upsampling would bake a false positive into the regression contract.
    assert(sharp_edge_result.verdict == lossless::Verdict::Inconclusive);
    assert(!sharp_edge_result.candidates.empty());

    // A quiet high-frequency tail under a dominant tone lies below the plot's
    // -120 dB floor, but remains measurable in original FFT powers. Generated
    // directly at 96 kHz: this edge is NOT proof of an upsampling history.
    const auto quiet_edge = directory / "native-quiet-edge-96000.wav";
    write_filtered_noise_pcm24(quiet_edge, 96'000, 22'050.0, true, 0.0001);
    const auto quiet_edge_result = lossless::analyzeFile(
        quiet_edge.u8string(), options, cancelled, {});
    assert(quiet_edge_result.measurements.lowLevelSpectralEdgeMeasured);
    assert(quiet_edge_result.measurements.lowLevelSpectralEdgeDepthDb > 24.0);
    assert(quiet_edge_result.measurements.lowLevelSpectralEdgeHz > 20'000.0);
    assert(quiet_edge_result.measurements.lowLevelSpectralEdgeHz < 24'000.0);
    assert(quiet_edge_result.verdict == lossless::Verdict::Inconclusive);
    assert(quiet_edge_result.measurements.spectralFlatness < 0.02);

    for (const auto [trim, gain] : {std::pair<std::size_t, double>{0U, 1.0}, {137U, 0.73}}) {
        const auto resampled = directory / ("actual-resampled-" + std::to_string(trim) + ".wav");
        write_actual_resampled_noise(resampled, trim, gain);
        const auto resampled_result = lossless::analyzeFile(resampled.u8string(), options, cancelled, {});
        assert(resampled_result.measurements.resamplingPhaseSourceRate == 44'100.0);
        assert(resampled_result.measurements.resamplingPhaseCoherence > 0.995);
        // A real resampling grid remains measurable, but the same structure
        // can be manufactured by native-rate AM; it alone is not causal proof.
        assert(resampled_result.verdict == lossless::Verdict::Inconclusive);
        assert(resampled_result.chain.empty());
        const auto grid = std::find_if(resampled_result.evidence.begin(),
            resampled_result.evidence.end(), [](const auto& evidence) {
                return evidence.code == "resampling_polyphase_grid";
            });
        assert(grid != resampled_result.evidence.end());
        assert(grid->direction == lossless::EvidenceDirection::Neutral);
    }
    const auto periodic = directory / "native-high-frequency-periodic.wav";
    write_pcm_wav(periodic, 96'000, 24, 384'000U,
        [](std::ofstream& out, std::size_t frame, std::uint16_t) {
            const double value = 0.1 * std::sin(2.0 * std::acos(-1.0) * 22'050.0 * frame / 96'000.0);
            write_le24(out, static_cast<std::int32_t>(std::lround(value * 7'500'000.0)));
        });
    const auto periodic_result = lossless::analyzeFile(periodic.u8string(), options, cancelled, {});
    assert(periodic_result.verdict != lossless::Verdict::SuspectedUpsample);
    for (const double modulation : {0.0, 997.0, 24'000.0}) {
        const auto native_am = directory / ("native-filter-am-" + std::to_string(static_cast<int>(modulation)) + ".wav");
        write_filtered_noise_pcm24(native_am, 96'000, 15'000.0, true, 1.0, modulation, 4U);
        const auto native_am_result = lossless::analyzeFile(native_am.u8string(), options, cancelled, {});
        assert(native_am_result.verdict != lossless::Verdict::SuspectedUpsample);
        assert(native_am_result.verdict != lossless::Verdict::SuspectedLossyUpsample);
        if (modulation == 24'000.0) {
            // Periodic processing can imitate grid energy; the accompanying
            // out-of-band suppression condition must still be enforced.
            assert(native_am_result.measurements.resamplingBandSuppressionDb < 35.0);
        }
    }

    const auto gradual_edge = directory / "unseen-gradual-lowpass-96000.wav";
    write_filtered_noise_pcm24(gradual_edge, 96'000, 15'000.0, false);
    const auto gradual_edge_result = lossless::analyzeFile(
        gradual_edge.u8string(), options, cancelled, {});
    assert(gradual_edge_result.verdict == lossless::Verdict::Inconclusive);
    assert(gradual_edge_result.measurements.spectralEdgeDepthDb
           < sharp_edge_result.measurements.spectralEdgeDepthDb);

    // A mastering engineer can apply a brick-wall FIR low-pass without any
    // lossy codec. Edge geometry and persistence are still one bandwidth
    // evidence family and must not assert a hidden lossy source by themselves.
    for (const auto [sample_rate, cutoff_hz] :
         {std::pair{44'100, 17'000.0}, std::pair{48'000, 20'000.0}}) {
        const auto digital_lowpass = directory
            / ("digital-lowpass-" + std::to_string(sample_rate) + ".wav");
        write_filtered_noise_pcm24(digital_lowpass, sample_rate, cutoff_hz,
                                   true);
        const auto digital_lowpass_result = lossless::analyzeFile(
            digital_lowpass.u8string(), options, cancelled, {});
        assert(digital_lowpass_result.measurements.spectralEdgeDepthDb
               >= 45.0);
        assert(digital_lowpass_result.verdict == lossless::Verdict::Inconclusive);
    }

    // A narrow test tone has too little content diversity for a reliable
    // bit-depth provenance conclusion, even when its low bits are zero.
    const auto expanded_tone = directory / "expanded-narrowband-24.wav";
    write_expanded_narrowband_pcm24(expanded_tone);
    const auto expanded_tone_result = lossless::analyzeFile(
        expanded_tone.u8string(), options, cancelled, {});
    assert(expanded_tone_result.measurements.effectiveBits <= 16.0);
    assert(expanded_tone_result.verdict == lossless::Verdict::Inconclusive);
    assert(expanded_tone_result.measurements.codecHoleScore == 0.0);

    const auto rate_48k = directory / "tone-48000.wav";
    write_precise_pcm32(rate_48k, 48'000, 9'000.0);
    const auto rate_48k_result = lossless::analyzeFile(
        rate_48k.u8string(), options, cancelled, {});
    assert(rate_48k_result.spectrum.nyquistHz == 24'000.0);

    const auto nonfinite = directory / "nonfinite-float.wav";
    write_nonfinite_float(nonfinite);
    const auto nonfinite_result = lossless::analyzeFile(
        nonfinite.u8string(), options, cancelled, {});
    assert(nonfinite_result.verdict != lossless::Verdict::AnalysisFailed);
    assert(!nonfinite_result.warnings.empty());

    cancelled.store(true);
    const auto cancelled_result = lossless::analyzeFile(
        precise.u8string(), options, cancelled, {});
    assert(cancelled_result.verdict == lossless::Verdict::Cancelled);
    assert(cancelled_result.cancelled);
    assert(cancelled_result.error.empty());

    if (argc > 1) {
        cancelled.store(false);
        const auto dst_result = lossless::analyzeFile(argv[1], options,
                                                       cancelled, {});
        assert(dst_result.verdict != lossless::Verdict::AnalysisFailed);
        assert(dst_result.source.kind == lossless::SourceKind::Dst);
        assert(dst_result.source.rawDsdSampleRate > 0);
        // Compressed DST packets are not raw 1-bit evidence, so decoded
        // noise-shaping alone cannot earn CredibleNativeDsd.
        assert(dst_result.verdict != lossless::Verdict::CredibleNativeDsd);
    }
    if (argc > 2) {
        cancelled.store(false);
        lossless::AnalysisOptions iso_options = options;
        iso_options.isoTrackIndex = 0;
        const auto iso_result = lossless::analyzeFile(argv[2], iso_options,
                                                       cancelled, {});
        assert(iso_result.verdict != lossless::Verdict::AnalysisFailed);
        assert(iso_result.source.container == "sacd_iso");
        assert(iso_result.source.kind == lossless::SourceKind::Dst);
    }
    if (argc > 3) {
        cancelled.store(false);
        const auto pcm_to_dsd_result = lossless::analyzeFile(
            argv[3], options, cancelled, {});
        assert(pcm_to_dsd_result.verdict != lossless::Verdict::AnalysisFailed);
        assert(pcm_to_dsd_result.source.kind == lossless::SourceKind::Dsd);
        // Noise shaping plus valid 1-bit packet statistics proves a usable DSD
        // representation, not that microphones captured a native DSD source.
        assert(pcm_to_dsd_result.verdict
               != lossless::Verdict::CredibleNativeDsd);
    }

    std::filesystem::remove_all(directory);
    return 0;
}
