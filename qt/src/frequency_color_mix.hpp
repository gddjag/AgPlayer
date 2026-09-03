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
    return std::pow(std::pow(clamped, 0.55), 1.20);
}

inline double applySoftKnee(const double channel) noexcept
{
    constexpr double strength = 1.35;
    const double clamped = std::clamp(channel, 0.0, 1.0);
    return (1.0 - std::exp(-strength * clamped))
        / (1.0 - std::exp(-strength));
}

inline void preserveAdditiveChroma(std::array<double, 3>& linear) noexcept
{
    // Dense music carries substantial energy in all three bands. Keep the
    // shared peak, but reduce the common light before the per-channel knee so
    // small band differences are not flattened into near-white output.
    double peak = *std::max_element(linear.begin(), linear.end());
    if (peak <= 0.0) return;
    if (peak > 1.0) {
        for (double& channel : linear) channel /= peak;
        peak = 1.0;
    }

    constexpr double commonRetention = 0.05;
    const double common = *std::min_element(linear.begin(), linear.end());
    for (double& channel : linear) {
        channel -= common * (1.0 - commonRetention);
    }

    const double adjustedPeak = *std::max_element(linear.begin(), linear.end());
    if (adjustedPeak > 0.0) {
        const double scale = peak / adjustedPeak;
        for (double& channel : linear) channel *= scale;
    }
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
    const std::array<double, 3> weights{
        detail::energyWeight(lowEnergy),
        detail::energyWeight(midEnergy),
        detail::energyWeight(highEnergy),
    };
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
    detail::preserveAdditiveChroma(linear);

    return QColor::fromRgbF(
        detail::linearToSrgb(detail::applySoftKnee(linear[0])),
        detail::linearToSrgb(detail::applySoftKnee(linear[1])),
        detail::linearToSrgb(detail::applySoftKnee(linear[2])));
}

} // namespace agplayer::ui
