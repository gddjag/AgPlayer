#include "theme_manager.hpp"
#include "settings_controller.hpp"

#include <QAbstractEventDispatcher>
#include <QCoreApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QMetaProperty>
#include <QSignalSpy>
#include <QSettings>
#include <QStandardPaths>
#include <QTest>
#include <QVariantMap>
#include <QtMath>

#include <type_traits>

namespace {

ThemeManager::Preferences generated(
    ThemeManager::AppearanceMode appearance,
    ThemeManager::SkinKind kind,
    ThemeManager::SkinStops stops)
{
    return {appearance, ThemeManager::SkinMode::Generated, kind, stops};
}

double luminance(const QColor& color)
{
    const auto linear = [](double channel) {
        channel /= 255.0;
        return channel <= 0.04045
            ? channel / 12.92
            : qPow((channel + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * linear(color.red()) + 0.7152 * linear(color.green())
        + 0.0722 * linear(color.blue());
}

double contrastRatio(const QColor& first, const QColor& second)
{
    const double a = luminance(first);
    const double b = luminance(second);
    return (qMax(a, b) + 0.05) / (qMin(a, b) + 0.05);
}

QColor composite(const QColor& over, const QColor& under)
{
    const double alpha = over.alphaF();
    return QColor::fromRgbF(
        over.redF() * alpha + under.redF() * (1.0 - alpha),
        over.greenF() * alpha + under.greenF() * (1.0 - alpha),
        over.blueF() * alpha + under.blueF() * (1.0 - alpha));
}

QList<QColor> glassComposites(const ThemePalette& palette)
{
    QList<QColor> values;
    for (const QColor& backdrop : {palette.backdropStart,
                                   palette.backdropMiddle,
                                   palette.backdropEnd}) {
        values << composite(palette.glassSurface, backdrop)
               << composite(palette.glassSurfaceElevated, backdrop)
               << composite(palette.glassSurfaceHover, backdrop)
               << composite(palette.glassSurfacePressed, backdrop);
    }
    return values;
}

double worstContrast(const QColor& foreground,
                     const QList<QColor>& backgrounds)
{
    double worst = 21.0;
    for (const QColor& background : backgrounds) {
        worst = qMin(worst, contrastRatio(foreground, background));
    }
    return worst;
}

double maxRgbDistance(const QList<QColor>& colors)
{
    double maximum = 0.0;
    for (qsizetype first = 0; first < colors.size(); ++first) {
        for (qsizetype second = first + 1; second < colors.size(); ++second) {
            const double red = colors.at(first).redF() - colors.at(second).redF();
            const double green = colors.at(first).greenF() - colors.at(second).greenF();
            const double blue = colors.at(first).blueF() - colors.at(second).blueF();
            maximum = qMax(maximum, qSqrt(red * red + green * green + blue * blue));
        }
    }
    return maximum;
}

QList<QColor> ordinaryColors(const ThemePalette& palette)
{
    return {palette.background, palette.surface, palette.surfaceElevated,
            palette.surfaceHover, palette.surfacePressed, palette.textPrimary,
            palette.textSecondary, palette.textTertiary, palette.textDisabled,
            palette.border, palette.borderStrong, palette.divider,
            palette.disabled, palette.accent, palette.accentHover,
            palette.accentPressed, palette.accentSoft, palette.highlight,
            palette.highlightHover, palette.highlightPressed,
            palette.highlightSoft, palette.focus, palette.currentTrackSurface};
}

void verifyGenerated(const ThemePalette& palette,
                     ThemeManager::AppearanceMode appearance)
{
    if (appearance == ThemeManager::AppearanceMode::Dark) {
        QVERIFY(luminance(palette.background) < luminance(palette.surface));
        QVERIFY(luminance(palette.surface) < luminance(palette.surfaceElevated));
        QVERIFY(luminance(palette.surfaceElevated) < luminance(palette.surfaceHover));
        QVERIFY(luminance(palette.surfaceHover) < luminance(palette.surfacePressed));
    } else {
        QVERIFY(luminance(palette.surface) > luminance(palette.background));
        QVERIFY(luminance(palette.background) > luminance(palette.surfaceElevated));
        QVERIFY(luminance(palette.surfaceElevated) > luminance(palette.surfaceHover));
        QVERIFY(luminance(palette.surfaceHover) > luminance(palette.surfacePressed));
    }
    for (const QColor& background : {palette.background, palette.surface}) {
        QVERIFY(contrastRatio(palette.textPrimary, background) >= 7.0);
        QVERIFY(contrastRatio(palette.textSecondary, background) >= 4.5);
        QVERIFY(contrastRatio(palette.textTertiary, background) >= 3.0);
    }
    QVERIFY(contrastRatio(palette.accentText, palette.accent) >= 4.5);
    QVERIFY(contrastRatio(palette.accentText, palette.accentHover) >= 4.5);
    QVERIFY(contrastRatio(palette.accentText, palette.accentPressed) >= 4.5);
    QVERIFY(contrastRatio(palette.accent, palette.background) >= 4.5);
    QVERIFY(contrastRatio(palette.accent, palette.surface) >= 4.5);
    QVERIFY(contrastRatio(palette.highlightText, palette.highlight) >= 4.5);
    QVERIFY(contrastRatio(palette.highlightText, palette.highlightHover) >= 4.5);
    QVERIFY(contrastRatio(palette.highlightText, palette.highlightPressed) >= 4.5);
    QVERIFY(contrastRatio(palette.focus, palette.surface) >= 3.0);
    QVERIFY(contrastRatio(palette.borderStrong, palette.surface) >= 3.0);
    QVERIFY(palette.accent != palette.accentHover);
    QVERIFY(palette.accentHover != palette.accentPressed);
    QVERIFY(palette.highlight != palette.highlightHover);
    QVERIFY(palette.highlightHover != palette.highlightPressed);
    QVERIFY(palette.currentTrackSurface != palette.highlight);
}

} // namespace

class ThemeManagerTest final : public QObject {
    Q_OBJECT

private slots:
    void defaultPaletteIsExact_data();
    void defaultPaletteIsExact();
    void recommendedGradientPresetsAreStable();
    void recommendedGradientPresetsAreExposedToQml();
    void generatedSeedsMeetContract_data();
    void generatedSeedsMeetContract();
    void generatedGlassPalettesMeetContract_data();
    void generatedGlassPalettesMeetContract();
    void solidBackdropExpandsSeedHue();
    void generatedStopsDriveCompletePalette();
    void generatedSeedChangesCompleteOrdinaryPalette();
    void achromaticSeedStaysAchromatic();
    void semanticColorsKeepIdentity();
    void normalizesPreferences();
    void notifiesOnceOnlyForRealChanges();
    void refreshesSystemPaletteOncePerRealChange();
    void applicationPaletteChangeRefreshesUnknownSystemPalette();
    void unknownSystemPaletteDoesNotReadBackAppliedTheme();
    void synchronizesNativePalette();
    void synchronizerOwnsConnectionLifetime();
    void synchronizerAppliesSkinSettingsAtStartupAndDuringTransactions();
    void ownsNoTimers();
};

void ThemeManagerTest::defaultPaletteIsExact_data()
{
    QTest::addColumn<ThemeManager::AppearanceMode>("appearance");
    QTest::newRow("light") << ThemeManager::AppearanceMode::Light;
    QTest::newRow("dark") << ThemeManager::AppearanceMode::Dark;
}

void ThemeManagerTest::defaultPaletteIsExact()
{
    QFETCH(ThemeManager::AppearanceMode, appearance);
    ThemeManager manager(*qApp);
    manager.applyPreferences({appearance, ThemeManager::SkinMode::Default,
                              ThemeManager::SkinKind::Solid,
                              {QColor(QStringLiteral("#FF00FF")),
                               QColor(QStringLiteral("#FF00FF")),
                               QColor(QStringLiteral("#FF00FF"))}});
    const ThemePalette& p = manager.palette();
    if (appearance == ThemeManager::AppearanceMode::Dark) {
        QCOMPARE(p.background, QColor(QStringLiteral("#101114")));
        QCOMPARE(p.surface, QColor(QStringLiteral("#17181B")));
        QCOMPARE(p.surfaceElevated, QColor(QStringLiteral("#1D1F23")));
        QCOMPARE(p.surfaceHover, QColor(QStringLiteral("#24262B")));
        QCOMPARE(p.surfacePressed, QColor(QStringLiteral("#2C2F35")));
        QCOMPARE(p.textPrimary, QColor(QStringLiteral("#F5F7FA")));
        QCOMPARE(p.textSecondary, QColor(QStringLiteral("#C9CDD4")));
        QCOMPARE(p.textTertiary, QColor(QStringLiteral("#9DA3AD")));
        QCOMPARE(p.textDisabled, QColor(QStringLiteral("#747A84")));
        QCOMPARE(p.border, QColor(QStringLiteral("#30333A")));
        QCOMPARE(p.borderStrong, QColor(QStringLiteral("#666C76")));
        QCOMPARE(p.divider, QColor(QStringLiteral("#282B30")));
        QCOMPARE(p.disabled, QColor(QStringLiteral("#3A3D44")));
        QCOMPARE(p.success, QColor(QStringLiteral("#4CCD78")));
        QCOMPARE(p.warning, QColor(QStringLiteral("#F2B84B")));
        QCOMPARE(p.error, QColor(QStringLiteral("#FF625C")));
    } else {
        QCOMPARE(p.background, QColor(QStringLiteral("#F5F5F7")));
        QCOMPARE(p.surface, QColor(QStringLiteral("#FFFFFF")));
        QCOMPARE(p.surfaceElevated, QColor(QStringLiteral("#F0F1F3")));
        QCOMPARE(p.surfaceHover, QColor(QStringLiteral("#E7E8EC")));
        QCOMPARE(p.surfacePressed, QColor(QStringLiteral("#DCDDE1")));
        QCOMPARE(p.textPrimary, QColor(QStringLiteral("#17181B")));
        QCOMPARE(p.textSecondary, QColor(QStringLiteral("#545862")));
        QCOMPARE(p.textTertiary, QColor(QStringLiteral("#777D88")));
        QCOMPARE(p.textDisabled, QColor(QStringLiteral("#9AA0AA")));
        QCOMPARE(p.border, QColor(QStringLiteral("#D7D9DE")));
        QCOMPARE(p.borderStrong, QColor(QStringLiteral("#858B96")));
        QCOMPARE(p.divider, QColor(QStringLiteral("#E5E6EA")));
        QCOMPARE(p.disabled, QColor(QStringLiteral("#D9DBE0")));
        QCOMPARE(p.success, QColor(QStringLiteral("#208A4A")));
        QCOMPARE(p.warning, QColor(QStringLiteral("#9B6500")));
        QCOMPARE(p.error, QColor(QStringLiteral("#C93632")));
    }
    QCOMPARE(p.accent, QColor(QStringLiteral("#007AFF")));
    QCOMPARE(p.highlight, QColor(QStringLiteral("#007AFF")));
    QCOMPARE(p.currentTrackSurface, QColor(143, 87, 201, 87));
    QVERIFY(p.currentTrackSurface != p.highlight);
    QCOMPARE(p.danger, p.error);
    QCOMPARE(p.critical, p.error);
    QCOMPARE(p.backdropStart, p.background);
    QCOMPARE(p.backdropMiddle, p.background);
    QCOMPARE(p.backdropEnd, p.background);
    QCOMPARE(p.glassSurface, p.surface);
    QCOMPARE(p.glassSurfaceElevated, p.surfaceElevated);
    QCOMPARE(p.glassSurfaceHover, p.surfaceHover);
    QCOMPARE(p.glassSurfacePressed, p.surfacePressed);
    QCOMPARE(p.glassBorder, p.border);
    QCOMPARE(p.glassDivider, p.divider);
    QCOMPARE(p.glassInnerHighlight, p.border);
}

void ThemeManagerTest::recommendedGradientPresetsAreStable()
{
    const QList<ThemeManager::Preset> expected{
        {QStringLiteral("aurora"), {QColor(QStringLiteral("#73A6FF")), QColor(QStringLiteral("#A98BFF")), QColor(QStringLiteral("#F0A8D8"))}},
        {QStringLiteral("seaGlass"), {QColor(QStringLiteral("#71D9D0")), QColor(QStringLiteral("#82C9F4")), QColor(QStringLiteral("#A7B7FF"))}},
        {QStringLiteral("sunset"), {QColor(QStringLiteral("#F49BC2")), QColor(QStringLiteral("#FF9B86")), QColor(QStringLiteral("#FFC97A"))}},
        {QStringLiteral("lavenderMist"), {QColor(QStringLiteral("#8295F2")), QColor(QStringLiteral("#B89BE8")), QColor(QStringLiteral("#E8B7D5"))}},
        {QStringLiteral("morningGlow"), {QColor(QStringLiteral("#8EDFCB")), QColor(QStringLiteral("#D4E9C2")), QColor(QStringLiteral("#FFD995"))}},
    };
    QCOMPARE(ThemeManager::presets(), expected);
    const auto purple = ThemeManager::legacyPresetSeed(QStringLiteral("purple"));
    QVERIFY(purple.has_value());
    QCOMPARE(*purple, QColor(QStringLiteral("#AF52DE")));
    QVERIFY(!ThemeManager::legacyPresetSeed(QStringLiteral("aurora")).has_value());
}

void ThemeManagerTest::recommendedGradientPresetsAreExposedToQml()
{
    const QPalette original = qApp->palette();
    const QVariantList expected{
        QVariantMap{{QStringLiteral("id"), QStringLiteral("aurora")}, {QStringLiteral("start"), QColor(QStringLiteral("#73A6FF"))}, {QStringLiteral("middle"), QColor(QStringLiteral("#A98BFF"))}, {QStringLiteral("end"), QColor(QStringLiteral("#F0A8D8"))}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("seaGlass")}, {QStringLiteral("start"), QColor(QStringLiteral("#71D9D0"))}, {QStringLiteral("middle"), QColor(QStringLiteral("#82C9F4"))}, {QStringLiteral("end"), QColor(QStringLiteral("#A7B7FF"))}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("sunset")}, {QStringLiteral("start"), QColor(QStringLiteral("#F49BC2"))}, {QStringLiteral("middle"), QColor(QStringLiteral("#FF9B86"))}, {QStringLiteral("end"), QColor(QStringLiteral("#FFC97A"))}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("lavenderMist")}, {QStringLiteral("start"), QColor(QStringLiteral("#8295F2"))}, {QStringLiteral("middle"), QColor(QStringLiteral("#B89BE8"))}, {QStringLiteral("end"), QColor(QStringLiteral("#E8B7D5"))}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("morningGlow")}, {QStringLiteral("start"), QColor(QStringLiteral("#8EDFCB"))}, {QStringLiteral("middle"), QColor(QStringLiteral("#D4E9C2"))}, {QStringLiteral("end"), QColor(QStringLiteral("#FFD995"))}},
    };
    {
        ThemeManager manager(*qApp);
        QCOMPARE(manager.recommendedPresets(), expected);
        const QMetaProperty property = manager.metaObject()->property(
            manager.metaObject()->indexOfProperty("recommendedPresets"));
        QVERIFY(property.isConstant());
        QVERIFY(!property.isWritable());
    }
    qApp->setPalette(original);
}

