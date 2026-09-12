#include "library_model.hpp"
#include "playback_controller.hpp"
#include "settings_controller.hpp"
#include "window_controller.hpp"
#include "../../core/src/audio_editor/audio_file_analyzer.hpp"
#include "../../core/src/audio_editor/editor_playback_stream.hpp"
#include "../../core/src/audio_editor/editor_player_bridge.hpp"

#include <agplayer/c_api.h>

#include <QQmlComponent>
#include <QQmlEngine>
#include <QtQml/qqml.h>
#include <QFile>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QDir>
#include <QScopedPointer>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
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
    void largeVisibleQueuePreservesOrderAndDeduplicates_data();
    void largeVisibleQueuePreservesOrderAndDeduplicates();
    void freshCoreCanBeAcquiredForEditorOutput();
    void editorOutputCanBeReacquiredAfterEmptySessionStream();
    void editorOutputRestoresExactScopedPlaybackSession();
    void editorOutputLeaseFailuresAreRetryable();
    void exactWaveformDurationAlignsPlaybackTimeline();
    void selectionCommitAdjustAndExternalSeekFollowContract();
    void exactDurationShrinkReclampsActiveSelection();
    void selectionLoopReturnsToStartAndPausePreservesSelection();
    void trackChangeClearsSelection();
    void loadsRowWithoutStartingPlayback();
    void nullCoreReportsStableErrors();
    void scratchBridgePublishesPausedStatusAndValidatesCommands();
    void survivesLibraryModelDestruction();
    void libraryRequestsShareQueueAndFavoriteState();
    void tempoAndBpmShareOneClampedRatio();
    void bpmUpdatesAndTrackChangesResetTempo();
    void beatGridStateUsesSourceFallbackAndPersistsPerTrack();
    void firstBeatAutoPositionHonorsTransport_data();
    void firstBeatAutoPositionHonorsTransport();
    void clearedAutomaticFirstBeatCueIsNotRecreated();
    void sameTrackReloadRestoresAutomaticFirstBeatCue();
    void manualBeatGridCalibrationUpdatesAutomaticCue();
    void deckStateStoreRejectsInvalidIntegerFields();
    void beatGridEditsDoNotChangeTempoMetadataOrCue();
    void cuePressReleaseAndPlayLatchFollowDeckContract();
    void cuePressIgnoresTrackSnapshotMismatchBeforePoll_data();
    void cuePressIgnoresTrackSnapshotMismatchBeforePoll();
    void clearAndJumpCueFollowTransportContract();
    void hotCuesPersistPerTrackAndKeepTransportState();
    void cuePlayFailureKeepsReleaseContract();
    void cueCancellationAndTrackChangeInvalidateHeldCue();
    void keepPitchSettingInitializesAndTracksRuntimeChanges();
    void playbackControllerIsAnAgPlayerQmlSingleton();
};

void PlaybackControllerTest::keepPitchSettingInitializesAndTracksRuntimeChanges()
{
    const QString oldOrganization = QCoreApplication::organizationName();
    const QString oldApplication = QCoreApplication::applicationName();
    QCoreApplication::setOrganizationName(QStringLiteral("AgPlayer"));
    QCoreApplication::setApplicationName(
        QStringLiteral("AgPlayer-playback-tempo-test"));
    QSettings().clear();
    {
        QSettings persistedSettings;
        persistedSettings.setValue(
            QStringLiteral("audioTools/keepPitchWhileSpeedChange"), false);
        persistedSettings.sync();
        QCOMPARE(persistedSettings.status(), QSettings::NoError);
    }

    SettingsController settings;
    QVERIFY(!settings.keepPitchWhileSpeedChange());
    ag_player_config config{AG_AUDIO_BACKEND_NULL, 2'048};
    ag_player* core = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &core), AG_OK);
    {
        PlaybackController controller(core);
        const auto applyKeepPitch = [&settings, &controller] {
            controller.setKeepPitch(
                settings.keepPitchWhileSpeedChange());
        };
        applyKeepPitch();
        const QMetaObject::Connection connection = connect(
            &settings,
            &SettingsController::keepPitchWhileSpeedChangeChanged,
            &controller, applyKeepPitch);
        QVERIFY(!controller.keepPitch());
        ag_playback_time_pitch_config coreConfig{};
        QCOMPARE(ag_player_get_time_pitch(core, &coreConfig), AG_OK);
        QVERIFY(!coreConfig.keep_pitch);

        settings.setKeepPitchWhileSpeedChange(true);
        QVERIFY(controller.keepPitch());
        QCOMPARE(ag_player_get_time_pitch(core, &coreConfig), AG_OK);
        QVERIFY(coreConfig.keep_pitch);

        controller.setPlayer(nullptr);
        settings.setKeepPitchWhileSpeedChange(false);
        QVERIFY(controller.keepPitch());
        disconnect(connection);
    }
    ag_player_destroy(core);
    settings.setKeepPitchWhileSpeedChange(true);
    QSettings().clear();
    QCoreApplication::setOrganizationName(oldOrganization);
    QCoreApplication::setApplicationName(oldApplication);
}

void PlaybackControllerTest::tempoAndBpmShareOneClampedRatio()
{
    const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_TEST_WAV"));
    QVERIFY(!fixture.isEmpty());
    LibraryModel model;
    TrackRecord known;
    known.trackId = QStringLiteral("known-bpm");
    known.path = fixture;
    known.available = true;
    known.bpm = 100.0;
    QVERIFY(model.append(known));

    ag_player_config config{AG_AUDIO_BACKEND_NULL, 2'048};
    ag_player* core = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &core), AG_OK);
    {
        PlaybackController controller(core, &model);
        controller.loadRow(0);
        QTRY_COMPARE(controller.currentTrackId(), known.trackId);
        QCOMPARE(controller.speedRatio(), 1.0);
        QCOMPARE(controller.sourceBpm(), 100.0);
        QCOMPARE(controller.targetBpm(), 100.0);
        QVERIFY(controller.keepPitch());

        controller.setSpeedRatio(1.25);
        QCOMPARE(controller.speedRatio(), 1.25);
        QCOMPARE(controller.targetBpm(), 125.0);

        controller.setTargetBpm(80.0);
        QCOMPARE(controller.speedRatio(), 0.8);
        QCOMPARE(controller.targetBpm(), 80.0);

        controller.setTargetBpm(400.0);
        QCOMPARE(controller.speedRatio(), 1.5);
        QCOMPARE(controller.targetBpm(), 150.0);

        controller.setSpeedRatio(0.1);
        QCOMPARE(controller.speedRatio(), 0.75);
        QCOMPARE(controller.targetBpm(), 75.0);
        controller.setSpeedRatio(9.0);
        QCOMPARE(controller.speedRatio(), 1.5);
        QCOMPARE(controller.targetBpm(), 150.0);

        controller.setTargetBpm(19.0);
        QCOMPARE(controller.speedRatio(), 1.5);
        controller.setTargetBpm(401.0);
        QCOMPARE(controller.speedRatio(), 1.5);

        controller.setKeepPitch(false);
        QVERIFY(!controller.keepPitch());
        controller.resetTempo();
        QCOMPARE(controller.speedRatio(), 1.0);
        QCOMPARE(controller.targetBpm(), 100.0);
    }
    ag_player_destroy(core);
}

void PlaybackControllerTest::bpmUpdatesAndTrackChangesResetTempo()
{
    const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_TEST_WAV"));
    QVERIFY(!fixture.isEmpty());
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    LibraryModel model;
    const QList<double> bpms{120.0, 0.0};
    for (int index = 0; index < bpms.size(); ++index) {
        TrackRecord track;
        track.trackId = QStringLiteral("tempo-%1").arg(index);
        track.path = directory.filePath(track.trackId + QStringLiteral(".wav"));
        QVERIFY(QFile::copy(fixture, track.path));
        track.available = true;
        track.bpm = bpms.at(index);
        QVERIFY(model.append(track));
    }

    ag_player_config config{AG_AUDIO_BACKEND_NULL, 2'048};
    ag_player* core = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &core), AG_OK);
    {
        PlaybackController controller(core, &model);
        controller.loadRow(0);
        QTRY_COMPARE(controller.currentTrackId(), QStringLiteral("tempo-0"));
        controller.setSpeedRatio(1.25);
        QCOMPARE(controller.targetBpm(), 150.0);
        QVERIFY(model.setBpm(QStringLiteral("tempo-0"), 128.0));
        QCOMPARE(controller.sourceBpm(), 128.0);
        QCOMPARE(controller.targetBpm(), 160.0);

        controller.playRow(1);
        QTRY_COMPARE(controller.currentTrackId(), QStringLiteral("tempo-1"));
        QCOMPARE(controller.speedRatio(), 1.0);
        QCOMPARE(controller.sourceBpm(), 0.0);
        QCOMPARE(controller.targetBpm(), 0.0);
        controller.setTargetBpm(120.0);
        QCOMPARE(controller.speedRatio(), 1.0);
        QCOMPARE(controller.targetBpm(), 0.0);
        controller.setSpeedRatio(1.25);
        QCOMPARE(controller.speedRatio(), 1.25);
        QCOMPARE(controller.targetBpm(), 0.0);
    }
    ag_player_destroy(core);
}

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
        QSignalSpy seekCommitted(&controller, &PlaybackController::seekCommitted);

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
        QCOMPARE(seekCommitted.count(), 1);
        QCOMPARE(seekCommitted.constFirst().constFirst().toLongLong(), seekTarget);
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

