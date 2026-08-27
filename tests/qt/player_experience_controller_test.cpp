#include "audio_visual_feature_controller.hpp"
#include "player_experience_controller.hpp"
#include "settings_controller.hpp"

#include <QCoreApplication>
#include <QSettings>
#include <QStandardPaths>
#include <QTest>

class PlayerExperienceControllerTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void defaultsAreIndependent();
    void persistsAndNormalizesValues();
    void togglesOnlyTheLayoutShellSetting();
    void derivesBandsAndEvents();
    void doesNoSpectrumWorkWhileInactive();
};

void PlayerExperienceControllerTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("AgPlayer"));
    QCoreApplication::setApplicationName(
        QStringLiteral("AgPlayer-player-experience-controller-test"));
    QSettings().clear();
}

void PlayerExperienceControllerTest::defaultsAreIndependent()
{
    QSettings().clear();
    PlayerExperienceController experience;

    QCOMPARE(experience.immersiveMode(), 0);
    QCOMPARE(experience.hostMode(), 0);
    QVERIFY(!experience.lyricsVisible());
    QVERIFY(experience.panelVisible());
    QVERIFY(!experience.desktopMousePassthrough());
    QCOMPARE(experience.qualityPreset(), 0);
    QCOMPARE(experience.coolColor(), QStringLiteral("#4F6FFF"));
    QCOMPARE(experience.warmColor(), QStringLiteral("#FF4778"));
    QCOMPARE(experience.visualEqGains(),
             QVariantList({90, 92, 50, 50, 50, 50, 50, 48}));

    experience.setImmersiveMode(1);
    experience.setLyricsVisible(true);
    QCOMPARE(experience.immersiveMode(), 1);
    QVERIFY(experience.lyricsVisible());
}

void PlayerExperienceControllerTest::persistsAndNormalizesValues()
{
    QSettings settings;
    settings.clear();
    {
        PlayerExperienceController experience;
        experience.setImmersiveMode(1);
        experience.setHostMode(2);
        experience.setLyricsVisible(true);
        experience.setPanelVisible(false);
        experience.setDesktopMousePassthrough(true);
        experience.setQualityPreset(3);
        experience.setCoolColor(QStringLiteral("#123456"));
        experience.setTerrainAmplitude(72);
        experience.setVisualEqGains(QVariantList({0, 1, 2, 3, 4, 5, 6, 7}));
    }

    PlayerExperienceController reloaded;
    QCOMPARE(reloaded.immersiveMode(), 1);
    QCOMPARE(reloaded.hostMode(), 2);
    QVERIFY(reloaded.lyricsVisible());
    QVERIFY(!reloaded.panelVisible());
    QVERIFY(reloaded.desktopMousePassthrough());
    QCOMPARE(reloaded.qualityPreset(), 3);
    QCOMPARE(reloaded.coolColor(), QStringLiteral("#123456"));
    QCOMPARE(reloaded.terrainAmplitude(), 72);
    QCOMPARE(reloaded.visualEqGains(), QVariantList({0, 1, 2, 3, 4, 5, 6, 7}));

    settings.setValue(QStringLiteral("immersiveVisual/mode"), 99);
    settings.setValue(QStringLiteral("immersiveVisual/hostMode"), -1);
    settings.setValue(QStringLiteral("immersiveVisual/qualityPreset"), 99);
    settings.setValue(QStringLiteral("immersiveVisual/coolColor"), QStringLiteral("bad"));
    settings.setValue(QStringLiteral("immersiveVisual/terrainAmplitude"), 999);
    settings.setValue(QStringLiteral("immersiveVisual/visualEqGains"),
                      QVariantList({1, 2}));

    PlayerExperienceController malformed;
    QCOMPARE(malformed.immersiveMode(), 0);
    QCOMPARE(malformed.hostMode(), 0);
    QCOMPARE(malformed.qualityPreset(), 0);
    QCOMPARE(malformed.coolColor(), QStringLiteral("#4F6FFF"));
    QCOMPARE(malformed.terrainAmplitude(), 100);
    QCOMPARE(malformed.visualEqGains(),
             QVariantList({90, 92, 50, 50, 50, 50, 50, 48}));
}

void PlayerExperienceControllerTest::togglesOnlyTheLayoutShellSetting()
{
    QSettings().clear();
    SettingsController settings;
    PlayerExperienceController experience(&settings);

    QCOMPARE(settings.playerShellMode(), 0);
    experience.togglePlayerShellMode();
    QCOMPARE(settings.playerShellMode(), 1);
    experience.togglePlayerShellMode();
    QCOMPARE(settings.playerShellMode(), 0);
}

void PlayerExperienceControllerTest::derivesBandsAndEvents()
{
    AudioVisualFeatureController features;
    QVariantList lowKick(128, 0.0);
    for (int index = 0; index < 32; ++index) {
        lowKick[index] = 0.8;
    }

    features.setActive(true);
    features.processSpectrum(lowKick);
    QCOMPARE(features.derivedUpdateCount(), 1);
    QCOMPARE(features.bands().size(), 8);
    QVERIFY(features.bands().at(0).toDouble() > 0.7);
    QVERIFY(features.energy() > 0.1);
    QVERIFY(features.spectralFlux() > 0.1);
    QVERIFY(features.kickPulse());
    QVERIFY(!features.snarePulse());

    QVariantList snare(128, 0.0);
    for (int index = 64; index < 96; ++index) {
        snare[index] = 0.8;
    }
    features.processSpectrum(snare);
    QCOMPARE(features.derivedUpdateCount(), 2);
    QVERIFY(features.snarePulse());
}

void PlayerExperienceControllerTest::doesNoSpectrumWorkWhileInactive()
{
    AudioVisualFeatureController features;
    const QVariantList spectrum(128, 0.5);

    features.processSpectrum(spectrum);
    QCOMPARE(features.derivedUpdateCount(), 0);
    QVERIFY(!features.active());

    features.setActive(true);
    features.processSpectrum(spectrum);
    QCOMPARE(features.derivedUpdateCount(), 1);

    features.setActive(false);
    features.processSpectrum(spectrum);
    QCOMPARE(features.derivedUpdateCount(), 1);
    QVERIFY(!features.active());
}

QTEST_MAIN(PlayerExperienceControllerTest)
#include "player_experience_controller_test.moc"
