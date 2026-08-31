#include "settings_controller.hpp"
#include "frequency_color_waveform_settings.hpp"

#include <QByteArray>
#include <QColor>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

double linearChannel(double value)
{
    return value <= 0.04045 ? value / 12.92
                            : std::pow((value + 0.055) / 1.055, 2.4);
}

double relativeLuminance(const QColor& color)
{
    return 0.2126 * linearChannel(color.redF())
        + 0.7152 * linearChannel(color.greenF())
        + 0.0722 * linearChannel(color.blueF());
}

double contrastRatio(const QString& foreground, const QString& background)
{
    const double first = relativeLuminance(QColor(foreground));
    const double second = relativeLuminance(QColor(background));
    return (std::max(first, second) + 0.05) / (std::min(first, second) + 0.05);
}

struct TestOklch {
    double lightness = 0.0;
    double chroma = 0.0;
    double hue = 0.0;
};

TestOklch testOklch(const QString& value)
{
    const QColor color(value);
    const double r = linearChannel(color.redF());
    const double g = linearChannel(color.greenF());
    const double b = linearChannel(color.blueF());
    const double l = std::cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
    const double m = std::cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
    const double s = std::cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
    const double a = 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s;
    const double yellowBlue = 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s;
    double hue = std::atan2(yellowBlue, a) * 180.0 / 3.14159265358979323846;
    if (hue < 0.0) {
        hue += 360.0;
    }
    const double lightness = 0.2104542553 * l + 0.7936177850 * m
        - 0.0040720468 * s;
    return {lightness, std::hypot(a, yellowBlue), hue};
}

double hueDistance(double first, double second)
{
    const double distance = std::abs(first - second);
    return std::min(distance, 360.0 - distance);
}

} // namespace

class SettingsControllerTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void defaultCacheDirectoryUsesStandardPaths();
    void emptyTestCacheLocationFallsBackBeforeAppending();
    void defaultOutputDirectoryUsesStandardPaths();
    void parallelJobsClampAndPersistAcrossReload();
    void cacheLimitMigratesOnlyUntouchedLegacyDefault();
    void transcodeDefaultsAreSplitAndPersisted();
    void migratesCombinedTranscodePreset();
    void loadCreatesDefaultDirectories();
    void migratesLegacyDefaultExportDirectory();
    void migratesOldBuiltInExportDirectoryButKeepsCustomDirectory();
    void migratesLegacyPlaybackModes();
    void playbackDeviceSettingsPersistAndMigrateDefaultLabel();
    void waveformAppearanceSettingsClampPersistAndReset();
    void frequencyColorDefaultsValidateAdaptAndReset();
    void frequencyColorTransactionsAndLegacyAliases();
    void frequencyColorMigratesKnownDefaults_data();
    void frequencyColorMigratesKnownDefaults();
    void frequencyColorMigrationPreservesCustomAndRunsOnce();
    void frequencyColorMigrationWaitsForPayloadSync();
    void migratesPreviousFrequencyDefaultsWithoutOverwritingCustomValues();
    void restoresLegacyRgbColorsWithoutDeletingKeys();
    void mapsSimplifiedColorsIntoRestoredContract();
    void listWaveformThumbnailSettingsPersistFallbackAndReset();
    void visualizerCanvasAndReplayGainSettingsPersist();
    void glassFeatureIsAbsentFromSettingsContract();
    void playerShellModeDefaultsPersistsAndNormalizes();
    void windowLayoutThemeDefaultsAndNormalizesToDualWindow();
    void appearanceDefaultsToDarkAndPreservesLegacyModes();
    void appearanceDefaultResetDoesNotTouchMediaSettingsOutsideEdit();
    void retiresLegacySmartPlaylists();
    void autoCleanCacheRemovesOldestFilesWhenOverLimit();
    void supportsOnlyFourLanguages();
    void editSessionCanCommitOrCancel();
    void rebindFileAssociationsEnablesRegistrationDuringEdit();
    void testModeDoesNotTouchStartupRegistry();
    void iniThemeSettingsPreserveStrictLegacyStrings();
};

void SettingsControllerTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("AgPlayer"));
    QCoreApplication::setApplicationName(QStringLiteral("AgPlayer-settings-controller-test"));
    QSettings().clear();
}

void SettingsControllerTest::glassFeatureIsAbsentFromSettingsContract()
{
    QSettings persisted;
    persisted.clear();
    persisted.setValue(QStringLiteral("appearance/glassEffect"), true);

    SettingsController settings;
    QCOMPARE(settings.metaObject()->indexOfProperty("glassEffect"), -1);
    QVERIFY(!persisted.contains(QStringLiteral("appearance/glassEffect")));
    persisted.clear();
}

void SettingsControllerTest::playerShellModeDefaultsPersistsAndNormalizes()
{
    QSettings persisted;
    persisted.clear();
    persisted.setValue(QStringLiteral("appearance/themeMode"), 1);

    {
        SettingsController settings;
        QCOMPARE(settings.playerShellMode(), 0);
        QCOMPARE(settings.themeMode(), 1);
        settings.setPlayerShellMode(1);
        QCOMPARE(settings.themeMode(), 1);
        QCOMPARE(settings.windowLayoutTheme(), QStringLiteral("single-window"));
        QCOMPARE(persisted.value(QStringLiteral("appearance/playerShellMode")).toInt(), 1);
        settings.setPlayerShellMode(2);
        QCOMPARE(settings.playerShellMode(), 2);
        QCOMPARE(settings.windowLayoutTheme(), QStringLiteral("rolling-player"));
        QCOMPARE(persisted.value(QStringLiteral("appearance/playerShellMode")).toInt(), 2);
    }

    SettingsController reloaded;
    QCOMPARE(reloaded.playerShellMode(), 2);
    QCOMPARE(reloaded.windowLayoutTheme(), QStringLiteral("rolling-player"));

    persisted.setValue(QStringLiteral("appearance/playerShellMode"), 99);
    persisted.setValue(QStringLiteral("appearance/windowLayoutTheme"),
                       QStringLiteral("unknown"));
    SettingsController malformed;
    QCOMPARE(malformed.playerShellMode(), 0);
    QCOMPARE(malformed.windowLayoutTheme(), QStringLiteral("dual-window"));
    QCOMPARE(persisted.value(QStringLiteral("appearance/playerShellMode")).toInt(), 0);

    persisted.clear();
    persisted.setValue(QStringLiteral("appearance/windowLayoutTheme"),
                       QStringLiteral("rolling-player"));
    SettingsController migratedLayout;
    QCOMPARE(migratedLayout.playerShellMode(), 2);
    QCOMPARE(persisted.value(QStringLiteral("appearance/playerShellMode")).toInt(), 2);

    migratedLayout.resetToDefaults();
    QCOMPARE(migratedLayout.playerShellMode(), 0);
    QCOMPARE(migratedLayout.windowLayoutTheme(), QStringLiteral("dual-window"));
}

void SettingsControllerTest::windowLayoutThemeDefaultsAndNormalizesToDualWindow()
{
    QSettings persisted;
    persisted.clear();

    SettingsController settings;
    QCOMPARE(settings.windowLayoutTheme(), QStringLiteral("dual-window"));
    QCOMPARE(settings.playerShellMode(), 0);

    settings.setWindowLayoutTheme(QStringLiteral("single-window"));
    QCOMPARE(settings.windowLayoutTheme(), QStringLiteral("single-window"));
    QCOMPARE(settings.playerShellMode(), 1);

    settings.setWindowLayoutTheme(QStringLiteral("rolling-player"));
    QCOMPARE(settings.windowLayoutTheme(), QStringLiteral("rolling-player"));
    QCOMPARE(settings.playerShellMode(), 2);

    settings.setPlayerShellMode(0);
    QCOMPARE(settings.windowLayoutTheme(), QStringLiteral("dual-window"));
    settings.setWindowLayoutTheme(QStringLiteral("unknown"));
    QCOMPARE(settings.windowLayoutTheme(), QStringLiteral("dual-window"));
    QCOMPARE(persisted.value(QStringLiteral("appearance/windowLayoutTheme")).toString(),
             QStringLiteral("dual-window"));
}

void SettingsControllerTest::appearanceDefaultsToDarkAndPreservesLegacyModes()
{
    QSettings persisted;
    persisted.clear();

    SettingsController defaults;
    QCOMPARE(defaults.themeMode(), 0);
    QCOMPARE(defaults.metaObject()->indexOfProperty("skinColorMode"), -1);
    QCOMPARE(defaults.metaObject()->indexOfProperty("skinPreset"), -1);
    QCOMPARE(defaults.metaObject()->indexOfProperty("skinCustomColor"), -1);

    for (const int mode : {0, 1, 2}) {
        persisted.clear();
        persisted.setValue(QStringLiteral("appearance/themeMode"), mode);
        SettingsController stored;
        QCOMPARE(stored.themeMode(), mode);
    }

    persisted.clear();
    persisted.setValue(QStringLiteral("appearance/themeMode"), 99);
    SettingsController invalid;
    QCOMPARE(invalid.themeMode(), 0);
    QCOMPARE(persisted.value(QStringLiteral("appearance/themeMode")).toInt(), 0);
}