void PlaybackControllerTest::selectionCommitAdjustAndExternalSeekFollowContract()
{
    const QByteArray path = qgetenv("AGPLAYER_TEST_WAV");
    QVERIFY(!path.isEmpty());
    ag_player_config config{AG_AUDIO_BACKEND_NULL, 2048};
    ag_player* core = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &core), AG_OK);
    {
        PlaybackController controller(core);
        QCOMPARE(ag_player_load(core, path.constData()), AG_OK);
        QTRY_COMPARE(controller.durationMs(), qint64{2'000});

        controller.commitSelection(900, 850);
        QCOMPARE(controller.selectionStartMs(), qint64{850});
        QCOMPARE(controller.selectionEndMs(), qint64{950});
        QVERIFY(controller.selectionLoopEnabled());
        QVERIFY(qAbs(controller.positionMs() - qint64{850}) <= 3);
        QTRY_COMPARE(controller.state(), PlaybackController::Playing);

        controller.adjustSelection(300, 700);
        QCOMPARE(controller.selectionStartMs(), qint64{300});
        QCOMPARE(controller.selectionEndMs(), qint64{700});
        QVERIFY(controller.selectionLoopEnabled());

        controller.disableSelectionLoopAndSeek(1'200);
        QVERIFY(!controller.selectionLoopEnabled());
        QCOMPARE(controller.selectionStartMs(), qint64{300});
        QCOMPARE(controller.selectionEndMs(), qint64{700});
        QTRY_VERIFY(qAbs(controller.positionMs() - qint64{1'200}) <= 3);

        controller.clearSelection();
        QCOMPARE(controller.selectionStartMs(), qint64{0});
        QCOMPARE(controller.selectionEndMs(), qint64{0});
        QVERIFY(!controller.selectionLoopEnabled());
    }
    ag_player_destroy(core);
}

void PlaybackControllerTest::exactDurationShrinkReclampsActiveSelection()
{
    const QString path = QString::fromUtf8(qgetenv("AGPLAYER_TEST_WAV"));
    QVERIFY(!path.isEmpty());
    LibraryModel model;
    TrackRecord track;
    track.trackId = QStringLiteral("duration-shrink");
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
        QTRY_COMPARE(controller.durationMs(), qint64{2'000});

        controller.adjustSelection(1'500, 1'900);
        QVERIFY(controller.selectionLoopEnabled());
        QVERIFY(controller.applyWaveformDuration(
            controller.currentTrackId(), 1'000));

        QCOMPARE(controller.selectionStartMs(), qint64{900});
        QCOMPARE(controller.selectionEndMs(), qint64{1'000});
        QVERIFY(controller.selectionLoopEnabled());
    }
    ag_player_destroy(core);
}

void PlaybackControllerTest::selectionLoopReturnsToStartAndPausePreservesSelection()
{
    const QByteArray path = qgetenv("AGPLAYER_TEST_WAV");
    QVERIFY(!path.isEmpty());
    ag_player_config config{AG_AUDIO_BACKEND_NULL, 2048};
    ag_player* core = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &core), AG_OK);
    {
        PlaybackController controller(core);
        QCOMPARE(ag_player_load(core, path.constData()), AG_OK);
        QTRY_COMPARE(controller.durationMs(), qint64{2'000});

        controller.adjustSelection(100, 250);
        QVERIFY(controller.selectionLoopEnabled());
        controller.setSpeedRatio(1.5);
        QCOMPARE(controller.speedRatio(), 1.5);
        controller.seek(100);
        controller.play();
        QTRY_COMPARE(controller.state(), PlaybackController::Playing);
        QTest::qWait(400);
        QTRY_VERIFY(controller.positionMs() >= 100 && controller.positionMs() < 250);
        QCOMPARE(controller.speedRatio(), 1.5);

        controller.pause();
        QTRY_COMPARE(controller.state(), PlaybackController::Paused);
        QCOMPARE(controller.selectionStartMs(), qint64{100});
        QCOMPARE(controller.selectionEndMs(), qint64{250});
        QVERIFY(controller.selectionLoopEnabled());
    }
    ag_player_destroy(core);
}

void PlaybackControllerTest::trackChangeClearsSelection()
{
    const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_TEST_WAV"));
    QVERIFY(!fixture.isEmpty());
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    LibraryModel model;
    for (const QString& id : {QStringLiteral("first"), QStringLiteral("second")}) {
        TrackRecord track;
        track.trackId = id;
        track.path = directory.filePath(id + QStringLiteral(".wav"));
        track.available = true;
        QVERIFY(QFile::copy(fixture, track.path));
        QVERIFY(model.append(track));
    }

    ag_player_config config{AG_AUDIO_BACKEND_NULL, 2048};
    ag_player* core = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &core), AG_OK);
    {
        PlaybackController controller(core, &model);
        controller.playRow(0);
        QTRY_COMPARE(controller.currentTrackId(), QStringLiteral("first"));
        controller.adjustSelection(100, 300);
        QVERIFY(controller.selectionLoopEnabled());

        controller.playRow(1);
        QTRY_COMPARE(controller.currentTrackId(), QStringLiteral("second"));
        QCOMPARE(controller.selectionStartMs(), qint64{0});
        QCOMPARE(controller.selectionEndMs(), qint64{0});
        QVERIFY(!controller.selectionLoopEnabled());
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

void PlaybackControllerTest::beatGridStateUsesSourceFallbackAndPersistsPerTrack()
{
    const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_TEST_WAV"));
    QVERIFY(!fixture.isEmpty());
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const QString oldOrganization = QCoreApplication::organizationName();
    const QString oldApplication = QCoreApplication::applicationName();
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("AgPlayer"));
    QCoreApplication::setApplicationName(
        QStringLiteral("AgPlayer-playback-deck-state-test"));
    const QString statePath = QDir(QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation)).filePath(
            QStringLiteral("deck-state.json"));
    QVERIFY(QFile::remove(statePath) || !QFile::exists(statePath));

    LibraryModel model;
    TrackRecord known;
    known.trackId = QStringLiteral("grid-known-bpm");
    known.path = directory.filePath(QStringLiteral("known.wav"));
    QVERIFY(QFile::copy(fixture, known.path));
    known.available = true;
    known.bpm = 128.5;
    QVERIFY(model.append(known));
    TrackRecord unknown;
    unknown.trackId = QStringLiteral("grid-unknown-bpm");
    unknown.path = directory.filePath(QStringLiteral("unknown.wav"));
    QVERIFY(QFile::copy(fixture, unknown.path));
    unknown.available = true;
    unknown.bpm = 0.0;
    QVERIFY(model.append(unknown));

    ag_player_config config{AG_AUDIO_BACKEND_NULL, 2'048};
    ag_player* firstCore = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &firstCore), AG_OK);
    {
        PlaybackController controller(firstCore, &model);
        controller.loadRow(0);
        QTRY_COMPARE(controller.currentTrackId(), known.trackId);
        QCOMPARE(controller.cuePositionMs(), qint64{-1});
        QCOMPARE(controller.beatGridBpm(), 128.5);
        QCOMPARE(controller.beatGridOffsetMs(), qint64{0});
        QVERIFY(!controller.beatGridCalibrated());
        QVERIFY(!controller.beatGridEstimatedBpm());

        // The complete waveform carries actual beat phase; opening a track
        // must not blindly anchor its grid to file time zero.
        QVariantList gridPeaks;
        gridPeaks.fill(0.0, 8000);
        for (int beat = 0; beat < 30; ++beat)
            gridPeaks[qRound((250.0 + beat * 60000.0 / 128.5) / 2.0)] = 0.8;
        QVERIFY(QMetaObject::invokeMethod(&controller, "applyBeatGridWaveform",
            Q_ARG(QString, known.trackId), Q_ARG(double, 128.5),
            Q_ARG(qint64, 16000), Q_ARG(QVariantList, gridPeaks)));
        QVERIFY(qAbs(controller.beatGridOffsetMs() - 250) <= 3);
        QVERIFY(!controller.beatGridCalibrated());

        controller.play();
        QTRY_COMPARE(controller.state(), PlaybackController::Playing);
        controller.pause();
        QTRY_COMPARE(controller.state(), PlaybackController::Paused);
        controller.seek(420);
        controller.cuePress();
        QCOMPARE(controller.cuePositionMs(), qint64{420});
        QVERIFY(!controller.cueAuditioning());
        QTRY_COMPARE(controller.state(), PlaybackController::Paused);
        // The grid command must use the core transport position, even before
        // the next UI poll delivers it (e.g. keyboard calibration while busy).
        QCOMPARE(ag_player_seek(firstCore, 460), AG_OK);
        controller.setBeatGridFirstBeat();
        QCOMPARE(controller.beatGridOffsetMs(), qint64{460});
        QVERIFY(controller.beatGridCalibrated());
        controller.nudgeBeatGrid(-25);
        QCOMPARE(controller.beatGridOffsetMs(), qint64{435});
        controller.setBeatGridBpm(130.25);
        QCOMPARE(controller.beatGridBpm(), 130.25);
        // A late/repeated analysis cannot overwrite manual calibration.
        QVERIFY(QMetaObject::invokeMethod(&controller, "applyBeatGridWaveform",
            Q_ARG(QString, known.trackId), Q_ARG(double, 128.5),
            Q_ARG(qint64, 16000), Q_ARG(QVariantList, gridPeaks)));
        QCOMPARE(controller.beatGridOffsetMs(), qint64{435});
        QCOMPARE(controller.beatGridBpm(), 130.25);
        QVERIFY(QFile::exists(statePath));

        controller.loadRow(1);
        QTRY_COMPARE(controller.currentTrackId(), unknown.trackId);
        QCOMPARE(controller.cuePositionMs(), qint64{-1});
        QCOMPARE(controller.beatGridBpm(), 120.0);
        QCOMPARE(controller.beatGridOffsetMs(), qint64{0});
        QVERIFY(!controller.beatGridCalibrated());
        QVERIFY(controller.beatGridEstimatedBpm());
        // Responses from the previous track are ignored after a switch.
        QVERIFY(QMetaObject::invokeMethod(&controller, "applyBeatGridWaveform",
            Q_ARG(QString, known.trackId), Q_ARG(double, 128.5),
            Q_ARG(qint64, 16000), Q_ARG(QVariantList, gridPeaks)));
        QCOMPARE(controller.beatGridOffsetMs(), qint64{0});
        QCOMPARE(controller.beatGridBpm(), 120.0);
        QVERIFY(QMetaObject::invokeMethod(&controller, "applyBeatGridWaveform",
            Q_ARG(QString, unknown.trackId), Q_ARG(double, 128.5),
            Q_ARG(qint64, 16000), Q_ARG(QVariantList, gridPeaks)));
        QVERIFY(qAbs(controller.beatGridOffsetMs() - 250) <= 3);
        QVERIFY(qAbs(controller.beatGridBpm() - 128.5) < 0.02);
        // A changed metadata tempo must invalidate/recompute the previous fit.
        QVERIFY(model.setBpm(unknown.trackId, 100.0));
        QCOMPARE(controller.beatGridBpm(), 100.0);
        QCOMPARE(controller.beatGridOffsetMs(), qint64{0});
        QVERIFY(model.setBpm(unknown.trackId, 128.5));
        QVERIFY(qAbs(controller.beatGridOffsetMs() - 250) <= 3);
        const qint64 autoOffset = controller.beatGridOffsetMs();
        const double autoBpm = controller.beatGridBpm();
        controller.nudgeBeatGrid(5);
        QCOMPARE(controller.beatGridOffsetMs(), autoOffset + 5);
        QCOMPARE(controller.beatGridBpm(), autoBpm);
        QVERIFY(controller.beatGridCalibrated());
        controller.resetBeatGrid();
        QCOMPARE(controller.beatGridOffsetMs(), autoOffset);
        QVERIFY(!controller.beatGridCalibrated());
    }
    ag_player_destroy(firstCore);

    ag_player* secondCore = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &secondCore), AG_OK);
    {
        PlaybackController restored(secondCore, &model);
        restored.loadRow(0);
        QTRY_COMPARE(restored.currentTrackId(), known.trackId);
        QCOMPARE(restored.cuePositionMs(), qint64{420});
        QCOMPARE(restored.beatGridBpm(), 130.25);
        QCOMPARE(restored.beatGridOffsetMs(), qint64{435});
        QVERIFY(restored.beatGridCalibrated());
        QVERIFY(!restored.beatGridEstimatedBpm());
        // Reopening triggers another automatic result, preserving saved edits.
        QVariantList reopenedPeaks;
        reopenedPeaks.fill(0.0, 8000);
        for (int beat = 0; beat < 30; ++beat)
            reopenedPeaks[qRound((250.0 + beat * 60000.0 / 128.5) / 2.0)] = 0.8;
        QVERIFY(QMetaObject::invokeMethod(&restored, "applyBeatGridWaveform",
            Q_ARG(QString, known.trackId), Q_ARG(double, 128.5),
            Q_ARG(qint64, 16000), Q_ARG(QVariantList, reopenedPeaks)));
        QCOMPARE(restored.beatGridOffsetMs(), qint64{435});
        QCOMPARE(restored.beatGridBpm(), 130.25);
    }
    ag_player_destroy(secondCore);

    QVERIFY(QFile::remove(statePath) || !QFile::exists(statePath));
    QCoreApplication::setOrganizationName(oldOrganization);
    QCoreApplication::setApplicationName(oldApplication);
    QStandardPaths::setTestModeEnabled(false);
}

void PlaybackControllerTest::firstBeatAutoPositionHonorsTransport_data()
{
    QTest::addColumn<int>("action");
    QTest::newRow("stopped") << 0;
    QTest::newRow("normal-playing-analysis-arrives-late") << 1;
    QTest::newRow("manual-seek-even-back-to-zero") << 2;
    QTest::newRow("manual-pause") << 3;
    QTest::newRow("manual-cue") << 4;
    QTest::newRow("manual-hot-cue") << 5;
    QTest::newRow("unreliable-analysis") << 6;
    QTest::newRow("saved-manual-first-beat") << 7;
    QTest::newRow("leave-and-reenter-professional") << 8;
    QTest::newRow("cancelled-old-track-can-align-on-next-visit") << 9;
    QTest::newRow("seek-before-track-poll-cancels-new-track") << 10;
    QTest::newRow("saved-manual-cue-is-not-overwritten") << 11;
}

void PlaybackControllerTest::firstBeatAutoPositionHonorsTransport()
{
    QFETCH(int, action);
    const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_TEST_WAV"));
    QVERIFY(!fixture.isEmpty());
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString oldApplication = QCoreApplication::applicationName();
    QCoreApplication::setApplicationName("AgPlayer-auto-first-beat-test");
    const QString statePath = QDir(QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation)).filePath("deck-state.json");
    const auto restoreApplication = qScopeGuard([&] {
        QFile::remove(statePath);
        QCoreApplication::setApplicationName(oldApplication);
    });
    LibraryModel model;
    TrackRecord track;
    track.trackId = directory.path();
    track.path = fixture;
    track.available = true;
    track.bpm = 120.0;
    QVERIFY(model.append(track));
    if (action == 9 || action == 10) {
        TrackRecord other = track;
        other.trackId += "/other";
        other.path = directory.filePath("other.wav");
        QVERIFY(QFile::copy(fixture, other.path));
        QVERIFY(model.append(other));
    }
    ag_player_config config{AG_AUDIO_BACKEND_NULL, 2048};
    ag_player* core = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &core), AG_OK);
    const auto destroyCore = qScopeGuard([&] { ag_player_destroy(core); });
    PlaybackController controller(core, &model);
    QVERIFY(QMetaObject::invokeMethod(&controller, "setBeatGridAutoPositionEnabled",
        Q_ARG(bool, true)));
    controller.loadRow(0);
    QTRY_COMPARE(controller.currentTrackId(), track.trackId);
    if (action == 1 || action == 3) {
        // Core progress is not a user transport command. A slow analysis may
        // finish after playback has already advanced beyond the opening frame.
        QCOMPARE(ag_player_set_duration_ms(core, 16000), AG_OK);
        QCOMPARE(ag_player_play(core), AG_OK);
        QCOMPARE(ag_player_seek(core, 700), AG_OK);
    }
    if (action == 2) controller.seek(0);
    if (action == 3) controller.pause();
    if (action == 4) controller.cuePress();
    if (action == 5) controller.activateHotCue(0);
    if (action == 7) {
        controller.deckStates_[track.trackId].beatGridCalibrated = true;
        controller.deckStates_[track.trackId].beatGridOffsetMs = 400;
    }
    if (action == 11) {
        controller.deckStates_[track.trackId].cuePositionMs = 420;
    }
    if (action == 8) {
        QVERIFY(QMetaObject::invokeMethod(&controller, "setBeatGridAutoPositionEnabled",
            Q_ARG(bool, false)));
        QVERIFY(QMetaObject::invokeMethod(&controller, "setBeatGridAutoPositionEnabled",
            Q_ARG(bool, true)));
    }
    if (action == 9) {
        controller.seek(0);
        QCOMPARE(ag_player_next(core), AG_OK);
        controller.pollSnapshot();
        QCOMPARE(controller.currentTrackId(), track.trackId + "/other");
        QCOMPARE(ag_player_previous(core), AG_OK);
        controller.pollSnapshot();
        QCOMPARE(controller.currentTrackId(), track.trackId);
    }
    if (action == 10) {
        QCOMPARE(ag_player_next(core), AG_OK);
        QCOMPARE(controller.currentTrackId(), track.trackId);
        controller.seek(0);
        controller.pollSnapshot();
        QCOMPARE(controller.currentTrackId(), track.trackId + "/other");
    }
    QVariantList peaks;
    peaks.fill(0.0, 8000);
    if (action != 6)
        for (int beat = 0; beat < 30; ++beat) peaks[125 + beat * 250] = 0.8;
    ag_playback_snapshot before{};
    QCOMPARE(ag_player_snapshot(core, &before), AG_OK);
    QSignalSpy seeks(&controller, &PlaybackController::seekCommitted);
    if (action == 10) {
        // An analysis completion for the previous track cannot initialize the
        // new track's CUE while the controller is between asynchronous polls.
        controller.applyBeatGridWaveform(track.trackId, 120.0, 16000, peaks);
        QCOMPARE(controller.cuePositionMs(), qint64{-1});
    }
    controller.applyBeatGridWaveform(controller.currentTrackId(), 120.0, 16000, peaks);
    ag_playback_snapshot after{};
    QCOMPARE(ag_player_snapshot(core, &after), AG_OK);
    QCOMPARE(after.state, before.state);
    const bool shouldAlign = action == 0 || action == 1 || action == 7
        || action == 9 || action == 11;
    QCOMPARE(seeks.size(), shouldAlign ? 1 : 0);
    if (shouldAlign) {
        const qint64 target = action == 7 ? 400 : 250;
        QCOMPARE(seeks.front().front().toLongLong(), target);
        QVERIFY(qAbs(after.position_ms - target) < 30);
    }
    const qint64 expectedCue = action == 4 ? 0
        : action == 11 ? 420
        : action == 7 ? 400
        : action == 0 || action == 1 || action == 9 ? 250 : -1;
    QCOMPARE(controller.cuePositionMs(), expectedCue);
    // Reanalysis/metadata changes cannot re-arm an already consumed request.
    seeks.clear();
    peaks[0] = 0.4;
    controller.applyBeatGridWaveform(controller.currentTrackId(), 120.0, 16000, peaks);
    QCOMPARE(seeks.size(), 0);
    QCOMPARE(controller.cuePositionMs(), expectedCue);
}

void PlaybackControllerTest::clearedAutomaticFirstBeatCueIsNotRecreated()
{
    const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_TEST_WAV"));
    QVERIFY(!fixture.isEmpty());
    LibraryModel model;
    TrackRecord track;
    track.trackId = QStringLiteral("cleared-auto-cue");
    track.path = fixture;
    track.available = true;
    track.bpm = 120.0;
    QVERIFY(model.append(track));

    ag_player_config config{AG_AUDIO_BACKEND_NULL, 2'048};
    ag_player* core = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &core), AG_OK);
    const auto destroyCore = qScopeGuard([&] { ag_player_destroy(core); });
    PlaybackController controller(core, &model);
    controller.setBeatGridAutoPositionEnabled(true);
    controller.loadRow(0);
    QTRY_COMPARE(controller.currentTrackId(), track.trackId);

    QVariantList peaks;
    peaks.fill(0.0, 8'000);
    for (int beat = 0; beat < 30; ++beat) peaks[125 + beat * 250] = 0.8;
    controller.applyBeatGridWaveform(track.trackId, 120.0, 16'000, peaks);
    QCOMPARE(controller.cuePositionMs(), qint64{250});
    QVERIFY(!controller.deckStates_.contains(track.trackId));

    controller.clearCue();
    QCOMPARE(controller.cuePositionMs(), qint64{-1});
    QVERIFY(!controller.deckStates_.contains(track.trackId));
    peaks[0] = 0.4;
    controller.applyBeatGridWaveform(track.trackId, 120.0, 16'000, peaks);
    QCOMPARE(controller.cuePositionMs(), qint64{-1});
    controller.nudgeBeatGrid(10);
    QCOMPARE(controller.cuePositionMs(), qint64{-1});
}

void PlaybackControllerTest::sameTrackReloadRestoresAutomaticFirstBeatCue()
{
    const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_TEST_WAV"));
    QVERIFY(!fixture.isEmpty());
    LibraryModel model;
    TrackRecord track;
    track.trackId = QStringLiteral("same-track-auto-cue");
    track.path = fixture;
    track.available = true;
    track.bpm = 120.0;
    QVERIFY(model.append(track));

    ag_player_config config{AG_AUDIO_BACKEND_NULL, 2'048};
    ag_player* core = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &core), AG_OK);
    const auto destroyCore = qScopeGuard([&] { ag_player_destroy(core); });
    PlaybackController controller(core, &model);
    controller.setBeatGridAutoPositionEnabled(true);
    controller.loadRow(0);
    QTRY_COMPARE(controller.currentTrackId(), track.trackId);

    QVariantList peaks;
    peaks.fill(0.0, 8'000);
    for (int beat = 0; beat < 30; ++beat) peaks[125 + beat * 250] = 0.8;
    controller.applyBeatGridWaveform(track.trackId, 120.0, 16'000, peaks);
    QCOMPARE(controller.cuePositionMs(), qint64{250});
    controller.clearCue();
    QCOMPARE(controller.cuePositionMs(), qint64{-1});

    controller.loadRow(0);
    QCOMPARE(controller.currentTrackId(), track.trackId);
    QCOMPARE(controller.cuePositionMs(), qint64{250});
}

void PlaybackControllerTest::manualBeatGridCalibrationUpdatesAutomaticCue()
{
    const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_TEST_WAV"));
    QVERIFY(!fixture.isEmpty());
    LibraryModel model;
    TrackRecord track;
    track.trackId = QStringLiteral("calibrated-auto-cue");
    track.path = fixture;
    track.available = true;
    track.bpm = 120.0;
    QVERIFY(model.append(track));

    ag_player_config config{AG_AUDIO_BACKEND_NULL, 2'048};
    ag_player* core = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &core), AG_OK);
    const auto destroyCore = qScopeGuard([&] { ag_player_destroy(core); });
    PlaybackController controller(core, &model);
    controller.setBeatGridAutoPositionEnabled(true);
    controller.loadRow(0);
    QTRY_COMPARE(controller.currentTrackId(), track.trackId);

    QVariantList peaks;
    peaks.fill(0.0, 8'000);
    for (int beat = 0; beat < 30; ++beat) peaks[125 + beat * 250] = 0.8;
    controller.applyBeatGridWaveform(track.trackId, 120.0, 16'000, peaks);
    QCOMPARE(controller.cuePositionMs(), qint64{250});

    controller.seek(600);
    controller.setBeatGridFirstBeat();
    QCOMPARE(controller.beatGridOffsetMs(), qint64{600});
    QCOMPARE(controller.cuePositionMs(), qint64{600});
    controller.nudgeBeatGrid(-25);
    QCOMPARE(controller.beatGridOffsetMs(), qint64{575});
    QCOMPARE(controller.cuePositionMs(), qint64{575});
    controller.resetBeatGrid();
    QCOMPARE(controller.beatGridOffsetMs(), qint64{250});
    QCOMPARE(controller.cuePositionMs(), qint64{250});
}

void PlaybackControllerTest::deckStateStoreRejectsInvalidIntegerFields()
{
    const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_TEST_WAV"));
    QVERIFY(!fixture.isEmpty());
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const QString oldOrganization = QCoreApplication::organizationName();
    const QString oldApplication = QCoreApplication::applicationName();
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("AgPlayer"));
    QCoreApplication::setApplicationName(
        QStringLiteral("AgPlayer-playback-deck-state-integer-test"));
    const QString statePath = QDir(QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation)).filePath(
            QStringLiteral("deck-state.json"));
    QVERIFY(QFile::remove(statePath) || !QFile::exists(statePath));

    QFile stateStore(statePath);
    QVERIFY(QDir().mkpath(QFileInfo(statePath).absolutePath()));
    QVERIFY(stateStore.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const QByteArray payload =
        R"({"version":1,"tracks":{"invalid-upper":{"cuePositionMs":9223372036854775808,"beatGridOffsetMs":9223372036854775808,"hotCuePositions":[9223372036854775808,12.5,-4,250]},"invalid-lower":{"cuePositionMs":-4,"beatGridOffsetMs":-1e100,"hotCuePositions":[-1e100]},"valid-min":{"beatGridOffsetMs":-9223372036854775808},"valid-max":{"cuePositionMs":9223372036854775807,"beatGridOffsetMs":9223372036854775807,"hotCuePositions":[9223372036854775807]}}})";
    QCOMPARE(stateStore.write(payload), payload.size());
    stateStore.close();

    LibraryModel model;
    const QStringList trackIds{
        QStringLiteral("invalid-upper"), QStringLiteral("invalid-lower"),
        QStringLiteral("valid-min"), QStringLiteral("valid-max")};
    for (qsizetype index = 0; index < trackIds.size(); ++index) {
        TrackRecord track;
        track.trackId = trackIds.at(index);
        track.path = directory.filePath(
            QStringLiteral("deck-state-%1.wav").arg(index));
        QVERIFY(QFile::copy(fixture, track.path));
        track.available = true;
        QVERIFY(model.append(track));
    }

    ag_player_config config{AG_AUDIO_BACKEND_NULL, 2'048};
    ag_player* core = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &core), AG_OK);
    {
        PlaybackController controller(core, &model);
        controller.loadRow(0);
        QTRY_COMPARE(controller.currentTrackId(), trackIds.at(0));
        QCOMPARE(controller.cuePositionMs(), qint64{-1});
        QCOMPARE(controller.beatGridOffsetMs(), qint64{0});
        const QVariantList invalidHotCues = controller.hotCuePositions();
        QCOMPARE(invalidHotCues.at(0).toLongLong(), qint64{-1});
        QCOMPARE(invalidHotCues.at(1).toLongLong(), qint64{-1});
        QCOMPARE(invalidHotCues.at(2).toLongLong(), qint64{-1});
        QCOMPARE(invalidHotCues.at(3).toLongLong(), qint64{250});

        controller.loadRow(1);
        QTRY_COMPARE(controller.currentTrackId(), trackIds.at(1));
        QCOMPARE(controller.cuePositionMs(), qint64{-1});
        QCOMPARE(controller.beatGridOffsetMs(), qint64{0});
        QCOMPARE(controller.hotCuePositions().at(0).toLongLong(), qint64{-1});

        controller.loadRow(2);
        QTRY_COMPARE(controller.currentTrackId(), trackIds.at(2));
        QCOMPARE(controller.beatGridOffsetMs(),
                 std::numeric_limits<qint64>::min());

        controller.loadRow(3);
        QTRY_COMPARE(controller.currentTrackId(), trackIds.at(3));
        QCOMPARE(controller.cuePositionMs(),
                 std::numeric_limits<qint64>::max());
        QCOMPARE(controller.beatGridOffsetMs(),
                 std::numeric_limits<qint64>::max());
        QCOMPARE(controller.hotCuePositions().at(0).toLongLong(),
                 std::numeric_limits<qint64>::max());
    }
    ag_player_destroy(core);

    QVERIFY(QFile::remove(statePath) || !QFile::exists(statePath));
    QCoreApplication::setOrganizationName(oldOrganization);
    QCoreApplication::setApplicationName(oldApplication);
    QStandardPaths::setTestModeEnabled(false);
}

void PlaybackControllerTest::beatGridEditsDoNotChangeTempoMetadataOrCue()
{
    const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_TEST_WAV"));
    QVERIFY(!fixture.isEmpty());
    LibraryModel model;
    TrackRecord track;
    track.trackId = QStringLiteral("grid-isolation");
    track.path = fixture;
    track.available = true;
    track.bpm = 100.0;
    QVERIFY(model.append(track));

    ag_player_config config{AG_AUDIO_BACKEND_NULL, 2'048};
    ag_player* core = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &core), AG_OK);
    {
        PlaybackController controller(core, &model);
        controller.loadRow(0);
        QTRY_COMPARE(controller.currentTrackId(), track.trackId);
        controller.play();
        QTRY_COMPARE(controller.state(), PlaybackController::Playing);
        controller.pause();
        QTRY_COMPARE(controller.state(), PlaybackController::Paused);
        controller.seek(250);
        controller.cuePress();
        QCOMPARE(controller.cuePositionMs(), qint64{250});
        controller.setSpeedRatio(1.25);
        QCOMPARE(controller.speedRatio(), 1.25);

        controller.setBeatGridFirstBeat();
        controller.nudgeBeatGrid(-10);
        controller.setBeatGridBpm(140.0);
        QCOMPARE(controller.speedRatio(), 1.25);
        QCOMPARE(model.recordForId(track.trackId)->bpm, 100.0);
        QCOMPARE(controller.cuePositionMs(), qint64{250});

        controller.setBeatGridBpm(10.0);
        QCOMPARE(controller.beatGridBpm(), 140.0);
        controller.resetBeatGrid();
        QCOMPARE(controller.beatGridBpm(), 100.0);
        QCOMPARE(controller.beatGridOffsetMs(), qint64{0});
        QVERIFY(!controller.beatGridCalibrated());
        QCOMPARE(controller.speedRatio(), 1.25);
        QCOMPARE(model.recordForId(track.trackId)->bpm, 100.0);
        QCOMPARE(controller.cuePositionMs(), qint64{250});
    }
    ag_player_destroy(core);
}

void PlaybackControllerTest::cuePressReleaseAndPlayLatchFollowDeckContract()
{
    const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_TEST_WAV"));
    QVERIFY(!fixture.isEmpty());
    LibraryModel model;
    TrackRecord track;
    track.trackId = QStringLiteral("cue-contract");
    track.path = fixture;
    track.available = true;
    QVERIFY(model.append(track));

    ag_player_config config{AG_AUDIO_BACKEND_NULL, 2'048};
    ag_player* core = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &core), AG_OK);
    {
        PlaybackController controller(core, &model);
        controller.loadRow(0);
        QTRY_COMPARE(controller.currentTrackId(), track.trackId);
        controller.play();
        QTRY_COMPARE(controller.state(), PlaybackController::Playing);
        controller.pause();
        QTRY_COMPARE(controller.state(), PlaybackController::Paused);
        controller.seek(300);

        controller.cuePress();
        QCOMPARE(controller.cuePositionMs(), qint64{300});
        QVERIFY(!controller.cueAuditioning());
        QTRY_COMPARE(controller.state(), PlaybackController::Paused);

        controller.cueRelease();
        controller.cuePress();
        QVERIFY(controller.cueAuditioning());
        QTRY_COMPARE(controller.state(), PlaybackController::Playing);
        QTest::qWait(80);
        QVERIFY(controller.positionMs() > 300);
        controller.cueRelease();
        QVERIFY(!controller.cueAuditioning());
        QTRY_COMPARE(controller.state(), PlaybackController::Paused);
        QVERIFY(qAbs(controller.positionMs() - qint64{300}) <= 2);

        controller.cuePress();
        QVERIFY(controller.cueAuditioning());
        controller.play();
        QVERIFY(!controller.cueAuditioning());
        QTRY_COMPARE(controller.state(), PlaybackController::Playing);
        QTest::qWait(80);
        const qint64 latchedPosition = controller.positionMs();
        controller.cueRelease();
        QTest::qWait(PlaybackController::PollIntervalMs * 3);
        QCOMPARE(controller.state(), PlaybackController::Playing);
        QVERIFY(controller.positionMs() >= latchedPosition);

        controller.seek(900);
        controller.cuePress();
        QVERIFY(!controller.cueAuditioning());
        QTRY_COMPARE(controller.state(), PlaybackController::Paused);
        QVERIFY(qAbs(controller.positionMs() - qint64{300}) <= 2);
        controller.cueRelease();

        controller.cuePress();
        QVERIFY(controller.cueAuditioning());
        QTRY_COMPARE(controller.state(), PlaybackController::Playing);
        controller.togglePlayback();
        QVERIFY(!controller.cueAuditioning());
        QTRY_COMPARE(controller.state(), PlaybackController::Playing);
        const qint64 toggleLatchedPosition = controller.positionMs();
        controller.cueRelease();
        QTest::qWait(PlaybackController::PollIntervalMs * 3);
        QCOMPARE(controller.state(), PlaybackController::Playing);
        QVERIFY(controller.positionMs() >= toggleLatchedPosition);

        controller.seek(700);
        controller.pause();
        controller.cuePress();
        QCOMPARE(controller.cuePositionMs(), qint64{700});
        QVERIFY(!controller.cueAuditioning());
        controller.cueRelease();
        QTRY_COMPARE(controller.state(), PlaybackController::Paused);

        controller.play();
        controller.cuePress();
        QVERIFY(!controller.cueAuditioning());
        QTRY_COMPARE(controller.state(), PlaybackController::Paused);
        QVERIFY(qAbs(controller.positionMs() - qint64{700}) <= 2);
        controller.cueRelease();
    }
    ag_player_destroy(core);
}

void PlaybackControllerTest::cuePressIgnoresTrackSnapshotMismatchBeforePoll_data()
{
    QTest::addColumn<bool>("playing");
    QTest::newRow("paused") << false;
    QTest::newRow("playing") << true;
}

void PlaybackControllerTest::cuePressIgnoresTrackSnapshotMismatchBeforePoll()
{
    QFETCH(bool, playing);
    const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_TEST_WAV"));
    QVERIFY(!fixture.isEmpty());
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    LibraryModel model;
    for (int index = 0; index < 2; ++index) {
        TrackRecord track;
        track.trackId = QStringLiteral("cue-mismatch-track-%1").arg(index);
        track.path = directory.filePath(QStringLiteral("cue-mismatch-%1.wav").arg(index));
        QVERIFY(QFile::copy(fixture, track.path));
        track.available = true;
        QVERIFY(model.append(track));
    }

    ag_player_config config{AG_AUDIO_BACKEND_NULL, 2'048};
    ag_player* core = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &core), AG_OK);
    {
        PlaybackController controller(core, &model);
        controller.playRow(0);
        QTRY_COMPARE(controller.currentTrackId(),
                     QStringLiteral("cue-mismatch-track-0"));
        controller.pause();
        QTRY_COMPARE(controller.state(), PlaybackController::Paused);
        constexpr qint64 savedCueMs = 1'200;
        controller.seek(savedCueMs);
        controller.cuePress();
        controller.cueRelease();
        QCOMPARE(controller.cuePositionMs(), savedCueMs);

        if (playing) {
            controller.play();
            QTRY_COMPARE(controller.state(), PlaybackController::Playing);
        }
        controller.next();
        QCOMPARE(controller.currentTrackId(),
                 QStringLiteral("cue-mismatch-track-0"));
        ag_playback_snapshot beforeCue{};
        QCOMPARE(ag_player_snapshot(core, &beforeCue), AG_OK);
        QCOMPARE(beforeCue.track_index, size_t{1});
        QCOMPARE(beforeCue.state, playing ? AG_PLAYING : AG_PAUSED);

        controller.cuePress();
        ag_playback_snapshot afterCue{};
        QCOMPARE(ag_player_snapshot(core, &afterCue), AG_OK);
        controller.cueRelease();
        QCOMPARE(afterCue.track_index, size_t{1});
        QCOMPARE(afterCue.state, beforeCue.state);
        QCOMPARE(controller.cuePositionMs(), savedCueMs);
        if (playing) {
            QVERIFY(afterCue.position_ms < savedCueMs / 2);
        }
    }
    ag_player_destroy(core);
}

void PlaybackControllerTest::clearAndJumpCueFollowTransportContract()
{
    const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_TEST_WAV"));
    QVERIFY(!fixture.isEmpty());
    LibraryModel model;
    TrackRecord track;
    track.trackId = QStringLiteral("cue-jump-clear");
    track.path = fixture;
    track.available = true;
    QVERIFY(model.append(track));

    ag_player_config config{AG_AUDIO_BACKEND_NULL, 2'048};
    ag_player* core = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &core), AG_OK);
    {
        PlaybackController controller(core, &model);
        controller.loadRow(0);
        QTRY_COMPARE(controller.currentTrackId(), track.trackId);
        controller.play();
        QTRY_COMPARE(controller.state(), PlaybackController::Playing);
        controller.pause();
        QTRY_COMPARE(controller.state(), PlaybackController::Paused);
        controller.seek(360);
        controller.cuePress();
        controller.cueRelease();
        QCOMPARE(controller.cuePositionMs(), qint64{360});

        controller.seek(900);
        controller.play();
        QTRY_COMPARE(controller.state(), PlaybackController::Playing);
        controller.jumpToCue();
        QVERIFY(!controller.cueAuditioning());
        QTRY_COMPARE(controller.state(), PlaybackController::Paused);
        QVERIFY(qAbs(controller.positionMs() - qint64{360}) <= 2);

        controller.clearCue();
        QCOMPARE(controller.cuePositionMs(), qint64{-1});
        controller.seek(700);
        controller.jumpToCue();
        QVERIFY(!controller.cueAuditioning());
        QTRY_COMPARE(controller.state(), PlaybackController::Paused);
        QVERIFY(qAbs(controller.positionMs()) <= 2);
    }
    ag_player_destroy(core);
}

