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
#include <QStringList>
#include <QTest>
#include <QVariantMap>

class PlayerExperienceControllerTest final : public QObject {
    Q_OBJECT

private slots:
    void customPalettePreviewIsTransientAndIndependent()
    {
        QSettings().clear();
        PlayerExperienceController style;
        QVERIFY(style.applyTheme(QStringLiteral("nocturnal")));
        const auto fixedCool = style.coolColor();
        const auto saved = style.customColors();
        style.applyCustomColors();
        QVERIFY(style.previewCustomColor(QStringLiteral("coolColor"), QStringLiteral("#10F020")));
        QCOMPARE(style.coolColor(), QStringLiteral("#10F020"));
        QCOMPARE(style.customColors(), saved);
        PlayerExperienceController duringPreview;
        QCOMPARE(duringPreview.coolColor(), saved.value("coolColor").toString());
        style.applyCustomColors(); // Cancel.
        QCOMPARE(style.coolColor(), saved.value("coolColor").toString());
        QVERIFY(style.setCustomColor(QStringLiteral("coolColor"), QStringLiteral("#10F020")));
        QVERIFY(style.setCustomColor(QStringLiteral("baseColor"), QStringLiteral("#010203")));
        QVERIFY(style.applyTheme(QStringLiteral("nocturnal")));
        QCOMPARE(style.coolColor(), fixedCool);
        QCOMPARE(style.customColors().value("coolColor").toString(), QStringLiteral("#10F020"));
        PlayerExperienceController restored;
        QCOMPARE(restored.themeId(), QStringLiteral("nocturnal"));
        restored.applyCustomColors();
        QCOMPARE(restored.coolColor(), QStringLiteral("#10F020"));
        QCOMPARE(restored.themeBackground(), QColor("#010203"));
        const auto custom = restored.customColors();
        QVERIFY(!restored.setCustomColor(QStringLiteral("other"), QStringLiteral("#FFFFFF")));
        QVERIFY(!restored.previewCustomColor(QStringLiteral("coolColor"), QStringLiteral("invalid")));
        QVERIFY(!restored.setCustomColor(QStringLiteral("coolColor"), QStringLiteral("transparent")));
        QCOMPARE(restored.customColors(), custom);
        QSettings().setValue(QStringLiteral("immersiveVisual/themeId"), QStringLiteral("daybreak-lime"));
        PlayerExperienceController migrated;
        QCOMPARE(migrated.themeId(), QStringLiteral("violet-heart"));
        QCOMPARE(migrated.customColors(), custom);
    }
    void referenceDefaultsUpgradeOnceWithoutResettingLaterEdits()
    {
        QSettings().clear();
        PlayerExperienceController original;
        QVERIFY(original.applyTheme(QStringLiteral("nocturnal")));
        original.setTerrainAmplitude(38);
        original.setRippleWidth(120);
        original.setReactorBrightness(68);
        original.setColumnSize(140);
        original.setColumnOpacity(65);
        original.setAutoRotateSpeed(0);
        original.setIdleBreathingEnabled(false);
        QSettings().setValue(QStringLiteral("immersiveVisual/referenceDefaultsRevision"), 1);
        PlayerExperienceController upgraded;
        QCOMPARE(upgraded.terrainAmplitude(), 50);
        QCOMPARE(upgraded.rippleWidth(), 100);
        QCOMPARE(upgraded.reactorBrightness(), 100);
        QCOMPARE(upgraded.columnSize(), 100);
        QCOMPARE(upgraded.columnOpacity(), 100);
        QCOMPARE(upgraded.autoRotateSpeed(), 15);
        QVERIFY(upgraded.idleBreathingEnabled());
        QCOMPARE(upgraded.themeId(), QStringLiteral("nocturnal"));
        upgraded.setTerrainAmplitude(72);
        upgraded.setRippleWidth(145);
        PlayerExperienceController reloaded;
        QCOMPARE(reloaded.terrainAmplitude(), 72);
        QCOMPARE(reloaded.rippleWidth(), 145);
    }
    void visualEqEnabledPreservesGainsAndPersists()
    {
        QSettings().clear();
        PlayerExperienceController controller;
        const int index = controller.metaObject()->indexOfProperty("visualEqEnabled");
        QVERIFY(index >= 0);
        const auto property = controller.metaObject()->property(index);
        const QVariantList all{true,true,true,true,true,true,true,true};
        QCOMPARE(property.read(&controller).toList(), all);
        const auto gains = controller.visualEqGains();
        QVERIFY(property.write(&controller, QVariantList{false,true,false,true,true,true,true,false}));
        QCOMPARE(controller.visualEqGains(), gains);
        PlayerExperienceController reloaded;
        QCOMPARE(property.read(&reloaded).toList(), QVariantList({false,true,false,true,true,true,true,false}));
        QVERIFY(property.write(&controller, QVariantList{false, QStringLiteral("invalid")}));
        QCOMPARE(property.read(&controller).toList(), QVariantList({false,true,true,true,true,true,true,true}));
    }
    void columnLightingPersistsClampsAndSurvivesPresets();
    void colorModePersistsAndClamps();
    void columnControlsNormalizeNotifyAndPersist();
    void materialControlsNormalizeNotifyAndPersist();
    void freshInstallUsesMinimalMonochromeTheme();
    void invalidStoredVisualValuesUseSafeDefaults();
    void storedColumnDensityOverridesTheFreshInstallDefault();
    void topographyDensityIsIndependentPersistentAndThemeStable();
    void themeRotationSettingsDefaultPersistAndClamp();
    void initTestCase();
    void defaultsAreIndependent();
    void persistsAndNormalizesValues();
    void legacyActivePresentationNeverReopensOnStartup();
    void defaultsExposeV46ExperienceControls();
    void clampsPersistsAndNotifiesV46ExperienceControls();
    void persistsAndClampsLyricPlacement();
    void strictlyParsesPersistedScalarTypes();
    void togglesOnlyTheLayoutShellSetting();
    void cyclesClassicIntegratedAndIndependentImmersiveTheme();
    void derivesBandsAndEvents();
    void followsPlaybackSpectrumOnlyWhileActive();
    void eventThresholdsIncludeBoundaries();
    void builtInThemesApplyWithoutDynamicPresetChanges();
    void restoresDynamicDefaultsWithoutChangingTheme();
    void invalidThemeDoesNotChangeState();
    void themePersistsAndLegacyColorsRemainManual();
    void legacyNinePresetApiIsRemoved();
    void manualColorClearsTheme();
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

void PlayerExperienceControllerTest::colorModePersistsAndClamps()
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
        {"columnSize", 50, 200, 100}, {"columnOpacity", 0, 100, 100},
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
        QCOMPARE(experience.terrainAmplitude(), 50);
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

void PlayerExperienceControllerTest::materialControlsNormalizeNotifyAndPersist()
{
    struct Control { const char* name; int minimum; int maximum; int fallback; };
    const Control controls[] = {
        {"materialMode", 0, 2, 0}, {"materialSoftness", 0, 100, 45},
        {"jellyElasticity", 0, 100, 35}, {"inkDensity", 0, 100, 60},
        {"rippleStrength", 0, 200, 100}, {"rippleWidth", 20, 200, 100},
        {"rippleDecay", 20, 200, 100},
        {"floatingBlockMinSize", 0, 100, 9}, {"floatingBlockMaxSize", 0, 100, 26},
        {"floatingBlockSpeed", 0, 100, 77}, {"floatingBlockIntensity", 0, 100, 55},
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

void PlayerExperienceControllerTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("AgPlayer"));
    QCoreApplication::setApplicationName(
        QStringLiteral("AgPlayer-player-experience-controller-test"));
    QSettings().clear();
}

void PlayerExperienceControllerTest::freshInstallUsesMinimalMonochromeTheme()
{
    QSettings().clear();
    PlayerExperienceController experience;

    QCOMPARE(experience.columnDensity(), 50);
    QCOMPARE(experience.columnSize(), 100);
    QCOMPARE(experience.columnOpacity(), 100);
    QCOMPARE(experience.reactorBrightness(), 100);
    QCOMPARE(experience.materialMode(), 0);
    QCOMPARE(experience.materialSoftness(), 45);
    QCOMPARE(experience.jellyElasticity(), 35);
    QCOMPARE(experience.inkDensity(), 60);
    QCOMPARE(experience.rippleStrength(), 100);
    QCOMPARE(experience.rippleWidth(), 100);
    QCOMPARE(experience.rippleDecay(), 100);
    QCOMPARE(experience.terrainAmplitude(), 50);
    QCOMPARE(experience.motionResponse(), 50);
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
    QCOMPARE(experience.autoRotateSpeed(), 15);
    QCOMPARE(experience.rhythmSensitivity(), 100);
    QCOMPARE(experience.colorMode(), PlayerExperienceController::MultiRegion);
    QCOMPARE(experience.themeId(), QStringLiteral("minimal-monochrome"));
    QCOMPARE(experience.coolColor(), QStringLiteral("#F3F3F3"));
    QCOMPARE(experience.warmColor(), QStringLiteral("#FFFFFF"));
    QCOMPARE(experience.accentColor(), QStringLiteral("#FFFFFF"));
    QCOMPARE(experience.peakColor(), QStringLiteral("#DADADA"));
    QCOMPARE(experience.baseColor(), QStringLiteral("#272727"));
    QVERIFY(experience.floatingCubesEnabled());
    QVERIFY(experience.meteorsEnabled());
    QVERIFY(experience.ripplesEnabled());
    QVERIFY(experience.burstEnabled());
    QVERIFY(experience.idleBreathingEnabled());
    QVERIFY(experience.streamHighlightEnabled());
    QVERIFY(!experience.themeCycleEnabled());
    QVERIFY(!experience.themeSongCycleEnabled());
    QCOMPARE(experience.themeCycleIntervalSeconds(), 10);
    QVERIFY(!experience.songAdaptiveColorEnabled());
}

void PlayerExperienceControllerTest::themeRotationSettingsDefaultPersistAndClamp()
{
    QSettings settings;
    settings.clear();
    {
        PlayerExperienceController experience;
        QVERIFY(!experience.themeCycleEnabled());
        QVERIFY(!experience.themeSongCycleEnabled());
        QCOMPARE(experience.themeCycleIntervalSeconds(), 10);

        experience.setThemeCycleEnabled(false);
        experience.setThemeSongCycleEnabled(true);
        experience.setThemeCycleIntervalSeconds(1);
        QCOMPARE(experience.themeCycleIntervalSeconds(), 3);
        experience.setThemeCycleIntervalSeconds(999);
        QCOMPARE(experience.themeCycleIntervalSeconds(), 120);
        experience.setThemeCycleIntervalSeconds(27);
    }

    PlayerExperienceController reloaded;
    QVERIFY(!reloaded.themeCycleEnabled());
    QVERIFY(reloaded.themeSongCycleEnabled());
    QCOMPARE(reloaded.themeCycleIntervalSeconds(), 27);
}

void PlayerExperienceControllerTest::invalidStoredVisualValuesUseSafeDefaults()
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
    QCOMPARE(experience.columnSize(), 100);
    QCOMPARE(experience.columnDensity(), 50);
    QCOMPARE(experience.columnOpacity(), 100);
    QCOMPARE(experience.terrainAmplitude(), 50);
    QCOMPARE(experience.motionResponse(), 50);
    QCOMPARE(experience.cinemaShake(), 0.81);
    QCOMPARE(experience.audioResponse(), 121);
    QCOMPARE(experience.centerHighlight(), 40);
    QCOMPARE(experience.subjectClarity(), 114);
    QCOMPARE(experience.rhythmSensitivity(), 100);
    QVERIFY(!experience.songAdaptiveColorEnabled());
}

