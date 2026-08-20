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

QTEST_GUILESS_MAIN(LibraryNavigationModelTest)
#include "library_navigation_model_test.moc"
