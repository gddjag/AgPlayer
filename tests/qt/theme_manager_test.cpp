#include "theme_manager.hpp"
#include "settings_controller.hpp"

#include <QAbstractEventDispatcher>
#include <QCoreApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QSignalSpy>
#include <QSettings>
#include <QStandardPaths>
#include <QTest>
#include <QtMath>

#include <type_traits>

namespace {

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
    void exposesStablePresetSeeds_data();
    void exposesStablePresetSeeds();
    void generatedSeedsMeetContract_data();
    void generatedSeedsMeetContract();
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
                              QColor(QStringLiteral("#FF00FF"))});
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
}

void ThemeManagerTest::exposesStablePresetSeeds_data()
{
    QTest::addColumn<int>("index");
    QTest::addColumn<QString>("id");
    QTest::addColumn<QColor>("seed");
    const QList<ThemeManager::Preset> expected = {
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
    for (qsizetype i = 0; i < expected.size(); ++i) {
        QTest::newRow(expected.at(i).id.toLatin1().constData())
            << static_cast<int>(i) << expected.at(i).id << expected.at(i).seed;
    }
}

void ThemeManagerTest::exposesStablePresetSeeds()
{
    QFETCH(int, index);
    QFETCH(QString, id);
    QFETCH(QColor, seed);
    QCOMPARE(ThemeManager::defaultSeed(), QColor(QStringLiteral("#D27722")));
    QCOMPARE(ThemeManager::presets().size(), 10);
    QCOMPARE(ThemeManager::presets().at(index).id, id);
    QCOMPARE(ThemeManager::presets().at(index).seed, seed);
}

void ThemeManagerTest::generatedSeedsMeetContract_data()
{
    QTest::addColumn<QColor>("seed");
    for (const ThemeManager::Preset& preset : ThemeManager::presets()) {
        QTest::newRow(preset.id.toLatin1().constData()) << preset.seed;
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
        manager.applyPreferences(
            {appearance, ThemeManager::SkinMode::Generated, seed});
        verifyGenerated(manager.palette(), appearance);
    }
}

void ThemeManagerTest::generatedSeedChangesCompleteOrdinaryPalette()
{
    ThemeManager manager(*qApp);
    manager.applyPreferences({ThemeManager::AppearanceMode::Dark,
                              ThemeManager::SkinMode::Generated,
                              QColor(QStringLiteral("#007AFF"))});
    const QList<QColor> blue = ordinaryColors(manager.palette());
    manager.applyPreferences({ThemeManager::AppearanceMode::Dark,
                              ThemeManager::SkinMode::Generated,
                              QColor(QStringLiteral("#FF3B30"))});
    const QList<QColor> red = ordinaryColors(manager.palette());
    for (qsizetype i = 0; i < blue.size(); ++i) {
        QVERIFY2(blue.at(i) != red.at(i), qPrintable(QString::number(i)));
    }
}

void ThemeManagerTest::achromaticSeedStaysAchromatic()
{
    ThemeManager manager(*qApp);
    manager.applyPreferences({ThemeManager::AppearanceMode::Light,
                              ThemeManager::SkinMode::Generated,
                              QColor(QStringLiteral("#777777"))});
    for (const QColor& color : ordinaryColors(manager.palette())) {
        QVERIFY2(color.toHsl().hslSaturation() <= 0,
                 qPrintable(color.name(QColor::HexArgb)));
    }
}

void ThemeManagerTest::semanticColorsKeepIdentity()
{
    ThemeManager manager(*qApp);
    manager.applyPreferences({ThemeManager::AppearanceMode::Light,
                              ThemeManager::SkinMode::Generated,
                              QColor(QStringLiteral("#007AFF"))});
    const ThemePalette blue = manager.palette();
    manager.applyPreferences({ThemeManager::AppearanceMode::Light,
                              ThemeManager::SkinMode::Generated,
                              QColor(QStringLiteral("#FF00FF"))});
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
                              translucent});
    QCOMPARE(manager.preferences().appearanceMode,
             ThemeManager::AppearanceMode::System);
    QCOMPARE(manager.preferences().skinMode, ThemeManager::SkinMode::Default);
    QCOMPARE(manager.preferences().skinSeed, QColor(QStringLiteral("#123456")));
    manager.applyPreferences({ThemeManager::AppearanceMode::Light,
                              ThemeManager::SkinMode::Generated, QColor()});
    QCOMPARE(manager.preferences().skinSeed, ThemeManager::defaultSeed());
}