void PlayerExperienceControllerTest::storedColumnDensityOverridesTheFreshInstallDefault()
{
    QSettings settings;
    settings.clear();
    settings.setValue(QStringLiteral("immersiveVisual/columnDensity"), 87);
    settings.setValue(QStringLiteral("immersiveVisual/columnSize"), 95);
    settings.setValue(QStringLiteral("immersiveVisual/terrainAmplitude"), 34);
    settings.setValue(QStringLiteral("immersiveVisual/motionResponse"), 56);
    settings.setValue(QStringLiteral("immersiveVisual/songAdaptiveColorEnabled"), true);

    PlayerExperienceController experience;
    QCOMPARE(experience.columnDensity(), 87);
    QCOMPARE(experience.columnSize(), 95);
    QCOMPARE(experience.terrainAmplitude(), 34);
    QCOMPARE(experience.motionResponse(), 56);
    QVERIFY(experience.songAdaptiveColorEnabled());
}

void PlayerExperienceControllerTest::topographyDensityIsIndependentPersistentAndThemeStable()
{
    QSettings settings;
    settings.clear();
    PlayerExperienceController experience;
    QCOMPARE(experience.topographyDensity(), 46);
    QSignalSpy changed(&experience, &PlayerExperienceController::topographyDensityChanged);

    experience.setTopographyDensity(-1);
    QCOMPARE(experience.topographyDensity(), 0);
    experience.setTopographyDensity(101);
    QCOMPARE(experience.topographyDensity(), 100);
    QCOMPARE(changed.count(), 2);

    experience.setTopographyDensity(63);
    QCOMPARE(settings.value(QStringLiteral("immersiveVisual/topographyDensity")).toInt(), 63);
    QCOMPARE(experience.columnDensity(), 50);
    QVERIFY(experience.applyTheme(QStringLiteral("neon-tokyo")));
    QCOMPARE(experience.topographyDensity(), 63);

    PlayerExperienceController restored;
    QCOMPARE(restored.topographyDensity(), 63);
    settings.setValue(QStringLiteral("immersiveVisual/topographyDensity"), 101);
    PlayerExperienceController high;
    QCOMPARE(high.topographyDensity(), 100);
}

