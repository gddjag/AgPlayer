#include "theme_manager.hpp"

#include <QAbstractEventDispatcher>
#include <QEvent>
#include <QGuiApplication>
#include <QSignalSpy>
#include <QTest>

namespace {

double contrastRatio(const QColor& first, const QColor& second)
{
    const auto luminance = [](const QColor& color) {
        const auto linear = [](double channel) {
            channel /= 255.0;
            return channel <= 0.04045
                ? channel / 12.92
                : qPow((channel + 0.055) / 1.055, 2.4);
        };
        return 0.2126 * linear(color.red()) + 0.7152 * linear(color.green())
            + 0.0722 * linear(color.blue());
    };
    const double firstLuminance = luminance(first);
    const double secondLuminance = luminance(second);
    return (qMax(firstLuminance, secondLuminance) + 0.05)
        / (qMin(firstLuminance, secondLuminance) + 0.05);
}

void verifyInteractiveTokens(const ThemePalette& palette)
{
    QVERIFY2(contrastRatio(palette.accent, palette.surface) >= 4.5,
             "resolved accent must be readable as a link or icon");
    QVERIFY2(contrastRatio(palette.highlight, palette.surface) >= 4.5,
             "independent highlight must be readable as a selection color");
    QVERIFY2(contrastRatio(palette.accentHover, palette.surface) >= 4.5,
             "accent hover must preserve control contrast");
    QVERIFY2(contrastRatio(palette.accentPressed, palette.surface) >= 4.5,
             "accent pressed must preserve control contrast");
    QVERIFY2(contrastRatio(palette.highlightHover, palette.surface) >= 4.5,
             "highlight hover must preserve control contrast");
    QVERIFY2(contrastRatio(palette.highlightPressed, palette.surface) >= 4.5,
             "highlight pressed must preserve control contrast");
    QVERIFY2(contrastRatio(palette.focus, palette.surface) >= 3.0,
             "focus must remain visible against the surface");
    QVERIFY2(contrastRatio(palette.borderStrong, palette.surface) >= 3.0,
             "strong border must remain visible against the surface");
    QVERIFY(palette.accentHover != palette.accent);
    QVERIFY(palette.accentPressed != palette.accentHover);
    QVERIFY(palette.highlightHover != palette.highlight);
    QVERIFY(palette.highlightPressed != palette.highlightHover);
}

} // namespace

class ThemeManagerTest final : public QObject {
    Q_OBJECT

private slots:
    void exposesStablePresetSeeds_data();
    void exposesStablePresetSeeds();
    void resolvesExtremeSeedsWithReadableTokens_data();
    void resolvesExtremeSeedsWithReadableTokens();
    void resolvesPresetStates_data();
    void resolvesPresetStates();
    void keepsNeutralAndSemanticTokensIndependent();
    void makesHighlightFollowAccentOnlyWhenRequested();
    void refreshesSystemPaletteOncePerRealChange();
    void applicationPaletteChangeRefreshesUnknownSystemPalette();
    void ownsNoTimers();
};

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
    for (qsizetype index = 0; index < expected.size(); ++index) {
        const ThemeManager::Preset& preset = expected.at(index);
        QTest::newRow(preset.id.toLatin1().constData())
            << static_cast<int>(index) << preset.id << preset.seed;
    }
}

void ThemeManagerTest::exposesStablePresetSeeds()
{
    QFETCH(int, index);
    QFETCH(QString, id);
    QFETCH(QColor, seed);

    QCOMPARE(ThemeManager::defaultSeed(), QColor(QStringLiteral("#D27722")));
    const QList<ThemeManager::Preset> actual = ThemeManager::presets();
    QCOMPARE(actual.size(), 10);
    QCOMPARE(actual.at(index).id, id);
    QCOMPARE(actual.at(index).seed, seed);
}

void ThemeManagerTest::resolvesExtremeSeedsWithReadableTokens_data()
{
    QTest::addColumn<QColor>("seed");
    for (const QString& value : {QStringLiteral("#FFFFFF"), QStringLiteral("#000000"),
             QStringLiteral("#FFFF00"), QStringLiteral("#FF0000"),
             QStringLiteral("#00FF00"), QStringLiteral("#0000FF"),
             QStringLiteral("#00FFFF"), QStringLiteral("#FF00FF"),
             QStringLiteral("#777777"), QStringLiteral("#D27722")}) {
        QTest::newRow(value.toLatin1().constData()) << QColor(value);
    }
}

void ThemeManagerTest::resolvesExtremeSeedsWithReadableTokens()
{
    QFETCH(QColor, seed);
    for (const auto mode : {ThemeManager::AppearanceMode::Light,
             ThemeManager::AppearanceMode::Dark}) {
        ThemeManager manager(*qApp);
        manager.applyPreferences({mode, seed, seed, false});
        const ThemePalette& palette = manager.palette();

        QVERIFY2(contrastRatio(palette.textPrimary, palette.background) >= 4.5,
                 "primary text must remain readable");
        QVERIFY2(contrastRatio(palette.accentText, palette.accent) >= 3.0,
                 "accent text must select the readable foreground");
        verifyInteractiveTokens(palette);
    }
}

void ThemeManagerTest::resolvesPresetStates_data()
{
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
    for (const ThemeManager::Preset& preset : expected) {
        QTest::newRow(preset.id.toLatin1().constData()) << preset.seed;
    }
}

void ThemeManagerTest::resolvesPresetStates()
{
    QFETCH(QColor, seed);
    for (const auto mode : {ThemeManager::AppearanceMode::Light,
             ThemeManager::AppearanceMode::Dark}) {
        ThemeManager manager(*qApp);
        manager.applyPreferences({mode, seed, seed, false});
        verifyInteractiveTokens(manager.palette());
    }
}

