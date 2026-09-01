#include "settings_controller.hpp"
#include "audio_file_discovery.hpp"
#include "file_association_controller.hpp"
#include "frequency_color_waveform_settings.hpp"

#include <QByteArray>
#include <QColor>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QSettings>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <cmath>
#include <limits>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

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
    void restoresLegacyRgbColorsWithoutDeletingKeys();
    void mapsSimplifiedColorsIntoRestoredContract();
    void listWaveformThumbnailSettingsPersistFallbackAndReset();
    void trackWaveformBrightnessDefaultsClampsPersistsAndResets();
    void visualizerCanvasAndReplayGainSettingsPersist();
    void glassFeatureIsAbsentFromSettingsContract();
    void playerShellModeDefaultsPersistsAndNormalizes();
    void windowLayoutThemeDefaultsAndNormalizesToDualWindow();
    void appearanceDefaultsToDarkAndPreservesLegacyModes();
    void appearanceDefaultResetDoesNotTouchMediaSettingsOutsideEdit();
    void retiresLegacySmartPlaylists();
    void autoCleanCacheRemovesOldestFilesWhenOverLimit();
    void supportsOnlyChineseAndEnglish();
    void editSessionCanCommitOrCancel();
    void rebindFileAssociationsEnablesRegistrationDuringEdit();
    void visibleAssociationChoicesRemainAudioOnly();
    void defaultPlayerToggleRegistersAndClearsHiddenVideoCapabilities();
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
        QCOMPARE(settings.waveformMode(), 0);
        QCOMPARE(QColor(settings.waveformSolidBaseColor()),
                 QColor(QStringLiteral("#9098A6")));
        auto* spectral = settings.frequencyColorWaveform();
        QCOMPARE(spectral->palette().size(), 8);
        QCOMPARE(spectral->palette().first().toString(), QStringLiteral("#123ecf"));
        QCOMPARE(spectral->palette().last().toString(), QStringLiteral("#e82718"));
        QCOMPARE(spectral->unplayedOpacity(), 0.88);

        settings.setWaveformHeight(3.0);
        settings.setWaveformDensity(0.1);
        settings.setWaveformThickness(2.34);
        spectral->setPaletteColor(0, QStringLiteral("#112233"));
        spectral->setUnplayedOpacity(0.40);
        QCOMPARE(settings.waveformHeight(), 1.5);
        QCOMPARE(settings.waveformDensity(), 0.5);
        QCOMPARE(settings.waveformThickness(), 2.3);
        QCOMPARE(spectral->unplayedOpacity(), 0.60);

        settings.setWaveformMode(3);
        QVERIFY(QMetaObject::invokeMethod(&settings, "cycleWaveformMode"));
        QCOMPARE(settings.waveformMode(), 1);
        QVERIFY(QMetaObject::invokeMethod(&settings, "cycleWaveformMode"));
        QCOMPARE(settings.waveformMode(), 2);
        QVERIFY(QMetaObject::invokeMethod(&settings, "cycleWaveformMode"));
        QCOMPARE(settings.waveformMode(), 0);
        QVERIFY(QMetaObject::invokeMethod(&settings, "cycleWaveformMode"));
        QCOMPARE(settings.waveformMode(), 3);
    }

    SettingsController reloaded;
    QCOMPARE(reloaded.frequencyColorWaveform()->palette().first().toString(),
             QStringLiteral("#112233"));
    QCOMPARE(reloaded.frequencyColorWaveform()->unplayedOpacity(), 0.60);
    reloaded.resetWaveformDefaults();
    QCOMPARE(reloaded.frequencyColorWaveform()->palette().first().toString(),
             QStringLiteral("#123ecf"));
    QCOMPARE(reloaded.frequencyColorWaveform()->unplayedOpacity(), 0.88);
    QCOMPARE(reloaded.listWaveformThumbnailMode(), QStringLiteral("Spectral"));
    persisted.clear();
}

