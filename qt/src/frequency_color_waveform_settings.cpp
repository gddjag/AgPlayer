#include "frequency_color_waveform_settings.hpp"

#include <QColor>
#include <QRegularExpression>

#include <algorithm>
#include <array>
#include <cmath>

namespace {

constexpr int kSchemaVersion = 1;
constexpr double kPi = 3.14159265358979323846;

struct Defaults {
    const char* darkColor;
    const char* lightColor;
    double darkOpacity;
    double lightOpacity;
};

constexpr std::array<Defaults, 4> kDefaults{{
    {"#7a8490", "#59636d", 0.18, 0.14},
    {"#269a8e", "#146b64", 0.44, 0.36},
    {"#c66b55", "#9d4938", 0.38, 0.32},
    {"#b5a4c6", "#6e5a7d", 0.46, 0.40},
}};

QString strictColor(const QString& value)
{
    static const QRegularExpression pattern(
        QStringLiteral("^#[0-9a-fA-F]{6}$"));
    return pattern.match(value).hasMatch() ? value.toLower() : QString();
}

double quantizedOpacity(double value, double fallback) noexcept
{
    if (!std::isfinite(value)) {
        return fallback;
    }
    const double clamped = std::clamp(value, 0.0, 1.0);
    return std::floor(clamped * 100.0 + 0.5 + 1e-12) / 100.0;
}

double srgbToLinear(double value)
{
    return value <= 0.04045 ? value / 12.92
                            : std::pow((value + 0.055) / 1.055, 2.4);
}

double linearToSrgb(double value)
{
    return value <= 0.0031308 ? 12.92 * value
                              : 1.055 * std::pow(value, 1.0 / 2.4) - 0.055;
}

struct Oklch {
    double lightness = 0.0;
    double chroma = 0.0;
    double hue = 0.0;
};

Oklch toOklch(const QColor& color)
{
    const double r = srgbToLinear(color.redF());
    const double g = srgbToLinear(color.greenF());
    const double b = srgbToLinear(color.blueF());
    const double l = std::cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
    const double m = std::cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
    const double s = std::cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
    const double lightness = 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s;
    const double a = 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s;
    const double yellowBlue = 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s;
    double hue = std::atan2(yellowBlue, a);
    if (hue < 0.0) {
        hue += 2.0 * kPi;
    }
    return {lightness, std::hypot(a, yellowBlue), hue};
}

struct LinearRgb {
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
};

LinearRgb fromOklch(double lightness, double chroma, double hue)
{
    const double a = chroma * std::cos(hue);
    const double yellowBlue = chroma * std::sin(hue);
    const double lRoot = lightness + 0.3963377774 * a + 0.2158037573 * yellowBlue;
    const double mRoot = lightness - 0.1055613458 * a - 0.0638541728 * yellowBlue;
    const double sRoot = lightness - 0.0894841775 * a - 1.2914855480 * yellowBlue;
    const double l = lRoot * lRoot * lRoot;
    const double m = mRoot * mRoot * mRoot;
    const double s = sRoot * sRoot * sRoot;
    return {
        4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
        -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
        -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s,
    };
}

bool inGamut(const LinearRgb& color)
{
    constexpr double epsilon = 1e-7;
    return color.r >= -epsilon && color.r <= 1.0 + epsilon
        && color.g >= -epsilon && color.g <= 1.0 + epsilon
        && color.b >= -epsilon && color.b <= 1.0 + epsilon;
}

LinearRgb gamutMapped(double lightness, double requestedChroma, double hue)
{
    LinearRgb candidate = fromOklch(lightness, requestedChroma, hue);
    if (inGamut(candidate)) {
        return candidate;
    }
    double low = 0.0;
    double high = requestedChroma;
    for (int iteration = 0; iteration < 24; ++iteration) {
        const double mid = (low + high) * 0.5;
        candidate = fromOklch(lightness, mid, hue);
        if (inGamut(candidate)) {
            low = mid;
        } else {
            high = mid;
        }
    }
    return fromOklch(lightness, low, hue);
}

double luminance(const LinearRgb& color)
{
    return 0.2126 * color.r + 0.7152 * color.g + 0.0722 * color.b;
}

double contrast(const LinearRgb& first, const LinearRgb& second)
{
    const double lighter = std::max(luminance(first), luminance(second));
    const double darker = std::min(luminance(first), luminance(second));
    return (lighter + 0.05) / (darker + 0.05);
}

QString toHex(const LinearRgb& linear)
{
    const auto encoded = [](double value) {
        return std::clamp(linearToSrgb(std::clamp(value, 0.0, 1.0)), 0.0, 1.0);
    };
    return QColor::fromRgbF(encoded(linear.r), encoded(linear.g), encoded(linear.b))
        .name(QColor::HexRgb).toLower();
}

QString adaptedColor(const QString& source, bool forDarkSurface)
{
    const Oklch input = toOklch(QColor(source));
    const double chroma = std::min(input.chroma, 0.16);
    const double minimum = forDarkSurface ? 0.68 : 0.34;
    const double maximum = forDarkSurface ? 0.82 : 0.48;
    double lightness = std::clamp(input.lightness, minimum, maximum);
    const QColor backgroundColor(forDarkSurface ? QStringLiteral("#0b1017")
                                                : QStringLiteral("#f5f3ef"));
    const LinearRgb background{
        srgbToLinear(backgroundColor.redF()),
        srgbToLinear(backgroundColor.greenF()),
        srgbToLinear(backgroundColor.blueF()),
    };
    LinearRgb candidate = gamutMapped(lightness, chroma, input.hue);
    if (contrast(candidate, background) < 3.0) {
        double passing = forDarkSurface ? maximum : minimum;
        double failing = lightness;
        for (int iteration = 0; iteration < 24; ++iteration) {
            const double mid = (passing + failing) * 0.5;
            const LinearRgb tested = gamutMapped(mid, chroma, input.hue);
            if (contrast(tested, background) >= 3.0) {
                passing = mid;
                candidate = tested;
            } else {
                failing = mid;
            }
        }
        candidate = gamutMapped(passing, chroma, input.hue);
    }
    return toHex(candidate);
}

struct LegacyPalette {
    const char* low;
    const char* mid;
    const char* high;
    double strength;
};

constexpr std::array<LegacyPalette, 7> kKnownLegacyPalettes{{
    {"#ff647c", "#3ed6ae", "#8a7cff", 0.85},
    {"#c45100", "#b04bcd", "#0a819a", 0.85},
    {"#c65332", "#9a4bc2", "#2e9b61", 0.70},
    {"#c47a5e", "#7787b5", "#4f9279", 0.62},
    {"#315cff", "#ffb02e", "#f04ca5", 0.62},
    {"#62c8a0", "#f1d79a", "#e98582", 0.62},
    {"#1098ad", "#f59f00", "#ae3ec9", 0.62},
}};

bool approximately(double first, double second)
{
    return std::abs(first - second) <= 0.000001;
}

} // namespace

