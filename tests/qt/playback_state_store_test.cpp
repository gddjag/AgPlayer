#include "playback_state_store.hpp"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

class PlaybackStateStoreTest final : public QObject {
    Q_OBJECT

private slots:
    void savesAndLoadsQueueAtomically();
    void marksRunningSessionAsUnclean();
    void rejectsCorruptState();
};

void PlaybackStateStoreTest::savesAndLoadsQueueAtomically()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    PlaybackStateStore store(directory.filePath(QStringLiteral("session.json")));

    PlaybackStateStore::State expected;
    expected.queueTrackIds = {QStringLiteral("one"), QStringLiteral("two")};
    expected.currentTrackId = QStringLiteral("two");
    expected.positionMs = 42'000;
    expected.mode = 3;
    expected.cleanExit = true;

    QVERIFY(store.save(expected));
    const PlaybackStateStore::State actual = store.load();
    QCOMPARE(actual.queueTrackIds, expected.queueTrackIds);
    QCOMPARE(actual.currentTrackId, expected.currentTrackId);
    QCOMPARE(actual.positionMs, expected.positionMs);
    QCOMPARE(actual.mode, expected.mode);
    QCOMPARE(actual.cleanExit, expected.cleanExit);
    QVERIFY(actual.valid);
}

void PlaybackStateStoreTest::marksRunningSessionAsUnclean()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    PlaybackStateStore store(directory.filePath(QStringLiteral("session.json")));
    PlaybackStateStore::State state;
    state.queueTrackIds = {QStringLiteral("track")};
    state.currentTrackId = QStringLiteral("track");
    state.positionMs = 7'500;
    state.cleanExit = true;
    QVERIFY(store.save(state));

    QVERIFY(store.markRunStarted());
    const auto running = store.load();
    QVERIFY(running.valid);
    QVERIFY(!running.cleanExit);
    QCOMPARE(running.queueTrackIds, state.queueTrackIds);
    QCOMPARE(running.positionMs, state.positionMs);
}

void PlaybackStateStoreTest::rejectsCorruptState()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("session.json"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("not-json"), 8);
    file.close();

    const PlaybackStateStore::State state = PlaybackStateStore(path).load();
    QVERIFY(!state.valid);
    QVERIFY(state.queueTrackIds.isEmpty());
}

QTEST_MAIN(PlaybackStateStoreTest)
#include "playback_state_store_test.moc"
