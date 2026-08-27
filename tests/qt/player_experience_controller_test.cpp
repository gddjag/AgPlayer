#include "audio_visual_feature_controller.hpp"
#include "playback_controller.hpp"
#include "player_experience_controller.hpp"
#include "settings_controller.hpp"

#include <agplayer/c_api.h>

#include <QCoreApplication>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

class PlayerExperienceControllerTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void defaultsAreIndependent();
    void persistsAndNormalizesValues();
    void strictlyParsesPersistedScalarTypes();
    void togglesOnlyTheLayoutShellSetting();
    void derivesBandsAndEvents();
    void followsPlaybackSpectrumOnlyWhileActive();
    void eventThresholdsIncludeBoundaries();
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

void PlayerExperienceControllerTest::strictlyParsesPersistedScalarTypes()
{
    QSettings settings;
    settings.clear();
    settings.setValue(QStringLiteral("immersiveVisual/terrainAmplitude"),
                      QStringLiteral(" 72"));
    settings.setValue(QStringLiteral("immersiveVisual/qualityPreset"),
                      QStringLiteral("3.0"));
    settings.setValue(QStringLiteral("immersiveVisual/cinemaShake"), true);
    settings.setValue(QStringLiteral("immersiveVisual/panelVisible"), 0);

    PlayerExperienceController malformed;
    QCOMPARE(malformed.terrainAmplitude(), 62);
    QCOMPARE(malformed.qualityPreset(), 0);
    QCOMPARE(malformed.cinemaShake(), 0.40);
    QVERIFY(malformed.panelVisible());
    QCOMPARE(settings.value(QStringLiteral("immersiveVisual/terrainAmplitude")),
             QVariant(62));
    QCOMPARE(settings.value(QStringLiteral("immersiveVisual/qualityPreset")),
             QVariant(0));
    QCOMPARE(settings.value(QStringLiteral("immersiveVisual/cinemaShake")),
             QVariant(0.40));
    QCOMPARE(settings.value(QStringLiteral("immersiveVisual/panelVisible")),
             QVariant(true));

    settings.clear();
    settings.setValue(QStringLiteral("immersiveVisual/terrainAmplitude"),
                      QStringLiteral("72"));
    settings.setValue(QStringLiteral("immersiveVisual/qualityPreset"),
                      QStringLiteral("3"));
    settings.setValue(QStringLiteral("immersiveVisual/cinemaShake"),
                      QStringLiteral("0.5"));
    settings.setValue(QStringLiteral("immersiveVisual/panelVisible"),
                      QStringLiteral("false"));

    PlayerExperienceController canonicalStrings;
    QCOMPARE(canonicalStrings.terrainAmplitude(), 72);
    QCOMPARE(canonicalStrings.qualityPreset(), 3);
    QCOMPARE(canonicalStrings.cinemaShake(), 0.5);
    QVERIFY(!canonicalStrings.panelVisible());
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

void PlayerExperienceControllerTest::followsPlaybackSpectrumOnlyWhileActive()
{
    const QByteArray path = qgetenv("AGPLAYER_TEST_WAV");
    QVERIFY(!path.isEmpty());
    ag_player_config config{AG_AUDIO_BACKEND_NULL, 2048};
    ag_player* core = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &core), AG_OK);
    {
        PlaybackController playback(core);
        AudioVisualFeatureController features(&playback);
        QSignalSpy spectrumChanged(&playback, &PlaybackController::spectrumChanged);

        QCOMPARE(ag_player_load(core, path.constData()), AG_OK);
        playback.play();
        QTRY_VERIFY(spectrumChanged.count() > 0);
        QCOMPARE(features.derivedUpdateCount(), quint64{0});

        features.setActive(true);
        const int beforeActivePublication = spectrumChanged.count();
        QCOMPARE(ag_player_stop(core), AG_OK);
        QCOMPARE(ag_player_load(core, path.constData()), AG_OK);
        playback.play();
        QTRY_VERIFY(spectrumChanged.count() > beforeActivePublication);
        QTRY_VERIFY(features.derivedUpdateCount() > 0);

        features.setActive(false);
        const quint64 derivedBeforeInactivePublication =
            features.derivedUpdateCount();
        const int beforeInactivePublication = spectrumChanged.count();
        QCOMPARE(ag_player_stop(core), AG_OK);
        QCOMPARE(ag_player_load(core, path.constData()), AG_OK);
        playback.play();
        QTRY_VERIFY(spectrumChanged.count() > beforeInactivePublication);
        QTest::qWait(PlaybackController::PollIntervalMs * 3);
        QCOMPARE(features.derivedUpdateCount(), derivedBeforeInactivePublication);
        QVERIFY(!features.active());
    }
    ag_player_destroy(core);
}

void PlayerExperienceControllerTest::eventThresholdsIncludeBoundaries()
{
    const auto hasKick = [](double baseline) {
        AudioVisualFeatureController features;
        QVariantList spectrum(128, 0.0);
        features.setActive(true);
        for (int index = 0; index < 32; ++index) spectrum[index] = baseline;
        features.processSpectrum(spectrum);
        for (int index = 0; index < 32; ++index) spectrum[index] = 0.1;
        features.processSpectrum(spectrum);
        return features.kickPulse();
    };
    QVERIFY(!hasKick(0.051)); // Positive delta is just below 0.05.
    QVERIFY(hasKick(0.05));   // Positive delta is exactly 0.05.
    QVERIFY(hasKick(0.049));  // Positive delta is just above 0.05.

    const auto hasSnare = [](double baseline) {
        AudioVisualFeatureController features;
        QVariantList spectrum(128, 0.0);
        features.setActive(true);
        for (int index = 64; index < 96; ++index) spectrum[index] = baseline;
        features.processSpectrum(spectrum);
        for (int index = 64; index < 96; ++index) spectrum[index] = 0.1;
        features.processSpectrum(spectrum);
        return features.snarePulse();
    };
    QVERIFY(!hasSnare(0.061)); // Positive delta is just below 0.04.
    QVERIFY(hasSnare(0.06));   // Positive delta is exactly 0.04.
    QVERIFY(hasSnare(0.059));  // Positive delta is just above 0.04.
}

QTEST_MAIN(PlayerExperienceControllerTest)
#include "player_experience_controller_test.moc"
