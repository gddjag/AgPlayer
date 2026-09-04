#include "frequency_color_waveform_settings.hpp"

#include <algorithm>
#include <cmath>

namespace {

const QColor defaultLow(QStringLiteral("#FC0909"));
const QColor defaultMid(QStringLiteral("#03FF00"));
const QColor defaultHigh(QStringLiteral("#0048FF"));
const QColor legacyDefaultLow(QStringLiteral("#8B3DFF"));
const QColor legacyDefaultMid(QStringLiteral("#FFB000"));
const QColor legacyDefaultHigh(QStringLiteral("#002FA7"));
constexpr int currentColorSchemaVersion = 2;
constexpr double defaultUnplayedOpacity = 0.12;
constexpr double legacyDefaultUnplayedOpacity = 0.38;

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
    return unplayedOpacity_;
}

void FrequencyColorWaveformSettings::setUnplayedOpacity(double opacity)
{
    const double clamped = std::clamp(
        std::isfinite(opacity) ? opacity : defaultUnplayedOpacity, 0.0, 1.0);
    if (qFuzzyCompare(unplayedOpacity_ + 1.0, clamped + 1.0)) {
        return;
    }
    unplayedOpacity_ = clamped;
    emit changed();
}

void FrequencyColorWaveformSettings::resetToDefault()
{
    const bool changedState = lowColor_ != defaultLow
        || midColor_ != defaultMid || highColor_ != defaultHigh
        || !qFuzzyCompare(unplayedOpacity_ + 1.0,
                          defaultUnplayedOpacity + 1.0);
    lowColor_ = defaultLow;
    midColor_ = defaultMid;
    highColor_ = defaultHigh;
    unplayedOpacity_ = defaultUnplayedOpacity;
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
    if (storedSchema < 1) {
        if (lowColor_ == legacyDefaultLow && midColor_ == legacyDefaultMid
            && highColor_ == legacyDefaultHigh) {
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
    }
    const QString opacityKey = QStringLiteral(
        "waveformFrequencyUnplayedOpacity");
    double storedOpacity = settings.value(
        opacityKey, defaultUnplayedOpacity).toDouble();
    const bool opacityWasStored = settings.contains(opacityKey);
    if (storedSchema < 2
        && qFuzzyCompare(storedOpacity + 1.0,
                         legacyDefaultUnplayedOpacity + 1.0)) {
        storedOpacity = defaultUnplayedOpacity;
        settings.setValue(opacityKey, storedOpacity);
    }
    unplayedOpacity_ = std::clamp(
        std::isfinite(storedOpacity) ? storedOpacity : defaultUnplayedOpacity,
        0.0, 1.0);
    if (storedSchema < currentColorSchemaVersion) {
        if (!opacityWasStored) {
            settings.setValue(opacityKey, unplayedOpacity_);
        }
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
                      unplayedOpacity_);
    settings.remove(QStringLiteral("waveformSpectralPalette"));
    settings.remove(QStringLiteral("waveformSpectralUnplayedOpacity"));
}
