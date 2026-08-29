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
    void defaultsExposeV46ExperienceControls();
    void clampsPersistsAndNotifiesV46ExperienceControls();
    void persistsAndClampsLyricPlacement();
    void appliesDistinctCompleteVisualPresetSnapshots();
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
    QVERIFY(experience.songAdaptiveColorEnabled());
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
        experience.setSongAdaptiveColorEnabled(false);
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
    QVERIFY(!reloaded.songAdaptiveColorEnabled());
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

void PlayerExperienceControllerTest::defaultsExposeV46ExperienceControls()
{
    QSettings().clear();
    PlayerExperienceController experience;

    QCOMPARE(experience.lyricClarity(), 78);
    QCOMPARE(experience.lyricDepth(), 62);
    QCOMPARE(experience.lyricSize(), 100);
    QCOMPARE(experience.lyricOpacity(), 88);
    QCOMPARE(experience.lyricPosition(), PlayerExperienceController::Center);
    QCOMPARE(experience.lyricPositionX(), 50);
    QCOMPARE(experience.lyricPositionY(), 42);
    QCOMPARE(experience.inputCompression(), 82);
    QCOMPARE(experience.audioResponse(), 128);
    QCOMPARE(experience.responseRange(), 100);
    QCOMPARE(experience.centerHighlight(), 58);
    QCOMPARE(experience.rhythmStrength(), 30);
    QCOMPARE(experience.depthOfField(), 86);
    QCOMPARE(experience.subjectClarity(), 110);
    QCOMPARE(experience.autoRotateSpeed(), 42);
    QCOMPARE(experience.rhythmSensitivity(), 78);
}

void PlayerExperienceControllerTest::persistsAndClampsLyricPlacement()
{
    QSettings().clear();
    {
        PlayerExperienceController experience;
        QSignalSpy placementChanged(&experience,
                                    &PlayerExperienceController::lyricPositionChanged);
        QSignalSpy horizontalChanged(&experience,
                                     &PlayerExperienceController::lyricPositionXChanged);
        QSignalSpy verticalChanged(&experience,
                                   &PlayerExperienceController::lyricPositionYChanged);
        QVERIFY(placementChanged.isValid());
        QVERIFY(horizontalChanged.isValid());
        QVERIFY(verticalChanged.isValid());

        experience.setLyricPosition(PlayerExperienceController::Left);
        experience.setLyricPositionX(-1);
        experience.setLyricPositionY(101);
        QCOMPARE(experience.lyricPosition(), PlayerExperienceController::Left);
        QCOMPARE(experience.lyricPositionX(), 0);
        QCOMPARE(experience.lyricPositionY(), 100);
        QCOMPARE(placementChanged.count(), 1);
        QCOMPARE(horizontalChanged.count(), 1);
        QCOMPARE(verticalChanged.count(), 1);
    }

    PlayerExperienceController reloaded;
    QCOMPARE(reloaded.lyricPosition(), PlayerExperienceController::Left);
    QCOMPARE(reloaded.lyricPositionX(), 0);
    QCOMPARE(reloaded.lyricPositionY(), 100);
    reloaded.setLyricPosition(999);
    QCOMPARE(reloaded.lyricPosition(), PlayerExperienceController::Center);
}

