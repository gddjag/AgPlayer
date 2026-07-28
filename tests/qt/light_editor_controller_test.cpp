#include "light_editor_controller.hpp"

#include "../core/bpm_fixture.hpp"

#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QVariantMap>

#include <cmath>

class LightEditorControllerTest final : public QObject {
    Q_OBJECT

private slots:
    void exposesSixTracks();
    void movesSnapsLocksAndRestoresClips();
    void trimsToSafeSourceBounds();
    void validatesBpmAndTrackSwitches();
    void analyzesAndUnifiesBpm();
    void exportsTimelineAndFiltersTracks();
    void exportsAllSupportedFormats();
    void reportsTrackAnalysisErrors();
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

void LightEditorControllerTest::analyzesAndUnifiesBpm()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = temp.filePath(QStringLiteral("bpm-90.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(path, 90, 10));

    LightEditor editor;
    editor.loadFileToTrack(0, QUrl::fromLocalFile(path));
    editor.setSnapEnabled(false);
    QVERIFY(editor.moveClip(0, 740));

    editor.analyzeTrackBpm(0);
    QTRY_VERIFY_WITH_TIMEOUT(!editor.busy(), 30000);
    QVariantMap track = editor.tracks().at(0).toMap();
    QVERIFY(std::abs(track.value(QStringLiteral("originalBpm")).toDouble()
                     - 90.0) < 1.0);
    QVERIFY(track.value(QStringLiteral("bpmConfidence")).toDouble() > 80.0);

    editor.setTargetBpm(120.0);
    editor.setKeepPitch(true);
    editor.unifyBpm(true);
    QTRY_VERIFY_WITH_TIMEOUT(!editor.busy(), 30000);

    track = editor.tracks().at(0).toMap();
    QVERIFY(std::abs(track.value(QStringLiteral("speedRatio")).toDouble()
                     - (120.0 / 90.0)) < 0.02);
    QVERIFY(track.value(QStringLiteral("aligned")).toBool());
    QCOMPARE(track.value(QStringLiteral("timelineStartMs")).toLongLong(), 500);
    const QString renderPath =
        track.value(QStringLiteral("renderPath")).toString();
    QVERIFY(!renderPath.isEmpty());
    QVERIFY(QFileInfo::exists(renderPath));
    QFile::remove(renderPath);
}

void LightEditorControllerTest::exportsTimelineAndFiltersTracks()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = temp.filePath(QStringLiteral("source.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(path, 120, 2));

    LightEditor editor;
    editor.setSnapEnabled(false);
    for (int index = 0; index < 3; ++index) {
        editor.loadFileToTrack(index, QUrl::fromLocalFile(path));
    }
    QVERIFY(editor.moveClip(1, 1000));
    QVERIFY(editor.moveClip(2, 500));
    editor.setTrackMuted(1, true);
    editor.setTrackSolo(2, true);

    QSignalSpy completed(&editor, &LightEditor::lightEditCompleted);
    editor.exportProject(temp.path(), QStringLiteral("wav"), 44100, 2);
    QVERIFY(completed.wait(30000));
    QCOMPARE(completed.count(), 1);

    const QString outputPath = completed.first().first().toString();
    ag_metadata* metadata = nullptr;
    QCOMPARE(ag_metadata_open(outputPath.toUtf8().constData(), &metadata), AG_OK);
    QVERIFY(metadata != nullptr);
    const qint64 duration = ag_metadata_duration_ms(metadata);
    QCOMPARE(ag_metadata_sample_rate(metadata), 44100);
    QCOMPARE(ag_metadata_channels(metadata), 2);
    ag_metadata_destroy(metadata);
    QVERIFY(duration >= 2400);
    QVERIFY(duration < 2800);
}

void LightEditorControllerTest::exportsAllSupportedFormats()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = temp.filePath(QStringLiteral("source.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(path, 120, 2));

    for (const QString& format : {QStringLiteral("wav"), QStringLiteral("mp3"),
                                 QStringLiteral("flac")}) {
        LightEditor editor;
        editor.loadFileToTrack(0, QUrl::fromLocalFile(path));

        QSignalSpy completed(&editor, &LightEditor::lightEditCompleted);
        editor.exportProject(temp.path(), format, 16000, 1);
        QVERIFY2(completed.wait(30000), qPrintable(format));

        const QString outputPath = completed.first().first().toString();
        QVERIFY2(QFileInfo::exists(outputPath), qPrintable(outputPath));
        ag_metadata* metadata = nullptr;
        QCOMPARE(ag_metadata_open(outputPath.toUtf8().constData(), &metadata), AG_OK);
        QVERIFY(metadata != nullptr);
        QCOMPARE(ag_metadata_sample_rate(metadata), 16000);
        QCOMPARE(ag_metadata_channels(metadata), 1);
        QVERIFY(ag_metadata_duration_ms(metadata) >= 1900);
        ag_metadata_destroy(metadata);

        ag_player_config config{};
        config.backend = AG_AUDIO_BACKEND_NULL;
        config.buffer_frames = 4096;
        ag_player* player = nullptr;
        QCOMPARE(ag_player_create_with_config(&config, &player), AG_OK);
        QVERIFY(player != nullptr);
        QCOMPARE(ag_player_load(player, outputPath.toUtf8().constData()), AG_OK);
        QCOMPARE(ag_player_play(player), AG_OK);
        QTest::qWait(50);
        ag_playback_snapshot snapshot{};
        QCOMPARE(ag_player_snapshot(player, &snapshot), AG_OK);
        QCOMPARE(snapshot.state, AG_PLAYING);
        QVERIFY(snapshot.position_ms > 0);
        ag_player_destroy(player);
    }
}

void LightEditorControllerTest::reportsTrackAnalysisErrors()
{
    LightEditor editor;
    QSignalSpy errorSpy(&editor, &LightEditor::trackError);
    editor.analyzeTrackBpm(5);
    QCOMPARE(errorSpy.count(), 1);
    QCOMPARE(errorSpy.first().first().toInt(), 5);
}

QTEST_MAIN(LightEditorControllerTest)
#include "light_editor_controller_test.moc"
