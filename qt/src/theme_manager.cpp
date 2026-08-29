#include "theme_manager.hpp"

#include "settings_controller.hpp"

#include <QEvent>
#include <QGuiApplication>
#include <QHash>
#include <QStyleHints>
#include <QVariantMap>
#include <QtMath>

#include <algorithm>
#include <tuple>
#include <utility>

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

ThemeManager::SkinStops normalizedSkinStops(
    const ThemeManager::SkinStops& requested)
{
    for (const QColor& stop : requested) {
        if (!stop.isValid() || stop.alpha() != 255) {
            const QColor seed = ThemeManager::defaultSeed();
            return {seed, seed, seed};
        }
    }
    return {normalizedSeed(requested[0]), normalizedSeed(requested[1]),
            normalizedSeed(requested[2])};
}

QColor withSoftTone(const QColor& input, const bool dark,
                    const double lightness)
{
    const QColor hsl = normalizedSeed(input).toHsl();
    const bool achromatic = hsl.hslSaturationF() < 0.01
        || hsl.hslHueF() < 0.0;
    const double sourceSaturation = achromatic ? 0.0 : hsl.hslSaturationF();
    const double saturation = achromatic ? 0.0
        : std::clamp(sourceSaturation * 0.48,
                     dark ? 0.28 : 0.16,
                     dark ? 0.50 : 0.36);
    return QColor::fromHslF(achromatic ? 0.0 : hsl.hslHueF(), saturation,
                            std::clamp(lightness, 0.0, 1.0));
}

ThemeManager::SkinStops expandedSolidStops(const QColor& seed, const bool dark)
{
    return dark
        ? ThemeManager::SkinStops{withSoftTone(seed, true, 0.09),
                                  withSoftTone(seed, true, 0.15),
                                  withSoftTone(seed, true, 0.11)}
        : ThemeManager::SkinStops{withSoftTone(seed, false, 0.94),
                                  withSoftTone(seed, false, 0.88),
                                  withSoftTone(seed, false, 0.96)};
}