FrequencyColorWaveformSettings::FrequencyColorWaveformSettings(QObject* parent)
    : QObject(parent)
{
}

int FrequencyColorWaveformSettings::roleIndex(Role role) noexcept
{
    return static_cast<int>(role);
}

QString FrequencyColorWaveformSettings::preset() const { return preset_; }
QString FrequencyColorWaveformSettings::mixDarkColor() const { return mixDarkColor_; }
QString FrequencyColorWaveformSettings::lowDarkColor() const { return lowDarkColor_; }
QString FrequencyColorWaveformSettings::midDarkColor() const { return midDarkColor_; }
QString FrequencyColorWaveformSettings::highDarkColor() const { return highDarkColor_; }
QString FrequencyColorWaveformSettings::mixLightColor() const { return mixLightColor_; }
QString FrequencyColorWaveformSettings::lowLightColor() const { return lowLightColor_; }
QString FrequencyColorWaveformSettings::midLightColor() const { return midLightColor_; }
QString FrequencyColorWaveformSettings::highLightColor() const { return highLightColor_; }
double FrequencyColorWaveformSettings::mixDarkOpacity() const noexcept { return mixDarkOpacity_; }
double FrequencyColorWaveformSettings::lowDarkOpacity() const noexcept { return lowDarkOpacity_; }
double FrequencyColorWaveformSettings::midDarkOpacity() const noexcept { return midDarkOpacity_; }
double FrequencyColorWaveformSettings::highDarkOpacity() const noexcept { return highDarkOpacity_; }
double FrequencyColorWaveformSettings::mixLightOpacity() const noexcept { return mixLightOpacity_; }
double FrequencyColorWaveformSettings::lowLightOpacity() const noexcept { return lowLightOpacity_; }
double FrequencyColorWaveformSettings::midLightOpacity() const noexcept { return midLightOpacity_; }
double FrequencyColorWaveformSettings::highLightOpacity() const noexcept { return highLightOpacity_; }
bool FrequencyColorWaveformSettings::playFocus() const noexcept { return playFocus_; }

