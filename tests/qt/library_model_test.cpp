#include "library_model.hpp"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QFile>

class LibraryModelTest final : public QObject {
    Q_OBJECT

private slots:
    void exposesRolesAndUpdatesFavorite();
    void playRowOnlyRequestsAvailableTracks();
    void trackIdUsesCanonicalPathAndFileIdentity();
    void appendRejectsNormalizedDuplicate();
    void replaceAllRebuildsCanonicalIndexForLargeLibrary();
    void countPropertyTracksRows();
    void findsRowByTrackId();
    void updatesRatingAndPlaybackHistory();
};

void LibraryModelTest::exposesRolesAndUpdatesFavorite()
{
    LibraryModel model;
    TrackRecord track;
    track.trackId = QStringLiteral("track-a");
    track.path = QStringLiteral("C:/music/a.wav");
    track.title = QStringLiteral("A");
    track.artist = QStringLiteral("Artist");
    track.album = QStringLiteral("Album");
    track.format = QStringLiteral("wav");
    track.sampleRate = 96000;
    track.bitDepth = 24;
    track.bitRate = 4608000;
    track.durationMs = 1234;
    track.fileSize = 5678;
    track.coverUrl = QUrl(QStringLiteral("qrc:/AgPlayer/assets/brand/logo-mark.png"));
    track.favorite = false;
    track.rating = 4;
    track.bpm = 128.5;
    track.available = true;
    model.append(track);

    QCOMPARE(model.rowCount(), 1);
    const QModelIndex index = model.index(0);
    QCOMPARE(model.data(index, LibraryModel::TrackIdRole).toString(), track.trackId);
    QCOMPARE(model.data(index, LibraryModel::PathRole).toString(), track.path);
    QCOMPARE(model.data(index, LibraryModel::TitleRole).toString(), track.title);
    QCOMPARE(model.data(index, LibraryModel::CoverUrlRole).toUrl(), track.coverUrl);
    QCOMPARE(model.data(index, LibraryModel::FavoriteRole).toBool(), track.favorite);
    QCOMPARE(model.data(index, LibraryModel::RatingRole).toInt(), track.rating);
    QCOMPARE(model.data(index, LibraryModel::BpmRole).toDouble(), track.bpm);
    QCOMPARE(model.favoriteCount(), 0);
    const QHash<int, QByteArray> roles = model.roleNames();
    QCOMPARE(roles.value(LibraryModel::TrackIdRole), QByteArray("trackId"));
    QCOMPARE(roles.value(LibraryModel::PathRole), QByteArray("path"));
    QCOMPARE(roles.value(LibraryModel::TitleRole), QByteArray("title"));
    QCOMPARE(roles.value(LibraryModel::ArtistRole), QByteArray("artist"));
    QCOMPARE(roles.value(LibraryModel::AlbumRole), QByteArray("album"));
    QCOMPARE(roles.value(LibraryModel::FormatRole), QByteArray("format"));
    QCOMPARE(roles.value(LibraryModel::SampleRateRole), QByteArray("sampleRate"));
    QCOMPARE(roles.value(LibraryModel::BitDepthRole), QByteArray("bitDepth"));
    QCOMPARE(roles.value(LibraryModel::BitRateRole), QByteArray("bitRate"));
    QCOMPARE(roles.value(LibraryModel::DurationMsRole), QByteArray("durationMs"));
    QCOMPARE(roles.value(LibraryModel::FileSizeRole), QByteArray("fileSize"));
    QCOMPARE(roles.value(LibraryModel::CoverUrlRole), QByteArray("coverUrl"));
    QCOMPARE(roles.value(LibraryModel::FavoriteRole), QByteArray("favorite"));
    QCOMPARE(roles.value(LibraryModel::RatingRole), QByteArray("rating"));
    QCOMPARE(roles.value(LibraryModel::BpmRole), QByteArray("bpm"));
    QCOMPARE(roles.value(LibraryModel::AvailableRole), QByteArray("available"));
    QCOMPARE(roles.value(LibraryModel::ImportErrorRole), QByteArray("importError"));
    QCOMPARE(roles.value(LibraryModel::PlayCountRole), QByteArray("playCount"));
    QCOMPARE(roles.value(LibraryModel::LastPlayedAtRole), QByteArray("lastPlayedAtMs"));

    QSignalSpy changed(&model, &LibraryModel::dataChanged);
    QSignalSpy favoriteCountChanged(&model, &LibraryModel::favoriteCountChanged);
    QVERIFY(model.setFavorite(0, true));
    QVERIFY(model.tracks().front().favorite);
    QCOMPARE(model.favoriteCount(), 1);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(changed.front().at(2).value<QList<int>>(), QList<int>{LibraryModel::FavoriteRole});
    QCOMPARE(favoriteCountChanged.count(), 1);
    QVERIFY(!model.setFavorite(-1, true));
}

void LibraryModelTest::playRowOnlyRequestsAvailableTracks()
{
    LibraryModel model;
    TrackRecord available;
    available.path = QStringLiteral("C:/music/available.wav");
    available.available = true;
    model.append(available);
    TrackRecord missing;
    missing.path = QStringLiteral("C:/music/missing.wav");
    missing.available = false;
    model.append(missing);

    QSignalSpy requested(&model, &LibraryModel::playRequested);
    model.playRow(-1);
    model.playRow(1);
    QCOMPARE(requested.count(), 0);
    model.playRow(0);
    QCOMPARE(requested.count(), 1);
    QCOMPARE(requested.front().front().toInt(), 0);
}

