#include "library_filter_model.hpp"
#include "library_model.hpp"
#include "playlist_model.hpp"

#include <QDateTime>
#include <QElapsedTimer>
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
    void filtersAndSortsLargePlaylistWithinBudget();
    void preservesDuplicateSourceTrackOrderInPlaylist();
    void foldsLargeSearchQueryWithinBudget();
    void intersectsTagFolderAndExistingFiltersWithoutSourceReset();
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
    QList<TrackRecord> tracks = sampleTracks();
    tracks[0].albumArtist = QStringLiteral("Compilation Curator");
    tracks[1].genre = QStringLiteral("City Pop");
    tracks[2].composer = QStringLiteral("Ryuichi Sakamoto");
    tracks[3].date = QStringLiteral("2024-05-20");
    source.replaceAll(tracks);
    LibraryFilterModel filter;
    filter.setSourceModel(&source);

    filter.setSearchText(QStringLiteral("BLUE"));
    QCOMPARE(filter.count(), 2);

    filter.setSearchText(QStringLiteral("horizon"));
    QCOMPARE(filter.count(), 2);

    filter.setSearchText(QStringLiteral("curator"));
    QCOMPARE(filter.count(), 1);

    filter.setSearchText(QStringLiteral("city pop"));
    QCOMPARE(filter.count(), 1);

    filter.setSearchText(QStringLiteral("sakamoto"));
    QCOMPARE(filter.count(), 1);

    filter.setSearchText(QStringLiteral("2024-05"));
    QCOMPARE(filter.count(), 1);

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
    const QString otherPlaylistId = playlists.createPlaylist(QStringLiteral("Other"));
    QVERIFY(playlists.addTracks(otherPlaylistId,
                               {QStringLiteral("alpha"), QStringLiteral("delta")}));

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
    filter.setCategory(otherPlaylistId);
    QCOMPARE(filter.count(), 2);
    QCOMPARE(filter.data(filter.index(0, 0), LibraryModel::TrackIdRole).toString(),
             QStringLiteral("alpha"));
    QVERIFY(playlists.moveTrack(playlistId, 2, 1));
    filter.setCategory(playlistId);
    QCOMPARE(filter.count(), 3);
    QCOMPARE(filter.data(filter.index(0, 0), LibraryModel::TrackIdRole).toString(),
             QStringLiteral("beta"));
    QCOMPARE(filter.data(filter.index(1, 0), LibraryModel::TrackIdRole).toString(),
             QStringLiteral("alpha"));
}

void LibraryFilterModelTest::filtersAndSortsLargePlaylistWithinBudget()
{
    constexpr int librarySize = 10000;
    constexpr int playlistSize = 5000;
    QList<TrackRecord> tracks;
    QStringList playlistTrackIds;
    tracks.reserve(librarySize);
    playlistTrackIds.reserve(playlistSize);
    for (int index = 0; index < librarySize; ++index) {
        const QString id = QStringLiteral("track-%1").arg(index, 5, 10, QLatin1Char('0'));
        tracks.append(makeTrack(id, QStringLiteral("Track %1").arg(index),
                                QStringLiteral("Artist"), QStringLiteral("Album"),
                                0, 0.0));
    }
    for (int index = playlistSize - 1; index >= 0; --index)
        playlistTrackIds.append(tracks.at(index * 2).trackId);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    PlaylistModel playlists(dir.filePath(QStringLiteral("playlists.json")));
    const QString playlistId = playlists.createPlaylist(QStringLiteral("Large"));
    QCOMPARE(playlists.addTracks(playlistId, playlistTrackIds), playlistSize);
    LibraryModel source;
    source.replaceAll(std::move(tracks));
    LibraryFilterModel filter;
    filter.setSourceModel(&source);
    filter.setPlaylistModel(&playlists);

    filter.setCategory(QStringLiteral("favorites"));
    QCOMPARE(filter.count(), 0);
    QElapsedTimer baselineElapsed;
    baselineElapsed.start();
    filter.setCategory(QStringLiteral("all"));
    QCOMPARE(filter.count(), librarySize);
    const qint64 allTracksMs = baselineElapsed.elapsed();
    QElapsedTimer playlistElapsed;
    playlistElapsed.start();
    filter.setCategory(playlistId);
    QCOMPARE(filter.count(), playlistSize);
    QCOMPARE(filter.data(filter.index(0, 0), LibraryModel::TrackIdRole).toString(),
             playlistTrackIds.front());
    QCOMPARE(filter.data(filter.index(playlistSize - 1, 0),
                         LibraryModel::TrackIdRole).toString(),
             playlistTrackIds.back());
    const qint64 playlistMs = playlistElapsed.elapsed();
    qInfo("PERF library_filter library=10000 playlist=5000 all_ms=%lld playlist_ms=%lld",
          allTracksMs, playlistMs);
    const qint64 playlistBudgetMs = qMax<qint64>(1500, allTracksMs * 20);
    QVERIFY2(playlistMs < playlistBudgetMs,
             qPrintable(QStringLiteral("10K library / 5K playlist filter+sort took %1 ms "
                                       "(all-tracks baseline %2 ms, budget %3 ms)")
                            .arg(playlistMs).arg(allTracksMs).arg(playlistBudgetMs)));
}