void ThemeManagerTest::generatedSeedsMeetContract_data()
{
    QTest::addColumn<QColor>("seed");
    for (const ThemeManager::Preset& preset : ThemeManager::presets()) {
        QTest::newRow(preset.id.toLatin1().constData()) << preset.stops.front();
    }
    for (const QString& value : {QStringLiteral("#FFFFFF"),
             QStringLiteral("#000000"), QStringLiteral("#FFFF00"),
             QStringLiteral("#00FF00"), QStringLiteral("#0000FF"),
             QStringLiteral("#00FFFF"), QStringLiteral("#FF00FF"),
             QStringLiteral("#777777")}) {
        QTest::newRow(value.toLatin1().constData()) << QColor(value);
    }
}

void ThemeManagerTest::generatedSeedsMeetContract()
{
    QFETCH(QColor, seed);
    for (const auto appearance : {ThemeManager::AppearanceMode::Light,
             ThemeManager::AppearanceMode::Dark}) {
        ThemeManager manager(*qApp);
        manager.applyPreferences(generated(
            appearance, ThemeManager::SkinKind::Solid, {seed, seed, seed}));
        verifyGenerated(manager.palette(), appearance);
    }
}

void ThemeManagerTest::generatedGlassPalettesMeetContract_data()
{
    QTest::addColumn<QColor>("start");
    QTest::addColumn<QColor>("middle");
    QTest::addColumn<QColor>("end");
    for (const ThemeManager::Preset& preset : ThemeManager::presets()) {
        QTest::newRow(preset.id.toLatin1().constData())
            << preset.stops[0] << preset.stops[1] << preset.stops[2];
    }
    const auto addStops = [](const char* name, const char* start,
                             const char* middle, const char* end) {
        QTest::newRow(name) << QColor(QString::fromLatin1(start))
                            << QColor(QString::fromLatin1(middle))
                            << QColor(QString::fromLatin1(end));
    };
    addStops("black", "#000000", "#000000", "#000000");
    addStops("white", "#FFFFFF", "#FFFFFF", "#FFFFFF");
    addStops("gray", "#777777", "#777777", "#777777");
    addStops("rgb", "#FF0000", "#00FF00", "#0000FF");
    addStops("cmy", "#00FFFF", "#FF00FF", "#FFFF00");
}

