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
    void columnLightingPersistsClampsAndSurvivesPresets();
    void rainbowColumnModePersistsAndRestoresOtherPresets();
    void columnControlsNormalizeNotifyAndPersist();
    void columnPresetsRestoreIndependentControls();
    void materialControlsNormalizeNotifyAndPersist();
    void materialPresetsRestoreAfterInk();
    void defaultPresetDoesNotOverrideSongPaletteWithTimeCycling();
    void freshInstallUsesAudioRangeEchoDefaults();
    void invalidStoredVisualValuesUseAudioRangeEchoDefaults();
    void storedColumnDensityOverridesTheFreshInstallDefault();
    void screenshotPresetValues();
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

void PlayerExperienceControllerTest::columnLightingPersistsClampsAndSurvivesPresets()
{
    struct Setting { const char* name; int minimum; int fallback; };
    const Setting settings[] = {{"columnInnerLight", 0, 155},
                                {"columnLightSpill", 0, 176},
                                {"columnLightRadius", 20, 86}};
    QSettings().clear();
    for (const auto& setting : settings) {
        PlayerExperienceController experience;
        const int index = experience.metaObject()->indexOfProperty(setting.name);
        QVERIFY2(index >= 0, setting.name);
        const auto property = experience.metaObject()->property(index);
        QCOMPARE(property.read(&experience).toInt(), setting.fallback);
        QSignalSpy changed(&experience, property.notifySignal());
        QVERIFY(changed.isValid());
        QVERIFY(property.write(&experience, -10));
        QCOMPARE(property.read(&experience).toInt(), setting.minimum);
        QCOMPARE(changed.count(), 1);
        QVERIFY(property.write(&experience, -10));
        QCOMPARE(changed.count(), 1);
        QVERIFY(property.write(&experience, 999));
        QCOMPARE(property.read(&experience).toInt(), 200);
        QCOMPARE(changed.count(), 2);
        QVERIFY(property.write(&experience, 137));
        PlayerExperienceController restored;
        QCOMPARE(restored.property(setting.name).toInt(), 137);
        QSettings storage;
        const QString key = QStringLiteral("immersiveVisual/") + setting.name;
        storage.setValue(key, -10);
        PlayerExperienceController low;
        QCOMPARE(low.property(setting.name).toInt(), setting.minimum);
        storage.setValue(key, 999);
        PlayerExperienceController high;
        QCOMPARE(high.property(setting.name).toInt(), 200);
        storage.setValue(key, QStringLiteral("invalid"));
        PlayerExperienceController invalid;
        QCOMPARE(invalid.property(setting.name).toInt(), setting.fallback);
        storage.remove(key);
    }
}

void PlayerExperienceControllerTest::rainbowColumnModePersistsAndRestoresOtherPresets()
{
    QSettings().clear();
    PlayerExperienceController experience;
    QSignalSpy changed(&experience, &PlayerExperienceController::colorModeChanged);
    experience.setColorMode(3);
    QCOMPARE(experience.colorMode(), 3);
    QCOMPARE(changed.count(), 1);
    experience.setColorMode(3);
    QCOMPARE(changed.count(), 1);
    PlayerExperienceController reloaded;
    QCOMPARE(reloaded.colorMode(), 3);
    const int modes[] = {0, 2, 1, 1, 0, 2, 3, 1, 1};
    for (int preset = 0; preset < 9; ++preset) {
        QVERIFY(experience.applyPreset(6));
        QCOMPARE(experience.colorMode(), 3);
        QVERIFY(experience.applyPreset(preset));
        QCOMPARE(experience.colorMode(), modes[preset]);
        PlayerExperienceController restored;
        QCOMPARE(restored.colorMode(), modes[preset]);
    }
    experience.setColorMode(4);
    QCOMPARE(experience.colorMode(), 0);
    QSettings().setValue(QStringLiteral("immersiveVisual/colorMode"), 4);
    PlayerExperienceController invalid;
    QCOMPARE(invalid.colorMode(), 0);
}

