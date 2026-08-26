#include "library_manager_controller.hpp"
#include "import_controller.hpp"
#include "library_model.hpp"
#include "library_store.hpp"

#include <QFile>
#include <QDateTime>
#include <QElapsedTimer>
#include <QProcess>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QTest>

#include <algorithm>
#include <filesystem>

class LibraryManagerControllerTest final : public QObject {
    Q_OBJECT

private slots:
    void marksMissingFilesAndFindsLayeredDuplicates();
    void monitorsUniqueFolders();
    void classifiesCanonicalDropPaths();
    void classifiesEverySupportedAudioExtension_data();
    void classifiesEverySupportedAudioExtension();
    void removesPersistedRootWithoutDeletingFiles();
    void persistsRootsAndImportsNewAudioRecursively();
    void removedLibraryTrackStaysExcludedUntilManualImport();
    void exposesNonDestructiveLibrarySummary();
    void scanRunsAsCancelableBackgroundTask();
    void filtersTenThousandRowsWithoutQmlDelegateChurn();
    void summaryCardsFilterAndSortTheLibrary();
    void backsUpNativeLibraryData();
    void importsBackupAndReplacesTheLiveLibrary();
};

namespace {
QString writeFile(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) return {};
    file.close();
    return path;
}

TrackRecord record(const QString& id, const QString& path, const QString& title,
                   const QByteArray& fingerprint = {})
{
    TrackRecord track;
    track.trackId = id;
    track.path = path;
    track.title = title;
    track.artist = QStringLiteral("Artist");
    track.durationMs = 180000;
    track.format = QStringLiteral("MP3");
    track.audioFingerprint = fingerprint;
    track.available = true;
    return track;
}
}

void LibraryManagerControllerTest::marksMissingFilesAndFindsLayeredDuplicates()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString exactA = writeFile(dir.filePath(QStringLiteral("a.mp3")), "same-bytes");
    const QString exactB = writeFile(dir.filePath(QStringLiteral("b.mp3")), "same-bytes");
    const QString nearA = writeFile(dir.filePath(QStringLiteral("c.mp3")), "encoded-one");
    const QString nearB = writeFile(dir.filePath(QStringLiteral("d.mp3")), "encoded-two");
    QVERIFY(!exactA.isEmpty() && !exactB.isEmpty() && !nearA.isEmpty() && !nearB.isEmpty());

    LibraryModel library;
    library.replaceAll({record(QStringLiteral("a"), exactA, QStringLiteral("One")),
                        record(QStringLiteral("b"), exactB, QStringLiteral("Two")),
                        record(QStringLiteral("c"), nearA, QStringLiteral("Near A"), "fingerprint"),
                        record(QStringLiteral("d"), nearB, QStringLiteral("Near B"), "fingerprint"),
                        record(QStringLiteral("missing"), dir.filePath(QStringLiteral("gone.mp3")),
                               QStringLiteral("Gone"))});
    LibraryManagerController manager;
    manager.setLibraryModel(&library);
    QSignalSpy finished(&manager,
                        &LibraryManagerController::scanFinished);
    manager.rescan();
    QVERIFY(finished.wait(3'000));

    QCOMPARE(manager.missingCount(), 1);
    QCOMPARE(manager.duplicateCount(), 4);
    QCOMPARE(library.trackForId(QStringLiteral("missing"))
                 .value(QStringLiteral("fileStatus")).toString(),
             QStringLiteral("missing"));
    const QVariantList groups = manager.duplicateGroups();
    QCOMPARE(groups.size(), 2);
    QCOMPARE(groups.at(0).toMap().value(QStringLiteral("kind")).toString(),
             QStringLiteral("exact"));
    QCOMPARE(groups.at(1).toMap().value(QStringLiteral("kind")).toString(),
             QStringLiteral("fingerprint"));
}

void LibraryManagerControllerTest::monitorsUniqueFolders()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    LibraryManagerController manager;
    QVERIFY(manager.addMonitoredFolder(dir.path()));
    QVERIFY(!manager.addMonitoredFolder(dir.path()));
    QCOMPARE(manager.monitoredFolders().size(), 1);
    QVERIFY(manager.removeMonitoredFolder(dir.path()));
    QCOMPARE(manager.monitoredFolders().size(), 0);
}

