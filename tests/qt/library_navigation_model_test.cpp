#include "resource_folder_controller.hpp"
#include "library_model.hpp"
#include "library_navigation_model.hpp"
#include "playlist_model.hpp"
#include "tag_model.hpp"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class LibraryNavigationModelTest final : public QObject {
    Q_OBJECT

private slots:
    void buildsRequiredLibraryAndTopLevelHierarchy();
    void keepsLibraryExpandableWhenThereAreNoCustomPlaylists();
    void expandsOnlyTheRequestedFolderRange();
    void expandsThreeLevelsIndependentlyFromIndexedTopology();
    void addsAndRemovesTrackOnlyFolderTopology();
    void updatesOnlyAffectedDirectoryCountsForTenThousandTracks();
    void propagatesPlaylistRenameWithoutRebuildingNavigation();
};

namespace {

QString cleanIdentity(const QString& path)
{
    return QDir::fromNativeSeparators(
        QDir::cleanPath(QFileInfo(path).canonicalFilePath().isEmpty()
                            ? QFileInfo(path).absoluteFilePath()
                            : QFileInfo(path).canonicalFilePath()));
}

int rowForNode(const LibraryNavigationModel& model, const QString& nodeId)
{
    for (int row = 0; row < model.rowCount(); ++row) {
        if (model.data(model.index(row, 0),
                       LibraryNavigationModel::NodeIdRole).toString() == nodeId) {
            return row;
        }
    }
    return -1;
}

} // namespace

void LibraryNavigationModelTest::buildsRequiredLibraryAndTopLevelHierarchy()
{
    // Catches custom playlists returning to the top level, legacy history
    // filters becoming visible again, or the library node losing expansion.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    LibraryModel library;
    PlaylistModel playlists(dir.filePath(QStringLiteral("playlists.json")));
    TagModel tags(&library, dir.filePath(QStringLiteral("tags.json")));
    ResourceFolderController manager;
    manager.setStoragePath(dir.filePath(QStringLiteral("roots.json")));
    const QString firstPlaylist = playlists.createPlaylist(QStringLiteral("晨间"));
    const QString secondPlaylist = playlists.createPlaylist(QStringLiteral("夜间"));
    QVERIFY(!firstPlaylist.isEmpty());
    QVERIFY(!secondPlaylist.isEmpty());

    LibraryNavigationModel navigation(&library, &playlists, &tags, &manager);
    const QString libraryId = QStringLiteral("library:all");
    const QString firstId = QStringLiteral("playlist:") + firstPlaylist;
    const QString secondId = QStringLiteral("playlist:") + secondPlaylist;
    const int libraryRow = rowForNode(navigation, libraryId);
    QVERIFY(libraryRow >= 0);
    QCOMPARE(navigation.data(navigation.index(libraryRow, 0),
                             LibraryNavigationModel::DepthRole).toInt(), 0);
    QVERIFY(navigation.data(navigation.index(libraryRow, 0),
                            LibraryNavigationModel::HasChildrenRole).toBool());
    QVERIFY(navigation.data(navigation.index(libraryRow, 0),
                            LibraryNavigationModel::ExpandedRole).toBool());
    for (const QString& playlistId : {firstId, secondId}) {
        const int row = rowForNode(navigation, playlistId);
        QVERIFY(row >= 0);
        QCOMPARE(navigation.data(navigation.index(row, 0),
                                 LibraryNavigationModel::DepthRole).toInt(), 1);
    }

    QStringList topLevelTypes;
    for (int row = 0; row < navigation.rowCount(); ++row) {
        const QModelIndex index = navigation.index(row, 0);
        const QString type = navigation.data(
            index, LibraryNavigationModel::NodeTypeRole).toString();
        QVERIFY(type != QStringLiteral("history"));
        QVERIFY(type != QStringLiteral("recentAdded"));
        QVERIFY(type != QStringLiteral("neverPlayed"));
        if (navigation.data(index, LibraryNavigationModel::DepthRole).toInt() == 0
            && type != QStringLiteral("resourceSection")) {
            topLevelTypes.append(type);
        }
    }
    QCOMPARE(topLevelTypes,
             QStringList({QStringLiteral("library"),
                          QStringLiteral("favorites"),
                          QStringLiteral("tags")}));

    QVERIFY(navigation.setExpanded(libraryId, false));
    QCOMPARE(rowForNode(navigation, firstId), -1);
    QCOMPARE(rowForNode(navigation, secondId), -1);
    QVERIFY(rowForNode(navigation, QStringLiteral("favorites:favorites")) >= 0);
    QVERIFY(rowForNode(navigation, QStringLiteral("tags:manage")) >= 0);
    QVERIFY(navigation.setExpanded(libraryId, true));
    QVERIFY(rowForNode(navigation, firstId) >= 0);
    QVERIFY(rowForNode(navigation, secondId) >= 0);
}

