#include "library_file_operations.hpp"
#include "library_model.hpp"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>


class LibraryFileOperationsTest final : public QObject {
    Q_OBJECT
private slots:
    void renamesCopiesMovesAndRelocatesWithoutSilentOverwrite();
    void renameRollsBackWhenLibraryRejectsTargetPath();
    void overwriteMoveRollsBackSourceAndExistingTargetWhenLibraryRejectsPath();
    void overwriteCopyToSameFolderPreservesSource();
    void trashTracksReportsPartialFailureWithoutDroppingLibraryRows();
    void trackDetailsIsPureRead();
};

namespace {

void writeFile(const QString& path, const QByteArray& contents)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(contents), qint64(contents.size()));
}

QByteArray readFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

TrackRecord availableTrack(const QString& trackId, const QString& path)
{
    TrackRecord record;
    record.trackId = trackId;
    record.path = path;
    record.available = true;
    return record;
}

} // namespace

void LibraryFileOperationsTest::trackDetailsIsPureRead()
{
    TrackRecord legacy;
    legacy.trackId = QStringLiteral("legacy-audio");
    legacy.path = QStringLiteral("C:/virtual/legacy.wav");
    legacy.title = QStringLiteral("Legacy audio");
    legacy.available = true;
    LibraryModel library;
    library.replaceAll({legacy});
    LibraryFileOperations operations;
    operations.setLibraryModel(&library);
    QSignalSpy dataChanged(&library, &LibraryModel::dataChanged);
    QSignalSpy flushRequested(&library, &LibraryModel::flushRequested);

    const QVariantMap details = operations.trackDetails(legacy.trackId);
    QCOMPARE(details.value(QStringLiteral("channels")).toInt(), 0);
    QCOMPARE(dataChanged.count(), 0);
    QCOMPARE(flushRequested.count(), 0);
}

void LibraryFileOperationsTest::renamesCopiesMovesAndRelocatesWithoutSilentOverwrite()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString original = dir.filePath(QStringLiteral("source.mp3"));
    QFile source(original);
    QVERIFY(source.open(QIODevice::WriteOnly));
    QCOMPARE(source.write("audio"), qint64(5));
    source.close();

    TrackRecord record;
    record.trackId = QStringLiteral("track");
    record.path = original;
    record.title = QStringLiteral("Source");
    record.available = true;
    LibraryModel library;
    library.replaceAll({record});
    LibraryFileOperations operations;
    operations.setLibraryModel(&library);

    QCOMPARE(operations.fileUrl(QStringLiteral("track")), QUrl::fromLocalFile(original));
    QVERIFY(operations.fileUrl(QStringLiteral("missing")).isEmpty());

    QVERIFY(operations.renameTrack(QStringLiteral("track"), QStringLiteral("renamed")));
    QString path = library.trackForId(QStringLiteral("track"))
                       .value(QStringLiteral("path")).toString();
    QVERIFY(path.endsWith(QStringLiteral("renamed.mp3")));
    QVERIFY(QFileInfo::exists(path));

    const QString copyDir = dir.filePath(QStringLiteral("copy"));
    QVERIFY(QDir().mkpath(copyDir));
    QCOMPARE(operations.copyTracks({QStringLiteral("track")}, copyDir,
                                   LibraryFileOperations::AutoRename), 1);
    QVERIFY(QFileInfo::exists(QDir(copyDir).filePath(QStringLiteral("renamed.mp3"))));
    QCOMPARE(operations.copyTracks({QStringLiteral("track")}, copyDir,
                                   LibraryFileOperations::Skip), 0);

    const QString moveDir = dir.filePath(QStringLiteral("move"));
    QVERIFY(QDir().mkpath(moveDir));
    QCOMPARE(operations.moveTracksToUrl({QStringLiteral("track")},
                                        QUrl::fromLocalFile(moveDir),
                                        LibraryFileOperations::Skip), 1);
    path = library.trackForId(QStringLiteral("track"))
               .value(QStringLiteral("path")).toString();
    QVERIFY(path.startsWith(QDir::fromNativeSeparators(moveDir)));

    const QString relocated = dir.filePath(QStringLiteral("relocated.mp3"));
    QVERIFY(QFile::copy(path, relocated));
    QVERIFY(operations.relocateTrackToUrl(QStringLiteral("track"),
                                          QUrl::fromLocalFile(relocated)));
    QCOMPARE(library.trackForId(QStringLiteral("track"))
                 .value(QStringLiteral("path")).toString(),
             QDir::fromNativeSeparators(QFileInfo(relocated).absoluteFilePath()));
}

