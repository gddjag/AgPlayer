#include "settings_controller.hpp"
#include "audio_file_discovery.hpp"
#include "file_association_controller.hpp"
#include "frequency_color_waveform_settings.hpp"
#include "frequency_color_mix.hpp"

#include <QByteArray>
#include <QColor>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QKeySequence>
#include <QRegularExpression>
#include <QSettings>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

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
    void nativeShortcutDisplayPreservesPortableSettings();
    void aboutUpdateServiceIsExplicitlyUnconfigured();
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
    void frequencyColorPaletteMigratesExactLegacyDefaults();
    void frequencyColorPalettePreservesCustomColors();
    void frequencyColorPaletteUpgradesPreviousRgbPreset();
    void frequencyColorMixPreservesPastelAndChroma();
    void frequencyColorMixHonorsPaletteAndRejectsNonfiniteEnergy();
    void frequencyColorMixKeepsCustomEndpointContinuous();
    void frequencyColorMixKeepsQuietAudioVisibleWithoutFlatteningDynamics();
    void restoresLegacyRgbColorsWithoutDeletingKeys();
    void mapsSimplifiedColorsIntoRestoredContract();
    void listWaveformThumbnailSettingsPersistFallbackAndReset();
    void trackWaveformBrightnessDefaultsClampsPersistsAndResets();
    void visualizerCanvasAndReplayGainSettingsPersist();
    void glassFeatureIsAbsentFromSettingsContract();
    void playerShellModeDefaultsPersistsAndNormalizes();
    void rollingBeatGridSettingsDefaultPersistNormalizeAndReset();
    void rollingKeyboardShortcutsNormalizeRejectConflictsPersistAndCancel();
    void rollingKeyboardShortcutsRejectLegacyConflictsBothDirections();
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
        QCOMPARE(settings.listWaveformThumbnailMode(), QStringLiteral("Spectral"));
        auto* frequency = settings.frequencyColorWaveform();
        QCOMPARE(frequency->lowColor(), QColor(QStringLiteral("#ff0000")));
        QCOMPARE(frequency->midColor(), QColor(QStringLiteral("#00ff00")));
        QCOMPARE(frequency->highColor(), QColor(QStringLiteral("#0000ff")));
        QCOMPARE(frequency->unplayedDimness(), 0.30);
        QCOMPARE(frequency->unplayedOpacity(), 0.70);

        settings.setWaveformHeight(3.0);
        settings.setWaveformDensity(0.1);
        settings.setWaveformThickness(2.34);
        frequency->setLowColor(QColor(QStringLiteral("#112233")));
        frequency->setMidColor(QColor(QStringLiteral("#445566")));
        frequency->setHighColor(QColor(QStringLiteral("#778899")));
        frequency->setUnplayedDimness(0.40);
        QCOMPARE(settings.waveformHeight(), 1.5);
        QCOMPARE(settings.waveformDensity(), 0.5);
        QCOMPARE(settings.waveformThickness(), 2.3);
        QCOMPARE(frequency->unplayedDimness(), 0.40);
        QCOMPARE(frequency->unplayedOpacity(), 0.60);

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
    QCOMPARE(reloaded.frequencyColorWaveform()->lowColor(),
             QColor(QStringLiteral("#112233")));
    QCOMPARE(reloaded.frequencyColorWaveform()->midColor(),
             QColor(QStringLiteral("#445566")));
    QCOMPARE(reloaded.frequencyColorWaveform()->highColor(),
             QColor(QStringLiteral("#778899")));
    QCOMPARE(reloaded.frequencyColorWaveform()->unplayedDimness(), 0.40);
    QCOMPARE(reloaded.frequencyColorWaveform()->unplayedOpacity(), 0.60);
    reloaded.resetWaveformDefaults();
    QCOMPARE(reloaded.frequencyColorWaveform()->lowColor(),
             QColor(QStringLiteral("#ff0000")));
    QCOMPARE(reloaded.frequencyColorWaveform()->midColor(),
             QColor(QStringLiteral("#00ff00")));
    QCOMPARE(reloaded.frequencyColorWaveform()->highColor(),
             QColor(QStringLiteral("#0000ff")));
    QCOMPARE(reloaded.frequencyColorWaveform()->unplayedDimness(), 0.30);
    QCOMPARE(reloaded.frequencyColorWaveform()->unplayedOpacity(), 0.70);
    QCOMPARE(reloaded.listWaveformThumbnailMode(), QStringLiteral("Spectral"));
    persisted.clear();
}