void LibraryNavigationModelTest::keepsLibraryExpandableWhenThereAreNoCustomPlaylists()
{
    // Keeps the library chevron available as a stable affordance even before
    // the user creates a first custom playlist.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    LibraryModel library;
    PlaylistModel playlists(dir.filePath(QStringLiteral("playlists.json")));
    TagModel tags(&library, dir.filePath(QStringLiteral("tags.json")));
    ResourceFolderController manager;
    manager.setStoragePath(dir.filePath(QStringLiteral("roots.json")));
    LibraryNavigationModel navigation(&library, &playlists, &tags, &manager);

    const int libraryRow = rowForNode(navigation, QStringLiteral("library:all"));
    QVERIFY(libraryRow >= 0);
    const QModelIndex index = navigation.index(libraryRow, 0);
    QVERIFY(navigation.data(index, LibraryNavigationModel::HasChildrenRole).toBool());
    QVERIFY(navigation.data(index, LibraryNavigationModel::ExpandedRole).toBool());
    QVERIFY(navigation.setExpanded(QStringLiteral("library:all"), false));
    QVERIFY(!navigation.data(index, LibraryNavigationModel::ExpandedRole).toBool());
    QVERIFY(navigation.setExpanded(QStringLiteral("library:all"), true));
}

void LibraryNavigationModelTest::propagatesPlaylistRenameWithoutRebuildingNavigation()
{
    // Catches a navigation model that refreshes only the playlist count, so
    // the left navigation keeps showing a stale playlist name until reset.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    LibraryModel library;
    PlaylistModel playlists(dir.filePath(QStringLiteral("playlists.json")));
    TagModel tags(&library, dir.filePath(QStringLiteral("tags.json")));
    ResourceFolderController manager;
    manager.setStoragePath(dir.filePath(QStringLiteral("roots.json")));
    const QString playlistId = playlists.createPlaylist(QStringLiteral("Before"));
    QVERIFY(!playlistId.isEmpty());
    LibraryNavigationModel navigation(&library, &playlists, &tags, &manager);

    const QString nodeId = QStringLiteral("playlist:") + playlistId;
    const int row = rowForNode(navigation, nodeId);
    QVERIFY(row >= 0);
    QSignalSpy changed(&navigation, &QAbstractItemModel::dataChanged);
    QVERIFY(playlists.renamePlaylist(playlistId, QStringLiteral("After")));
    QCOMPARE(navigation.data(navigation.index(row, 0),
                             LibraryNavigationModel::DisplayNameRole).toString(),
             QStringLiteral("After"));
    QCOMPARE(changed.count(), 1);
    const QList<int> roles = changed.takeFirst().at(2).value<QList<int>>();
    QVERIFY(roles.contains(LibraryNavigationModel::DisplayNameRole));
}

