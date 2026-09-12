#include "library_model.hpp"
#include "tag_filter_model.hpp"
#include "tag_model.hpp"

#include <QColor>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class TagFilterModelTest final : public QObject {
    Q_OBJECT

private slots:
    void filtersCaseInsensitivelyAndPropagatesIncrementalChanges();
    void sortsByTrackCountThenName();
};

void TagFilterModelTest::filtersCaseInsensitivelyAndPropagatesIncrementalChanges()
{
    // Catches filtering by copied QML rows or rebuilding/resetting the proxy
    // when one source tag is inserted, removed, renamed, or recolored.
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    LibraryModel library;
    TagModel tags(&library, directory.filePath(QStringLiteral("tags.json")));
    QVERIFY(tags.createTag(QStringLiteral("Alpha")));
    QVERIFY(tags.createTag(QStringLiteral("Beta")));

    TagFilterModel filter;
    filter.setSourceModel(&tags);
    QCOMPARE(filter.sourceModel(), &tags);
    filter.setQuery(QStringLiteral("ALP"));
    QCOMPARE(filter.rowCount(), 1);
    QCOMPARE(filter.data(filter.index(0, 0), TagModel::DisplayNameRole).toString(),
             QStringLiteral("Alpha"));

    QSignalSpy sourceReset(&tags, &QAbstractItemModel::modelReset);
    QSignalSpy proxyReset(&filter, &QAbstractItemModel::modelReset);
    QSignalSpy inserted(&filter, &QAbstractItemModel::rowsInserted);
    QSignalSpy removed(&filter, &QAbstractItemModel::rowsRemoved);
    QSignalSpy changed(&filter, &QAbstractItemModel::dataChanged);

    QVERIFY(tags.createTag(QStringLiteral("Alpine")));
    QCOMPARE(filter.rowCount(), 2);
    QCOMPARE(inserted.count(), 1);

    tags.removeTag(QStringLiteral("alpha"));
    QCOMPARE(filter.rowCount(), 1);
    QCOMPARE(removed.count(), 1);

    tags.renameTag(QStringLiteral("beta"), QStringLiteral("Alphabet"));
    QCOMPARE(filter.rowCount(), 2);
    QCOMPARE(inserted.count(), 2);

    QVERIFY(tags.setTagColor(QStringLiteral("alphabet"),
                             QStringLiteral("#AABBCC")));
    QCOMPARE(changed.count(), 1);

    tags.renameTag(QStringLiteral("alphabet"), QStringLiteral("Gamma"));
    QCOMPARE(filter.rowCount(), 1);
    QCOMPARE(removed.count(), 2);
    QCOMPARE(sourceReset.count(), 0);
    QCOMPARE(proxyReset.count(), 0);
}

void TagFilterModelTest::sortsByTrackCountThenName()
{
    // Catches the tag panel inheriting creation order instead of exposing the
    // most-used tags first, with a stable readable tie-breaker.
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    LibraryModel library;
    TagModel tags(&library, directory.filePath(QStringLiteral("tags.json")));

    TrackRecord first;
    first.trackId = QStringLiteral("first");
    first.path = directory.filePath(QStringLiteral("first.wav"));
    first.tags = {QStringLiteral("Beta"), QStringLiteral("Zulu")};
    TrackRecord second;
    second.trackId = QStringLiteral("second");
    second.path = directory.filePath(QStringLiteral("second.wav"));
    second.tags = {QStringLiteral("Alpha"), QStringLiteral("Beta")};
    TrackRecord third;
    third.trackId = QStringLiteral("third");
    third.path = directory.filePath(QStringLiteral("third.wav"));
    third.tags = {QStringLiteral("Alpha")};
    library.replaceAll({first, second, third});

    TagFilterModel filter;
    filter.setSourceModel(&tags);
    QCOMPARE(filter.rowCount(), 3);
    QStringList names;
    for (int row = 0; row < filter.rowCount(); ++row) {
        names.append(filter.data(filter.index(row, 0),
                                 TagModel::DisplayNameRole).toString());
    }
    QCOMPARE(names, QStringList({QStringLiteral("Alpha"),
                                 QStringLiteral("Beta"),
                                 QStringLiteral("Zulu")}));
}

QTEST_GUILESS_MAIN(TagFilterModelTest)
#include "tag_filter_model_test.moc"