void PlayerExperienceControllerTest::clampsPersistsAndNotifiesV46ExperienceControls()
{
    QSettings().clear();
    {
        PlayerExperienceController experience;
        QSignalSpy instanceClarityChanged(&experience,
                                          &PlayerExperienceController::lyricClarityChanged);
        QSignalSpy instanceRotationSpeedChanged(
            &experience, &PlayerExperienceController::autoRotateSpeedChanged);
        QSignalSpy depthChanged(&experience, &PlayerExperienceController::lyricDepthChanged);
        QSignalSpy sizeChanged(&experience, &PlayerExperienceController::lyricSizeChanged);
        QSignalSpy opacityChanged(&experience, &PlayerExperienceController::lyricOpacityChanged);
        QSignalSpy compressionChanged(
            &experience, &PlayerExperienceController::inputCompressionChanged);
        QSignalSpy audioResponseChanged(
            &experience, &PlayerExperienceController::audioResponseChanged);
        QSignalSpy responseRangeChanged(
            &experience, &PlayerExperienceController::responseRangeChanged);
        QSignalSpy centerHighlightChanged(
            &experience, &PlayerExperienceController::centerHighlightChanged);
        QSignalSpy rhythmStrengthChanged(
            &experience, &PlayerExperienceController::rhythmStrengthChanged);
        QSignalSpy depthOfFieldChanged(
            &experience, &PlayerExperienceController::depthOfFieldChanged);
        QSignalSpy subjectClarityChanged(
            &experience, &PlayerExperienceController::subjectClarityChanged);
        QSignalSpy rhythmSensitivityChanged(
            &experience, &PlayerExperienceController::rhythmSensitivityChanged);
        QVERIFY(instanceClarityChanged.isValid());
        QVERIFY(instanceRotationSpeedChanged.isValid());
        QVERIFY(depthChanged.isValid());
        QVERIFY(sizeChanged.isValid());
        QVERIFY(opacityChanged.isValid());
        QVERIFY(compressionChanged.isValid());
        QVERIFY(audioResponseChanged.isValid());
        QVERIFY(responseRangeChanged.isValid());
        QVERIFY(centerHighlightChanged.isValid());
        QVERIFY(rhythmStrengthChanged.isValid());
        QVERIFY(depthOfFieldChanged.isValid());
        QVERIFY(subjectClarityChanged.isValid());
        QVERIFY(rhythmSensitivityChanged.isValid());

        experience.setLyricClarity(-1);
        experience.setLyricDepth(101);
        experience.setLyricSize(59);
        experience.setLyricOpacity(9);
        experience.setInputCompression(19);
        experience.setAudioResponse(201);
        experience.setResponseRange(49);
        experience.setCenterHighlight(101);
        experience.setRhythmStrength(141);
        experience.setDepthOfField(151);
        experience.setSubjectClarity(19);
        experience.setAutoRotateSpeed(101);
        experience.setRhythmSensitivity(-1);

        QCOMPARE(experience.lyricClarity(), 0);
        QCOMPARE(experience.lyricDepth(), 100);
        QCOMPARE(experience.lyricSize(), 60);
        QCOMPARE(experience.lyricOpacity(), 10);
        QCOMPARE(experience.inputCompression(), 20);
        QCOMPARE(experience.audioResponse(), 200);
        QCOMPARE(experience.responseRange(), 50);
        QCOMPARE(experience.centerHighlight(), 100);
        QCOMPARE(experience.rhythmStrength(), 140);
        QCOMPARE(experience.depthOfField(), 150);
        QCOMPARE(experience.subjectClarity(), 20);
        QCOMPARE(experience.autoRotateSpeed(), 100);
        QCOMPARE(experience.rhythmSensitivity(), 0);
        QCOMPARE(instanceClarityChanged.count(), 1);
        QCOMPARE(instanceRotationSpeedChanged.count(), 1);
        QCOMPARE(depthChanged.count(), 1);
        QCOMPARE(sizeChanged.count(), 1);
        QCOMPARE(opacityChanged.count(), 1);
        QCOMPARE(compressionChanged.count(), 1);
        QCOMPARE(audioResponseChanged.count(), 1);
        QCOMPARE(responseRangeChanged.count(), 1);
        QCOMPARE(centerHighlightChanged.count(), 1);
        QCOMPARE(rhythmStrengthChanged.count(), 1);
        QCOMPARE(depthOfFieldChanged.count(), 1);
        QCOMPARE(subjectClarityChanged.count(), 1);
        QCOMPARE(rhythmSensitivityChanged.count(), 1);
    }

    PlayerExperienceController reloaded;
    QCOMPARE(reloaded.lyricClarity(), 0);
    QCOMPARE(reloaded.lyricDepth(), 100);
    QCOMPARE(reloaded.lyricSize(), 60);
    QCOMPARE(reloaded.lyricOpacity(), 10);
    QCOMPARE(reloaded.inputCompression(), 20);
    QCOMPARE(reloaded.audioResponse(), 200);
    QCOMPARE(reloaded.responseRange(), 50);
    QCOMPARE(reloaded.centerHighlight(), 100);
    QCOMPARE(reloaded.rhythmStrength(), 140);
    QCOMPARE(reloaded.depthOfField(), 150);
    QCOMPARE(reloaded.subjectClarity(), 20);
    QCOMPARE(reloaded.autoRotateSpeed(), 100);
    QCOMPARE(reloaded.rhythmSensitivity(), 0);
}

