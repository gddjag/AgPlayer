#include "library_model.hpp"
#include "library_store.hpp"

#include <QFile>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class LibraryStoreTest final : public QObject {
    Q_OBJECT

private slots:
    void persistsEveryRoleAndMarksMissingFilesUnavailable();
    void mergesDeferredSaveRequests();
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
    track.format = QStringLiteral("flac");
    track.sampleRate = 192000;
    track.bitDepth = 24;
    track.bitRate = 9216000;
    track.durationMs = 8642;
    track.fileSize = 4321;
    track.coverUrl = QUrl::fromLocalFile(QStringLiteral("C:/cache/cover.png"));
    track.favorite = true;
    track.available = true;
    track.importError = QStringLiteral("old warning");
    return track;
}
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
    QCOMPARE(track.format, source.format);
    QCOMPARE(track.sampleRate, source.sampleRate);
    QCOMPARE(track.bitDepth, source.bitDepth);
    QCOMPARE(track.bitRate, source.bitRate);
    QCOMPARE(track.durationMs, source.durationMs);
    QCOMPARE(track.fileSize, source.fileSize);
    QCOMPARE(track.coverUrl, source.coverUrl);
    QCOMPARE(track.favorite, source.favorite);
    QVERIFY(!track.available);
    QCOMPARE(track.importError, source.importError);
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

QTEST_GUILESS_MAIN(LibraryStoreTest)
#include "library_store_test.moc"