void SettingsControllerTest::trackWaveformBrightnessDefaultsClampsPersistsAndResets()
{
    QSettings persisted;
    persisted.clear();

    {
        SettingsController settings;
        QCOMPARE(settings.trackWaveformBrightness(), 0.50);
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
        QCOMPARE(settings.trackWaveformBrightness(), 0.50);
    }

    SettingsController reloaded;
    QCOMPARE(reloaded.trackWaveformBrightness(), 0.50);
    reloaded.setTrackWaveformBrightness(0.81);
    reloaded.resetWaveformDefaults();
    QCOMPARE(reloaded.trackWaveformBrightness(), 0.50);
    persisted.clear();
}

void SettingsControllerTest::frequencyColorPaletteMigratesExactLegacyDefaults()
{
    // Catches an omitted schema migration or a migration that updates only the
    // in-memory values without making the one-time upgrade persistent.
    QSettings persisted;
    persisted.clear();
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyLowColor"),
                       QStringLiteral("#8B3DFF"));
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyMidColor"),
                       QStringLiteral("#FFB000"));
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyHighColor"),
                       QStringLiteral("#002FA7"));
    persisted.setValue(
        QStringLiteral("appearance/waveformFrequencyUnplayedOpacity"), 0.38);

    SettingsController settings;
    QCOMPARE(settings.frequencyColorWaveform()->lowColor(),
             QColor(QStringLiteral("#ff0000")));
    QCOMPARE(settings.frequencyColorWaveform()->midColor(),
             QColor(QStringLiteral("#00ff00")));
    QCOMPARE(settings.frequencyColorWaveform()->highColor(),
             QColor(QStringLiteral("#0000ff")));
    // The palette itself migrates, but an explicitly stored legacy opacity is
    // inverted into dimness so an upgrade keeps the user's visual contrast.
    QCOMPARE(settings.frequencyColorWaveform()->unplayedOpacity(), 0.38);
    QCOMPARE(settings.frequencyColorWaveform()->unplayedDimness(), 0.62);
    QCOMPARE(persisted.value(QStringLiteral(
                 "appearance/waveformFrequencyUnplayedDimness")).toDouble(),
             0.62);
    QCOMPARE(persisted.value(
                 QStringLiteral("appearance/waveformFrequencyLowColor")).toString(),
             QStringLiteral("#ff0000"));
    QCOMPARE(persisted.value(
                 QStringLiteral("appearance/waveformFrequencyMidColor")).toString(),
             QStringLiteral("#00ff00"));
    QCOMPARE(persisted.value(
                 QStringLiteral("appearance/waveformFrequencyHighColor")).toString(),
             QStringLiteral("#0000ff"));
    QCOMPARE(persisted.value(QStringLiteral(
                 "appearance/waveformFrequencyColorSchemaVersion")).toInt(),
             4);

    persisted.clear();
    persisted.setValue(QStringLiteral(
                           "appearance/waveformFrequencyColorSchemaVersion"),
                       1);
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyLowColor"),
                       QStringLiteral("#8B3DFF"));
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyMidColor"),
                       QStringLiteral("#FFB000"));
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyHighColor"),
                       QStringLiteral("#002FA7"));

    SettingsController alreadyMigratedSchema;
    QCOMPARE(alreadyMigratedSchema.frequencyColorWaveform()->lowColor(),
             QColor(QStringLiteral("#8b3dff")));
    QCOMPARE(alreadyMigratedSchema.frequencyColorWaveform()->midColor(),
             QColor(QStringLiteral("#ffb000")));
    QCOMPARE(alreadyMigratedSchema.frequencyColorWaveform()->highColor(),
             QColor(QStringLiteral("#002fa7")));
    QCOMPARE(alreadyMigratedSchema.frequencyColorWaveform()->unplayedOpacity(),
             0.70);
    persisted.clear();
}

