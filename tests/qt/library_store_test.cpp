#include "library_model.hpp"
#include "library_store.hpp"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class LibraryStoreTest final : public QObject {
    Q_OBJECT

private slots:
    void persistsEveryRoleAndMarksMissingFilesUnavailable();
    void mergesDeferredSaveRequests();
    void preservesIntegersBeyondJsonDoublePrecision();
    void destructorFlushesPendingSnapshot();
    void explicitFlushCommitsPendingSnapshot();
    void migratesMissingAddedTimestampFromTheAudioFile();
};

namespace {
TrackRecord makeTrack(const QString& path, const QString& title)
{
    TrackRecord track;
    track.trackId = QStringLiteral("stable-id");
    track.path = path;
    track.title = title;
    track.artist = QStringLiteral("Artist");
    track.album = QStringLiteral("Album");
    track.albumArtist = QStringLiteral("Album Artist");
    track.genre = QStringLiteral("City Pop");
    track.year = QStringLiteral("2024");
    track.date = QStringLiteral("2024-05-20");
    track.composer = QStringLiteral("Composer");
    track.format = QStringLiteral("flac");
    track.sampleRate = 192000;
    track.bitDepth = 24;
    track.channels = 2;
    track.hasAudio = true;
    track.hasVideo = true;
    track.metadataProbeAttempted = true;
    track.bitRate = 9216000;
    track.durationMs = 8642;
    track.fileSize = 4321;
    track.coverUrl = QUrl::fromLocalFile(QStringLiteral("C:/cache/cover.png"));
    track.favorite = true;
    track.rating = 3;
    track.bpm = 120.0;
    track.playCount = 7;
    track.lastPlayedAtMs = Q_INT64_C(1722222222333);
    track.tags = {QStringLiteral("Night"), QStringLiteral("Workout")};
    track.addedAtMs = Q_INT64_C(1721111111222);
    track.fileStatus = QStringLiteral("missing");
    track.contentHash = QStringLiteral("abc123");
    track.audioFingerprint = QByteArray::fromHex("01020304");
    track.replayGainScanned = true;
    track.replayGainTrackDb = -4.25;
    track.replayGainAlbumDb = -3.5;
    track.replayPeak = 0.92;
    track.available = true;
    track.importError = QStringLiteral("old warning");
    return track;
}
}

void LibraryStoreTest::migratesMissingAddedTimestampFromTheAudioFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString audioPath = dir.filePath(QStringLiteral("legacy.wav"));
    QFile audio(audioPath);
    QVERIFY(audio.open(QIODevice::WriteOnly));
    QCOMPARE(audio.write("legacy"), 6);
    audio.close();

    const QString libraryPath = dir.filePath(QStringLiteral("library.json"));
    QFile json(libraryPath);
    QVERIFY(json.open(QIODevice::WriteOnly));
    const QJsonArray legacyLibrary{
        QJsonObject{{QStringLiteral("path"), audioPath},
                    {QStringLiteral("title"), QStringLiteral("Legacy")}}};
    QCOMPARE(json.write(QJsonDocument(legacyLibrary).toJson(
                 QJsonDocument::Compact)) > 0, true);
    json.close();

    LibraryStore store(libraryPath);
    const QList<TrackRecord> loaded = store.load();

    QCOMPARE(loaded.size(), 1);
    QCOMPARE(loaded.front().channels, 0);
    QVERIFY(!loaded.front().hasAudio);
    QVERIFY(!loaded.front().hasVideo);
    QVERIFY(!loaded.front().metadataProbeAttempted);
    QVERIFY(loaded.front().addedAtMs > 0);
    const QFileInfo info(audioPath);
    const qint64 fileTimestamp = info.birthTime().isValid()
        ? info.birthTime().toMSecsSinceEpoch()
        : info.lastModified().toMSecsSinceEpoch();
    QCOMPARE(loaded.front().addedAtMs, fileTimestamp);
}

