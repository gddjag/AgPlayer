#include "library_model.hpp"
#include "playback_controller.hpp"
#include "window_controller.hpp"
#include "../../core/src/audio_editor/audio_file_analyzer.hpp"
#include "../../core/src/audio_editor/editor_playback_stream.hpp"
#include "../../core/src/audio_editor/editor_player_bridge.hpp"

#include <agplayer/c_api.h>

#include <QQmlComponent>
#include <QQmlEngine>
#include <QtQml/qqml.h>
#include <QFile>
#include <QScopedPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <cstdio>

class PlaybackControllerTest final : public QObject {
    Q_OBJECT

private slots:
    void commandsReflectCoreSnapshotsAndCommittedSeekImmediately();
    void seekImmediatelyAfterPauseKeepsCommittedPosition();
    void outputDevicesAreEnumeratedAndApplied();
    void snapshotSignalsEmitOnlyForChangesAtBoundedFrequency();
    void liveSpectrumIsPublishedAtBoundedFrequency();
    void unavailableRowsAreExcludedFromQueueIndices();
    void queuesSelectedTrackNextWithoutRestartingPlayback();
    void restoresSavedQueueOrderAndFiltersUnavailableTracks();
    void startsPlaybackFromVisibleListScope();
    void freshCoreCanBeAcquiredForEditorOutput();
    void editorOutputRestoresExactScopedPlaybackSession();
    void exactWaveformDurationAlignsPlaybackTimeline();
    void loadsRowWithoutStartingPlayback();
    void nullCoreReportsStableErrors();
    void survivesLibraryModelDestruction();
    void libraryRequestsShareQueueAndFavoriteState();
    void playbackControllerIsAnAgPlayerQmlSingleton();
};