void SettingsControllerTest::frequencyColorPalettePreservesCustomColors()
{
    // Catches treating a partly customized legacy palette as an untouched
    // default merely because two of its colors still match old defaults.
    QSettings persisted;
    persisted.clear();
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyLowColor"),
                       QStringLiteral("#8B3DFF"));
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyMidColor"),
                       QStringLiteral("#FFB000"));
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyHighColor"),
                       QStringLiteral("#123456"));

    {
        SettingsController partialCustomization;
        QCOMPARE(partialCustomization.frequencyColorWaveform()->lowColor(),
                 QColor(QStringLiteral("#8b3dff")));
        QCOMPARE(partialCustomization.frequencyColorWaveform()->midColor(),
                 QColor(QStringLiteral("#ffb000")));
        QCOMPARE(partialCustomization.frequencyColorWaveform()->highColor(),
                 QColor(QStringLiteral("#123456")));
    }
    QCOMPARE(persisted.value(QStringLiteral(
                 "appearance/waveformFrequencyColorSchemaVersion")).toInt(),
             4);

    persisted.clear();
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyLowColor"),
                       QStringLiteral("#112233"));
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyMidColor"),
                       QStringLiteral("#445566"));
    persisted.setValue(QStringLiteral("appearance/waveformFrequencyHighColor"),
                       QStringLiteral("#778899"));

    SettingsController customPalette;
    QCOMPARE(customPalette.frequencyColorWaveform()->lowColor(),
             QColor(QStringLiteral("#112233")));
    QCOMPARE(customPalette.frequencyColorWaveform()->midColor(),
             QColor(QStringLiteral("#445566")));
    QCOMPARE(customPalette.frequencyColorWaveform()->highColor(),
             QColor(QStringLiteral("#778899")));
    QCOMPARE(persisted.value(QStringLiteral(
                 "appearance/waveformFrequencyColorSchemaVersion")).toInt(),
             4);
    QCOMPARE(persisted.value(QStringLiteral(
                 "appearance/waveformFrequencyUnplayedOpacity")).toDouble(),
             0.70);
    persisted.clear();
}

void SettingsControllerTest::frequencyColorPaletteUpgradesPreviousRgbPreset()
{
    // Only the entire old preset should migrate; a one-channel customization
    // and an explicitly selected old palette in the new schema must survive.
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings persisted(directory.filePath("palette.ini"), QSettings::IniFormat);
    for (const int schema : {0, 1, 2, 3, 4}) {
        for (const bool customized : {false, true}) {
            persisted.clear();
            persisted.setValue("waveformFrequencyColorSchemaVersion", schema);
            persisted.setValue("waveformFrequencyLowColor", "#fc0909");
            persisted.setValue("waveformFrequencyMidColor", "#03ff00");
            persisted.setValue("waveformFrequencyHighColor",
                               customized ? "#0048fe" : "#0048ff");
            persisted.setValue("waveformFrequencyUnplayedDimness", 0.42);
            FrequencyColorWaveformSettings settings;
            settings.load(persisted);
            const bool migrate = !customized && schema < 4;
            QCOMPARE(settings.lowColor(), QColor(migrate ? "#ff0000" : "#fc0909"));
            QCOMPARE(settings.midColor(), QColor(migrate ? "#00ff00" : "#03ff00"));
            QCOMPARE(settings.highColor(), QColor(customized ? "#0048fe"
                                                             : migrate ? "#0000ff" : "#0048ff"));
            QCOMPARE(settings.unplayedDimness(), 0.42);
            FrequencyColorWaveformSettings reloaded;
            reloaded.load(persisted);
            QCOMPARE(reloaded.lowColor(), settings.lowColor());
            QCOMPARE(reloaded.highColor(), settings.highColor());
        }
    }
}