void ThemeManagerTest::notifiesOnceOnlyForRealChanges()
{
    ThemeManager manager(*qApp);
    const ThemeManager::Preferences blue = {
        ThemeManager::AppearanceMode::Dark, ThemeManager::SkinMode::Generated,
        QColor(QStringLiteral("#007AFF"))};
    manager.applyPreferences(blue);
    QSignalSpy changed(&manager, &ThemeManager::paletteChanged);
    manager.applyPreferences(blue);
    QCOMPARE(changed.count(), 0);
    QColor translucentBlue(QStringLiteral("#007AFF"));
    translucentBlue.setAlpha(8);
    manager.applyPreferences({ThemeManager::AppearanceMode::Dark,
                              ThemeManager::SkinMode::Generated,
                              translucentBlue});
    QCOMPARE(changed.count(), 0);
    manager.applyPreferences({ThemeManager::AppearanceMode::Dark,
                              ThemeManager::SkinMode::Generated,
                              QColor(QStringLiteral("#FF3B30"))});
    QCOMPARE(changed.count(), 1);
    manager.applyPreferences({ThemeManager::AppearanceMode::Dark,
                              ThemeManager::SkinMode::Default,
                              QColor(QStringLiteral("#00FF00"))});
    QCOMPARE(changed.count(), 2);
    manager.applyPreferences({ThemeManager::AppearanceMode::Dark,
                              ThemeManager::SkinMode::Default,
                              QColor(QStringLiteral("#FF00FF"))});
    QCOMPARE(changed.count(), 2);
}

void ThemeManagerTest::refreshesSystemPaletteOncePerRealChange()
{
    ThemeManager manager(*qApp);
    manager.applyPreferences({ThemeManager::AppearanceMode::System,
                              ThemeManager::SkinMode::Default,
                              ThemeManager::defaultSeed()});
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
                              ThemeManager::defaultSeed()});
    manager.applyPreferences({ThemeManager::AppearanceMode::System,
                              ThemeManager::SkinMode::Default,
                              ThemeManager::defaultSeed()});
    QCOMPARE(manager.palette().background, QColor(QStringLiteral("#101114")));
}

void ThemeManagerTest::applicationPaletteChangeRefreshesUnknownSystemPalette()
{
    ThemeManager manager(*qApp);
    manager.applyPreferences({ThemeManager::AppearanceMode::System,
                              ThemeManager::SkinMode::Default,
                              ThemeManager::defaultSeed()});
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
    manager.applyPreferences({ThemeManager::AppearanceMode::Dark,
                              ThemeManager::SkinMode::Generated,
                              QColor(QStringLiteral("#AF52DE"))});
    const ThemePalette& p = manager.palette();
    QCOMPARE(qApp->palette().color(QPalette::Window), p.background);
    QCOMPARE(qApp->palette().color(QPalette::Base), p.surface);
    QCOMPARE(qApp->palette().color(QPalette::Highlight), p.highlight);
    QCOMPARE(qApp->palette().color(QPalette::HighlightedText), p.highlightText);
    QCOMPARE(qApp->palette().color(QPalette::Link), p.accent);
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
                       QStringLiteral("purple"));
    SettingsController settings;
    ThemeManager manager(*qApp);
    ThemeSettingsSynchronizer synchronizer(manager, settings);
    QCOMPARE(manager.preferences().appearanceMode, ThemeManager::AppearanceMode::Dark);
    QCOMPARE(manager.preferences().skinMode, ThemeManager::SkinMode::Generated);
    QCOMPARE(manager.preferences().skinSeed, QColor(QStringLiteral("#AF52DE")));

    settings.beginEdit();
    settings.setThemeMode(1);
    settings.setSkinColorMode(3);
    settings.setSkinCustomColor(QStringLiteral("#123456"));
    QCOMPARE(manager.preferences().appearanceMode, ThemeManager::AppearanceMode::Light);
    QCOMPARE(manager.preferences().skinMode, ThemeManager::SkinMode::Generated);
    QCOMPARE(manager.preferences().skinSeed, QColor(QStringLiteral("#123456")));
    settings.resetToDefaults();
    QCOMPARE(manager.preferences().appearanceMode,
             ThemeManager::AppearanceMode::System);
    QCOMPARE(manager.preferences().skinMode, ThemeManager::SkinMode::Default);
    settings.cancelEdit();
    QCOMPARE(manager.preferences().appearanceMode, ThemeManager::AppearanceMode::Dark);
    QCOMPARE(manager.preferences().skinMode, ThemeManager::SkinMode::Generated);
    QCOMPARE(manager.preferences().skinSeed, QColor(QStringLiteral("#AF52DE")));

    settings.beginEdit();
    settings.setSkinColorMode(3);
    settings.setSkinCustomColor(QStringLiteral("#123456"));
    settings.commitEdit();
    QCOMPARE(manager.preferences().skinMode, ThemeManager::SkinMode::Generated);
    QCOMPARE(manager.preferences().skinSeed, QColor(QStringLiteral("#123456")));
}

void ThemeManagerTest::ownsNoTimers()
{
    ThemeManager manager(*qApp);
    QCOMPARE(QAbstractEventDispatcher::instance()->registeredTimers(&manager).size(),
             0);
}

QTEST_MAIN(ThemeManagerTest)
#include "theme_manager_test.moc"
