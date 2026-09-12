#include "library_model.hpp"
#include "tag_model.hpp"

#include <QColor>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class TagModelTest final : public QObject {
    Q_OBJECT

private slots:
    void persistsEmptyTagsAndUpdatesOnlyAffectedCounts();
    void assignsStableRandomPaletteColors();
    void retainsUnsavedEmptyDirectoryEntriesAcrossLibraryReset();
    void persistsMetadataDiscoveredFromTrackTags();
    void renamesAnEmptyTagWithoutLosingItsColor();
    void retainsDirtyEmptyTagMetadataAcrossImmediateLibraryReset();
    void doesNotResurrectDirtyDeletedTagOnLibraryReset();
    void rebuildsDirtyDirectoryCountsExactlyOnceOnLibraryReset();
    void shortWriteDoesNotReplaceValidTagFile();
    void failedFlushRetriesThenClearsObservableError();
    void failedFlushHasBoundedRetryCount();
    void synchronousFlushPersistsImmediateShutdownEdit();
    void rejectsRenameCollisionWithoutMutatingRelationsOrSelection();
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

void TagModelTest::assignsStableRandomPaletteColors()
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
    QStringList names{QStringLiteral("热"), QStringLiteral("新品"),
        QStringLiteral("高推荐"), QStringLiteral("限时折扣"),
        QStringLiteral("官方精选款"), QStringLiteral("会员专属福利"),
        QStringLiteral("平台爆款热销单品"), QStringLiteral("春季上新限定活动专区"),
        QStringLiteral("HOT"), QStringLiteral("NEW 2026年度限定好物推荐")};
    // Crossing the initial twenty swatches must allocate new stable colors,
    // not silently reuse the least-used swatch.
    for (int index = 0; index < 54; ++index)
        names.append(QStringLiteral("overflow-%1").arg(index));
    LibraryModel firstLibrary;
    TagModel firstOrder(&firstLibrary, dir.filePath(QStringLiteral("palette-first.json")));
    QHash<QString, QColor> firstColors;
    QSet<QString> usedColors;
    for (const QString& name : names) {
        QVERIFY(firstOrder.createTag(name));
        const QColor color = firstOrder.colorForKey(name);
        const QString colorName = color.name(QColor::HexRgb).toUpper();
        QVERIFY(color.isValid());
        firstColors.insert(name, color);
        usedColors.insert(colorName);
    }
    QCOMPARE(usedColors.size(), names.size());
    // Explicit user choices may deliberately share a color; allocation must
    // not recolor those tags to enforce uniqueness retroactively.
    for (const QString& name : {names.at(0), names.at(1)}) {
        QVERIFY(firstOrder.setTagColor(name, QStringLiteral("#3654FF")));
        firstColors[name] = QColor(QStringLiteral("#3654FF"));
    }
    QVERIFY(firstOrder.flush());
    TagModel restored(&firstLibrary, dir.filePath(QStringLiteral("palette-first.json")));
    for (const QString& name : names) {
        QCOMPARE(restored.colorForKey(name), firstColors.value(name));
    }
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

    QVERIFY(tags.renameTag(QStringLiteral("road"), QStringLiteral("Driving")));
    QCOMPARE(tags.countForKey(QStringLiteral("driving")), 0);
    QCOMPARE(tags.colorForKey(QStringLiteral("driving")), QColor(QStringLiteral("#AABBCC")));
}

void TagModelTest::shortWriteDoesNotReplaceValidTagFile()
{
    // Catches a QSaveFile transaction committing a truncated JSON payload
    // after the device reports a positive-but-short write.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("tags.json"));
    TagStore valid(path);
    QVERIFY(valid.save({{QStringLiteral("road"), QStringLiteral("Road"), 0,
                         QColor(QStringLiteral("#AABBCC"))}}));

    TagStore shortWriter(path, [](QIODevice& device, const QByteArray& payload) {
        return device.write(payload.constData(), qMax<qsizetype>(0, payload.size() - 1));
    });
    QVERIFY(!shortWriter.save({{QStringLiteral("night"), QStringLiteral("Night"), 0,
                                QColor(QStringLiteral("#112233"))}}));

    const QList<TagEntry> restored = valid.load();
    QCOMPARE(restored.size(), 1);
    QCOMPARE(restored.constFirst().key, QStringLiteral("road"));
    QCOMPARE(restored.constFirst().displayName, QStringLiteral("Road"));
}