void SettingsControllerTest::appearanceDefaultResetDoesNotTouchMediaSettingsOutsideEdit()
{
    QSettings persisted;
    persisted.clear();
    const QStringList mediaKeys = {
        QStringLiteral("appearance/waveformMode"),
        QStringLiteral("appearance/waveformHeight"),
        QStringLiteral("appearance/waveformDensity"),
        QStringLiteral("appearance/waveformThickness"),
        QStringLiteral("appearance/waveformPeakAlgorithm"),
        QStringLiteral("appearance/waveformSolidBaseColor"),
        QStringLiteral("appearance/waveformSolidProgressColor"),
        QStringLiteral("appearance/waveformRgbBaseColor"),
        QStringLiteral("appearance/waveformRgbStartColor"),
        QStringLiteral("appearance/waveformRgbMiddleColor"),
        QStringLiteral("appearance/waveformRgbEndColor"),
        QStringLiteral("appearance/waveformRgbProgress"),
        QStringLiteral("appearance/waveformHoverTimePreview"),
        QStringLiteral("appearance/waveformPlaybackGuide"),
        QStringLiteral("appearance/waveformCanvasHeight"),
        QStringLiteral("appearance/waveformCanvasLocked"),
        QStringLiteral("appearance/listWaveformThumbnailEnabled"),
        QStringLiteral("appearance/listWaveformThumbnailMode"),
        QStringLiteral("appearance/spectrumColorMode"),
        QStringLiteral("appearance/spectrumSolidColor"),
        QStringLiteral("appearance/spectrumRgbStartColor"),
        QStringLiteral("appearance/spectrumRgbMiddleColor"),
        QStringLiteral("appearance/spectrumRgbEndColor"),
    };

    SettingsController settings;
    for (const QString& key : mediaKeys) {
        persisted.remove(key);
    }
    persisted.sync();
    for (const QString& key : mediaKeys) {
        QVERIFY2(!persisted.contains(key), qPrintable(key));
    }
    settings.setThemeMode(1);
    QSignalSpy waveformChanged(&settings, &SettingsController::waveformModeChanged);
    QSignalSpy spectrumChanged(&settings, &SettingsController::spectrumColorModeChanged);
    QSignalSpy thumbnailChanged(
        &settings, &SettingsController::listWaveformThumbnailModeChanged);

    settings.resetToDefaults();

    QCOMPARE(settings.themeMode(), 0);
    QCOMPARE(waveformChanged.count(), 0);
    QCOMPARE(spectrumChanged.count(), 0);
    QCOMPARE(thumbnailChanged.count(), 0);
    for (const QString& key : mediaKeys) {
        QVERIFY2(!persisted.contains(key), qPrintable(key));
    }
}

void SettingsControllerTest::defaultCacheDirectoryUsesStandardPaths()
{
    SettingsController settings;
    const QString cacheDir = settings.cacheDirectory();
    QVERIFY(!cacheDir.contains(QStringLiteral("D:\\Music")));
    const QString expected =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
        + QStringLiteral("/AgPlayer/Cache");
    QCOMPARE(QDir::cleanPath(cacheDir), QDir::cleanPath(expected));
}

void SettingsControllerTest::emptyTestCacheLocationFallsBackBeforeAppending()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString tempBase = directory.filePath(QStringLiteral("temp-base"));
    const QString appDataBase =
        directory.filePath(QStringLiteral("appdata-base"));

    QCOMPARE(SettingsController::resolveTestCacheDirectory(
                 QString{}, tempBase, appDataBase),
             QDir(tempBase).filePath(QStringLiteral("AgPlayer/Cache")));
    QCOMPARE(SettingsController::resolveTestCacheDirectory(
                 QString{}, QString{}, appDataBase),
             QDir(appDataBase).filePath(QStringLiteral("AgPlayer/Cache")));
}

void SettingsControllerTest::defaultOutputDirectoryUsesStandardPaths()
{
    SettingsController settings;
    const QString exportDir = settings.defaultOutputDirectory();
    QVERIFY(!exportDir.contains(QStringLiteral("D:\\Music")));
    QCOMPARE(
        QDir::cleanPath(exportDir),
        QDir::cleanPath(QStandardPaths::writableLocation(
            QStandardPaths::DesktopLocation)));
}

void SettingsControllerTest::loadCreatesDefaultDirectories()
{
    SettingsController settings;
    const QString cacheDir = settings.cacheDirectory();
    const QString exportDir = settings.defaultOutputDirectory();
    QVERIFY(!cacheDir.isEmpty());
    QVERIFY(!exportDir.isEmpty());
    QVERIFY(QDir(cacheDir).exists());
    QVERIFY(QDir(exportDir).exists());
}

void SettingsControllerTest::parallelJobsClampAndPersistAcrossReload()
{
    QSettings persisted;
    persisted.clear();

    {
        SettingsController settings;
        QCOMPARE(settings.parallelJobs(), 5);
        settings.setParallelJobs(0);
        QCOMPARE(settings.parallelJobs(), 1);
        settings.setParallelJobs(11);
        QCOMPARE(settings.parallelJobs(), 10);
        settings.setParallelJobs(3);
        QCOMPARE(settings.parallelJobs(), 3);
    }

    SettingsController reloaded;
    QCOMPARE(reloaded.parallelJobs(), 3);
    persisted.clear();
}

void SettingsControllerTest::transcodeDefaultsAreSplitAndPersisted()
{
    QSettings persisted;
    persisted.clear();

    {
        SettingsController settings;
        QCOMPARE(settings.transcodeFormat(), QStringLiteral("MP3"));
        QCOMPARE(settings.transcodeBitrateKbps(), 320);
        QCOMPARE(settings.transcodeSampleRateHz(), 44100);
        QCOMPARE(settings.transcodeChannels(), 2);
        QCOMPARE(settings.preserveMetadata(), true);
        QCOMPARE(settings.preserveCover(), true);
        QCOMPARE(settings.preserveDirectoryStructure(), true);
        QCOMPARE(settings.extractVideoAudio(), true);

        settings.setTranscodeFormat(QStringLiteral("AIFF"));
        settings.setTranscodeBitrateKbps(256);
        settings.setTranscodeSampleRateHz(192000);
        settings.setTranscodeChannels(1);
        settings.setPreserveCover(false);
        settings.setPreserveDirectoryStructure(false);
        settings.setExtractVideoAudio(false);
    }

    SettingsController reloaded;
    QCOMPARE(reloaded.transcodeFormat(), QStringLiteral("AIFF"));
    QCOMPARE(reloaded.transcodeBitrateKbps(), 256);
    QCOMPARE(reloaded.transcodeSampleRateHz(), 192000);
    QCOMPARE(reloaded.transcodeChannels(), 1);
    QCOMPARE(reloaded.preserveCover(), false);
    QCOMPARE(reloaded.preserveDirectoryStructure(), false);
    QCOMPARE(reloaded.extractVideoAudio(), false);
    persisted.clear();
}

void SettingsControllerTest::migratesCombinedTranscodePreset()
{
    QSettings persisted;
    persisted.clear();
    persisted.setValue(QStringLiteral("audioTools/defaultTranscodeFormat"),
                       QStringLiteral("MP3 / 192kbps / 48kHz / Mono"));

    SettingsController settings;
    QCOMPARE(settings.transcodeFormat(), QStringLiteral("MP3"));
    QCOMPARE(settings.transcodeBitrateKbps(), 192);
    QCOMPARE(settings.transcodeSampleRateHz(), 48000);
    QCOMPARE(settings.transcodeChannels(), 1);
    QVERIFY(!persisted.contains(
        QStringLiteral("audioTools/defaultTranscodeFormat")));
    persisted.clear();
}

void SettingsControllerTest::migratesLegacyDefaultExportDirectory()
{
    QSettings persisted;
    persisted.clear();
    const QString legacyPath =
        QDir::tempPath() + QStringLiteral("/AgPlayer_legacy_export");
    persisted.setValue(QStringLiteral("general/defaultExportDirectory"),
                       legacyPath);

    SettingsController settings;
    QCOMPARE(settings.defaultOutputDirectory(), legacyPath);
    QCOMPARE(
        persisted.value(QStringLiteral("audioTools/defaultOutputDirectory"))
            .toString(),
        legacyPath);
    QVERIFY(!persisted.contains(
        QStringLiteral("general/defaultExportDirectory")));
    persisted.clear();
}

void SettingsControllerTest::
migratesOldBuiltInExportDirectoryButKeepsCustomDirectory()
{
    QSettings persisted;
    persisted.clear();
    const QString oldBuiltIn =
        QStandardPaths::writableLocation(QStandardPaths::MusicLocation)
        + QStringLiteral("/AgPlayer_Export");
    persisted.setValue(
        QStringLiteral("audioTools/defaultOutputDirectory"), oldBuiltIn);

    {
        SettingsController settings;
        QCOMPARE(
            QDir::cleanPath(settings.defaultOutputDirectory()),
            QDir::cleanPath(QStandardPaths::writableLocation(
                QStandardPaths::DesktopLocation)));
    }

    const QString custom =
        QDir::tempPath() + QStringLiteral("/AgPlayer_custom_export");
    persisted.setValue(
        QStringLiteral("audioTools/defaultOutputDirectory"), custom);
    {
        SettingsController settings;
        QCOMPARE(settings.defaultOutputDirectory(), custom);
    }
    persisted.clear();
}

