#include "resource_folder_controller.hpp"
#include "import_controller.hpp"
#include "library_model.hpp"
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QTest>
#include <filesystem>

class ResourceFolderControllerTest final : public QObject {
    Q_OBJECT
private slots:
    void monitorsUniqueFolders();
    void classifiesCanonicalDropPaths();
    void classifiesEverySupportedAudioExtension_data();
    void classifiesEverySupportedAudioExtension();
    void removesPersistedRootWithoutDeletingFiles();
    void persistsRootsAndImportsNewAudioRecursively();
    void removedLibraryTrackStaysExcludedUntilManualImport();
    void failedExclusionPersistenceKeepsTrackInLibrary();
    void discoveryDoesNotRunTrackMaintenance();
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


void ResourceFolderControllerTest::monitorsUniqueFolders()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ResourceFolderController manager;
    QVERIFY(manager.addMonitoredFolder(dir.path()));
    QVERIFY(!manager.addMonitoredFolder(dir.path()));
    QCOMPARE(manager.monitoredFolders().size(), 1);
    QVERIFY(manager.removeMonitoredFolder(dir.path()));
    QCOMPARE(manager.monitoredFolders().size(), 0);
}

void ResourceFolderControllerTest::classifiesCanonicalDropPaths()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString musicRoot = directory.filePath(QStringLiteral("music"));
    QVERIFY(QDir().mkpath(musicRoot));
    const QString audioPath = writeFile(
        QDir(musicRoot).filePath(QStringLiteral("song.mp3")), "audio");
    const QString textPath = writeFile(
        QDir(musicRoot).filePath(QStringLiteral("notes.txt")), "notes");
    const QString videoPath = writeFile(
        QDir(musicRoot).filePath(QStringLiteral("clip.MP4")), "video");
    QVERIFY(!audioPath.isEmpty() && !textPath.isEmpty() && !videoPath.isEmpty());

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

    ResourceFolderController manager;
    const QVariantMap folder = manager.classifyDropUrl(
        QUrl::fromLocalFile(musicRoot));
    QCOMPARE(folder.value(QStringLiteral("kind"))
                 .value<ResourceFolderController::DropPathKind>(),
             ResourceFolderController::DropPathKind::Directory);
    QCOMPARE(folder.value(QStringLiteral("path")).toString(),
             QFileInfo(musicRoot).canonicalFilePath());

    const QVariantMap alias = manager.classifyDropUrl(
        QUrl::fromLocalFile(aliasPath));
    QCOMPARE(alias.value(QStringLiteral("kind"))
                 .value<ResourceFolderController::DropPathKind>(),
             ResourceFolderController::DropPathKind::Directory);
    QCOMPARE(alias.value(QStringLiteral("path")),
             folder.value(QStringLiteral("path")));

    QVERIFY(manager.addMonitoredFolder(aliasPath));
    QVERIFY(!manager.addMonitoredFolder(musicRoot));
    QCOMPARE(manager.monitoredFolders(),
             QStringList{folder.value(QStringLiteral("path")).toString()});
    QVERIFY(manager.removeMonitoredFolder(musicRoot));

    QCOMPARE(manager.classifyDropUrl(QUrl::fromLocalFile(audioPath))
                 .value(QStringLiteral("kind"))
                 .value<ResourceFolderController::DropPathKind>(),
             ResourceFolderController::DropPathKind::AudioFile);
    QCOMPARE(manager.classifyDropUrl(QUrl::fromLocalFile(textPath))
                 .value(QStringLiteral("kind"))
                 .value<ResourceFolderController::DropPathKind>(),
             ResourceFolderController::DropPathKind::OtherFile);
    QCOMPARE(manager.classifyDropUrl(QUrl::fromLocalFile(videoPath))
                 .value(QStringLiteral("kind"))
                 .value<ResourceFolderController::DropPathKind>(),
             ResourceFolderController::DropPathKind::VideoFile);
    QCOMPARE(manager.classifyDropUrl(QUrl::fromLocalFile(
                 directory.filePath(QStringLiteral("missing.mp3"))))
                 .value(QStringLiteral("kind"))
                 .value<ResourceFolderController::DropPathKind>(),
             ResourceFolderController::DropPathKind::Invalid);
    QCOMPARE(manager.classifyDropUrl(QUrl(QStringLiteral("https://example.test/song.mp3")))
                 .value(QStringLiteral("kind"))
                 .value<ResourceFolderController::DropPathKind>(),
             ResourceFolderController::DropPathKind::Invalid);
}

void ResourceFolderControllerTest::classifiesEverySupportedAudioExtension_data()
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

void ResourceFolderControllerTest::classifiesEverySupportedAudioExtension()
{
    QFETCH(QString, extension);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeFile(
        dir.filePath(QStringLiteral("track.") + extension.toUpper()), "audio");
    ResourceFolderController manager;
    QCOMPARE(manager.classifyDropUrl(QUrl::fromLocalFile(path))
                 .value(QStringLiteral("kind"))
                 .value<ResourceFolderController::DropPathKind>(),
             ResourceFolderController::DropPathKind::AudioFile);
}

