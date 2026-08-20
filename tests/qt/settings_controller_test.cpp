#include "settings_controller.hpp"

#include <QByteArray>
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

class SettingsControllerTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void defaultCacheDirectoryUsesStandardPaths();
    void defaultOutputDirectoryUsesStandardPaths();
    void transcodeDefaultsAreSplitAndPersisted();
    void migratesCombinedTranscodePreset();
    void loadCreatesDefaultDirectories();
    void migratesLegacyDefaultExportDirectory();
    void migratesOldBuiltInExportDirectoryButKeepsCustomDirectory();
    void migratesLegacyPlaybackModes();
    void playbackDeviceSettingsPersistAndMigrateDefaultLabel();
    void waveformAppearanceSettingsClampPersistAndReset();
    void listWaveformThumbnailSettingsPersistFallbackAndReset();
    void visualizerCanvasAndReplayGainSettingsPersist();
    void retiresLegacySmartPlaylists();
    void autoCleanCacheRemovesOldestFilesWhenOverLimit();
    void supportsOnlyFourLanguages();
    void editSessionCanCommitOrCancel();
    void rebindFileAssociationsEnablesRegistrationDuringEdit();
    void testModeDoesNotTouchStartupRegistry();
};

void SettingsControllerTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("AgPlayer"));
    QCoreApplication::setApplicationName(QStringLiteral("AgPlayer-settings-controller-test"));
    QSettings().clear();
}

