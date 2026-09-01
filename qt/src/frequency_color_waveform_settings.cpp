#include "frequency_color_waveform_settings.hpp"

#include <QColor>
#include <QStringList>

#include <algorithm>
#include <cmath>

FrequencyColorWaveformSettings::FrequencyColorWaveformSettings(QObject* parent)
    : QObject(parent)
{
}

QVariantList FrequencyColorWaveformSettings::defaultPalette()
{
    return {QStringLiteral("#123ecf"), QStringLiteral("#00a7ba"),
            QStringLiteral("#00a76f"), QStringLiteral("#62bb39"),
            QStringLiteral("#d8dc2f"), QStringLiteral("#ffad22"),
            QStringLiteral("#ff611f"), QStringLiteral("#e82718")};
}

QVariantList FrequencyColorWaveformSettings::normalizedPalette(
    const QVariantList& palette)
{
    if (palette.size() != 8) {
        return {};
    }
    QVariantList result;
    result.reserve(8);
    for (const QVariant& value : palette) {
        const QColor color(value.toString());
        if (!color.isValid()) {
            return {};
        }
        result.append(color.name(QColor::HexRgb));
    }
    return result;
}

QVariantList FrequencyColorWaveformSettings::palette() const { return palette_; }

void FrequencyColorWaveformSettings::setPalette(const QVariantList& palette)
{
    const QVariantList normalized = normalizedPalette(palette);
    if (normalized.isEmpty() || normalized == palette_) {
        return;
    }
    palette_ = normalized;
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

void FrequencyColorWaveformSettings::setPaletteColor(int index,
                                                       const QString& color)
{
    if (index < 0 || index >= palette_.size()) {
        return;
    }
    const QColor parsed(color);
    if (!parsed.isValid()) {
        return;
    }
    QVariantList updated = palette_;
    updated[index] = parsed.name(QColor::HexRgb);
    setPalette(updated);
}

void FrequencyColorWaveformSettings::resetToDefault()
{
    const QVariantList defaults = defaultPalette();
    const bool changedState = palette_ != defaults
        || !qFuzzyCompare(unplayedOpacity_ + 1.0, 1.88);
    palette_ = defaults;
    unplayedOpacity_ = 0.88;
    if (changedState) {
        emit changed();
    }
}

void FrequencyColorWaveformSettings::load(QSettings& settings)
{
    QVariantList loaded;
    const QStringList stored = settings.value(
        QStringLiteral("waveformSpectralPalette")).toStringList();
    for (const QString& color : stored) {
        loaded.append(color);
    }
    const QVariantList normalized = normalizedPalette(loaded);
    palette_ = normalized.isEmpty() ? defaultPalette() : normalized;
    const double storedOpacity = settings.value(
        QStringLiteral("waveformSpectralUnplayedOpacity"), 0.88).toDouble();
    unplayedOpacity_ = std::clamp(
        std::isfinite(storedOpacity) ? storedOpacity : 0.88, 0.60, 1.0);
}

void FrequencyColorWaveformSettings::save(QSettings& settings) const
{
    QStringList colors;
    colors.reserve(palette_.size());
    for (const QVariant& color : palette_) {
        colors.append(color.toString());
    }
    settings.setValue(QStringLiteral("waveformSpectralPalette"), colors);
    settings.setValue(QStringLiteral("waveformSpectralUnplayedOpacity"),
                      unplayedOpacity_);
}