void SettingsControllerTest::migratesLegacyPlaybackModes()
{
    QSettings persisted;
    persisted.clear();
    persisted.setValue(QStringLiteral("playback/defaultPlaybackMode"), 1);

    {
        SettingsController settings;
        QCOMPARE(settings.defaultPlaybackMode(), 2);
    }
    QCOMPARE(
        persisted.value(QStringLiteral("playback/defaultPlaybackMode")).toInt(),
        2);
    QCOMPARE(
        persisted.value(QStringLiteral("playback/modeSchemaVersion")).toInt(),
        2);

    persisted.clear();
    persisted.setValue(QStringLiteral("playback/defaultPlaybackMode"), 2);
    {
        SettingsController settings;
        QCOMPARE(settings.defaultPlaybackMode(), 1);
    }

    persisted.clear();
    persisted.setValue(QStringLiteral("playback/defaultPlaybackMode"), 1);
    persisted.setValue(QStringLiteral("playback/modeSchemaVersion"), 2);
    {
        SettingsController settings;
        QCOMPARE(settings.defaultPlaybackMode(), 1);
    }
    persisted.clear();
}

void SettingsControllerTest::playbackDeviceSettingsPersistAndMigrateDefaultLabel()
{
    QSettings persisted;
    persisted.clear();
    persisted.setValue(
        QStringLiteral("playback/outputDevice"),
        QStringLiteral("\u81EA\u52A8 / \u7CFB\u7EDF\u9ED8\u8BA4\u8BBE\u5907"));
    {
        SettingsController settings;
        QCOMPARE(settings.outputDevice(), QString());
        QCOMPARE(settings.exclusiveMode(), false);
        settings.setOutputDevice(QStringLiteral("Test Device"));
        settings.setExclusiveMode(true);
        settings.setMatchTrackSampleRate(false);
        settings.setTransitionFadeMs(500);
        settings.setTransitionFadeMs(100);
        QCOMPARE(settings.transitionFadeMs(), 500);
    }
    SettingsController reloaded;
    QCOMPARE(reloaded.outputDevice(), QStringLiteral("Test Device"));
    QCOMPARE(reloaded.exclusiveMode(), true);
    QCOMPARE(reloaded.matchTrackSampleRate(), false);
    QCOMPARE(reloaded.transitionFadeMs(), 500);
    persisted.clear();
}

void SettingsControllerTest::waveformAppearanceSettingsClampPersistAndReset()
{
    QSettings persisted;
    persisted.clear();
    {
        SettingsController settings;
        QCOMPARE(settings.waveformHeight(), 0.8);
        QCOMPARE(settings.waveformDensity(), 2.0);
        QCOMPARE(settings.waveformThickness(), 1.0);
        QCOMPARE(settings.waveformPeakAlgorithm(), 0);
        QCOMPARE(settings.property("waveformSolidBaseColor").toString(), QStringLiteral("#9098a6"));
        QCOMPARE(settings.property("waveformSolidProgressColor").toString(), QStringLiteral("#d27722"));
        QCOMPARE(settings.property("waveformRgbBaseColor").toString(), QStringLiteral("#00b4a0"));
        QCOMPARE(settings.property("waveformRgbStartColor").toString(), QStringLiteral("#00d4ff"));
        QCOMPARE(settings.property("waveformRgbMiddleColor").toString(), QStringLiteral("#7b2ff7"));
        QCOMPARE(settings.property("waveformRgbEndColor").toString(), QStringLiteral("#e62e9b"));
        QCOMPARE(settings.property("waveformFrequencyLowColor").toString(),
                 QStringLiteral("#269a8e"));
        QCOMPARE(settings.property("waveformFrequencyMidColor").toString(),
                 QStringLiteral("#c66b55"));
        QCOMPARE(settings.property("waveformFrequencyHighColor").toString(),
                 QStringLiteral("#b5a4c6"));
        QCOMPARE(settings.property("waveformFrequencyStrength").toDouble(), 0.62);
        QCOMPARE(settings.property("waveformRgbProgress").toBool(), false);
        QCOMPARE(settings.waveformMode(), 0);
        QCOMPARE(settings.waveformPlaybackGuide(), false);

        settings.setWaveformHeight(3.0);
        settings.setWaveformDensity(0.1);
        settings.setWaveformThickness(2.34);
        settings.setWaveformPeakAlgorithm(1);
        QVERIFY(settings.setProperty("waveformSolidBaseColor", QStringLiteral("#112233")));
        QVERIFY(settings.setProperty("waveformSolidProgressColor", QStringLiteral("invalid")));
        QVERIFY(settings.setProperty("waveformRgbProgress", true));
        QVERIFY(settings.setProperty("waveformFrequencyLowColor",
                                     QStringLiteral("#112233")));
        QVERIFY(settings.setProperty("waveformFrequencyMidColor",
                                     QStringLiteral("#445566")));
        QVERIFY(settings.setProperty("waveformFrequencyHighColor",
                                     QStringLiteral("#778899")));
        QVERIFY(settings.setProperty("waveformFrequencyStrength", 1.4));
        settings.setWaveformPlaybackGuide(true);

        settings.setWaveformMode(3);
        QCOMPARE(settings.waveformMode(), 3);
        QVERIFY(QMetaObject::invokeMethod(&settings, "cycleWaveformMode"));
        QCOMPARE(settings.waveformMode(), 1);
        QVERIFY(QMetaObject::invokeMethod(&settings, "cycleWaveformMode"));
        QCOMPARE(settings.waveformMode(), 2);
        QVERIFY(QMetaObject::invokeMethod(&settings, "cycleWaveformMode"));
        QCOMPARE(settings.waveformMode(), 0);
        QVERIFY(QMetaObject::invokeMethod(&settings, "cycleWaveformMode"));
        QCOMPARE(settings.waveformMode(), 3);

        QCOMPARE(settings.waveformHeight(), 1.5);
        QCOMPARE(settings.waveformDensity(), 0.5);
        QCOMPARE(settings.waveformThickness(), 2.3);
        QCOMPARE(settings.waveformPeakAlgorithm(), 1);
        QCOMPARE(settings.property("waveformSolidBaseColor").toString(), QStringLiteral("#112233"));
        QCOMPARE(settings.property("waveformSolidProgressColor").toString(), QStringLiteral("#d27722"));
        QCOMPARE(settings.property("waveformRgbProgress").toBool(), true);
        QCOMPARE(settings.waveformPlaybackGuide(), true);
        QCOMPARE(settings.property("waveformFrequencyStrength").toDouble(), 1.0);
    }

    SettingsController reloaded;
    QCOMPARE(reloaded.waveformHeight(), 1.5);
    QCOMPARE(reloaded.waveformDensity(), 0.5);
    QCOMPARE(reloaded.waveformThickness(), 2.3);
    QCOMPARE(reloaded.waveformPeakAlgorithm(), 1);
    QCOMPARE(reloaded.property("waveformSolidBaseColor").toString(), QStringLiteral("#112233"));
    QCOMPARE(reloaded.property("waveformRgbProgress").toBool(), true);
    QCOMPARE(reloaded.property("waveformFrequencyLowColor").toString(),
             QStringLiteral("#112233"));
    QCOMPARE(reloaded.property("waveformFrequencyMidColor").toString(),
             QStringLiteral("#445566"));
    QCOMPARE(reloaded.property("waveformFrequencyHighColor").toString(),
             QStringLiteral("#778899"));
    QCOMPARE(reloaded.property("waveformFrequencyStrength").toDouble(), 1.0);
    QCOMPARE(reloaded.waveformPlaybackGuide(), true);

    auto* resetFrequencies = reloaded.frequencyColorWaveform();
    resetFrequencies->setMixDarkColor(QStringLiteral("#123456"));
    resetFrequencies->setLowLightColor(QStringLiteral("#654321"));
    resetFrequencies->setMidDarkOpacity(0.01);
    resetFrequencies->setHighLightOpacity(0.99);
    resetFrequencies->setPlayFocus(false);
    QVERIFY(QMetaObject::invokeMethod(&reloaded, "resetWaveformFrequencyColors"));
    QCOMPARE(reloaded.property("waveformFrequencyLowColor").toString(),
             QStringLiteral("#269a8e"));
    QCOMPARE(reloaded.property("waveformFrequencyMidColor").toString(),
             QStringLiteral("#c66b55"));
    QCOMPARE(reloaded.property("waveformFrequencyHighColor").toString(),
             QStringLiteral("#b5a4c6"));
    QCOMPARE(resetFrequencies->preset(), QStringLiteral("luminousGlaze"));
    QCOMPARE(resetFrequencies->mixDarkColor(), QStringLiteral("#7a8490"));
    QCOMPARE(resetFrequencies->mixLightColor(), QStringLiteral("#59636d"));
    QCOMPARE(resetFrequencies->lowLightColor(), QStringLiteral("#146b64"));
    QCOMPARE(resetFrequencies->midDarkOpacity(), 0.38);
    QCOMPARE(resetFrequencies->highLightOpacity(), 0.40);
    QCOMPARE(resetFrequencies->playFocus(), true);

    reloaded.resetWaveformDefaults();
    QCOMPARE(reloaded.waveformHeight(), 0.8);
    QCOMPARE(reloaded.waveformDensity(), 2.0);
    QCOMPARE(reloaded.waveformThickness(), 1.0);
    QCOMPARE(reloaded.waveformPeakAlgorithm(), 0);
    QCOMPARE(reloaded.property("waveformSolidBaseColor").toString(), QStringLiteral("#9098a6"));
    QCOMPARE(reloaded.property("waveformSolidProgressColor").toString(), QStringLiteral("#d27722"));
    QCOMPARE(reloaded.property("waveformRgbProgress").toBool(), false);
    QCOMPARE(reloaded.property("waveformFrequencyLowColor").toString(),
             QStringLiteral("#269a8e"));
    QCOMPARE(reloaded.property("waveformFrequencyMidColor").toString(),
             QStringLiteral("#c66b55"));
    QCOMPARE(reloaded.property("waveformFrequencyHighColor").toString(),
             QStringLiteral("#b5a4c6"));
    QCOMPARE(reloaded.property("waveformFrequencyStrength").toDouble(), 0.62);
    QCOMPARE(reloaded.waveformMode(), 0);
    QCOMPARE(reloaded.waveformPlaybackGuide(), false);
    persisted.clear();
}