void SettingsControllerTest::defaultCacheDirectoryUsesStandardPaths()
{
    SettingsController settings;
    const QString cacheDir = settings.cacheDirectory();
    QVERIFY(!cacheDir.contains(QStringLiteral("D:\\Music")));
    const QString expected =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
        + QStringLiteral("/AgPlayer/Cache");
    QCOMPARE(QDir::cleanPath(cacheDir), QDir::cleanPath(expected));
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

        settings.setTranscodeFormat(QStringLiteral("FLAC"));
        settings.setTranscodeBitrateKbps(256);
        settings.setTranscodeSampleRateHz(192000);
        settings.setTranscodeChannels(1);
    }

    SettingsController reloaded;
    QCOMPARE(reloaded.transcodeFormat(), QStringLiteral("FLAC"));
    QCOMPARE(reloaded.transcodeBitrateKbps(), 256);
    QCOMPARE(reloaded.transcodeSampleRateHz(), 192000);
    QCOMPARE(reloaded.transcodeChannels(), 1);
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
        QCOMPARE(settings.waveformSolidBaseColor(), QStringLiteral("#9098a6"));
        QCOMPARE(settings.waveformSolidProgressColor(), QStringLiteral("#d27722"));
        QCOMPARE(settings.waveformRgbBaseColor(), QStringLiteral("#00b4a0"));
        QCOMPARE(settings.waveformMode(), 0);
        QCOMPARE(settings.waveformPlaybackGuide(), false);

        settings.setWaveformHeight(3.0);
        settings.setWaveformDensity(0.1);
        settings.setWaveformThickness(2.34);
        settings.setWaveformPeakAlgorithm(1);
        settings.setWaveformSolidBaseColor(QStringLiteral("#112233"));
        settings.setWaveformSolidProgressColor(QStringLiteral("invalid"));
        settings.setWaveformPlaybackGuide(true);

        QCOMPARE(settings.waveformHeight(), 1.5);
        QCOMPARE(settings.waveformDensity(), 0.5);
        QCOMPARE(settings.waveformThickness(), 2.3);
        QCOMPARE(settings.waveformPeakAlgorithm(), 1);
        QCOMPARE(settings.waveformSolidBaseColor(), QStringLiteral("#112233"));
        QCOMPARE(settings.waveformSolidProgressColor(), QStringLiteral("#d27722"));
        QCOMPARE(settings.waveformPlaybackGuide(), true);
    }

    SettingsController reloaded;
    QCOMPARE(reloaded.waveformHeight(), 1.5);
    QCOMPARE(reloaded.waveformDensity(), 0.5);
    QCOMPARE(reloaded.waveformThickness(), 2.3);
    QCOMPARE(reloaded.waveformPeakAlgorithm(), 1);
    QCOMPARE(reloaded.waveformSolidBaseColor(), QStringLiteral("#112233"));
    QCOMPARE(reloaded.waveformPlaybackGuide(), true);

    reloaded.resetWaveformDefaults();
    QCOMPARE(reloaded.waveformHeight(), 0.8);
    QCOMPARE(reloaded.waveformDensity(), 2.0);
    QCOMPARE(reloaded.waveformThickness(), 1.0);
    QCOMPARE(reloaded.waveformPeakAlgorithm(), 0);
    QCOMPARE(reloaded.waveformSolidBaseColor(), QStringLiteral("#9098a6"));
    QCOMPARE(reloaded.waveformSolidProgressColor(), QStringLiteral("#d27722"));
    QCOMPARE(reloaded.waveformRgbBaseColor(), QStringLiteral("#00b4a0"));
    QCOMPARE(reloaded.waveformMode(), 0);
    QCOMPARE(reloaded.waveformPlaybackGuide(), false);
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

    reloaded.setListWaveformThumbnailEnabled(false);
    reloaded.setListWaveformThumbnailMode(QStringLiteral("Mono"));
    QSignalSpy enabledReset(
        &reloaded,
        &SettingsController::listWaveformThumbnailEnabledChanged);
    QSignalSpy modeReset(
        &reloaded,
        &SettingsController::listWaveformThumbnailModeChanged);
    reloaded.resetToDefaults();
    QCOMPARE(reloaded.listWaveformThumbnailEnabled(), true);
    QCOMPARE(reloaded.listWaveformThumbnailMode(), QStringLiteral("Color36"));
    QVERIFY(enabledReset.count() >= 1);
    QVERIFY(modeReset.count() >= 1);

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
        QCOMPARE(settings.spectrumColorMode(), 0);
        QCOMPARE(settings.spectrumSolidColor(), QStringLiteral("#0078d4"));
        QCOMPARE(settings.spectrumRgbStartColor(), QStringLiteral("#00d4ff"));
        QCOMPARE(settings.spectrumRgbMiddleColor(), QStringLiteral("#7b2ff7"));
        QCOMPARE(settings.spectrumRgbEndColor(), QStringLiteral("#e62e9b"));
        QCOMPARE(settings.replayGainMode(), 0);
        QCOMPARE(settings.replayGainClipProtection(), true);

        settings.setWaveformCanvasHeight(120);
        settings.setWaveformCanvasLocked(false);
        settings.setSpectrumColorMode(1);
        settings.setSpectrumSolidColor(QStringLiteral("#123456"));
        settings.setSpectrumRgbStartColor(QStringLiteral("#112233"));
        settings.setSpectrumRgbMiddleColor(QStringLiteral("#445566"));
        settings.setSpectrumRgbEndColor(QStringLiteral("#778899"));
        settings.setReplayGainMode(2);
        settings.setReplayGainClipProtection(false);

        QCOMPARE(settings.waveformCanvasHeight(), 84);
    }

    SettingsController reloaded;
    QCOMPARE(reloaded.waveformCanvasHeight(), 84);
    QCOMPARE(reloaded.waveformCanvasLocked(), false);
    QCOMPARE(reloaded.spectrumColorMode(), 1);
    QCOMPARE(reloaded.spectrumSolidColor(), QStringLiteral("#123456"));
    QCOMPARE(reloaded.spectrumRgbStartColor(), QStringLiteral("#112233"));
    QCOMPARE(reloaded.spectrumRgbMiddleColor(), QStringLiteral("#445566"));
    QCOMPARE(reloaded.spectrumRgbEndColor(), QStringLiteral("#778899"));
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

QTEST_MAIN(SettingsControllerTest)
#include "settings_controller_test.moc"
