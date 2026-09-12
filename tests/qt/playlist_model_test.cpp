#include "playlist_model.hpp"
#include "library_model.hpp"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

class PlaylistModelTest final : public QObject {
    Q_OBJECT

private slots:
    void managesPlaylistsAndMembership();
    void batchesAddMoveAndRemoveTracks();
    void persistsAndRestoresStableMembership();
    void preservesPlaylistAndTrackOrderAcrossRestart();
    void importsAndExportsSupportedPlaylistFormats();
    void rejectsInvalidNamesAndMalformedStorage();
};

void PlaylistModelTest::managesPlaylistsAndMembership()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    PlaylistModel model(dir.filePath(QStringLiteral("playlists.json")));
    QSignalSpy countChanged(&model, &PlaylistModel::countChanged);
    QSignalSpy membershipChanged(&model, &PlaylistModel::membershipChanged);

    const QString playlistId = model.createPlaylist(QStringLiteral("  Road Trip  "));
    QVERIFY(!playlistId.isEmpty());
    QCOMPARE(model.count(), 1);
    QCOMPARE(model.nameForId(playlistId), QStringLiteral("Road Trip"));
    QCOMPARE(model.data(model.index(0), PlaylistModel::TrackCountRole).toInt(), 0);
    QCOMPARE(countChanged.count(), 1);

    QVERIFY(model.addTrack(playlistId, QStringLiteral("track-b")));
    QVERIFY(model.addTrack(playlistId, QStringLiteral("track-a")));
    QVERIFY(!model.addTrack(playlistId, QStringLiteral("track-a")));
    QVERIFY(model.containsTrack(playlistId, QStringLiteral("track-a")));
    QCOMPARE(model.data(model.index(0), PlaylistModel::TrackCountRole).toInt(), 2);
    QCOMPARE(membershipChanged.count(), 2);

    const QString secondPlaylist =
        model.createPlaylist(QStringLiteral("Evening Drive"));
    QVERIFY(model.addTrack(secondPlaylist, QStringLiteral("track-a")));
    QVERIFY(model.removeTrackFromAll(QStringLiteral("track-a")));
    QVERIFY(!model.containsTrack(playlistId, QStringLiteral("track-a")));
    QVERIFY(!model.containsTrack(secondPlaylist, QStringLiteral("track-a")));
    QVERIFY(!model.removeTrackFromAll(QStringLiteral("missing")));

    QVERIFY(model.renamePlaylist(playlistId, QStringLiteral("Morning Drive")));
    QCOMPARE(model.nameForId(playlistId), QStringLiteral("Morning Drive"));
    QVERIFY(model.removeTrack(playlistId, QStringLiteral("track-b")));
    QVERIFY(!model.containsTrack(playlistId, QStringLiteral("track-b")));
    QVERIFY(model.removePlaylist(playlistId));
    QVERIFY(model.removePlaylist(secondPlaylist));
    QCOMPARE(model.count(), 0);
}

void PlaylistModelTest::persistsAndRestoresStableMembership()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("playlists.json"));
    QString playlistId;
    {
        PlaylistModel writer(path);
        playlistId = writer.createPlaylist(QStringLiteral("Set"));
        QVERIFY(writer.addTrack(playlistId, QStringLiteral("track-2")));
        QVERIFY(writer.addTrack(playlistId, QStringLiteral("track-1")));
        QVERIFY(writer.flush());
    }

    PlaylistModel reader(path);
    QVERIFY(reader.load());
    QCOMPARE(reader.count(), 1);
    QCOMPARE(reader.idAt(0), playlistId);
    QCOMPARE(reader.nameForId(playlistId), QStringLiteral("Set"));
    QVERIFY(reader.containsTrack(playlistId, QStringLiteral("track-1")));
    QVERIFY(reader.containsTrack(playlistId, QStringLiteral("track-2")));
}

void PlaylistModelTest::preservesPlaylistAndTrackOrderAcrossRestart()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("playlists.json"));

    QString firstId;
    QString secondId;
    {
        PlaylistModel writer(path);
        firstId = writer.createPlaylist(QStringLiteral("First"));
        secondId = writer.createPlaylist(QStringLiteral("Second"));
        QVERIFY(writer.addTracks(firstId, {"track-c", "track-a", "track-b"}) == 3);
        QCOMPARE(writer.trackIdsForPlaylist(firstId),
                 QStringList({"track-c", "track-a", "track-b"}));
        QVERIFY(writer.moveTrack(firstId, 2, 0));
        QCOMPARE(writer.trackIdsForPlaylist(firstId),
                 QStringList({"track-b", "track-c", "track-a"}));
        QCOMPARE(writer.reorderTracks(firstId,
                                      {"track-b", "track-a"},
                                      "track-c"), 2);
        QCOMPARE(writer.trackIdsForPlaylist(firstId),
                 QStringList({"track-b", "track-a", "track-c"}));
        QVERIFY(writer.movePlaylist(1, 0));
        QCOMPARE(writer.idAt(0), secondId);
        QVERIFY(writer.flush());
    }

    PlaylistModel reader(path);
    QVERIFY(reader.load());
    QCOMPARE(reader.idAt(0), secondId);
    QCOMPARE(reader.idAt(1), firstId);
    QCOMPARE(reader.trackIdsForPlaylist(firstId),
             QStringList({"track-b", "track-a", "track-c"}));
}