void ThemeManagerTest::generatedGlassPalettesMeetContract()
{
    QFETCH(QColor, start);
    QFETCH(QColor, middle);
    QFETCH(QColor, end);
    for (const auto appearance : {ThemeManager::AppearanceMode::Light,
                                  ThemeManager::AppearanceMode::Dark}) {
        ThemeManager manager(*qApp);
        manager.applyPreferences(generated(
            appearance, ThemeManager::SkinKind::Gradient,
            {start, middle, end}));
        const ThemePalette& palette = manager.palette();
        const QList<QColor> composites = glassComposites(palette);
        QVERIFY(worstContrast(palette.textPrimary, composites) >= 7.0);
        QVERIFY(worstContrast(palette.textSecondary, composites) >= 4.5);
        QVERIFY(worstContrast(palette.textTertiary, composites) >= 3.0);
        QVERIFY(worstContrast(palette.borderStrong, composites) >= 3.0);
        QVERIFY(worstContrast(palette.accent, composites) >= 4.5);
        QVERIFY(worstContrast(palette.focus, composites) >= 3.0);
        QVERIFY(contrastRatio(palette.accentText, palette.accent) >= 4.5);
        QVERIFY(maxRgbDistance({palette.backdropStart,
                                palette.backdropMiddle,
                                palette.backdropEnd}) >= 0.08);
        QCOMPARE(palette.backdropStart.alpha(), 255);
        QCOMPARE(palette.backdropMiddle.alpha(), 255);
        QCOMPARE(palette.backdropEnd.alpha(), 255);
        QCOMPARE(palette.glassSurface.alpha(),
                 appearance == ThemeManager::AppearanceMode::Dark ? 184 : 173);
        QCOMPARE(palette.glassSurfaceElevated.alpha(),
                 appearance == ThemeManager::AppearanceMode::Dark ? 199 : 189);
        QCOMPARE(palette.glassSurfaceHover.alpha(),
                 appearance == ThemeManager::AppearanceMode::Dark ? 209 : 199);
        QCOMPARE(palette.glassSurfacePressed.alpha(),
                 appearance == ThemeManager::AppearanceMode::Dark ? 219 : 209);
        QCOMPARE(palette.glassBorder.alpha(),
                 appearance == ThemeManager::AppearanceMode::Dark ? 133 : 107);
        QCOMPARE(palette.glassDivider.alpha(),
                 appearance == ThemeManager::AppearanceMode::Dark ? 107 : 87);
        QCOMPARE(palette.glassInnerHighlight.alpha(),
                 appearance == ThemeManager::AppearanceMode::Dark ? 18 : 82);
        QCOMPARE(palette.surface,
                 composite(palette.glassSurface, palette.backdropMiddle));
        QCOMPARE(palette.surfaceElevated,
                 composite(palette.glassSurfaceElevated,
                           palette.backdropMiddle));
        QCOMPARE(palette.surfaceHover,
                 composite(palette.glassSurfaceHover, palette.backdropMiddle));
        QCOMPARE(palette.surfacePressed,
                 composite(palette.glassSurfacePressed,
                           palette.backdropMiddle));
    }
}