void ResourceFolderControllerTest::removesPersistedRootWithoutDeletingFiles()
{
    // Catches root removal accidentally deleting user audio or leaving the
    // persisted reference/watch contract stale.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString root = dir.filePath(QStringLiteral("music"));
    QVERIFY(QDir().mkpath(root));
    const QString audioPath = writeFile(QDir(root).filePath(QStringLiteral("song.mp3")), "audio");
    const QString settingsPath = dir.filePath(QStringLiteral("roots.json"));
    ResourceFolderController manager;
    manager.setStoragePath(settingsPath);
    QSignalSpy rootsChanged(&manager, &ResourceFolderController::resourceRootsChanged);

    QVERIFY(manager.addMonitoredFolder(root));
    QVERIFY(manager.removeMonitoredFolder(root));
    QVERIFY(QFileInfo::exists(audioPath));
    QCOMPARE(rootsChanged.count(), 2);

    ResourceFolderController restored;
    restored.setStoragePath(settingsPath);
    QVERIFY(restored.monitoredFolders().isEmpty());
}

void ResourceFolderControllerTest::persistsRootsAndImportsNewAudioRecursively()
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
        ResourceFolderController manager;
        manager.setStoragePath(settingsPath);
        manager.setImportController(&importer);
        manager.setLibraryModel(&library);
        QVERIFY(manager.addMonitoredFolder(musicRoot));
        manager.rescan();
        QVERIFY(imported.wait(3'000));
        QCOMPARE(library.count(), 1);
    }

    ResourceFolderController restored;
    restored.setStoragePath(settingsPath);
    QCOMPARE(restored.monitoredFolders(), QStringList({musicRoot}));
}

void ResourceFolderControllerTest::removedLibraryTrackStaysExcludedUntilManualImport()
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
    QSignalSpy imported(&importer, &ImportController::finished);
    {
        ResourceFolderController manager;
        manager.setStoragePath(settingsPath);
        manager.setImportController(&importer);
        manager.setLibraryModel(&library);
        QVERIFY(manager.addMonitoredFolder(musicRoot));

        manager.rescan();
        QVERIFY(imported.wait(3'000));
        QCOMPARE(library.count(), 1);
        const QString trackId = library.tracks().constFirst().trackId;

        QVERIFY(manager.removeTrackFromLibrary(trackId));
        QCOMPARE(library.count(), 0);
        QSignalSpy scanFinished(&manager,
                                &ResourceFolderController::scanFinished);
        manager.rescan();
        QVERIFY(scanFinished.wait(3'000));
        QTest::qWait(500);
        QCOMPARE(library.count(), 0);
    }

    {
        ResourceFolderController restored;
        restored.setStoragePath(settingsPath);
        restored.setImportController(&importer);
        restored.setLibraryModel(&library);
        QSignalSpy restoredScanFinished(&restored,
                                        &ResourceFolderController::scanFinished);
        // Restoring the resource roots schedules directory discovery itself;
        // the persisted removal must survive that startup scan as well.
        QVERIFY(restoredScanFinished.wait(3'000));
        QTest::qWait(500);
        QCOMPARE(library.count(), 0);

        // Keep a controller connected while importing so the manual action
        // clears and persists the tombstone before reconstruction.
        imported.clear();
        importer.importPaths({audioPath});
        QVERIFY(imported.wait(3'000));
        QCOMPARE(library.count(), 1);
    }

    // A manual import clears the tombstone durably, not merely in the live
    // controller.  Reconstructing the controller and rescanning must retain it.
    {
        ResourceFolderController afterManualImport;
        afterManualImport.setStoragePath(settingsPath);
        afterManualImport.setImportController(&importer);
        afterManualImport.setLibraryModel(&library);
        QSignalSpy scanFinished(&afterManualImport,
                                &ResourceFolderController::scanFinished);
        afterManualImport.rescan();
        QVERIFY(scanFinished.wait(3'000));
        QTest::qWait(500);
        QCOMPARE(library.count(), 1);

        // Keep the re-delete assertion independent and last so it cannot make
        // the manual-import persistence assertion pass accidentally.
        QVERIFY(afterManualImport.removeTrackFromLibrary(
            library.tracks().constFirst().trackId));
        QCOMPARE(library.count(), 0);
    }
}

void ResourceFolderControllerTest::failedExclusionPersistenceKeepsTrackInLibrary()
{
    // Catches an exclusion write failure being reported as success after the
    // in-memory model was already mutated.
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString audioPath = writeFile(
        directory.filePath(QStringLiteral("kept.mp3")), "audio");
    LibraryModel library;
    library.replaceAll({record(QStringLiteral("kept"), audioPath,
                               QStringLiteral("Kept"))});
    ResourceFolderController manager;
    manager.setLibraryModel(&library);
    manager.setStoragePath(directory.path()); // A directory cannot be QSaveFile output.

    QVERIFY(!manager.removeTrackFromLibrary(QStringLiteral("kept")));
    QCOMPARE(library.count(), 1);
    QCOMPARE(library.trackForId(QStringLiteral("kept"))
                 .value(QStringLiteral("path")).toString(), audioPath);
    QVERIFY(!manager.lastPersistenceError().isEmpty());
}


void ResourceFolderControllerTest::discoveryDoesNotRunTrackMaintenance()
{
    QTemporaryDir directory;
    LibraryModel library;
    auto track = record(QStringLiteral("missing"), directory.filePath("missing.mp3"), "Keep");
    track.favorite = true;
    track.rating = 4;
    library.replaceAll({track});
    const QVariantMap before = library.trackForId(track.trackId);
    ResourceFolderController folders;
    folders.setLibraryModel(&library);
    QSignalSpy changed(&library, &QAbstractItemModel::dataChanged);
    QSignalSpy finished(&folders, &ResourceFolderController::scanFinished);
    folders.rescan();
    QVERIFY(finished.wait(3000));
    QCOMPARE(library.trackForId(track.trackId), before);
    QCOMPARE(changed.count(), 0);
}

QTEST_MAIN(ResourceFolderControllerTest)
#include "resource_folder_controller_test.moc"