void PlaybackControllerTest::hotCuesPersistPerTrackAndKeepTransportState()
{
    const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_TEST_WAV"));
    QVERIFY(!fixture.isEmpty());
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const QString oldOrganization = QCoreApplication::organizationName();
    const QString oldApplication = QCoreApplication::applicationName();
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("AgPlayer"));
    QCoreApplication::setApplicationName(
        QStringLiteral("AgPlayer-playback-hot-cue-test"));
    const QString statePath = QDir(QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation)).filePath(
            QStringLiteral("deck-state.json"));
    QVERIFY(QFile::remove(statePath) || !QFile::exists(statePath));

    LibraryModel model;
    for (int index = 0; index < 2; ++index) {
        TrackRecord track;
        track.trackId = QStringLiteral("hot-cue-track-%1").arg(index);
        track.path = directory.filePath(QStringLiteral("hot-cue-%1.wav").arg(index));
        QVERIFY(QFile::copy(fixture, track.path));
        track.available = true;
        QVERIFY(model.append(track));
    }

    ag_player_config config{AG_AUDIO_BACKEND_NULL, 2'048};
    ag_player* firstCore = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &firstCore), AG_OK);
    {
        PlaybackController controller(firstCore, &model);
        controller.loadRow(0);
        QTRY_COMPARE(controller.currentTrackId(), QStringLiteral("hot-cue-track-0"));
        QCOMPARE(controller.hotCuePositions().size(), 8);
        for (const QVariant& position : controller.hotCuePositions()) {
            QCOMPARE(position.toLongLong(), qint64{-1});
        }

        controller.play();
        QTRY_COMPARE(controller.state(), PlaybackController::Playing);
        controller.pause();
        QTRY_COMPARE(controller.state(), PlaybackController::Paused);
        controller.seek(240);
        controller.activateHotCue(0);
        QCOMPARE(controller.state(), PlaybackController::Paused);
        QCOMPARE(controller.hotCuePositions().at(0).toLongLong(), qint64{240});

        const QVariantList beforeInvalidSlots = controller.hotCuePositions();
        controller.activateHotCue(-1);
        controller.activateHotCue(8);
        controller.clearHotCue(-1);
        controller.clearHotCue(8);
        QCOMPARE(controller.hotCuePositions(), beforeInvalidSlots);

        controller.seek(700);
        controller.activateHotCue(0);
        QTRY_COMPARE(controller.state(), PlaybackController::Playing);
        QVERIFY(controller.positionMs() >= 240);

        QCOMPARE(ag_player_next(firstCore), AG_OK);
        controller.activateHotCue(2);
        QTRY_COMPARE(controller.currentTrackId(), QStringLiteral("hot-cue-track-1"));
        for (const QVariant& position : controller.hotCuePositions()) {
            QCOMPARE(position.toLongLong(), qint64{-1});
        }
        controller.play();
        QTRY_COMPARE(controller.state(), PlaybackController::Playing);
        controller.seek(480);
        controller.activateHotCue(1);
        QCOMPARE(controller.state(), PlaybackController::Playing);
        QCOMPARE(controller.hotCuePositions().at(1).toLongLong(), qint64{480});
        controller.clearHotCue(1);
        QCOMPARE(controller.hotCuePositions().at(1).toLongLong(), qint64{-1});
        QCOMPARE(controller.state(), PlaybackController::Playing);

        controller.loadRow(0);
        QTRY_COMPARE(controller.currentTrackId(), QStringLiteral("hot-cue-track-0"));
        QCOMPARE(controller.hotCuePositions().at(0).toLongLong(), qint64{240});
        QCOMPARE(controller.hotCuePositions().at(2).toLongLong(), qint64{-1});
    }
    ag_player_destroy(firstCore);

    ag_player* secondCore = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &secondCore), AG_OK);
    {
        PlaybackController restored(secondCore, &model);
        restored.loadRow(0);
        QTRY_COMPARE(restored.currentTrackId(), QStringLiteral("hot-cue-track-0"));
        QCOMPARE(restored.hotCuePositions().size(), 8);
        QCOMPARE(restored.hotCuePositions().at(0).toLongLong(), qint64{240});
        for (int slot = 1; slot < 8; ++slot) {
            QCOMPARE(restored.hotCuePositions().at(slot).toLongLong(), qint64{-1});
        }
        restored.loadRow(1);
        QTRY_COMPARE(restored.currentTrackId(), QStringLiteral("hot-cue-track-1"));
        for (const QVariant& position : restored.hotCuePositions()) {
            QCOMPARE(position.toLongLong(), qint64{-1});
        }
    }
    ag_player_destroy(secondCore);

    QFile legacyStore(statePath);
    QVERIFY(legacyStore.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const QByteArray legacyPayload =
        R"({"version":1,"tracks":{"hot-cue-track-0":{"cuePositionMs":125,"beatGridBpm":128}}})";
    QCOMPARE(legacyStore.write(legacyPayload), legacyPayload.size());
    legacyStore.close();

    ag_player* legacyCore = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &legacyCore), AG_OK);
    {
        PlaybackController legacy(legacyCore, &model);
        legacy.loadRow(0);
        QTRY_COMPARE(legacy.currentTrackId(), QStringLiteral("hot-cue-track-0"));
        QCOMPARE(legacy.cuePositionMs(), qint64{125});
        QCOMPARE(legacy.beatGridBpm(), 128.0);
        QCOMPARE(legacy.hotCuePositions().size(), 8);
        for (const QVariant& position : legacy.hotCuePositions()) {
            QCOMPARE(position.toLongLong(), qint64{-1});
        }
    }
    ag_player_destroy(legacyCore);

    QVERIFY(QFile::remove(statePath) || !QFile::exists(statePath));
    QCoreApplication::setOrganizationName(oldOrganization);
    QCoreApplication::setApplicationName(oldApplication);
    QStandardPaths::setTestModeEnabled(false);
}