void ThemeManagerTest::solidBackdropExpandsSeedHue()
{
    const QColor seed(QStringLiteral("#D948B1"));
    const int seedHue = seed.toHsl().hslHue();
    for (const auto appearance : {ThemeManager::AppearanceMode::Light,
                                  ThemeManager::AppearanceMode::Dark}) {
        ThemeManager manager(*qApp);
        manager.applyPreferences(generated(
            appearance, ThemeManager::SkinKind::Solid, {seed, seed, seed}));
        const QList<QColor> stops{manager.palette().backdropStart,
                                  manager.palette().backdropMiddle,
                                  manager.palette().backdropEnd};
        QVERIFY(maxRgbDistance(stops) >= 0.08);
        for (const QColor& stop : stops) {
            const int delta = qAbs(stop.toHsl().hslHue() - seedHue);
            QVERIFY(qMin(delta, 360 - delta) <= 2);
        }
    }

    const QColor gray(QStringLiteral("#777777"));
    ThemeManager manager(*qApp);
    manager.applyPreferences(generated(
        ThemeManager::AppearanceMode::Dark, ThemeManager::SkinKind::Solid,
        {gray, gray, gray}));
    for (const QColor& stop : {manager.palette().backdropStart,
                               manager.palette().backdropMiddle,
                               manager.palette().backdropEnd}) {
        QCOMPARE(stop.toHsl().hslSaturation(), 0);
    }
}