void SettingsControllerTest::rollingBeatGridSettingsDefaultPersistNormalizeAndReset()
{
    QSettings persisted;
    persisted.clear();

    {
        SettingsController settings;
        QVERIFY(!settings.rollingBeatGridEnabled());
        QCOMPARE(settings.rollingBeatGridGrouping(), 4);
        settings.setRollingBeatGridEnabled(true);
        settings.setRollingBeatGridGrouping(8);
        QCOMPARE(persisted.value(
                     QStringLiteral("appearance/rollingBeatGridEnabled"))
                     .toBool(),
                 true);
        QCOMPARE(persisted.value(
                     QStringLiteral("appearance/rollingBeatGridGrouping"))
                     .toInt(),
                 8);
    }

    SettingsController reloaded;
    QVERIFY(reloaded.rollingBeatGridEnabled());
    QCOMPARE(reloaded.rollingBeatGridGrouping(), 8);

    reloaded.setRollingBeatGridGrouping(3);
    QCOMPARE(reloaded.rollingBeatGridGrouping(), 4);
    reloaded.setRollingBeatGridGrouping(12);
    QCOMPARE(reloaded.rollingBeatGridGrouping(), 4);
    reloaded.setRollingBeatGridGrouping(8);
    reloaded.resetToDefaults();
    QVERIFY(!reloaded.rollingBeatGridEnabled());
    QCOMPARE(reloaded.rollingBeatGridGrouping(), 4);

    persisted.setValue(QStringLiteral("appearance/rollingBeatGridGrouping"),
                       QStringLiteral("invalid"));
    SettingsController malformed;
    QCOMPARE(malformed.rollingBeatGridGrouping(), 4);
    QCOMPARE(persisted.value(
                 QStringLiteral("appearance/rollingBeatGridGrouping"))
                 .toInt(),
             4);
}