void PlaybackControllerTest::cuePlayFailureKeepsReleaseContract()
{
    const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_TEST_WAV"));
    QVERIFY(!fixture.isEmpty());
    LibraryModel model;
    TrackRecord track;
    track.trackId = QStringLiteral("cue-play-failure");
    track.path = fixture;
    track.available = true;
    QVERIFY(model.append(track));

    ag_player_config config{AG_AUDIO_BACKEND_NULL, 2'048};
    ag_player* core = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &core), AG_OK);
    {
        PlaybackController controller(core, &model);
        controller.loadRow(0);
        QTRY_COMPARE(controller.currentTrackId(), track.trackId);
        controller.seek(420);
        controller.cuePress();
        controller.cueRelease();
        controller.cuePress();
        QVERIFY(controller.cueAuditioning());
        QTRY_COMPARE(controller.state(), PlaybackController::Playing);

        QCOMPARE(ag_player_simulate_device_loss(core), AG_OK);
        controller.play();
        QVERIFY(controller.cueAuditioning());
        controller.cueRelease();
        QVERIFY(!controller.cueAuditioning());
    }
    ag_player_destroy(core);
}

void PlaybackControllerTest::cueCancellationAndTrackChangeInvalidateHeldCue()
{
    const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_TEST_WAV"));
    QVERIFY(!fixture.isEmpty());
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    LibraryModel model;
    for (int index = 0; index < 2; ++index) {
        TrackRecord track;
        track.trackId = QStringLiteral("cue-track-%1").arg(index);
        track.path = directory.filePath(QStringLiteral("cue-%1.wav").arg(index));
        QVERIFY(QFile::copy(fixture, track.path));
        track.available = true;
        QVERIFY(model.append(track));
    }

    ag_player_config config{AG_AUDIO_BACKEND_NULL, 2'048};
    ag_player* core = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &core), AG_OK);
    {
        PlaybackController controller(core, &model);
        controller.playRow(0);
        QTRY_COMPARE(controller.currentTrackId(), QStringLiteral("cue-track-0"));
        controller.pause();
        QTRY_COMPARE(controller.state(), PlaybackController::Paused);
        controller.seek(240);
        controller.cuePress();
        controller.cueRelease();
        controller.cuePress();
        QVERIFY(controller.cueAuditioning());
        QTRY_COMPARE(controller.state(), PlaybackController::Playing);

        controller.cancelCue();
        QVERIFY(!controller.cueAuditioning());
        QTRY_COMPARE(controller.state(), PlaybackController::Paused);
        QVERIFY(qAbs(controller.positionMs() - qint64{240}) <= 2);
        controller.cueRelease();
        QTest::qWait(PlaybackController::PollIntervalMs * 2);
        QCOMPARE(controller.state(), PlaybackController::Paused);

        controller.cuePress();
        QVERIFY(controller.cueAuditioning());
        controller.playRow(1);
        QTRY_COMPARE(controller.currentTrackId(), QStringLiteral("cue-track-1"));
        QTRY_COMPARE(controller.state(), PlaybackController::Playing);
        QVERIFY(!controller.cueAuditioning());
        const qint64 newTrackPosition = controller.positionMs();
        controller.cueRelease();
        QTest::qWait(PlaybackController::PollIntervalMs * 3);
        QCOMPARE(controller.currentTrackId(), QStringLiteral("cue-track-1"));
        QCOMPARE(controller.state(), PlaybackController::Playing);
        QVERIFY(controller.positionMs() >= newTrackPosition);

        controller.seek(700);
        controller.cuePress();
        QVERIFY(!controller.cueAuditioning());
        QTRY_COMPARE(controller.state(), PlaybackController::Paused);
        QVERIFY(qAbs(controller.positionMs()) <= 2);
        QCOMPARE(controller.cuePositionMs(), qint64{-1});
        controller.cueRelease();

        controller.playRow(0);
        QTRY_COMPARE(controller.currentTrackId(), QStringLiteral("cue-track-0"));
        controller.pause();
        QTRY_COMPARE(controller.state(), PlaybackController::Paused);
        controller.seek(240);
        controller.cuePress();
        QVERIFY(controller.cueAuditioning());
        QTRY_COMPARE(controller.state(), PlaybackController::Playing);
        QCOMPARE(ag_player_next(core), AG_OK);
        controller.cueRelease();
        QTRY_COMPARE(controller.currentTrackId(), QStringLiteral("cue-track-1"));
        QTRY_COMPARE(controller.state(), PlaybackController::Playing);
    }
    ag_player_destroy(core);
}

