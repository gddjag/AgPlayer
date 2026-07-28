#include "light_editor_controller.hpp"

#include "../core/bpm_fixture.hpp"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QVariantMap>

class LightEditorControllerTest final : public QObject {
    Q_OBJECT

private slots:
    void exposesSixTracks();
    void movesSnapsLocksAndRestoresClips();
    void trimsToSafeSourceBounds();
    void validatesBpmAndTrackSwitches();
};

void LightEditorControllerTest::exposesSixTracks()
{
    LightEditor editor;
    QCOMPARE(editor.trackCount(), 6);
    QCOMPARE(editor.tracks().size(), 6);
    const QVariantMap emptyTrack = editor.tracks().first().toMap();
    QVERIFY(emptyTrack.contains(QStringLiteral("timelineStartMs")));
    QVERIFY(emptyTrack.contains(QStringLiteral("locked")));
    QVERIFY(emptyTrack.contains(QStringLiteral("peaks")));
}

void LightEditorControllerTest::movesSnapsLocksAndRestoresClips()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = temp.filePath(QStringLiteral("track.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(path, 120, 2));

    LightEditor editor;
    editor.loadFileToTrack(0, QUrl::fromLocalFile(path));
    editor.setTargetBpm(120.0);
    editor.setSnapEnabled(true);

    QSignalSpy tracksSpy(&editor, &LightEditor::tracksChanged);
    QVERIFY(editor.moveClip(0, 740));
    QCOMPARE(tracksSpy.count(), 1);
    QCOMPARE(editor.tracks().at(0).toMap().value(QStringLiteral("timelineStartMs")).toLongLong(),
             500);

    editor.setTrackLocked(0, true);
    tracksSpy.clear();
    QVERIFY(!editor.moveClip(0, 1000));
    QCOMPARE(tracksSpy.count(), 0);
    QCOMPARE(editor.tracks().at(0).toMap().value(QStringLiteral("timelineStartMs")).toLongLong(),
             500);

    editor.setTrackLocked(0, false);
    QVERIFY(editor.moveClip(0, 760));
    QCOMPARE(editor.tracks().at(0).toMap().value(QStringLiteral("timelineStartMs")).toLongLong(),
             1000);

    QVERIFY(editor.canUndo());
    editor.undo();
    QCOMPARE(editor.tracks().at(0).toMap().value(QStringLiteral("timelineStartMs")).toLongLong(),
             500);
    QVERIFY(editor.canRedo());
    editor.redo();
    QCOMPARE(editor.tracks().at(0).toMap().value(QStringLiteral("timelineStartMs")).toLongLong(),
             1000);
}

void LightEditorControllerTest::trimsToSafeSourceBounds()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = temp.filePath(QStringLiteral("trim.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(path, 128, 2));

    LightEditor editor;
    editor.loadFileToTrack(0, QUrl::fromLocalFile(path));
    const qint64 duration = editor.tracks().at(0).toMap()
                                .value(QStringLiteral("durationMs")).toLongLong();
    QVERIFY(duration >= 1900);

    QVERIFY(editor.trimClip(0, duration - 50, duration - 20));
    const QVariantMap track = editor.tracks().at(0).toMap();
    const qint64 inMs = track.value(QStringLiteral("inMs")).toLongLong();
    const qint64 outMs = track.value(QStringLiteral("outMs")).toLongLong();
    QVERIFY(outMs <= duration);
    QVERIFY(inMs >= 0);
    QVERIFY(outMs - inMs >= 200);
}

void LightEditorControllerTest::validatesBpmAndTrackSwitches()
{
    LightEditor editor;
    QCOMPARE(editor.targetBpm(), 128.0);
    editor.setTargetBpm(39.0);
    QCOMPARE(editor.targetBpm(), 128.0);
    editor.setTargetBpm(241.0);
    QCOMPARE(editor.targetBpm(), 128.0);
    editor.setTargetBpm(140.0);
    QCOMPARE(editor.targetBpm(), 140.0);

    editor.setTrackMuted(2, true);
    editor.setTrackSolo(2, true);
    editor.setTrackLocked(2, true);
    const QVariantMap track = editor.tracks().at(2).toMap();
    QVERIFY(track.value(QStringLiteral("muted")).toBool());
    QVERIFY(track.value(QStringLiteral("solo")).toBool());
    QVERIFY(track.value(QStringLiteral("locked")).toBool());
}

QTEST_MAIN(LightEditorControllerTest)
#include "light_editor_controller_test.moc"
