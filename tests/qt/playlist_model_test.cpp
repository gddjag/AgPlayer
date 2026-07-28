#include "playlist_model.hpp"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class PlaylistModelTest final : public QObject {
    Q_OBJECT

private slots:
    void managesPlaylistsAndMembership();
    void persistsAndRestoresStableMembership();
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

    QVERIFY(model.renamePlaylist(playlistId, QStringLiteral("Morning Drive")));
    QCOMPARE(model.nameForId(playlistId), QStringLiteral("Morning Drive"));
    QVERIFY(model.removeTrack(playlistId, QStringLiteral("track-b")));
    QVERIFY(!model.containsTrack(playlistId, QStringLiteral("track-b")));
    QVERIFY(model.removePlaylist(playlistId));
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