void SettingsControllerTest::frequencyColorDefaultsValidateAdaptAndReset()
{
    QSettings persisted;
    persisted.clear();

    SettingsController settings;
    auto* fcw = settings.frequencyColorWaveform();
    QVERIFY(fcw);
    QCOMPARE(fcw->preset(), QStringLiteral("luminousGlaze"));
    QCOMPARE(fcw->mixDarkColor(), QStringLiteral("#7a8490"));
    QCOMPARE(fcw->lowDarkColor(), QStringLiteral("#269a8e"));
    QCOMPARE(fcw->midDarkColor(), QStringLiteral("#c66b55"));
    QCOMPARE(fcw->highDarkColor(), QStringLiteral("#b5a4c6"));
    QCOMPARE(fcw->mixLightColor(), QStringLiteral("#59636d"));
    QCOMPARE(fcw->lowLightColor(), QStringLiteral("#146b64"));
    QCOMPARE(fcw->midLightColor(), QStringLiteral("#9d4938"));
    QCOMPARE(fcw->highLightColor(), QStringLiteral("#6e5a7d"));
    QCOMPARE(fcw->mixDarkOpacity(), 0.18);
    QCOMPARE(fcw->lowDarkOpacity(), 0.44);
    QCOMPARE(fcw->midDarkOpacity(), 0.38);
    QCOMPARE(fcw->highDarkOpacity(), 0.46);
    QCOMPARE(fcw->mixLightOpacity(), 0.14);
    QCOMPARE(fcw->lowLightOpacity(), 0.36);
    QCOMPARE(fcw->midLightOpacity(), 0.32);
    QCOMPARE(fcw->highLightOpacity(), 0.40);
    QCOMPARE(fcw->playFocus(), true);

    QSignalSpy changed(fcw, &FrequencyColorWaveformSettings::changed);
    fcw->setLowDarkOpacity(0.437);
    QCOMPARE(fcw->lowDarkOpacity(), 0.44);
    QCOMPARE(fcw->preset(), QStringLiteral("custom"));
    QCOMPARE(changed.count(), 1);
    fcw->setLowDarkOpacity(0.437);
    QCOMPARE(changed.count(), 1);
    fcw->setLowDarkOpacity(0.0049);
    QCOMPARE(fcw->lowDarkOpacity(), 0.0);
    fcw->setLowDarkOpacity(0.005);
    QCOMPARE(fcw->lowDarkOpacity(), 0.01);
    fcw->setLowDarkOpacity(0.995);
    QCOMPARE(fcw->lowDarkOpacity(), 1.0);
    fcw->setLowDarkOpacity(1.4);
    QCOMPARE(fcw->lowDarkOpacity(), 1.0);
    QCOMPARE(changed.count(), 4);
    fcw->setLowDarkOpacity(-0.2);
    QCOMPARE(fcw->lowDarkOpacity(), 0.0);
    QCOMPARE(changed.count(), 5);
    fcw->setLowDarkOpacity(std::numeric_limits<double>::quiet_NaN());
    QCOMPARE(fcw->lowDarkOpacity(), 0.44);
    QCOMPARE(changed.count(), 6);
    fcw->setLowDarkOpacity(std::numeric_limits<double>::infinity());
    QCOMPARE(fcw->lowDarkOpacity(), 0.44);
    QCOMPARE(changed.count(), 6);

    fcw->setMidDarkColor(QStringLiteral("#ABCDEF"));
    QCOMPARE(fcw->midDarkColor(), QStringLiteral("#abcdef"));
    QCOMPARE(fcw->preset(), QStringLiteral("custom"));
    const int beforeInvalid = changed.count();
    fcw->setMidDarkColor(QStringLiteral("not-a-color"));
    fcw->setMidDarkColor(QStringLiteral("red"));
    fcw->setMidDarkColor(QStringLiteral("#abc"));
    fcw->setMidDarkColor(QStringLiteral("#80112233"));
    fcw->setMidDarkColor(QStringLiteral("transparent"));
    QCOMPARE(fcw->midDarkColor(), QStringLiteral("#abcdef"));
    QCOMPARE(changed.count(), beforeInvalid);

    fcw->resetToLuminousGlaze();
    const TestOklch source = testOklch(QStringLiteral("#ff00ff"));
    fcw->setLowDarkColor(QStringLiteral("#ff00ff"));
    const TestOklch adapted = testOklch(fcw->lowLightColor());
    QVERIFY2(hueDistance(source.hue, adapted.hue) <= 3.0,
             qPrintable(QStringLiteral("hue delta %1").arg(
                 hueDistance(source.hue, adapted.hue))));
    QVERIFY(adapted.chroma <= 0.161);
    QVERIFY(adapted.lightness >= 0.34 && adapted.lightness <= 0.481);
    QVERIFY(contrastRatio(fcw->lowLightColor(), QStringLiteral("#f5f3ef")) >= 3.0);

    const QString manuallyChosenLight = fcw->lowLightColor();
    fcw->setLowLightColor(manuallyChosenLight); // same value still establishes a lock
    fcw->setLowDarkColor(QStringLiteral("#00ff00"));
    QCOMPARE(fcw->lowLightColor(), manuallyChosenLight);

    fcw->resetToLuminousGlaze();
    fcw->setMixLightColor(QStringLiteral("#ff00ff"));
    const TestOklch adaptedDark = testOklch(fcw->mixDarkColor());
    QVERIFY(adaptedDark.lightness >= 0.679 && adaptedDark.lightness <= 0.82);
    QVERIFY(contrastRatio(fcw->mixDarkColor(), QStringLiteral("#0b1017")) >= 3.0);

    fcw->resetToLuminousGlaze();
    fcw->setLowDarkColor(QStringLiteral("#ff0010"));
    QVERIFY(hueDistance(testOklch(QStringLiteral("#ff0010")).hue,
                        testOklch(fcw->lowLightColor()).hue) <= 3.0);
    fcw->setLowDarkColor(QStringLiteral("#808080"));
    QVERIFY(contrastRatio(fcw->lowLightColor(), QStringLiteral("#f5f3ef")) >= 3.0);
    fcw->setLowDarkColor(QStringLiteral("#0000ff"));
    QVERIFY(testOklch(fcw->lowLightColor()).chroma <= 0.161);

    fcw->setPlayFocus(false);
    const int beforeReset = changed.count();
    fcw->resetToLuminousGlaze();
    QCOMPARE(changed.count(), beforeReset + 1);
    QCOMPARE(fcw->preset(), QStringLiteral("luminousGlaze"));
    QCOMPARE(fcw->playFocus(), true);
    QCOMPARE(fcw->lowDarkColor(), QStringLiteral("#269a8e"));
    QCOMPARE(fcw->lowLightOpacity(), 0.36);

    persisted.setValue(QStringLiteral("appearance/waveformFrequencyColorSchema"), 1);
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyMidDarkColor"),
                       QStringLiteral("#abc"));
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyHighLightColor"),
                       QStringLiteral("transparent"));
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyHighLightOpacity"),
                       QStringLiteral("not-a-number"));
    SettingsController corruptLoad;
    QCOMPARE(corruptLoad.frequencyColorWaveform()->midDarkColor(),
             QStringLiteral("#c66b55"));
    QCOMPARE(corruptLoad.frequencyColorWaveform()->highLightColor(),
             QStringLiteral("#6e5a7d"));
    QCOMPARE(corruptLoad.frequencyColorWaveform()->highLightOpacity(), 0.40);
    persisted.clear();
}