void ThemeManagerTest::generatedStopsDriveCompletePalette()
{
    ThemeManager manager(*qApp);
    const ThemeManager::SkinStops initial{
        QColor(QStringLiteral("#73A6FF")), QColor(QStringLiteral("#A98BFF")),
        QColor(QStringLiteral("#F0A8D8"))};
    manager.applyPreferences(generated(
        ThemeManager::AppearanceMode::Dark, ThemeManager::SkinKind::Gradient,
        initial));
    const ThemePalette first = manager.palette();
    QSignalSpy changed(&manager, &ThemeManager::paletteChanged);

    ThemeManager::SkinStops middleChanged = initial;
    middleChanged[1] = QColor(QStringLiteral("#48D9A5"));
    manager.applyPreferences(generated(
        ThemeManager::AppearanceMode::Dark, ThemeManager::SkinKind::Gradient,
        middleChanged));
    const ThemePalette second = manager.palette();
    QCOMPARE(changed.count(), 1);
    QVERIFY(second != first);
    QVERIFY(second.backdropMiddle != first.backdropMiddle);
    QVERIFY(glassComposites(second) != glassComposites(first));

    ThemeManager::SkinStops endChanged = middleChanged;
    endChanged[2] = QColor(QStringLiteral("#FFD36E"));
    manager.applyPreferences(generated(
        ThemeManager::AppearanceMode::Dark, ThemeManager::SkinKind::Gradient,
        endChanged));
    const ThemePalette third = manager.palette();
    QCOMPARE(changed.count(), 2);
    QVERIFY(third != second);
    QVERIFY(third.backdropEnd != second.backdropEnd);
    QVERIFY(glassComposites(third) != glassComposites(second));

    manager.applyPreferences(generated(
        ThemeManager::AppearanceMode::Dark, ThemeManager::SkinKind::Gradient,
        endChanged));
    QCOMPARE(changed.count(), 2);
}

void ThemeManagerTest::generatedSeedChangesCompleteOrdinaryPalette()
{
    ThemeManager manager(*qApp);
    const QColor blueSeed(QStringLiteral("#007AFF"));
    manager.applyPreferences(generated(
        ThemeManager::AppearanceMode::Dark, ThemeManager::SkinKind::Solid,
        {blueSeed, blueSeed, blueSeed}));
    const QList<QColor> blue = ordinaryColors(manager.palette());
    const QColor redSeed(QStringLiteral("#FF3B30"));
    manager.applyPreferences(generated(
        ThemeManager::AppearanceMode::Dark, ThemeManager::SkinKind::Solid,
        {redSeed, redSeed, redSeed}));
    const QList<QColor> red = ordinaryColors(manager.palette());
    for (qsizetype i = 0; i < blue.size(); ++i) {
        QVERIFY2(blue.at(i) != red.at(i), qPrintable(QString::number(i)));
    }
}

