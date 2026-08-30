#include "library_file_operations.hpp"
#include "library_model.hpp"

#include <QElapsedTimer>
#include <QFile>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <atomic>

class LibraryFileOperationsTest final : public QObject {
    Q_OBJECT
private slots:
    void renamesCopiesMovesAndRelocatesWithoutSilentOverwrite();
    void trashTracksReportsPartialFailureWithoutDroppingLibraryRows();
    void trackDetailsIsPureRead();
    void hydrationRequestReturnsImmediatelyAndDeduplicatesSlowProbe();
    void failedAndZeroChannelHydrationsAreAttemptedOnce();
};

void LibraryFileOperationsTest::trackDetailsIsPureRead()
{
    TrackRecord legacy;
    legacy.trackId = QStringLiteral("legacy-audio");
    legacy.path = QStringLiteral("C:/virtual/legacy.wav");
    legacy.title = QStringLiteral("Legacy audio");
    legacy.available = true;
    LibraryModel library;
    library.replaceAll({legacy});
    std::atomic_int probeCalls = 0;
    LibraryFileOperations operations([&probeCalls](const QString&) {
        ++probeCalls;
        return ProbeResult{AG_OK, {}, {}};
    });
    operations.setLibraryModel(&library);
    QSignalSpy dataChanged(&library, &LibraryModel::dataChanged);
    QSignalSpy flushRequested(&library, &LibraryModel::flushRequested);

    const QVariantMap details = operations.trackDetails(legacy.trackId);
    QCOMPARE(details.value(QStringLiteral("channels")).toInt(), 0);
    QCOMPARE(probeCalls.load(), 0);
    QCOMPARE(dataChanged.count(), 0);
    QCOMPARE(flushRequested.count(), 0);
}

void LibraryFileOperationsTest::hydrationRequestReturnsImmediatelyAndDeduplicatesSlowProbe()
{
    TrackRecord legacy;
    legacy.trackId = QStringLiteral("slow-audio");
    legacy.path = QStringLiteral("C:/virtual/slow.wav");
    legacy.available = true;
    LibraryModel library;
    library.replaceAll({legacy});
    QSemaphore entered;
    QSemaphore release;
    std::atomic_int probeCalls = 0;
    const ProbeFunction probe =
        [&entered, &release, &probeCalls](const QString& path) {
            ++probeCalls;
            entered.release();
            release.acquire();
            TrackRecord probed;
            probed.path = path;
            probed.format = QStringLiteral("wav");
            probed.sampleRate = 48000;
            probed.bitDepth = 24;
            probed.channels = 2;
            probed.bitRate = 2304000;
            probed.durationMs = 12000;
            probed.fileSize = 3456;
            return ProbeResult{AG_OK, probed, {}};
        };
    LibraryFileOperations operations(probe);
    LibraryFileOperations competingOperations(probe);
    operations.setLibraryModel(&library);
    competingOperations.setLibraryModel(&library);
    QSignalSpy detailsChanged(&operations,
                              &LibraryFileOperations::trackDetailsChanged);
    QSignalSpy flushRequested(&library, &LibraryModel::flushRequested);

    QElapsedTimer elapsed;
    elapsed.start();
    QVERIFY(operations.requestTrackDetailsHydration(legacy.trackId));
    QVERIFY2(elapsed.elapsed() < 250,
             "requestTrackDetailsHydration must not wait for the probe");
    QVERIFY(entered.tryAcquire(1, 1000));
    QVERIFY(!operations.requestTrackDetailsHydration(legacy.trackId));
    QVERIFY(!competingOperations.requestTrackDetailsHydration(legacy.trackId));
    release.release();
    QTRY_COMPARE_WITH_TIMEOUT(detailsChanged.count(), 1, 3000);

    const QVariantMap details = operations.trackDetails(legacy.trackId);
    QCOMPARE(details.value(QStringLiteral("sampleRate")).toInt(), 48000);
    QCOMPARE(details.value(QStringLiteral("bitDepth")).toInt(), 24);
    QCOMPARE(details.value(QStringLiteral("channels")).toInt(), 2);
    QCOMPARE(details.value(QStringLiteral("bitRate")).toLongLong(), qint64{2304000});
    QCOMPARE(details.value(QStringLiteral("durationMs")).toLongLong(), qint64{12000});
    QCOMPARE(details.value(QStringLiteral("fileSize")).toLongLong(), qint64{3456});
    QVERIFY(library.tracks().front().metadataProbeAttempted);
    QVERIFY(!operations.requestTrackDetailsHydration(legacy.trackId));
    QCOMPARE(probeCalls.load(), 1);
    QCOMPARE(flushRequested.count(), 0);
}

void LibraryFileOperationsTest::failedAndZeroChannelHydrationsAreAttemptedOnce()
{
    TrackRecord failed;
    failed.trackId = QStringLiteral("failed-audio");
    failed.path = QStringLiteral("C:/virtual/failed.wav");
    failed.available = true;
    TrackRecord zeroChannels;
    zeroChannels.trackId = QStringLiteral("zero-channel-audio");
    zeroChannels.path = QStringLiteral("C:/virtual/zero.wav");
    zeroChannels.available = true;
    LibraryModel library;
    library.replaceAll({failed, zeroChannels});
    std::atomic_int probeCalls = 0;
    LibraryFileOperations operations([&probeCalls](const QString& path) {
        ++probeCalls;
        if (path.contains(QStringLiteral("failed"))) {
            return ProbeResult{AG_DECODE_ERROR, {}, QStringLiteral("decode failed")};
        }
        TrackRecord probed;
        probed.path = path;
        probed.format = QStringLiteral("wav");
        probed.sampleRate = 48000;
        probed.bitDepth = 24;
        probed.channels = 0;
        probed.bitRate = 1152000;
        probed.durationMs = 1000;
        probed.fileSize = 512;
        return ProbeResult{AG_OK, probed, {}};
    });
    operations.setLibraryModel(&library);
    QSignalSpy detailsChanged(&operations,
                              &LibraryFileOperations::trackDetailsChanged);
    QSignalSpy flushRequested(&library, &LibraryModel::flushRequested);

    QVERIFY(operations.requestTrackDetailsHydration(failed.trackId));
    QVERIFY(operations.requestTrackDetailsHydration(zeroChannels.trackId));
    QTRY_COMPARE_WITH_TIMEOUT(detailsChanged.count(), 2, 3000);
    QVERIFY(library.recordForId(failed.trackId)->metadataProbeAttempted);
    QVERIFY(library.recordForId(zeroChannels.trackId)->metadataProbeAttempted);
    QCOMPARE(library.recordForId(zeroChannels.trackId)->channels, 0);
    QVERIFY(!operations.requestTrackDetailsHydration(failed.trackId));
    QVERIFY(!operations.requestTrackDetailsHydration(zeroChannels.trackId));
    QCOMPARE(probeCalls.load(), 2);
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
