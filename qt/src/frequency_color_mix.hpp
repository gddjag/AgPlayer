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

} // namespace detail

inline QColor mixFrequencyColor(const double lowEnergy,
                                const double midEnergy,
                                const double highEnergy,
                                const QColor& lowColor,
                                const QColor& midColor,
                                const QColor& highColor)
{
    const std::array<QColor, 3> colors{lowColor, midColor, highColor};
    std::array<double, 3> weights{
        std::sqrt(std::clamp(lowEnergy, 0.0, 1.0)),
        std::sqrt(std::clamp(midEnergy, 0.0, 1.0)),
        std::sqrt(std::clamp(highEnergy, 0.0, 1.0)),
    };
    constexpr double visible = 1.0e-6;
    int active = 0;
    int dominant = 0;
    for (int index = 0; index < 3; ++index) {
        if (weights[static_cast<std::size_t>(index)] > visible) ++active;
        if (weights[static_cast<std::size_t>(index)]
            > weights[static_cast<std::size_t>(dominant)]) {
            dominant = index;
        }
    }
    if (active == 1) return colors[static_cast<std::size_t>(dominant)];
    if (active == 0) return lowColor;

    const double sum = weights[0] + weights[1] + weights[2];
    for (double& weight : weights) weight /= sum;

    std::array<double, 3> linear{};
    double targetSaturation = 0.0;
    double targetLightness = 0.0;
    for (std::size_t index = 0; index < colors.size(); ++index) {
        linear[0] += detail::srgbToLinear(colors[index].redF()) * weights[index];
        linear[1] += detail::srgbToLinear(colors[index].greenF()) * weights[index];
        linear[2] += detail::srgbToLinear(colors[index].blueF()) * weights[index];
        targetSaturation += colors[index].hslSaturationF() * weights[index];
        targetLightness += colors[index].lightnessF() * weights[index];
    }

    QColor mixed = QColor::fromRgbF(detail::linearToSrgb(linear[0]),
                                    detail::linearToSrgb(linear[1]),
                                    detail::linearToSrgb(linear[2]));
    float hue = 0.0F;
    float saturation = 0.0F;
    float lightness = 0.0F;
    float alpha = 1.0F;
    mixed.getHslF(&hue, &saturation, &lightness, &alpha);
    if (hue < 0.0) hue = colors[static_cast<std::size_t>(dominant)].hslHueF();
    saturation = static_cast<float>(std::clamp(
        std::max<double>(saturation, targetSaturation * 0.75), 0.0, 1.0));
    lightness = static_cast<float>(std::clamp(
        std::max<double>(lightness, targetLightness * 0.90), 0.18, 0.78));
    return QColor::fromHslF(hue, saturation, lightness, 1.0);
}

} // namespace agplayer::ui