void LibraryManagerControllerTest::classifiesCanonicalDropPaths()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString musicRoot = directory.filePath(QStringLiteral("music"));
    QVERIFY(QDir().mkpath(musicRoot));
    const QString audioPath = writeFile(
        QDir(musicRoot).filePath(QStringLiteral("song.mp3")), "audio");
    const QString textPath = writeFile(
        QDir(musicRoot).filePath(QStringLiteral("notes.txt")), "notes");
    QVERIFY(!audioPath.isEmpty() && !textPath.isEmpty());

    const QString aliasPath = directory.filePath(QStringLiteral("music-alias"));
#ifdef Q_OS_WIN
    QCOMPARE(QProcess::execute(
                 QStringLiteral("cmd.exe"),
                 {QStringLiteral("/d"), QStringLiteral("/c"),
                  QStringLiteral("mklink"), QStringLiteral("/J"),
                  QDir::toNativeSeparators(aliasPath),
                  QDir::toNativeSeparators(musicRoot)}), 0);
#else
    std::error_code linkError;
    std::filesystem::create_directory_symlink(
        std::filesystem::path(musicRoot.toStdWString()),
        std::filesystem::path(aliasPath.toStdWString()), linkError);
    QVERIFY2(!linkError, linkError.message().c_str());
#endif

    LibraryManagerController manager;
    const QVariantMap folder = manager.classifyDropUrl(
        QUrl::fromLocalFile(musicRoot));
    QCOMPARE(folder.value(QStringLiteral("kind"))
                 .value<LibraryManagerController::DropPathKind>(),
             LibraryManagerController::DropPathKind::Directory);
    QCOMPARE(folder.value(QStringLiteral("path")).toString(),
             QFileInfo(musicRoot).canonicalFilePath());

    const QVariantMap alias = manager.classifyDropUrl(
        QUrl::fromLocalFile(aliasPath));
    QCOMPARE(alias.value(QStringLiteral("kind"))
                 .value<LibraryManagerController::DropPathKind>(),
             LibraryManagerController::DropPathKind::Directory);
    QCOMPARE(alias.value(QStringLiteral("path")),
             folder.value(QStringLiteral("path")));

    QVERIFY(manager.addMonitoredFolder(aliasPath));
    QVERIFY(!manager.addMonitoredFolder(musicRoot));
    QCOMPARE(manager.monitoredFolders(),
             QStringList{folder.value(QStringLiteral("path")).toString()});
    QVERIFY(manager.removeMonitoredFolder(musicRoot));

    QCOMPARE(manager.classifyDropUrl(QUrl::fromLocalFile(audioPath))
                 .value(QStringLiteral("kind"))
                 .value<LibraryManagerController::DropPathKind>(),
             LibraryManagerController::DropPathKind::AudioFile);
    QCOMPARE(manager.classifyDropUrl(QUrl::fromLocalFile(textPath))
                 .value(QStringLiteral("kind"))
                 .value<LibraryManagerController::DropPathKind>(),
             LibraryManagerController::DropPathKind::OtherFile);
    QCOMPARE(manager.classifyDropUrl(QUrl::fromLocalFile(
                 directory.filePath(QStringLiteral("missing.mp3"))))
                 .value(QStringLiteral("kind"))
                 .value<LibraryManagerController::DropPathKind>(),
             LibraryManagerController::DropPathKind::Invalid);
    QCOMPARE(manager.classifyDropUrl(QUrl(QStringLiteral("https://example.test/song.mp3")))
                 .value(QStringLiteral("kind"))
                 .value<LibraryManagerController::DropPathKind>(),
             LibraryManagerController::DropPathKind::Invalid);
}

void LibraryManagerControllerTest::classifiesEverySupportedAudioExtension_data()
{
    QTest::addColumn<QString>("extension");
    for (const QString& extension : {
             QStringLiteral("mp3"), QStringLiteral("wav"),
             QStringLiteral("flac"), QStringLiteral("aac"),
             QStringLiteral("m4a"), QStringLiteral("ogg"),
             QStringLiteral("wma"), QStringLiteral("ape"),
             QStringLiteral("opus"), QStringLiteral("aif"),
             QStringLiteral("aiff")}) {
        QTest::newRow(qPrintable(extension)) << extension;
    }
}

void LibraryManagerControllerTest::classifiesEverySupportedAudioExtension()
{
    QFETCH(QString, extension);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeFile(
        dir.filePath(QStringLiteral("track.") + extension.toUpper()), "audio");
    LibraryManagerController manager;
    QCOMPARE(manager.classifyDropUrl(QUrl::fromLocalFile(path))
                 .value(QStringLiteral("kind"))
                 .value<LibraryManagerController::DropPathKind>(),
             LibraryManagerController::DropPathKind::AudioFile);
}