void FrequencyColorWaveformSettings::setMixDarkColor(const QString& value) { setDarkColor(Role::Mix, value); }
void FrequencyColorWaveformSettings::setLowDarkColor(const QString& value) { setDarkColor(Role::Low, value); }
void FrequencyColorWaveformSettings::setMidDarkColor(const QString& value) { setDarkColor(Role::Mid, value); }
void FrequencyColorWaveformSettings::setHighDarkColor(const QString& value) { setDarkColor(Role::High, value); }
void FrequencyColorWaveformSettings::setMixLightColor(const QString& value) { setLightColor(Role::Mix, value); }
void FrequencyColorWaveformSettings::setLowLightColor(const QString& value) { setLightColor(Role::Low, value); }
void FrequencyColorWaveformSettings::setMidLightColor(const QString& value) { setLightColor(Role::Mid, value); }
void FrequencyColorWaveformSettings::setHighLightColor(const QString& value) { setLightColor(Role::High, value); }
void FrequencyColorWaveformSettings::setMixDarkOpacity(double value) { setOpacity(Role::Mix, Surface::Dark, value); }
void FrequencyColorWaveformSettings::setLowDarkOpacity(double value) { setOpacity(Role::Low, Surface::Dark, value); }
void FrequencyColorWaveformSettings::setMidDarkOpacity(double value) { setOpacity(Role::Mid, Surface::Dark, value); }
void FrequencyColorWaveformSettings::setHighDarkOpacity(double value) { setOpacity(Role::High, Surface::Dark, value); }
void FrequencyColorWaveformSettings::setMixLightOpacity(double value) { setOpacity(Role::Mix, Surface::Light, value); }
void FrequencyColorWaveformSettings::setLowLightOpacity(double value) { setOpacity(Role::Low, Surface::Light, value); }
void FrequencyColorWaveformSettings::setMidLightOpacity(double value) { setOpacity(Role::Mid, Surface::Light, value); }
void FrequencyColorWaveformSettings::setHighLightOpacity(double value) { setOpacity(Role::High, Surface::Light, value); }

void FrequencyColorWaveformSettings::setPlayFocus(bool value)
{
    const bool stateChanged = playFocus_ != value;
    playFocus_ = value;
    markCustomAndNotify(stateChanged);
}

QString& FrequencyColorWaveformSettings::color(Role role, Surface surface)
{
    if (surface == Surface::Dark) {
        switch (role) {
        case Role::Mix: return mixDarkColor_;
        case Role::Low: return lowDarkColor_;
        case Role::Mid: return midDarkColor_;
        case Role::High: return highDarkColor_;
        }
    }
    switch (role) {
    case Role::Mix: return mixLightColor_;
    case Role::Low: return lowLightColor_;
    case Role::Mid: return midLightColor_;
    case Role::High: return highLightColor_;
    }
    return mixDarkColor_;
}

