#include "theme_manager.hpp"

#include "settings_controller.hpp"

#include <QEvent>
#include <QGuiApplication>
#include <QStyleHints>
#include <QtMath>

#include <tuple>

namespace {

constexpr qreal kButtonContrast = 4.5;

qreal linearChannel(const qreal channel)
{
    return channel <= 0.04045
        ? channel / 12.92
        : qPow((channel + 0.055) / 1.055, 2.4);
}

qreal relativeLuminance(const QColor& color)
{
    const QColor rgb = color.toRgb();
    return 0.2126 * linearChannel(rgb.redF())
        + 0.7152 * linearChannel(rgb.greenF())
        + 0.0722 * linearChannel(rgb.blueF());
}

qreal contrastRatio(const QColor& first, const QColor& second)
{
    const qreal firstLuminance = relativeLuminance(first);
    const qreal secondLuminance = relativeLuminance(second);
    return (qMax(firstLuminance, secondLuminance) + 0.05)
        / (qMin(firstLuminance, secondLuminance) + 0.05);
}

QColor readableForeground(const QColor& background)
{
    const QColor black(Qt::black);
    const QColor white(Qt::white);
    return contrastRatio(black, background) >= contrastRatio(white, background)
        ? black
        : white;
}

QColor normalizedSeed(const QColor& requested)
{
    QColor seed = requested.isValid() ? requested.toRgb()
                                      : ThemeManager::defaultSeed();
    seed.setAlpha(255);
    return seed;
}

QColor tone(const QColor& seed, const int lightness,
            const qreal saturationScale = 1.0)
{
    const QColor hsl = seed.toHsl();
    const int saturation = hsl.hslSaturation() < 0
        ? 0
        : qRound(hsl.hslSaturation() * saturationScale);
    QColor result;
    result.setHsl(hsl.hslHue(), qBound(0, saturation, 255),
                  qBound(0, lightness, 255));
    return result.toRgb();
}

QColor contrastTone(const QColor& seed, const qreal saturationScale,
                    const int start, const int direction,
                    const QList<QColor>& backgrounds, const qreal target)
{
    // Leave a small quantization margin: QColor stores 8-bit channels while
    // contrast math operates on floating point channel values.
    const qreal required = target + 0.05;
    for (int lightness = start; lightness >= 0 && lightness <= 255;
         lightness += direction) {
        const QColor candidate = tone(seed, lightness, saturationScale);
        bool readable = true;
        for (const QColor& background : backgrounds) {
            if (contrastRatio(candidate, background) < required) {
                readable = false;
                break;
            }
        }
        if (readable) {
            return candidate;
        }
    }
    return tone(seed, direction < 0 ? 0 : 255, saturationScale);
}

QColor visibleTone(const QColor& seed, const QColor& background,
                   const qreal target)
{
    if (contrastRatio(seed, background) >= target + 0.05) {
        return seed;
    }
    const bool lightBackground = relativeLuminance(background) > 0.5;
    return contrastTone(seed, 1.0, seed.toHsl().lightness(),
                        lightBackground ? -1 : 1, {background}, target);
}

QColor resolveColorChoice(const int mode, const QString& presetId,
                          const QString& customColor)
{
    if (mode == 0) {
        return ThemeManager::defaultSeed();
    }
    if (mode == 1) {
        const QList<ThemeManager::Preset> presets = ThemeManager::presets();
        for (const ThemeManager::Preset& preset : presets) {
            if (preset.id == presetId) {
                return preset.seed;
            }
        }
        return ThemeManager::defaultSeed();
    }

    const QColor custom(customColor);
    return custom.isValid() ? custom : ThemeManager::defaultSeed();
}

ThemeManager::Preferences preferencesFromSettings(
    const SettingsController& settings)
{
    const int mode = settings.accentMode();
    return {
        static_cast<ThemeManager::AppearanceMode>(settings.themeMode()),
        mode == 0 ? ThemeManager::SkinMode::Default
                  : ThemeManager::SkinMode::Generated,
        resolveColorChoice(mode, settings.accentPreset(),
                           settings.accentCustomColor())};
}

QColor stateTone(const QColor& base, const QColor& foreground, const int amount)
{
    const QColor hsl = base.toHsl();
    const bool darkForeground = relativeLuminance(foreground) < 0.5;
    const QColor preferred = tone(
        base, hsl.lightness() + (darkForeground ? amount : -amount));
    if (preferred != base
        && contrastRatio(preferred, foreground) >= kButtonContrast) {
        return preferred;
    }
    const QColor alternate = tone(
        base, hsl.lightness() + (darkForeground ? -amount : amount));
    if (alternate != base
        && contrastRatio(alternate, foreground) >= kButtonContrast) {
        return alternate;
    }
    const int direction = darkForeground ? 1 : -1;
    return contrastTone(base, 1.0, hsl.lightness() + direction, direction,
                        {foreground}, kButtonContrast);
}

QColor softTone(const QColor& color, const qreal alpha)
{
    QColor result = color;
    result.setAlphaF(alpha);
    return result;
}

ThemePalette calculatePalette(const ThemeManager::Preferences& preferences,
                              const ThemeManager::AppearanceMode appearance)
{
    const bool dark = appearance == ThemeManager::AppearanceMode::Dark;
    ThemePalette palette;
    if (dark) {
        palette.success = QColor(QStringLiteral("#4CCD78"));
        palette.warning = QColor(QStringLiteral("#F2B84B"));
        palette.error = QColor(QStringLiteral("#FF625C"));
        palette.danger = QColor(QStringLiteral("#FF625C"));
        palette.recording = QColor(QStringLiteral("#FF4D5E"));
        palette.critical = QColor(QStringLiteral("#FF625C"));
    } else {
        palette.success = QColor(QStringLiteral("#208A4A"));
        palette.warning = QColor(QStringLiteral("#9B6500"));
        palette.error = QColor(QStringLiteral("#C93632"));
        palette.danger = QColor(QStringLiteral("#C93632"));
        palette.recording = QColor(QStringLiteral("#C7253E"));
        palette.critical = QColor(QStringLiteral("#C93632"));
    }

    const QColor seed = normalizedSeed(preferences.skinSeed);
    if (preferences.skinMode == ThemeManager::SkinMode::Generated) {
        if (dark) {
            palette.background = tone(seed, 16, 0.22);
            palette.surface = tone(seed, 24, 0.22);
            palette.surfaceElevated = tone(seed, 34, 0.22);
            palette.surfaceHover = tone(seed, 46, 0.22);
            palette.surfacePressed = tone(seed, 60, 0.22);
            const QList<QColor> surfaces = {palette.background, palette.surface};
            palette.textPrimary = contrastTone(seed, 0.14, 215, 1, surfaces, 7.0);
            palette.textSecondary = contrastTone(seed, 0.18, 170, 1, surfaces, 4.5);
            palette.textTertiary = contrastTone(seed, 0.22, 130, 1, surfaces, 3.0);
            palette.textDisabled = tone(seed, 110, 0.20);
            palette.divider = tone(seed, 50, 0.22);
            palette.border = tone(seed, 64, 0.22);
            palette.borderStrong = contrastTone(
                seed, 0.25, 100, 1, {palette.surface}, 3.0);
            palette.disabled = tone(seed, 68, 0.18);
        } else {
            palette.surface = tone(seed, 250, 0.22);
            palette.background = tone(seed, 246, 0.22);
            palette.surfaceElevated = tone(seed, 238, 0.22);
            palette.surfaceHover = tone(seed, 226, 0.22);
            palette.surfacePressed = tone(seed, 212, 0.22);
            const QList<QColor> surfaces = {palette.background, palette.surface};
            palette.textPrimary = contrastTone(seed, 0.14, 40, -1, surfaces, 7.0);
            palette.textSecondary = contrastTone(seed, 0.18, 90, -1, surfaces, 4.5);
            palette.textTertiary = contrastTone(seed, 0.22, 120, -1, surfaces, 3.0);
            palette.textDisabled = tone(seed, 145, 0.20);
            palette.divider = tone(seed, 220, 0.22);
            palette.border = tone(seed, 205, 0.22);
            palette.borderStrong = contrastTone(
                seed, 0.25, 155, -1, {palette.surface}, 3.0);
            palette.disabled = tone(seed, 218, 0.18);
        }
        palette.accent = seed;
        palette.highlight = visibleTone(seed, palette.surface, 3.0);
        palette.focus = visibleTone(seed, palette.surface, 3.0);
        palette.currentTrackSurface = softTone(palette.focus, 0.28);
    } else {
        if (dark) {
            palette.background = QColor(QStringLiteral("#101114"));
            palette.surface = QColor(QStringLiteral("#17181B"));
            palette.surfaceElevated = QColor(QStringLiteral("#1D1F23"));
            palette.surfaceHover = QColor(QStringLiteral("#24262B"));
            palette.surfacePressed = QColor(QStringLiteral("#2C2F35"));
            palette.textPrimary = QColor(QStringLiteral("#F5F7FA"));
            palette.textSecondary = QColor(QStringLiteral("#C9CDD4"));
            palette.textTertiary = QColor(QStringLiteral("#9DA3AD"));
            palette.textDisabled = QColor(QStringLiteral("#747A84"));
            palette.border = QColor(QStringLiteral("#30333A"));
            palette.borderStrong = QColor(QStringLiteral("#666C76"));
            palette.divider = QColor(QStringLiteral("#282B30"));
            palette.disabled = QColor(QStringLiteral("#3A3D44"));
        } else {
            palette.background = QColor(QStringLiteral("#F5F5F7"));
            palette.surface = QColor(QStringLiteral("#FFFFFF"));
            palette.surfaceElevated = QColor(QStringLiteral("#F0F1F3"));
            palette.surfaceHover = QColor(QStringLiteral("#E7E8EC"));
            palette.surfacePressed = QColor(QStringLiteral("#DCDDE1"));
            palette.textPrimary = QColor(QStringLiteral("#17181B"));
            palette.textSecondary = QColor(QStringLiteral("#545862"));
            palette.textTertiary = QColor(QStringLiteral("#777D88"));
            palette.textDisabled = QColor(QStringLiteral("#9AA0AA"));
            palette.border = QColor(QStringLiteral("#D7D9DE"));
            palette.borderStrong = QColor(QStringLiteral("#858B96"));
            palette.divider = QColor(QStringLiteral("#E5E6EA"));
            palette.disabled = QColor(QStringLiteral("#D9DBE0"));
        }
        palette.accent = QColor(QStringLiteral("#007AFF"));
        palette.highlight = QColor(QStringLiteral("#007AFF"));
        palette.focus = palette.accent;
        palette.currentTrackSurface = QColor(143, 87, 201, 87);
    }

    palette.accentText = readableForeground(palette.accent);
    palette.accentHover = stateTone(palette.accent, palette.accentText, 14);
    palette.accentPressed = stateTone(palette.accent, palette.accentText, 28);
    palette.accentSoft = softTone(palette.accent, dark ? 0.22 : 0.12);
    palette.highlightText = readableForeground(palette.highlight);
    palette.highlightHover = stateTone(
        palette.highlight, palette.highlightText, 14);
    palette.highlightPressed = stateTone(
        palette.highlight, palette.highlightText, 28);
    palette.highlightSoft = softTone(palette.highlight, dark ? 0.20 : 0.10);
    return palette;
}

} // namespace