void LibraryNavigationModelTest::expandsOnlyTheRequestedFolderRange()
{
    // Catches a collapse/expand implementation that resets every navigation
    // row or synchronously walks folders not already represented by the library.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString root = dir.filePath(QStringLiteral("music"));
    const QString child = QDir(root).filePath(QStringLiteral("album"));
    QVERIFY(QDir().mkpath(child));
    const QString audioPath = QDir(child).filePath(QStringLiteral("track.mp3"));
    QFile audio(audioPath);
    QVERIFY(audio.open(QIODevice::WriteOnly));
    audio.close();

    TrackRecord track;
    track.trackId = QStringLiteral("track");
    track.path = audioPath;
    LibraryModel library;
    library.replaceAll({track});
    PlaylistModel playlists(dir.filePath(QStringLiteral("playlists.json")));
    TagModel tags(&library, dir.filePath(QStringLiteral("tags.json")));
    ResourceFolderController manager;
    manager.setStoragePath(dir.filePath(QStringLiteral("roots.json")));
    QVERIFY(manager.addMonitoredFolder(root));
    LibraryNavigationModel navigation(&library, &playlists, &tags, &manager);
    const QString nodeId = QStringLiteral("root:")
        + QDir::fromNativeSeparators(QDir::cleanPath(QFileInfo(root).absoluteFilePath()));
    const int before = navigation.rowCount();
    QSignalSpy inserted(&navigation, &QAbstractItemModel::rowsInserted);
    QSignalSpy reset(&navigation, &QAbstractItemModel::modelReset);

    QVERIFY(navigation.setExpanded(nodeId, true));
    QCOMPARE(inserted.count(), 1);
    QVERIFY(navigation.rowCount() > before);
    QCOMPARE(reset.count(), 0);
    QVERIFY(navigation.setExpanded(nodeId, false));
    QCOMPARE(navigation.rowCount(), before);
}

void LibraryNavigationModelTest::expandsThreeLevelsIndependentlyFromIndexedTopology()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString root = dir.filePath(QStringLiteral("music"));
    const QString genre = QDir(root).filePath(QStringLiteral("genre"));
    const QString artist = QDir(genre).filePath(QStringLiteral("artist"));
    const QString album = QDir(artist).filePath(QStringLiteral("album"));
    QVERIFY(QDir().mkpath(album));
    const QString audioPath = QDir(album).filePath(QStringLiteral("song.flac"));
    QFile audio(audioPath);
    QVERIFY(audio.open(QIODevice::WriteOnly));
    audio.close();

    TrackRecord track;
    track.trackId = QStringLiteral("deep");
    track.path = audioPath;
    LibraryModel library;
    library.replaceAll({track});
    PlaylistModel playlists(dir.filePath(QStringLiteral("playlists.json")));
    TagModel tags(&library, dir.filePath(QStringLiteral("tags.json")));
    ResourceFolderController manager;
    manager.setStoragePath(dir.filePath(QStringLiteral("roots.json")));
    QVERIFY(manager.addMonitoredFolder(root));
    LibraryNavigationModel navigation(&library, &playlists, &tags, &manager);

    const QString rootId = QStringLiteral("root:") + cleanIdentity(root);
    const QString genreId = QStringLiteral("folder:") + cleanIdentity(genre);
    const QString artistId = QStringLiteral("folder:") + cleanIdentity(artist);
    const QString albumId = QStringLiteral("folder:") + cleanIdentity(album);
    QVERIFY(navigation.setExpanded(rootId, true));
    QVERIFY(rowForNode(navigation, genreId) >= 0);
    QVERIFY(navigation.setExpanded(genreId, true));
    QVERIFY(rowForNode(navigation, artistId) >= 0);
    QVERIFY(navigation.setExpanded(artistId, true));
    const int albumRow = rowForNode(navigation, albumId);
    QVERIFY(albumRow >= 0);
    QCOMPARE(navigation.data(navigation.index(albumRow),
                             LibraryNavigationModel::DepthRole).toInt(), 3);
    QCOMPARE(navigation.data(navigation.index(albumRow),
                             LibraryNavigationModel::CountRole).toInt(), 1);

    QVERIFY(navigation.setExpanded(artistId, false));
    QCOMPARE(rowForNode(navigation, albumId), -1);
    QVERIFY(rowForNode(navigation, genreId) >= 0);
    QVERIFY(navigation.setExpanded(artistId, true));
    QVERIFY(rowForNode(navigation, albumId) >= 0);
    QVERIFY(navigation.setExpanded(rootId, false));
    QVERIFY(navigation.setExpanded(rootId, true));
    QVERIFY(rowForNode(navigation, albumId) >= 0);
}