const QString& FrequencyColorWaveformSettings::color(Role role, Surface surface) const
{
    return const_cast<FrequencyColorWaveformSettings*>(this)->color(role, surface);
}

double& FrequencyColorWaveformSettings::opacity(Role role, Surface surface)
{
    if (surface == Surface::Dark) {
        switch (role) {
        case Role::Mix: return mixDarkOpacity_;
        case Role::Low: return lowDarkOpacity_;
        case Role::Mid: return midDarkOpacity_;
        case Role::High: return highDarkOpacity_;
        }
    }
    switch (role) {
    case Role::Mix: return mixLightOpacity_;
    case Role::Low: return lowLightOpacity_;
    case Role::Mid: return midLightOpacity_;
    case Role::High: return highLightOpacity_;
    }
    return mixDarkOpacity_;
}

double FrequencyColorWaveformSettings::opacity(Role role, Surface surface) const
{
    return const_cast<FrequencyColorWaveformSettings*>(this)->opacity(role, surface);
}

bool& FrequencyColorWaveformSettings::manual(Role role, Surface surface)
{
    if (surface == Surface::Dark) {
        switch (role) {
        case Role::Mix: return mixDarkManual_;
        case Role::Low: return lowDarkManual_;
        case Role::Mid: return midDarkManual_;
        case Role::High: return highDarkManual_;
        }
    }
    switch (role) {
    case Role::Mix: return mixLightManual_;
    case Role::Low: return lowLightManual_;
    case Role::Mid: return midLightManual_;
    case Role::High: return highLightManual_;
    }
    return mixDarkManual_;
}

bool FrequencyColorWaveformSettings::manual(Role role, Surface surface) const
{
    return const_cast<FrequencyColorWaveformSettings*>(this)->manual(role, surface);
}

void FrequencyColorWaveformSettings::markCustomAndNotify(bool stateChanged)
{
    if (preset_ != QStringLiteral("custom")) {
        preset_ = QStringLiteral("custom");
        stateChanged = true;
    }
    if (stateChanged) {
        emit changed();
    }
}

void FrequencyColorWaveformSettings::setDarkColor(Role role, const QString& value)
{
    const QString normalized = strictColor(value);
    if (normalized.isEmpty()) {
        return;
    }
    bool stateChanged = color(role, Surface::Dark) != normalized;
    color(role, Surface::Dark) = normalized;
    if (!manual(role, Surface::Dark)) {
        manual(role, Surface::Dark) = true;
        stateChanged = true;
    }
    if (!manual(role, Surface::Light)) {
        const QString generated = adaptedColor(normalized, false);
        if (color(role, Surface::Light) != generated) {
            color(role, Surface::Light) = generated;
            stateChanged = true;
        }
    }
    markCustomAndNotify(stateChanged);
}

void FrequencyColorWaveformSettings::setLightColor(Role role, const QString& value)
{
    const QString normalized = strictColor(value);
    if (normalized.isEmpty()) {
        return;
    }
    bool stateChanged = color(role, Surface::Light) != normalized;
    color(role, Surface::Light) = normalized;
    if (!manual(role, Surface::Light)) {
        manual(role, Surface::Light) = true;
        stateChanged = true;
    }
    if (!manual(role, Surface::Dark)) {
        const QString generated = adaptedColor(normalized, true);
        if (color(role, Surface::Dark) != generated) {
            color(role, Surface::Dark) = generated;
            stateChanged = true;
        }
    }
    markCustomAndNotify(stateChanged);
}