bool ThemePalette::operator==(const ThemePalette& other) const
{
    return std::tie(background, surface, surfaceElevated, surfaceHover,
                    surfacePressed, textPrimary, textSecondary, textTertiary,
                    textDisabled, border, borderStrong, divider, disabled, accent,
                    accentHover, accentPressed, accentSoft, accentText, highlight,
                    highlightHover, highlightPressed, highlightSoft, highlightText,
                    focus, currentTrackSurface, success, warning, error, danger,
                    recording, critical)
        == std::tie(other.background, other.surface, other.surfaceElevated,
                    other.surfaceHover, other.surfacePressed, other.textPrimary,
                    other.textSecondary, other.textTertiary, other.textDisabled,
                    other.border, other.borderStrong, other.divider, other.disabled,
                    other.accent, other.accentHover, other.accentPressed,
                    other.accentSoft, other.accentText, other.highlight,
                     other.highlightHover, other.highlightPressed,
                     other.highlightSoft, other.highlightText, other.focus,
                     other.currentTrackSurface, other.success, other.warning,
                     other.error, other.danger, other.recording, other.critical);
}

ThemeManager::ThemeManager(QGuiApplication& application, QObject* parent)
    : QObject(parent)
    , application_(application)
    , systemWindowColor_(application.palette().color(QPalette::Window))
{
    application_.installEventFilter(this);
    systemColorScheme_ = application_.styleHints()->colorScheme();
    connect(application_.styleHints(), &QStyleHints::colorSchemeChanged, this,
            &ThemeManager::handleSystemColorSchemeChanged);
    refreshPalette();
}