void LibraryNavigationModelTest::addsAndRemovesTrackOnlyFolderTopology()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString root = dir.filePath(QStringLiteral("music"));
    QVERIFY(QDir().mkpath(root));
    LibraryModel library;
    PlaylistModel playlists(dir.filePath(QStringLiteral("playlists.json")));
    TagModel tags(&library, dir.filePath(QStringLiteral("tags.json")));
    ResourceFolderController manager;
    manager.setStoragePath(dir.filePath(QStringLiteral("roots.json")));
    QVERIFY(manager.addMonitoredFolder(root));
    LibraryNavigationModel navigation(&library, &playlists, &tags, &manager);
    const QString rootId = QStringLiteral("root:") + cleanIdentity(root);

    TrackRecord track;
    track.trackId = QStringLiteral("new-folder-track");
    const QString branch = QDir(root).filePath(QStringLiteral("new/deep"));
    track.path = QDir(branch).filePath(QStringLiteral("missing.mp3"));
    QVERIFY(library.append(track));
    QVERIFY(navigation.setExpanded(rootId, true));
    const QString newId = QStringLiteral("folder:")
        + QDir::fromNativeSeparators(QDir::cleanPath(
            QDir(root).filePath(QStringLiteral("new"))));
    const QString deepId = QStringLiteral("folder:")
        + QDir::fromNativeSeparators(QDir::cleanPath(branch));
    const int newRow = rowForNode(navigation, newId);
    QVERIFY(newRow >= 0);
    QCOMPARE(navigation.data(navigation.index(newRow),
                             LibraryNavigationModel::CountRole).toInt(), 1);
    QVERIFY(navigation.setExpanded(newId, true));
    const int deepRow = rowForNode(navigation, deepId);
    QVERIFY(deepRow >= 0);
    QCOMPARE(navigation.data(navigation.index(deepRow),
                             LibraryNavigationModel::CountRole).toInt(), 1);

    QVERIFY(library.removeTrack(track.trackId));
    QCOMPARE(rowForNode(navigation, newId), -1);
    const int rootRow = rowForNode(navigation, rootId);
    QVERIFY(rootRow >= 0);
    QCOMPARE(navigation.data(navigation.index(rootRow),
                             LibraryNavigationModel::CountRole).toInt(), 0);
}