void SettingsControllerTest::rollingKeyboardShortcutsNormalizeRejectConflictsPersistAndCancel()
{
    QSettings persisted;
    persisted.clear();

    SettingsController settings;
    QVariantMap shortcuts = settings.rollingKeyboardShortcuts();
    QCOMPARE(shortcuts.size(), 22);
    QCOMPARE(shortcuts.value(QStringLiteral("cue")).toString(),
             QStringLiteral("C"));
    QCOMPARE(shortcuts.value(QStringLiteral("cueJump")).toString(),
             QStringLiteral("Shift+C"));
    QCOMPARE(shortcuts.value(QStringLiteral("cueDelete")).toString(),
             QStringLiteral("Alt+C"));
    QCOMPARE(shortcuts.value(QStringLiteral("gridOrigin")).toString(),
             QStringLiteral("Q"));
    QCOMPARE(shortcuts.value(QStringLiteral("gridLeft")).toString(),
             QStringLiteral("Left"));
    QCOMPARE(shortcuts.value(QStringLiteral("gridRight")).toString(),
             QStringLiteral("Right"));
    for (int index = 1; index <= 8; ++index) {
        QCOMPARE(shortcuts.value(QStringLiteral("hotCue%1").arg(index)).toString(),
                 QString::number(index));
        QCOMPARE(shortcuts.value(
                     QStringLiteral("hotCueDelete%1").arg(index)).toString(),
                 QStringLiteral("Alt+%1").arg(index));
    }

    QVERIFY(!settings.setRollingKeyboardShortcut(QStringLiteral("unknown"),
                                                  QStringLiteral("Ctrl+K")));
    QVERIFY(!settings.setRollingKeyboardShortcut(QStringLiteral("cue"),
                                                  QStringLiteral("1")));
    QVERIFY(!settings.setRollingKeyboardShortcut(
        QStringLiteral("cue"), QStringLiteral("Ctrl+K, Ctrl+C")));
    QVERIFY(!settings.setRollingKeyboardShortcut(
        QStringLiteral("cue"), QStringLiteral("Ctrl + K , Ctrl + C")));
    QVERIFY(settings.setRollingKeyboardShortcut(QStringLiteral("cue"),
                                                 QStringLiteral(" Ctrl + K ")));
    QCOMPARE(settings.rollingKeyboardShortcuts()
                 .value(QStringLiteral("cue")).toString(),
             QStringLiteral("Ctrl+K"));
    QVERIFY(settings.setRollingKeyboardShortcut(QStringLiteral("cue"),
                                                 QString()));
    QVERIFY(settings.setRollingKeyboardShortcut(QStringLiteral("cue"),
                                                 QStringLiteral(" Meta + K ")));
    QCOMPARE(settings.rollingKeyboardShortcuts()
                 .value(QStringLiteral("cue")).toString(),
             QStringLiteral("Meta+K"));
    QVERIFY(settings.setRollingKeyboardShortcut(QStringLiteral("cueJump"),
                                                 QString()));
    QVERIFY(settings.setRollingKeyboardShortcut(QStringLiteral("cue"),
                                                 QStringLiteral("Shift+C")));

    SettingsController reloaded;
    QCOMPARE(reloaded.rollingKeyboardShortcuts()
                 .value(QStringLiteral("cue")).toString(),
             QStringLiteral("Shift+C"));
    QCOMPARE(reloaded.rollingKeyboardShortcuts()
                 .value(QStringLiteral("cueJump")).toString(), QString());

    reloaded.beginEdit();
    QVERIFY(reloaded.setRollingKeyboardShortcut(QStringLiteral("gridOrigin"),
                                                 QStringLiteral("Ctrl+G")));
    reloaded.resetRollingKeyboardShortcuts();
    QCOMPARE(reloaded.rollingKeyboardShortcuts()
                 .value(QStringLiteral("cue")).toString(),
             QStringLiteral("C"));
    reloaded.cancelEdit();
    QCOMPARE(reloaded.rollingKeyboardShortcuts()
                 .value(QStringLiteral("cue")).toString(),
             QStringLiteral("Shift+C"));
    QCOMPARE(reloaded.rollingKeyboardShortcuts()
                 .value(QStringLiteral("cueJump")).toString(), QString());

    reloaded.resetRollingKeyboardShortcuts();
    QCOMPARE(reloaded.rollingKeyboardShortcuts()
                 .value(QStringLiteral("hotCueDelete8")).toString(),
             QStringLiteral("Alt+8"));
    SettingsController defaultsReloaded;
    QCOMPARE(defaultsReloaded.rollingKeyboardShortcuts()
                 .value(QStringLiteral("cueJump")).toString(),
             QStringLiteral("Shift+C"));
    QVERIFY(defaultsReloaded.setRollingKeyboardShortcut(
        QStringLiteral("cue"), QString()));
    QVERIFY(defaultsReloaded.setRollingKeyboardShortcut(
        QStringLiteral("cueJump"), QString()));
    QVERIFY(defaultsReloaded.setRollingKeyboardShortcut(
        QStringLiteral("cue"), QStringLiteral("Shift+C")));
    QVERIFY(defaultsReloaded.setRollingKeyboardShortcut(
        QStringLiteral("cueJump"), QStringLiteral("C")));
    SettingsController swappedReloaded;
    QCOMPARE(swappedReloaded.rollingKeyboardShortcuts()
                 .value(QStringLiteral("cue")).toString(),
             QStringLiteral("Shift+C"));
    QCOMPARE(swappedReloaded.rollingKeyboardShortcuts()
                 .value(QStringLiteral("cueJump")).toString(),
             QStringLiteral("C"));
    persisted.clear();
}