void FrequencyColorWaveformSettings::setOpacity(Role role, Surface surface, double value)
{
    const int index = roleIndex(role);
    const double fallback = surface == Surface::Dark
        ? kDefaults[static_cast<std::size_t>(index)].darkOpacity
        : kDefaults[static_cast<std::size_t>(index)].lightOpacity;
    const double normalized = quantizedOpacity(value, fallback);
    const bool stateChanged = !approximately(opacity(role, surface), normalized);
    opacity(role, surface) = normalized;
    markCustomAndNotify(stateChanged);
}

void FrequencyColorWaveformSettings::resetToLuminousGlaze()
{
    const bool stateChanged = preset_ != QStringLiteral("luminousGlaze")
        || mixDarkColor_ != QStringLiteral("#7a8490")
        || lowDarkColor_ != QStringLiteral("#269a8e")
        || midDarkColor_ != QStringLiteral("#c66b55")
        || highDarkColor_ != QStringLiteral("#b5a4c6")
        || mixLightColor_ != QStringLiteral("#59636d")
        || lowLightColor_ != QStringLiteral("#146b64")
        || midLightColor_ != QStringLiteral("#9d4938")
        || highLightColor_ != QStringLiteral("#6e5a7d")
        || !approximately(mixDarkOpacity_, 0.18)
        || !approximately(lowDarkOpacity_, 0.44)
        || !approximately(midDarkOpacity_, 0.38)
        || !approximately(highDarkOpacity_, 0.46)
        || !approximately(mixLightOpacity_, 0.14)
        || !approximately(lowLightOpacity_, 0.36)
        || !approximately(midLightOpacity_, 0.32)
        || !approximately(highLightOpacity_, 0.40)
        || !playFocus_ || mixDarkManual_ || lowDarkManual_ || midDarkManual_
        || highDarkManual_ || mixLightManual_ || lowLightManual_
        || midLightManual_ || highLightManual_;

    preset_ = QStringLiteral("luminousGlaze");
    for (int index = 0; index < 4; ++index) {
        const Role role = static_cast<Role>(index);
        color(role, Surface::Dark) = QString::fromLatin1(kDefaults[index].darkColor);
        color(role, Surface::Light) = QString::fromLatin1(kDefaults[index].lightColor);
        opacity(role, Surface::Dark) = kDefaults[index].darkOpacity;
        opacity(role, Surface::Light) = kDefaults[index].lightOpacity;
        manual(role, Surface::Dark) = false;
        manual(role, Surface::Light) = false;
    }
    playFocus_ = true;
    if (stateChanged) {
        emit changed();
    }
}

