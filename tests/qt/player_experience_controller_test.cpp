#include "audio_visual_feature_controller.hpp"
#include "playback_controller.hpp"
#include "player_experience_controller.hpp"
#include "settings_controller.hpp"

#include <agplayer/c_api.h>

#include <QCoreApplication>
#include <QSettings>
#include <QMetaProperty>
#include <QSet>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

class PlayerExperienceControllerTest final : public QObject {
    Q_OBJECT

private slots:
    void columnControlsNormalizeNotifyAndPersist();
    void columnPresetsRestoreIndependentControls();
    void materialControlsNormalizeNotifyAndPersist();
    void materialPresetsRestoreAfterInk();
    void defaultPresetDoesNotOverrideSongPaletteWithTimeCycling();
    void initTestCase();
    void defaultsAreIndependent();
    void persistsAndNormalizesValues();
    void defaultsExposeV46ExperienceControls();
    void clampsPersistsAndNotifiesV46ExperienceControls();
    void persistsAndClampsLyricPlacement();
    void appliesDistinctCompleteVisualPresetSnapshots();
    void presetsMatchFinalHtmlContract();
    void strictlyParsesPersistedScalarTypes();
    void togglesOnlyTheLayoutShellSetting();
    void cyclesClassicIntegratedAndIndependentImmersiveTheme();
    void derivesBandsAndEvents();
    void followsPlaybackSpectrumOnlyWhileActive();
    void eventThresholdsIncludeBoundaries();
};

void PlayerExperienceControllerTest::columnControlsNormalizeNotifyAndPersist()
{
    struct Control { const char* name; int minimum; int maximum; int fallback; };
    const Control controls[] = {
        {"columnDensity", 50, 200, 125},
        {"columnSize", 50, 200, 50}, {"columnOpacity", 0, 100, 72},
        {"reactorBrightness", 0, 200, 100},
    };
    QSettings().clear();
    for (const auto& control : controls) {
        PlayerExperienceController experience;
        const int index = experience.metaObject()->indexOfProperty(control.name);
        QVERIFY2(index >= 0, control.name);
        const auto property = experience.metaObject()->property(index);
        QCOMPARE(property.read(&experience).toInt(), control.fallback);
        QSignalSpy changed(&experience, property.notifySignal());
        QVERIFY(changed.isValid());
        QVERIFY(property.write(&experience, -50));
        QCOMPARE(property.read(&experience).toInt(), control.minimum);
        const int lowChangeCount = control.fallback == control.minimum ? 0 : 1;
        QCOMPARE(changed.count(), lowChangeCount);
        QVERIFY(property.write(&experience, -50));
        QCOMPARE(changed.count(), lowChangeCount);
        QVERIFY(property.write(&experience, 999));
        QCOMPARE(property.read(&experience).toInt(), control.maximum);
        QCOMPARE(changed.count(), lowChangeCount + 1);
        QCOMPARE(experience.terrainAmplitude(), 42);
        QCOMPARE(experience.subjectClarity(), 110);
        QCOMPARE(experience.rhythmStrength(), 30);
        PlayerExperienceController reloaded;
        QCOMPARE(reloaded.property(control.name).toInt(), control.maximum);
        QSettings settings;
        const QString key = QStringLiteral("immersiveVisual/") + control.name;
        settings.setValue(key, QStringLiteral("invalid"));
        PlayerExperienceController invalid;
        QCOMPARE(invalid.property(control.name).toInt(), control.fallback);
        settings.setValue(key, -99);
        PlayerExperienceController low;
        QCOMPARE(low.property(control.name).toInt(), control.minimum);
        settings.setValue(key, 999);
        PlayerExperienceController high;
        QCOMPARE(high.property(control.name).toInt(), control.maximum);
    }
}