void SettingsControllerTest::trackWaveformBrightnessDefaultsClampsPersistsAndResets()
{
    QSettings persisted;
    persisted.clear();

    {
        SettingsController settings;
        QCOMPARE(settings.trackWaveformBrightness(), 0.66);
        QSignalSpy changed(&settings,
                           &SettingsController::trackWaveformBrightnessChanged);

        settings.setTrackWaveformBrightness(0.72);
        QCOMPARE(settings.trackWaveformBrightness(), 0.72);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(persisted.value(
                     QStringLiteral("appearance/trackWaveformBrightness"))
                     .toDouble(),
                 0.72);

        settings.setTrackWaveformBrightness(4.0);
        QCOMPARE(settings.trackWaveformBrightness(), 1.0);
        settings.setTrackWaveformBrightness(-3.0);
        QCOMPARE(settings.trackWaveformBrightness(), 0.20);
        settings.setTrackWaveformBrightness(
            std::numeric_limits<double>::quiet_NaN());
        QCOMPARE(settings.trackWaveformBrightness(), 0.66);
    }

    SettingsController reloaded;
    QCOMPARE(reloaded.trackWaveformBrightness(), 0.66);
    reloaded.setTrackWaveformBrightness(0.81);
    reloaded.resetWaveformDefaults();
    QCOMPARE(reloaded.trackWaveformBrightness(), 0.66);
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
                 QStringLiteral("Spectral"));

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
    QCOMPARE(reloaded.listWaveformThumbnailMode(), QStringLiteral("Spectral"));
    QCOMPARE(modeChanged.count(), 1);

    reloaded.setListWaveformThumbnailEnabled(false);
    reloaded.setListWaveformThumbnailMode(QStringLiteral("Mono"));
    reloaded.resetWaveformDefaults();
    QCOMPARE(reloaded.listWaveformThumbnailEnabled(), true);
    QCOMPARE(reloaded.listWaveformThumbnailMode(), QStringLiteral("Spectral"));
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
             QStringLiteral("Spectral"));
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

void SettingsControllerTest::supportsOnlyChineseAndEnglish()
{
    QSettings persisted;
    persisted.clear();

    const QStringList supported = {
        QStringLiteral("zh"),
        QStringLiteral("en"),
    };
    for (const QString& language : supported) {
        SettingsController settings;
        settings.setLanguage(language);
        QCOMPARE(settings.language(), language);
    }

    const QStringList unsupported = {
        QStringLiteral("th"),
        QStringLiteral("vi"),
        QStringLiteral("ko"),
        QStringLiteral("my"),
        QStringLiteral("lo"),
        QStringLiteral("fr"),
    };
    for (const QString& language : unsupported) {
        persisted.setValue(QStringLiteral("general/language"), language);
        SettingsController loaded;
        QCOMPARE(loaded.language(), QStringLiteral("zh"));
        loaded.setLanguage(language);
        QCOMPARE(loaded.language(), QStringLiteral("zh"));
    }
    persisted.clear();
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
    settings.setLanguage(QStringLiteral("zh"));
    persisted.sync();
    QCOMPARE(persisted.value(QStringLiteral("appearance/themeMode")).toInt(), 1);
    QCOMPARE(persisted.value(QStringLiteral("general/language")).toString(),
             QStringLiteral("en"));
    settings.commitEdit();

    SettingsController committed;
    QCOMPARE(committed.themeMode(), 2);
    QCOMPARE(committed.language(), QStringLiteral("zh"));
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

void SettingsControllerTest::visibleAssociationChoicesRemainAudioOnly()
{
    // Catches a hidden video compatibility registration becoming a visible
    // per-format setting or changing the existing audio selection contract.
    QSettings().clear();
    SettingsController settings;
    QCOMPARE(settings.fileAssociations(), agplayer::qt::supportedAudioExtensions());
    for (const QString& extension : agplayer::qt::supportedVideoExtensions()) {
        QVERIFY(!settings.fileAssociations().contains(extension));
    }
}

void SettingsControllerTest::defaultPlayerToggleRegistersAndClearsHiddenVideoCapabilities()
{
#ifdef Q_OS_WIN
    // Catches the existing default-player switch registering only the visible
    // audio choices, or leaving hidden video associations after it is off.
    const bool wasTestModeEnabled = QStandardPaths::isTestModeEnabled();
    QStandardPaths::setTestModeEnabled(false);
    const auto restoreTestMode = qScopeGuard([wasTestModeEnabled] {
        QStandardPaths::setTestModeEnabled(wasTestModeEnabled);
    });
    QSettings persisted;
    persisted.clear();

    {
        SettingsController settings;
        settings.setSetAsDefaultPlayer(true);
        QVERIFY(FileAssociationController().isAssociated(QStringLiteral(".mp4")));

        settings.setSetAsDefaultPlayer(false);
        QVERIFY(!FileAssociationController().isAssociated(QStringLiteral("mp4")));
    }

    const QString capabilityPath = QStringLiteral("Software\\AgPlayer\\Capabilities");
    const std::wstring capabilityPathW = capabilityPath.toStdWString();
    HKEY key = nullptr;
    QVERIFY(RegOpenKeyExW(HKEY_CURRENT_USER, capabilityPathW.c_str(), 0,
                          KEY_READ, &key) != ERROR_SUCCESS);
#else
    QSKIP("Windows Default Apps capabilities are Windows-only");
#endif
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