void SettingsControllerTest::rollingKeyboardShortcutsRejectLegacyConflictsBothDirections()
{
    QSettings persisted;
    persisted.clear();

    SettingsController settings;
    QVERIFY(!settings.setRollingKeyboardShortcut(
        QStringLiteral("cue"), QStringLiteral("Ctrl+F")));
    QVERIFY(!settings.setRollingKeyboardShortcut(
        QStringLiteral("cue"), QStringLiteral("Tab")));
    QVERIFY(!settings.setRollingKeyboardShortcut(
        QStringLiteral("cue"), QStringLiteral("Alt+D")));
    QVERIFY(!settings.setRollingKeyboardShortcut(
        QStringLiteral("cue"), QStringLiteral("MediaPlayPause")));
    QVERIFY(!settings.setRollingKeyboardShortcut(
        QStringLiteral("cue"), QStringLiteral("MediaPrevTrack")));
    QCOMPARE(settings.rollingKeyboardShortcuts()
                 .value(QStringLiteral("cue")).toString(),
             QStringLiteral("C"));

    settings.setHkSearch(QStringLiteral("C"));
    QCOMPARE(settings.hkSearch(), QStringLiteral("Ctrl + F"));
    settings.setHkSearch(QStringLiteral("Ctrl + G"));
    QCOMPARE(settings.hkSearch(), QStringLiteral("Ctrl + G"));
    QVERIFY(!settings.setRollingKeyboardShortcut(
        QStringLiteral("cue"), QStringLiteral("Ctrl+G")));

    QVERIFY(settings.setRollingKeyboardShortcut(QStringLiteral("cue"),
                                                 QString()));
    settings.setHkPlayPause(QStringLiteral("Alt + X"));
    QCOMPARE(settings.hkPlayPause(), QStringLiteral("Alt + X"));
    QVERIFY(!settings.setRollingKeyboardShortcut(
        QStringLiteral("cue"), QStringLiteral("Space")));

    settings.setHkSearch(QStringLiteral("C"));
    QCOMPARE(settings.hkSearch(), QStringLiteral("C"));
    settings.resetRollingKeyboardShortcuts();
    QCOMPARE(settings.rollingKeyboardShortcuts()
                 .value(QStringLiteral("cue")).toString(), QString());

    persisted.clear();
    persisted.setValue(QStringLiteral("hotkeys/search"), QStringLiteral("C"));
    SettingsController legacyReloaded;
    QCOMPARE(legacyReloaded.hkSearch(), QStringLiteral("C"));
    QCOMPARE(legacyReloaded.rollingKeyboardShortcuts()
                 .value(QStringLiteral("cue")).toString(), QString());
    persisted.clear();
}

void SettingsControllerTest::aboutUpdateServiceIsExplicitlyUnconfigured()
{
    SettingsController settings;
    const QVariant serviceProperty = settings.property("updateChecker");
    QVERIFY2(serviceProperty.isValid(), "About must expose a real update service, not a static latest-version label");
    QObject* service = serviceProperty.value<QObject*>();
    QVERIFY(service);
    const QString currentVersion = service->property("currentVersion").toString();
    QVERIFY(QRegularExpression(QStringLiteral("^[0-9]+\\.[0-9]+\\.[0-9]+$")).match(currentVersion).hasMatch());
    // Configured builds must also run offline tests without contacting production.
    if (service->property("state").toString() == QStringLiteral("unconfigured")) {
        QVERIFY(QMetaObject::invokeMethod(service, "check"));
        QCOMPARE(service->property("state").toString(), QStringLiteral("unconfigured"));
    } else {
        QCOMPARE(service->property("state").toString(), QStringLiteral("idle"));
    }
    QCOMPARE(service->property("updateAvailable").toBool(), false);
    QSignalSpy translatedStatus(service, SIGNAL(changed()));
    const QString previousLanguage = settings.language();
    const auto restoreLanguage = qScopeGuard([&] { settings.setLanguage(previousLanguage); });
    settings.setLanguage(settings.language() == QStringLiteral("en")
                             ? QStringLiteral("zh") : QStringLiteral("en"));
    QTRY_VERIFY(translatedStatus.count() > 0);
}