void PlaybackControllerTest::largeVisibleQueuePreservesOrderAndDeduplicates_data()
{
    QTest::addColumn<int>("count");
    QTest::addColumn<bool>("fallback");
    QTest::newRow("large-scope") << 12000 << false;
    QTest::newRow("small-scope-large-library") << 12000 << true;
}

void PlaybackControllerTest::largeVisibleQueuePreservesOrderAndDeduplicates()
{
    QFETCH(int, count);
    QFETCH(bool, fallback);
    const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_TEST_WAV"));
    QVERIFY(QFile::exists(fixture));
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    LibraryModel model;
    QList<TrackRecord> records;
    QStringList expected;
    for (int index = 0; index < count; ++index) {
        TrackRecord record;
        record.trackId = QStringLiteral("queue-%1").arg(index);
        // Only the first item is decoded. Remaining records exercise queue
        // construction, independently of disk size or decoder throughput.
        record.path = index == 0 ? fixture
            : directory.filePath(record.trackId + QStringLiteral(".wav"));
        record.available = true;
        expected.append(record.trackId);
        records.append(std::move(record));
    }
    QCOMPARE(model.appendBatch(std::move(records)).size(), count);
    QStringList requested = fallback ? expected.mid(0, 3) : expected;
    requested.append(requested.first());
    requested.append(QStringLiteral("missing"));
    ag_player_config config{AG_AUDIO_BACKEND_NULL, 2048};
    ag_player* core = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &core), AG_OK);
    {
        PlaybackController controller(core, &model);
        QList<double> timings;
        for (int sample = 0; sample < 5; ++sample) {
            QElapsedTimer timer;
            timer.start();
            QVERIFY(controller.playTrackIds(requested, expected.first()));
            timings.append(timer.nsecsElapsed() / 1000000.0);
            QCOMPARE(controller.queueTrackIds(), expected);
            controller.stop();
        }
        std::sort(timings.begin(), timings.end());
        qInfo("queue setup count=%d fallback=%d median_ms=%.3f", count,
              fallback ? 1 : 0, timings.at(timings.size() / 2));
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
        controller.setSpeedRatio(1.25);
        controller.setKeepPitch(false);
        QTRY_COMPARE(controller.state(), PlaybackController::Paused);
        QTRY_VERIFY(qAbs(controller.positionMs() - 750) <= 2);
        const QStringList expectedQueue = controller.queueTrackIds();
        const QString expectedTrack = controller.currentTrackId();

        QVERIFY(controller.acquireEditorOutput());
        ag_playback_time_pitch_config editorConfig{};
        QCOMPARE(ag_player_get_time_pitch(core, &editorConfig), AG_OK);
        QCOMPARE(editorConfig.speed_ratio, 1.0);
        QVERIFY(!editorConfig.keep_pitch);
        controller.setSpeedRatio(0.75);
        QCOMPARE(ag_player_get_time_pitch(core, &editorConfig), AG_OK);
        QCOMPARE(editorConfig.speed_ratio, 1.0);
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
        QCOMPARE(controller.speedRatio(), 1.25);
        QVERIFY(!controller.keepPitch());
        ag_playback_time_pitch_config restoredConfig{};
        QCOMPARE(ag_player_get_time_pitch(core, &restoredConfig), AG_OK);
        QCOMPARE(restoredConfig.speed_ratio, 1.25);
        QVERIFY(!restoredConfig.keep_pitch);
    }
    ag_player_destroy(core);
}