void SettingsControllerTest::frequencyColorTransactionsAndLegacyAliases()
{
    QSettings persisted;
    persisted.clear();

    SettingsController settings;
    auto* fcw = settings.frequencyColorWaveform();
    QCOMPARE(persisted.value(
                 QStringLiteral("appearance/waveformFrequencyColorSchema")).toInt(),
             1);

    fcw->setLowDarkColor(QStringLiteral("#112233"));
    QCOMPARE(persisted.value(
                 QStringLiteral("appearance/waveformFrequencyLowDarkColor")).toString(),
             QStringLiteral("#112233"));
    QCOMPARE(persisted.value(
                 QStringLiteral("appearance/waveformFrequencyLowColor")).toString(),
             QStringLiteral("#112233"));

    settings.beginEdit();
    fcw->setLowDarkColor(QStringLiteral("#445566"));
    QCOMPARE(fcw->lowDarkColor(), QStringLiteral("#445566"));
    QCOMPARE(persisted.value(
                 QStringLiteral("appearance/waveformFrequencyLowDarkColor")).toString(),
             QStringLiteral("#112233"));
    settings.cancelEdit();
    QCOMPARE(fcw->lowDarkColor(), QStringLiteral("#112233"));

    settings.beginEdit();
    fcw->setLowDarkColor(QStringLiteral("#778899"));
    settings.commitEdit();
    QCOMPARE(persisted.value(
                 QStringLiteral("appearance/waveformFrequencyLowDarkColor")).toString(),
             QStringLiteral("#778899"));

    settings.setWaveformFrequencyMidColor(QStringLiteral("#abcdef"));
    QCOMPARE(fcw->midDarkColor(), QStringLiteral("#abcdef"));
    QCOMPARE(settings.waveformFrequencyMidColor(), QStringLiteral("#abcdef"));

    QSignalSpy changed(fcw, &FrequencyColorWaveformSettings::changed);
    settings.setWaveformFrequencyStrength(0.31);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(settings.waveformFrequencyStrength(), 0.31);
    QCOMPARE(fcw->lowDarkOpacity(), 0.22);
    QCOMPARE(fcw->midDarkOpacity(), 0.19);
    QCOMPARE(fcw->highDarkOpacity(), 0.23);
    QCOMPARE(fcw->lowLightOpacity(), 0.18);
    QCOMPARE(fcw->midLightOpacity(), 0.16);
    QCOMPARE(fcw->highLightOpacity(), 0.20);
    QCOMPARE(persisted.value(
                 QStringLiteral("appearance/waveformFrequencyStrength")).toDouble(),
             0.31);

    settings.setWaveformFrequencyStrength(1.0);
    QCOMPARE(changed.count(), 2);
    QCOMPARE(settings.waveformFrequencyStrength(), 1.0);
    QCOMPARE(fcw->lowDarkOpacity(), 0.71);
    QCOMPARE(fcw->midDarkOpacity(), 0.61);
    QCOMPARE(fcw->highDarkOpacity(), 0.74);
    QCOMPARE(fcw->lowLightOpacity(), 0.58);
    QCOMPARE(fcw->midLightOpacity(), 0.52);
    QCOMPARE(fcw->highLightOpacity(), 0.65);

    fcw->resetToLuminousGlaze();
    fcw->setLowDarkColor(QStringLiteral("#ff00ff"));
    const QString lockedLight = fcw->lowLightColor();
    fcw->setLowLightColor(lockedLight);
    {
        SettingsController reloaded;
        auto* reloadedFcw = reloaded.frequencyColorWaveform();
        QCOMPARE(reloadedFcw->lowLightColor(), lockedLight);
        reloadedFcw->setLowDarkColor(QStringLiteral("#00ff00"));
        QCOMPARE(reloadedFcw->lowLightColor(), lockedLight);
    }
    persisted.clear();
}

void SettingsControllerTest::frequencyColorMigratesKnownDefaults_data()
{
    QTest::addColumn<QString>("low");
    QTest::addColumn<QString>("mid");
    QTest::addColumn<QString>("high");
    QTest::addColumn<double>("strength");

    QTest::newRow("original") << QStringLiteral("#ff647c")
        << QStringLiteral("#3ed6ae") << QStringLiteral("#8a7cff") << 0.85;
    QTest::newRow("first-generation") << QStringLiteral("#c45100")
        << QStringLiteral("#b04bcd") << QStringLiteral("#0a819a") << 0.85;
    QTest::newRow("second-generation") << QStringLiteral("#c65332")
        << QStringLiteral("#9a4bc2") << QStringLiteral("#2e9b61") << 0.70;
    QTest::newRow("third-generation") << QStringLiteral("#c47a5e")
        << QStringLiteral("#7787b5") << QStringLiteral("#4f9279") << 0.62;
    QTest::newRow("klein-ember") << QStringLiteral("#315cff")
        << QStringLiteral("#ffb02e") << QStringLiteral("#f04ca5") << 0.62;
    QTest::newRow("mint") << QStringLiteral("#62c8a0")
        << QStringLiteral("#f1d79a") << QStringLiteral("#e98582") << 0.62;
    QTest::newRow("temporary-open-color") << QStringLiteral("#1098ad")
        << QStringLiteral("#f59f00") << QStringLiteral("#ae3ec9") << 0.62;
}

void SettingsControllerTest::frequencyColorMigratesKnownDefaults()
{
    QFETCH(QString, low);
    QFETCH(QString, mid);
    QFETCH(QString, high);
    QFETCH(double, strength);
    QSettings persisted;
    persisted.clear();
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyLowColor"), low);
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyMidColor"), mid);
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyHighColor"), high);
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyStrength"), strength);

    SettingsController migrated;
    auto* fcw = migrated.frequencyColorWaveform();
    QCOMPARE(fcw->preset(), QStringLiteral("luminousGlaze"));
    QCOMPARE(fcw->lowDarkColor(), QStringLiteral("#269a8e"));
    QCOMPARE(fcw->midDarkColor(), QStringLiteral("#c66b55"));
    QCOMPARE(fcw->highDarkColor(), QStringLiteral("#b5a4c6"));
    QCOMPARE(fcw->lowLightColor(), QStringLiteral("#146b64"));
    QCOMPARE(fcw->lowDarkOpacity(), 0.44);
    QCOMPARE(fcw->highLightOpacity(), 0.40);
    QCOMPARE(persisted.value(
                 QStringLiteral("appearance/waveformFrequencyColorSchema")).toInt(),
             1);
    QCOMPARE(persisted.value(
                 QStringLiteral("appearance/waveformFrequencyLowColor")).toString(),
             QStringLiteral("#269a8e"));
    persisted.clear();
}

void SettingsControllerTest::frequencyColorMigrationPreservesCustomAndRunsOnce()
{
    QSettings persisted;
    persisted.clear();
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyLowColor"),
                       QStringLiteral("#112233"));
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyMidColor"),
                       QStringLiteral("#9a4bc2"));
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyHighColor"),
                       QStringLiteral("#2e9b61"));
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyStrength"), 0.70);

    {
        SettingsController migrated;
        auto* fcw = migrated.frequencyColorWaveform();
        QCOMPARE(fcw->preset(), QStringLiteral("custom"));
        QCOMPARE(fcw->lowDarkColor(), QStringLiteral("#112233"));
        QCOMPARE(fcw->midDarkColor(), QStringLiteral("#9a4bc2"));
        QCOMPARE(fcw->highDarkColor(), QStringLiteral("#2e9b61"));
        QCOMPARE(fcw->lowDarkOpacity(), 0.50);
        QCOMPARE(fcw->midDarkOpacity(), 0.43);
        QCOMPARE(fcw->highDarkOpacity(), 0.52);
        QCOMPARE(fcw->lowLightOpacity(), 0.41);
        QCOMPARE(fcw->midLightOpacity(), 0.36);
        QCOMPARE(fcw->highLightOpacity(), 0.45);
        const QString generatedBeforeEdit = fcw->lowLightColor();
        migrated.beginEdit();
        fcw->setLowDarkColor(QStringLiteral("#654321"));
        QVERIFY(fcw->lowLightColor() != generatedBeforeEdit);
        migrated.cancelEdit();
        QCOMPARE(fcw->lowDarkColor(), QStringLiteral("#112233"));
    }

    {
        SettingsController reloaded;
        auto* reloadedFcw = reloaded.frequencyColorWaveform();
        reloadedFcw->setLowLightColor(QStringLiteral("#aabbcc"));
        reloadedFcw->setMidLightColor(QStringLiteral("#bbccdd"));
        reloadedFcw->setHighLightColor(QStringLiteral("#ccddee"));
        QCOMPARE(reloadedFcw->lowDarkColor(), QStringLiteral("#112233"));
        QCOMPARE(reloadedFcw->midDarkColor(), QStringLiteral("#9a4bc2"));
        QCOMPARE(reloadedFcw->highDarkColor(), QStringLiteral("#2e9b61"));
    }

    persisted.setValue(QStringLiteral("appearance/waveformFrequencyLowColor"),
                       QStringLiteral("#c45100"));
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyMidColor"),
                       QStringLiteral("#b04bcd"));
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyHighColor"),
                       QStringLiteral("#0a819a"));
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyStrength"), 0.85);
    {
        SettingsController secondLoad;
        QCOMPARE(secondLoad.frequencyColorWaveform()->lowDarkColor(),
                 QStringLiteral("#112233"));
        QCOMPARE(secondLoad.frequencyColorWaveform()->midDarkColor(),
                 QStringLiteral("#9a4bc2"));
        QCOMPARE(secondLoad.frequencyColorWaveform()->highDarkColor(),
                 QStringLiteral("#2e9b61"));
    }

    persisted.clear();
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyLowColor"),
                       QStringLiteral("#123456"));
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyStrength"), 0.31);
    SettingsController partial;
    auto* fcw = partial.frequencyColorWaveform();
    QCOMPARE(fcw->preset(), QStringLiteral("custom"));
    QCOMPARE(fcw->lowDarkColor(), QStringLiteral("#123456"));
    QCOMPARE(fcw->midDarkColor(), QStringLiteral("#c66b55"));
    QCOMPARE(fcw->highDarkColor(), QStringLiteral("#b5a4c6"));
    QCOMPARE(fcw->lowDarkOpacity(), 0.22);
    QCOMPARE(fcw->midDarkOpacity(), 0.19);
    QCOMPARE(fcw->highDarkOpacity(), 0.23);
    QCOMPARE(persisted.value(
                 QStringLiteral("appearance/waveformFrequencyMidColor")).toString(),
             QStringLiteral("#c66b55"));

    fcw->setLowLightColor(QStringLiteral("#aabbcc"));
    QCOMPARE(fcw->lowDarkColor(), QStringLiteral("#123456"));
    fcw->setMidLightColor(QStringLiteral("#bbccdd"));
    QVERIFY(fcw->midDarkColor() != QStringLiteral("#c66b55"));

    persisted.clear();
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyLowColor"),
                       QStringLiteral("#112233"));
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyMidColor"),
                       QStringLiteral("#445566"));
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyHighColor"),
                       QStringLiteral("#778899"));
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyStrength"), 0.625);
    SettingsController unroundedStrength;
    auto* unroundedFcw = unroundedStrength.frequencyColorWaveform();
    QCOMPARE(unroundedFcw->lowDarkOpacity(), 0.44);
    QCOMPARE(unroundedFcw->midDarkOpacity(), 0.38);
    QCOMPARE(unroundedFcw->highDarkOpacity(), 0.46);
    persisted.clear();
}

