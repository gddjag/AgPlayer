#include "library_manager_controller.hpp"
#include "library_model.hpp"
#include "library_navigation_model.hpp"
#include "playlist_model.hpp"
#include "tag_model.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class LibraryNavigationModelTest final : public QObject {
    Q_OBJECT

private slots:
    void expandsOnlyTheRequestedFolderRange();
    void updatesOnlyAffectedDirectoryCountsForTenThousandTracks();
};

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
    LibraryManagerController manager;
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
    LibraryManagerController manager;
    manager.setStoragePath(dir.filePath(QStringLiteral("roots.json")));
    QVERIFY(manager.addMonitoredFolder(root));
    LibraryNavigationModel navigation(&library, &playlists, &tags, &manager);
    const QString rootId = QStringLiteral("root:")
        + QDir::fromNativeSeparators(QDir::cleanPath(QFileInfo(root).absoluteFilePath()));
    QVERIFY(navigation.setExpanded(rootId, true));
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
    QCOMPARE(changedNodes(), QSet<QString>({albumId}));
    QCOMPARE(countFor(rootId), 10'000);
    QCOMPARE(countFor(albumId), 9'999);
    QCOMPARE(reset.count(), 0);
    QCOMPARE(insertedRows.count(), 0);
    QCOMPARE(removedRows.count(), 0);
    QCOMPARE(aboutToRemoveRows.count(), 0);
}

QTEST_GUILESS_MAIN(LibraryNavigationModelTest)
#include "library_navigation_model_test.moc"