void PlaybackControllerTest::editorOutputLeaseFailuresAreRetryable()
{
    const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_TEST_WAV"));
    QVERIFY(!fixture.isEmpty());
    LibraryModel model;
    TrackRecord track;
    track.trackId = QStringLiteral("editor-retry");
    track.path = fixture;
    track.available = true;
    QVERIFY(model.append(track));

    ag_player_config config{AG_AUDIO_BACKEND_NULL, 2'048};
    ag_player* core = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &core), AG_OK);
    {
        PlaybackController controller(core, &model);
        controller.playRow(0);
        controller.seek(500);
        controller.pause();
        controller.setSpeedRatio(1.25);
        controller.setKeepPitch(false);
        QTRY_COMPARE(controller.state(), PlaybackController::Paused);
        QTRY_VERIFY(qAbs(controller.positionMs() - 500) <= 2);

        controller.editorOutputFailureStepForTesting_ =
            PlaybackController::EditorOutputStep::AcquireStop;
        QVERIFY(!controller.acquireEditorOutput());
        QVERIFY(!controller.editorOutputOwned_);
        QVERIFY(!controller.editorSessionSnapshot_.has_value());
        QTRY_COMPARE(controller.state(), PlaybackController::Paused);
        QTRY_VERIFY(qAbs(controller.positionMs() - 500) <= 2);

        controller.editorOutputFailureStepForTesting_ =
            PlaybackController::EditorOutputStep::AcquireStopAfterCall;
        QVERIFY(!controller.acquireEditorOutput());
        QVERIFY(!controller.editorOutputOwned_);
        QVERIFY(!controller.editorSessionSnapshot_.has_value());
        ag_playback_snapshot coreSnapshot{};
        QCOMPARE(ag_player_snapshot(core, &coreSnapshot), AG_OK);
        QCOMPARE(coreSnapshot.state, AG_PAUSED);
        QVERIFY(qAbs(coreSnapshot.position_ms - 500) <= 2);

        controller.editorOutputFailureStepForTesting_ =
            PlaybackController::EditorOutputStep::AcquireStopAfterCall;
        controller.editorOutputSecondFailureStepForTesting_ =
            PlaybackController::EditorOutputStep::RestoreQueue;
        QVERIFY(!controller.acquireEditorOutput());
        QVERIFY(controller.editorOutputOwned_);
        QVERIFY(controller.editorSessionSnapshot_.has_value());
        QCOMPARE(ag_player_snapshot(core, &coreSnapshot), AG_OK);
        QVERIFY(coreSnapshot.state != AG_PLAYING);
        controller.releaseEditorOutput();
        QVERIFY(!controller.editorOutputOwned_);
        QVERIFY(!controller.editorSessionSnapshot_.has_value());
        QTRY_COMPARE(controller.state(), PlaybackController::Paused);
        QTRY_VERIFY(qAbs(controller.positionMs() - 500) <= 2);

        controller.editorOutputFailureStepForTesting_ =
            PlaybackController::EditorOutputStep::AcquireTimePitch;
        QVERIFY(!controller.acquireEditorOutput());
        QVERIFY(!controller.editorOutputOwned_);
        QVERIFY(!controller.editorSessionSnapshot_.has_value());
        QTRY_COMPARE(controller.state(), PlaybackController::Paused);
        QTRY_VERIFY(qAbs(controller.positionMs() - 500) <= 2);
        QCOMPARE(controller.speedRatio(), 1.25);
        QVERIFY(!controller.keepPitch());

        for (const PlaybackController::EditorOutputStep step : {
                 PlaybackController::EditorOutputStep::RestoreStop,
                 PlaybackController::EditorOutputStep::RestoreQueue,
                 PlaybackController::EditorOutputStep::RestoreMode,
                 PlaybackController::EditorOutputStep::RestoreTimePitch,
                 PlaybackController::EditorOutputStep::RestoreSeek,
                 PlaybackController::EditorOutputStep::RestorePlay,
                 PlaybackController::EditorOutputStep::RestorePause}) {
            QVERIFY(controller.acquireEditorOutput());
            controller.editorOutputFailureStepForTesting_ = step;
            controller.releaseEditorOutput();
            QVERIFY(controller.editorOutputOwned_);
            QVERIFY(controller.editorSessionSnapshot_.has_value());
            QCOMPARE(ag_player_snapshot(core, &coreSnapshot), AG_OK);
            QVERIFY(coreSnapshot.state != AG_PLAYING);
            const std::int64_t failedPosition = coreSnapshot.position_ms;
            QTest::qWait(50);
            QCOMPARE(ag_player_snapshot(core, &coreSnapshot), AG_OK);
            QVERIFY(coreSnapshot.state != AG_PLAYING);
            QCOMPARE(coreSnapshot.position_ms, failedPosition);

            controller.releaseEditorOutput();
            QVERIFY(!controller.editorOutputOwned_);
            QVERIFY(!controller.editorSessionSnapshot_.has_value());
            QTRY_COMPARE(controller.currentTrackId(), track.trackId);
            QTRY_COMPARE(controller.state(), PlaybackController::Paused);
            QTRY_VERIFY(qAbs(controller.positionMs() - 500) <= 2);
            QCOMPARE(controller.speedRatio(), 1.25);
            QVERIFY(!controller.keepPitch());
        }
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
        controller.editorOutputFailureStepForTesting_ =
            PlaybackController::EditorOutputStep::RestoreStop;
        controller.releaseEditorOutput();
        QVERIFY(controller.editorOutputOwned_);
        controller.releaseEditorOutput();
        QVERIFY(!controller.editorOutputOwned_);
        QVERIFY(controller.acquireEditorOutput());
        controller.releaseEditorOutput();
    }
    ag_player_destroy(core);
}

