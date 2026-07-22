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
    track.available = true;
    model.append(track);

    QCOMPARE(model.rowCount(), 1);
    const QModelIndex index = model.index(0);
    QCOMPARE(model.data(index, LibraryModel::TrackIdRole).toString(), track.trackId);
    QCOMPARE(model.data(index, LibraryModel::PathRole).toString(), track.path);
    QCOMPARE(model.data(index, LibraryModel::TitleRole).toString(), track.title);
    QCOMPARE(model.data(index, LibraryModel::CoverUrlRole).toUrl(), track.coverUrl);
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
    QCOMPARE(roles.value(LibraryModel::AvailableRole), QByteArray("available"));
    QCOMPARE(roles.value(LibraryModel::ImportErrorRole), QByteArray("importError"));

    QSignalSpy changed(&model, &LibraryModel::dataChanged);
    QVERIFY(model.setFavorite(0, true));
    QVERIFY(model.tracks().front().favorite);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(changed.front().at(2).value<QList<int>>(), QList<int>{LibraryModel::FavoriteRole});
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

QTEST_GUILESS_MAIN(LibraryModelTest)
#include "library_model_test.moc"