void SettingsControllerTest::nativeShortcutDisplayPreservesPortableSettings()
{
    SettingsController settings;
    const QString original = settings.hkPlayPause();
    const auto restore = qScopeGuard([&] { settings.setHkPlayPause(original); });
    const QString portable = QStringLiteral("Ctrl+Shift+P / Space");
    settings.setHkPlayPause(portable);
    const QString before = settings.hkPlayPause();
#ifdef Q_OS_MACOS
    QCOMPARE(settings.shortcutDisplayText(QStringLiteral("Ctrl + Shift + P / Space")),
             QKeySequence(QStringLiteral("Ctrl+Shift+P")).toString(QKeySequence::NativeText)
                 + QStringLiteral(" / ")
                 + QKeySequence(QStringLiteral("Space")).toString(QKeySequence::NativeText));
    QCOMPARE(settings.shortcutDisplayText(QStringLiteral("Ctrl+/")),
             QKeySequence(QStringLiteral("Ctrl+/")).toString(QKeySequence::NativeText));
#else
    QCOMPARE(settings.shortcutDisplayText(portable), portable);
#endif
    QCOMPARE(settings.hkPlayPause(), before);
}

void SettingsControllerTest::frequencyColorMixPreservesPastelAndChroma()
{
    const auto mix = [](double low, double mid, double high) {
        return agplayer::ui::mixFrequencyColor(low, mid, high,
                                              Qt::red, Qt::green, Qt::blue);
    };
    QCOMPARE(mix(1, 1, 1), QColor(Qt::white));
    QCOMPARE(mix(1, 1, 0), QColor(Qt::yellow));
    QCOMPARE(mix(0, 1, 1), QColor(Qt::cyan));
    QCOMPARE(mix(1, 0, 1), QColor(Qt::magenta));
    const QColor pastel = mix(0.7, 0.8, 0.9);
    // Near-balanced bands remain light, but no longer wash toward white:
    // the approved RGB contrast must retain their relative band differences.
    QVERIFY(pastel.redF() > 0.85 && pastel.redF() < 0.90);
    const QColor bassDominant = mix(1.0, 0.2, 0.05);
    QVERIFY(bassDominant.red() >= 245);
    QVERIFY(bassDominant.green() < 130 && bassDominant.blue() < 75);
    QVERIFY(pastel.redF() < pastel.greenF());
    QVERIFY(pastel.greenF() < pastel.blueF());
    // Scaling all energies equally must preserve linear-light chroma. A
    // per-channel knee or common-light subtraction breaks this contract.
    const QColor bright = mix(0.2, 0.5, 0.9);
    const QColor dim = mix(0.02, 0.05, 0.09);
    const double brightRatio = linearChannel(bright.redF()) / linearChannel(bright.blueF());
    const double dimRatio = linearChannel(dim.redF()) / linearChannel(dim.blueF());
    QVERIFY(std::abs(brightRatio - dimRatio) < 0.001);
    QVERIFY(relativeLuminance(dim) < relativeLuminance(bright));
    QColor previous = mix(0.7, 0.8, 0.80);
    for (int step = 1; step <= 100; ++step) {
        const QColor next = mix(0.7, 0.8, 0.80 + 0.001 * step);
        QVERIFY(std::abs(next.redF() - previous.redF()) < 0.002);
        QVERIFY(std::abs(next.blueF() - previous.blueF()) < 0.002);
        previous = next;
    }
}