void TagModelTest::failedFlushRetriesThenClearsObservableError()
{
    // Catches failed persistence being treated as clean, or retry success
    // leaving a stale user-visible error behind.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    int attempts = 0;
    LibraryModel library;
    TagModel tags(
        &library, dir.filePath(QStringLiteral("tags.json")), nullptr,
        [&attempts](QIODevice& device, const QByteArray& payload) {
            ++attempts;
            if (attempts < 3) return device.write(payload.constData(), 1);
            return device.write(payload);
        });

    QVERIFY(tags.createTag(QStringLiteral("Road")));
    QTRY_VERIFY_WITH_TIMEOUT(attempts >= 1, 500);
    QVERIFY(tags.dirty());
    QVERIFY(!tags.persistenceError().isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(attempts, 3, 1500);
    QTRY_VERIFY_WITH_TIMEOUT(!tags.dirty(), 500);
    QVERIFY(tags.persistenceError().isEmpty());
}

void TagModelTest::failedFlushHasBoundedRetryCount()
{
    // Catches an unbounded zero-delay retry loop after a persistent failure.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    int attempts = 0;
    LibraryModel library;
    TagModel tags(
        &library, dir.filePath(QStringLiteral("tags.json")), nullptr,
        [&attempts](QIODevice&, const QByteArray&) {
            ++attempts;
            return qint64{0};
        });

    QVERIFY(tags.createTag(QStringLiteral("Road")));
    QTRY_COMPARE_WITH_TIMEOUT(attempts, 4, 1500);
    QTest::qWait(350);
    QCOMPARE(attempts, 4);
    QVERIFY(tags.dirty());
    QVERIFY(!tags.persistenceError().isEmpty());
}

void TagModelTest::synchronousFlushPersistsImmediateShutdownEdit()
{
    // Catches an immediate quit losing an edit that has not reached the
    // coalescing timer yet; this is the exact method used by app shutdown.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("tags.json"));
    LibraryModel library;
    TagModel tags(&library, path);
    QVERIFY(tags.createTag(QStringLiteral("Road")));
    QVERIFY(tags.dirty());
    QVERIFY(tags.flush());

    TagStore restored(path);
    QCOMPARE(restored.load().constFirst().key, QStringLiteral("road"));
}

void TagModelTest::rejectsRenameCollisionWithoutMutatingRelationsOrSelection()
{
    // Catches implicit merge semantics when a directory rename targets an
    // already existing key.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    LibraryModel library;
    library.replaceAll({makeTrack(QStringLiteral("a"), {QStringLiteral("Rock")}),
                        makeTrack(QStringLiteral("b"), {QStringLiteral("Jazz")})});
    TagModel tags(&library, dir.filePath(QStringLiteral("tags.json")));
    QVERIFY(tags.setTagColor(QStringLiteral("rock"), QStringLiteral("#AABBCC")));
    QVERIFY(tags.setTagColor(QStringLiteral("jazz"), QStringLiteral("#112233")));
    tags.setSelectedKey(QStringLiteral("rock"));
    const QColor rockColor = tags.colorForKey(QStringLiteral("rock"));
    const QColor jazzColor = tags.colorForKey(QStringLiteral("jazz"));
    QSignalSpy relationChanges(&library, &LibraryModel::tagsChanged);
    QSignalSpy flushes(&library, &LibraryModel::flushRequested);
    QSignalSpy tagChanges(&tags, &QAbstractItemModel::dataChanged);

    QVERIFY(!tags.renameTag(QStringLiteral("rock"), QStringLiteral("Jazz")));

    QCOMPARE(library.recordForId(QStringLiteral("a"))->tags,
             QStringList{QStringLiteral("Rock")});
    QCOMPARE(library.recordForId(QStringLiteral("b"))->tags,
             QStringList{QStringLiteral("Jazz")});
    QCOMPARE(tags.colorForKey(QStringLiteral("rock")), rockColor);
    QCOMPARE(tags.colorForKey(QStringLiteral("jazz")), jazzColor);
    QCOMPARE(tags.countForKey(QStringLiteral("rock")), 1);
    QCOMPARE(tags.countForKey(QStringLiteral("jazz")), 1);
    QCOMPARE(tags.selectedKey(), QStringLiteral("rock"));
    QCOMPARE(relationChanges.count(), 0);
    QCOMPARE(flushes.count(), 0);
    QCOMPARE(tagChanges.count(), 0);
}

void TagModelTest::retainsDirtyEmptyTagMetadataAcrossImmediateLibraryReset()
{
    // Catches model-reset reload preferring a stale on-disk snapshot over
    // dirty in-memory directory metadata before its coalesced flush executes.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString storagePath = dir.filePath(QStringLiteral("tags.json"));
    LibraryModel library;
    TagModel tags(&library, storagePath);
    QVERIFY(tags.createTag(QStringLiteral("Driving")));
    QVERIFY(tags.setTagColor(QStringLiteral("driving"), QStringLiteral("#AABBCC")));

    library.replaceAll({makeTrack(QStringLiteral("a"), {QStringLiteral("Rock")})});

    QCOMPARE(tags.countForKey(QStringLiteral("driving")), 0);
    QCOMPARE(tags.colorForKey(QStringLiteral("driving")), QColor(QStringLiteral("#AABBCC")));
    QVERIFY(tags.flush());
    TagModel restored(&library, storagePath);
    QCOMPARE(restored.countForKey(QStringLiteral("driving")), 0);
    QCOMPARE(restored.colorForKey(QStringLiteral("driving")), QColor(QStringLiteral("#AABBCC")));
}

void TagModelTest::doesNotResurrectDirtyDeletedTagOnLibraryReset()
{
    // Catches a dirty-directory merge that treats stale disk metadata as
    // authoritative and brings back an intentionally removed empty tag.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString storagePath = dir.filePath(QStringLiteral("tags.json"));
    LibraryModel library;
    TagModel tags(&library, storagePath);
    QVERIFY(tags.createTag(QStringLiteral("Driving")));
    QVERIFY(tags.flush());
    QCOMPARE(tags.removeTag(QStringLiteral("driving")), 0);

    library.replaceAll({makeTrack(QStringLiteral("a"), {QStringLiteral("Rock")})});

    QCOMPARE(tags.countForKey(QStringLiteral("driving")), 0);
    QVERIFY(!tags.colorForKey(QStringLiteral("driving")).isValid());
}

void TagModelTest::rebuildsDirtyDirectoryCountsExactlyOnceOnLibraryReset()
{
    // Catches reset aggregation adding to an in-memory dirty count instead of
    // recomputing it from the LibraryModel source of truth.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    LibraryModel library;
    library.replaceAll({makeTrack(QStringLiteral("a"), {QStringLiteral("Rock")}),
                        makeTrack(QStringLiteral("b"), {QStringLiteral("Rock")})});
    TagModel tags(&library, dir.filePath(QStringLiteral("tags.json")));
    QCOMPARE(tags.countForKey(QStringLiteral("rock")), 2);

    library.replaceAll({makeTrack(QStringLiteral("a"), {QStringLiteral("Rock")}),
                        makeTrack(QStringLiteral("b"), {QStringLiteral("Rock")})});

    QCOMPARE(tags.countForKey(QStringLiteral("rock")), 2);
}

QTEST_GUILESS_MAIN(TagModelTest)
#include "tag_model_test.moc"