ThemeManager::SkinStops softenedGradientStops(
    const ThemeManager::SkinStops& stops, const bool dark)
{
    return dark
        ? ThemeManager::SkinStops{withSoftTone(stops[0], true, 0.09),
                                  withSoftTone(stops[1], true, 0.15),
                                  withSoftTone(stops[2], true, 0.11)}
        : ThemeManager::SkinStops{withSoftTone(stops[0], false, 0.94),
                                  withSoftTone(stops[1], false, 0.88),
                                  withSoftTone(stops[2], false, 0.96)};
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

ThemeManager::Preferences preferencesFromSettings(
    const SettingsController& settings)
{
    const int mode = settings.skinColorMode();
    const auto appearance =
        static_cast<ThemeManager::AppearanceMode>(settings.themeMode());
    const QColor fallback = ThemeManager::defaultSeed();
    const ThemeManager::SkinStops defaultStops{fallback, fallback, fallback};
    if (mode == 1) {
        const QList<ThemeManager::Preset> presets = ThemeManager::presets();
        const auto preset = std::find_if(
            presets.cbegin(), presets.cend(),
            [&settings](const ThemeManager::Preset& value) {
                return value.id == settings.skinPreset();
            });
        if (preset != presets.cend()) {
            return {appearance, ThemeManager::SkinMode::Generated,
                    ThemeManager::SkinKind::Gradient, preset->stops};
        }
        return {appearance, ThemeManager::SkinMode::Default,
                ThemeManager::SkinKind::Solid, defaultStops};
    }
    if (mode == 2) {
        const QColor start(settings.skinCustomColor());
        if (settings.skinCustomKind() == 1) {
            return {appearance, ThemeManager::SkinMode::Generated,
                    ThemeManager::SkinKind::Gradient,
                    {start, QColor(settings.skinCustomColorMiddle()),
                     QColor(settings.skinCustomColorEnd())}};
        }
        return {appearance, ThemeManager::SkinMode::Generated,
                ThemeManager::SkinKind::Solid, {start, start, start}};
    }
    return {appearance, ThemeManager::SkinMode::Default,
            ThemeManager::SkinKind::Solid, defaultStops};
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

QColor alphaColor(const QColor& color, const qreal alpha)
{
    QColor result = color;
    result.setAlphaF(alpha);
    return result;
}

QColor softTone(const QColor& color, const qreal alpha)
{
    return alphaColor(color, alpha);
}

QColor compositeColor(const QColor& over, const QColor& under)
{
    const qreal alpha = over.alphaF();
    return QColor::fromRgbF(
        over.redF() * alpha + under.redF() * (1.0 - alpha),
        over.greenF() * alpha + under.greenF() * (1.0 - alpha),
        over.blueF() * alpha + under.blueF() * (1.0 - alpha));
}

QList<QColor> compositeSurfaces(const ThemePalette& palette)
{
    QList<QColor> surfaces;
    for (const QColor& backdrop : {palette.backdropStart,
                                   palette.backdropMiddle,
                                   palette.backdropEnd}) {
        surfaces << compositeColor(palette.glassSurface, backdrop)
                 << compositeColor(palette.glassSurfaceElevated, backdrop)
                 << compositeColor(palette.glassSurfaceHover, backdrop)
                 << compositeColor(palette.glassSurfacePressed, backdrop);
    }
    return surfaces;
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

    const ThemeManager::SkinStops normalizedStops =
        normalizedSkinStops(preferences.skinStops);
    if (preferences.skinMode == ThemeManager::SkinMode::Generated) {
        const ThemeManager::SkinStops backdrop =
            preferences.skinKind == ThemeManager::SkinKind::Solid
            ? expandedSolidStops(normalizedStops.front(), dark)
            : softenedGradientStops(normalizedStops, dark);
        palette.backdropStart = backdrop[0];
        palette.backdropMiddle = backdrop[1];
        palette.backdropEnd = backdrop[2];

        const QColor representative = normalizedStops[1];
        QColor backgroundFamily;
        QColor surfaceFamily;
        QColor elevatedFamily;
        QColor hoverFamily;
        QColor pressedFamily;
        if (dark) {
            backgroundFamily = tone(representative, 16, 0.22);
            surfaceFamily = tone(representative, 24, 0.22);
            elevatedFamily = tone(representative, 34, 0.22);
            hoverFamily = tone(representative, 46, 0.22);
            pressedFamily = tone(representative, 60, 0.22);
            palette.textDisabled = tone(representative, 110, 0.20);
            palette.divider = tone(representative, 50, 0.22);
            palette.border = tone(representative, 64, 0.22);
            palette.disabled = tone(representative, 68, 0.18);
        } else {
            backgroundFamily = tone(representative, 246, 0.22);
            surfaceFamily = tone(representative, 250, 0.22);
            elevatedFamily = tone(representative, 238, 0.22);
            hoverFamily = tone(representative, 226, 0.22);
            pressedFamily = tone(representative, 212, 0.22);
            palette.textDisabled = tone(representative, 145, 0.20);
            palette.divider = tone(representative, 220, 0.22);
            palette.border = tone(representative, 205, 0.22);
            palette.disabled = tone(representative, 218, 0.18);
        }

        palette.glassSurface = alphaColor(surfaceFamily, dark ? 0.72 : 0.68);
        palette.glassSurfaceElevated = alphaColor(
            elevatedFamily, dark ? 0.78 : 0.74);
        palette.glassSurfaceHover = alphaColor(
            hoverFamily, dark ? 0.82 : 0.78);
        palette.glassSurfacePressed = alphaColor(
            pressedFamily, dark ? 0.86 : 0.82);
        palette.background = compositeColor(
            alphaColor(backgroundFamily, dark ? 0.72 : 0.68),
            palette.backdropMiddle);
        palette.surface = compositeColor(
            palette.glassSurface, palette.backdropMiddle);
        palette.surfaceElevated = compositeColor(
            palette.glassSurfaceElevated, palette.backdropMiddle);
        palette.surfaceHover = compositeColor(
            palette.glassSurfaceHover, palette.backdropMiddle);
        palette.surfacePressed = compositeColor(
            palette.glassSurfacePressed, palette.backdropMiddle);

        const QList<QColor> readableSurfaces = compositeSurfaces(palette);
        palette.textPrimary = contrastTone(
            representative, 0.12, dark ? 245 : 20, dark ? 1 : -1,
            readableSurfaces, 7.0);
        palette.textSecondary = contrastTone(
            representative, 0.16, dark ? 210 : 55, dark ? 1 : -1,
            readableSurfaces, 4.5);
        palette.textTertiary = contrastTone(
            representative, 0.18, dark ? 175 : 90, dark ? 1 : -1,
            readableSurfaces, 3.0);
        palette.borderStrong = contrastTone(
            representative, 0.25, dark ? 100 : 155, dark ? 1 : -1,
            readableSurfaces, 3.0);
        palette.glassBorder = alphaColor(
            palette.borderStrong, dark ? 0.52 : 0.42);
        palette.glassDivider = alphaColor(
            palette.border, dark ? 0.42 : 0.34);
        palette.glassInnerHighlight = QColor(255, 255, 255, dark ? 18 : 82);
        palette.accent = contrastTone(
            representative, 0.72, representative.toHsl().lightness(),
            dark ? 1 : -1, readableSurfaces, kButtonContrast);
        palette.highlight = palette.accent;
        palette.focus = palette.accent;
        palette.currentTrackSurface = softTone(palette.accent, 0.28);
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
        palette.backdropStart = palette.background;
        palette.backdropMiddle = palette.background;
        palette.backdropEnd = palette.background;
        palette.glassSurface = palette.surface;
        palette.glassSurfaceElevated = palette.surfaceElevated;
        palette.glassSurfaceHover = palette.surfaceHover;
        palette.glassSurfacePressed = palette.surfacePressed;
        palette.glassBorder = palette.border;
        palette.glassDivider = palette.divider;
        palette.glassInnerHighlight = palette.border;
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
    return std::tie(background, backdropStart, backdropMiddle, backdropEnd,
                    surface, surfaceElevated, surfaceHover, surfacePressed,
                    textPrimary, textSecondary, textTertiary, textDisabled,
                    border, borderStrong, divider, disabled, accent, accentHover,
                    accentPressed, accentSoft, accentText, highlight,
                    highlightHover, highlightPressed, highlightSoft,
                    highlightText, focus, currentTrackSurface, glassSurface,
                    glassSurfaceElevated, glassSurfaceHover, glassSurfacePressed,
                    glassBorder, glassDivider, glassInnerHighlight, success,
                    warning, error, danger, recording, critical)
        == std::tie(other.background, other.backdropStart, other.backdropMiddle,
                    other.backdropEnd, other.surface, other.surfaceElevated,
                    other.surfaceHover, other.surfacePressed, other.textPrimary,
                    other.textSecondary, other.textTertiary, other.textDisabled,
                    other.border, other.borderStrong, other.divider,
                    other.disabled, other.accent, other.accentHover,
                    other.accentPressed, other.accentSoft, other.accentText,
                    other.highlight, other.highlightHover, other.highlightPressed,
                    other.highlightSoft, other.highlightText, other.focus,
                    other.currentTrackSurface, other.glassSurface,
                    other.glassSurfaceElevated, other.glassSurfaceHover,
                    other.glassSurfacePressed, other.glassBorder,
                    other.glassDivider, other.glassInnerHighlight, other.success,
                    other.warning, other.error, other.danger, other.recording,
                    other.critical);
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
        {QStringLiteral("aurora"), {QColor(QStringLiteral("#73A6FF")), QColor(QStringLiteral("#A98BFF")), QColor(QStringLiteral("#F0A8D8"))}},
        {QStringLiteral("seaGlass"), {QColor(QStringLiteral("#71D9D0")), QColor(QStringLiteral("#82C9F4")), QColor(QStringLiteral("#A7B7FF"))}},
        {QStringLiteral("sunset"), {QColor(QStringLiteral("#F49BC2")), QColor(QStringLiteral("#FF9B86")), QColor(QStringLiteral("#FFC97A"))}},
        {QStringLiteral("lavenderMist"), {QColor(QStringLiteral("#8295F2")), QColor(QStringLiteral("#B89BE8")), QColor(QStringLiteral("#E8B7D5"))}},
        {QStringLiteral("morningGlow"), {QColor(QStringLiteral("#8EDFCB")), QColor(QStringLiteral("#D4E9C2")), QColor(QStringLiteral("#FFD995"))}},
    };
}

std::optional<QColor> ThemeManager::legacyPresetSeed(const QString& id)
{
    static const QHash<QString, QColor> legacy{
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
    const auto it = legacy.constFind(id);
    return it == legacy.cend() ? std::nullopt
                               : std::optional<QColor>(*it);
}

QVariantList ThemeManager::recommendedPresets() const
{
    const QList<Preset> values = presets();
    QVariantList result;
    result.reserve(values.size());
    for (const Preset& preset : values) {
        result.append(QVariantMap{
            {QStringLiteral("id"), preset.id},
            {QStringLiteral("start"), preset.stops[0]},
            {QStringLiteral("middle"), preset.stops[1]},
            {QStringLiteral("end"), preset.stops[2]},
        });
    }
    return result;
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
    if (preferences_.skinKind != SkinKind::Gradient) {
        preferences_.skinKind = SkinKind::Solid;
    }
    preferences_.skinStops = normalizedSkinStops(preferences_.skinStops);
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
    QObject::connect(&settings_, &SettingsController::skinConfigurationChanged,
                     this, apply);
    applyFromCompleteSettings();
}

void ThemeSettingsSynchronizer::applyFromCompleteSettings()
{
    manager_.applyPreferences(preferencesFromSettings(settings_));
}
