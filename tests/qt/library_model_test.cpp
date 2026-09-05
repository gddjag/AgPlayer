#include "library_model.hpp"

#include <QDateTime>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

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
    void updatesRenamedPathsAsOneBatchWithoutChangingTrackIds();
    void updatesRatingAndPlaybackHistory();
    void removesOnlyTheSelectedHistoryEntry();
    void updatesTagsAndManualOrder();
    void batchesTagMutationsWithoutResetOrExtraFlush();
    void removesOneTagOnlyFromRequestedTracks();
    void removesTrackWithoutDeletingTheFile();
    void appendsLargeBatchesWithSingleModelNotification();
    void insertBatchPreservesPrefixAndUpdatesShiftedIndexes();
    void stampsNewImportsWithoutOverwritingExistingTimestamps();
    void exposesLiveRecentAndNeverPlayedCounts();
    void missingLocalCoverFallsBackToPackagedArtwork();
    void mediaKindProbePublishesOneAtomicChange();
    void staleMetadataProbeCannotOverwriteRelocatedTrack();
    void staleMetadataProbeCannotStealRecreatedTrackClaim();
    void metadataRefreshRejectsStaleClaimsAndPreservesUserState();
};

void LibraryModelTest::insertBatchPreservesPrefixAndUpdatesShiftedIndexes()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const auto track = [&temp](int id) {
        TrackRecord record;
        record.trackId = QString::number(id);
        record.path = temp.filePath(QStringLiteral("track-%1.wav").arg(id));
        record.available = true;
        return record;
    };
    LibraryModel model;
    model.appendBatch({track(0), track(3), track(4)});
    QCOMPARE(model.insertBatch(1, {track(1), track(2), track(3)}),
             QStringList({QStringLiteral("1"), QStringLiteral("2")}));
    for (int i = 0; i < 5; ++i) {
        QCOMPARE(model.indexForTrackId(QString::number(i)), i);
        QCOMPARE(model.indexForLocalFile(track(i).path), i);
        QVERIFY(model.containsPath(track(i).path));
    }
    QCOMPARE(model.appendBatch({track(5), track(0)}), QStringList{QStringLiteral("5")});
    QCOMPARE(model.indexForTrackId(QStringLiteral("0")), 0);
    QCOMPARE(model.indexForTrackId(QStringLiteral("5")), 5);
    QCOMPARE(model.indexForLocalFile(track(5).path), 5);
}