QColor ThemeManager::defaultSeed()
{
    return QColor(QStringLiteral("#D27722"));
}

QList<ThemeManager::Preset> ThemeManager::presets()
{
    return {
        {QStringLiteral("systemBlue"), QColor(QStringLiteral("#007AFF"))},
        {QStringLiteral("indigo"), QColor(QStringLiteral("#5856D6"))},
        {QStringLiteral("purple"), QColor(QStringLiteral("#AF52DE"))},
        {QStringLiteral("pink"), QColor(QStringLiteral("#FF2D55"))},
        {QStringLiteral("red"), QColor(QStringLiteral("#FF3B30"))},
        {QStringLiteral("orange"), QColor(QStringLiteral("#FF9500"))},
        {QStringLiteral("gold"), QColor(QStringLiteral("#FFCC00"))},
        {QStringLiteral("green"), QColor(QStringLiteral("#34C759"))},
        {QStringLiteral("teal"), QColor(QStringLiteral("#30B0C7"))},
        {QStringLiteral("cyan"), QColor(QStringLiteral("#32ADE6"))},
    };
}

void ThemeManager::applyPreferences(const Preferences& preferences)
{
    preferences_ = preferences;
    switch (preferences_.appearanceMode) {
    case AppearanceMode::Dark:
    case AppearanceMode::Light:
    case AppearanceMode::System:
        break;
    default:
        preferences_.appearanceMode = AppearanceMode::System;
        break;
    }
    if (preferences_.skinMode != SkinMode::Generated) {
        preferences_.skinMode = SkinMode::Default;
    }
    preferences_.skinSeed = normalizedSeed(preferences_.skinSeed);
    refreshPalette();
}