void SettingsControllerTest::frequencyColorMigrationWaitsForPayloadSync()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString blockedParent = temporaryDirectory.filePath(QStringLiteral("blocked"));
    QFile blocker(blockedParent);
    QVERIFY(blocker.open(QIODevice::WriteOnly));
    blocker.close();
    const QString iniPath = blockedParent + QStringLiteral("/frequency.ini");

    QSettings blocked(iniPath, QSettings::IniFormat);
    FrequencyColorWaveformSettings waveform;
    waveform.load(blocked);
    blocked.sync();
    QCOMPARE(blocked.status(), QSettings::AccessError);
    QVERIFY(!blocked.contains(QStringLiteral("waveformFrequencyColorSchema")));

    blocker.remove();
    QVERIFY(QDir().mkpath(blockedParent));
    QSettings recovered(iniPath, QSettings::IniFormat);
    recovered.setValue(QStringLiteral("waveformFrequencyLowColor"),
                       QStringLiteral("#112233"));
    recovered.setValue(QStringLiteral("waveformFrequencyMidColor"),
                       QStringLiteral("#445566"));
    recovered.setValue(QStringLiteral("waveformFrequencyHighColor"),
                       QStringLiteral("#778899"));
    recovered.setValue(QStringLiteral("waveformFrequencyStrength"), 0.625);
    recovered.sync();
    QCOMPARE(recovered.status(), QSettings::NoError);

    FrequencyColorWaveformSettings remigrated;
    remigrated.load(recovered);
    QCOMPARE(remigrated.preset(), QStringLiteral("custom"));
    QCOMPARE(remigrated.lowDarkOpacity(), 0.44);
    QCOMPARE(recovered.value(QStringLiteral("waveformFrequencyColorSchema")).toInt(), 1);
}

void SettingsControllerTest::migratesPreviousFrequencyDefaultsWithoutOverwritingCustomValues()
{
    QSettings persisted;
    persisted.clear();
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyLowColor"),
                       QStringLiteral("#ff647c"));
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyMidColor"),
                       QStringLiteral("#3ed6ae"));
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyHighColor"),
                       QStringLiteral("#8a7cff"));
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyStrength"), 0.85);

    {
        SettingsController migrated;
        QCOMPARE(migrated.property("waveformFrequencyLowColor").toString(),
                 QStringLiteral("#269a8e"));
        QCOMPARE(migrated.property("waveformFrequencyMidColor").toString(),
                 QStringLiteral("#c66b55"));
        QCOMPARE(migrated.property("waveformFrequencyHighColor").toString(),
                 QStringLiteral("#b5a4c6"));
        QCOMPARE(migrated.property("waveformFrequencyStrength").toDouble(), 0.62);
    }

    persisted.clear();
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyLowColor"),
                       QStringLiteral("#c45100"));
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyMidColor"),
                       QStringLiteral("#b04bcd"));
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyHighColor"),
                       QStringLiteral("#0a819a"));
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyStrength"), 0.85);

    {
        SettingsController migrated;
        QCOMPARE(migrated.property("waveformFrequencyLowColor").toString(),
                 QStringLiteral("#269a8e"));
        QCOMPARE(migrated.property("waveformFrequencyMidColor").toString(),
                 QStringLiteral("#c66b55"));
        QCOMPARE(migrated.property("waveformFrequencyHighColor").toString(),
                 QStringLiteral("#b5a4c6"));
        QCOMPARE(migrated.property("waveformFrequencyStrength").toDouble(), 0.62);
    }

    persisted.clear();
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyLowColor"),
                       QStringLiteral("#c65332"));
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyMidColor"),
                       QStringLiteral("#9a4bc2"));
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyHighColor"),
                       QStringLiteral("#2e9b61"));
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyStrength"), 0.7);

    {
        SettingsController migrated;
        QCOMPARE(migrated.property("waveformFrequencyLowColor").toString(),
                 QStringLiteral("#269a8e"));
        QCOMPARE(migrated.property("waveformFrequencyMidColor").toString(),
                 QStringLiteral("#c66b55"));
        QCOMPARE(migrated.property("waveformFrequencyHighColor").toString(),
                 QStringLiteral("#b5a4c6"));
        QCOMPARE(migrated.property("waveformFrequencyStrength").toDouble(), 0.62);
        QCOMPARE(persisted.value(QStringLiteral("appearance/waveformFrequencyStrength")).toDouble(),
                 0.62);
    }

    persisted.clear();
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyLowColor"),
                       QStringLiteral("#c47a5e"));
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyMidColor"),
                       QStringLiteral("#7787b5"));
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyHighColor"),
                       QStringLiteral("#4f9279"));
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyStrength"), 0.62);

    {
        SettingsController migrated;
        QCOMPARE(migrated.property("waveformFrequencyLowColor").toString(),
                 QStringLiteral("#269a8e"));
        QCOMPARE(migrated.property("waveformFrequencyMidColor").toString(),
                 QStringLiteral("#c66b55"));
        QCOMPARE(migrated.property("waveformFrequencyHighColor").toString(),
                 QStringLiteral("#b5a4c6"));
        QCOMPARE(migrated.property("waveformFrequencyStrength").toDouble(), 0.62);
    }

    persisted.clear();
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyLowColor"),
                       QStringLiteral("#315cff"));
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyMidColor"),
                       QStringLiteral("#ffb02e"));
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyHighColor"),
                       QStringLiteral("#f04ca5"));
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyStrength"), 0.62);

    {
        SettingsController migrated;
        QCOMPARE(migrated.property("waveformFrequencyLowColor").toString(),
                 QStringLiteral("#269a8e"));
        QCOMPARE(migrated.property("waveformFrequencyMidColor").toString(),
                 QStringLiteral("#c66b55"));
        QCOMPARE(migrated.property("waveformFrequencyHighColor").toString(),
                 QStringLiteral("#b5a4c6"));
        QCOMPARE(migrated.property("waveformFrequencyStrength").toDouble(), 0.62);
    }

    persisted.clear();
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyLowColor"),
                       QStringLiteral("#62c8a0"));
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyMidColor"),
                       QStringLiteral("#f1d79a"));
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyHighColor"),
                       QStringLiteral("#e98582"));
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyStrength"), 0.62);

    {
        SettingsController migrated;
        QCOMPARE(migrated.property("waveformFrequencyLowColor").toString(),
                 QStringLiteral("#269a8e"));
        QCOMPARE(migrated.property("waveformFrequencyMidColor").toString(),
                 QStringLiteral("#c66b55"));
        QCOMPARE(migrated.property("waveformFrequencyHighColor").toString(),
                 QStringLiteral("#b5a4c6"));
        QCOMPARE(migrated.property("waveformFrequencyStrength").toDouble(), 0.62);
    }

    persisted.clear();
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyLowColor"),
                       QStringLiteral("#112233"));
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyMidColor"),
                       QStringLiteral("#9a4bc2"));
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyHighColor"),
                       QStringLiteral("#2e9b61"));
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyStrength"), 0.7);

    {
        SettingsController customized;
        QCOMPARE(customized.property("waveformFrequencyLowColor").toString(),
                 QStringLiteral("#112233"));
        QCOMPARE(customized.property("waveformFrequencyMidColor").toString(),
                 QStringLiteral("#9a4bc2"));
        QCOMPARE(customized.property("waveformFrequencyHighColor").toString(),
                 QStringLiteral("#2e9b61"));
        QCOMPARE(customized.property("waveformFrequencyStrength").toDouble(), 0.7);
    }

    persisted.clear();
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyLowColor"),
                       QStringLiteral("#62c8a0"));
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyMidColor"),
                       QStringLiteral("#f1d79a"));
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyHighColor"),
                       QStringLiteral("#e98582"));
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyStrength"), 0.55);

    {
        SettingsController customized;
        QCOMPARE(customized.property("waveformFrequencyLowColor").toString(),
                 QStringLiteral("#62c8a0"));
        QCOMPARE(customized.property("waveformFrequencyMidColor").toString(),
                 QStringLiteral("#f1d79a"));
        QCOMPARE(customized.property("waveformFrequencyHighColor").toString(),
                 QStringLiteral("#e98582"));
        QCOMPARE(customized.property("waveformFrequencyStrength").toDouble(), 0.55);
    }

    persisted.clear();
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyLowColor"),
                       QStringLiteral("#c47a5e"));
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyMidColor"),
                       QStringLiteral("#7787b5"));
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyHighColor"),
                       QStringLiteral("#4f9279"));
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyStrength"), 0.55);

    {
        SettingsController customized;
        QCOMPARE(customized.property("waveformFrequencyLowColor").toString(),
                 QStringLiteral("#c47a5e"));
        QCOMPARE(customized.property("waveformFrequencyMidColor").toString(),
                 QStringLiteral("#7787b5"));
        QCOMPARE(customized.property("waveformFrequencyHighColor").toString(),
                 QStringLiteral("#4f9279"));
        QCOMPARE(customized.property("waveformFrequencyStrength").toDouble(), 0.55);
    }
    persisted.clear();
}