void PlayerExperienceControllerTest::columnControlsNormalizeNotifyAndPersist()
{
    struct Control { const char* name; int minimum; int maximum; int fallback; };
    const Control controls[] = {
        {"columnDensity", 50, 200, 50},
        {"columnSize", 50, 200, 95}, {"columnOpacity", 0, 100, 100},
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
        QCOMPARE(experience.terrainAmplitude(), 34);
        QCOMPARE(experience.subjectClarity(), 114);
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
    const int sizes[] = {95, 90, 80, 100, 110, 90, 85, 105, 105};
    const int brightness[] = {100, 99, 92, 131, 187, 110, 71, 153, 71};
    const int density[] = {50, 50, 50, 50, 200, 170, 200, 200, 200};
    for (int preset = 0; preset < 9; ++preset) {
        experience.setProperty("columnDensity", 175);
        for (const char* key : keys) QVERIFY(experience.setProperty(key, 51));
        QVERIFY(experience.applyPreset(preset));
        QCOMPARE(experience.property("columnDensity").toInt(), density[preset]);
        QCOMPARE(experience.property("columnSize").toInt(), sizes[preset]);
        QCOMPARE(experience.property("columnOpacity").toInt(), 100);
        QCOMPARE(experience.property("reactorBrightness").toInt(), brightness[preset]);
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
        {"rippleStrength", 0, 200, 39}, {"rippleWidth", 20, 200, 82},
        {"rippleDecay", 20, 200, 81},
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


void PlayerExperienceControllerTest::screenshotPresetValues()
{
    QSettings().clear();
    const char* keys[] = {"rippleStrength", "rippleWidth", "rippleDecay", "columnDensity", "terrainAmplitude", "subjectClarity", "inputCompression", "audioResponse", "peakBoost", "responseRange", "reactorBrightness", "columnInnerLight", "columnLightSpill", "columnLightRadius", "centerHighlight", "glowIntensity", "depthOfField", "autoRotateSpeed", "cinemaShake", "songAdaptiveColorEnabled", "rhythmSensitivity"};
    const int expected[9][21] = {
        {39, 82, 81, 50, 34, 114, 99, 121, 55, 129, 100, 155, 176, 86, 40, 100, 83, 78, 81, 1, 80},
        {41, 57, 81, 50, 40, 65, 108, 127, 62, 122, 99, 152, 94, 121, 31, 46, 69, 72, 43, 1, 84},
        {85, 107, 73, 50, 46, 120, 76, 127, 56, 160, 92, 52, 86, 88, 39, 77, 98, 61, 80, 0, 72},
        {139, 163, 99, 50, 26, 110, 107, 135, 64, 169, 131, 171, 125, 122, 55, 73, 70, 100, 119, 1, 80},
        {26, 142, 125, 200, 57, 104, 130, 73, 55, 93, 187, 134, 125, 166, 74, 80, 135, 100, 8, 0, 66},
        {155, 103, 119, 170, 29, 110, 80, 140, 68, 164, 110, 174, 125, 130, 64, 52, 116, 93, 116, 0, 95},
        {91, 128, 115, 200, 67, 75, 92, 110, 49, 104, 71, 108, 60, 81, 62, 46, 56, 100, 95, 0, 82},
        {65, 165, 147, 200, 28, 110, 135, 118, 70, 140, 153, 136, 79, 113, 66, 28, 108, 100, 99, 0, 70},
        {78, 125, 90, 200, 52, 89, 84, 142, 94, 220, 71, 176, 131, 167, 80, 76, 129, 100, 111, 0, 74},
    };
    PlayerExperienceController experience;
    for (int preset = 0; preset < 9; ++preset) {
        QVERIFY(experience.applyPreset(preset));
        for (int field = 0; field < 21; ++field) {
            const double value = experience.property(keys[field]).toDouble();
            const double expectedValue = field == 18 ? expected[preset][field] / 100.0 : expected[preset][field];
            QVERIFY2(qAbs(value - expectedValue) < 0.00001,
                     qPrintable(QString("preset %1 %2: got %3 expected %4").arg(preset).arg(keys[field]).arg(value).arg(expectedValue)));
        }
        PlayerExperienceController restored;
        for (const char* key : keys) QCOMPARE(restored.property(key), experience.property(key));
    }
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

void PlayerExperienceControllerTest::freshInstallUsesAudioRangeEchoDefaults()
{
    QSettings().clear();
    PlayerExperienceController experience;

    QCOMPARE(experience.columnDensity(), 50);
    QCOMPARE(experience.columnSize(), 95);
    QCOMPARE(experience.columnOpacity(), 100);
    QCOMPARE(experience.reactorBrightness(), 100);
    QCOMPARE(experience.materialMode(), 0);
    QCOMPARE(experience.materialSoftness(), 45);
    QCOMPARE(experience.jellyElasticity(), 35);
    QCOMPARE(experience.inkDensity(), 60);
    QCOMPARE(experience.rippleStrength(), 39);
    QCOMPARE(experience.rippleWidth(), 82);
    QCOMPARE(experience.rippleDecay(), 81);
    QCOMPARE(experience.terrainAmplitude(), 34);
    QCOMPARE(experience.motionResponse(), 56);
    QCOMPARE(experience.gradientLayers(), 74);
    QCOMPARE(experience.glowIntensity(), 100);
    QCOMPARE(experience.cinemaShake(), 0.81);
    QCOMPARE(experience.autoRotate(), 54);
    QCOMPARE(experience.peakBoost(), 55);
    QCOMPARE(experience.visualEqGains(),
             QVariantList({90, 92, 50, 50, 50, 50, 50, 48}));
    QCOMPARE(experience.inputCompression(), 99);
    QCOMPARE(experience.audioResponse(), 121);
    QCOMPARE(experience.responseRange(), 129);
    QCOMPARE(experience.centerHighlight(), 40);
    QCOMPARE(experience.rhythmStrength(), 30);
    QCOMPARE(experience.depthOfField(), 83);
    QCOMPARE(experience.subjectClarity(), 114);
    QCOMPARE(experience.autoRotateSpeed(), 78);
    QCOMPARE(experience.rhythmSensitivity(), 80);
    QCOMPARE(experience.colorMode(), PlayerExperienceController::MultiRegion);
    QCOMPARE(experience.coolColor(), QStringLiteral("#5276E8"));
    QCOMPARE(experience.warmColor(), QStringLiteral("#F58DAD"));
    QCOMPARE(experience.accentColor(), QStringLiteral("#A880ED"));
    QCOMPARE(experience.peakColor(), QStringLiteral("#F5DBEC"));
    QCOMPARE(experience.baseColor(), QStringLiteral("#040A1C"));
    QVERIFY(experience.floatingCubesEnabled());
    QVERIFY(experience.meteorsEnabled());
    QVERIFY(experience.ripplesEnabled());
    QVERIFY(experience.burstEnabled());
    QVERIFY(experience.idleBreathingEnabled());
    QVERIFY(experience.streamHighlightEnabled());
    QVERIFY(!experience.themeCycleEnabled());
    QVERIFY(experience.songAdaptiveColorEnabled());
}

void PlayerExperienceControllerTest::invalidStoredVisualValuesUseAudioRangeEchoDefaults()
{
    QSettings settings;
    settings.clear();
    const QString group = QStringLiteral("immersiveVisual/");
    const QString invalid = QStringLiteral("invalid");
    for (const char* key : {"columnSize", "columnDensity", "columnOpacity",
                            "terrainAmplitude", "motionResponse", "cinemaShake",
                            "audioResponse", "centerHighlight", "subjectClarity",
                            "rhythmSensitivity", "songAdaptiveColorEnabled"}) {
        settings.setValue(group + QLatin1String(key), invalid);
    }

    PlayerExperienceController experience;
    QCOMPARE(experience.columnSize(), 95);
    QCOMPARE(experience.columnDensity(), 50);
    QCOMPARE(experience.columnOpacity(), 100);
    QCOMPARE(experience.terrainAmplitude(), 34);
    QCOMPARE(experience.motionResponse(), 56);
    QCOMPARE(experience.cinemaShake(), 0.81);
    QCOMPARE(experience.audioResponse(), 121);
    QCOMPARE(experience.centerHighlight(), 40);
    QCOMPARE(experience.subjectClarity(), 114);
    QCOMPARE(experience.rhythmSensitivity(), 80);
    QVERIFY(experience.songAdaptiveColorEnabled());
}

void PlayerExperienceControllerTest::storedColumnDensityOverridesTheFreshInstallDefault()
{
    QSettings settings;
    settings.clear();
    settings.setValue(QStringLiteral("immersiveVisual/columnDensity"), 87);

    PlayerExperienceController experience;
    QCOMPARE(experience.columnDensity(), 87);
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
    QCOMPARE(experience.coolColor(), QStringLiteral("#5276E8"));
    QCOMPARE(experience.warmColor(), QStringLiteral("#F58DAD"));
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
    QCOMPARE(malformed.coolColor(), QStringLiteral("#5276E8"));
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
    QCOMPARE(experience.inputCompression(), 99);
    QCOMPARE(experience.audioResponse(), 121);
    QCOMPARE(experience.responseRange(), 129);
    QCOMPARE(experience.centerHighlight(), 40);
    QCOMPARE(experience.rhythmStrength(), 30);
    QCOMPARE(experience.depthOfField(), 83);
    QCOMPARE(experience.subjectClarity(), 114);
    QCOMPARE(experience.autoRotateSpeed(), 78);
    QCOMPARE(experience.rhythmSensitivity(), 80);
    QCOMPARE(experience.coolColor(), QStringLiteral("#5276E8"));
    QCOMPARE(experience.warmColor(), QStringLiteral("#F58DAD"));
    QCOMPARE(experience.accentColor(), QStringLiteral("#A880ED"));
    QCOMPARE(experience.peakColor(), QStringLiteral("#F5DBEC"));
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
        QCOMPARE(experience.songAdaptiveColorEnabled(), preset == 0 || preset == 1 || preset == 3);
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
    QList<QVariantList> expected = {
        {0, "#5276E8", "#F58DAD", "#A880ED", "#F5DBEC", "#040A1C", 34, 56, 74,
         100, 0.81, 54, 55, true, true, true, true, false, QVariantList({90, 92, 50, 50, 50, 50, 50, 48}),
         99, 121, 129, 40, 30, 83, 114, 78, 80},
        {2, "#5554D8", "#EF5AAE", "#36D9DF", "#DFEBFA", "#070B1B", 40, 80, 84,
         46, 0.43, 64, 62, true, true, true, false, true, QVariantList({92, 84, 58, 48, 54, 72, 96, 100}),
         108, 127, 122, 31, 106, 69, 65, 72, 84},
        {1, "#203C3D", "#58655E", "#93B6A7", "#C4D6C8", "#F4F1E8", 46, 36, 42,
         77, 0.80, 30, 56, true, false, false, true, false, QVariantList({62, 58, 54, 50, 48, 44, 42, 40}),
         76, 127, 160, 39, 82, 98, 120, 61, 72},
        {1, "#76BFD7", "#E7B8A6", "#9ED5C0", "#ECF6EF", "#08141C", 26, 48, 36,
         73, 1.19, 34, 64, true, false, false, false, false, QVariantList({70, 68, 62, 58, 58, 62, 68, 72}),
         107, 135, 169, 55, 92, 70, 110, 100, 80},
        {0, "#637EA3", "#BAA5CA", "#76B7B1", "#D6E9E4", "#09151D", 57, 22, 30,
         80, 0.08, 26, 55, true, false, false, true, false, QVariantList({48, 46, 44, 42, 42, 40, 38, 36}),
         130, 73, 93, 74, 64, 135, 104, 100, 66},
        {2, "#4B70D2", "#C567B5", "#E8AD75", "#DCE5FA", "#050918", 29, 68, 76,
         52, 1.16, 58, 68, true, true, true, true, true, QVariantList({96, 88, 66, 54, 58, 76, 94, 100}),
         80, 140, 164, 64, 104, 116, 110, 93, 95},
        {3, "#39CFE0", "#EF70A5", "#9778E8", "#F3DEEB", "#050718", 67, 62, 82,
         46, 0.95, 46, 49, true, true, true, true, false, QVariantList({92, 86, 66, 58, 62, 76, 88, 94}),
         92, 110, 104, 62, 74, 56, 75, 100, 82},
        {1, "#245785", "#3DB4B1", "#7979B8", "#CAE5E8", "#030C17", 28, 38, 58,
         28, 0.99, 30, 70, true, false, false, true, false, QVariantList({78, 74, 68, 60, 52, 48, 44, 40}),
         135, 118, 140, 66, 58, 108, 110, 100, 70},
        {1, "#285A62", "#DC984E", "#EFBA72", "#F5DFC0", "#0C1014", 52, 48, 66,
         76, 1.11, 34, 94, true, true, true, true, false, QVariantList({88, 84, 72, 62, 54, 48, 44, 42}),
         84, 142, 220, 80, 66, 129, 89, 100, 74},
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
    // Latest screenshot values override pictured HTML parameters; rhythmStrength was not pictured.
    const std::array expected{
        Expected{0, 99, 121, 129, 40, 30, 83, 114, 78, 80},
        Expected{1, 108, 127, 122, 31, 106, 69, 65, 72, 84},
        Expected{2, 76, 127, 160, 39, 82, 98, 120, 61, 72},
        Expected{3, 107, 135, 169, 55, 92, 70, 110, 100, 80},
        Expected{4, 130, 73, 93, 74, 64, 135, 104, 100, 66},
        Expected{5, 80, 140, 164, 64, 104, 116, 110, 93, 95},
        Expected{6, 92, 110, 104, 62, 74, 56, 75, 100, 82},
        Expected{7, 135, 118, 140, 66, 58, 108, 110, 100, 70},
        Expected{8, 84, 142, 220, 80, 66, 129, 89, 100, 74},
    };

    for (const Expected& values : expected) {
        QVERIFY(experience.applyPreset(values.preset));
        const int colorModes[] = {0, 2, 1, 1, 0, 2, 3, 1, 1};
        QCOMPARE(experience.colorMode(), colorModes[values.preset]);
        QCOMPARE(experience.inputCompression(), values.compression);
        QCOMPARE(experience.audioResponse(), values.audioResponse);
        QCOMPARE(experience.responseRange(), values.responseRange);
        QCOMPARE(experience.centerHighlight(), values.centerHighlight);
        QCOMPARE(experience.rhythmStrength(), values.rhythmStrength);
        QCOMPARE(experience.depthOfField(), values.depthOfField);
        QCOMPARE(experience.subjectClarity(), values.subjectClarity);
        QCOMPARE(experience.autoRotateSpeed(), values.autoRotateSpeed);
        QCOMPARE(experience.rhythmSensitivity(), values.rhythmSensitivity);
        QCOMPARE(experience.burstEnabled(), values.preset < 6);
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
    QCOMPARE(malformed.terrainAmplitude(), 34);
    QCOMPARE(malformed.qualityPreset(), 0);
    QCOMPARE(malformed.cinemaShake(), 0.81);
    QVERIFY(malformed.panelVisible());
    QCOMPARE(settings.value(QStringLiteral("immersiveVisual/terrainAmplitude")),
             QVariant(34));
    QCOMPARE(settings.value(QStringLiteral("immersiveVisual/qualityPreset")),
             QVariant(0));
    QCOMPARE(settings.value(QStringLiteral("immersiveVisual/cinemaShake")).toDouble(),
             0.81);
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