void LibraryNavigationModelTest::updatesOnlyAffectedDirectoryCountsForTenThousandTracks()
{
    // Catches every source change recomputing every visible folder count by
    // traversing the full library, including irrelevant TagsRole updates.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString root = dir.filePath(QStringLiteral("music"));
    const QString album = QDir(root).filePath(QStringLiteral("album"));
    QVERIFY(QDir().mkpath(album));
    QList<TrackRecord> tracks;
    tracks.reserve(10'000);
    for (int index = 0; index < 10'000; ++index) {
        TrackRecord track;
        track.trackId = QStringLiteral("track-%1").arg(index);
        track.path = QDir(album).filePath(QStringLiteral("%1.mp3").arg(index));
        tracks.append(std::move(track));
    }
    LibraryModel library;
    library.replaceAll(std::move(tracks));
    PlaylistModel playlists(dir.filePath(QStringLiteral("playlists.json")));
    TagModel tags(&library, dir.filePath(QStringLiteral("tags.json")));
    ResourceFolderController manager;
    manager.setStoragePath(dir.filePath(QStringLiteral("roots.json")));
    QVERIFY(manager.addMonitoredFolder(root));
    LibraryNavigationModel navigation(&library, &playlists, &tags, &manager);
    const QString rootId = QStringLiteral("root:")
        + QDir::fromNativeSeparators(QDir::cleanPath(QFileInfo(root).absoluteFilePath()));
    QElapsedTimer expansionTimer;
    expansionTimer.start();
    QVERIFY(navigation.setExpanded(rootId, true));
    QVERIFY2(expansionTimer.elapsed() < 50,
             "expansion must use the indexed snapshot, not rescan 10K tracks");
    const QString albumId = QStringLiteral("folder:")
        + QDir::fromNativeSeparators(QDir::cleanPath(QFileInfo(album).absoluteFilePath()));
    QVERIFY(tags.createTag(QStringLiteral("Road")));
    QSignalSpy changed(&navigation, &QAbstractItemModel::dataChanged);
    QSignalSpy reset(&navigation, &QAbstractItemModel::modelReset);
    QSignalSpy insertedRows(&navigation, &QAbstractItemModel::rowsInserted);
    QSignalSpy removedRows(&navigation, &QAbstractItemModel::rowsRemoved);
    QSignalSpy aboutToRemoveRows(&navigation, &QAbstractItemModel::rowsAboutToBeRemoved);
    const auto countFor = [&navigation](const QString& nodeId) {
        for (int row = 0; row < navigation.rowCount(); ++row) {
            const QModelIndex index = navigation.index(row, 0);
            if (navigation.data(index, LibraryNavigationModel::NodeIdRole).toString() == nodeId) {
                return navigation.data(index, LibraryNavigationModel::CountRole).toInt();
            }
        }
        return -1;
    };
    const auto changedNodes = [&navigation, &changed] {
        QSet<QString> nodeIds;
        for (const QList<QVariant>& arguments : changed) {
            const QModelIndex first = arguments.at(0).value<QModelIndex>();
            const QModelIndex last = arguments.at(1).value<QModelIndex>();
            for (int row = first.row(); row <= last.row(); ++row) {
                nodeIds.insert(navigation.data(navigation.index(row, 0),
                                               LibraryNavigationModel::NodeIdRole).toString());
            }
        }
        return nodeIds;
    };

    TrackRecord inserted;
    inserted.trackId = QStringLiteral("inserted");
    inserted.path = QDir(album).filePath(QStringLiteral("inserted.mp3"));
    QVERIFY(library.append(inserted));
    QCOMPARE(changedNodes(), QSet<QString>({QStringLiteral("library:all"), rootId, albumId}));
    QCOMPARE(countFor(QStringLiteral("library:all")), 10'001);
    QCOMPARE(countFor(rootId), 10'001);
    QCOMPARE(countFor(albumId), 10'001);
    QCOMPARE(reset.count(), 0);
    QCOMPARE(insertedRows.count(), 0);
    QCOMPARE(removedRows.count(), 0);
    QCOMPARE(aboutToRemoveRows.count(), 0);

    changed.clear();
    QVERIFY(library.setTags(QStringLiteral("track-0"), {QStringLiteral("Road")}));
    QCOMPARE(changed.count(), 0);
    QCOMPARE(reset.count(), 0);
    QCOMPARE(insertedRows.count(), 0);
    QCOMPARE(removedRows.count(), 0);
    QCOMPARE(aboutToRemoveRows.count(), 0);

    changed.clear();
    QVERIFY(library.removeTrack(QStringLiteral("inserted")));
    QCOMPARE(changedNodes(), QSet<QString>({QStringLiteral("library:all"), rootId, albumId}));
    QCOMPARE(countFor(QStringLiteral("library:all")), 10'000);
    QCOMPARE(countFor(rootId), 10'000);
    QCOMPARE(countFor(albumId), 10'000);
    QCOMPARE(reset.count(), 0);
    QCOMPARE(insertedRows.count(), 0);
    QCOMPARE(removedRows.count(), 0);
    QCOMPARE(aboutToRemoveRows.count(), 0);

    const QString movedFolder = QDir(root).filePath(QStringLiteral("moved"));
    QVERIFY(QDir().mkpath(movedFolder));
    const QString movedPath = QDir(movedFolder).filePath(QStringLiteral("track-0.mp3"));
    QFile moved(movedPath);
    QVERIFY(moved.open(QIODevice::WriteOnly));
    moved.close();
    changed.clear();
    QVERIFY(library.updateTrackPath(QStringLiteral("track-0"), movedPath));
    const QString movedId = QStringLiteral("folder:")
        + QDir::fromNativeSeparators(QDir::cleanPath(
            QFileInfo(movedFolder).absoluteFilePath()));
    QCOMPARE(changedNodes(), QSet<QString>({albumId, movedId}));
    QCOMPARE(countFor(rootId), 10'000);
    QCOMPARE(countFor(albumId), 9'999);
    QCOMPARE(countFor(movedId), 1);
    QCOMPARE(reset.count(), 0);
    QCOMPARE(insertedRows.count(), 1);
    QCOMPARE(removedRows.count(), 0);
    QCOMPARE(aboutToRemoveRows.count(), 0);
}

QTEST_GUILESS_MAIN(LibraryNavigationModelTest)
#include "library_navigation_model_test.moc"
