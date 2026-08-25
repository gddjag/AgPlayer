#include "theme_manager.hpp"

#include <QEvent>
#include <QGuiApplication>
#include <QStyleHints>
#include <QtMath>

#include <tuple>

namespace {

constexpr qreal kActionContrast = 4.5;

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

QColor withHslLightness(const QColor& color, const int lightness)
{
    QColor hsl = color.toHsl();
    hsl.setHsl(hsl.hslHue(), hsl.hslSaturation(), qBound(0, lightness, 255),
               color.alpha());
    return hsl.toRgb();
}

QColor resolvedSeed(const QColor& requested, const QColor& surface)
{
    QColor seed = requested.isValid() ? requested.toRgb()
                                      : ThemeManager::defaultSeed();
    seed.setAlpha(255);
    if (contrastRatio(seed, surface) >= kActionContrast) {
        return seed;
    }

    const QColor hsl = seed.toHsl();
    const int step = relativeLuminance(surface) > 0.5 ? -1 : 1;
    for (int lightness = hsl.lightness(); lightness >= 0 && lightness <= 255;
         lightness += step) {
        const QColor candidate = withHslLightness(seed, lightness);
        if (contrastRatio(candidate, surface) >= kActionContrast) {
            return candidate;
        }
    }
    return withHslLightness(seed, step < 0 ? 0 : 255);
}

QColor stateTone(const QColor& base, const QColor& surface, const bool dark,
                 const int amount)
{
    const QColor hsl = base.toHsl();
    const int preferredLightness = hsl.lightness() + (dark ? amount : -amount);
    const QColor preferred = withHslLightness(base, preferredLightness);
    if (preferred != base && contrastRatio(preferred, surface) >= kActionContrast) {
        return preferred;
    }
    const QColor alternate = withHslLightness(
        base, hsl.lightness() + (dark ? -amount : amount));
    if (alternate != base && contrastRatio(alternate, surface) >= kActionContrast) {
        return alternate;
    }
    return base;
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
        palette.success = QColor(QStringLiteral("#4CCD78"));
        palette.warning = QColor(QStringLiteral("#F2B84B"));
        palette.error = QColor(QStringLiteral("#FF625C"));
        palette.danger = QColor(QStringLiteral("#FF625C"));
        palette.recording = QColor(QStringLiteral("#FF4D5E"));
        palette.critical = QColor(QStringLiteral("#FF625C"));
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
        palette.success = QColor(QStringLiteral("#208A4A"));
        palette.warning = QColor(QStringLiteral("#9B6500"));
        palette.error = QColor(QStringLiteral("#C93632"));
        palette.danger = QColor(QStringLiteral("#C93632"));
        palette.recording = QColor(QStringLiteral("#C7253E"));
        palette.critical = QColor(QStringLiteral("#C93632"));
    }

    palette.accent = resolvedSeed(preferences.accentSeed, palette.surface);
    const QColor highlightSeed = preferences.highlightFollowsAccent
        ? preferences.accentSeed
        : preferences.highlightSeed;
    palette.highlight = resolvedSeed(highlightSeed, palette.surface);
    palette.accentHover = stateTone(palette.accent, palette.surface, dark, 15);
    palette.accentPressed = stateTone(palette.accent, palette.surface, dark, 31);
    palette.accentSoft = softTone(palette.accent, dark ? 0.22 : 0.12);
    palette.accentText = readableForeground(palette.accent);
    palette.highlightHover = stateTone(palette.highlight, palette.surface, dark, 15);
    palette.highlightPressed = stateTone(palette.highlight, palette.surface, dark, 31);
    palette.highlightSoft = softTone(palette.highlight, dark ? 0.20 : 0.10);
    palette.highlightText = readableForeground(palette.highlight);
    palette.focus = palette.accent;
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
                    focus, success, warning, error, danger, recording, critical)
        == std::tie(other.background, other.surface, other.surfaceElevated,
                    other.surfaceHover, other.surfacePressed, other.textPrimary,
                    other.textSecondary, other.textTertiary, other.textDisabled,
                    other.border, other.borderStrong, other.divider, other.disabled,
                    other.accent, other.accentHover, other.accentPressed,
                    other.accentSoft, other.accentText, other.highlight,
                    other.highlightHover, other.highlightPressed,
                    other.highlightSoft, other.highlightText, other.focus,
                    other.success, other.warning, other.error, other.danger,
                    other.recording, other.critical);
}

ThemeManager::ThemeManager(QGuiApplication& application, QObject* parent)
    : QObject(parent)
    , application_(application)
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
    if (!preferences_.accentSeed.isValid()) {
        preferences_.accentSeed = defaultSeed();
    }
    if (!preferences_.highlightSeed.isValid()) {
        preferences_.highlightSeed = defaultSeed();
    }
    refreshPalette();
}

bool ThemeManager::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == &application_ && event->type() == QEvent::ApplicationPaletteChange
        && !applyingApplicationPalette_
        && preferences_.appearanceMode == AppearanceMode::System) {
        refreshPalette();
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
        return relativeLuminance(application_.palette().color(QPalette::Window))
                > 0.5
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