void ThemeManagerTest::achromaticSeedStaysAchromatic()
{
    ThemeManager manager(*qApp);
    const QColor seed(QStringLiteral("#777777"));
    manager.applyPreferences(generated(
        ThemeManager::AppearanceMode::Light, ThemeManager::SkinKind::Solid,
        {seed, seed, seed}));
    for (const QColor& color : ordinaryColors(manager.palette())) {
        QVERIFY2(color.toHsl().hslSaturation() <= 0,
                 qPrintable(color.name(QColor::HexArgb)));
    }
}

void ThemeManagerTest::semanticColorsKeepIdentity()
{
    ThemeManager manager(*qApp);
    const QColor blueSeed(QStringLiteral("#007AFF"));
    manager.applyPreferences(generated(
        ThemeManager::AppearanceMode::Light, ThemeManager::SkinKind::Solid,
        {blueSeed, blueSeed, blueSeed}));
    const ThemePalette blue = manager.palette();
    const QColor pinkSeed(QStringLiteral("#FF00FF"));
    manager.applyPreferences(generated(
        ThemeManager::AppearanceMode::Light, ThemeManager::SkinKind::Solid,
        {pinkSeed, pinkSeed, pinkSeed}));
    const ThemePalette pink = manager.palette();
    QCOMPARE(blue.success, pink.success);
    QCOMPARE(blue.warning, pink.warning);
    QCOMPARE(blue.error, pink.error);
    QCOMPARE(blue.recording, pink.recording);
    QVERIFY(blue.success.toHsl().hslHue() >= 90
            && blue.success.toHsl().hslHue() <= 160);
    QVERIFY(blue.warning.toHsl().hslHue() >= 30
            && blue.warning.toHsl().hslHue() <= 60);
    QVERIFY(blue.error.toHsl().hslHue() <= 15
            || blue.error.toHsl().hslHue() >= 345);
}

void ThemeManagerTest::normalizesPreferences()
{
    ThemeManager manager(*qApp);
    QColor translucent(QStringLiteral("#123456"));
    translucent.setAlpha(12);
    manager.applyPreferences({static_cast<ThemeManager::AppearanceMode>(99),
                              static_cast<ThemeManager::SkinMode>(99),
                              static_cast<ThemeManager::SkinKind>(99),
                              {translucent, QColor(), translucent}});
    QCOMPARE(manager.preferences().appearanceMode,
             ThemeManager::AppearanceMode::System);
    QCOMPARE(manager.preferences().skinMode, ThemeManager::SkinMode::Default);
    QCOMPARE(manager.preferences().skinKind, ThemeManager::SkinKind::Solid);
    QCOMPARE(manager.preferences().skinStops,
             (ThemeManager::SkinStops{ThemeManager::defaultSeed(),
                                      ThemeManager::defaultSeed(),
                                      ThemeManager::defaultSeed()}));

    manager.applyPreferences(generated(
        ThemeManager::AppearanceMode::Light, ThemeManager::SkinKind::Gradient,
        {QColor(QStringLiteral("#123456")), QColor(),
         QColor(QStringLiteral("#654321"))}));
    QCOMPARE(manager.preferences().skinStops,
             (ThemeManager::SkinStops{ThemeManager::defaultSeed(),
                                      ThemeManager::defaultSeed(),
                                      ThemeManager::defaultSeed()}));

    manager.applyPreferences(generated(
        ThemeManager::AppearanceMode::Light, ThemeManager::SkinKind::Gradient,
        {QColor(QStringLiteral("#123456")), translucent,
         QColor(QStringLiteral("#654321"))}));
    QCOMPARE(manager.preferences().skinStops,
             (ThemeManager::SkinStops{ThemeManager::defaultSeed(),
                                      ThemeManager::defaultSeed(),
                                      ThemeManager::defaultSeed()}));
}

void ThemeManagerTest::notifiesOnceOnlyForRealChanges()
{
    ThemeManager manager(*qApp);
    const QColor blueSeed(QStringLiteral("#007AFF"));
    const ThemeManager::Preferences blue = generated(
        ThemeManager::AppearanceMode::Dark, ThemeManager::SkinKind::Solid,
        {blueSeed, blueSeed, blueSeed});
    manager.applyPreferences(blue);
    QSignalSpy changed(&manager, &ThemeManager::paletteChanged);
    manager.applyPreferences(blue);
    QCOMPARE(changed.count(), 0);
    QColor translucentBlue(QStringLiteral("#007AFF"));
    translucentBlue.setAlpha(8);
    manager.applyPreferences(generated(
        ThemeManager::AppearanceMode::Dark, ThemeManager::SkinKind::Solid,
        {translucentBlue, translucentBlue, translucentBlue}));
    QCOMPARE(changed.count(), 1);
    const QColor redSeed(QStringLiteral("#FF3B30"));
    manager.applyPreferences(generated(
        ThemeManager::AppearanceMode::Dark, ThemeManager::SkinKind::Solid,
        {redSeed, redSeed, redSeed}));
    QCOMPARE(changed.count(), 2);
    manager.applyPreferences({ThemeManager::AppearanceMode::Dark,
                              ThemeManager::SkinMode::Default,
                              ThemeManager::SkinKind::Solid,
                              {QColor(QStringLiteral("#00FF00")),
                               QColor(QStringLiteral("#00FF00")),
                               QColor(QStringLiteral("#00FF00"))}});
    QCOMPARE(changed.count(), 3);
    manager.applyPreferences({ThemeManager::AppearanceMode::Dark,
                              ThemeManager::SkinMode::Default,
                              ThemeManager::SkinKind::Solid,
                              {QColor(QStringLiteral("#FF00FF")),
                               QColor(QStringLiteral("#FF00FF")),
                               QColor(QStringLiteral("#FF00FF"))}});
    QCOMPARE(changed.count(), 3);
}