void LibraryModelTest::metadataRefreshRejectsStaleClaimsAndPreservesUserState()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    TrackRecord original;
    original.trackId = QStringLiteral("refresh");
    original.path = temp.filePath(QStringLiteral("original.flac"));
    original.title = QStringLiteral("Before");
    original.rating = 4;
    original.favorite = true;
    original.playCount = 17;
    original.tags = {QStringLiteral("user-tag")};
    LibraryModel model;
    QVERIFY(model.append(original));
    const auto older = model.beginMetadataRefresh(original.trackId);
    const auto newer = model.beginMetadataRefresh(original.trackId);
    QVERIFY(older && newer);
    TrackRecord metadata;
    metadata.path = newer->path;
    metadata.title = QStringLiteral("After");
    metadata.sampleRate = 96000;
    metadata.coverUrl = QUrl(QStringLiteral("qrc:/new-cover.png"));
    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
    QSignalSpy flushed(&model, &LibraryModel::flushRequested);
    QCOMPARE(model.completeMetadataRefreshes({{*older, metadata}}), 0);
    QVERIFY(!model.abandonMetadataProbe(*older));
    // A result with the right generation and wrong source identity is rejected.
    TrackRecord wrongPath = metadata;
    wrongPath.path = temp.filePath(QStringLiteral("other.flac"));
    QCOMPARE(model.completeMetadataRefreshes({{*newer, wrongPath}}), 0);
    QCOMPARE(model.completeMetadataRefreshes({{*newer, metadata}}), 1);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(flushed.count(), 1);
    const TrackRecord* updated = model.recordForId(original.trackId);
    QVERIFY(updated);
    QCOMPARE(updated->title, metadata.title);
    QCOMPARE(updated->sampleRate, 96000);
    QCOMPARE(updated->coverUrl, metadata.coverUrl);
    QCOMPARE(updated->rating, 4);
    QVERIFY(updated->favorite);
    QCOMPARE(updated->playCount, 17);
    QCOMPARE(updated->tags, original.tags);
    QCOMPARE(model.completeMetadataRefreshes({{*newer, metadata}}), 0);

    const auto beforeMove = model.beginMetadataRefresh(original.trackId);
    QVERIFY(beforeMove);
    const QString relocated = temp.filePath(QStringLiteral("relocated.flac"));
    QVERIFY(model.updateTrackPath(original.trackId, relocated));
    QCOMPARE(model.completeMetadataRefreshes({{*beforeMove, metadata}}), 0);
    const auto beforeRemoval = model.beginMetadataRefresh(original.trackId);
    QVERIFY(beforeRemoval);
    QVERIFY(model.removeTrack(original.trackId));
    QVERIFY(model.append(original));
    const auto recreated = model.beginMetadataRefresh(original.trackId);
    QVERIFY(recreated);
    metadata.path = beforeRemoval->path;
    QCOMPARE(model.completeMetadataRefreshes({{*beforeRemoval, metadata}}), 0);
    metadata.path = recreated->path;
    QCOMPARE(model.completeMetadataRefreshes({{*recreated, metadata}}), 1);
}

void LibraryModelTest::mediaKindProbePublishesOneAtomicChange()
{
    TrackRecord legacy;
    legacy.trackId = QStringLiteral("legacy-video");
    legacy.path = QStringLiteral("C:/media/legacy-video.avi");
    legacy.available = true;
    legacy.hasAudio = true;
    LibraryModel model;
    model.replaceAll({legacy});
    const auto claim = model.beginMetadataProbe(legacy.trackId);
    QVERIFY(claim.has_value());
    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);

    TrackRecord probed;
    probed.path = legacy.path;
    probed.hasAudio = true;
    probed.hasVideo = true;
    QVERIFY(model.completeMediaKindProbe(*claim, true, probed));

    QCOMPARE(changed.count(), 1);
    const QList<int> roles = qvariant_cast<QList<int>>(changed.at(0).at(2));
    QVERIFY(roles.contains(LibraryModel::MetadataProbeAttemptedRole));
    QVERIFY(roles.contains(LibraryModel::HasAudioRole));
    QVERIFY(roles.contains(LibraryModel::HasVideoRole));
    const TrackRecord* current = model.recordForId(legacy.trackId);
    QVERIFY(current != nullptr);
    QVERIFY(current->metadataProbeAttempted);
    QVERIFY(current->hasAudio);
    QVERIFY(current->hasVideo);
}

