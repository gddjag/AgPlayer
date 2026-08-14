#include "library_filter_model.hpp"
#include "library_model.hpp"
#include "playlist_model.hpp"

#include <QDateTime>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class LibraryFilterModelTest final : public QObject {
    Q_OBJECT

private slots:
    void defaultFullRangeDoesNotHideTracks();
    void filtersKeywordAcrossMetadataFields();
    void filtersKeywordAcrossCustomTags();
    void combinesExactRatingBpmAndFavoriteFilters();
    void filtersExactRatingInsteadOfMinimumRating();
    void filtersAndSortsPlaybackHistory();
    void filtersRecentAndNeverPlayedCategories();
    void newImportAppearsInRecentAndLeavesNeverPlayedAfterPlayback();
    void writesRatingThroughProxyRows();
    void filtersPlaylistMembershipAndTracksLiveChanges();
    void preservesCustomPlaylistOrder();
};

namespace {

TrackRecord makeTrack(const QString& id,
                      const QString& title,
                      const QString& artist,
                      const QString& album,
                      int rating,
                      double bpm,
                      bool favorite = false)
{
    TrackRecord track;
    track.trackId = id;
    track.path = QStringLiteral("C:/music/") + id + QStringLiteral(".wav");
    track.title = title;
    track.artist = artist;
    track.album = album;
    track.rating = rating;
    track.bpm = bpm;
    track.favorite = favorite;
    track.available = true;
    return track;
}

QList<TrackRecord> sampleTracks()
{
    return {
        makeTrack(QStringLiteral("alpha"), QStringLiteral("Blue Horizon"),
                  QStringLiteral("DJ Green"), QStringLiteral("Night Drive"),
                  5, 128.0, true),
        makeTrack(QStringLiteral("beta"), QStringLiteral("Warm Rain"),
                  QStringLiteral("An"), QStringLiteral("Blue Room"),
                  3, 95.0),
        makeTrack(QStringLiteral("gamma"), QStringLiteral("Fast Lane"),
                  QStringLiteral("Horizon Crew"), QStringLiteral("Road"),
                  4, 150.0, true),
        makeTrack(QStringLiteral("delta"), QStringLiteral("Untimed"),
                  QStringLiteral("Unknown"), QStringLiteral("Archive"),
                  2, 0.0),
    };
}

} // namespace

void LibraryFilterModelTest::defaultFullRangeDoesNotHideTracks()
{
    LibraryModel source;
    QList<TrackRecord> tracks = sampleTracks();
    tracks.append(makeTrack(QStringLiteral("epsilon"),
                            QStringLiteral("Double Time"),
                            QStringLiteral("Pulse"),
                            QStringLiteral("Fast"),
                            1, 210.0));
    source.replaceAll(tracks);

    LibraryFilterModel filter;
    filter.setSourceModel(&source);

    QCOMPARE(filter.minBpm(), 60.0);
    QCOMPARE(filter.maxBpm(), 160.0);
    QCOMPARE(filter.count(), tracks.size());
}

void LibraryFilterModelTest::filtersKeywordAcrossMetadataFields()
{
    LibraryModel source;
    source.replaceAll(sampleTracks());
    LibraryFilterModel filter;
    filter.setSourceModel(&source);

    filter.setSearchText(QStringLiteral("BLUE"));
    QCOMPARE(filter.count(), 2);

    filter.setSearchText(QStringLiteral("horizon"));
    QCOMPARE(filter.count(), 2);

    filter.setSearchText(QStringLiteral("missing"));
    QCOMPARE(filter.count(), 0);
}

void LibraryFilterModelTest::filtersKeywordAcrossCustomTags()
{
    QList<TrackRecord> tracks = sampleTracks();
    tracks[1].tags = {QStringLiteral("Chill"), QStringLiteral("Rainy Night")};
    LibraryModel source;
    source.replaceAll(tracks);
    LibraryFilterModel filter;
    filter.setSourceModel(&source);

    filter.setSearchText(QStringLiteral("rainy"));
    QCOMPARE(filter.count(), 1);
    QCOMPARE(filter.data(filter.index(0, 0), LibraryModel::TrackIdRole).toString(),
             QStringLiteral("beta"));
}

void LibraryFilterModelTest::combinesExactRatingBpmAndFavoriteFilters()
{
    LibraryModel source;
    source.replaceAll(sampleTracks());
    LibraryFilterModel filter;
    filter.setSourceModel(&source);
    filter.setSearchText(QStringLiteral("horizon"));
    filter.setExactRating(5);
    filter.setMinBpm(120.0);
    filter.setMaxBpm(140.0);
    filter.setCategory(QStringLiteral("favorites"));

    QCOMPARE(filter.count(), 1);
    QCOMPARE(filter.data(filter.index(0, 0), LibraryModel::TrackIdRole).toString(),
             QStringLiteral("alpha"));
}

void LibraryFilterModelTest::filtersExactRatingInsteadOfMinimumRating()
{
    LibraryModel source;
    source.replaceAll(sampleTracks());
    LibraryFilterModel filter;
    filter.setSourceModel(&source);

    filter.setExactRating(3);

    QCOMPARE(filter.count(), 1);
    QCOMPARE(filter.data(filter.index(0, 0), LibraryModel::TrackIdRole).toString(),
             QStringLiteral("beta"));
}

