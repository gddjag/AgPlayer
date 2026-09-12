#pragma once

#include <QColor>

#include <algorithm>
#include <array>
#include <cmath>

namespace agplayer::ui {
namespace detail {

inline double srgbToLinear(const double channel) noexcept
{
    return channel <= 0.04045
        ? channel / 12.92
        : std::pow((channel + 0.055) / 1.055, 2.4);
}

inline double linearToSrgb(const double channel) noexcept
{
    const double clamped = std::clamp(channel, 0.0, 1.0);
    return clamped <= 0.0031308
        ? clamped * 12.92
        : 1.055 * std::pow(clamped, 1.0 / 2.4) - 0.055;
}

inline double energyWeight(const double energy) noexcept
{
    const double clamped = std::clamp(
        std::isfinite(energy) ? energy : 0.0, 0.0, 1.0);
    return std::pow(clamped, 0.66);
}

inline double applySoftKnee(const double channel) noexcept
{
    constexpr double strength = 1.35;
    const double clamped = std::clamp(channel, 0.0, 1.0);
    return (1.0 - std::exp(-strength * clamped))
        / (1.0 - std::exp(-strength));
}

inline void mapAdditiveBrightness(std::array<double, 3>& linear,
                                   const double peakWeight) noexcept
{
    // Map energy once, independently of palette brightness. This keeps a dark
    // custom color continuous as its band approaches full energy. One shared
    // gain preserves linear-light channel ratios, including pastel mixtures.
    // Lift quiet audio in display brightness without changing its raw waveform
    // height; overlapping custom colors are scaled together to stay in gamut.
    const double peak = *std::max_element(linear.begin(), linear.end());
    if (peak <= 0.0 || peakWeight <= 0.0) return;
    const double gain = std::min(std::sqrt(applySoftKnee(peakWeight)) / peakWeight,
                                1.0 / peak);
    for (double& channel : linear) channel *= gain;
}

} // namespace detail

inline QColor mixFrequencyColor(const double lowEnergy,
                                const double midEnergy,
                                const double highEnergy,
                                const QColor& lowColor,
                                const QColor& midColor,
                                const QColor& highColor)
{
    const std::array<QColor, 3> colors{lowColor, midColor, highColor};
    const auto boundedEnergy = [](double value) {
        return std::clamp(std::isfinite(value) ? value : 0.0, 0.0, 1.0);
    };
    const std::array<double, 3> energies{
        boundedEnergy(lowEnergy), boundedEnergy(midEnergy), boundedEnergy(highEnergy)};
    const double peakEnergy = *std::max_element(energies.begin(), energies.end());
    const double peakWeight = detail::energyWeight(peakEnergy);
    std::array<double, 3> weights{};
    if (peakEnergy > 0.0) {
        // Keep absolute energy brightness separate from chroma. Lifting every
        // weak band with energy^0.66 washed bass/treble detail towards white.
        // A continuous relative contrast retains mixed hues without gates,
        // winner-take-all colours, or frame/viewport-dependent normalization.
        for (std::size_t index = 0; index < weights.size(); ++index) {
            weights[index] = peakWeight * std::pow(energies[index] / peakEnergy, 1.15);
        }
    }
    if (weights[0] == 0.0 && weights[1] == 0.0 && weights[2] == 0.0) {
        return lowColor;
    }

    for (std::size_t index = 0; index < weights.size(); ++index) {
        if (weights[index] == 1.0
            && weights[(index + 1) % weights.size()] == 0.0
            && weights[(index + 2) % weights.size()] == 0.0) {
            return colors[index];
        }
    }

    std::array<double, 3> linear{};
    for (std::size_t index = 0; index < colors.size(); ++index) {
        linear[0] += detail::srgbToLinear(colors[index].redF()) * weights[index];
        linear[1] += detail::srgbToLinear(colors[index].greenF()) * weights[index];
        linear[2] += detail::srgbToLinear(colors[index].blueF()) * weights[index];
    }
    detail::mapAdditiveBrightness(
        linear, *std::max_element(weights.begin(), weights.end()));

    return QColor::fromRgbF(
        detail::linearToSrgb(linear[0]),
        detail::linearToSrgb(linear[1]),
        detail::linearToSrgb(linear[2]));
}

} // namespace agplayer::ui