void PlayerExperienceControllerTest::appliesDistinctCompleteVisualPresetSnapshots()
{
    QSettings().clear();
    PlayerExperienceController experience;
    const QList<int> presets = {
        PlayerExperienceController::AudioRangeEcho,
        PlayerExperienceController::NeonRainNight,
        PlayerExperienceController::InkWash,
        PlayerExperienceController::PureStage,
        PlayerExperienceController::Quiet,
        PlayerExperienceController::Galaxy,
    };
    QCOMPARE(PlayerExperienceController::AudioRangeEcho, 0);
    QCOMPARE(PlayerExperienceController::NeonRainNight, 1);
    QCOMPARE(PlayerExperienceController::InkWash, 2);
    QCOMPARE(PlayerExperienceController::PureStage, 3);
    QCOMPARE(PlayerExperienceController::Quiet, 4);
    QCOMPARE(PlayerExperienceController::Galaxy, 5);
    QList<QVariantList> snapshots;

    for (const int preset : presets) {
        QVERIFY(experience.applyPreset(preset));
        snapshots.append({
            experience.colorMode(), experience.coolColor(), experience.warmColor(),
            experience.accentColor(), experience.peakColor(), experience.baseColor(),
            experience.terrainAmplitude(), experience.motionResponse(),
            experience.gradientLayers(), experience.glowIntensity(), experience.cinemaShake(),
            experience.autoRotate(), experience.peakBoost(), experience.ripplesEnabled(),
            experience.floatingCubesEnabled(), experience.meteorsEnabled(),
            experience.idleBreathingEnabled(), experience.themeCycleEnabled(),
            experience.visualEqGains(), experience.inputCompression(),
            experience.audioResponse(), experience.responseRange(),
            experience.centerHighlight(), experience.rhythmStrength(),
            experience.depthOfField(), experience.subjectClarity(),
            experience.autoRotateSpeed(), experience.rhythmSensitivity(),
        });
    }

    for (int left = 0; left < snapshots.size(); ++left) {
        for (int right = left + 1; right < snapshots.size(); ++right) {
            QVERIFY2(snapshots.at(left) != snapshots.at(right),
                     "every visual preset must apply a different complete state snapshot");
        }
    }
    const QList<QVariantList> expected = {
        {0, "#2F6BFF", "#FF3D81", "#50F6E8", "#F3FF75", "#060A1A", 74, 82, 88,
         66, 0.65, 70, 86, true, true, false, true, true,
         QVariantList({100, 94, 68, 52, 50, 56, 76, 92}), 104, 152, 140, 72, 96, 92,
         118, 68, 92},
        {2, "#00D9FF", "#FF2C9C", "#7CFF6B", "#FFE45C", "#080316", 68, 94, 96,
         82, 0.82, 82, 92, true, true, true, false, true,
         QVariantList({92, 84, 58, 48, 54, 72, 96, 100}), 126, 176, 166, 84, 116, 112,
         126, 86, 100},
        {1, "#7B8B93", "#2E363C", "#C9D1CA", "#EEF0E8", "#F3F1E7", 42, 34, 30,
         18, 0.12, 18, 24, false, false, false, true, false,
         QVariantList({62, 58, 54, 50, 48, 44, 42, 40}), 56, 64, 76, 34, 18, 72,
         92, 16, 42},
        {1, "#4C79B8", "#F2A65A", "#FFFFFF", "#FFF2A6", "#111827", 36, 48, 46,
         30, 0.20, 24, 38, false, false, false, false, false,
         QVariantList({70, 68, 62, 58, 58, 62, 68, 72}), 72, 88, 92, 62, 32, 84,
         120, 30, 58},
        {0, "#5E7180", "#71806B", "#A8B6A0", "#D8D6BE", "#141A1C", 20, 18, 22,
         12, 0.0, 0, 16, false, false, false, true, false,
         QVariantList({48, 46, 44, 42, 42, 40, 38, 36}), 38, 42, 58, 22, 0, 48,
         78, 0, 28},
        {2, "#536DFF", "#B15CFF", "#52E5FF", "#FFF28A", "#03051A", 88, 76, 92,
         78, 0.56, 90, 80, true, true, true, true, true,
         QVariantList({96, 88, 66, 54, 58, 76, 94, 100}), 118, 162, 154, 80, 104,
         104, 124, 80, 96},
    };
    QCOMPARE(snapshots, expected);
    QVERIFY(!experience.applyPreset(-1));
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