void LibraryModelTest::staleMetadataProbeCannotStealRecreatedTrackClaim()
{
    TrackRecord original;
    original.trackId = QStringLiteral("recreated-probe");
    original.path = QStringLiteral("C:/same/location.wav");
    original.available = true;
    LibraryModel model;
    model.replaceAll({original});

    const auto oldClaim = model.beginMetadataProbe(original.trackId);
    QVERIFY(oldClaim.has_value());
    QVERIFY(oldClaim->generation != 0);
    QVERIFY(model.removeTrack(original.trackId));
    QVERIFY(model.append(original));
    const auto newClaim = model.beginMetadataProbe(original.trackId);
    QVERIFY(newClaim.has_value());
    QVERIFY(newClaim->generation > oldClaim->generation);

    TrackRecord oldResult;
    oldResult.path = oldClaim->path;
    oldResult.format = QStringLiteral("wav");
    oldResult.sampleRate = 44100;
    oldResult.bitDepth = 16;
    oldResult.channels = 2;
    oldResult.bitRate = 1411200;
    oldResult.durationMs = 5000;
    oldResult.fileSize = 882000;
    QVERIFY(!model.completeMetadataProbe(*oldClaim, true, oldResult));

    TrackRecord newResult = oldResult;
    newResult.path = newClaim->path;
    newResult.sampleRate = 96000;
    newResult.bitDepth = 24;
    newResult.channels = 6;
    newResult.bitRate = 13824000;
    newResult.durationMs = 7000;
    newResult.fileSize = 12096000;
    QVERIFY(model.completeMetadataProbe(*newClaim, true, newResult));

    const TrackRecord* current = model.recordForId(original.trackId);
    QVERIFY(current != nullptr);
    QCOMPARE(current->sampleRate, 96000);
    QCOMPARE(current->bitDepth, 24);
    QCOMPARE(current->channels, 6);
    QCOMPARE(current->bitRate, qint64{13824000});
    QCOMPARE(current->durationMs, qint64{7000});
    QCOMPARE(current->fileSize, qint64{12096000});
    QVERIFY(current->metadataProbeAttempted);
}

void LibraryModelTest::staleMetadataProbeCannotOverwriteRelocatedTrack()
{
    TrackRecord legacy;
    legacy.trackId = QStringLiteral("relocated-probe");
    legacy.path = QStringLiteral("C:/old/location.wav");
    legacy.available = true;
    LibraryModel model;
    model.replaceAll({legacy});

    const auto claim = model.beginMetadataProbe(legacy.trackId);
    QVERIFY(claim.has_value());
    QVERIFY(model.updateTrackPath(legacy.trackId,
                                  QStringLiteral("C:/new/location.wav")));
    const auto relocatedClaim = model.beginMetadataProbe(legacy.trackId);
    QVERIFY(relocatedClaim.has_value());
    TrackRecord staleResult;
    staleResult.path = claim->path;
    staleResult.format = QStringLiteral("wav");
    staleResult.sampleRate = 44100;
    staleResult.bitDepth = 16;
    staleResult.channels = 2;
    staleResult.bitRate = 1411200;
    staleResult.durationMs = 5000;
    staleResult.fileSize = 882000;

    QVERIFY(!model.completeMetadataProbe(*claim, true, staleResult));
    const TrackRecord* current = model.recordForId(legacy.trackId);
    QVERIFY(current != nullptr);
    QCOMPARE(current->sampleRate, 0);
    QCOMPARE(current->channels, 0);
    QVERIFY(!current->metadataProbeAttempted);

    TrackRecord relocatedResult = staleResult;
    relocatedResult.path = relocatedClaim->path;
    relocatedResult.sampleRate = 96000;
    QVERIFY(model.completeMetadataProbe(*relocatedClaim, true, relocatedResult));
    current = model.recordForId(legacy.trackId);
    QCOMPARE(current->sampleRate, 96000);
    QVERIFY(current->metadataProbeAttempted);
}

void LibraryModelTest::missingLocalCoverFallsBackToPackagedArtwork()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    TrackRecord track;
    track.trackId = QStringLiteral("missing-cover");
    track.path = dir.filePath(QStringLiteral("track.mp3"));
    track.coverUrl = QUrl::fromLocalFile(
        dir.filePath(QStringLiteral("deleted-cover.jpg")));

    LibraryModel model;
    model.replaceAll({track});

    const QUrl expected(QStringLiteral(
        "qrc:/qt/qml/AgPlayer/assets/brand/logo-mark.png"));
    QCOMPARE(model.data(model.index(0, 0), LibraryModel::CoverUrlRole).toUrl(),
             expected);
    QCOMPARE(model.trackForId(track.trackId)
                 .value(QStringLiteral("coverUrl")).toUrl(),
             expected);
}