bool ThemeManager::isLight() const
{
    return effectiveAppearance() == AppearanceMode::Light;
}

bool ThemeManager::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == &application_ && event->type() == QEvent::ApplicationPaletteChange
        && !applyingApplicationPalette_) {
        systemWindowColor_ = application_.palette().color(QPalette::Window);
        if (preferences_.appearanceMode == AppearanceMode::System) {
            refreshPalette();
        }
    }
    return QObject::eventFilter(watched, event);
}

void ThemeManager::handleSystemColorSchemeChanged(const Qt::ColorScheme scheme)
{
    if (systemColorScheme_ == scheme) {
        return;
    }
    systemColorScheme_ = scheme;
    if (preferences_.appearanceMode == AppearanceMode::System) {
        refreshPalette();
    }
}

ThemeManager::AppearanceMode ThemeManager::effectiveAppearance() const
{
    if (preferences_.appearanceMode != AppearanceMode::System) {
        return preferences_.appearanceMode;
    }
    switch (systemColorScheme_) {
    case Qt::ColorScheme::Light:
        return AppearanceMode::Light;
    case Qt::ColorScheme::Dark:
        return AppearanceMode::Dark;
    case Qt::ColorScheme::Unknown:
        return relativeLuminance(systemWindowColor_) > 0.5
            ? AppearanceMode::Light
            : AppearanceMode::Dark;
    }
    return AppearanceMode::Light;
}

