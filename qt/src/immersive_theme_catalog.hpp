#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string_view>

namespace agplayer::immersive {

// This catalog is data only. It intentionally does not decide how a renderer
// maps a palette to materials, lights, audio or geometry.
enum class ThemeColorRole : std::size_t {
    BasePrimary,
    BaseSecondary,
    Fog,
    CoolCore,
    CoolEdge,
    WarmCore,
    WarmEdge,
    Ripple,
    Count,
};

constexpr std::size_t themeColorRoleCount = static_cast<std::size_t>(ThemeColorRole::Count);
constexpr std::size_t themeColorIndex(const ThemeColorRole role) noexcept
{
    return static_cast<std::size_t>(role);
}

enum class ThemeColorEncoding { WorkingLinear, Srgb };

struct LinearRgb { float red; float green; float blue; };
struct SrgbRgb { float red; float green; float blue; };

struct ThemeColorInput {
    ThemeColorEncoding encoding;
    float red;
    float green;
    float blue;
};

struct BuiltInTheme {
    std::string_view id;
    std::string_view displayName;
    std::array<ThemeColorInput, themeColorRoleCount> colors;
    float glowIntensity;
};

inline float srgbChannelToLinear(const float channel) noexcept
{
    const float value = std::clamp(channel, 0.0F, 1.0F);
    return value <= 0.04045F
        ? value / 12.92F
        : std::pow((value + 0.055F) / 1.055F, 2.4F);
}

inline float workingLinearChannelToSrgb(const float channel) noexcept
{
    const float value = std::clamp(channel, 0.0F, 1.0F);
    return value <= 0.0031308F
        ? value * 12.92F
        : 1.055F * std::pow(value, 1.0F / 2.4F) - 0.055F;
}

inline LinearRgb toWorkingLinear(const ThemeColorInput color) noexcept
{
    if (color.encoding == ThemeColorEncoding::WorkingLinear) {
        return {color.red, color.green, color.blue};
    }
    return {srgbChannelToLinear(color.red), srgbChannelToLinear(color.green),
            srgbChannelToLinear(color.blue)};
}

inline SrgbRgb workingLinearToSrgb(const LinearRgb color) noexcept
{
    return {workingLinearChannelToSrgb(color.red),
            workingLinearChannelToSrgb(color.green),
            workingLinearChannelToSrgb(color.blue)};
}

// Input convention: values tagged WorkingLinear are exact fixed-study numeric
// RGB facts. The later built-ins originate in six-digit sRGB notation and are
// converted here before exposing the catalog, so renderer consumers receive
// the same working-linear values without a rounded intermediate table.
inline const std::array<BuiltInTheme, 13>& builtInThemes()
{
    constexpr auto l = [](const float r, const float g, const float b) {
        return ThemeColorInput{ThemeColorEncoding::WorkingLinear, r, g, b};
    };
    const auto fromSrgb8 = [](const int red, const int green, const int blue) {
        return toWorkingLinear({ThemeColorEncoding::Srgb, red / 255.0F,
                                green / 255.0F, blue / 255.0F});
    };
    const auto asWorking = [](const LinearRgb color) {
        return ThemeColorInput{ThemeColorEncoding::WorkingLinear,
                               color.red, color.green, color.blue};
    };
    const auto s = [&fromSrgb8, &asWorking](int r, int g, int b) {
        return asWorking(fromSrgb8(r, g, b));
    };
    const auto mix = [](const LinearRgb first, const LinearRgb second,
                        const float secondWeight) {
        const float firstWeight = 1.0F - secondWeight;
        return LinearRgb{first.red * firstWeight + second.red * secondWeight,
                         first.green * firstWeight + second.green * secondWeight,
                         first.blue * firstWeight + second.blue * secondWeight};
    };
    const auto hexTheme = [&fromSrgb8, &asWorking, &mix](std::string_view id,
                                                          std::string_view title,
                                                          const std::array<int, 3> background,
                                                          const std::array<int, 3> fog,
                                                          const std::array<int, 3> cool,
                                                          const std::array<int, 3> warm,
                                                          const std::array<int, 3> ripple,
                                                          const float glow) {
        const LinearRgb base = fromSrgb8(background[0], background[1], background[2]);
        const LinearRgb coolCore = fromSrgb8(cool[0], cool[1], cool[2]);
        const LinearRgb warmCore = fromSrgb8(warm[0], warm[1], warm[2]);
        return BuiltInTheme{id, title,
            {asWorking(base), asWorking(mix(base, {1.0F, 1.0F, 1.0F}, 0.12F)),
             asWorking(fromSrgb8(fog[0], fog[1], fog[2])), asWorking(coolCore),
             asWorking(mix(coolCore, base, 0.35F)), asWorking(warmCore),
             asWorking(mix(warmCore, base, 0.35F)),
             asWorking(fromSrgb8(ripple[0], ripple[1], ripple[2]))}, glow};
    };
    static const std::array<BuiltInTheme, 13> themes{{
        {"ink-wash", "Ink Wash", {l(1,1,1),l(1,1,1),l(1,1,1),l(0,0,0),l(.35F,.35F,.35F),l(0,0,0),l(.35F,.35F,.35F),l(.66F,.74F,.76F)}, 1.10F},
        {"nocturnal", "Nocturnal", {l(.01F,.02F,.04F),l(.03F,.05F,.09F),l(.01F,.02F,.04F),l(0,.3F,1),l(.6F,.2F,1),l(1,.2F,.1F),l(1,.6F,0),l(.2F,.9F,1)}, 1.00F},
        {"neon-tokyo", "Neon Tokyo", {l(.01F,.005F,.02F),l(.04F,.01F,.06F),l(.01F,.005F,.02F),l(1,.1F,.6F),l(.6F,.1F,1),l(.1F,1,.8F),l(.1F,.4F,1),l(1,1,1)}, 1.50F},
        {"cyber-forest", "Cyber Forest", {l(.01F,.02F,.01F),l(.02F,.05F,.02F),l(.01F,.02F,.01F),l(.1F,1,.5F),l(.05F,.5F,.3F),l(.8F,1,.1F),l(.9F,.5F,.1F),l(.6F,1,.3F)}, 1.30F},
        {"minimal-monochrome", "Minimal Monochrome", {l(.02F,.02F,.02F),l(.06F,.06F,.06F),l(.02F,.02F,.02F),l(.9F,.9F,.9F),l(.4F,.4F,.4F),l(1,1,1),l(.7F,.7F,.7F),l(1,1,1)}, .80F},
        hexTheme("glacier-day", "Glacier Day", {216,230,234}, {229,238,240}, {45,142,163}, {217,111,77}, {47,89,99}, .82F),
        hexTheme("koi-pond", "Koi Pond", {18,58,54}, {15,44,42}, {85,214,178}, {242,166,90}, {200,238,228}, 1.12F),
        hexTheme("coral-reef", "Coral Reef", {64,37,42}, {47,32,36}, {95,202,208}, {232,112,95}, {240,183,164}, 1.08F),
        hexTheme("moss-glass", "Moss Glass", {46,58,36}, {36,48,30}, {136,200,163}, {214,195,109}, {221,232,179}, .98F),
        hexTheme("blue-hour", "Blue Hour", {39,60,85}, {29,49,72}, {139,197,231}, {242,140,114}, {207,231,244}, 1.05F),
        hexTheme("porcelain-teal", "Porcelain Teal", {221,232,228}, {238,244,241}, {36,120,111}, {184,93,77}, {79,112,106}, .78F),
        hexTheme("wine-signal", "Wine Signal", {58,36,48}, {47,32,42}, {131,197,190}, {217,93,115}, {240,203,211}, 1.06F),
        // Calibrated for the reference shader's direct linear output: purple
        // surfaces, pink interior and a small warm-yellow hot center.
        {"violet-heart", "Violet Heart", {s(5,2,10),s(107,57,155),s(4,2,8),
            s(201,122,255),s(165,90,218),s(255,164,218),s(255,246,159),s(201,122,255)}, 1.10F},
    }};
    return themes;
}

inline const BuiltInTheme& defaultBuiltInTheme() noexcept
{
    return builtInThemes().at(4); // minimal-monochrome, retained by stable order test.
}

inline const BuiltInTheme* findBuiltInTheme(const std::string_view id) noexcept
{
    const auto& themes = builtInThemes();
    const auto it = std::find_if(themes.begin(), themes.end(),
        [id](const BuiltInTheme& theme) { return theme.id == id; });
    return it == themes.end() ? nullptr : &*it;
}

} // namespace agplayer::immersive