void LibraryManagerControllerTest::removesPersistedRootWithoutDeletingFiles()
{
    // Catches root removal accidentally deleting user audio or leaving the
    // persisted reference/watch contract stale.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString root = dir.filePath(QStringLiteral("music"));
    QVERIFY(QDir().mkpath(root));
    const QString audioPath = writeFile(QDir(root).filePath(QStringLiteral("song.mp3")), "audio");
    const QString settingsPath = dir.filePath(QStringLiteral("roots.json"));
    LibraryManagerController manager;
    manager.setStoragePath(settingsPath);
    QSignalSpy rootsChanged(&manager, &LibraryManagerController::resourceRootsChanged);

    QVERIFY(manager.addMonitoredFolder(root));
    QVERIFY(manager.removeMonitoredFolder(root));
    QVERIFY(QFileInfo::exists(audioPath));
    QCOMPARE(rootsChanged.count(), 2);

    LibraryManagerController restored;
    restored.setStoragePath(settingsPath);
    QVERIFY(restored.monitoredFolders().isEmpty());
}

void LibraryManagerControllerTest::persistsRootsAndImportsNewAudioRecursively()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString musicRoot = directory.filePath(QStringLiteral("music"));
    const QString nested = QDir(musicRoot).filePath(QStringLiteral("nested"));
    QVERIFY(QDir().mkpath(nested));
    const QString audioPath = writeFile(
        QDir(nested).filePath(QStringLiteral("new-track.mp3")), "audio");
    QVERIFY(!audioPath.isEmpty());
    const QString settingsPath = directory.filePath(QStringLiteral("manager.json"));

    LibraryModel library;
    ImportController importer(&library, [](const QString& path) {
        TrackRecord track;
        track.path = path;
        track.title = QFileInfo(path).completeBaseName();
        track.available = true;
        return ProbeResult{AG_OK, track, {}};
    });
    QSignalSpy imported(&importer, &ImportController::finished);
    {
        LibraryManagerController manager;
        manager.setStoragePath(settingsPath);
        manager.setImportController(&importer);
        manager.setLibraryModel(&library);
        QVERIFY(manager.addMonitoredFolder(musicRoot));
        manager.rescan();
        QVERIFY(imported.wait(3'000));
        QCOMPARE(library.count(), 1);
    }

    LibraryManagerController restored;
    restored.setStoragePath(settingsPath);
    QCOMPARE(restored.monitoredFolders(), QStringList({musicRoot}));
}

void LibraryManagerControllerTest::removedLibraryTrackStaysExcludedUntilManualImport()
{
    // Catches monitored-folder rescans immediately resurrecting a track that
    // the user explicitly removed from the all-library view.
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString musicRoot = directory.filePath(QStringLiteral("music"));
    QVERIFY(QDir().mkpath(musicRoot));
    const QString audioPath = writeFile(
        QDir(musicRoot).filePath(QStringLiteral("removed-track.mp3")), "audio");
    const QString settingsPath = directory.filePath(QStringLiteral("manager.json"));

    LibraryModel library;
    ImportController importer(&library, [](const QString& path) {
        TrackRecord track;
        track.path = path;
        track.title = QFileInfo(path).completeBaseName();
        track.available = true;
        return ProbeResult{AG_OK, track, {}};
    });
    LibraryManagerController manager;
    manager.setStoragePath(settingsPath);
    manager.setImportController(&importer);
    manager.setLibraryModel(&library);
    QVERIFY(manager.addMonitoredFolder(musicRoot));

    QSignalSpy imported(&importer, &ImportController::finished);
    manager.rescan();
    QVERIFY(imported.wait(3'000));
    QCOMPARE(library.count(), 1);
    const QString trackId = library.tracks().constFirst().trackId;

    QVERIFY(manager.removeTrackFromLibrary(trackId));
    QCOMPARE(library.count(), 0);
    QSignalSpy scanFinished(&manager,
                            &LibraryManagerController::scanFinished);
    manager.rescan();
    QVERIFY(scanFinished.wait(3'000));
    QTest::qWait(500);
    QCOMPARE(library.count(), 0);

    LibraryManagerController restored;
    restored.setStoragePath(settingsPath);
    restored.setImportController(&importer);
    restored.setLibraryModel(&library);
    QSignalSpy restoredScanFinished(&restored,
                                    &LibraryManagerController::scanFinished);
    restored.rescan();
    QVERIFY(restoredScanFinished.wait(3'000));
    QTest::qWait(500);
    QCOMPARE(library.count(), 0);

    imported.clear();
    importer.importPaths({audioPath});
    QVERIFY(imported.wait(3'000));
    QCOMPARE(library.count(), 1);
    QVERIFY(restored.removeTrackFromLibrary(library.tracks().constFirst().trackId));
}

void LibraryManagerControllerTest::exposesNonDestructiveLibrarySummary()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString firstPath = writeFile(dir.filePath(QStringLiteral("first.mp3")), "12345");
    const QString secondPath = writeFile(dir.filePath(QStringLiteral("second.flac")), "1234567");
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    TrackRecord first = record(QStringLiteral("first"), firstPath, QStringLiteral("First"));
    first.fileSize = 5;
    first.rating = 0;
    first.playCount = 12;
    first.addedAtMs = now - 2 * 24 * 60 * 60 * 1000LL;
    first.lastPlayedAtMs = now - 24 * 60 * 60 * 1000LL;
    TrackRecord second = record(QStringLiteral("second"), secondPath, QStringLiteral("Second"));
    second.fileSize = 7;
    second.rating = 5;
    second.playCount = 2;
    second.addedAtMs = now - 60 * 24 * 60 * 60 * 1000LL;
    LibraryModel library;
    library.replaceAll({first, second});
    LibraryManagerController manager;
    manager.setLibraryModel(&library);
    QTRY_COMPARE_WITH_TIMEOUT(manager.progress(), 100, 3'000);

    QCOMPARE(manager.totalCount(), 2);
    QCOMPARE(manager.totalBytes(), 12);
    QCOMPARE(manager.recentAddedCount(), 1);
    QCOMPARE(manager.recentPlayedCount(), 1);
    QCOMPARE(manager.highFrequencyCount(), 1);
    QCOMPARE(manager.lowFrequencyCount(), 1);
    QCOMPARE(manager.neverPlayedCount(), 0);
    QCOMPARE(manager.unratedCount(), 1);
    QCOMPARE(library.count(), 2); // 扫描与整理建议不得修改资料库。
}

void LibraryManagerControllerTest::scanRunsAsCancelableBackgroundTask()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeFile(
        dir.filePath(QStringLiteral("large.mp3")), QByteArray(8 * 1024 * 1024, 'x'));
    QVERIFY(!path.isEmpty());

    LibraryModel library;
    library.replaceAll({record(QStringLiteral("large"), path,
                               QStringLiteral("Large"))});
    LibraryManagerController manager;
    manager.setLibraryModel(&library);
    QSignalSpy finished(&manager,
                        &LibraryManagerController::scanFinished);
    QList<int> reportedProgress;
    connect(&manager, &LibraryManagerController::progressChanged,
            &manager, [&] { reportedProgress.append(manager.progress()); });

    manager.rescan();
    QVERIFY(manager.scanning());
    manager.cancelScan();
    QTRY_VERIFY_WITH_TIMEOUT(!manager.scanning(), 3'000);

    manager.rescan();
    QVERIFY(manager.scanning());
    QVERIFY(finished.wait(3'000));
    QCOMPARE(manager.progress(), 100);
    QVERIFY(std::any_of(reportedProgress.cbegin(), reportedProgress.cend(),
                        [](int value) { return value > 0 && value < 100; }));
}

void LibraryManagerControllerTest::filtersTenThousandRowsWithoutQmlDelegateChurn()
{
    QList<TrackRecord> tracks;
    tracks.reserve(10'000);
    for (int index = 0; index < 10'000; ++index) {
        TrackRecord track = record(QString::number(index),
            QStringLiteral("C:/virtual/%1.mp3").arg(index),
            index == 9'999 ? QStringLiteral("Needle") : QStringLiteral("Track %1").arg(index));
        track.bpm = 60 + (index % 101);
        track.rating = index % 6;
        tracks.append(std::move(track));
    }
    LibraryModel library;
    library.replaceAll(std::move(tracks));
    LibraryManagerController manager;
    manager.setLibraryModel(&library);
    QTRY_COMPARE_WITH_TIMEOUT(manager.progress(), 100, 5'000);

    QElapsedTimer timer;
    timer.start();
    manager.setKeyword(QStringLiteral("Needle"));
    manager.setBpmRange(60, 160);
    manager.setExactRating(3);
    QVERIFY2(timer.elapsed() < 100, "10K in-memory filter must complete within one frame budget class");
    QCOMPARE(manager.rowCount(), 1);
}

void LibraryManagerControllerTest::summaryCardsFilterAndSortTheLibrary()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    TrackRecord small = record(QStringLiteral("small"),
        writeFile(dir.filePath(QStringLiteral("small.mp3")), "123"),
        QStringLiteral("Small"));
    TrackRecord large = record(QStringLiteral("large"),
        writeFile(dir.filePath(QStringLiteral("large.mp3")), "1234567890"),
        QStringLiteral("Large"));
    TrackRecord medium = record(QStringLiteral("medium"),
        writeFile(dir.filePath(QStringLiteral("medium.mp3")), "12345"),
        QStringLiteral("Medium"));
    small.playCount = 0;
    large.playCount = 12;
    medium.playCount = 2;
    small.fileSize = 3;
    large.fileSize = 10;
    medium.fileSize = 5;

    LibraryModel library;
    library.replaceAll({small, large, medium});
    LibraryManagerController manager;
    manager.setLibraryModel(&library);
    QTRY_COMPARE_WITH_TIMEOUT(manager.progress(), 100, 3'000);

    QVERIFY(manager.setProperty("activeCategory", QStringLiteral("highFrequency")));
    QCOMPARE(manager.rowCount(), 1);
    QCOMPARE(manager.data(manager.index(0), LibraryManagerController::TrackIdRole).toString(),
             QStringLiteral("large"));

    QVERIFY(manager.setProperty("activeCategory", QStringLiteral("storage")));
    QCOMPARE(manager.rowCount(), 3);
    QCOMPARE(manager.data(manager.index(0), LibraryManagerController::TrackIdRole).toString(),
             QStringLiteral("large"));
    QCOMPARE(manager.data(manager.index(2), LibraryManagerController::TrackIdRole).toString(),
             QStringLiteral("small"));
}

void LibraryManagerControllerTest::backsUpNativeLibraryData()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QByteArray payload("{\"version\":3,\"tracks\":[]}");
    const QString source = writeFile(
        dir.filePath(QStringLiteral("library.json")), payload);
    const QString destination = dir.filePath(
        QStringLiteral("AgPlayer曲库数据.db"));
    QVERIFY(!source.isEmpty());

    LibraryManagerController manager;
    manager.setLibraryDataPath(source);
    QVERIFY(manager.backupLibraryData(QUrl::fromLocalFile(destination)));

    QFile backup(destination);
    QVERIFY(backup.open(QIODevice::ReadOnly));
    QCOMPARE(backup.readAll(), payload);
    QCOMPARE(manager.lastBackupPath(), destination);
    QVERIFY(manager.lastBackupError().isEmpty());

    const QString defaultName = manager.defaultBackupUrl().fileName();
    QVERIFY2(defaultName.contains(QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"))),
             qPrintable(QStringLiteral("backup file must include today's date: %1")
                            .arg(defaultName)));
}

void LibraryManagerControllerTest::importsBackupAndReplacesTheLiveLibrary()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString backupPath = dir.filePath(QStringLiteral("backup.db"));
    const QString livePath = dir.filePath(QStringLiteral("library.json"));
    TrackRecord imported = record(QStringLiteral("restored"),
                                  dir.filePath(QStringLiteral("song.mp3")),
                                  QStringLiteral("Restored Song"));
    LibraryStore backupStore(backupPath);
    QVERIFY(backupStore.save({imported}));

    LibraryModel library;
    library.replaceAll({record(QStringLiteral("old"),
                               dir.filePath(QStringLiteral("old.mp3")),
                               QStringLiteral("Old Song"))});
    LibraryManagerController manager;
    manager.setLibraryModel(&library);
    manager.setLibraryDataPath(livePath);

    QVERIFY(manager.importLibraryBackup(QUrl::fromLocalFile(backupPath)));
    QCOMPARE(library.rowCount(), 1);
    QCOMPARE(library.trackForId(QStringLiteral("restored"))
                 .value(QStringLiteral("title")).toString(),
             QStringLiteral("Restored Song"));
    LibraryStore liveStore(livePath);
    QCOMPARE(liveStore.load().size(), 1);
    QCOMPARE(manager.lastBackupPath(), backupPath);
    QVERIFY(manager.lastBackupError().isEmpty());
}

QTEST_GUILESS_MAIN(LibraryManagerControllerTest)
#include "library_manager_controller_test.moc"