void PlaylistModelTest::importsAndExportsSupportedPlaylistFormats()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString firstPath = dir.filePath(QStringLiteral("第一首.mp3"));
    const QString secondPath = dir.filePath(QStringLiteral("second.flac"));
    for (const QString& path : {firstPath, secondPath}) {
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("audio");
    }
    const QString m3uPath = dir.filePath(QStringLiteral("input.m3u8"));
    QFile m3u(m3uPath);
    QVERIFY(m3u.open(QIODevice::WriteOnly));
    m3u.write("#EXTM3U\n");
    m3u.write(firstPath.toUtf8() + "\n" + secondPath.toUtf8() + "\n");
    m3u.close();

    PlaylistModel model(dir.filePath(QStringLiteral("playlists.json")));
    const QString importedId = model.importPlaylist(m3uPath);
    QVERIFY(!importedId.isEmpty());
    QCOMPARE(model.trackIdsForPlaylist(importedId),
             QStringList({trackIdForPath(firstPath), trackIdForPath(secondPath)}));

    QVariantMap paths;
    paths.insert(trackIdForPath(firstPath), firstPath);
    paths.insert(trackIdForPath(secondPath), secondPath);
    const QString exportPath = dir.filePath(QStringLiteral("export.m3u8"));
    QVERIFY(model.exportPlaylist(importedId, exportPath, paths, false));
    QFile exported(exportPath); QVERIFY(exported.open(QIODevice::ReadOnly));
    const QString text = QString::fromUtf8(exported.readAll());
    QVERIFY(text.startsWith(QStringLiteral("#EXTM3U\n")));
    QVERIFY(text.contains(QStringLiteral("第一首.mp3")));

    const QString jsonPath = dir.filePath(QStringLiteral("export.json"));
    QVERIFY(model.exportPlaylist(importedId, jsonPath, paths, false));
    PlaylistModel restored(dir.filePath(QStringLiteral("restored.json")));
    const QString restoredId = restored.importPlaylist(jsonPath);
    QVERIFY(!restoredId.isEmpty());
    QCOMPARE(restored.trackIdsForPlaylist(restoredId), model.trackIdsForPlaylist(importedId));

    const QString m3uUrl = QUrl::fromLocalFile(m3uPath).toString();
    QCOMPARE(model.pathsFromPlaylist(m3uUrl),
             QStringList({firstPath, secondPath}));
    QVERIFY(model.exportPlaylist(
        importedId, QUrl::fromLocalFile(dir.filePath(QStringLiteral("url.m3u8"))).toString(),
        paths, false));
}

void PlaylistModelTest::batchesAddMoveAndRemoveTracks()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    PlaylistModel model(dir.filePath(QStringLiteral("playlists.json")));
    const QString source = model.createPlaylist(QStringLiteral("Source"));
    const QString target = model.createPlaylist(QStringLiteral("Target"));

    QCOMPARE(model.addTracks(source, {"one", "two", "two", ""}), 2);
    QCOMPARE(model.addTracks(target, {"two"}), 1);
    QCOMPARE(model.moveTracks(source, target, {"one", "two"}), 2);
    QVERIFY(!model.containsTrack(source, QStringLiteral("one")));
    QVERIFY(!model.containsTrack(source, QStringLiteral("two")));
    QVERIFY(model.containsTrack(target, QStringLiteral("one")));
    QVERIFY(model.containsTrack(target, QStringLiteral("two")));
    QCOMPARE(model.removeTracks(target, {"one", "missing"}), 1);
    QVERIFY(!model.containsTrack(target, QStringLiteral("one")));
}

void PlaylistModelTest::rejectsInvalidNamesAndMalformedStorage()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("playlists.json"));
    PlaylistModel model(path);
    QVERIFY(model.createPlaylist(QStringLiteral(" ")).isEmpty());
    const QString id = model.createPlaylist(QStringLiteral("Focus"));
    QVERIFY(!id.isEmpty());
    QVERIFY(model.createPlaylist(QStringLiteral("focus")).isEmpty());
    QVERIFY(!model.renamePlaylist(id, QStringLiteral(" ")));
    QVERIFY(!model.addTrack(QStringLiteral("missing"), QStringLiteral("track")));
    QVERIFY(!model.addTrack(id, QString()));

    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(file.write("{broken"), 7);
    file.close();

    PlaylistModel malformed(path);
    QVERIFY(!malformed.load());
    QCOMPARE(malformed.count(), 0);
}

QTEST_GUILESS_MAIN(PlaylistModelTest)
#include "playlist_model_test.moc"