void LibraryFilterModelTest::filtersAndSortsPlaybackHistory()
{
    LibraryModel source;
    source.replaceAll(sampleTracks());
    QVERIFY(source.markPlayed(QStringLiteral("alpha"), 1000));
    QVERIFY(source.markPlayed(QStringLiteral("gamma"), 3000));
    QVERIFY(source.markPlayed(QStringLiteral("beta"), 2000));

    LibraryFilterModel filter;
    filter.setSourceModel(&source);
    filter.setCategory(QStringLiteral("history"));

    QCOMPARE(filter.count(), 3);
    QCOMPARE(filter.data(filter.index(0, 0), LibraryModel::TrackIdRole).toString(),
             QStringLiteral("gamma"));
    QCOMPARE(filter.data(filter.index(1, 0), LibraryModel::TrackIdRole).toString(),
             QStringLiteral("beta"));
    QCOMPARE(filter.data(filter.index(2, 0), LibraryModel::TrackIdRole).toString(),
             QStringLiteral("alpha"));
}

void LibraryFilterModelTest::filtersRecentAndNeverPlayedCategories()
{
    QList<TrackRecord> tracks = sampleTracks();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    tracks[0].addedAtMs = now - 1000;
    tracks[1].addedAtMs = now - (31LL * 24 * 60 * 60 * 1000);
    tracks[2].addedAtMs = now - 2000;
    tracks[3].addedAtMs = 0;

    LibraryModel source;
    source.replaceAll(tracks);
    QVERIFY(source.markPlayed(QStringLiteral("alpha"), now));

    LibraryFilterModel filter;
    filter.setSourceModel(&source);
    filter.setCategory(QStringLiteral("recentAdded"));
    QCOMPARE(filter.count(), 2);
    QCOMPARE(filter.data(filter.index(0, 0), LibraryModel::TrackIdRole).toString(),
             QStringLiteral("alpha"));

    filter.setCategory(QStringLiteral("neverPlayed"));
    QCOMPARE(filter.count(), 3);
    for (int row = 0; row < filter.count(); ++row) {
        QVERIFY(filter.data(filter.index(row, 0), LibraryModel::TrackIdRole).toString()
                != QStringLiteral("alpha"));
    }
}

void LibraryFilterModelTest::newImportAppearsInRecentAndLeavesNeverPlayedAfterPlayback()
{
    LibraryModel source;
    TrackRecord track = makeTrack(QStringLiteral("new"),
                                  QStringLiteral("New import"),
                                  QStringLiteral("Artist"),
                                  QStringLiteral("Album"), 0, 100.0);
    QVERIFY(source.append(track));

    LibraryFilterModel filter;
    filter.setSourceModel(&source);
    filter.setCategory(QStringLiteral("recentAdded"));
    QCOMPARE(filter.count(), 1);

    filter.setCategory(QStringLiteral("neverPlayed"));
    QCOMPARE(filter.count(), 1);
    QVERIFY(source.markPlayed(source.tracks().front().trackId, 123456));
    QCOMPARE(filter.count(), 0);
}

void LibraryFilterModelTest::writesRatingThroughProxyRows()
{
    LibraryModel source;
    source.replaceAll(sampleTracks());
    LibraryFilterModel filter;
    filter.setSourceModel(&source);
    filter.setSearchText(QStringLiteral("Warm Rain"));
    QCOMPARE(filter.count(), 1);

    QSignalSpy changed(&source, &QAbstractItemModel::dataChanged);
    QVERIFY(filter.setRating(0, 5));
    QCOMPARE(source.tracks().at(1).rating, 5);
    QCOMPARE(changed.count(), 1);
    QVERIFY(!filter.setRating(4, 2));
}

void LibraryFilterModelTest::filtersPlaylistMembershipAndTracksLiveChanges()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    PlaylistModel playlists(dir.filePath(QStringLiteral("playlists.json")));
    const QString playlistId = playlists.createPlaylist(QStringLiteral("Focus"));
    QVERIFY(playlists.addTrack(playlistId, QStringLiteral("beta")));

    LibraryModel source;
    source.replaceAll(sampleTracks());
    LibraryFilterModel filter;
    filter.setSourceModel(&source);
    filter.setPlaylistModel(&playlists);
    filter.setCategory(playlistId);

    QCOMPARE(filter.count(), 1);
    QCOMPARE(filter.data(filter.index(0, 0), LibraryModel::TrackIdRole).toString(),
             QStringLiteral("beta"));
    QVERIFY(playlists.addTrack(playlistId, QStringLiteral("gamma")));
    QCOMPARE(filter.count(), 2);
    QVERIFY(playlists.removeTrack(playlistId, QStringLiteral("beta")));
    QCOMPARE(filter.count(), 1);
}

void LibraryFilterModelTest::preservesCustomPlaylistOrder()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    PlaylistModel playlists(dir.filePath(QStringLiteral("playlists.json")));
    const QString playlistId = playlists.createPlaylist(QStringLiteral("Focus"));
    QVERIFY(playlists.addTracks(playlistId,
                               {QStringLiteral("gamma"), QStringLiteral("alpha"),
                                QStringLiteral("beta")}));

    LibraryModel source;
    source.replaceAll(sampleTracks());
    LibraryFilterModel filter;
    filter.setSourceModel(&source);
    filter.setPlaylistModel(&playlists);
    filter.setCategory(playlistId);

    QCOMPARE(filter.data(filter.index(0, 0), LibraryModel::TrackIdRole).toString(),
             QStringLiteral("gamma"));
    QCOMPARE(filter.data(filter.index(1, 0), LibraryModel::TrackIdRole).toString(),
             QStringLiteral("alpha"));
    QVERIFY(playlists.moveTrack(playlistId, 2, 0));
    QCOMPARE(filter.data(filter.index(0, 0), LibraryModel::TrackIdRole).toString(),
             QStringLiteral("beta"));
}

QTEST_GUILESS_MAIN(LibraryFilterModelTest)
#include "library_filter_model_test.moc"
