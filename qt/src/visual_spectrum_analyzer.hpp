#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>

namespace agplayer {

// Independent visual analysis of consecutive mono windows, called by the visual
// consumer, never by the audio callback. No allocations or shared FFT state.
// Equations: W3C Web Audio Recommendation, 17 June 2021, sections 1.8.3 / 1.8.6.
// https://www.w3.org/TR/2021/REC-webaudio-20210617/#fft-windowing-and-smoothing-over-time
class VisualSpectrumAnalyzer {
public:
    static constexpr std::size_t windowSize = 1024;
    static constexpr std::size_t binCount = windowSize / 2;
    using Window = std::array<float, windowSize>;
    using Spectrum = std::array<std::uint8_t, binCount>;

    VisualSpectrumAnalyzer() noexcept
    {
        for (std::size_t i = 0; i < windowSize; ++i) {
            const double angle = 2.0 * pi * static_cast<double>(i) / static_cast<double>(windowSize);
            window_[i] = 0.42 - 0.5 * std::cos(angle) + 0.08 * std::cos(2.0 * angle);
        }
        for (std::size_t i = 0; i < binCount; ++i) {
            const double angle = -2.0 * pi * static_cast<double>(i) / static_cast<double>(windowSize);
            roots_[i] = {std::cos(angle), std::sin(angle)};
        }
    }

    // Reset on seek, track/sample-rate change, discontinuity or visual restart.
    void reset() noexcept { smoothed_.fill(0.0); onsetSpectrum_.fill(0); attackMagnitudes_.fill(0); rms_ = 0; }

    const Spectrum& onsetSpectrum() const noexcept { return onsetSpectrum_; }
    const std::array<double, 8>& attackMagnitudes() const noexcept { return attackMagnitudes_; }
    double rootMeanSquare() const noexcept { return rms_; }

    Spectrum process(const Window& mono) noexcept
    {
        double energy = 0;
        for (std::size_t i = 0; i < windowSize; ++i) {
            const double value = std::isfinite(mono[i]) ? static_cast<double>(mono[i]) : 0.0;
            energy += value * value;
            scratch_[i] = {value * window_[i], 0.0};
        }
        rms_ = std::sqrt(energy / windowSize);

        // In-place radix-2 Cooley-Tukey FFT, bit-reversed input permutation.
        for (std::size_t i = 1, j = 0; i < windowSize; ++i) {
            std::size_t bit = windowSize / 2;
            for (; (j & bit) != 0; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap(scratch_[i], scratch_[j]);
        }
        for (std::size_t length = 2; length <= windowSize; length *= 2) {
            const std::size_t half = length / 2;
            const std::size_t stride = windowSize / length;
            for (std::size_t base = 0; base < windowSize; base += length) {
                for (std::size_t j = 0; j < half; ++j) {
                    const auto even = scratch_[base + j];
                    const auto odd = scratch_[base + j + half] * roots_[j * stride];
                    scratch_[base + j] = even + odd;
                    scratch_[base + j + half] = even - odd;
                }
            }
        }

        Spectrum result{};
        for (std::size_t i = 0; i < binCount; ++i) {
            const double magnitude = std::abs(scratch_[i]) / static_cast<double>(windowSize);
            if (i < attackMagnitudes_.size()) attackMagnitudes_[i] = magnitude;
            // Detection needs the transient before display smoothing and the
            // -30 dB display ceiling. Reuse this FFT, with full headroom.
            // KickResponse reads only bins 0..7. DC is excluded below; no
            // other detector consumes this unsmoothed spectrum. Avoid 505
            // redundant logarithms without changing the full display FFT.
            onsetSpectrum_[i] = i > 0 && i < 8 && magnitude > 0.0
                ? static_cast<std::uint8_t>(std::clamp(
                    255.0 * (20.0 * std::log10(magnitude) + 75.0) / 75.0, 0.0, 255.0))
                : 0;
            smoothed_[i] = 0.8 * smoothed_[i] + 0.2 * magnitude;
            if (smoothed_[i] > 0.0) {
                const double scaled = 255.0 * (20.0 * std::log10(smoothed_[i]) + 75.0) / 45.0;
                result[i] = static_cast<std::uint8_t>(std::clamp(scaled, 0.0, 255.0));
            }
        }
        // DC leakage changes with the phase of a sustained low tone in a
        // finite FFT window. It is not an acoustic attack. Keep the original
        // display spectrum, but exclude this bin from onset detection.
        onsetSpectrum_[0] = 0;
        return result;
    }

private:
    static constexpr double pi = 3.14159265358979323846;
    std::array<double, windowSize> window_{};
    std::array<std::complex<double>, binCount> roots_{};
    std::array<std::complex<double>, windowSize> scratch_{};
    std::array<double, binCount> smoothed_{};
    Spectrum onsetSpectrum_{};
    std::array<double, 8> attackMagnitudes_{};
    double rms_ = 0;
};
}