void LibraryStoreTest::persistsEveryRoleAndMarksMissingFilesUnavailable()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString libraryPath = dir.filePath(QStringLiteral("library.json"));
    LibraryStore store(libraryPath);
    const TrackRecord source = makeTrack(dir.filePath(QStringLiteral("missing.flac")),
                                         QStringLiteral("Original"));

    QVERIFY(store.save({source}));
    QFile jsonFile(libraryPath);
    QVERIFY(jsonFile.open(QIODevice::ReadOnly));
    const QByteArray bytes = jsonFile.readAll();
    QVERIFY(QJsonDocument::fromJson(bytes).isArray());
    QVERIFY(!bytes.contains("\n"));

    const QList<TrackRecord> loaded = store.load();
    QCOMPARE(loaded.size(), 1);
    const TrackRecord& track = loaded.front();
    QCOMPARE(track.trackId, source.trackId);
    QCOMPARE(track.path, source.path);
    QCOMPARE(track.title, source.title);
    QCOMPARE(track.artist, source.artist);
    QCOMPARE(track.album, source.album);
    QCOMPARE(track.albumArtist, source.albumArtist);
    QCOMPARE(track.genre, source.genre);
    QCOMPARE(track.year, source.year);
    QCOMPARE(track.date, source.date);
    QCOMPARE(track.composer, source.composer);
    QCOMPARE(track.format, source.format);
    QCOMPARE(track.sampleRate, source.sampleRate);
    QCOMPARE(track.bitDepth, source.bitDepth);
    QCOMPARE(track.channels, source.channels);
    QCOMPARE(track.hasAudio, source.hasAudio);
    QCOMPARE(track.hasVideo, source.hasVideo);
    QCOMPARE(track.metadataProbeAttempted, source.metadataProbeAttempted);
    QCOMPARE(track.bitRate, source.bitRate);
    QCOMPARE(track.durationMs, source.durationMs);
    QCOMPARE(track.fileSize, source.fileSize);
    QCOMPARE(track.coverUrl, source.coverUrl);
    QCOMPARE(track.favorite, source.favorite);
    QCOMPARE(track.rating, source.rating);
    QCOMPARE(track.bpm, source.bpm);
    QCOMPARE(track.playCount, source.playCount);
    QCOMPARE(track.lastPlayedAtMs, source.lastPlayedAtMs);
    QCOMPARE(track.tags, source.tags);
    QCOMPARE(track.addedAtMs, source.addedAtMs);
    QCOMPARE(track.fileStatus, source.fileStatus);
    QCOMPARE(track.contentHash, source.contentHash);
    QCOMPARE(track.audioFingerprint, source.audioFingerprint);
    QCOMPARE(track.replayGainScanned, source.replayGainScanned);
    QCOMPARE(track.replayGainTrackDb, source.replayGainTrackDb);
    QCOMPARE(track.replayGainAlbumDb, source.replayGainAlbumDb);
    QCOMPARE(track.replayPeak, source.replayPeak);
    QVERIFY(!track.available);
    QCOMPARE(track.importError, source.importError);
    LibraryModel model;
    model.replaceAll(loaded);
    QVERIFY(model.containsPath(source.path));
}

void LibraryStoreTest::mergesDeferredSaveRequests()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    LibraryStore store(dir.filePath(QStringLiteral("library.json")));
    QSignalSpy saved(&store, &LibraryStore::saveFinished);

    store.requestSave({makeTrack(QStringLiteral("first.flac"), QStringLiteral("First"))});
    store.requestSave({makeTrack(QStringLiteral("last.flac"), QStringLiteral("Last"))});

    QTRY_COMPARE_WITH_TIMEOUT(saved.count(), 1, 1000);
    QVERIFY(saved.front().front().toBool());
    const QList<TrackRecord> loaded = store.load();
    QCOMPARE(loaded.size(), 1);
    QCOMPARE(loaded.front().title, QStringLiteral("Last"));
}

void LibraryStoreTest::preservesIntegersBeyondJsonDoublePrecision()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    LibraryStore store(dir.filePath(QStringLiteral("library.json")));
    TrackRecord source = makeTrack(QStringLiteral("large.flac"), QStringLiteral("Large"));
    source.bitRate = Q_INT64_C(9007199254740993);
    source.durationMs = Q_INT64_C(9007199254740995);
    source.fileSize = Q_INT64_C(9007199254740997);

    QVERIFY(store.save({source}));
    const QList<TrackRecord> loaded = store.load();
    QCOMPARE(loaded.size(), 1);
    QCOMPARE(loaded.front().bitRate, source.bitRate);
    QCOMPARE(loaded.front().durationMs, source.durationMs);
    QCOMPARE(loaded.front().fileSize, source.fileSize);
}

void LibraryStoreTest::destructorFlushesPendingSnapshot()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("library.json"));
    {
        LibraryStore store(path);
        store.requestSave({makeTrack(QStringLiteral("shutdown.flac"),
                                     QStringLiteral("Shutdown"))});
    }

    LibraryStore reader(path);
    const QList<TrackRecord> loaded = reader.load();
    QCOMPARE(loaded.size(), 1);
    QCOMPARE(loaded.front().title, QStringLiteral("Shutdown"));

    {
        LibraryStore store(path);
        store.requestSave({});
    }
    QCOMPARE(reader.load().size(), 0);
}

void LibraryStoreTest::explicitFlushCommitsPendingSnapshot()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    LibraryStore store(dir.filePath(QStringLiteral("library.json")));
    store.requestSave({makeTrack(QStringLiteral("flush.flac"), QStringLiteral("Flush"))});

    QVERIFY(store.flush());
    const QList<TrackRecord> loaded = store.load();
    QCOMPARE(loaded.size(), 1);
    QCOMPARE(loaded.front().title, QStringLiteral("Flush"));
}

QTEST_GUILESS_MAIN(LibraryStoreTest)
#include "library_store_test.moc"
