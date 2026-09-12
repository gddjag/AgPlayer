#include "frequency_color_waveform_settings.hpp"

#include <algorithm>
#include <cmath>

namespace {

const QColor defaultLow(QStringLiteral("#FF0000"));
const QColor defaultMid(QStringLiteral("#00FF00"));
const QColor defaultHigh(QStringLiteral("#0000FF"));
const QColor previousDefaultLow(QStringLiteral("#FC0909"));
const QColor previousDefaultMid(QStringLiteral("#03FF00"));
const QColor previousDefaultHigh(QStringLiteral("#0048FF"));
const QColor legacyDefaultLow(QStringLiteral("#8B3DFF"));
const QColor legacyDefaultMid(QStringLiteral("#FFB000"));
const QColor legacyDefaultHigh(QStringLiteral("#002FA7"));
constexpr int currentColorSchemaVersion = 4;
constexpr double defaultUnplayedDimness = 0.30;

QColor storedColor(QSettings& settings, const QString& key,
                   const QColor& fallback)
{
    const QColor color(settings.value(key, fallback.name()).toString());
    return color.isValid() ? color : fallback;
}

} // namespace

FrequencyColorWaveformSettings::FrequencyColorWaveformSettings(QObject* parent)
    : QObject(parent)
{
}

QColor FrequencyColorWaveformSettings::lowColor() const
{
    return lowColor_;
}

void FrequencyColorWaveformSettings::setLowColor(const QColor& color)
{
    if (!color.isValid() || color == lowColor_) return;
    lowColor_ = color;
    emit changed();
}

QColor FrequencyColorWaveformSettings::midColor() const
{
    return midColor_;
}

void FrequencyColorWaveformSettings::setMidColor(const QColor& color)
{
    if (!color.isValid() || color == midColor_) return;
    midColor_ = color;
    emit changed();
}

QColor FrequencyColorWaveformSettings::highColor() const
{
    return highColor_;
}

void FrequencyColorWaveformSettings::setHighColor(const QColor& color)
{
    if (!color.isValid() || color == highColor_) return;
    highColor_ = color;
    emit changed();
}

double FrequencyColorWaveformSettings::unplayedOpacity() const noexcept
{
    return 1.0 - unplayedDimness_;
}

void FrequencyColorWaveformSettings::setUnplayedOpacity(double opacity)
{
    const double clamped = std::clamp(
        std::isfinite(opacity) ? opacity : 1.0 - defaultUnplayedDimness,
        0.0, 1.0);
    setUnplayedDimness(1.0 - clamped);
}

double FrequencyColorWaveformSettings::unplayedDimness() const noexcept
{
    return unplayedDimness_;
}

void FrequencyColorWaveformSettings::setUnplayedDimness(double dimness)
{
    const double clamped = std::clamp(
        std::isfinite(dimness) ? dimness : defaultUnplayedDimness, 0.0, 1.0);
    if (qFuzzyCompare(unplayedDimness_ + 1.0, clamped + 1.0)) return;
    unplayedDimness_ = clamped;
    emit changed();
}

void FrequencyColorWaveformSettings::resetToDefault()
{
    const bool changedState = lowColor_ != defaultLow
        || midColor_ != defaultMid || highColor_ != defaultHigh
        || !qFuzzyCompare(unplayedDimness_ + 1.0,
                          defaultUnplayedDimness + 1.0);
    lowColor_ = defaultLow;
    midColor_ = defaultMid;
    highColor_ = defaultHigh;
    unplayedDimness_ = defaultUnplayedDimness;
    if (changedState) {
        emit changed();
    }
}

void FrequencyColorWaveformSettings::load(QSettings& settings)
{
    lowColor_ = storedColor(settings, QStringLiteral("waveformFrequencyLowColor"),
                            defaultLow);
    midColor_ = storedColor(settings, QStringLiteral("waveformFrequencyMidColor"),
                            defaultMid);
    highColor_ = storedColor(settings, QStringLiteral("waveformFrequencyHighColor"),
                             defaultHigh);
    const QString schemaKey = QStringLiteral(
        "waveformFrequencyColorSchemaVersion");
    const int storedSchema = settings.value(schemaKey, 0).toInt();
    // Match complete presets only. The older violet palette in schema 1+
    // was already a deliberate choice and retains its existing protection.
    const bool untouchedLegacy = storedSchema < 1
        && lowColor_ == legacyDefaultLow && midColor_ == legacyDefaultMid
        && highColor_ == legacyDefaultHigh;
    const bool untouchedPrevious = storedSchema < currentColorSchemaVersion
        && lowColor_ == previousDefaultLow && midColor_ == previousDefaultMid
        && highColor_ == previousDefaultHigh;
    if (untouchedLegacy || untouchedPrevious) {
        lowColor_ = defaultLow;
        midColor_ = defaultMid;
        highColor_ = defaultHigh;
        settings.setValue(QStringLiteral("waveformFrequencyLowColor"),
                          lowColor_.name(QColor::HexRgb));
        settings.setValue(QStringLiteral("waveformFrequencyMidColor"),
                          midColor_.name(QColor::HexRgb));
        settings.setValue(QStringLiteral("waveformFrequencyHighColor"),
                          highColor_.name(QColor::HexRgb));
    }
    const QString opacityKey = QStringLiteral(
        "waveformFrequencyUnplayedOpacity");
    const QString dimnessKey = QStringLiteral(
        "waveformFrequencyUnplayedDimness");
    const bool opacityWasStored = settings.contains(opacityKey);
    const bool dimnessWasStored = settings.contains(dimnessKey);
    double storedOpacity = settings.value(
        opacityKey, 1.0 - defaultUnplayedDimness).toDouble();
    const double storedDimness = dimnessWasStored
        ? settings.value(dimnessKey, defaultUnplayedDimness).toDouble()
        : 1.0 - storedOpacity;
    unplayedDimness_ = std::clamp(
        std::isfinite(storedDimness) ? storedDimness : defaultUnplayedDimness,
        0.0, 1.0);
    if (!dimnessWasStored) {
        settings.setValue(dimnessKey, unplayedDimness_);
    }
    if (storedSchema < currentColorSchemaVersion) {
        if (!opacityWasStored)
            settings.setValue(opacityKey, unplayedOpacity());
        settings.setValue(schemaKey, currentColorSchemaVersion);
    }
}

void FrequencyColorWaveformSettings::save(QSettings& settings) const
{
    settings.setValue(QStringLiteral("waveformFrequencyLowColor"),
                      lowColor_.name(QColor::HexRgb));
    settings.setValue(QStringLiteral("waveformFrequencyMidColor"),
                      midColor_.name(QColor::HexRgb));
    settings.setValue(QStringLiteral("waveformFrequencyHighColor"),
                      highColor_.name(QColor::HexRgb));
    settings.setValue(QStringLiteral("waveformFrequencyColorSchemaVersion"),
                      currentColorSchemaVersion);
    settings.setValue(QStringLiteral("waveformFrequencyUnplayedOpacity"),
                      unplayedOpacity());
    settings.setValue(QStringLiteral("waveformFrequencyUnplayedDimness"),
                      unplayedDimness_);
    settings.remove(QStringLiteral("waveformSpectralPalette"));
    settings.remove(QStringLiteral("waveformSpectralUnplayedOpacity"));
}