void ThemeManagerTest::keepsNeutralAndSemanticTokensIndependent()
{
    ThemeManager manager(*qApp);
    manager.applyPreferences({ThemeManager::AppearanceMode::Dark,
                              QColor(QStringLiteral("#007AFF")),
                              QColor(QStringLiteral("#AF52DE")), false});
    const ThemePalette blue = manager.palette();

    manager.applyPreferences({ThemeManager::AppearanceMode::Dark,
                              QColor(QStringLiteral("#FF3B30")),
                              QColor(QStringLiteral("#34C759")), false});
    const ThemePalette red = manager.palette();

    QCOMPARE(blue.background, red.background);
    QCOMPARE(blue.surface, red.surface);
    QCOMPARE(blue.success, red.success);
    QCOMPARE(blue.warning, red.warning);
    QCOMPARE(blue.error, red.error);
    QCOMPARE(blue.danger, red.danger);
    QCOMPARE(blue.recording, red.recording);
    QVERIFY(blue.accent != red.accent);
    QVERIFY(blue.highlight != red.highlight);
    QCOMPARE(qApp->palette().color(QPalette::Window), red.background);
    QCOMPARE(qApp->palette().color(QPalette::Highlight), red.highlight);
}

void ThemeManagerTest::makesHighlightFollowAccentOnlyWhenRequested()
{
    ThemeManager manager(*qApp);
    const ThemeManager::Preferences independent = {
        ThemeManager::AppearanceMode::Light, QColor(QStringLiteral("#007AFF")),
        QColor(QStringLiteral("#AF52DE")), false};
    manager.applyPreferences(independent);
    const QColor purpleHighlight = manager.palette().highlight;
    QVERIFY(manager.palette().accent != purpleHighlight);

    ThemeManager::Preferences follow = independent;
    follow.highlightFollowsAccent = true;
    manager.applyPreferences(follow);
    QCOMPARE(manager.palette().highlight, manager.palette().accent);

    follow.accentSeed = QColor(QStringLiteral("#FF9500"));
    manager.applyPreferences(follow);
    QCOMPARE(manager.palette().highlight, manager.palette().accent);
    QVERIFY(manager.palette().highlight != purpleHighlight);
}

void ThemeManagerTest::refreshesSystemPaletteOncePerRealChange()
{
    ThemeManager manager(*qApp);
    manager.applyPreferences({ThemeManager::AppearanceMode::System,
                              ThemeManager::defaultSeed(),
                              ThemeManager::defaultSeed(), true});
    QVERIFY(QMetaObject::invokeMethod(
        &manager, "handleSystemColorSchemeChanged", Qt::DirectConnection,
        Q_ARG(Qt::ColorScheme, Qt::ColorScheme::Light)));
    QCOMPARE(manager.palette().background, QColor(QStringLiteral("#F5F5F7")));
    QSignalSpy changed(&manager, &ThemeManager::paletteChanged);

    QVERIFY(QMetaObject::invokeMethod(
        &manager, "handleSystemColorSchemeChanged", Qt::DirectConnection,
        Q_ARG(Qt::ColorScheme, Qt::ColorScheme::Dark)));
    QCOMPARE(manager.palette().background, QColor(QStringLiteral("#101114")));
    QCOMPARE(changed.count(), 1);

    QVERIFY(QMetaObject::invokeMethod(
        &manager, "handleSystemColorSchemeChanged", Qt::DirectConnection,
        Q_ARG(Qt::ColorScheme, Qt::ColorScheme::Dark)));
    QCOMPARE(changed.count(), 1);

    QPalette lightPalette = qApp->palette();
    lightPalette.setColor(QPalette::Window, QColor(QStringLiteral("#F5F5F7")));
    qApp->setPalette(lightPalette);
    QCOMPARE(changed.count(), 1);

    QVERIFY(QMetaObject::invokeMethod(
        &manager, "handleSystemColorSchemeChanged", Qt::DirectConnection,
        Q_ARG(Qt::ColorScheme, Qt::ColorScheme::Unknown)));
    QCOMPARE(manager.palette().background, QColor(QStringLiteral("#F5F5F7")));
    QCOMPARE(changed.count(), 2);

    QEvent duplicate(QEvent::ApplicationPaletteChange);
    QCoreApplication::sendEvent(qApp, &duplicate);
    QCOMPARE(changed.count(), 2);
}

void ThemeManagerTest::applicationPaletteChangeRefreshesUnknownSystemPalette()
{
    ThemeManager manager(*qApp);
    manager.applyPreferences({ThemeManager::AppearanceMode::System,
                              ThemeManager::defaultSeed(),
                              ThemeManager::defaultSeed(), true});
    QVERIFY(QMetaObject::invokeMethod(
        &manager, "handleSystemColorSchemeChanged", Qt::DirectConnection,
        Q_ARG(Qt::ColorScheme, Qt::ColorScheme::Dark)));
    QVERIFY(QMetaObject::invokeMethod(
        &manager, "handleSystemColorSchemeChanged", Qt::DirectConnection,
        Q_ARG(Qt::ColorScheme, Qt::ColorScheme::Unknown)));
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

void ThemeManagerTest::ownsNoTimers()
{
    ThemeManager manager(*qApp);
    QCOMPARE(QAbstractEventDispatcher::instance()->registeredTimers(&manager).size(),
             0);
}

QTEST_MAIN(ThemeManagerTest)
#include "theme_manager_test.moc"