void SettingsControllerTest::frequencyColorMixHonorsPaletteAndRejectsNonfiniteEnergy()
{
    const QColor low("#804020"), mid("#123456"), high("#d0e0f0");
    const auto mix = [&](double l, double m, double h) {
        return agplayer::ui::mixFrequencyColor(l, m, h, low, mid, high);
    };
    QCOMPARE(mix(1, 0, 0), low);
    QCOMPARE(mix(0, 1, 0), mid);
    QCOMPARE(mix(0, 0, 1), high);
    QCOMPARE(mix(0, 0, 0), low);
    QCOMPARE(mix(std::numeric_limits<double>::quiet_NaN(), 1,
                 std::numeric_limits<double>::infinity()), mid);
    QCOMPARE(mix(-1, 2, 0), mid);
    const QColor mixed = mix(0.3, 0.6, 0.9);
    QVERIFY(mixed.isValid());
    QVERIFY(mixed != agplayer::ui::mixFrequencyColor(0.3, 0.6, 0.9,
                                                    Qt::red, Qt::green, Qt::blue));
}

void SettingsControllerTest::frequencyColorMixKeepsCustomEndpointContinuous()
{
    const QColor custom("#804020");
    const QColor nearEndpoint = agplayer::ui::mixFrequencyColor(
        0.9999, 0, 0, custom, Qt::green, Qt::blue);
    QVERIFY(std::abs(nearEndpoint.redF() - custom.redF()) < 0.001);
    QVERIFY(std::abs(nearEndpoint.greenF() - custom.greenF()) < 0.001);
    QVERIFY(std::abs(nearEndpoint.blueF() - custom.blueF()) < 0.001);
}

void SettingsControllerTest::frequencyColorMixKeepsQuietAudioVisibleWithoutFlatteningDynamics()
{
    const auto mix = [](double low, double mid, double high) {
        return agplayer::ui::mixFrequencyColor(low, mid, high,
                                              Qt::red, Qt::green, Qt::blue);
    };
    const QColor quiet = mix(0.05, 0, 0);
    const QColor veryQuiet = mix(0.001, 0, 0);
    QVERIFY(quiet.red() >= 170);
    QCOMPARE(quiet.green(), 0);
    QCOMPARE(quiet.blue(), 0);
    QVERIFY(veryQuiet.red() < quiet.red());
    QVERIFY(veryQuiet.red() > 0);
    const QColor quietMix = mix(0.02, 0.05, 0.09);
    const QColor brighterMix = mix(0.2, 0.5, 0.9);
    const double quietRatio = linearChannel(quietMix.redF()) / linearChannel(quietMix.blueF());
    const double brighterRatio = linearChannel(brighterMix.redF()) / linearChannel(brighterMix.blueF());
    QVERIFY(std::abs(quietRatio - brighterRatio) < 0.001);
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
    // The controller is injected into a per-test registry namespace. Keep Qt
    // test mode enabled so settings storage and all association writes remain
    // outside the user's production registry keys.
    const QString registryRoot = QStringLiteral("Software\\AgPlayer\\Tests\\%1")
                                     .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    const auto cleanupSandbox = qScopeGuard([registryRoot] {
        const std::wstring registryRootW = registryRoot.toStdWString();
        RegDeleteTreeW(HKEY_CURRENT_USER, registryRootW.c_str());
    });
    {
        SettingsController settings;
        settings.fileAssociationController_ =
            std::make_unique<FileAssociationController>(registryRoot);
        settings.setSetAsDefaultPlayer(true);
        QCOMPARE(settings.fileAssociationController_->registryRootPath(), registryRoot);
        QVERIFY(settings.fileAssociationController_->isAssociated(QStringLiteral(".mp4")));

        settings.setSetAsDefaultPlayer(false);
        QVERIFY(!settings.fileAssociationController_->isAssociated(QStringLiteral("mp4")));
    }

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