void PlayerExperienceControllerTest::columnPresetsRestoreIndependentControls()
{
    QSettings().clear();
    PlayerExperienceController experience;
    const char* keys[] = {"columnSize", "columnOpacity", "reactorBrightness"};
    for (const char* key : keys)
        QVERIFY2(experience.metaObject()->indexOfProperty(key) >= 0, key);
    const int sizes[] = {120, 110, 95, 125, 150, 110, 100, 130, 140};
    for (int preset = 0; preset < 9; ++preset) {
        experience.setProperty("columnDensity", 175);
        for (const char* key : keys) QVERIFY(experience.setProperty(key, 51));
        QVERIFY(experience.applyPreset(preset));
        QCOMPARE(experience.property("columnDensity").toInt(), 175);
        QCOMPARE(experience.property("columnSize").toInt(), sizes[preset]);
        QCOMPARE(experience.property("columnOpacity").toInt(), 100);
        QCOMPARE(experience.property("reactorBrightness").toInt(), 100);
        PlayerExperienceController reloaded;
        for (const char* key : keys)
            QCOMPARE(reloaded.property(key), experience.property(key));
    }
}

void PlayerExperienceControllerTest::materialControlsNormalizeNotifyAndPersist()
{
    struct Control { const char* name; int minimum; int maximum; int fallback; };
    const Control controls[] = {
        {"materialMode", 0, 2, 0}, {"materialSoftness", 0, 100, 45},
        {"jellyElasticity", 0, 100, 35}, {"inkDensity", 0, 100, 60},
        {"rippleStrength", 0, 200, 100}, {"rippleWidth", 20, 200, 100},
        {"rippleDecay", 20, 200, 100},
    };
    QSettings().clear();
    for (const auto& control : controls) {
        PlayerExperienceController experience;
        const int index = experience.metaObject()->indexOfProperty(control.name);
        QVERIFY2(index >= 0, control.name);
        const auto property = experience.metaObject()->property(index);
        QCOMPARE(property.read(&experience).toInt(), control.fallback);
        QSignalSpy changed(&experience, property.notifySignal());
        QVERIFY(changed.isValid());
        QVERIFY(property.write(&experience, control.maximum));
        QCOMPARE(property.read(&experience).toInt(), control.maximum);
        QCOMPARE(changed.count(), 1);
        QVERIFY(property.write(&experience, control.maximum));
        QCOMPARE(changed.count(), 1);
        QVERIFY(property.write(&experience, -50));
        QCOMPARE(property.read(&experience).toInt(), control.minimum);
        QVERIFY(property.write(&experience, 999));
        QCOMPARE(property.read(&experience).toInt(),
                 QByteArray(control.name) == "materialMode" ? 0 : control.maximum);
        QVERIFY(property.write(&experience, control.maximum));
        PlayerExperienceController reloaded;
        QCOMPARE(reloaded.property(control.name).toInt(), control.maximum);
        QSettings settings;
        const QString key = QStringLiteral("immersiveVisual/") + control.name;
        settings.setValue(key, QStringLiteral("invalid"));
        PlayerExperienceController invalid;
        QCOMPARE(invalid.property(control.name).toInt(), control.fallback);
        settings.setValue(key, -99);
        PlayerExperienceController clamped;
        QCOMPARE(clamped.property(control.name).toInt(), control.minimum);
    }
}

void PlayerExperienceControllerTest::materialPresetsRestoreAfterInk()
{
    QSettings().clear();
    PlayerExperienceController experience;
    QVERIFY2(experience.metaObject()->indexOfProperty("materialMode") >= 0,
             "materialMode property missing");
    const char* keys[] = {"materialMode", "materialSoftness", "jellyElasticity",
                         "inkDensity", "rippleStrength", "rippleWidth", "rippleDecay"};
    const int modes[] = {0, 1, 2, 1, 0, 0, 1, 1, 0};
    QSet<QString> configurations;
    for (int preset = 0; preset < 9; ++preset) {
        QVERIFY(experience.applyPreset(preset));
        QVariantList expected;
        QString signature;
        for (const auto* key : keys) {
            QVERIFY2(experience.metaObject()->indexOfProperty(key) >= 0, key);
            expected.append(experience.property(key));
            signature += experience.property(key).toString() + '/';
        }
        configurations.insert(signature);
        QCOMPARE(experience.property("materialMode").toInt(), modes[preset]);
        const QString color = experience.coolColor();
        const bool cycling = experience.themeCycleEnabled();
        for (int repeat = 0; repeat < 3; ++repeat) {
            QVERIFY(experience.applyPreset(PlayerExperienceController::InkWash));
            QCOMPARE(experience.baseColor(), QStringLiteral("#F4F1E8"));
            QVERIFY(!experience.floatingCubesEnabled());
            QVERIFY(!experience.meteorsEnabled());
            QVERIFY(!experience.themeCycleEnabled());
            QVERIFY(experience.applyPreset(preset));
            for (int index = 0; index < 7; ++index)
                QCOMPARE(experience.property(keys[index]), expected[index]);
            QCOMPARE(experience.coolColor(), color);
            QCOMPARE(experience.themeCycleEnabled(), cycling);
        }
    }
    QCOMPARE(configurations.size(), 9);
}

void PlayerExperienceControllerTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("AgPlayer"));
    QCoreApplication::setApplicationName(
        QStringLiteral("AgPlayer-player-experience-controller-test"));
    QSettings().clear();
}

void PlayerExperienceControllerTest::defaultPresetDoesNotOverrideSongPaletteWithTimeCycling()
{
    QSettings().clear();
    PlayerExperienceController experience;
    QVERIFY(experience.applyPreset(PlayerExperienceController::NeonRainNight));
    QVERIFY(experience.themeCycleEnabled());
    QVERIFY(experience.applyPreset(PlayerExperienceController::AudioRangeEcho));
    QVERIFY2(!experience.themeCycleEnabled(),
             "Default song colors are overwritten by time-based rainbow cycling");
    QCOMPARE(experience.colorMode(), 0);
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
    QCOMPARE(experience.coolColor(), QStringLiteral("#8BDCFF"));
    QCOMPARE(experience.warmColor(), QStringLiteral("#EB7894"));
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
    QCOMPARE(malformed.coolColor(), QStringLiteral("#8BDCFF"));
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
    QCOMPARE(experience.coolColor(), QStringLiteral("#8BDCFF"));
    QCOMPARE(experience.warmColor(), QStringLiteral("#EB7894"));
    QCOMPARE(experience.accentColor(), QStringLiteral("#FFD7DF"));
    QCOMPARE(experience.peakColor(), QStringLiteral("#FFF7FB"));
    QVERIFY(experience.burstEnabled());
    QVERIFY(experience.streamHighlightEnabled());
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
        PlayerExperienceController::MultiSourceNeon,
        PlayerExperienceController::DeepSeaSoftWave,
        PlayerExperienceController::AmberCinema,
    };
    QCOMPARE(PlayerExperienceController::AudioRangeEcho, 0);
    QCOMPARE(PlayerExperienceController::NeonRainNight, 1);
    QCOMPARE(PlayerExperienceController::InkWash, 2);
    QCOMPARE(PlayerExperienceController::PureStage, 3);
    QCOMPARE(PlayerExperienceController::Quiet, 4);
    QCOMPARE(PlayerExperienceController::Galaxy, 5);
    QCOMPARE(PlayerExperienceController::MultiSourceNeon, 6);
    QCOMPARE(PlayerExperienceController::DeepSeaSoftWave, 7);
    QCOMPARE(PlayerExperienceController::AmberCinema, 8);
    QList<QVariantList> snapshots;

    for (const int preset : presets) {
        experience.setSongAdaptiveColorEnabled(true);
        QVERIFY(experience.applyPreset(preset));
        QVERIFY2(!experience.songAdaptiveColorEnabled(),
                 "Selecting a preset must display its own palette, not the track-derived palette");
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
        {0, "#8BDCFF", "#EB7894", "#FFD7DF", "#FFF7FB", "#050206", 62, 56, 74,
         38, 0.30, 54, 58, true, true, true, true, false,
         QVariantList({90, 92, 50, 50, 50, 50, 50, 48}), 82, 136, 100, 64, 30, 86,
         112, 42, 80},
        {2, "#7F5CFF", "#FF4FD8", "#22F0FF", "#F7F2FF", "#070310", 70, 80, 84,
         58, 0.48, 64, 72, true, true, true, false, true,
         QVariantList({92, 84, 58, 48, 54, 72, 96, 100}), 84, 144, 178, 68, 106, 94,
         108, 48, 84},
        {1, "#19282B", "#343B3B", "#B4CDCA", "#7F8D89", "#F4F1E8", 48, 36, 42,
         22, 0.12, 30, 36, true, false, false, true, false,
         QVariantList({62, 58, 54, 50, 48, 44, 42, 40}), 88, 122, 160, 48, 82, 116,
         120, 30, 72},
        {1, "#8EDFFF", "#D9B9FF", "#9EF2D1", "#F7FFFF", "#0B1117", 52, 48, 36,
         24, 0.18, 34, 42, true, false, false, false, false,
         QVariantList({70, 68, 62, 58, 58, 62, 68, 72}), 80, 134, 166, 56, 92, 70,
         126, 34, 80},
        {0, "#5264D9", "#B89CFF", "#46C7BC", "#DAF3EE", "#0A1018", 28, 22, 30,
         16, 0.08, 26, 24, true, false, false, true, false,
         QVariantList({48, 46, 44, 42, 42, 40, 38, 36}), 92, 108, 154, 46, 64, 112,
         106, 26, 66},
        {2, "#44D9FF", "#FF4FA7", "#FF8A45", "#FFF1D1", "#05030D", 72, 68, 76,
         52, 0.45, 58, 68, true, true, true, true, true,
         QVariantList({96, 88, 66, 54, 58, 76, 94, 100}), 80, 140, 182, 64, 104, 96,
         110, 46, 84},
        {2, "#38D8FF", "#FF5A9D", "#8A7CFF", "#F8F4FF", "#03040B", 58, 62, 82,
         46, 0.22, 46, 64, true, true, true, true, false,
         QVariantList({92, 86, 66, 58, 62, 76, 88, 94}), 82, 138, 168, 62, 74, 82,
         118, 40, 82},
        {1, "#174C78", "#2EC4B6", "#78DCE8", "#E9FDFF", "#02070C", 44, 38, 58,
         28, 0.10, 30, 42, true, false, false, true, false,
         QVariantList({78, 74, 68, 60, 52, 48, 44, 40}), 88, 118, 174, 52, 58, 108,
         116, 24, 70},
        {1, "#8A3D22", "#E6813B", "#FFC66D", "#FFF1C2", "#090502", 56, 48, 66,
         34, 0.18, 34, 52, true, true, true, true, false,
         QVariantList({88, 84, 72, 62, 54, 48, 44, 42}), 84, 126, 162, 60, 66, 94,
         120, 28, 74},
    };
    QCOMPARE(snapshots, expected);
    QVERIFY(!experience.applyPreset(-1));
}

