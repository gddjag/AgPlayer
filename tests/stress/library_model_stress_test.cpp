#include "library_filter_model.hpp"
#include "library_model.hpp"

#include <QElapsedTimer>
#include <QTest>

class LibraryModelStressTest final : public QObject {
    Q_OBJECT

private slots:
    void loadsSearchesAndSortsTenThousandTracks();
};

void LibraryModelStressTest::loadsSearchesAndSortsTenThousandTracks()
{
    constexpr int trackCount = 10000;
    QList<TrackRecord> tracks;
    tracks.reserve(trackCount);
    for (int row = 0; row < trackCount; ++row) {
        TrackRecord track;
        track.trackId = QStringLiteral("track-%1").arg(row);
        track.path = QStringLiteral("C:/stress/track-%1.flac").arg(row);
        track.title = row % 100 == 0
            ? QStringLiteral("Needle %1").arg(row)
            : QStringLiteral("Track %1").arg(row);
        track.artist = QStringLiteral("Artist %1").arg(row % 500);
        track.album = QStringLiteral("Album %1").arg(row % 100);
        track.rating = row % 6;
        track.bpm = 60.0 + (row % 101);
        track.favorite = row % 10 == 0;
        track.available = true;
        track.playCount = row % 4 == 0 ? 1 : 0;
        track.lastPlayedAtMs = track.playCount > 0 ? row : 0;
        tracks.append(std::move(track));
    }

    LibraryModel source;
    QElapsedTimer timer;
    timer.start();
    source.replaceAll(std::move(tracks));
    const qint64 loadMs = timer.elapsed();
    QCOMPARE(source.count(), trackCount);
    QVERIFY2(loadMs < 2000, qPrintable(QStringLiteral("load took %1 ms").arg(loadMs)));

    timer.restart();
    for (int row = 0; row < trackCount; ++row) {
        QCOMPARE(source.indexForTrackId(QStringLiteral("track-%1").arg(row)), row);
    }
    const qint64 lookupMs = timer.elapsed();
    QVERIFY2(lookupMs < 1000,
             qPrintable(QStringLiteral("10k lookups took %1 ms").arg(lookupMs)));

    timer.restart();
    for (int row = 0; row < trackCount; ++row) {
        QCOMPARE(source.indexForLocalFile(
                     QStringLiteral("C:/stress/track-%1.flac").arg(row)), row);
    }
    const qint64 pathLookupMs = timer.elapsed();
    QVERIFY2(pathLookupMs < 1000,
             qPrintable(QStringLiteral("10k path lookups took %1 ms")
                            .arg(pathLookupMs)));

    LibraryFilterModel filter;
    filter.setSourceModel(&source);
    filter.setMinBpm(0.0);
    filter.setMaxBpm(300.0);
    timer.restart();
    filter.setSearchText(QStringLiteral("needle"));
    const qint64 searchMs = timer.elapsed();
    QCOMPARE(filter.count(), 100);
    QVERIFY2(searchMs < 1000,
             qPrintable(QStringLiteral("search took %1 ms").arg(searchMs)));

    timer.restart();
    filter.setSearchText({});
    filter.setExactRating(4);
    filter.setMinBpm(100.0);
    filter.setMaxBpm(140.0);
    filter.setCategory(QStringLiteral("favorites"));
    const qint64 combinedMs = timer.elapsed();
    QVERIFY(filter.count() > 0);
    QVERIFY2(combinedMs < 1500,
             qPrintable(QStringLiteral("combined filter took %1 ms").arg(combinedMs)));

    timer.restart();
    filter.setExactRating(0);
    filter.setMinBpm(0.0);
    filter.setMaxBpm(300.0);
    filter.setCategory(QStringLiteral("history"));
    const qint64 historyMs = timer.elapsed();
    QCOMPARE(filter.count(), trackCount / 4);
    QCOMPARE(filter.data(filter.index(0, 0), LibraryModel::TrackIdRole).toString(),
             QStringLiteral("track-9996"));
    QVERIFY2(historyMs < 2000,
             qPrintable(QStringLiteral("history sort took %1 ms").arg(historyMs)));
}

QTEST_GUILESS_MAIN(LibraryModelStressTest)
#include "library_model_stress_test.moc"