void SettingsControllerTest::cacheLimitMigratesOnlyUntouchedLegacyDefault()
{
    QSettings persisted;
    persisted.clear();
    persisted.setValue(QStringLiteral("cache/schemaVersion"), 1);
    persisted.setValue(QStringLiteral("cache/sizeLimitUserModified"), false);
    persisted.setValue(QStringLiteral("cache/sizeLimitMB"), 1024);
    SettingsController migrated;
    QCOMPARE(migrated.cacheSizeLimitMB(), 10 * 1024);
    QCOMPARE(persisted.value(QStringLiteral("cache/sizeLimitMB")).toInt(), 10 * 1024);

    persisted.clear();
    // Schema-less installations cannot reliably distinguish an old default
    // from a deliberate 1 GB choice, so their stored value is preserved.
    persisted.setValue(QStringLiteral("cache/sizeLimitMB"), 1024);
    SettingsController unmarkedLegacyDefault;
    QCOMPARE(unmarkedLegacyDefault.cacheSizeLimitMB(), 1024);
    QCOMPARE(persisted.value(QStringLiteral("cache/schemaVersion")).toInt(), 2);

    persisted.clear();
    // Explicit user interaction records the marker even when the selected
    // value happens to equal the former default.
    persisted.setValue(QStringLiteral("cache/sizeLimitUserModified"), true);
    persisted.setValue(QStringLiteral("cache/sizeLimitMB"), 1024);
    SettingsController explicitlyCustomized;
    QCOMPARE(explicitlyCustomized.cacheSizeLimitMB(), 1024);

    persisted.clear();
    persisted.setValue(QStringLiteral("cache/schemaVersion"), 1);
    persisted.setValue(QStringLiteral("cache/sizeLimitUserModified"), true);
    persisted.setValue(QStringLiteral("cache/sizeLimitMB"), 1024);
    SettingsController customizedLegacyDefault;
    QCOMPARE(customizedLegacyDefault.cacheSizeLimitMB(), 1024);

    persisted.clear();
    persisted.setValue(QStringLiteral("cache/sizeLimitMB"), 2048);
    SettingsController customized;
    QCOMPARE(customized.cacheSizeLimitMB(), 2048);
    persisted.clear();
}

void SettingsControllerTest::restoresLegacyRgbColorsWithoutDeletingKeys()
{
    QSettings persisted;
    persisted.clear();
    persisted.setValue(QStringLiteral("appearance/waveformRgbBaseColor"), "#112233");
    persisted.setValue(QStringLiteral("appearance/waveformRgbStartColor"), "#445566");
    persisted.setValue(QStringLiteral("appearance/waveformRgbMiddleColor"), "#556677");
    persisted.setValue(QStringLiteral("appearance/waveformRgbEndColor"), "#667788");
    persisted.setValue(QStringLiteral("appearance/waveformRgbProgress"), true);
    persisted.setValue(QStringLiteral("appearance/waveformUnplayedColor"), "#badbad");
    persisted.setValue(QStringLiteral("appearance/spectrumColorMode"), 1);
    persisted.setValue(QStringLiteral("appearance/spectrumRgbStartColor"), "#778899");
    persisted.setValue(QStringLiteral("appearance/spectrumRgbMiddleColor"), "#aabbcc");
    SettingsController settings;
    QCOMPARE(settings.property("waveformRgbBaseColor").toString(), QStringLiteral("#112233"));
    QCOMPARE(settings.property("waveformRgbStartColor").toString(), QStringLiteral("#445566"));
    QCOMPARE(settings.property("waveformRgbMiddleColor").toString(), QStringLiteral("#556677"));
    QCOMPARE(settings.property("waveformRgbEndColor").toString(), QStringLiteral("#667788"));
    QCOMPARE(settings.property("waveformRgbProgress").toBool(), true);
    QCOMPARE(settings.property("spectrumRgbStartColor").toString(), QStringLiteral("#778899"));
    QCOMPARE(settings.property("spectrumRgbMiddleColor").toString(), QStringLiteral("#aabbcc"));
    QVERIFY(persisted.contains(QStringLiteral("appearance/waveformRgbBaseColor")));
    QVERIFY(persisted.contains(QStringLiteral("appearance/spectrumRgbStartColor")));
    persisted.clear();
}

void SettingsControllerTest::mapsSimplifiedColorsIntoRestoredContract()
{
    QSettings persisted;
    persisted.clear();
    persisted.setValue(QStringLiteral("appearance/visualColorSchema"), 1);
    persisted.setValue(QStringLiteral("appearance/waveformUnplayedColor"), "#112233");
    persisted.setValue(QStringLiteral("appearance/waveformPlayedColor"), "#445566");
    persisted.setValue(QStringLiteral("appearance/spectrumUnplayedColor"), "#778899");
    persisted.setValue(QStringLiteral("appearance/spectrumPlayedColor"), "#aabbcc");

    SettingsController settings;
    QCOMPARE(settings.property("waveformSolidBaseColor").toString(), QStringLiteral("#112233"));
    QCOMPARE(settings.property("waveformSolidProgressColor").toString(), QStringLiteral("#445566"));
    QCOMPARE(settings.property("waveformRgbBaseColor").toString(), QStringLiteral("#112233"));
    QCOMPARE(settings.property("waveformRgbStartColor").toString(), QStringLiteral("#445566"));
    QCOMPARE(settings.property("waveformRgbMiddleColor").toString(), QStringLiteral("#7b2ff7"));
    QCOMPARE(settings.property("waveformRgbEndColor").toString(), QStringLiteral("#e62e9b"));
    QCOMPARE(settings.property("spectrumSolidColor").toString(), QStringLiteral("#778899"));
    QCOMPARE(settings.property("spectrumRgbStartColor").toString(), QStringLiteral("#778899"));
    QCOMPARE(settings.property("spectrumRgbMiddleColor").toString(), QStringLiteral("#aabbcc"));
    QCOMPARE(settings.property("spectrumRgbEndColor").toString(), QStringLiteral("#e62e9b"));
    QVERIFY(persisted.contains(QStringLiteral("appearance/waveformUnplayedColor")));
    QVERIFY(persisted.contains(QStringLiteral("appearance/spectrumPlayedColor")));
    persisted.clear();
}

void SettingsControllerTest::listWaveformThumbnailSettingsPersistFallbackAndReset()
{
    // Catches missing appearance persistence, accepting an unsupported mode,
    // or either reset path leaving list-thumbnail state non-default.
    QSettings persisted;
    persisted.clear();
    {
        SettingsController settings;
        QCOMPARE(settings.listWaveformThumbnailEnabled(), true);
        QCOMPARE(settings.listWaveformThumbnailMode(),
                 QStringLiteral("Color36"));

        settings.setListWaveformThumbnailEnabled(false);
        settings.setListWaveformThumbnailMode(QStringLiteral("Mono"));
    }

    SettingsController reloaded;
    QCOMPARE(reloaded.listWaveformThumbnailEnabled(), false);
    QCOMPARE(reloaded.listWaveformThumbnailMode(), QStringLiteral("Mono"));
    QCOMPARE(persisted.value(
                 QStringLiteral("appearance/listWaveformThumbnailEnabled"))
                 .toBool(),
             false);
    QCOMPARE(persisted.value(
                 QStringLiteral("appearance/listWaveformThumbnailMode"))
                 .toString(),
             QStringLiteral("Mono"));

    QSignalSpy modeChanged(&reloaded,
                           &SettingsController::listWaveformThumbnailModeChanged);
    reloaded.setListWaveformThumbnailMode(QStringLiteral("unsupported"));
    QCOMPARE(reloaded.listWaveformThumbnailMode(), QStringLiteral("Color36"));
    QCOMPARE(modeChanged.count(), 1);

    reloaded.setListWaveformThumbnailEnabled(false);
    reloaded.setListWaveformThumbnailMode(QStringLiteral("Mono"));
    reloaded.resetWaveformDefaults();
    QCOMPARE(reloaded.listWaveformThumbnailEnabled(), true);
    QCOMPARE(reloaded.listWaveformThumbnailMode(), QStringLiteral("Color36"));
    QCOMPARE(reloaded.spectrumColorMode(), 1);
    QCOMPARE(reloaded.spectrumSolidColor(), QStringLiteral("#7b2ff7"));

    reloaded.setListWaveformThumbnailEnabled(false);
    reloaded.setListWaveformThumbnailMode(QStringLiteral("Mono"));
    QSignalSpy enabledReset(
        &reloaded,
        &SettingsController::listWaveformThumbnailEnabledChanged);
    QSignalSpy modeReset(
        &reloaded,
        &SettingsController::listWaveformThumbnailModeChanged);
    reloaded.resetToDefaults();
    QCOMPARE(reloaded.listWaveformThumbnailEnabled(), false);
    QCOMPARE(reloaded.listWaveformThumbnailMode(), QStringLiteral("Mono"));
    QCOMPARE(enabledReset.count(), 0);
    QCOMPARE(modeReset.count(), 0);

    persisted.setValue(QStringLiteral("appearance/listWaveformThumbnailMode"),
                       QStringLiteral("invalid-on-disk"));
    SettingsController invalidReload;
    QCOMPARE(invalidReload.listWaveformThumbnailMode(),
             QStringLiteral("Color36"));
    persisted.clear();
}

