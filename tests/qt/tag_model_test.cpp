#include "library_model.hpp"
#include "tag_model.hpp"

#include <QColor>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class TagModelTest final : public QObject {
    Q_OBJECT

private slots:
    void persistsEmptyTagsAndUpdatesOnlyAffectedCounts();
    void assignsDistinctInitialPaletteColors();
    void retainsUnsavedEmptyDirectoryEntriesAcrossLibraryReset();
    void persistsMetadataDiscoveredFromTrackTags();
    void renamesAnEmptyTagWithoutLosingItsColor();
};

namespace {
TrackRecord makeTrack(const QString& id, const QStringList& tags)
{
    TrackRecord track;
    track.trackId = id;
    track.path = QStringLiteral("C:/music/") + id + QStringLiteral(".mp3");
    track.tags = tags;
    return track;
}
}

void TagModelTest::persistsEmptyTagsAndUpdatesOnlyAffectedCounts()
{
    // Catches a directory that drops an intentionally empty tag, loses an
    // explicit colour, or rebuilds all rows after one track tag change.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    LibraryModel library;
    library.replaceAll({makeTrack(QStringLiteral("a"), {QStringLiteral("Rock")}),
                        makeTrack(QStringLiteral("b"), {QStringLiteral("Jazz")})});
    const QString storagePath = dir.filePath(QStringLiteral("tags.json"));
    TagModel tags(&library, storagePath);
    QVERIFY(tags.createTag(QStringLiteral("Driving")));
    QVERIFY(tags.setTagColor(QStringLiteral("rock"), QStringLiteral("#AABBCC")));
    QSignalSpy changed(&tags, &QAbstractItemModel::dataChanged);
    QSignalSpy reset(&tags, &QAbstractItemModel::modelReset);

    QVERIFY(library.setTags(QStringLiteral("a"), {QStringLiteral("Jazz")}));
    QCOMPARE(tags.countForKey(QStringLiteral("rock")), 0);
    QCOMPARE(tags.countForKey(QStringLiteral("jazz")), 2);
    QCOMPARE(changed.count(), 2);
    QCOMPARE(reset.count(), 0);
    QVERIFY(tags.flush());

    TagModel restored(&library, storagePath);
    QCOMPARE(restored.colorForKey(QStringLiteral("rock")), QColor(QStringLiteral("#AABBCC")));
    QCOMPARE(restored.countForKey(QStringLiteral("driving")), 0);
}

void TagModelTest::assignsDistinctInitialPaletteColors()
{
    // Catches initial aggregation allocating every missing tag from an empty
    // stale palette snapshot instead of reserving colours as entries are made.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    LibraryModel library;
    library.replaceAll({makeTrack(QStringLiteral("rock"), {QStringLiteral("Rock")}),
                        makeTrack(QStringLiteral("jazz"), {QStringLiteral("Jazz")})});
    TagModel tags(&library, dir.filePath(QStringLiteral("tags.json")));

    QVERIFY(tags.colorForKey(QStringLiteral("rock")).isValid());
    QVERIFY(tags.colorForKey(QStringLiteral("jazz")).isValid());
    QVERIFY(tags.colorForKey(QStringLiteral("rock")) != tags.colorForKey(QStringLiteral("jazz")));
}

void TagModelTest::retainsUnsavedEmptyDirectoryEntriesAcrossLibraryReset()
{
    // Catches reset handling that reloads only the on-disk snapshot and drops
    // a valid zero-track directory entry before its coalesced write runs.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    LibraryModel library;
    TagModel tags(&library, dir.filePath(QStringLiteral("tags.json")));
    QVERIFY(tags.createTag(QStringLiteral("Driving")));

    library.replaceAll({makeTrack(QStringLiteral("a"), {QStringLiteral("Rock")})});

    QCOMPARE(tags.countForKey(QStringLiteral("driving")), 0);
    QCOMPARE(tags.countForKey(QStringLiteral("rock")), 1);
}

void TagModelTest::persistsMetadataDiscoveredFromTrackTags()
{
    // Catches a first-seen track tag receiving a colour in memory but never
    // being recorded in the metadata directory for a stable later restore.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString storagePath = dir.filePath(QStringLiteral("tags.json"));
    LibraryModel library;
    library.replaceAll({makeTrack(QStringLiteral("a"), {QStringLiteral("Rock")})});
    TagModel tags(&library, storagePath);

    QVERIFY(tags.flush());
    QVERIFY(QFileInfo::exists(storagePath));
}

void TagModelTest::renamesAnEmptyTagWithoutLosingItsColor()
{
    // Catches zero-track directory rename treating an unchanged LibraryModel
    // as a failed operation and discarding the directory metadata instead.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    LibraryModel library;
    TagModel tags(&library, dir.filePath(QStringLiteral("tags.json")));
    QVERIFY(tags.createTag(QStringLiteral("Road")));
    QVERIFY(tags.setTagColor(QStringLiteral("road"), QStringLiteral("#AABBCC")));

    QCOMPARE(tags.renameTag(QStringLiteral("road"), QStringLiteral("Driving")), 0);
    QCOMPARE(tags.countForKey(QStringLiteral("driving")), 0);
    QCOMPARE(tags.colorForKey(QStringLiteral("driving")), QColor(QStringLiteral("#AABBCC")));
}

QTEST_GUILESS_MAIN(TagModelTest)
#include "tag_model_test.moc"