void PlaybackControllerTest::editorOutputCanBeReacquiredAfterEmptySessionStream()
{
    const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_TEST_WAV"));
    QVERIFY(!fixture.isEmpty());

    ag_player_config config{AG_AUDIO_BACKEND_NULL, 2048};
    ag_player* core = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &core), AG_OK);
    {
        PlaybackController controller(core);
        controller.setSpeedRatio(1.25);
        controller.setKeepPitch(true);
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
        controller.releaseEditorOutput();

        ag_playback_time_pitch_config beforeReacquire{};
        QCOMPARE(ag_player_get_time_pitch(core, &beforeReacquire), AG_OK);
        QCOMPARE(beforeReacquire.speed_ratio, 1.0);
        QVERIFY(beforeReacquire.keep_pitch);

        QVERIFY(controller.acquireEditorOutput());
        ag_playback_time_pitch_config afterReacquire{};
        QCOMPARE(ag_player_get_time_pitch(core, &afterReacquire), AG_OK);
        QCOMPARE(afterReacquire.speed_ratio, beforeReacquire.speed_ratio);
        QCOMPARE(afterReacquire.keep_pitch, beforeReacquire.keep_pitch);
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
    QSignalSpy seekCommitted(&controller, &PlaybackController::seekCommitted);
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
    QCOMPARE(seekCommitted.count(), 0);
}