void PlayerExperienceControllerTest::presetsMatchFinalHtmlContract()
{
    QSettings().clear();
    PlayerExperienceController experience;

    struct Expected {
        int preset;
        int compression;
        int audioResponse;
        int responseRange;
        int centerHighlight;
        int rhythmStrength;
        int depthOfField;
        int subjectClarity;
        int autoRotateSpeed;
        int rhythmSensitivity;
    };
    const std::array expected{
        Expected{0, 82, 136, 100, 64, 30, 86, 112, 42, 80},
        Expected{1, 84, 144, 178, 68, 106, 94, 108, 48, 84},
        Expected{2, 88, 122, 160, 48, 82, 116, 120, 30, 72},
        Expected{3, 80, 134, 166, 56, 92, 70, 126, 34, 80},
        Expected{4, 92, 108, 154, 46, 64, 112, 106, 26, 66},
        Expected{5, 80, 140, 182, 64, 104, 96, 110, 46, 84},
    };

    for (const Expected& values : expected) {
        QVERIFY(experience.applyPreset(values.preset));
        QCOMPARE(experience.inputCompression(), values.compression);
        QCOMPARE(experience.audioResponse(), values.audioResponse);
        QCOMPARE(experience.responseRange(), values.responseRange);
        QCOMPARE(experience.centerHighlight(), values.centerHighlight);
        QCOMPARE(experience.rhythmStrength(), values.rhythmStrength);
        QCOMPARE(experience.depthOfField(), values.depthOfField);
        QCOMPARE(experience.subjectClarity(), values.subjectClarity);
        QCOMPARE(experience.autoRotateSpeed(), values.autoRotateSpeed);
        QCOMPARE(experience.rhythmSensitivity(), values.rhythmSensitivity);
        QVERIFY(experience.burstEnabled());
        QVERIFY(experience.streamHighlightEnabled());
    }
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
    QCOMPARE(malformed.terrainAmplitude(), 42);
    QCOMPARE(malformed.qualityPreset(), 0);
    QCOMPARE(malformed.cinemaShake(), 0.40);
    QVERIFY(malformed.panelVisible());
    QCOMPARE(settings.value(QStringLiteral("immersiveVisual/terrainAmplitude")),
             QVariant(42));
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

    settings.clear();
    settings.setValue(QStringLiteral("immersiveVisual/qualityPreset"), 4);
    PlayerExperienceController legacyUltra;
    QCOMPARE(legacyUltra.qualityPreset(), PlayerExperienceController::Ultra);
    QCOMPARE(settings.value(QStringLiteral("immersiveVisual/qualityPreset")),
             QVariant(PlayerExperienceController::Ultra));
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

void PlayerExperienceControllerTest::cyclesClassicIntegratedAndIndependentImmersiveTheme()
{
    QSettings().clear();
    SettingsController settings;
    PlayerExperienceController experience(&settings);

    QCOMPARE(settings.playerShellMode(), 0);
    QCOMPARE(experience.immersiveMode(), PlayerExperienceController::Off);

    experience.cycleExperienceTheme();
    QCOMPARE(settings.playerShellMode(), 1);
    QCOMPARE(experience.immersiveMode(), PlayerExperienceController::Off);

    experience.cycleExperienceTheme();
    QCOMPARE(settings.playerShellMode(), 1);
    QCOMPARE(experience.hostMode(), PlayerExperienceController::Windowed);
    QCOMPARE(experience.immersiveMode(), PlayerExperienceController::TerrainReactor);

    experience.cycleExperienceTheme();
    // Returning from the independent immersive window must restore the
    // window theme that launched it. Switching to the classic shell here
    // loads a different persisted geometry and makes the player jump in size.
    QCOMPARE(settings.playerShellMode(), 1);
    QCOMPARE(experience.immersiveMode(), PlayerExperienceController::Off);
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
        for (int index = 0; index < 22; ++index) spectrum[index] = baseline;
        features.processSpectrum(spectrum);
        for (int index = 0; index < 22; ++index) spectrum[index] = 0.2;
        features.processSpectrum(spectrum);
        return features.kickPulse();
    };
    QVERIFY(!hasKick(0.151)); // Positive delta is just below 0.05.
    QVERIFY(hasKick(0.150));  // Positive delta is exactly 0.05.
    QVERIFY(hasKick(0.149));  // Positive delta is just above 0.05.

    const auto hasSnare = [](double baseline) {
        AudioVisualFeatureController features;
        QVariantList spectrum(128, 0.0);
        features.setActive(true);
        for (int index = 36; index < 96; ++index) spectrum[index] = baseline;
        features.processSpectrum(spectrum);
        for (int index = 36; index < 96; ++index) spectrum[index] = 0.2;
        features.processSpectrum(spectrum);
        return features.snarePulse();
    };
    QVERIFY(!hasSnare(0.161)); // Positive delta is just below 0.04.
    QVERIFY(hasSnare(0.160));  // Positive delta is exactly 0.04.
    QVERIFY(hasSnare(0.159));  // Positive delta is just above 0.04.
}

QTEST_MAIN(PlayerExperienceControllerTest)
#include "player_experience_controller_test.moc"