void ThemeManager::refreshPalette()
{
    const ThemePalette next = calculatePalette(preferences_, effectiveAppearance());
    if (next == palette_) {
        return;
    }
    palette_ = next;
    applyApplicationPalette(palette_);
    emit paletteChanged();
}

void ThemeManager::applyApplicationPalette(const ThemePalette& palette)
{
    QPalette applicationPalette;
    applicationPalette.setColor(QPalette::All, QPalette::Window, palette.background);
    applicationPalette.setColor(QPalette::All, QPalette::WindowText, palette.textPrimary);
    applicationPalette.setColor(QPalette::All, QPalette::Base, palette.surface);
    applicationPalette.setColor(QPalette::All, QPalette::AlternateBase,
                                palette.surfaceElevated);
    applicationPalette.setColor(QPalette::All, QPalette::Text, palette.textPrimary);
    applicationPalette.setColor(QPalette::All, QPalette::Button, palette.surfaceElevated);
    applicationPalette.setColor(QPalette::All, QPalette::ButtonText, palette.textPrimary);
    applicationPalette.setColor(QPalette::All, QPalette::Highlight, palette.highlight);
    applicationPalette.setColor(QPalette::All, QPalette::HighlightedText,
                                palette.highlightText);
    applicationPalette.setColor(QPalette::All, QPalette::PlaceholderText,
                                palette.textTertiary);
    applicationPalette.setColor(QPalette::All, QPalette::ToolTipBase,
                                palette.surfaceElevated);
    applicationPalette.setColor(QPalette::All, QPalette::ToolTipText,
                                palette.textPrimary);
    applicationPalette.setColor(QPalette::All, QPalette::Link, palette.accent);
    applicationPalette.setColor(QPalette::All, QPalette::LinkVisited,
                                palette.accentPressed);
    applicationPalette.setColor(QPalette::All, QPalette::BrightText, palette.error);
    applicationPalette.setColor(QPalette::Disabled, QPalette::WindowText,
                                palette.textDisabled);
    applicationPalette.setColor(QPalette::Disabled, QPalette::Text, palette.textDisabled);
    applicationPalette.setColor(QPalette::Disabled, QPalette::ButtonText,
                                palette.textDisabled);
    applicationPalette.setColor(QPalette::Disabled, QPalette::Highlight,
                                palette.disabled);
    applicationPalette.setColor(QPalette::Disabled, QPalette::HighlightedText,
                                palette.textDisabled);

    applyingApplicationPalette_ = true;
    application_.setPalette(applicationPalette);
    applyingApplicationPalette_ = false;
}

ThemeSettingsSynchronizer::ThemeSettingsSynchronizer(
    ThemeManager& manager, SettingsController& settings)
    : QObject(&manager)
    , manager_(manager)
    , settings_(settings)
{
    const auto apply = [this]() { applyFromCompleteSettings(); };
    QObject::connect(&settings_, &SettingsController::themeModeChanged,
                     this, apply);
    QObject::connect(&settings_, &SettingsController::accentModeChanged,
                     this, apply);
    QObject::connect(&settings_, &SettingsController::accentPresetChanged,
                     this, apply);
    QObject::connect(&settings_, &SettingsController::accentCustomColorChanged,
                     this, apply);
    QObject::connect(&settings_, &SettingsController::highlightFollowAccentChanged,
                     this, apply);
    QObject::connect(&settings_, &SettingsController::highlightModeChanged,
                     this, apply);
    QObject::connect(&settings_, &SettingsController::highlightPresetChanged,
                     this, apply);
    QObject::connect(&settings_, &SettingsController::highlightCustomColorChanged,
                     this, apply);
    applyFromCompleteSettings();
}

void ThemeSettingsSynchronizer::applyFromCompleteSettings()
{
    manager_.applyPreferences(preferencesFromSettings(settings_));
}