void FrequencyColorWaveformSettings::load(QSettings& settings)
{
    if (settings.value(QStringLiteral("waveformFrequencyColorSchema"), 0).toInt()
        >= kSchemaVersion) {
        const QString previousPreset = preset_;
        const auto readColor = [&settings](const QString& key, const char* fallback) {
            const QString normalized = strictColor(settings.value(
                key, QString::fromLatin1(fallback)).toString());
            return normalized.isEmpty() ? QString::fromLatin1(fallback) : normalized;
        };
        const auto readOpacity = [&settings](const QString& key, double fallback) {
            bool converted = false;
            const double stored = settings.value(key, fallback).toDouble(&converted);
            return converted ? quantizedOpacity(stored, fallback) : fallback;
        };
        preset_ = settings.value(QStringLiteral("waveformFrequencyColorPreset"),
                                 QStringLiteral("luminousGlaze")).toString();
        if (preset_ != QStringLiteral("custom")) {
            preset_ = QStringLiteral("luminousGlaze");
        }
        bool stateChanged = previousPreset != preset_;
        for (int index = 0; index < 4; ++index) {
            const Role role = static_cast<Role>(index);
            const QString roleName = index == 0 ? QStringLiteral("Mix")
                : index == 1 ? QStringLiteral("Low")
                : index == 2 ? QStringLiteral("Mid") : QStringLiteral("High");
            const QString dark = readColor(
                QStringLiteral("waveformFrequency%1DarkColor").arg(roleName),
                kDefaults[index].darkColor);
            const QString light = readColor(
                QStringLiteral("waveformFrequency%1LightColor").arg(roleName),
                kDefaults[index].lightColor);
            const double darkOpacity = readOpacity(
                QStringLiteral("waveformFrequency%1DarkOpacity").arg(roleName),
                kDefaults[index].darkOpacity);
            const double lightOpacity = readOpacity(
                QStringLiteral("waveformFrequency%1LightOpacity").arg(roleName),
                kDefaults[index].lightOpacity);
            const bool darkManual = settings.value(
                QStringLiteral("waveformFrequency%1DarkManual").arg(roleName), false).toBool();
            const bool lightManual = settings.value(
                QStringLiteral("waveformFrequency%1LightManual").arg(roleName), false).toBool();
            stateChanged = stateChanged || color(role, Surface::Dark) != dark
                || color(role, Surface::Light) != light
                || !approximately(opacity(role, Surface::Dark), darkOpacity)
                || !approximately(opacity(role, Surface::Light), lightOpacity)
                || manual(role, Surface::Dark) != darkManual
                || manual(role, Surface::Light) != lightManual;
            color(role, Surface::Dark) = dark;
            color(role, Surface::Light) = light;
            opacity(role, Surface::Dark) = darkOpacity;
            opacity(role, Surface::Light) = lightOpacity;
            manual(role, Surface::Dark) = darkManual;
            manual(role, Surface::Light) = lightManual;
        }
        const bool playFocus = settings.value(
            QStringLiteral("waveformFrequencyPlayFocus"), true).toBool();
        stateChanged = stateChanged || playFocus_ != playFocus;
        playFocus_ = playFocus;
        if (stateChanged) {
            emit changed();
        }
        return;
    }

    const bool hasLow = settings.contains(QStringLiteral("waveformFrequencyLowColor"));
    const bool hasMid = settings.contains(QStringLiteral("waveformFrequencyMidColor"));
    const bool hasHigh = settings.contains(QStringLiteral("waveformFrequencyHighColor"));
    const bool hasStrength = settings.contains(QStringLiteral("waveformFrequencyStrength"));
    const QString low = strictColor(settings.value(
        QStringLiteral("waveformFrequencyLowColor"), QStringLiteral("#269a8e")).toString());
    const QString mid = strictColor(settings.value(
        QStringLiteral("waveformFrequencyMidColor"), QStringLiteral("#c66b55")).toString());
    const QString high = strictColor(settings.value(
        QStringLiteral("waveformFrequencyHighColor"), QStringLiteral("#b5a4c6")).toString());
    bool strengthConverted = false;
    const double rawStrength = settings.value(
        QStringLiteral("waveformFrequencyStrength"), 0.62).toDouble(&strengthConverted);
    const bool strengthValid = strengthConverted && std::isfinite(rawStrength);
    const double strength = quantizedOpacity(rawStrength, 0.62);
    bool knownDefault = !(hasLow || hasMid || hasHigh || hasStrength);
    if (hasLow && hasMid && hasHigh && hasStrength && strengthValid) {
        for (const LegacyPalette& known : kKnownLegacyPalettes) {
            if (low == QString::fromLatin1(known.low)
                && mid == QString::fromLatin1(known.mid)
                && high == QString::fromLatin1(known.high)
                && approximately(strength, known.strength)) {
                knownDefault = true;
                break;
            }
        }
    }

    if (knownDefault) {
        resetToLuminousGlaze();
    } else {
        resetToLuminousGlaze();
        preset_ = QStringLiteral("custom");
        lowDarkColor_ = low.isEmpty() ? QStringLiteral("#269a8e") : low;
        midDarkColor_ = mid.isEmpty() ? QStringLiteral("#c66b55") : mid;
        highDarkColor_ = high.isEmpty() ? QStringLiteral("#b5a4c6") : high;
        lowLightColor_ = adaptedColor(lowDarkColor_, false);
        midLightColor_ = adaptedColor(midDarkColor_, false);
        highLightColor_ = adaptedColor(highDarkColor_, false);
        for (const Role role : {Role::Low, Role::Mid, Role::High}) {
            const int index = roleIndex(role);
            opacity(role, Surface::Dark) = quantizedOpacity(
                strength * kDefaults[index].darkOpacity / 0.62,
                kDefaults[index].darkOpacity);
            opacity(role, Surface::Light) = quantizedOpacity(
                strength * kDefaults[index].lightOpacity / 0.62,
                kDefaults[index].lightOpacity);
        }
    }
    save(settings); // New state and compatibility aliases are written before schema last.
}

