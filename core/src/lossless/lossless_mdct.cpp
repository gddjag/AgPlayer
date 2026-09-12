#include "lossless_mdct.hpp"

extern "C" {
#include <libavutil/tx.h>
}
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace agplayer::lossless {
namespace {
// Framing differential and circular aggregation follow the method described by
// Kim & Rafii, EUSIPCO 2018, section III. This is an independent implementation.
// https://eurasip.org/Proceedings/Eusipco/Eusipco2018/papers/1570436395.pdf
// Their codec-classification score is not a lossless-negative accuracy claim.
constexpr double pi = 3.14159265358979323846;
constexpr std::size_t framesPerOffset = 4;
constexpr std::size_t excerptCount = 4;
constexpr std::size_t alignmentTolerance = 2;
constexpr double minimumRms = 1e-6;
constexpr double coefficientFloor = 1e-10;
double besselI0(double x) noexcept
{
    double term = 1.0, sum = 1.0;
    for (int k = 1; k < 64; ++k) {
        term *= (x * x) / (4.0 * k * k);
        sum += term;
        if (term < sum * 1e-16) break;
    }
    return sum;
}
}

struct MdctFramingProbe::Impl final {
    int channels;
    std::size_t size, hop, excerptSize, profileCount;
    std::uint64_t cursor = 0;
    std::array<std::uint64_t, excerptCount> starts{};
    std::size_t current = 0;
    std::vector<double> captured;
    std::array<std::vector<double>, 3> windows;
    std::vector<AVComplexDouble> pre, post, input, output;
    AVTXContext* context = nullptr;
    av_tx_fn transform = nullptr;
    struct Accumulator {
        double real = 0.0, imaginary = 0.0, peaks = 0.0, z = 0.0;
        std::size_t count = 0;
        std::array<std::size_t, excerptCount> phases{};
    };
    std::array<Accumulator, 3> accumulators{};

    Impl(int rate, int channelCount, std::uint64_t total, std::size_t transformSize)
        : channels(channelCount), size(transformSize), hop(size / 2),
          excerptSize(size + framesPerOffset * hop), profileCount(size == 1152 ? 1 : 3),
          pre(size), post(hop), input(size), output(size)
    {
        for (auto& window : windows) window.resize(size);
        for (std::size_t block = 0; block < excerptCount; ++block) {
            if (total >= excerptCount * excerptSize) {
                const auto first = std::min({static_cast<std::uint64_t>(rate), total / 10,
                                            total - excerptCount * excerptSize});
                starts[block] = first + (total - excerptSize - first) * block / (excerptCount - 1);
            } else {
                starts[block] = block * std::max<std::uint64_t>(rate, excerptSize);
            }
        }
        captured.reserve(excerptSize * static_cast<std::size_t>(channels));
        std::vector<double> kaiser(hop + 1);
        double totalKaiser = 0.0;
        for (std::size_t n = 0; n <= hop; ++n) {
            const double position = 2.0 * static_cast<double>(n) / hop - 1.0;
            kaiser[n] = besselI0(4.0 * pi * std::sqrt(std::max(0.0, 1.0 - position * position)));
            totalKaiser += kaiser[n];
        }
        double cumulative = 0.0;
        for (std::size_t n = 0; n < size; ++n) {
            windows[0][n] = std::sin(pi / size * (static_cast<double>(n) + 0.5));
            windows[2][n] = std::sin(pi / 2.0 * windows[0][n] * windows[0][n]);
            pre[n] = {std::cos(-pi * n / size), std::sin(-pi * n / size)};
        }
        for (std::size_t n = 0; n < hop; ++n) {
            cumulative += kaiser[n];
            windows[1][n] = windows[1][size - n - 1] = std::sqrt(cumulative / totalKaiser);
            const double angle = -2.0 * pi / size * (0.5 + size / 4.0) * (n + 0.5);
            post[n] = {std::cos(angle), std::sin(angle)};
        }
        if (av_tx_init(&context, &transform, AV_TX_DOUBLE_FFT, 0,
                       static_cast<int>(size), nullptr, AV_TX_UNALIGNED) < 0) {
            av_tx_uninit(&context);
            throw std::runtime_error("MDCT framing FFT initialization failed");
        }
    }
    ~Impl() { av_tx_uninit(&context); }

    void measure(const std::atomic_bool& cancel)
    {
        // Choose the most active channel in this excerpt. Signed downmix could
        // erase antiphase content or create spurious zeros in the MDCT.
        std::size_t selected = 0;
        double maximumPower = 0.0;
        for (int channel = 0; channel < channels; ++channel) {
            double power = 0.0;
            for (std::size_t n = 0; n < excerptSize; ++n) {
                const auto value = captured[n * channels + channel];
                if (!std::isfinite(value)) return;
                power += value * value;
            }
            if (!std::isfinite(power)) return;
            if (power > maximumPower) { maximumPower = power; selected = channel; }
        }
        if (maximumPower / excerptSize < minimumRms * minimumRms) return;
        std::vector<double> mono(excerptSize);
        for (std::size_t n = 0; n < excerptSize; ++n) mono[n] = captured[n * channels + selected];
        for (std::size_t profile = 0; profile < profileCount; ++profile) {
            std::vector<double> energies(hop + 1);
            for (std::size_t shift = 0; shift <= hop; ++shift) {
                if (cancel.load(std::memory_order_relaxed)) return;
                double energy = 0.0;
                for (std::size_t frame = 0; frame < framesPerOffset; ++frame) {
                    const auto start = shift + frame * hop;
                    for (std::size_t n = 0; n < size; ++n) {
                        const auto value = mono[start + n] * windows[profile][n];
                        input[n] = {value * pre[n].re, value * pre[n].im};
                    }
                    transform(context, output.data(), input.data(), sizeof(AVComplexDouble));
                    for (std::size_t k = 0; k < hop; ++k) {
                        const double coefficient = output[k].re * post[k].re - output[k].im * post[k].im;
                        energy += 20.0 * std::log10(std::max(coefficientFloor, std::abs(coefficient)));
                    }
                }
                energies[shift] = energy / (framesPerOffset * hop);
            }
            double peak = 0.0, sum = 0.0, squares = 0.0;
            std::size_t peakOffset = 0;
            for (std::size_t shift = 0; shift < hop; ++shift) {
                const double difference = energies[shift + 1] - energies[shift];
                sum += difference; squares += difference * difference;
                if (difference > peak) { peak = difference; peakOffset = shift; }
            }
            const double mean = sum / hop;
            const double deviation = std::sqrt(std::max(0.0, squares / hop - mean * mean));
            const double phase = 2.0 * pi * static_cast<double>((starts[current] + peakOffset) % hop) / hop;
            auto& accumulator = accumulators[profile];
            accumulator.real += peak * std::cos(phase);
            accumulator.imaginary += peak * std::sin(phase);
            accumulator.peaks += peak;
            accumulator.z += peak / std::max(deviation, 1e-12);
            accumulator.phases[accumulator.count] = static_cast<std::size_t>((starts[current] + peakOffset) % hop);
            ++accumulator.count;
        }
    }
};

MdctFramingProbe::MdctFramingProbe(int rate, int channels, std::uint64_t total)
{
    if (rate >= 32000 && rate <= 48000 && channels > 0 && channels <= 64) {
        impl_ = std::make_unique<Impl>(rate, channels, total, 2048);
        mp3_ = std::make_unique<Impl>(rate, channels, total, 1152);
    }
}
MdctFramingProbe::~MdctFramingProbe() = default;

void MdctFramingProbe::consume(const double* samples, std::size_t frames,
                              const std::atomic_bool& cancel)
{
    if (!impl_ || !samples || cancel.load(std::memory_order_relaxed)) return;
    for (auto* state : {impl_.get(), mp3_.get()}) {
        if (cancel.load(std::memory_order_relaxed)) break;
        auto& p = *state;
        const auto end = p.cursor + frames;
        while (p.current < excerptCount && p.starts[p.current] < end) {
            const auto begin = std::max(p.cursor, p.starts[p.current]);
            const auto stop = std::min(end, p.starts[p.current] + p.excerptSize);
            if (stop > begin) {
                p.captured.insert(p.captured.end(), samples + (begin - p.cursor) * p.channels,
                                  samples + (stop - p.cursor) * p.channels);
            }
            if (p.captured.size() != p.excerptSize * static_cast<std::size_t>(p.channels)) break;
            p.measure(cancel);
            p.captured.clear();
            ++p.current;
            if (cancel.load(std::memory_order_relaxed)) break;
        }
        p.cursor = end;
    }
}

std::array<MdctFrameEvidence, 4> MdctFramingProbe::result() const noexcept
{
    std::array<MdctFrameEvidence, 4> result{};
    constexpr std::array<const char*, 4> names{"sine", "kbd4", "slope", "sine1152"};
    for (std::size_t index = 0; index < result.size(); ++index) {
        auto& value = result[index]; value.window = names[index];
        if (!impl_) continue;
        const auto& state = index == 3 ? *mp3_ : *impl_;
        const auto& a = state.accumulators[index == 3 ? 0 : index];
        value.activeBlocks = a.count;
        if (a.count == 0) continue;
        const double magnitude = std::hypot(a.real, a.imaginary);
        value.coherentPeakDb = magnitude / a.count;
        value.phaseConcentration = a.peaks > 0.0 ? std::clamp(magnitude / a.peaks, 0.0, 1.0) : 0.0;
        value.meanPeakZ = a.z / a.count;
        for (std::size_t i = 0; i < a.count; ++i) {
            std::size_t aligned = 0;
            for (std::size_t j = 0; j < a.count; ++j) {
                const auto distance = a.phases[i] > a.phases[j]
                    ? a.phases[i] - a.phases[j] : a.phases[j] - a.phases[i];
                if (std::min(distance, state.hop - distance) <= alignmentTolerance) ++aligned;
            }
            value.alignedBlocks = std::max(value.alignedBlocks, aligned);
        }
    }
    return result;
}
} // namespace agplayer::lossless