void PlaybackControllerTest::seekImmediatelyAfterPauseKeepsCommittedPosition()
{
    const QByteArray path = qgetenv("AGPLAYER_TEST_WAV");
    QVERIFY(!path.isEmpty());
    ag_player_config config{AG_AUDIO_BACKEND_NULL, 2048};
    ag_player* core = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &core), AG_OK);
    {
        PlaybackController controller(core);
        QCOMPARE(ag_player_load(core, path.constData()), AG_OK);
        QTRY_COMPARE(controller.durationMs(), 2'000);
        controller.play();
        QTRY_COMPARE(controller.state(), PlaybackController::Playing);
        QTest::qWait(120);

        controller.pause();
        controller.seek(1'500);

        QCOMPARE(controller.positionMs(), qint64{1'500});
        QTRY_COMPARE(controller.state(), PlaybackController::Paused);
        QTest::qWait(PlaybackController::PollIntervalMs * 3);
        ag_playback_snapshot snapshot{};
        QCOMPARE(ag_player_snapshot(core, &snapshot), AG_OK);
        QVERIFY(qAbs(snapshot.position_ms - qint64{1'500}) <= 2);
        QVERIFY(qAbs(controller.positionMs() - qint64{1'500}) <= 2);
    }
    ag_player_destroy(core);
}

void PlaybackControllerTest::outputDevicesAreEnumeratedAndApplied()
{
    const QByteArray path = qgetenv("AGPLAYER_TEST_WAV");
    QVERIFY(!path.isEmpty());
    ag_player_config config{AG_AUDIO_BACKEND_NULL, 2048};
    ag_player* core = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &core), AG_OK);
    {
        PlaybackController controller(core);
        QVERIFY(!controller.outputDevices().isEmpty());
        QCOMPARE(controller.outputDevices().size(),
                 controller.outputDeviceIds().size());
        QVERIFY(controller.setTransitionFadeMs(200));
        QVERIFY(!controller.setTransitionFadeMs(100));
        QVERIFY(controller.setMatchTrackSampleRate(true));
        QVERIFY(controller.setMatchTrackSampleRate(false));
        QVERIFY(controller.setReplayGainSettings(0, true));
        QVERIFY(!controller.setReplayGainSettings(3, true));
        QCOMPARE(ag_player_load(core, path.constData()), AG_OK);
        controller.play();
        QTRY_COMPARE(controller.state(), PlaybackController::Playing);
        QVERIFY(controller.setOutputDevice(
            controller.outputDeviceIds().first(), false));
        QTRY_COMPARE(controller.state(), PlaybackController::Playing);
        QVERIFY(controller.setOutputDevice(QString(), false));
        QTRY_COMPARE(controller.state(), PlaybackController::Playing);
        QVERIFY(!controller.setOutputDevice(
            QStringLiteral("missing-output-device"), false));
        QVERIFY(!controller.errorMessage().isEmpty());
    }
    ag_player_destroy(core);
}

void PlaybackControllerTest::commandsReflectCoreSnapshotsAndCommittedSeekImmediately()
{
    const QByteArray path = qgetenv("AGPLAYER_TEST_WAV");
    QVERIFY(!path.isEmpty());
    ag_player_config config{AG_AUDIO_BACKEND_NULL, 2048};
    ag_player* core = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &core), AG_OK);
    {
        PlaybackController controller(core);
        QSignalSpy durationChanged(&controller, &PlaybackController::durationMsChanged);
        QSignalSpy stateChanged(&controller, &PlaybackController::stateChanged);
        QSignalSpy positionChanged(&controller, &PlaybackController::positionMsChanged);

        QCOMPARE(controller.durationMs(), 0);
        QCOMPARE(ag_player_load(core, path.constData()), AG_OK);
        QTRY_COMPARE(controller.durationMs(), 2'000);
        QCOMPARE(durationChanged.count(), 1);
        ag_playback_snapshot snapshot{};
        QCOMPARE(ag_player_snapshot(core, &snapshot), AG_OK);
        QCOMPARE(snapshot.duration_ms, 2'000);
        QCOMPARE(controller.durationMs(), snapshot.duration_ms);
        QTest::qWait(PlaybackController::PollIntervalMs * 3);
        QCOMPARE(durationChanged.count(), 1);

        const auto initialState = controller.state();
        controller.play();
        QCOMPARE(controller.state(), initialState);
        QTRY_COMPARE(controller.state(), PlaybackController::Playing);
        QCOMPARE(stateChanged.count(), 1);
        QTest::qWait(PlaybackController::PollIntervalMs * 3);
        QCOMPARE(stateChanged.count(), 1);
        controller.pause();
        QCOMPARE(controller.state(), PlaybackController::Playing);
        QTRY_COMPARE(controller.state(), PlaybackController::Paused);
        QCOMPARE(stateChanged.count(), 2);
        QTest::qWait(PlaybackController::PollIntervalMs * 3);
        QCOMPARE(stateChanged.count(), 2);

        controller.togglePlayback();
        QCOMPARE(controller.state(), PlaybackController::Paused);
        QTRY_COMPARE(controller.state(), PlaybackController::Playing);
        QCOMPARE(stateChanged.count(), 3);
        QTest::qWait(PlaybackController::PollIntervalMs * 3);
        QCOMPARE(stateChanged.count(), 3);

        controller.pause();
        QCOMPARE(controller.state(), PlaybackController::Playing);
        QTRY_COMPARE(controller.state(), PlaybackController::Paused);
        QCOMPARE(stateChanged.count(), 4);
        QTest::qWait(PlaybackController::PollIntervalMs * 3);
        QCOMPARE(stateChanged.count(), 4);

        const qint64 pausedPosition = controller.positionMs();
        const qint64 laterTarget = controller.durationMs() * 3 / 4;
        const qint64 earlierTarget = controller.durationMs() / 4;
        const qint64 seekTarget = qAbs(laterTarget - pausedPosition) >= 100
                                      ? laterTarget
                                      : earlierTarget;
        QVERIFY(seekTarget > 0);
        QVERIFY(seekTarget < controller.durationMs());
        QVERIFY(qAbs(seekTarget - pausedPosition) >= 100);
        positionChanged.clear();

        controller.seek(seekTarget);
        QVERIFY(qAbs(controller.positionMs() - seekTarget) <= 2);
        QCOMPARE(positionChanged.count(), 1);
        QCOMPARE(ag_player_snapshot(core, &snapshot), AG_OK);
        QVERIFY(qAbs(snapshot.position_ms - seekTarget) <= 2);
        QCOMPARE(controller.positionMs(), snapshot.position_ms);
        const qint64 landedPosition = controller.positionMs();
        QTest::qWait(PlaybackController::PollIntervalMs * 3);
        QCOMPARE(positionChanged.count(), 1);
        QCOMPARE(controller.positionMs(), landedPosition);
        QCOMPARE(ag_player_snapshot(core, &snapshot), AG_OK);
        QCOMPARE(snapshot.position_ms, landedPosition);

        QSignalSpy volumeChanged(&controller, &PlaybackController::volumeChanged);
        controller.setVolume(0.25F);
        QCOMPARE(controller.volume(), 1.0F);
        QTRY_COMPARE(controller.volume(), 0.25F);
        QCOMPARE(volumeChanged.count(), 1);

        QSignalSpy mutedChanged(&controller, &PlaybackController::mutedChanged);
        controller.toggleMuted();
        QCOMPARE(controller.muted(), false);
        QTRY_COMPARE(controller.muted(), true);
        QCOMPARE(mutedChanged.count(), 1);

        QSignalSpy modeChanged(&controller, &PlaybackController::modeChanged);
        controller.cycleMode();
        QCOMPARE(controller.mode(), PlaybackController::Sequential);
        QTRY_COMPARE(controller.mode(), PlaybackController::RepeatOne);
        QCOMPARE(modeChanged.count(), 1);

        controller.setMode(PlaybackController::RepeatAll);
        QTRY_COMPARE(controller.mode(), PlaybackController::RepeatAll);
        QCOMPARE(modeChanged.count(), 2);
    }
    ag_player_destroy(core);
}

void PlaybackControllerTest::snapshotSignalsEmitOnlyForChangesAtBoundedFrequency()
{
    QCOMPARE(PlaybackController::PollIntervalMs, 17);
    QVERIFY(PlaybackController::IdlePollIntervalMs
            > PlaybackController::PollIntervalMs);

    ag_player_config config{AG_AUDIO_BACKEND_NULL, 2048};
    ag_player* core = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &core), AG_OK);
    {
        PlaybackController controller(core);
        QSignalSpy stateChanged(&controller, &PlaybackController::stateChanged);
        QSignalSpy positionChanged(&controller, &PlaybackController::positionMsChanged);
        QSignalSpy durationChanged(&controller, &PlaybackController::durationMsChanged);
        QSignalSpy volumeChanged(&controller, &PlaybackController::volumeChanged);
        QSignalSpy mutedChanged(&controller, &PlaybackController::mutedChanged);
        QSignalSpy modeChanged(&controller, &PlaybackController::modeChanged);
        QSignalSpy indexChanged(&controller, &PlaybackController::trackIndexChanged);
        QSignalSpy countChanged(&controller, &PlaybackController::trackCountChanged);
        QSignalSpy trackIdChanged(&controller, &PlaybackController::currentTrackIdChanged);
        QTest::qWait(PlaybackController::PollIntervalMs * 3);
        QCOMPARE(stateChanged.count(), 0);
        QCOMPARE(positionChanged.count(), 0);
        QCOMPARE(durationChanged.count(), 0);
        QCOMPARE(volumeChanged.count(), 0);
        QCOMPARE(mutedChanged.count(), 0);
        QCOMPARE(modeChanged.count(), 0);
        QCOMPARE(indexChanged.count(), 0);
        QCOMPARE(countChanged.count(), 0);
        QCOMPARE(trackIdChanged.count(), 0);

        controller.setVolume(0.5F);
        QTRY_COMPARE(controller.volume(), 0.5F);
        QCOMPARE(volumeChanged.count(), 1);
        QTest::qWait(PlaybackController::PollIntervalMs * 3);
        QCOMPARE(volumeChanged.count(), 1);
    }
    ag_player_destroy(core);
}

void PlaybackControllerTest::liveSpectrumIsPublishedAtBoundedFrequency()
{
    const QByteArray path = qgetenv("AGPLAYER_TEST_WAV");
    QVERIFY(!path.isEmpty());
    ag_player_config config{AG_AUDIO_BACKEND_NULL, 2048};
    ag_player* core = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &core), AG_OK);
    {
        PlaybackController controller(core);
        QSignalSpy spectrumChanged(&controller,
                                   &PlaybackController::spectrumChanged);
        QCOMPARE(ag_player_load(core, path.constData()), AG_OK);
        controller.play();
        const auto hasEnergy = [&controller] {
            const QVariantList values = controller.spectrum();
            return std::any_of(
                values.cbegin(), values.cend(),
                [](const QVariant& value) { return value.toFloat() > 0.05F; });
        };
        QTRY_VERIFY(hasEnergy());
        QCOMPARE(controller.spectrum().size(), 128);
        const int before = spectrumChanged.count();
        QTest::qWait(200);
        QVERIFY(spectrumChanged.count() - before <= 14);
    }
    ag_player_destroy(core);
}

void PlaybackControllerTest::unavailableRowsAreExcludedFromQueueIndices()
{
    const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_TEST_WAV"));
    QVERIFY(!fixture.isEmpty());
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString firstPath = directory.filePath(QStringLiteral("first.wav"));
    const QString thirdPath = directory.filePath(QStringLiteral("third.wav"));
    QVERIFY(QFile::copy(fixture, firstPath));
    QVERIFY(QFile::copy(fixture, thirdPath));

    LibraryModel model;
    TrackRecord first;
    first.trackId = QStringLiteral("first");
    first.path = firstPath;
    first.available = true;
    QVERIFY(model.append(first));
    TrackRecord unavailable;
    unavailable.trackId = QStringLiteral("missing");
    unavailable.path = directory.filePath(QStringLiteral("missing.wav"));
    unavailable.available = false;
    QVERIFY(model.append(unavailable));
    TrackRecord third;
    third.trackId = QStringLiteral("third");
    third.path = thirdPath;
    third.available = true;
    QVERIFY(model.append(third));

    ag_player_config config{AG_AUDIO_BACKEND_NULL, 2048};
    ag_player* core = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &core), AG_OK);
    {
        PlaybackController controller(core, &model);
        QSignalSpy indexChanged(&controller, &PlaybackController::trackIndexChanged);
        QSignalSpy countChanged(&controller, &PlaybackController::trackCountChanged);
        QSignalSpy trackIdChanged(&controller, &PlaybackController::currentTrackIdChanged);
        controller.playRow(2);
        QTRY_COMPARE(controller.trackCount(), 2);
        QTRY_COMPARE(controller.trackIndex(), 1);
        QCOMPARE(controller.currentTrackId(), QStringLiteral("third"));
        QCOMPARE(countChanged.count(), 1);
        QCOMPARE(indexChanged.count(), 1);
        QCOMPARE(trackIdChanged.count(), 1);
        QTest::qWait(PlaybackController::PollIntervalMs * 3);
        QCOMPARE(countChanged.count(), 1);
        QCOMPARE(indexChanged.count(), 1);
        QCOMPARE(trackIdChanged.count(), 1);

        controller.previous();
        QCOMPARE(controller.trackIndex(), 1);
        QTRY_COMPARE(controller.trackIndex(), 0);
        QCOMPARE(controller.currentTrackId(), QStringLiteral("first"));
        QCOMPARE(countChanged.count(), 1);
        QCOMPARE(indexChanged.count(), 2);
        QCOMPARE(trackIdChanged.count(), 2);
        QTest::qWait(PlaybackController::PollIntervalMs * 3);
        QCOMPARE(countChanged.count(), 1);
        QCOMPARE(indexChanged.count(), 2);
        QCOMPARE(trackIdChanged.count(), 2);

        controller.next();
        QCOMPARE(controller.trackIndex(), 0);
        QTRY_COMPARE(controller.trackIndex(), 1);
        QCOMPARE(controller.currentTrackId(), QStringLiteral("third"));
        QCOMPARE(countChanged.count(), 1);
        QCOMPARE(indexChanged.count(), 3);
        QCOMPARE(trackIdChanged.count(), 3);
        QTest::qWait(PlaybackController::PollIntervalMs * 3);
        QCOMPARE(countChanged.count(), 1);
        QCOMPARE(indexChanged.count(), 3);
        QCOMPARE(trackIdChanged.count(), 3);
    }
    ag_player_destroy(core);
}

void PlaybackControllerTest::exactWaveformDurationAlignsPlaybackTimeline()
{
    const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_TEST_WAV"));
    QVERIFY(!fixture.isEmpty());
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("exact-duration.wav"));
    QVERIFY(QFile::copy(fixture, path));

    LibraryModel model;
    TrackRecord track;
    track.trackId = QStringLiteral("exact-duration");
    track.path = path;
    track.available = true;
    QVERIFY(model.append(track));

    ag_player_config config{AG_AUDIO_BACKEND_NULL, 2048};
    ag_player* core = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &core), AG_OK);
    {
        PlaybackController controller(core, &model);
        controller.loadRow(0);
        QTRY_COMPARE(controller.currentTrackId(), track.trackId);
        QTRY_COMPARE(controller.durationMs(), 2'000);

        bool accepted = true;
        QVERIFY(QMetaObject::invokeMethod(
            &controller, "applyWaveformDuration", Qt::DirectConnection,
            Q_RETURN_ARG(bool, accepted), Q_ARG(QString, track.trackId),
            Q_ARG(qint64, 1'875)));
        QVERIFY(accepted);
        QTRY_COMPARE(controller.durationMs(), 1'875);
        ag_playback_snapshot snapshot{};
        QCOMPARE(ag_player_snapshot(core, &snapshot), AG_OK);
        QCOMPARE(snapshot.duration_ms, 1'875);

        // Full PCM analysis is the exact seekable timeline; container metadata
        // may include encoder padding and must not stretch the waveform.
        controller.seek(1'750);
        QCOMPARE(controller.positionMs(), 1'750);
        QTRY_VERIFY(qAbs(controller.positionMs() - 1'750) <= 2);

        accepted = true;
        QVERIFY(QMetaObject::invokeMethod(
            &controller, "applyWaveformDuration", Qt::DirectConnection,
            Q_RETURN_ARG(bool, accepted), Q_ARG(QString, QStringLiteral("other")),
            Q_ARG(qint64, 1'000)));
        QVERIFY(!accepted);
        QCOMPARE(controller.durationMs(), 1'875);
    }
    ag_player_destroy(core);
}

void PlaybackControllerTest::queuesSelectedTrackNextWithoutRestartingPlayback()
{
    const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_TEST_WAV"));
    QVERIFY(!fixture.isEmpty());
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    LibraryModel model;
    const QStringList ids{QStringLiteral("first"),
                          QStringLiteral("second"),
                          QStringLiteral("third")};
    for (const QString& id : ids) {
        const QString path = directory.filePath(id + QStringLiteral(".wav"));
        QVERIFY(QFile::copy(fixture, path));
        TrackRecord track;
        track.trackId = id;
        track.path = path;
        track.available = true;
        QVERIFY(model.append(track));
    }

    ag_player_config config{AG_AUDIO_BACKEND_NULL, 2048};
    ag_player* core = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &core), AG_OK);
    {
        PlaybackController controller(core, &model);
        controller.playRow(0);
        QTRY_COMPARE(controller.currentTrackId(), QStringLiteral("first"));
        QTRY_COMPARE(controller.state(), PlaybackController::Playing);

        QVERIFY(controller.queueNext(QStringLiteral("third")));
        QCOMPARE(controller.state(), PlaybackController::Playing);
        controller.next();
        QTRY_COMPARE(controller.currentTrackId(), QStringLiteral("third"));
        controller.next();
        QTRY_COMPARE(controller.currentTrackId(), QStringLiteral("second"));
    }
    ag_player_destroy(core);
}

void PlaybackControllerTest::restoresSavedQueueOrderAndFiltersUnavailableTracks()
{
    const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_TEST_WAV"));
    QVERIFY(!fixture.isEmpty());
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    LibraryModel model;
    for (const QString& id : {QStringLiteral("first"), QStringLiteral("second"),
                              QStringLiteral("third")}) {
        const QString path = directory.filePath(id + QStringLiteral(".wav"));
        QVERIFY(QFile::copy(fixture, path));
        TrackRecord track;
        track.trackId = id;
        track.path = path;
        track.available = id != QStringLiteral("second");
        QVERIFY(model.append(track));
    }

    ag_player_config config{AG_AUDIO_BACKEND_NULL, 2048};
    ag_player* core = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &core), AG_OK);
    {
        PlaybackController controller(core, &model);
        QVERIFY(controller.restoreQueue(
            {QStringLiteral("third"), QStringLiteral("missing"),
             QStringLiteral("second"), QStringLiteral("first")},
            QStringLiteral("third")));
        QCOMPARE(controller.queueTrackIds(),
                 QStringList({QStringLiteral("third"), QStringLiteral("first")}));
        QTRY_COMPARE(controller.currentTrackId(), QStringLiteral("third"));
        QCOMPARE(controller.trackIndex(), 0);
        QCOMPARE(controller.state(), PlaybackController::Stopped);
    }
    ag_player_destroy(core);
}

void PlaybackControllerTest::startsPlaybackFromVisibleListScope()
{
    const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_TEST_WAV"));
    QVERIFY(!fixture.isEmpty());
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    LibraryModel model;
    for (int index = 0; index < 7; ++index) {
        const QString id = QStringLiteral("track-%1").arg(index);
        const QString path = directory.filePath(id + QStringLiteral(".wav"));
        QVERIFY(QFile::copy(fixture, path));
        TrackRecord track;
        track.trackId = id;
        track.path = path;
        track.available = true;
        QVERIFY(model.append(track));
    }

    ag_player_config config{AG_AUDIO_BACKEND_NULL, 2048};
    ag_player* core = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &core), AG_OK);
    {
        PlaybackController controller(core, &model);
        QVERIFY(controller.playTrackIds(
            {QStringLiteral("track-1"), QStringLiteral("track-2"),
             QStringLiteral("track-3"), QStringLiteral("track-4"),
             QStringLiteral("track-5")},
            QStringLiteral("track-3")));
        QCOMPARE(controller.queueTrackIds(),
                 QStringList({QStringLiteral("track-3"),
                              QStringLiteral("track-4"),
                              QStringLiteral("track-5"),
                              QStringLiteral("track-1"),
                              QStringLiteral("track-2")}));
        QTRY_COMPARE(controller.currentTrackId(), QStringLiteral("track-3"));

        QVERIFY(controller.playTrackIds(
            {QStringLiteral("track-2"), QStringLiteral("track-3"),
             QStringLiteral("track-4"), QStringLiteral("missing")},
            QStringLiteral("track-3")));
        QCOMPARE(controller.queueTrackIds(),
                 QStringList({QStringLiteral("track-3"),
                              QStringLiteral("track-4"),
                              QStringLiteral("track-2"),
                              QStringLiteral("track-0"),
                              QStringLiteral("track-1"),
                              QStringLiteral("track-5"),
                              QStringLiteral("track-6")}));
    }
    ag_player_destroy(core);
}

void PlaybackControllerTest::editorOutputRestoresExactScopedPlaybackSession()
{
    const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_TEST_WAV"));
    QVERIFY(!fixture.isEmpty());
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    LibraryModel model;
    for (int index = 0; index < 7; ++index) {
        TrackRecord track;
        track.trackId = QStringLiteral("session-%1").arg(index);
        track.path = directory.filePath(track.trackId + QStringLiteral(".wav"));
        track.available = true;
        QVERIFY(QFile::copy(fixture, track.path));
        QVERIFY(model.append(track));
    }

    ag_player_config config{AG_AUDIO_BACKEND_NULL, 2048};
    ag_player* core = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &core), AG_OK);
    {
        PlaybackController controller(core, &model);
        const QStringList scoped{
            QStringLiteral("session-1"), QStringLiteral("session-2"),
            QStringLiteral("session-3"), QStringLiteral("session-4"),
            QStringLiteral("session-5")};
        QVERIFY(controller.playTrackIds(scoped, QStringLiteral("session-3")));
        controller.setMode(PlaybackController::RepeatAll);
        controller.seek(750);
        controller.pause();
        QTRY_COMPARE(controller.state(), PlaybackController::Paused);
        QTRY_VERIFY(qAbs(controller.positionMs() - 750) <= 2);
        const QStringList expectedQueue = controller.queueTrackIds();
        const QString expectedTrack = controller.currentTrackId();

        QVERIFY(controller.acquireEditorOutput());
        const auto analysis = agplayer::editor::AudioFileAnalyzer::analyze(
            std::filesystem::path(fixture.toStdWString()), 64U);
        QVERIFY(analysis.success);
        agplayer::editor::EditorPlaybackParameters parameters;
        std::string streamError;
        auto editorStream = agplayer::editor::EditorPlaybackStream::create(
            agplayer::editor::AudioDocument::fromSource(
                analysis.source).timelineSnapshot(), parameters, streamError);
        QVERIFY2(editorStream != nullptr, streamError.c_str());
        QCOMPARE(agplayer::editor::load_editor_playback_stream(
                     core, std::move(editorStream)), AG_OK);
        QCOMPARE(ag_player_play(core), AG_OK);
        QCOMPARE(ag_player_stop(core), AG_OK);
        QVERIFY(controller.acquireEditorOutput());
        controller.releaseEditorOutput();

        QTRY_COMPARE(controller.queueTrackIds(), expectedQueue);
        QTRY_COMPARE(controller.currentTrackId(), expectedTrack);
        QTRY_COMPARE(controller.mode(), PlaybackController::RepeatAll);
        QTRY_COMPARE(controller.state(), PlaybackController::Paused);
        QTRY_VERIFY(qAbs(controller.positionMs() - 750) <= 2);
        QCOMPARE(controller.trackCount(), qint64{5});
    }
    ag_player_destroy(core);
}

void PlaybackControllerTest::freshCoreCanBeAcquiredForEditorOutput()
{
    ag_player_config config{AG_AUDIO_BACKEND_NULL, 2048};
    ag_player* core = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &core), AG_OK);
    {
        PlaybackController controller(core);
        QVERIFY(controller.acquireEditorOutput());
        QVERIFY(controller.acquireEditorOutput());
        controller.releaseEditorOutput();
        QVERIFY(controller.acquireEditorOutput());
        controller.releaseEditorOutput();
    }
    ag_player_destroy(core);
}

void PlaybackControllerTest::loadsRowWithoutStartingPlayback()
{
    const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_TEST_WAV"));
    QVERIFY(!fixture.isEmpty());
    LibraryModel model;
    TrackRecord track;
    track.trackId = QStringLiteral("restore-track");
    track.path = fixture;
    track.available = true;
    QVERIFY(model.append(track));

    ag_player_config config{AG_AUDIO_BACKEND_NULL, 2048};
    ag_player* core = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &core), AG_OK);
    {
        PlaybackController controller(core, &model);
        controller.loadRow(0);
        QTRY_COMPARE(controller.trackIndex(), 0);
        QTRY_COMPARE(controller.currentTrackId(), QStringLiteral("restore-track"));
        QCOMPARE(controller.state(), PlaybackController::Stopped);

        controller.seek(750);
        QTRY_VERIFY(qAbs(controller.positionMs() - 750) <= 2);
        QCOMPARE(controller.state(), PlaybackController::Stopped);

        QTRY_VERIFY(controller.durationMs() > 0);
        controller.seek(controller.durationMs() + 10'000);
        QTRY_COMPARE(controller.positionMs(), controller.durationMs());
        QCOMPARE(controller.errorMessage(), QString());
    }
    ag_player_destroy(core);
}

void PlaybackControllerTest::nullCoreReportsStableErrors()
{
    PlaybackController controller;
    QSignalSpy errorChanged(&controller, &PlaybackController::errorMessageChanged);
    controller.play();
    QCOMPARE(controller.errorMessage(), QStringLiteral("Playback core is unavailable"));
    QCOMPARE(errorChanged.count(), 1);
    controller.pause();
    controller.seek(1);
    controller.next();
    controller.previous();
    controller.setVolume(0.5F);
    controller.toggleMuted();
    controller.cycleMode();
    QCOMPARE(controller.errorMessage(), QStringLiteral("Playback core is unavailable"));
    QCOMPARE(errorChanged.count(), 1);
}

void PlaybackControllerTest::survivesLibraryModelDestruction()
{
    auto* model = new LibraryModel;
    PlaybackController controller(nullptr, model);
    delete model;
    controller.playRow(0);
    controller.toggleFavorite();
    controller.toggleFavorite(0);
    QTest::qWait(PlaybackController::PollIntervalMs * 2);
}

void PlaybackControllerTest::libraryRequestsShareQueueAndFavoriteState()
{
    const QString path = QString::fromUtf8(qgetenv("AGPLAYER_TEST_WAV"));
    QVERIFY(!path.isEmpty());
    ag_player_config config{AG_AUDIO_BACKEND_NULL, 2048};
    ag_player* core = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &core), AG_OK);

    LibraryModel model;
    TrackRecord track;
    track.trackId = QStringLiteral("fixture-track");
    track.path = path;
    track.available = true;
    QVERIFY(model.append(track));

    {
        PlaybackController controller(core, &model);
        model.playRow(0);
        QTRY_COMPARE(controller.currentTrackId(), model.tracks().front().trackId);
        QTRY_COMPARE(controller.state(), PlaybackController::Playing);
        QTRY_COMPARE(model.tracks().front().playCount, 1);
        QVERIFY(model.tracks().front().lastPlayedAtMs > 0);
        controller.toggleFavorite();
        QVERIFY(model.tracks().front().favorite);
    }
    ag_player_destroy(core);
}