void FrequencyColorWaveformSettings::save(QSettings& settings) const
{
    settings.setValue(QStringLiteral("waveformFrequencyColorPreset"), preset_);
    for (int index = 0; index < 4; ++index) {
        const Role role = static_cast<Role>(index);
        const QString roleName = index == 0 ? QStringLiteral("Mix")
            : index == 1 ? QStringLiteral("Low")
            : index == 2 ? QStringLiteral("Mid") : QStringLiteral("High");
        settings.setValue(QStringLiteral("waveformFrequency%1DarkColor").arg(roleName),
                          color(role, Surface::Dark));
        settings.setValue(QStringLiteral("waveformFrequency%1LightColor").arg(roleName),
                          color(role, Surface::Light));
        settings.setValue(QStringLiteral("waveformFrequency%1DarkOpacity").arg(roleName),
                          opacity(role, Surface::Dark));
        settings.setValue(QStringLiteral("waveformFrequency%1LightOpacity").arg(roleName),
                          opacity(role, Surface::Light));
        settings.setValue(QStringLiteral("waveformFrequency%1DarkManual").arg(roleName),
                          manual(role, Surface::Dark));
        settings.setValue(QStringLiteral("waveformFrequency%1LightManual").arg(roleName),
                          manual(role, Surface::Light));
    }
    settings.setValue(QStringLiteral("waveformFrequencyPlayFocus"), playFocus_);
    settings.setValue(QStringLiteral("waveformFrequencyLowColor"), lowDarkColor_);
    settings.setValue(QStringLiteral("waveformFrequencyMidColor"), midDarkColor_);
    settings.setValue(QStringLiteral("waveformFrequencyHighColor"), highDarkColor_);
    settings.setValue(QStringLiteral("waveformFrequencyStrength"), legacyStrength());
    settings.setValue(QStringLiteral("waveformFrequencyColorSchema"), kSchemaVersion);
}

void FrequencyColorWaveformSettings::setLegacyStrength(double value)
{
    const double strength = quantizedOpacity(value, 0.62);
    bool stateChanged = false;
    for (const Role role : {Role::Low, Role::Mid, Role::High}) {
        const int index = roleIndex(role);
        const double dark = quantizedOpacity(
            strength * kDefaults[index].darkOpacity / 0.62,
            kDefaults[index].darkOpacity);
        const double light = quantizedOpacity(
            strength * kDefaults[index].lightOpacity / 0.62,
            kDefaults[index].lightOpacity);
        stateChanged = stateChanged
            || !approximately(opacity(role, Surface::Dark), dark)
            || !approximately(opacity(role, Surface::Light), light);
        opacity(role, Surface::Dark) = dark;
        opacity(role, Surface::Light) = light;
    }
    markCustomAndNotify(stateChanged);
}

double FrequencyColorWaveformSettings::legacyStrength() const noexcept
{
    double sum = 0.0;
    int count = 0;
    for (const Role role : {Role::Low, Role::Mid, Role::High}) {
        const int index = roleIndex(role);
        sum += opacity(role, Surface::Dark) * 0.62 / kDefaults[index].darkOpacity;
        sum += opacity(role, Surface::Light) * 0.62 / kDefaults[index].lightOpacity;
        count += 2;
    }
    return quantizedOpacity(sum / count, 0.62);
}