void LibraryModelTest::trackIdUsesCanonicalPathAndFileIdentity()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("identity.wav"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("one"), 3);
    file.close();

    TrackRecord first;
    first.path = path;
    first.title = QStringLiteral("First title");
    LibraryModel firstModel;
    firstModel.append(first);

    TrackRecord sameFile;
    sameFile.path = dir.filePath(QStringLiteral("./identity.wav"));
    sameFile.title = QStringLiteral("Changed display metadata");
    LibraryModel secondModel;
    secondModel.append(sameFile);
    QCOMPARE(firstModel.tracks().front().trackId, secondModel.tracks().front().trackId);

    QVERIFY(file.open(QIODevice::Append));
    QCOMPARE(file.write("-changed"), 8);
    file.close();
    LibraryModel changedModel;
    changedModel.append(first);
    QVERIFY(firstModel.tracks().front().trackId != changedModel.tracks().front().trackId);
}

void LibraryModelTest::appendRejectsNormalizedDuplicate()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("duplicate.wav"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("audio"), 5);
    file.close();
    TrackRecord first;
    first.path = path;
    TrackRecord duplicate;
    duplicate.path = dir.filePath(QStringLiteral("./duplicate.wav"));
    LibraryModel model;

    QVERIFY(model.append(first));
    QVERIFY(!model.append(duplicate));
    QCOMPARE(model.rowCount(), 1);
    QVERIFY(model.containsPath(duplicate.path));
}

void LibraryModelTest::replaceAllRebuildsCanonicalIndexForLargeLibrary()
{
    constexpr int trackCount = 4096;
    QList<TrackRecord> tracks;
    tracks.reserve(trackCount + 1);
    for (int index = 0; index < trackCount; ++index) {
        TrackRecord track;
        track.path = QStringLiteral("C:/large-library/track-%1.flac").arg(index);
        tracks.append(std::move(track));
    }
    tracks.append(tracks.front());
    LibraryModel model;

    model.replaceAll(tracks);

    QCOMPARE(model.rowCount(), trackCount);
    for (int index = 0; index < trackCount; ++index) {
        QVERIFY(model.containsPath(
            QStringLiteral("C:/large-library/./track-%1.flac").arg(index)));
    }
    QVERIFY(!model.append(tracks.at(trackCount / 2)));
    TrackRecord replacement;
    replacement.path = QStringLiteral("C:/replacement/new.flac");
    model.replaceAll({replacement});
    QCOMPARE(model.rowCount(), 1);
    QVERIFY(model.containsPath(replacement.path));
    QVERIFY(!model.containsPath(tracks.front().path));
}

void LibraryModelTest::countPropertyTracksRows()
{
    LibraryModel model;
    QSignalSpy countChanged(&model, &LibraryModel::countChanged);
    QCOMPARE(model.count(), 0);

    TrackRecord first;
    first.path = QStringLiteral("C:/music/count-a.wav");
    QVERIFY(model.append(first));
    QCOMPARE(model.count(), 1);
    QCOMPARE(countChanged.count(), 1);

    QVERIFY(!model.append(first));
    QCOMPARE(countChanged.count(), 1);

    TrackRecord second;
    second.path = QStringLiteral("C:/music/count-b.wav");
    model.replaceAll({first, second});
    QCOMPARE(model.count(), 2);
    QCOMPARE(countChanged.count(), 2);
}

void LibraryModelTest::findsRowByTrackId()
{
    TrackRecord first;
    first.trackId = QStringLiteral("track-a");
    first.path = QStringLiteral("C:/music/a.wav");
    TrackRecord second;
    second.trackId = QStringLiteral("track-b");
    second.path = QStringLiteral("C:/music/b.wav");
    LibraryModel model;
    model.replaceAll({first, second});

    QCOMPARE(model.indexForTrackId(QStringLiteral("track-a")), 0);
    QCOMPARE(model.indexForTrackId(QStringLiteral("track-b")), 1);
    QCOMPARE(model.indexForTrackId(QStringLiteral("missing")), -1);
}

void LibraryModelTest::updatesRatingAndPlaybackHistory()
{
    TrackRecord track;
    track.trackId = QStringLiteral("track-a");
    track.path = QStringLiteral("C:/music/a.wav");
    track.rating = 2;
    LibraryModel model;
    model.append(track);
    QSignalSpy changed(&model, &LibraryModel::dataChanged);

    QVERIFY(model.setRating(0, 9));
    QCOMPARE(model.tracks().front().rating, 5);
    QVERIFY(!model.setRating(0, 5));
    QVERIFY(!model.setRating(-1, 3));
    QVERIFY(model.markPlayed(track.trackId, 123456));
    QCOMPARE(model.tracks().front().playCount, 1);
    QCOMPARE(model.tracks().front().lastPlayedAtMs, 123456);
    QVERIFY(!model.markPlayed(QStringLiteral("missing"), 789));
    QCOMPARE(changed.count(), 2);
    QCOMPARE(changed.at(0).at(2).value<QList<int>>(),
             QList<int>{LibraryModel::RatingRole});
    QCOMPARE(changed.at(1).at(2).value<QList<int>>(),
             QList<int>({LibraryModel::PlayCountRole, LibraryModel::LastPlayedAtRole}));
}

QTEST_GUILESS_MAIN(LibraryModelTest)
#include "library_model_test.moc"