void PlaybackControllerTest::playbackControllerIsAnAgPlayerQmlSingleton()
{
    PlaybackController playback;
    WindowController windows;
    qmlRegisterSingletonInstance("AgPlayer", 1, 0, "PlaybackController", &playback);
    qmlRegisterSingletonInstance("AgPlayer", 1, 0, "WindowController", &windows);
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(
        "import QtQml 2.15\n"
        "import AgPlayer 1.0\n"
        "QtObject {\n"
        "  property var first: PlaybackController\n"
        "  property var second: PlaybackController\n"
        "  property var firstWindow: WindowController\n"
        "  property var secondWindow: WindowController\n"
        "}",
        QUrl());
    QScopedPointer<QObject> object(component.create());
    QVERIFY2(object != nullptr, qPrintable(component.errorString()));
    QObject* first = object->property("first").value<QObject*>();
    QObject* second = object->property("second").value<QObject*>();
    QCOMPARE(first, &playback);
    QCOMPARE(first, second);
    QObject* firstWindow = object->property("firstWindow").value<QObject*>();
    QObject* secondWindow = object->property("secondWindow").value<QObject*>();
    QCOMPARE(firstWindow, &windows);
    QCOMPARE(firstWindow, secondWindow);
}

QTEST_GUILESS_MAIN(PlaybackControllerTest)
#include "playback_controller_test.moc"
