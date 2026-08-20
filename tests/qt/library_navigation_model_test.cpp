#include "library_manager_controller.hpp"
#include "library_model.hpp"
#include "library_navigation_model.hpp"
#include "playlist_model.hpp"
#include "tag_model.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
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
    QSignalSpy changed(&navigation, &QAbstractItemModel::dataChanged);
    QSignalSpy reset(&navigation, &QAbstractItemModel::modelReset);

    TrackRecord inserted;
    inserted.trackId = QStringLiteral("inserted");
    inserted.path = QDir(album).filePath(QStringLiteral("inserted.mp3"));
    QVERIFY(library.append(inserted));
    QCOMPARE(navigation.lastIncrementalTrackVisits(), 1);
    QCOMPARE(changed.count(), 3); // library, root, and expanded album only
    QCOMPARE(reset.count(), 0);

    changed.clear();
    QVERIFY(library.setTags(QStringLiteral("track-0"), {QStringLiteral("Road")}));
    QCOMPARE(navigation.lastIncrementalTrackVisits(), 0);
    QCOMPARE(reset.count(), 0);

    changed.clear();
    QVERIFY(library.removeTrack(QStringLiteral("inserted")));
    QCOMPARE(navigation.lastIncrementalTrackVisits(), 1);
    QCOMPARE(changed.count(), 3);
    QCOMPARE(reset.count(), 0);

    const QString movedFolder = QDir(root).filePath(QStringLiteral("moved"));
    QVERIFY(QDir().mkpath(movedFolder));
    const QString movedPath = QDir(movedFolder).filePath(QStringLiteral("track-0.mp3"));
    QFile moved(movedPath);
    QVERIFY(moved.open(QIODevice::WriteOnly));
    moved.close();
    changed.clear();
    QVERIFY(library.updateTrackPath(QStringLiteral("track-0"), movedPath));
    QCOMPARE(navigation.lastIncrementalTrackVisits(), 1);
    QCOMPARE(changed.count(), 1); // the expanded album alone loses one track
    QCOMPARE(reset.count(), 0);
}

QTEST_GUILESS_MAIN(LibraryNavigationModelTest)
#include "library_navigation_model_test.moc"