void LibraryFileOperationsTest::renameRollsBackWhenLibraryRejectsTargetPath()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString source = dir.filePath(QStringLiteral("source.mp3"));
    const QString target = dir.filePath(QStringLiteral("renamed.mp3"));
    writeFile(source, "source-audio");

    LibraryModel library;
    library.replaceAll({availableTrack(QStringLiteral("source"), source),
                        availableTrack(QStringLiteral("reserved"), target)});
    LibraryFileOperations operations;
    operations.setLibraryModel(&library);

    QVERIFY(!operations.renameTrack(QStringLiteral("source"),
                                    QStringLiteral("renamed")));
    QCOMPARE(readFile(source), QByteArray("source-audio"));
    QVERIFY(!QFileInfo::exists(target));
    QCOMPARE(library.trackForId(QStringLiteral("source"))
                 .value(QStringLiteral("path")).toString(),
             QDir::fromNativeSeparators(QFileInfo(source).absoluteFilePath()));
    QVERIFY(QDir(dir.path()).entryList(
        {QStringLiteral(".agplayer-*")}, QDir::Files).isEmpty());
}

void LibraryFileOperationsTest::overwriteMoveRollsBackSourceAndExistingTargetWhenLibraryRejectsPath()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString source = dir.filePath(QStringLiteral("song.mp3"));
    const QString destination = dir.filePath(QStringLiteral("destination"));
    QVERIFY(QDir().mkpath(destination));
    const QString target = QDir(destination).filePath(QStringLiteral("song.mp3"));
    writeFile(source, "source-audio");
    writeFile(target, "existing-target-audio");

    LibraryModel library;
    library.replaceAll({availableTrack(QStringLiteral("source"), source),
                        availableTrack(QStringLiteral("reserved"), target)});
    LibraryFileOperations operations;
    operations.setLibraryModel(&library);

    QCOMPARE(operations.moveTracks({QStringLiteral("source")}, destination,
                                   LibraryFileOperations::Overwrite), 0);
    QCOMPARE(readFile(source), QByteArray("source-audio"));
    QCOMPARE(readFile(target), QByteArray("existing-target-audio"));
    QCOMPARE(library.trackForId(QStringLiteral("source"))
                 .value(QStringLiteral("path")).toString(),
             QDir::fromNativeSeparators(QFileInfo(source).absoluteFilePath()));
    QVERIFY(QDir(dir.path()).entryList(
        {QStringLiteral(".agplayer-*")}, QDir::Files).isEmpty());
    QVERIFY(QDir(destination).entryList(
        {QStringLiteral(".agplayer-*")}, QDir::Files).isEmpty());
}

void LibraryFileOperationsTest::overwriteCopyToSameFolderPreservesSource()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString source = dir.filePath(QStringLiteral("song.mp3"));
    writeFile(source, "source-audio");

    LibraryModel library;
    library.replaceAll({availableTrack(QStringLiteral("source"), source)});
    LibraryFileOperations operations;
    operations.setLibraryModel(&library);

    QCOMPARE(operations.copyTracks({QStringLiteral("source")}, dir.path(),
                                   LibraryFileOperations::Overwrite), 0);
    QCOMPARE(readFile(source), QByteArray("source-audio"));
    QCOMPARE(library.trackForId(QStringLiteral("source"))
                 .value(QStringLiteral("path")).toString(),
             QDir::fromNativeSeparators(QFileInfo(source).absoluteFilePath()));
}

void LibraryFileOperationsTest::trashTracksReportsPartialFailureWithoutDroppingLibraryRows()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString removable = dir.filePath(QStringLiteral("trash-me.mp3"));
    QFile file(removable);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("audio"), qint64{5});
    file.close();

    TrackRecord good;
    good.trackId = QStringLiteral("good");
    good.path = removable;
    good.available = true;
    TrackRecord missing;
    missing.trackId = QStringLiteral("missing");
    missing.path = dir.filePath(QStringLiteral("missing.mp3"));
    missing.available = false;
    LibraryModel library;
    library.replaceAll({good, missing});
    LibraryFileOperations operations;
    operations.setLibraryModel(&library);

    const QVariantMap result = operations.trashTracks(
        {QStringLiteral("good"), QStringLiteral("missing")});
    QCOMPARE(result.value(QStringLiteral("successCount")).toInt(), 1);
    QCOMPARE(result.value(QStringLiteral("failureCount")).toInt(), 1);
    QCOMPARE(result.value(QStringLiteral("failures")).toList().size(), 1);
    QVERIFY(library.trackForId(QStringLiteral("good")).isEmpty());
    QVERIFY(!library.trackForId(QStringLiteral("missing")).isEmpty());
}

QTEST_GUILESS_MAIN(LibraryFileOperationsTest)
#include "library_file_operations_test.moc"