void ThemeManagerTest::refreshesSystemPaletteOncePerRealChange()
{
    ThemeManager manager(*qApp);
    manager.applyPreferences({ThemeManager::AppearanceMode::System,
                              ThemeManager::SkinMode::Default,
                              ThemeManager::SkinKind::Solid,
                              {ThemeManager::defaultSeed(),
                               ThemeManager::defaultSeed(),
                               ThemeManager::defaultSeed()}});
    QVERIFY(QMetaObject::invokeMethod(&manager, "handleSystemColorSchemeChanged",
        Qt::DirectConnection, Q_ARG(Qt::ColorScheme, Qt::ColorScheme::Light)));
    QSignalSpy changed(&manager, &ThemeManager::paletteChanged);
    QVERIFY(QMetaObject::invokeMethod(&manager, "handleSystemColorSchemeChanged",
        Qt::DirectConnection, Q_ARG(Qt::ColorScheme, Qt::ColorScheme::Dark)));
    QCOMPARE(manager.palette().background, QColor(QStringLiteral("#101114")));
    QCOMPARE(changed.count(), 1);
    QVERIFY(QMetaObject::invokeMethod(&manager, "handleSystemColorSchemeChanged",
        Qt::DirectConnection, Q_ARG(Qt::ColorScheme, Qt::ColorScheme::Dark)));
    QCOMPARE(changed.count(), 1);
}

void ThemeManagerTest::unknownSystemPaletteDoesNotReadBackAppliedTheme()
{
    QPalette platformDark = qApp->palette();
    platformDark.setColor(QPalette::Window, QColor(QStringLiteral("#101114")));
    qApp->setPalette(platformDark);
    ThemeManager manager(*qApp);
    QVERIFY(QMetaObject::invokeMethod(&manager, "handleSystemColorSchemeChanged",
        Qt::DirectConnection, Q_ARG(Qt::ColorScheme, Qt::ColorScheme::Unknown)));
    manager.applyPreferences({ThemeManager::AppearanceMode::Light,
                              ThemeManager::SkinMode::Default,
                              ThemeManager::SkinKind::Solid,
                              {ThemeManager::defaultSeed(),
                               ThemeManager::defaultSeed(),
                               ThemeManager::defaultSeed()}});
    manager.applyPreferences({ThemeManager::AppearanceMode::System,
                              ThemeManager::SkinMode::Default,
                              ThemeManager::SkinKind::Solid,
                              {ThemeManager::defaultSeed(),
                               ThemeManager::defaultSeed(),
                               ThemeManager::defaultSeed()}});
    QCOMPARE(manager.palette().background, QColor(QStringLiteral("#101114")));
}

void ThemeManagerTest::applicationPaletteChangeRefreshesUnknownSystemPalette()
{
    ThemeManager manager(*qApp);
    manager.applyPreferences({ThemeManager::AppearanceMode::System,
                              ThemeManager::SkinMode::Default,
                              ThemeManager::SkinKind::Solid,
                              {ThemeManager::defaultSeed(),
                               ThemeManager::defaultSeed(),
                               ThemeManager::defaultSeed()}});
    QVERIFY(QMetaObject::invokeMethod(&manager, "handleSystemColorSchemeChanged",
        Qt::DirectConnection, Q_ARG(Qt::ColorScheme, Qt::ColorScheme::Dark)));
    QVERIFY(QMetaObject::invokeMethod(&manager, "handleSystemColorSchemeChanged",
        Qt::DirectConnection, Q_ARG(Qt::ColorScheme, Qt::ColorScheme::Unknown)));
    QCOMPARE(manager.palette().background, QColor(QStringLiteral("#101114")));

    QSignalSpy changed(&manager, &ThemeManager::paletteChanged);
    QPalette systemLight = qApp->palette();
    systemLight.setColor(QPalette::Window, QColor(QStringLiteral("#F5F5F7")));
    qApp->setPalette(systemLight);
    QCOMPARE(manager.palette().background, QColor(QStringLiteral("#F5F5F7")));
    QCOMPARE(changed.count(), 1);

    QEvent duplicate(QEvent::ApplicationPaletteChange);
    QCoreApplication::sendEvent(qApp, &duplicate);
    QCOMPARE(changed.count(), 1);
}