void PlayerExperienceControllerTest::legacyActivePresentationNeverReopensOnStartup()
{
    QSettings settings;
    settings.clear();
    settings.setValue(QStringLiteral("immersiveVisual/mode"), 1);
    settings.setValue(QStringLiteral("immersiveVisual/terrainAmplitude"), 67);
    settings.sync();
    PlayerExperienceController restored;
    QCOMPARE(restored.immersiveMode(), PlayerExperienceController::Off);
    QCOMPARE(restored.terrainAmplitude(), 67);
    restored.setImmersiveMode(PlayerExperienceController::TerrainReactor);
    QCOMPARE(restored.immersiveMode(), PlayerExperienceController::TerrainReactor);
    QVERIFY(!settings.contains(QStringLiteral("immersiveVisual/mode")));
    PlayerExperienceController restarted;
    QCOMPARE(restarted.immersiveMode(), PlayerExperienceController::Off);
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
    QVERIFY(!experience.songAdaptiveColorEnabled());
    QCOMPARE(experience.qualityPreset(), 0);
    QCOMPARE(experience.coolColor(), QStringLiteral("#F3F3F3"));
    QCOMPARE(experience.warmColor(), QStringLiteral("#FFFFFF"));
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
    QCOMPARE(reloaded.immersiveMode(), PlayerExperienceController::Off);
    QCOMPARE(reloaded.hostMode(), 2);
    QVERIFY(!reloaded.lyricsVisible());
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
    QCOMPARE(experience.autoRotateSpeed(), 15);
    QCOMPARE(experience.rhythmSensitivity(), 100);
    QCOMPARE(experience.coolColor(), QStringLiteral("#F3F3F3"));
    QCOMPARE(experience.warmColor(), QStringLiteral("#FFFFFF"));
    QCOMPARE(experience.accentColor(), QStringLiteral("#FFFFFF"));
    QCOMPARE(experience.peakColor(), QStringLiteral("#DADADA"));
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
    QCOMPARE(malformed.terrainAmplitude(), 50);
    QCOMPARE(malformed.qualityPreset(), 0);
    QCOMPARE(malformed.cinemaShake(), 0.81);
    QVERIFY(malformed.panelVisible());
    QCOMPARE(settings.value(QStringLiteral("immersiveVisual/terrainAmplitude")),
             QVariant(50));
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

void PlayerExperienceControllerTest::builtInThemesApplyWithoutDynamicPresetChanges()
{
    QSettings().clear();
    PlayerExperienceController experience;
    experience.setTerrainAmplitude(73);
    experience.setRippleStrength(147);
    experience.setMaterialSoftness(19);

    const QVariantList choices = experience.builtInThemeChoices();
    const QStringList expectedTitles = {
        QStringLiteral("水墨"), QStringLiteral("夜色"), QStringLiteral("东京霓虹"),
        QStringLiteral("赛博森林"), QStringLiteral("极简黑白"), QStringLiteral("冰川白昼"),
        QStringLiteral("锦鲤池"), QStringLiteral("珊瑚礁"), QStringLiteral("苔藓玻璃"),
        QStringLiteral("蓝调时刻"), QStringLiteral("青瓷"), QStringLiteral("绯红信号"),
        QStringLiteral("紫夜霓心"),
    };
    QCOMPARE(choices.size(), 13);
    for (qsizetype index = 0; index < choices.size(); ++index) {
        const QVariant& item = choices.at(index);
        const QVariantMap choice = item.toMap();
        const QString id = choice.value(QStringLiteral("id")).toString();
        QVERIFY(!id.isEmpty());
        QCOMPARE(choice.value(QStringLiteral("title")).toString(), expectedTitles.at(index));
        QVERIFY(choice.value(QStringLiteral("from")).toString().startsWith('#'));
        QVERIFY(choice.value(QStringLiteral("to")).toString().startsWith('#'));
        QVERIFY(choice.value(QStringLiteral("background")).toString().startsWith('#'));
        QVERIFY(experience.applyTheme(id));
        QCOMPARE(experience.themeId(), id);
        QCOMPARE(experience.themeBackground().name(QColor::HexRgb).toUpper(),
                 choice.value(QStringLiteral("background")).toString());
        QCOMPARE(experience.colorMode(), PlayerExperienceController::MultiRegion);
        QCOMPARE(experience.materialMode(), 0);
        QVERIFY(!experience.songAdaptiveColorEnabled());
        QCOMPARE(experience.terrainAmplitude(), 73);
        QCOMPARE(experience.rippleStrength(), 147);
        QCOMPARE(experience.materialSoftness(), 19);
    }
}

void PlayerExperienceControllerTest::invalidThemeDoesNotChangeState()
{
    QSettings().clear();
    PlayerExperienceController experience;
    QVERIFY(experience.applyTheme(QStringLiteral("wine-signal")));
    const QString id = experience.themeId();
    const QString cool = experience.coolColor();
    const QColor background = experience.themeBackground();
    const float glow = experience.themeGlow();
    QSignalSpy changed(&experience, &PlayerExperienceController::themeChanged);

    QVERIFY(!experience.applyTheme(QStringLiteral("not-a-theme")));
    QCOMPARE(experience.themeId(), id);
    QCOMPARE(experience.coolColor(), cool);
    QCOMPARE(experience.themeBackground(), background);
    QCOMPARE(experience.themeGlow(), glow);
    QCOMPARE(changed.count(), 0);
    QCOMPARE(QSettings().value(QStringLiteral("immersiveVisual/themeId")).toString(), id);
}

void PlayerExperienceControllerTest::restoresDynamicDefaultsWithoutChangingTheme()
{
    QSettings().clear();
    PlayerExperienceController experience;
    QVERIFY(experience.applyTheme(QStringLiteral("wine-signal")));
    const QString themeId = experience.themeId();
    const QString coolColor = experience.coolColor();

    experience.setRippleStrength(190);
    experience.setRippleWidth(190);
    experience.setRippleDecay(190);
    experience.setTerrainAmplitude(8);
    experience.setGlowIntensity(4);
    experience.setRhythmSensitivity(5);
    experience.setRipplesEnabled(false);
    experience.setStreamHighlightEnabled(false);
    experience.setVisualEqGains(QVariantList({1,2,3,4,5,6,7,8}));

    experience.restoreDynamicDefaults();

    QCOMPARE(experience.themeId(), themeId);
    QCOMPARE(experience.coolColor(), coolColor);
    QCOMPARE(experience.rippleStrength(), 100);
    QCOMPARE(experience.rippleWidth(), 100);
    QCOMPARE(experience.rippleDecay(), 100);
    QCOMPARE(experience.terrainAmplitude(), 50);
    QCOMPARE(experience.glowIntensity(), 100);
    QCOMPARE(experience.rhythmSensitivity(), 100);
    QVERIFY(experience.ripplesEnabled());
    QVERIFY(experience.streamHighlightEnabled());
    QCOMPARE(experience.visualEqGains(),
             QVariantList({90, 92, 50, 50, 50, 50, 50, 48}));
}

void PlayerExperienceControllerTest::themePersistsAndLegacyColorsRemainManual()
{
    QSettings settings;
    settings.clear();
    {
        PlayerExperienceController experience;
        QVERIFY(experience.applyTheme(QStringLiteral("glacier-day")));
    }
    PlayerExperienceController restored;
    QCOMPARE(restored.themeId(), QStringLiteral("glacier-day"));
    QCOMPARE(restored.themeBackground().name(QColor::HexRgb).toUpper(),
             QStringLiteral("#E5EEF0"));

    settings.clear();
    settings.setValue(QStringLiteral("immersiveVisual/coolColor"), QStringLiteral("#123456"));
    settings.setValue(QStringLiteral("immersiveVisual/warmColor"), QStringLiteral("#ABCDEF"));
    PlayerExperienceController legacy;
    QVERIFY(legacy.themeId().isEmpty());
    QCOMPARE(legacy.coolColor(), QStringLiteral("#123456"));
    QCOMPARE(legacy.warmColor(), QStringLiteral("#ABCDEF"));
    QVERIFY(!settings.contains(QStringLiteral("immersiveVisual/themeId")));
}

void PlayerExperienceControllerTest::legacyNinePresetApiIsRemoved()
{
    PlayerExperienceController experience;
    const QMetaObject* meta = experience.metaObject();
    QCOMPARE(meta->indexOfEnumerator("VisualPreset"), -1);
    QCOMPARE(meta->indexOfMethod("applyPreset(int)"), -1);
}

void PlayerExperienceControllerTest::manualColorClearsTheme()
{
    QSettings().clear();
    PlayerExperienceController experience;
    QVERIFY(experience.applyTheme(QStringLiteral("neon-tokyo")));
    experience.setAccentColor(QStringLiteral("#123456"));
    QVERIFY(experience.themeId().isEmpty());
    experience.setColorMode(PlayerExperienceController::RgbSweep);
    QCOMPARE(experience.colorMode(), PlayerExperienceController::RgbSweep);
}

QTEST_MAIN(PlayerExperienceControllerTest)
#include "player_experience_controller_test.moc"
