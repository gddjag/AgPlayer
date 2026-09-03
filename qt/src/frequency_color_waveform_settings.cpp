#include "frequency_color_waveform_settings.hpp"

#include <algorithm>
#include <cmath>

namespace {

const QColor defaultLow(QStringLiteral("#FC0909"));
const QColor defaultMid(QStringLiteral("#03FF00"));
const QColor defaultHigh(QStringLiteral("#0048FF"));

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
        std::isfinite(opacity) ? opacity : 0.88, 0.60, 1.0);
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
        || !qFuzzyCompare(unplayedOpacity_ + 1.0, 1.88);
    lowColor_ = defaultLow;
    midColor_ = defaultMid;
    highColor_ = defaultHigh;
    unplayedOpacity_ = 0.88;
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
    const double storedOpacity = settings.value(
        QStringLiteral("waveformFrequencyUnplayedOpacity"), 0.88).toDouble();
    unplayedOpacity_ = std::clamp(
        std::isfinite(storedOpacity) ? storedOpacity : 0.88, 0.60, 1.0);
}

void FrequencyColorWaveformSettings::save(QSettings& settings) const
{
    settings.setValue(QStringLiteral("waveformFrequencyLowColor"),
                      lowColor_.name(QColor::HexRgb));
    settings.setValue(QStringLiteral("waveformFrequencyMidColor"),
                      midColor_.name(QColor::HexRgb));
    settings.setValue(QStringLiteral("waveformFrequencyHighColor"),
                      highColor_.name(QColor::HexRgb));
    settings.setValue(QStringLiteral("waveformFrequencyUnplayedOpacity"),
                      unplayedOpacity_);
    settings.remove(QStringLiteral("waveformSpectralPalette"));
    settings.remove(QStringLiteral("waveformSpectralUnplayedOpacity"));
}