void LibraryFilterModelTest::preservesDuplicateSourceTrackOrderInPlaylist()
{
    TrackRecord first = makeTrack(QStringLiteral("duplicate"), QStringLiteral("First"),
                                  QStringLiteral("Artist"), QStringLiteral("Album"), 0, 0.0);
    TrackRecord second = makeTrack(QStringLiteral("duplicate"), QStringLiteral("Second"),
                                   QStringLiteral("Artist"), QStringLiteral("Album"), 0, 0.0);
    second.path = QStringLiteral("C:/music/duplicate-second.wav");
    LibraryModel source;
    source.replaceAll({first, second});
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    PlaylistModel playlists(dir.filePath(QStringLiteral("playlists.json")));
    const QString playlistId = playlists.createPlaylist(QStringLiteral("Duplicates"));
    QVERIFY(playlists.addTrack(playlistId, QStringLiteral("duplicate")));
    LibraryFilterModel filter;
    filter.setSourceModel(&source);
    filter.setPlaylistModel(&playlists);

    filter.setCategory(playlistId);

    QCOMPARE(filter.count(), 2);
    QCOMPARE(filter.data(filter.index(0, 0), LibraryModel::TitleRole).toString(),
             QStringLiteral("First"));
    QCOMPARE(filter.data(filter.index(1, 0), LibraryModel::TitleRole).toString(),
             QStringLiteral("Second"));
}

void LibraryFilterModelTest::foldsLargeSearchQueryWithinBudget()
{
    constexpr int librarySize = 10000;
    QList<TrackRecord> tracks;
    tracks.reserve(librarySize);
    for (int index = 0; index < librarySize; ++index) {
        const QString id = QStringLiteral("track-%1").arg(index, 5, 10, QLatin1Char('0'));
        tracks.append(makeTrack(id, QStringLiteral("Ordinary title"),
                                QStringLiteral("Artist"), QStringLiteral("Album"),
                                0, 0.0));
    }
    LibraryModel source;
    source.replaceAll(std::move(tracks));
    LibraryFilterModel filter;
    filter.setSourceModel(&source);
    QElapsedTimer shortQueryElapsed;
    shortQueryElapsed.start();
    filter.setSearchText(QStringLiteral("missing"));
    QCOMPARE(filter.count(), 0);
    const qint64 shortQueryMs = shortQueryElapsed.elapsed();
    filter.setSearchText(QString{});
    QCOMPARE(filter.count(), librarySize);
    const QString query = QString(32768, QChar(0x0130));
    QElapsedTimer stressQueryElapsed;
    stressQueryElapsed.start();
    filter.setSearchText(query);
    QCOMPARE(filter.count(), 0);
    const qint64 stressQueryMs = stressQueryElapsed.elapsed();
    qInfo("PERF library_search tracks=10000 short_ms=%lld stress_code_units=32768 stress_ms=%lld",
          shortQueryMs, stressQueryMs);
    const qint64 stressBudgetMs = shortQueryMs * 4 + 300;
    QVERIFY2(stressQueryMs < stressBudgetMs,
             qPrintable(QStringLiteral("10K-track stress-query search took %1 ms "
                                       "(short-query baseline %2 ms, budget %3 ms)")
                            .arg(stressQueryMs).arg(shortQueryMs).arg(stressBudgetMs)));
}

void LibraryFilterModelTest::intersectsTagFolderAndExistingFiltersWithoutSourceReset()
{
    // Catches an OR-combination that leaks tracks from another root/tag and a
    // source-model reset caused by changing only proxy criteria.
    TrackRecord road = makeTrack(QStringLiteral("road"), QStringLiteral("Night Ride"),
                                 QStringLiteral("Artist"), QStringLiteral("Album"), 5, 128.0);
    road.path = QStringLiteral("C:/Music/A/road.mp3");
    road.tags = {QStringLiteral("Road")};
    TrackRecord jazz = makeTrack(QStringLiteral("jazz"), QStringLiteral("Night Jazz"),
                                 QStringLiteral("Artist"), QStringLiteral("Album"), 5, 128.0);
    jazz.path = QStringLiteral("C:/Music/B/jazz.mp3");
    jazz.tags = {QStringLiteral("Jazz")};
    LibraryModel source;
    source.replaceAll({road, jazz});
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    PlaylistModel playlists(dir.filePath(QStringLiteral("playlists.json")));
    const QString playlist = playlists.createPlaylist(QStringLiteral("Road trips"));
    QVERIFY(playlists.addTrack(playlist, QStringLiteral("road")));
    LibraryFilterModel filter;
    filter.setSourceModel(&source);
    filter.setPlaylistModel(&playlists);
    QSignalSpy reset(&source, &QAbstractItemModel::modelReset);

    filter.setTagKey(QStringLiteral("road"));
    filter.setResourceFolder(QStringLiteral("C:/Music/A"));
    filter.setCategory(playlist);
    filter.setSearchText(QStringLiteral("night"));
    filter.setExactRating(5);
    filter.setMinBpm(120.0);
    filter.setMaxBpm(140.0);

    QCOMPARE(filter.count(), 1);
    QCOMPARE(filter.data(filter.index(0, 0), LibraryModel::TrackIdRole).toString(),
             QStringLiteral("road"));
    QCOMPARE(reset.count(), 0);
}

QTEST_GUILESS_MAIN(LibraryFilterModelTest)
#include "library_filter_model_test.moc"