void PlaybackControllerTest::scratchBridgePublishesPausedStatusAndValidatesCommands()
{
    const QByteArray path = qgetenv("AGPLAYER_TEST_WAV");
    QVERIFY(!path.isEmpty());
    ag_player_config config{AG_AUDIO_BACKEND_NULL, 2'048};
    ag_player* core = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &core), AG_OK);
    {
        PlaybackController controller(core);
        QCOMPARE(ag_player_load(core, path.constData()), AG_OK);
        controller.play();
        QTRY_COMPARE(controller.state(), PlaybackController::Playing);
        controller.pause();
        QTRY_COMPARE(controller.state(), PlaybackController::Paused);

        QSignalSpy statusChanged(
            &controller, &PlaybackController::scratchStatusChanged);
        QVERIFY(controller.beginScratch());
        QTRY_VERIFY(controller.scratchActive());
        QCOMPARE(controller.state(), PlaybackController::Paused);
        QCOMPARE(controller.pollTimer_.interval(),
                 PlaybackController::PollIntervalMs);
        QVERIFY(statusChanged.count() > 0);

        QVERIFY(!controller.updateScratch(
            std::numeric_limits<double>::quiet_NaN()));
        QVERIFY(!controller.errorMessage().isEmpty());

        // No status query is allowed on the high-frequency update path.  This
        // impossible public-state combination is replaced only by polling.
        controller.pollTimer_.stop();
        controller.scratchReady_ = true;
        controller.scratchBuffering_ = true;
        QVERIFY(controller.updateScratch(1.0));
        QVERIFY(controller.scratchReady());
        QVERIFY(controller.scratchBuffering());
        controller.pollSnapshot();
        QVERIFY(!(controller.scratchReady()
                  && controller.scratchBuffering()));
        controller.pollTimer_.start();

        QVERIFY(controller.cancelScratch());
        QTRY_VERIFY(!controller.scratchActive());
        QCOMPARE(controller.pollTimer_.interval(),
                 PlaybackController::IdlePollIntervalMs);
    }
    ag_player_destroy(core);

    PlaybackController unavailable;
    QVERIFY(!unavailable.beginScratch());
    QVERIFY(!unavailable.updateScratch(1.0));
    QVERIFY(!unavailable.endScratch());
    QVERIFY(!unavailable.cancelScratch());
    QCOMPARE(unavailable.errorMessage(),
             QStringLiteral("Playback core is unavailable"));
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