void LibraryModelTest::exposesLiveRecentAndNeverPlayedCounts()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    TrackRecord recentUnplayed;
    recentUnplayed.path = QStringLiteral("C:/music/recent-unplayed.wav");
    recentUnplayed.addedAtMs = now - 1'000;
    TrackRecord oldUnplayed;
    oldUnplayed.path = QStringLiteral("C:/music/old-unplayed.wav");
    oldUnplayed.addedAtMs = now - 31LL * 24 * 60 * 60 * 1'000;
    TrackRecord recentPlayed;
    recentPlayed.path = QStringLiteral("C:/music/recent-played.wav");
    recentPlayed.addedAtMs = now - 2'000;
    recentPlayed.playCount = 1;

    LibraryModel model;
    QSignalSpy recentChanged(&model, &LibraryModel::recentAddedCountChanged);
    QSignalSpy neverChanged(&model, &LibraryModel::neverPlayedCountChanged);
    model.replaceAll({recentUnplayed, oldUnplayed, recentPlayed});
    QCOMPARE(model.recentAddedCount(), 2);
    QCOMPARE(model.neverPlayedCount(), 2);
    QCOMPARE(recentChanged.count(), 1);
    QCOMPARE(neverChanged.count(), 1);

    const QString playedId = model.tracks().front().trackId;
    QVERIFY(model.markPlayed(playedId, now));
    QCOMPARE(model.neverPlayedCount(), 1);
    QCOMPARE(neverChanged.count(), 2);
    QVERIFY(model.removeTrack(model.tracks().at(0).trackId));
    QCOMPARE(model.neverPlayedCount(), 1);
    QVERIFY(model.removeTrack(model.tracks().at(0).trackId));
    QCOMPARE(model.neverPlayedCount(), 0);
}

void LibraryModelTest::stampsNewImportsWithoutOverwritingExistingTimestamps()
{
    TrackRecord first;
    first.path = QStringLiteral("C:/music/new-a.wav");
    TrackRecord second;
    second.path = QStringLiteral("C:/music/new-b.wav");
    TrackRecord preserved;
    preserved.path = QStringLiteral("C:/music/known.wav");
    preserved.addedAtMs = 123456;
    const qint64 before = QDateTime::currentMSecsSinceEpoch();

    LibraryModel model;
    model.appendBatch({first, second, preserved});

    QCOMPARE(model.rowCount(), 3);
    QVERIFY(model.tracks().at(0).addedAtMs >= before);
    QCOMPARE(model.tracks().at(1).addedAtMs,
             model.tracks().at(0).addedAtMs);
    QCOMPARE(model.tracks().at(2).addedAtMs, Q_INT64_C(123456));
}

void LibraryModelTest::appendsLargeBatchesWithSingleModelNotification()
{
    LibraryModel model;
    QList<TrackRecord> tracks;
    tracks.reserve(250);
    for (int index = 0; index < 250; ++index) {
        TrackRecord track;
        track.path = QStringLiteral("C:/music/batch-%1.wav").arg(index);
        track.title = QStringLiteral("Batch %1").arg(index);
        track.available = true;
        tracks.append(std::move(track));
    }
    tracks.append(tracks.front());

    QSignalSpy rowsInserted(&model, &QAbstractItemModel::rowsInserted);
    QSignalSpy countChanged(&model, &LibraryModel::countChanged);
    const QStringList inserted = model.appendBatch(std::move(tracks));

    QCOMPARE(inserted.size(), 250);
    QCOMPARE(model.rowCount(), 250);
    QCOMPARE(rowsInserted.count(), 1);
    QCOMPARE(countChanged.count(), 1);
}