void SettingsControllerTest::visualizerCanvasAndReplayGainSettingsPersist()
{
    QSettings persisted;
    persisted.clear();
    {
        SettingsController settings;
        QCOMPARE(settings.waveformCanvasHeight(), 78);
        QCOMPARE(settings.waveformCanvasLocked(), true);
        QCOMPARE(settings.spectrumColorMode(), 1);
        QCOMPARE(settings.spectrumSolidColor(), QStringLiteral("#7b2ff7"));
        QCOMPARE(settings.spectrumRgbStartColor(), QStringLiteral("#00d4ff"));
        QCOMPARE(settings.spectrumRgbMiddleColor(), QStringLiteral("#7b2ff7"));
        QCOMPARE(settings.spectrumRgbEndColor(), QStringLiteral("#e62e9b"));
        QCOMPARE(settings.replayGainMode(), 0);
        QCOMPARE(settings.replayGainClipProtection(), true);

        settings.setWaveformCanvasHeight(120);
        settings.setWaveformCanvasLocked(false);
        settings.setSpectrumColorMode(0);
        settings.setSpectrumSolidColor(QStringLiteral("#112233"));
        settings.setSpectrumRgbMiddleColor(QStringLiteral("#445566"));
        settings.setReplayGainMode(2);
        settings.setReplayGainClipProtection(false);

        QCOMPARE(settings.waveformCanvasHeight(), 84);
    }

    SettingsController reloaded;
    QCOMPARE(reloaded.waveformCanvasHeight(), 84);
    QCOMPARE(reloaded.waveformCanvasLocked(), false);
    QCOMPARE(reloaded.spectrumColorMode(), 0);
    QCOMPARE(reloaded.spectrumSolidColor(), QStringLiteral("#112233"));
    QCOMPARE(reloaded.spectrumRgbMiddleColor(), QStringLiteral("#445566"));
    QCOMPARE(reloaded.replayGainMode(), 2);
    QCOMPARE(reloaded.replayGainClipProtection(), false);
    persisted.clear();
}

void SettingsControllerTest::retiresLegacySmartPlaylists()
{
    const QDir appData(
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    QVERIFY(QDir().mkpath(appData.path()));
    const QString smart =
        appData.filePath(QStringLiteral("smart-playlists.json"));
    const QString retired = smart + QStringLiteral(".retired.bak");
    QFile::remove(smart);
    QFile::remove(retired);

    QFile smartFile(smart);
    QVERIFY(smartFile.open(QIODevice::WriteOnly));
    QCOMPARE(smartFile.write("{\"legacy\":true}"), 15);
    smartFile.close();
    SettingsController settings;
    QVERIFY(!QFile::exists(smart));
    QVERIFY(QFile::exists(retired));

    QFile::remove(retired);
}

void SettingsControllerTest::autoCleanCacheRemovesOldestFilesWhenOverLimit()
{
    SettingsController settings;
    QSignalSpy trimSpy(&settings, &SettingsController::cacheTrimReport);

    const QString cacheDir = QDir::tempPath()
                             + QStringLiteral("/AgPlayer_cache_janitor_test");
    QDir(cacheDir).removeRecursively();
    QVERIFY(QDir().mkpath(cacheDir));

    settings.setCacheDirectory(cacheDir);
    settings.setAutoCleanCache(true);
    settings.setCacheSizeLimitMB(100);

    const QByteArray payload(45 * 1024 * 1024, 'x');
    auto createFile = [&](const QString& name, int daysOld) {
        QFile file(cacheDir + QStringLiteral("/") + name);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(payload), payload.size());
        const QDateTime time = QDateTime::currentDateTime().addDays(-daysOld);
        QVERIFY(file.setFileTime(time, QFileDevice::FileAccessTime)
                || file.setFileTime(time, QFileDevice::FileModificationTime));
        file.close();
    };

    createFile(QStringLiteral("old.agwf"), 5);
    createFile(QStringLiteral("mid.agwf"), 2);
    createFile(QStringLiteral("new.agwf"), 0);

    settings.trimCacheNow();

    QVERIFY(QFile::exists(cacheDir + QStringLiteral("/new.agwf")));
    QVERIFY(QFile::exists(cacheDir + QStringLiteral("/mid.agwf")));
    QVERIFY(!QFile::exists(cacheDir + QStringLiteral("/old.agwf")));

    QCOMPARE(trimSpy.count(), 1);
    const QList<QVariant> args = trimSpy.takeFirst();
    QVERIFY(args.at(0).toLongLong() >= 45LL * 1024 * 1024);
    QCOMPARE(args.at(1).toInt(), 1);

    QDir(cacheDir).removeRecursively();
}

void SettingsControllerTest::supportsOnlyFourLanguages()
{
    SettingsController settings;

    const QStringList supported = {
        QStringLiteral("zh"),
        QStringLiteral("en"),
        QStringLiteral("th"),
        QStringLiteral("vi"),
    };
    for (const QString& language : supported) {
        settings.setLanguage(language);
        QCOMPARE(settings.language(), language);
    }

    const QStringList unsupported = {
        QStringLiteral("ko"),
        QStringLiteral("my"),
        QStringLiteral("lo"),
        QStringLiteral("fr"),
    };
    for (const QString& language : unsupported) {
        settings.setLanguage(language);
        QCOMPARE(settings.language(), QStringLiteral("zh"));
    }
}

void SettingsControllerTest::editSessionCanCommitOrCancel()
{
    SettingsController settings;
    settings.setThemeMode(1);
    settings.setLanguage(QStringLiteral("en"));
    QSettings persisted;
    QCOMPARE(persisted.value(QStringLiteral("appearance/themeMode")).toInt(), 1);
    QCOMPARE(persisted.value(QStringLiteral("general/language")).toString(),
             QStringLiteral("en"));

    settings.beginEdit();
    settings.setThemeMode(2);
    settings.setLanguage(QStringLiteral("th"));
    persisted.sync();
    QCOMPARE(persisted.value(QStringLiteral("appearance/themeMode")).toInt(), 1);
    QCOMPARE(persisted.value(QStringLiteral("general/language")).toString(),
             QStringLiteral("en"));
    settings.resetToDefaults();
    persisted.sync();
    QCOMPARE(persisted.value(QStringLiteral("appearance/themeMode")).toInt(), 1);
    QCOMPARE(persisted.value(QStringLiteral("general/language")).toString(),
             QStringLiteral("en"));
    settings.cancelEdit();
    persisted.sync();
    QCOMPARE(persisted.value(QStringLiteral("appearance/themeMode")).toInt(), 1);
    QCOMPARE(persisted.value(QStringLiteral("general/language")).toString(),
             QStringLiteral("en"));
    QCOMPARE(settings.themeMode(), 1);
    QCOMPARE(settings.language(), QStringLiteral("en"));

    {
        SettingsController reloaded;
        QCOMPARE(reloaded.themeMode(), 1);
        QCOMPARE(reloaded.language(), QStringLiteral("en"));
    }

    settings.beginEdit();
    settings.setThemeMode(2);
    settings.setLanguage(QStringLiteral("vi"));
    persisted.sync();
    QCOMPARE(persisted.value(QStringLiteral("appearance/themeMode")).toInt(), 1);
    QCOMPARE(persisted.value(QStringLiteral("general/language")).toString(),
             QStringLiteral("en"));
    settings.commitEdit();

    SettingsController committed;
    QCOMPARE(committed.themeMode(), 2);
    QCOMPARE(committed.language(), QStringLiteral("vi"));
}

void SettingsControllerTest::testModeDoesNotTouchStartupRegistry()
{
#ifdef Q_OS_WIN
    QSettings run(
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
        QSettings::NativeFormat);
    const QVariant before = run.value(QStringLiteral("AgPlayer"));

    SettingsController settings;
    settings.setAutoStartWithWindows(!settings.autoStartWithWindows());
    settings.beginEdit();
    settings.setAutoStartWithWindows(!settings.autoStartWithWindows());
    settings.rebindFileAssociations();
    settings.commitEdit();

    run.sync();
    QCOMPARE(run.value(QStringLiteral("AgPlayer")), before);
#endif
}

void SettingsControllerTest::rebindFileAssociationsEnablesRegistrationDuringEdit()
{
    QSettings().clear();
    SettingsController settings;
    QVERIFY(!settings.setAsDefaultPlayer());

    settings.beginEdit();
    settings.rebindFileAssociations();
    QVERIFY(settings.setAsDefaultPlayer());
    settings.commitEdit();

    SettingsController reloaded;
    QVERIFY(reloaded.setAsDefaultPlayer());
}

void SettingsControllerTest::iniThemeSettingsPreserveStrictLegacyStrings()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QSettings::Format originalFormat = QSettings::defaultFormat();
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       directory.path());

    QSettings persisted;
    for (const int legacyMode : {0, 1, 2}) {
        persisted.clear();
        persisted.setValue(QStringLiteral("appearance/themeMode"),
                           QString::number(legacyMode));
        persisted.sync();
        SettingsController settings;
        QCOMPARE(settings.themeMode(), legacyMode);
    }

    for (const QString& malformed : {
             QStringLiteral(" 0"),
             QStringLiteral("1.0"),
             QStringLiteral("2extra"),
             QStringLiteral("3")}) {
        persisted.clear();
        persisted.setValue(QStringLiteral("appearance/themeMode"), malformed);
        persisted.sync();
        SettingsController settings;
        QCOMPARE(settings.themeMode(), 0);
    }
    QSettings::setDefaultFormat(originalFormat);
}

QTEST_MAIN(SettingsControllerTest)
#include "settings_controller_test.moc"