void ThemeManagerTest::synchronizesNativePalette()
{
    ThemeManager manager(*qApp);
    const QColor seed(QStringLiteral("#AF52DE"));
    manager.applyPreferences(generated(
        ThemeManager::AppearanceMode::Dark, ThemeManager::SkinKind::Solid,
        {seed, seed, seed}));
    const ThemePalette& p = manager.palette();
    QCOMPARE(qApp->palette().color(QPalette::Window), p.background);
    QCOMPARE(qApp->palette().color(QPalette::Base), p.surface);
    QCOMPARE(qApp->palette().color(QPalette::Highlight), p.highlight);
    QCOMPARE(qApp->palette().color(QPalette::HighlightedText), p.highlightText);
    QCOMPARE(qApp->palette().color(QPalette::Link), p.accent);
    for (int group = 0; group < QPalette::NColorGroups; ++group) {
        for (int role = 0; role < QPalette::NColorRoles; ++role) {
            const QColor color = qApp->palette().color(
                static_cast<QPalette::ColorGroup>(group),
                static_cast<QPalette::ColorRole>(role));
            QCOMPARE(color.alpha(), 255);
        }
    }
}

void ThemeManagerTest::synchronizerOwnsConnectionLifetime()
{
    QVERIFY((std::is_base_of_v<QObject, ThemeSettingsSynchronizer>));
}

void ThemeManagerTest::synchronizerAppliesSkinSettingsAtStartupAndDuringTransactions()
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("AgPlayer"));
    QCoreApplication::setApplicationName(QStringLiteral("AgPlayer-theme-manager-test"));
    QSettings persisted;
    persisted.clear();
    persisted.setValue(QStringLiteral("appearance/themeMode"), 0);
    persisted.setValue(QStringLiteral("appearance/skinColorMode"), 1);
    persisted.setValue(QStringLiteral("appearance/skinPreset"),
                       QStringLiteral("aurora"));
    SettingsController settings;
    ThemeManager manager(*qApp);
    ThemeSettingsSynchronizer synchronizer(manager, settings);
    QCOMPARE(manager.preferences().appearanceMode, ThemeManager::AppearanceMode::Dark);
    QCOMPARE(manager.preferences().skinMode, ThemeManager::SkinMode::Generated);
    QCOMPARE(manager.preferences().skinKind, ThemeManager::SkinKind::Gradient);
    QCOMPARE(manager.preferences().skinStops,
             (ThemeManager::SkinStops{QColor(QStringLiteral("#73A6FF")),
                                      QColor(QStringLiteral("#A98BFF")),
                                      QColor(QStringLiteral("#F0A8D8"))}));

    settings.beginEdit();
    settings.setThemeMode(1);
    settings.setSkinColorMode(2);
    settings.setSkinCustomColor(QStringLiteral("#123456"));
    QCOMPARE(manager.preferences().appearanceMode, ThemeManager::AppearanceMode::Light);
    QCOMPARE(manager.preferences().skinMode, ThemeManager::SkinMode::Generated);
    QCOMPARE(manager.preferences().skinKind, ThemeManager::SkinKind::Solid);
    QCOMPARE(manager.preferences().skinStops,
             (ThemeManager::SkinStops{QColor(QStringLiteral("#123456")),
                                      QColor(QStringLiteral("#123456")),
                                      QColor(QStringLiteral("#123456"))}));
    settings.resetToDefaults();
    QCOMPARE(manager.preferences().appearanceMode,
             ThemeManager::AppearanceMode::System);
    QCOMPARE(manager.preferences().skinMode, ThemeManager::SkinMode::Default);
    settings.cancelEdit();
    QCOMPARE(manager.preferences().appearanceMode, ThemeManager::AppearanceMode::Dark);
    QCOMPARE(manager.preferences().skinMode, ThemeManager::SkinMode::Generated);
    QCOMPARE(manager.preferences().skinKind, ThemeManager::SkinKind::Gradient);
    QCOMPARE(manager.preferences().skinStops,
             (ThemeManager::SkinStops{QColor(QStringLiteral("#73A6FF")),
                                      QColor(QStringLiteral("#A98BFF")),
                                      QColor(QStringLiteral("#F0A8D8"))}));

    settings.beginEdit();
    settings.setSkinColorMode(2);
    settings.setSkinCustomColor(QStringLiteral("#123456"));
    settings.commitEdit();
    QCOMPARE(manager.preferences().skinMode, ThemeManager::SkinMode::Generated);
    QCOMPARE(manager.preferences().skinStops,
             (ThemeManager::SkinStops{QColor(QStringLiteral("#123456")),
                                      QColor(QStringLiteral("#123456")),
                                      QColor(QStringLiteral("#123456"))}));
}

void ThemeManagerTest::ownsNoTimers()
{
    ThemeManager manager(*qApp);
    QCOMPARE(QAbstractEventDispatcher::instance()->registeredTimers(&manager).size(),
             0);
}

QTEST_MAIN(ThemeManagerTest)
#include "theme_manager_test.moc"