void LibraryModelTest::exposesRolesAndUpdatesFavorite()
{
    LibraryModel model;
    TrackRecord track;
    track.trackId = QStringLiteral("track-a");
    track.path = QStringLiteral("C:/music/a.wav");
    track.title = QStringLiteral("A");
    track.artist = QStringLiteral("Artist");
    track.album = QStringLiteral("Album");
    track.albumArtist = QStringLiteral("Album Artist");
    track.genre = QStringLiteral("City Pop");
    track.year = QStringLiteral("2024");
    track.date = QStringLiteral("2024-05-20");
    track.composer = QStringLiteral("Composer");
    track.format = QStringLiteral("wav");
    track.sampleRate = 96000;
    track.bitDepth = 24;
    track.channels = 2;
    track.hasAudio = true;
    track.hasVideo = true;
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
    QCOMPARE(model.data(index, LibraryModel::AlbumArtistRole).toString(), track.albumArtist);
    QCOMPARE(model.data(index, LibraryModel::GenreRole).toString(), track.genre);
    QCOMPARE(model.data(index, LibraryModel::YearRole).toString(), track.year);
    QCOMPARE(model.data(index, LibraryModel::DateRole).toString(), track.date);
    QCOMPARE(model.data(index, LibraryModel::ComposerRole).toString(), track.composer);
    QCOMPARE(model.data(index, LibraryModel::ChannelsRole).toInt(), track.channels);
    QCOMPARE(model.data(index, LibraryModel::HasAudioRole).toBool(), true);
    QCOMPARE(model.data(index, LibraryModel::HasVideoRole).toBool(), true);
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
    QCOMPARE(roles.value(LibraryModel::AlbumArtistRole), QByteArray("albumArtist"));
    QCOMPARE(roles.value(LibraryModel::GenreRole), QByteArray("genre"));
    QCOMPARE(roles.value(LibraryModel::YearRole), QByteArray("year"));
    QCOMPARE(roles.value(LibraryModel::DateRole), QByteArray("date"));
    QCOMPARE(roles.value(LibraryModel::ComposerRole), QByteArray("composer"));
    QCOMPARE(roles.value(LibraryModel::FormatRole), QByteArray("format"));
    QCOMPARE(roles.value(LibraryModel::SampleRateRole), QByteArray("sampleRate"));
    QCOMPARE(roles.value(LibraryModel::BitDepthRole), QByteArray("bitDepth"));
    QCOMPARE(roles.value(LibraryModel::ChannelsRole), QByteArray("channels"));
    QCOMPARE(roles.value(LibraryModel::HasAudioRole), QByteArray("hasAudio"));
    QCOMPARE(roles.value(LibraryModel::HasVideoRole), QByteArray("hasVideo"));
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
    changed.clear();
    QSignalSpy flushRequested(&model, &LibraryModel::flushRequested);
    QVERIFY(model.setBpm(track.trackId, 127.5));
    QCOMPARE(model.data(index, LibraryModel::BpmRole).toDouble(), 127.5);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(changed.front().at(2).value<QList<int>>(),
             QList<int>{LibraryModel::BpmRole});
    QCOMPARE(flushRequested.count(), 1);
    QVERIFY(!model.setBpm(track.trackId, 127.5));
    QVERIFY(!model.setBpm(track.trackId, 10.0));
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

void LibraryModelTest::updatesRenamedPathsAsOneBatchWithoutChangingTrackIds()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString firstPath = directory.filePath(QStringLiteral("first.wav"));
    const QString secondPath = directory.filePath(QStringLiteral("second.wav"));
    QFile(firstPath).open(QIODevice::WriteOnly);
    QFile(secondPath).open(QIODevice::WriteOnly);
    TrackRecord first;
    first.trackId = QStringLiteral("first-id");
    first.path = firstPath;
    first.rating = 5;
    TrackRecord second;
    second.trackId = QStringLiteral("second-id");
    second.path = secondPath;
    second.favorite = true;
    LibraryModel model;
    model.replaceAll({first, second});

    QVERIFY(model.updateTrackPaths({{QStringLiteral("first-id"), secondPath},
                                    {QStringLiteral("second-id"), firstPath}}));
    QCOMPARE(model.trackForId(QStringLiteral("first-id")).value(QStringLiteral("path")).toString(), secondPath);
    QCOMPARE(model.trackForId(QStringLiteral("first-id")).value(QStringLiteral("rating")).toInt(), 5);
    QCOMPARE(model.trackForId(QStringLiteral("second-id")).value(QStringLiteral("path")).toString(), firstPath);
    QVERIFY(model.trackForId(QStringLiteral("second-id")).value(QStringLiteral("favorite")).toBool());
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

void LibraryModelTest::removesOnlyTheSelectedHistoryEntry()
{
    TrackRecord track;
    track.trackId = QStringLiteral("track-a");
    track.path = QStringLiteral("C:/music/a.wav");
    track.playCount = 4;
    track.lastPlayedAtMs = 123456;
    LibraryModel model;
    model.append(track);

    QVERIFY(model.removeFromHistory(track.trackId));
    QCOMPARE(model.count(), 1);
    QCOMPARE(model.tracks().front().playCount, 0);
    QCOMPARE(model.tracks().front().lastPlayedAtMs, 0);
    QVERIFY(!model.removeFromHistory(track.trackId));
    QVERIFY(!model.removeFromHistory(QStringLiteral("missing")));
}

void LibraryModelTest::updatesTagsAndManualOrder()
{
    TrackRecord first;
    first.trackId = QStringLiteral("first");
    first.path = QStringLiteral("C:/music/first.wav");
    TrackRecord second;
    second.trackId = QStringLiteral("second");
    second.path = QStringLiteral("C:/music/second.wav");
    LibraryModel model;
    model.replaceAll({first, second});

    QVERIFY(model.setTags(QStringLiteral("first"),
                          {QStringLiteral(" Workout "),
                           QStringLiteral("workout"),
                           QStringLiteral("Night")}));
    QCOMPARE(model.data(model.index(0), LibraryModel::TagsRole).toStringList(),
             QStringList({QStringLiteral("Workout"), QStringLiteral("Night")}));
    QVERIFY(model.moveTrack(1, 0));
    QCOMPARE(model.data(model.index(0), LibraryModel::TrackIdRole).toString(),
             QStringLiteral("second"));
    QCOMPARE(model.data(model.index(1), LibraryModel::TrackIdRole).toString(),
             QStringLiteral("first"));
    TrackRecord third;
    third.trackId = QStringLiteral("third");
    third.path = QStringLiteral("C:/music/third.wav");
    QVERIFY(model.append(third));
    QCOMPARE(model.reorderTracks({QStringLiteral("first"), QStringLiteral("third")},
                                 QStringLiteral("second")), 2);
    QCOMPARE(model.data(model.index(0), LibraryModel::TrackIdRole).toString(),
             QStringLiteral("first"));
    QCOMPARE(model.data(model.index(1), LibraryModel::TrackIdRole).toString(),
             QStringLiteral("third"));
    QCOMPARE(model.data(model.index(2), LibraryModel::TrackIdRole).toString(),
             QStringLiteral("second"));
    const QVariantMap snapshot = model.trackForId(QStringLiteral("first"));
    QCOMPARE(snapshot.value(QStringLiteral("tags")).toStringList(),
             QStringList({QStringLiteral("Workout"), QStringLiteral("Night")}));
    QCOMPARE(snapshot.value(QStringLiteral("title")).toString(), first.title);
}

void LibraryModelTest::batchesTagMutationsWithoutResetOrExtraFlush()
{
    // Catches an accidental per-row disk flush, a model reset, or a tag edit
    // that fails to expose the exact old/new value needed by incremental users.
    TrackRecord first;
    first.trackId = QStringLiteral("one");
    first.path = QStringLiteral("C:/music/one.wav");
    first.tags = {QStringLiteral("Rock")};
    TrackRecord second;
    second.trackId = QStringLiteral("two");
    second.path = QStringLiteral("C:/music/two.wav");
    second.tags = {QStringLiteral("rock"), QStringLiteral("Night")};
    LibraryModel model;
    model.replaceAll({first, second});
    QSignalSpy changes(&model, &LibraryModel::tagsChanged);
    QSignalSpy flushes(&model, &LibraryModel::flushRequested);
    QSignalSpy resets(&model, &QAbstractItemModel::modelReset);

    QCOMPARE(model.setTagsForTracks({QStringLiteral("one"), QStringLiteral("two")},
                                    {QStringLiteral(" Road "), QStringLiteral("road")}), 2);
    QCOMPARE(changes.count(), 2);
    QCOMPARE(flushes.count(), 1);
    QCOMPARE(resets.count(), 0);
    QCOMPARE(model.data(model.index(0), LibraryModel::TagsRole).toStringList(),
             QStringList{QStringLiteral("Road")});
    QCOMPARE(model.renameTag(QStringLiteral("road"), QStringLiteral("Driving")), 2);
    QCOMPARE(model.removeTag(QStringLiteral("DRIVING")), 2);
    QCOMPARE(model.count(), 2);
}

void LibraryModelTest::removesOneTagOnlyFromRequestedTracks()
{
    TrackRecord first;
    first.trackId = QStringLiteral("one");
    first.path = QStringLiteral("C:/music/one.wav");
    first.tags = {QStringLiteral("Focus"), QStringLiteral("Night")};
    TrackRecord second;
    second.trackId = QStringLiteral("two");
    second.path = QStringLiteral("C:/music/two.wav");
    second.tags = {QStringLiteral("focus"), QStringLiteral("Road")};
    LibraryModel model;
    model.replaceAll({first, second});
    QSignalSpy changes(&model, &LibraryModel::tagsChanged);
    QSignalSpy flushes(&model, &LibraryModel::flushRequested);
    QSignalSpy resets(&model, &QAbstractItemModel::modelReset);

    QCOMPARE(model.removeTagFromTracks({QStringLiteral("one"),
                                        QStringLiteral("two")},
                                       QStringLiteral(" FOCUS ")), 2);
    QCOMPARE(changes.count(), 2);
    QCOMPARE(flushes.count(), 1);
    QCOMPARE(resets.count(), 0);
    QCOMPARE(model.trackForId(QStringLiteral("one"))
                 .value(QStringLiteral("tags")).toStringList(),
             QStringList{QStringLiteral("Night")});
    QCOMPARE(model.trackForId(QStringLiteral("two"))
                 .value(QStringLiteral("tags")).toStringList(),
             QStringList{QStringLiteral("Road")});
}

void LibraryModelTest::removesTrackWithoutDeletingTheFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString firstPath = dir.filePath(QStringLiteral("first.wav"));
    QFile firstFile(firstPath);
    QVERIFY(firstFile.open(QIODevice::WriteOnly));
    QCOMPARE(firstFile.write("audio"), 5);
    firstFile.close();

    TrackRecord first;
    first.trackId = QStringLiteral("track-a");
    first.path = firstPath;
    first.favorite = true;
    first.playCount = 1;
    TrackRecord second;
    second.trackId = QStringLiteral("track-b");
    second.path = dir.filePath(QStringLiteral("second.wav"));

    LibraryModel model;
    model.replaceAll({first, second});
    QSignalSpy removed(&model, &LibraryModel::trackRemoved);
    QSignalSpy flushed(&model, &LibraryModel::flushRequested);

    QCOMPARE(model.containingFolderUrl(first.trackId),
             QUrl::fromLocalFile(dir.path()));
    QVERIFY(model.removeTrack(first.trackId));
    QVERIFY(QFileInfo::exists(firstPath));
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.indexForTrackId(second.trackId), 0);
    QCOMPARE(model.favoriteCount(), 0);
    QCOMPARE(model.historyCount(), 0);
    QCOMPARE(removed.count(), 1);
    QCOMPARE(removed.front().front().toString(), first.trackId);
    QCOMPARE(flushed.count(), 1);
    QVERIFY(!model.removeTrack(QStringLiteral("missing")));
}

QTEST_GUILESS_MAIN(LibraryModelTest)
#include "library_model_test.moc"
