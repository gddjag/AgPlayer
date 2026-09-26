#include "playback_controller.hpp"

#include "library_model.hpp"
#include "runtime_log.hpp"

#include <agplayer/c_api.h>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#ifdef Q_OS_MACOS
#include <QGuiApplication>
#include <QWindow>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace {
PlaybackController::State toState(ag_playback_state state)
{
    switch (state) {
    case AG_STOPPED:
        return PlaybackController::Stopped;
    case AG_LOADING:
        return PlaybackController::Loading;
    case AG_PLAYING:
        return PlaybackController::Playing;
    case AG_PAUSED:
        return PlaybackController::Paused;
    case AG_ERROR:
        return PlaybackController::Error;
    }
    return PlaybackController::Error;
}

PlaybackController::Mode toMode(ag_playback_mode mode)
{
    switch (mode) {
    case AG_MODE_SEQUENTIAL:
        return PlaybackController::Sequential;
    case AG_MODE_REPEAT_ONE:
        return PlaybackController::RepeatOne;
    case AG_MODE_SHUFFLE:
        return PlaybackController::Shuffle;
    case AG_MODE_REPEAT_ALL:
        return PlaybackController::RepeatAll;
    }
    return PlaybackController::Sequential;
}

ag_playback_mode toCoreMode(PlaybackController::Mode mode)
{
    switch (mode) {
    case PlaybackController::Sequential:
        return AG_MODE_SEQUENTIAL;
    case PlaybackController::RepeatOne:
        return AG_MODE_REPEAT_ONE;
    case PlaybackController::Shuffle:
        return AG_MODE_SHUFFLE;
    case PlaybackController::RepeatAll:
        return AG_MODE_REPEAT_ALL;
    }
    return AG_MODE_SEQUENTIAL;
}

qint64 checkedSize(size_t value)
{
    constexpr auto maximum = static_cast<size_t>(std::numeric_limits<qint64>::max());
    return value <= maximum ? static_cast<qint64>(value) : -1;
}

QString playerError(ag_player* player)
{
    size_t required = 0;
    if (ag_player_last_error(player, nullptr, 0, &required) != AG_OK || required == 0) {
        return QStringLiteral("Playback operation failed");
    }
    QByteArray bytes(static_cast<qsizetype>(required), Qt::Uninitialized);
    if (ag_player_last_error(player, bytes.data(), required, &required) != AG_OK) {
        return QStringLiteral("Playback operation failed");
    }
    return QString::fromUtf8(bytes.constData());
}
}

PlaybackController::PlaybackController(ag_player* player,
                                       LibraryModel* library,
                                       QObject* parent)
    : QObject(parent), player_(player)
{
    spectrum_.fill(0.0F, 64);
    loadDeckStateStore();
    setLibraryModel(library);
    pollTimer_.setInterval(PollIntervalMs);
    pollTimer_.setTimerType(Qt::PreciseTimer);
    connect(&pollTimer_, &QTimer::timeout, this, &PlaybackController::pollSnapshot);
    refreshOutputDevices();
    pollSnapshot();
    pollTimer_.start();
}

PlaybackController::~PlaybackController()
{
    emit aboutToBeDestroyed();
}

PlaybackController::State PlaybackController::state() const noexcept { return state_; }
qint64 PlaybackController::positionMs() const noexcept { return positionMs_; }
qint64 PlaybackController::durationMs() const noexcept { return durationMs_; }
float PlaybackController::volume() const noexcept { return volume_; }
bool PlaybackController::muted() const noexcept { return muted_; }
PlaybackController::Mode PlaybackController::mode() const noexcept { return mode_; }
qint64 PlaybackController::trackIndex() const noexcept { return trackIndex_; }
qint64 PlaybackController::trackCount() const noexcept { return trackCount_; }
QString PlaybackController::currentTrackId() const { return currentTrackId_; }
QStringList PlaybackController::queueTrackIds() const { return queueTrackIds_; }
QString PlaybackController::lyrics() const { return lyrics_; }
QString PlaybackController::errorMessage() const { return errorMessage_; }
bool PlaybackController::deviceLost() const noexcept { return deviceLost_; }
QStringList PlaybackController::outputDevices() const { return outputDevices_; }
QStringList PlaybackController::outputDeviceIds() const { return outputDeviceIds_; }
bool PlaybackController::exclusiveModeActive() const noexcept
{
    return exclusiveModeActive_;
}

QVariantList PlaybackController::spectrum() const
{
    return spectrum_;
}

bool PlaybackController::replayGainClippingWarning() const noexcept
{
    return replayGainClippingWarning_;
}

qint64 PlaybackController::selectionStartMs() const noexcept
{
    return selectionStartMs_;
}

qint64 PlaybackController::selectionEndMs() const noexcept
{
    return selectionEndMs_;
}

bool PlaybackController::selectionLoopEnabled() const noexcept
{
    return selectionLoopEnabled_;
}

double PlaybackController::speedRatio() const noexcept { return speedRatio_; }
double PlaybackController::sourceBpm() const noexcept { return sourceBpm_; }
double PlaybackController::targetBpm() const noexcept
{
    return sourceBpm_ > 0.0 ? sourceBpm_ * speedRatio_ : 0.0;
}
bool PlaybackController::keepPitch() const noexcept { return keepPitch_; }
bool PlaybackController::scratchActive() const noexcept
{
    return scratchActive_;
}
bool PlaybackController::scratchReady() const noexcept
{
    return scratchReady_;
}
bool PlaybackController::scratchBuffering() const noexcept
{
    return scratchBuffering_;
}

qint64 PlaybackController::cuePositionMs() const noexcept
{
    const qint64 manualCue = currentDeckState().cuePositionMs;
    return manualCue >= 0 ? manualCue : automaticCuePositionMs_;
}

bool PlaybackController::cueAuditioning() const noexcept
{
    return cueAuditioning_;
}

double PlaybackController::beatGridBpm() const noexcept
{
    const auto deck = currentDeckState();
    const double overrideBpm = deck.beatGridBpmOverride;
    if (validBeatGridBpm(overrideBpm)) return overrideBpm;
    if (!deck.beatGridCalibrated && automaticBeatGrid_.reliable)
        return automaticBeatGrid_.bpm;
    if (validBeatGridBpm(sourceBpm_)) return sourceBpm_;
    if (validBeatGridBpm(beatGridWaveformBpm_)) return beatGridWaveformBpm_;
    return 120.0;
}

qint64 PlaybackController::beatGridOffsetMs() const noexcept
{
    const auto deck = currentDeckState();
    return deck.beatGridCalibrated ? deck.beatGridOffsetMs
        : automaticBeatGrid_.reliable ? automaticBeatGrid_.offsetMs
                                     : deck.beatGridOffsetMs;
}

bool PlaybackController::beatGridCalibrated() const noexcept
{
    return currentDeckState().beatGridCalibrated;
}

bool PlaybackController::beatGridEstimatedBpm() const noexcept
{
    return !validBeatGridBpm(currentDeckState().beatGridBpmOverride)
        && !validBeatGridBpm(sourceBpm_) && !automaticBeatGrid_.reliable
        && !validBeatGridBpm(beatGridWaveformBpm_);
}

QVariantList PlaybackController::hotCuePositions() const
{
    QVariantList positions;
    positions.reserve(8);
    for (const qint64 position : currentDeckState().hotCuePositions) {
        positions.push_back(position);
    }
    return positions;
}

void PlaybackController::setLibraryModel(LibraryModel* library)
{
    disconnect(playRequestedConnection_);
    disconnect(libraryDataChangedConnection_);
    disconnect(libraryResetConnection_);
    library_ = library;
    if (library_ != nullptr) {
        playRequestedConnection_ = connect(
            library_, &LibraryModel::playRequested, this, &PlaybackController::playRow);
        libraryDataChangedConnection_ = connect(
            library_, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex& first, const QModelIndex& last,
                   const QList<int>& roles) {
                if (!roles.isEmpty() && !roles.contains(LibraryModel::BpmRole)) {
                    return;
                }
                const int row = library_ != nullptr
                    ? library_->indexForTrackId(currentTrackId_) : -1;
                if (row >= first.row() && row <= last.row()) {
                    refreshSourceBpm();
                }
            });
        libraryResetConnection_ = connect(
            library_, &QAbstractItemModel::modelReset, this,
            &PlaybackController::refreshSourceBpm);
    }
    refreshSourceBpm();
}

void PlaybackController::setPlayer(ag_player* player)
{
    invalidateCueHoldForTrackChange();
    editorOutputOwned_ = false;
    editorRestorePending_ = false;
    editorSessionSnapshot_.reset();
    editorOutputFailureStepForTesting_.reset();
    editorOutputSecondFailureStepForTesting_.reset();
    player_ = player;
    refreshOutputDevices();
    syncTimePitchFromCore();
}

bool PlaybackController::acquireEditorOutput() noexcept
{
    if (player_ == nullptr) return false;
    if (editorOutputOwned_) {
        return editorSessionSnapshot_.has_value() && !editorRestorePending_;
    }
    try {
        ag_playback_snapshot snapshot{};
        if (ag_player_snapshot(player_, &snapshot) != AG_OK) return false;
        PlaybackSessionSnapshot saved;
        saved.queueTrackIds = queueTrackIds_;
        if (snapshot.track_index
            < static_cast<std::size_t>(queueTrackIds_.size())) {
            saved.currentTrackId = queueTrackIds_.at(
                static_cast<qsizetype>(snapshot.track_index));
        } else {
            saved.currentTrackId = currentTrackId_;
        }
        saved.positionMs = snapshot.position_ms;
        saved.state = toState(snapshot.state);
        saved.mode = toMode(snapshot.mode);
        saved.scopeSize = activeScopeSize_ > 0
            ? activeScopeSize_ : queueTrackIds_.size();
        saved.allowFallback = activeScopeAllowsFallback_;
        ag_playback_time_pitch_config activeConfig{};
        if (ag_player_get_time_pitch(player_, &activeConfig) != AG_OK) {
            return false;
        }
        saved.speedRatio = activeConfig.speed_ratio;
        saved.keepPitch = activeConfig.keep_pitch != 0;
        // Persist recovery credentials before the first mutating Core call.
        // A failed acquire can then either roll back immediately or leave a
        // retryable restore-pending lease without losing the main session.
        editorSessionSnapshot_ = saved;
        // A fresh player and an already-stopped loaded player both report
        // AG_STOPPED.  Neither needs a stop command before the editor stream
        // replaces its source; an unloaded core rejects stop as invalid.
        // Active/error states still take the real stop path and propagate any
        // failure instead of claiming the output was acquired.
        const bool stoppedForLease = snapshot.state != AG_STOPPED;
        if (stoppedForLease) {
            if (shouldFailEditorOutputStep(EditorOutputStep::AcquireStop)) {
                editorSessionSnapshot_.reset();
                return false;
            }
            ag_result stopResult = ag_player_stop(player_);
            if (stopResult == AG_OK
                && shouldFailEditorOutputStep(
                    EditorOutputStep::AcquireStopAfterCall)) {
                stopResult = AG_INTERNAL_ERROR;
            }
            if (stopResult != AG_OK) {
                editorOutputOwned_ = true;
                editorRestorePending_ = true;
                if (restoreEditorSession()) {
                    editorOutputOwned_ = false;
                    editorRestorePending_ = false;
                    editorSessionSnapshot_.reset();
                }
                return false;
            }
        }
        const ag_playback_time_pitch_config editorConfig{
            1.0, saved.keepPitch ? 1 : 0};
        ag_result tempoResult = AG_OK;
        if (!qFuzzyCompare(activeConfig.speed_ratio, editorConfig.speed_ratio)
            || activeConfig.keep_pitch != editorConfig.keep_pitch) {
            tempoResult =
                shouldFailEditorOutputStep(EditorOutputStep::AcquireTimePitch)
                ? AG_INTERNAL_ERROR
                : ag_player_set_time_pitch(player_, &editorConfig);
        }
        if (tempoResult != AG_OK) {
            if (stoppedForLease) {
                editorOutputOwned_ = true;
                editorRestorePending_ = true;
                if (restoreEditorSession()) {
                    editorOutputOwned_ = false;
                    editorRestorePending_ = false;
                    editorSessionSnapshot_.reset();
                }
            } else {
                editorSessionSnapshot_.reset();
            }
            return false;
        }
        editorOutputOwned_ = true;
        editorRestorePending_ = false;
        const bool tempoChangedValue = !qFuzzyCompare(speedRatio_, 1.0)
            || keepPitch_ != editorSessionSnapshot_->keepPitch;
        speedRatio_ = 1.0;
        keepPitch_ = editorSessionSnapshot_->keepPitch;
        if (tempoChangedValue) emit tempoChanged();
        if (state_ != Stopped) {
            state_ = Stopped;
            emit stateChanged();
        }
        if (positionMs_ != 0) {
            positionMs_ = 0;
            emit positionMsChanged();
        }
        return true;
    } catch (...) {
        return false;
    }
}

void PlaybackController::releaseEditorOutput(const bool resumePrevious) noexcept
{
    if (!editorOutputOwned_) return;
    if (!editorSessionSnapshot_) {
        editorRestorePending_ = true;
        setErrorMessage(QStringLiteral("Unable to restore the playback session"));
        return;
    }
    if (!resumePrevious && editorSessionSnapshot_->state == Playing)
        editorSessionSnapshot_->state = Paused;
    const bool hasMainSession =
        !editorSessionSnapshot_->queueTrackIds.isEmpty();
    const bool injectedStopFailure =
        shouldFailEditorOutputStep(EditorOutputStep::RestoreStop);
    const ag_result stopResult = injectedStopFailure
        ? AG_INTERNAL_ERROR : ag_player_stop(player_);
    if (stopResult != AG_OK
        && (injectedStopFailure || hasMainSession
            || stopResult != AG_INVALID_ARGUMENT)) {
        editorRestorePending_ = true;
        (void)ag_player_stop(player_);
        runCommand(stopResult);
        return;
    }
    if (!hasMainSession) {
        editorOutputOwned_ = false;
        editorRestorePending_ = false;
        editorSessionSnapshot_.reset();
        return;
    }
    editorRestorePending_ = true;
    if (!restoreEditorSession()) return;
    editorOutputOwned_ = false;
    editorRestorePending_ = false;
    editorSessionSnapshot_.reset();
}

bool PlaybackController::restoreEditorSession() noexcept
{
    if (!editorSessionSnapshot_ || library_ == nullptr || player_ == nullptr) {
        setErrorMessage(QStringLiteral("Unable to restore the playback session"));
        return false;
    }
    try {
        const PlaybackSessionSnapshot& saved = *editorSessionSnapshot_;
        std::vector<QByteArray> utf8Paths;
        std::vector<const char*> paths;
        utf8Paths.reserve(static_cast<std::size_t>(saved.queueTrackIds.size()));
        paths.reserve(static_cast<std::size_t>(saved.queueTrackIds.size()));
        for (const QString& trackId : saved.queueTrackIds) {
            const int row = library_->indexForTrackId(trackId);
            if (row < 0 || !library_->tracks().at(row).available
                || library_->tracks().at(row).path.isEmpty()) {
                setErrorMessage(QStringLiteral(
                    "Unable to restore the playback session"));
                return false;
            }
            utf8Paths.push_back(library_->tracks().at(row).path.toUtf8());
            paths.push_back(utf8Paths.back().constData());
        }
        const qsizetype current = saved.queueTrackIds.indexOf(saved.currentTrackId);
        const std::size_t startIndex = current >= 0
            ? static_cast<std::size_t>(current) : 0U;
        const std::size_t scopeSize = static_cast<std::size_t>(std::clamp<qsizetype>(
            saved.scopeSize, 1, saved.queueTrackIds.size()));
        ag_result result =
            shouldFailEditorOutputStep(EditorOutputStep::RestoreQueue)
            ? AG_INTERNAL_ERROR
            : ag_player_set_scoped_queue(
                player_, paths.data(), paths.size(), startIndex, scopeSize,
                saved.allowFallback ? 1 : 0);
        if (result == AG_OK) {
            result = shouldFailEditorOutputStep(EditorOutputStep::RestoreMode)
                ? AG_INTERNAL_ERROR
                : ag_player_set_mode(player_, toCoreMode(saved.mode));
        }
        if (result == AG_OK) {
            const ag_playback_time_pitch_config restoredConfig{
                saved.speedRatio, saved.keepPitch ? 1 : 0};
            result =
                shouldFailEditorOutputStep(EditorOutputStep::RestoreTimePitch)
                ? AG_INTERNAL_ERROR
                : ag_player_set_time_pitch(player_, &restoredConfig);
        }
        if (result == AG_OK && saved.positionMs > 0) {
            result = shouldFailEditorOutputStep(EditorOutputStep::RestoreSeek)
                ? AG_INTERNAL_ERROR
                : ag_player_seek(player_, saved.positionMs);
        }
        if (result == AG_OK && (saved.state == Playing || saved.state == Paused)) {
            result = shouldFailEditorOutputStep(EditorOutputStep::RestorePlay)
                ? AG_INTERNAL_ERROR : ag_player_play(player_);
        }
        if (result == AG_OK && saved.state == Paused) {
            result = shouldFailEditorOutputStep(EditorOutputStep::RestorePause)
                ? AG_INTERNAL_ERROR : ag_player_pause(player_);
        }
        if (result != AG_OK) {
            (void)ag_player_stop(player_);
            runCommand(result);
            syncTimePitchFromCore();
            return false;
        }
        const bool tempoChangedValue =
            !qFuzzyCompare(speedRatio_, saved.speedRatio)
            || keepPitch_ != saved.keepPitch;
        speedRatio_ = saved.speedRatio;
        keepPitch_ = saved.keepPitch;
        if (tempoChangedValue) emit tempoChanged();
        queueTrackIds_ = saved.queueTrackIds;
        activeScopeSize_ = saved.scopeSize;
        activeScopeAllowsFallback_ = saved.allowFallback;
        emit queueTrackIdsChanged();
        setErrorMessage(QString());
        pollSnapshot();
        return true;
    } catch (...) {
        if (player_ != nullptr) (void)ag_player_stop(player_);
        setErrorMessage(QStringLiteral("Unable to restore the playback session"));
        return false;
    }
}

bool PlaybackController::shouldFailEditorOutputStep(
    const EditorOutputStep step) noexcept
{
    if (!editorOutputFailureStepForTesting_
        || *editorOutputFailureStepForTesting_ != step) {
        if (!editorOutputSecondFailureStepForTesting_
            || *editorOutputSecondFailureStepForTesting_ != step) {
            return false;
        }
        editorOutputSecondFailureStepForTesting_.reset();
        return true;
    }
    editorOutputFailureStepForTesting_.reset();
    return true;
}

void PlaybackController::play()
{
    cancelBeatGridAutoPosition();
    bool accepted = true;
    emit playbackRequested(&accepted);
    if (!accepted) return;
    if (editorOutputOwned_) return;
    const ag_result result = player_ != nullptr
        ? ag_player_play(player_) : AG_INVALID_ARGUMENT;
    runCommand(result);
    if (result == AG_OK && cueHeld_ && cueAuditioning_) {
        cueAuditioning_ = false;
        cueAuditionTrackId_.clear();
        cueAuditionReturnMs_ = -1;
        emit cueChanged();
    }
}

void PlaybackController::pause()
{
    cancelBeatGridAutoPosition();
    if (editorOutputOwned_) return;
    runCommand(player_ != nullptr ? ag_player_pause(player_) : AG_INVALID_ARGUMENT);
}

bool PlaybackController::pauseForPlaybackHandoff()
{
    if (!player_ || editorOutputOwned_) return true;
    ag_playback_snapshot snapshot{};
    if (ag_player_snapshot(player_, &snapshot) != AG_OK) return false;
    if (snapshot.state != AG_PLAYING) return true;
    const ag_result result = ag_player_pause(player_);
    runCommand(result);
    return result == AG_OK;
}

void PlaybackController::stop()
{
    cancelBeatGridAutoPosition();
    if (editorOutputOwned_) return;
    runCommand(player_ != nullptr ? ag_player_stop(player_) : AG_INVALID_ARGUMENT);
}

void PlaybackController::togglePlayback()
{
    if (cueHeld_) {
        play();
        return;
    }
    if (state_ == Playing) {
        pause();
    } else {
        play();
    }
}

void PlaybackController::seek(qint64 positionMs)
{
    cancelBeatGridAutoPosition();
    if (editorOutputOwned_) return;
    if (player_ == nullptr) {
        runCommand(AG_INVALID_ARGUMENT);
        return;
    }

    ag_playback_snapshot snapshot{};
    const ag_result snapshotResult = ag_player_snapshot(player_, &snapshot);
    if (snapshotResult != AG_OK) {
        runCommand(snapshotResult);
        return;
    }
    // The waveform decoder may have replaced container metadata with the exact
    // PCM duration. Do not re-clamp a UI seek to a stale metadata snapshot.
    const qint64 durationMs = std::max({qint64{0}, durationMs_, snapshot.duration_ms});
    const qint64 targetMs = std::clamp(positionMs, qint64{0}, durationMs);
    // The decoder thread can publish an older container duration between UI
    // polling ticks. Reassert the displayed timeline before submitting the
    // seek so one click cannot be clamped to a different pixel position.
    if (durationMs_ > 0 && snapshot.duration_ms != durationMs_) {
        const ag_result syncResult = ag_player_set_duration_ms(player_, durationMs_);
        runCommand(syncResult);
        if (syncResult != AG_OK) {
            return;
        }
    }
    const ag_result result = ag_player_seek(player_, targetMs);
    runCommand(result);
    if (result == AG_OK) {
        if (positionMs_ != targetMs) {
            positionMs_ = targetMs;
            emit positionMsChanged();
        }
        emit seekCommitted(targetMs);
    }
}

void PlaybackController::commitSelection(qint64 startMs, qint64 endMs)
{
    if (!setSelection(startMs, endMs, true)) {
        return;
    }
    seek(selectionStartMs_);
    play();
}

void PlaybackController::adjustSelection(qint64 startMs, qint64 endMs)
{
    setSelection(startMs, endMs, true);
}

void PlaybackController::disableSelectionLoopAndSeek(qint64 positionMs)
{
    if (selectionLoopEnabled_) {
        selectionLoopEnabled_ = false;
        emit selectionLoopEnabledChanged();
    }
    seek(positionMs);
}

void PlaybackController::clearSelection()
{
    const bool startChanged = selectionStartMs_ != 0;
    const bool endChanged = selectionEndMs_ != 0;
    const bool loopChanged = selectionLoopEnabled_;
    selectionStartMs_ = 0;
    selectionEndMs_ = 0;
    selectionLoopEnabled_ = false;
    if (startChanged) {
        emit selectionStartMsChanged();
    }
    if (endChanged) {
        emit selectionEndMsChanged();
    }
    if (loopChanged) {
        emit selectionLoopEnabledChanged();
    }
}

void PlaybackController::cuePress()
{
    cancelBeatGridAutoPosition();
    bool accepted = true;
    emit playbackRequested(&accepted);
    if (!accepted) return;
    if (cueHeld_ || player_ == nullptr || editorOutputOwned_
        || !hasPersistentCurrentTrack()) {
        return;
    }

    ag_playback_snapshot snapshot{};
    const ag_result snapshotResult = ag_player_snapshot(player_, &snapshot);
    if (snapshotResult != AG_OK) {
        runCommand(snapshotResult);
        return;
    }
    if (snapshot.track_index >= static_cast<size_t>(queueTrackIds_.size())
        || queueTrackIds_.at(static_cast<qsizetype>(snapshot.track_index))
            != currentTrackId_) {
        return;
    }

    cueHeld_ = true;
    const qint64 activeCue = cuePositionMs();
    const State activeState = toState(snapshot.state);
    if (activeState == Playing) {
        const ag_result pauseResult = ag_player_pause(player_);
        runCommand(pauseResult);
        if (pauseResult == AG_OK) {
            seek(activeCue >= 0 ? activeCue : 0);
        }
        return;
    }
    if (activeState != Paused && activeState != Stopped) {
        cueHeld_ = false;
        return;
    }

    constexpr qint64 cuePositionToleranceMs = 2;
    if (activeCue >= 0
        && qAbs(snapshot.position_ms - activeCue)
            <= cuePositionToleranceMs) {
        cueAuditioning_ = true;
        cueAuditionTrackId_ = currentTrackId_;
        cueAuditionReturnMs_ = activeCue;
        emit cueChanged();
        const ag_result playResult = ag_player_play(player_);
        runCommand(playResult);
        if (playResult != AG_OK) {
            invalidateCueHoldForTrackChange();
        }
        return;
    }

    DeckState& current = deckStates_[currentTrackId_];
    automaticCuePositionMs_ = -1;
    automaticCueSuppressed_ = true;
    current.cuePositionMs = std::max(qint64{0}, snapshot.position_ms);
    if (!saveDeckStateStore()) {
        setErrorMessage(QStringLiteral("Unable to save deck state"));
    }
    emit cueChanged();
}

void PlaybackController::cueRelease()
{
    finishCueAudition(true);
}

void PlaybackController::cancelCue()
{
    finishCueAudition(true);
}

void PlaybackController::clearCue()
{
    finishCueAudition(true);
    if (!hasPersistentCurrentTrack()) return;
    cancelBeatGridAutoPosition();
    const bool hadCue = cuePositionMs() >= 0;
    automaticCuePositionMs_ = -1;
    automaticCueSuppressed_ = true;
    auto current = deckStates_.find(currentTrackId_);
    if (current != deckStates_.end() && current->cuePositionMs >= 0) {
        current->cuePositionMs = -1;
        if (deckStateIsEmpty(*current)) deckStates_.erase(current);
        if (!saveDeckStateStore()) {
            setErrorMessage(QStringLiteral("Unable to save deck state"));
        }
    }
    if (hadCue) emit cueChanged();
}

void PlaybackController::jumpToCue()
{
    cancelBeatGridAutoPosition();
    if (player_ == nullptr || editorOutputOwned_
        || !hasPersistentCurrentTrack()) {
        return;
    }
    ag_playback_snapshot snapshot{};
    const ag_result snapshotResult = ag_player_snapshot(player_, &snapshot);
    if (snapshotResult != AG_OK) {
        runCommand(snapshotResult);
        return;
    }
    if (snapshot.track_index >= static_cast<size_t>(queueTrackIds_.size())
        || queueTrackIds_.at(static_cast<qsizetype>(snapshot.track_index))
            != currentTrackId_) {
        return;
    }
    finishCueAudition(false);
    const qint64 target = std::max(qint64{0}, cuePositionMs());
    const ag_result pauseResult = ag_player_pause(player_);
    runCommand(pauseResult);
    if (pauseResult == AG_OK) seek(target);
}

void PlaybackController::activateHotCue(const int slot)
{
    cancelBeatGridAutoPosition();
    bool accepted = true;
    emit playbackRequested(&accepted);
    if (!accepted) return;
    if (slot < 0 || slot >= 8 || player_ == nullptr || editorOutputOwned_
        || !hasPersistentCurrentTrack()) {
        return;
    }

    ag_playback_snapshot snapshot{};
    const ag_result snapshotResult = ag_player_snapshot(player_, &snapshot);
    if (snapshotResult != AG_OK) {
        runCommand(snapshotResult);
        return;
    }
    if (snapshot.track_index >= static_cast<size_t>(queueTrackIds_.size())
        || queueTrackIds_.at(static_cast<qsizetype>(snapshot.track_index))
            != currentTrackId_) {
        return;
    }

    DeckState& current = deckStates_[currentTrackId_];
    const qint64 savedPosition = current.hotCuePositions[slot];
    if (savedPosition >= 0) {
        seek(savedPosition);
        play();
        return;
    }
    current.hotCuePositions[slot] = std::max(qint64{0}, snapshot.position_ms);
    if (!saveDeckStateStore()) {
        setErrorMessage(QStringLiteral("Unable to save deck state"));
    }
    emit hotCuePositionsChanged();
}

void PlaybackController::clearHotCue(const int slot)
{
    if (slot < 0 || slot >= 8 || !hasPersistentCurrentTrack()) return;
    DeckState& current = deckStates_[currentTrackId_];
    if (current.hotCuePositions[slot] < 0) return;
    current.hotCuePositions[slot] = -1;
    if (deckStateIsEmpty(current)) deckStates_.remove(currentTrackId_);
    if (!saveDeckStateStore()) {
        setErrorMessage(QStringLiteral("Unable to save deck state"));
    }
    emit hotCuePositionsChanged();
}

void PlaybackController::setBeatGridFirstBeat()
{
    cancelBeatGridAutoPosition();
    if (!hasPersistentCurrentTrack()) return;
    ag_playback_snapshot snapshot{};
    if (ag_player_snapshot(player_, &snapshot) != AG_OK
        || snapshot.track_index >= static_cast<size_t>(queueTrackIds_.size())
        || queueTrackIds_.at(static_cast<qsizetype>(snapshot.track_index))
               != currentTrackId_) return;
    DeckState& current = deckStates_[currentTrackId_];
    preserveAutomaticBeatGrid(current);
    current.beatGridOffsetMs = std::max(qint64{0}, qint64(snapshot.position_ms));
    current.beatGridCalibrated = true;
    syncAutomaticCueToBeatGrid(true);
    if (!saveDeckStateStore()) {
        setErrorMessage(QStringLiteral("Unable to save deck state"));
    }
    emit beatGridChanged();
}

void PlaybackController::nudgeBeatGrid(const qint64 deltaMs)
{
    cancelBeatGridAutoPosition();
    if (!hasPersistentCurrentTrack() || deltaMs == 0) return;
    DeckState& current = deckStates_[currentTrackId_];
    preserveAutomaticBeatGrid(current);
    if (deltaMs > 0
        && current.beatGridOffsetMs
            > std::numeric_limits<qint64>::max() - deltaMs) {
        current.beatGridOffsetMs = std::numeric_limits<qint64>::max();
    } else if (deltaMs < 0
               && current.beatGridOffsetMs
                   < std::numeric_limits<qint64>::min() - deltaMs) {
        current.beatGridOffsetMs = std::numeric_limits<qint64>::min();
    } else {
        current.beatGridOffsetMs += deltaMs;
    }
    current.beatGridCalibrated = true;
    syncAutomaticCueToBeatGrid(true);
    if (!saveDeckStateStore()) {
        setErrorMessage(QStringLiteral("Unable to save deck state"));
    }
    emit beatGridChanged();
}

void PlaybackController::setBeatGridBpm(const double bpm)
{
    cancelBeatGridAutoPosition();
    if (!hasPersistentCurrentTrack() || !validBeatGridBpm(bpm)) return;
    DeckState& current = deckStates_[currentTrackId_];
    if (qFuzzyCompare(current.beatGridBpmOverride + 1.0, bpm + 1.0)
        && current.beatGridCalibrated) {
        return;
    }
    preserveAutomaticBeatGrid(current);
    current.beatGridBpmOverride = bpm;
    current.beatGridCalibrated = true;
    syncAutomaticCueToBeatGrid(true);
    if (!saveDeckStateStore()) {
        setErrorMessage(QStringLiteral("Unable to save deck state"));
    }
    emit beatGridChanged();
}

void PlaybackController::preserveAutomaticBeatGrid(DeckState& deck)
{
    if (deck.beatGridCalibrated) return;
    deck.beatGridOffsetMs = beatGridOffsetMs();
    // Freeze the displayed tempo with the manual anchor, so later metadata
    // updates/reopening cannot silently move a grid the user already aligned.
    if (!validBeatGridBpm(deck.beatGridBpmOverride))
        deck.beatGridBpmOverride = beatGridBpm();
}

void PlaybackController::syncAutomaticCueToBeatGrid(const bool reliable)
{
    if (automaticCuePositionMs_ < 0
        || currentDeckState().cuePositionMs >= 0) return;
    qint64 nextCue = -1;
    if (reliable) {
        const qint64 target = beatGridOffsetMs();
        const qint64 duration = std::max(durationMs_, beatGridWaveformDurationMs_);
        if (target >= 0 && (duration <= 0 || target < duration)) {
            nextCue = target;
        }
    }
    if (automaticCuePositionMs_ == nextCue) return;
    automaticCuePositionMs_ = nextCue;
    emit cueChanged();
}

void PlaybackController::applyBeatGridWaveform(const QString& trackId,
                                              const double bpm,
                                              const qint64 durationMs,
                                              const QVariantList& peaks)
{
    if (trackId.isEmpty() || trackId != currentTrackId_
        || durationMs <= 0 || peaks.isEmpty()) return;
    const double waveformBpm = validBeatGridBpm(bpm) ? bpm : 0.0;
    if (beatGridWaveformDurationMs_ == durationMs
        && beatGridWaveformBpm_ == waveformBpm && beatGridPeaks_ == peaks) {
        tryAlignBeatGridStart();
        return;
    }
    beatGridPeaks_ = peaks;
    beatGridWaveformDurationMs_ = durationMs;
    beatGridWaveformBpm_ = waveformBpm;
    refreshAutomaticBeatGrid();
    emit beatGridChanged();
    tryAlignBeatGridStart();
}

void PlaybackController::setBeatGridAutoPositionEnabled(const bool enabled)
{
    if (beatGridAutoPositionEnabled_ == enabled) return;
    beatGridAutoPositionEnabled_ = enabled;
    // Entering professional mode does not re-arm an already loaded song.
    if (!enabled) cancelBeatGridAutoPosition();
}

void PlaybackController::cancelBeatGridAutoPosition()
{
    const bool suppressAutomaticCue = beatGridAutoPositionPending_;
    beatGridAutoPositionPending_ = false;
    // A user can seek between core queue selection and the next UI poll.
    // Remember the actual target so that poll cannot re-arm it afterward.
    ag_playback_snapshot snapshot{};
    if (player_ && ag_player_snapshot(player_, &snapshot) == AG_OK
        && snapshot.track_index < static_cast<size_t>(queueTrackIds_.size())) {
        const QString targetTrackId =
            queueTrackIds_.at(static_cast<qsizetype>(snapshot.track_index));
        beatGridAutoPositionCancelledTrackId_ = targetTrackId;
        if (suppressAutomaticCue && targetTrackId == currentTrackId_) {
            automaticCueSuppressed_ = true;
        }
    }
}

void PlaybackController::tryAlignBeatGridStart()
{
    if (!beatGridAutoPositionEnabled_ || !beatGridAutoPositionPending_
        || !hasPersistentCurrentTrack() || editorOutputOwned_
        || cueHeld_ || cueAuditioning_ || scratchActive_) return;
    const auto deck = currentDeckState();
    if (!deck.beatGridCalibrated && !automaticBeatGrid_.reliable) return;
    ag_playback_snapshot snapshot{};
    if (ag_player_snapshot(player_, &snapshot) != AG_OK
        || snapshot.track_index >= static_cast<size_t>(queueTrackIds_.size())
        || queueTrackIds_.at(static_cast<qsizetype>(snapshot.track_index))
            != currentTrackId_) return;
    beatGridAutoPositionPending_ = false;
    const qint64 target = beatGridOffsetMs();
    const qint64 duration = std::max(durationMs_, qint64(snapshot.duration_ms));
    // Malformed/out-of-range saved anchors must not seek to the end of a song.
    if (target < 0 || target >= duration) return;
    if (deck.cuePositionMs < 0 && !automaticCueSuppressed_) {
        automaticCuePositionMs_ = target;
        emit cueChanged();
    }
    // seek preserves the core's stopped/paused/playing state and publishes the
    // committed source position used by the centered rolling playhead.
    seek(target);
}

void PlaybackController::refreshAutomaticBeatGrid()
{
    automaticBeatGrid_ = estimateBeatGrid(beatGridPeaks_,
        beatGridWaveformDurationMs_, validBeatGridBpm(sourceBpm_)
            ? sourceBpm_ : beatGridWaveformBpm_);
}

void PlaybackController::resetBeatGrid()
{
    cancelBeatGridAutoPosition();
    if (!hasPersistentCurrentTrack()) return;
    DeckState& current = deckStates_[currentTrackId_];
    if (!validBeatGridBpm(current.beatGridBpmOverride)
        && current.beatGridOffsetMs == 0 && !current.beatGridCalibrated) {
        return;
    }
    current.beatGridBpmOverride = 0.0;
    current.beatGridOffsetMs = 0;
    current.beatGridCalibrated = false;
    const bool restoreAutomaticCue = automaticBeatGrid_.reliable;
    if (deckStateIsEmpty(current)) {
        deckStates_.remove(currentTrackId_);
    }
    syncAutomaticCueToBeatGrid(restoreAutomaticCue);
    if (!saveDeckStateStore()) {
        setErrorMessage(QStringLiteral("Unable to save deck state"));
    }
    emit beatGridChanged();
}

bool PlaybackController::applyWaveformDuration(const QString& trackId,
                                               qint64 durationMs)
{
    if (trackId.isEmpty() || trackId != currentTrackId_ || durationMs <= 0) {
        return false;
    }
    if (player_ == nullptr) {
        return false;
    }
    // Full PCM analysis counts the exact decoded frames. Container metadata
    // can include encoder delay/padding, which stretches every beat on the
    // waveform when it is used as the visual clock.
    const ag_result result = ag_player_set_duration_ms(player_, durationMs);
    if (result != AG_OK) {
        runCommand(result);
        return false;
    }
    if (durationMs_ != durationMs) {
        durationMs_ = durationMs;
        emit durationMsChanged();
    }
    if (positionMs_ > durationMs_) {
        positionMs_ = durationMs_;
        emit positionMsChanged();
    }
    if (selectionEndMs_ > selectionStartMs_) {
        const bool loopEnabled = selectionLoopEnabled_;
        if (!setSelection(selectionStartMs_, selectionEndMs_, loopEnabled)) {
            clearSelection();
        }
    }
    return true;
}

bool PlaybackController::setSelection(qint64 startMs,
                                      qint64 endMs,
                                      bool loopEnabled)
{
    if (durationMs_ <= 0) {
        return false;
    }
    const qint64 low = std::clamp(
        std::min(startMs, endMs), qint64{0}, durationMs_);
    qint64 high = std::clamp(
        std::max(startMs, endMs), qint64{0}, durationMs_);
    constexpr qint64 minimumSelectionMs = 100;
    if (high - low < minimumSelectionMs) {
        high = std::min(durationMs_, low + minimumSelectionMs);
        if (high - low < minimumSelectionMs) {
            startMs = std::max(qint64{0}, high - minimumSelectionMs);
        } else {
            startMs = low;
        }
    } else {
        startMs = low;
    }
    if (high - startMs < minimumSelectionMs) {
        return false;
    }

    const bool startChanged = selectionStartMs_ != startMs;
    const bool endChanged = selectionEndMs_ != high;
    const bool loopChanged = selectionLoopEnabled_ != loopEnabled;
    selectionStartMs_ = startMs;
    selectionEndMs_ = high;
    selectionLoopEnabled_ = loopEnabled;
    if (startChanged) {
        emit selectionStartMsChanged();
    }
    if (endChanged) {
        emit selectionEndMsChanged();
    }
    if (loopChanged) {
        emit selectionLoopEnabledChanged();
    }
    return true;
}

void PlaybackController::next()
{
    bool accepted = true;
    emit playbackRequested(&accepted);
    if (!accepted) return;
    if (editorOutputOwned_) return;
    const ag_result result = player_ != nullptr
        ? ag_player_next(player_) : AG_INVALID_ARGUMENT;
    runCommand(result);
    if (result == AG_OK) invalidateCueHoldForTrackChange();
}

void PlaybackController::previous()
{
    bool accepted = true;
    emit playbackRequested(&accepted);
    if (!accepted) return;
    if (editorOutputOwned_) return;
    const ag_result result = player_ != nullptr
        ? ag_player_previous(player_) : AG_INVALID_ARGUMENT;
    runCommand(result);
    if (result == AG_OK) invalidateCueHoldForTrackChange();
}

bool PlaybackController::queueNext(const QString& trackId)
{
    if (player_ == nullptr || library_.isNull()) {
        setErrorMessage(QStringLiteral("Playback core is unavailable"));
        return false;
    }
    const int row = library_->indexForTrackId(trackId);
    if (row < 0 || !library_->tracks().at(row).available) {
        return false;
    }

    const QByteArray utf8Path = library_->tracks().at(row).path.toUtf8();
    const ag_result result = ag_player_queue_next(player_, utf8Path.constData());
    runCommand(result);
    if (result != AG_OK) {
        return false;
    }

    int current = queueTrackIds_.indexOf(currentTrackId_);
    const int existing = queueTrackIds_.indexOf(trackId);
    if (existing >= 0) {
        queueTrackIds_.removeAt(existing);
        if (activeScopeSize_ > 0 && existing < activeScopeSize_) {
            --activeScopeSize_;
        }
        if (existing < current) {
            --current;
        }
    }
    if (activeScopeSize_ > 0 && current >= 0 && current < activeScopeSize_) {
        ++activeScopeSize_;
    }
    queueTrackIds_.insert(
        std::min(current + 1, static_cast<int>(queueTrackIds_.size())),
        trackId);
    emit queueTrackIdsChanged();
    return true;
}

bool PlaybackController::restoreQueue(const QStringList& trackIds,
                                      const QString& currentTrackId)
{
    if (player_ == nullptr || library_.isNull()) {
        return false;
    }

    std::vector<QByteArray> utf8Paths;
    std::vector<const char*> paths;
    QStringList validIds;
    utf8Paths.reserve(static_cast<size_t>(trackIds.size()));
    paths.reserve(static_cast<size_t>(trackIds.size()));
    validIds.reserve(trackIds.size());
    size_t currentIndex = 0;
    for (const QString& trackId : trackIds) {
        const int row = library_->indexForTrackId(trackId);
        if (row < 0) {
            continue;
        }
        const TrackRecord& track = library_->tracks().at(row);
        if (!track.available || track.path.isEmpty()) {
            continue;
        }
        if (trackId == currentTrackId) {
            currentIndex = paths.size();
        }
        utf8Paths.push_back(track.path.toUtf8());
        paths.push_back(utf8Paths.back().constData());
        validIds.append(trackId);
    }
    if (paths.empty()) {
        return false;
    }
    if (!validIds.contains(currentTrackId)) {
        currentIndex = 0;
    }

    const ag_result result =
        ag_player_set_queue(player_, paths.data(), paths.size(), currentIndex);
    runCommand(result);
    if (result != AG_OK) {
        return false;
    }
    invalidateCueHoldForTrackChange();
    queueTrackIds_ = std::move(validIds);
    activeScopeSize_ = queueTrackIds_.size();
    activeScopeAllowsFallback_ = false;
    emit queueTrackIdsChanged();
    pollSnapshot();
    return true;
}

bool PlaybackController::playTrackIds(const QStringList& trackIds,
                                      const QString& currentTrackId)
{
    if (player_ == nullptr || library_.isNull() || currentTrackId.isEmpty()) {
        return false;
    }

    QStringList scopeIds;
    scopeIds.reserve(trackIds.size());
    QSet<QString> queuedIds;
    queuedIds.reserve(trackIds.size());
    for (const QString& trackId : trackIds) {
        if (queuedIds.contains(trackId)) {
            continue;
        }
        const int row = library_->indexForTrackId(trackId);
        if (row >= 0 && library_->tracks().at(row).available
            && !library_->tracks().at(row).path.isEmpty()) {
            scopeIds.append(trackId);
            queuedIds.insert(trackId);
        }
    }
    const int current = scopeIds.indexOf(currentTrackId);
    if (current < 0) {
        return false;
    }
    if (current > 0) {
        scopeIds = scopeIds.mid(current) + scopeIds.mid(0, current);
    }

    constexpr int minimumStrictScope = 5;
    QStringList queueIds = scopeIds;
    const bool allowFallback = scopeIds.size() < minimumStrictScope;
    if (allowFallback) {
        for (const TrackRecord& track : library_->tracks()) {
            if (track.available && !track.path.isEmpty()
                && !queuedIds.contains(track.trackId)) {
                queueIds.append(track.trackId);
                queuedIds.insert(track.trackId);
            }
        }
    }

    std::vector<QByteArray> utf8Paths;
    std::vector<const char*> paths;
    utf8Paths.reserve(static_cast<size_t>(queueIds.size()));
    paths.reserve(static_cast<size_t>(queueIds.size()));
    for (const QString& trackId : queueIds) {
        const int row = library_->indexForTrackId(trackId);
        utf8Paths.push_back(library_->tracks().at(row).path.toUtf8());
        paths.push_back(utf8Paths.back().constData());
    }

    bool accepted = true;
    emit playbackRequested(&accepted);
    if (!accepted || editorOutputOwned_) return false;
    applyReplayGainForTrack(currentTrackId);
    const ag_result result = ag_player_set_scoped_queue(
        player_, paths.data(), paths.size(), 0U,
        static_cast<size_t>(scopeIds.size()), allowFallback ? 1 : 0);
    runCommand(result);
    if (result != AG_OK) {
        return false;
    }
    invalidateCueHoldForTrackChange();
    queueTrackIds_ = std::move(queueIds);
    activeScopeSize_ = scopeIds.size();
    activeScopeAllowsFallback_ = allowFallback;
    emit queueTrackIdsChanged();

    const ag_result playResult = ag_player_play(player_);
    runCommand(playResult);
    if (playResult != AG_OK) {
        return false;
    }
    library_->markPlayed(currentTrackId);
    lastHistoryTrackId_ = currentTrackId;
    pollSnapshot();
    return true;
}

void PlaybackController::setVolume(float volume)
{
    runCommand(player_ != nullptr ? ag_player_set_volume(player_, volume) : AG_INVALID_ARGUMENT);
}

void PlaybackController::volumeUp(float step)
{
    const float target = std::min(1.0F, volume_ + step);
    setVolume(target);
}

void PlaybackController::volumeDown(float step)
{
    const float target = std::max(0.0F, volume_ - step);
    setVolume(target);
}

void PlaybackController::toggleMuted()
{
    runCommand(player_ != nullptr ? ag_player_set_muted(player_, muted_ ? 0 : 1)
                                  : AG_INVALID_ARGUMENT);
}

void PlaybackController::cycleMode()
{
    setMode(static_cast<Mode>((static_cast<int>(mode_) + 1) % 4));
}

void PlaybackController::setMode(Mode mode)
{
    if (mode < Sequential || mode > RepeatAll) {
        return;
    }
    runCommand(player_ != nullptr ? ag_player_set_mode(player_, toCoreMode(mode))
                                  : AG_INVALID_ARGUMENT);
}

bool PlaybackController::prepareRow(int row)
{
    if (player_ == nullptr || library_ == nullptr || row < 0 ||
        row >= library_->tracks().size() || !library_->tracks().at(row).available) {
        return false;
    }

    bool accepted = true;
    emit playbackRequested(&accepted);
    if (!accepted) return false;
    if (editorOutputOwned_) {
        releaseEditorOutput();
        if (editorOutputOwned_) return false;
    }
    std::vector<QByteArray> utf8Paths;
    std::vector<const char*> paths;
    QStringList trackIds;
    const auto capacity = static_cast<size_t>(library_->tracks().size());
    utf8Paths.reserve(capacity);
    paths.reserve(capacity);
    trackIds.reserve(library_->tracks().size());
    size_t requestedIndex = 0;
    for (int index = 0; index < library_->tracks().size(); ++index) {
        const TrackRecord& track = library_->tracks().at(index);
        if (!track.available) {
            continue;
        }
        if (index == row) {
            requestedIndex = paths.size();
        }
        utf8Paths.push_back(track.path.toUtf8());
        paths.push_back(utf8Paths.back().constData());
        trackIds.append(track.trackId);
    }

    applyReplayGainForTrack(library_->tracks().at(row).trackId);
    const ag_result queueResult =
        ag_player_set_queue(player_, paths.data(), paths.size(), requestedIndex);
    if (queueResult != AG_OK) {
        runCommand(queueResult);
        return false;
    }
    invalidateCueHoldForTrackChange();
    queueTrackIds_ = std::move(trackIds);
    beatGridAutoPositionCancelledTrackId_.clear();
    if (library_->tracks().at(row).trackId == currentTrackId_) {
        automaticCueSuppressed_ = false;
    }
    beatGridAutoPositionPending_ = beatGridAutoPositionEnabled_;
    activeScopeSize_ = queueTrackIds_.size();
    activeScopeAllowsFallback_ = false;
    emit queueTrackIdsChanged();
    tryAlignBeatGridStart();
    return true;
}

bool PlaybackController::setReplayGainSettings(const int mode,
                                               const bool clipProtection)
{
    if (mode < 0 || mode > 2) return false;
    replayGainMode_ = mode;
    replayGainClipProtection_ = clipProtection;
    return applyReplayGainForTrack(currentTrackId_);
}

void PlaybackController::setSpeedRatio(const double ratio)
{
    if (!std::isfinite(ratio)) return;
    const double clamped = std::clamp(ratio, 0.75, 1.50);
    if (qFuzzyCompare(speedRatio_, clamped)) return;
    if (!applyTimePitch(clamped, keepPitch_)) return;
    speedRatio_ = clamped;
    emit tempoChanged();
}

void PlaybackController::setTargetBpm(const double bpm)
{
    if (!std::isfinite(bpm) || bpm < 20.0 || bpm > 400.0
        || sourceBpm_ <= 0.0) {
        return;
    }
    setSpeedRatio(std::clamp(bpm / sourceBpm_, 0.75, 1.50));
}

void PlaybackController::resetTempo()
{
    setSpeedRatio(1.0);
}

void PlaybackController::setKeepPitch(const bool keepPitch)
{
    if (keepPitch_ == keepPitch) return;
    if (!applyTimePitch(speedRatio_, keepPitch)) return;
    keepPitch_ = keepPitch;
    emit tempoChanged();
}

bool PlaybackController::beginScratch()
{
    cancelBeatGridAutoPosition();
    if (player_ == nullptr || editorOutputOwned_) {
        runCommand(AG_INVALID_ARGUMENT);
        return false;
    }
    const ag_result result = ag_player_begin_scratch(player_);
    runCommand(result);
    if (result != AG_OK) return false;
    setErrorMessage({});
    pollSnapshot();
    return true;
}

bool PlaybackController::updateScratch(const double signedRate)
{
    if (player_ == nullptr || editorOutputOwned_
        || !std::isfinite(signedRate)
        || std::abs(signedRate)
               > static_cast<double>(std::numeric_limits<float>::max())) {
        runCommand(AG_INVALID_ARGUMENT);
        return false;
    }
    const ag_result result = ag_player_update_scratch(
        player_, static_cast<float>(signedRate));
    runCommand(result);
    if (result != AG_OK) return false;
    // This is the pointer-move hot path. Status is intentionally left to the
    // existing 17 ms poll instead of adding a second Core query per gesture.
    setErrorMessage({});
    return true;
}

bool PlaybackController::endScratch()
{
    if (player_ == nullptr || editorOutputOwned_) {
        runCommand(AG_INVALID_ARGUMENT);
        return false;
    }
    const ag_result result = ag_player_end_scratch(player_);
    runCommand(result);
    if (result != AG_OK) return false;
    setErrorMessage({});
    pollSnapshot();
    return true;
}

bool PlaybackController::cancelScratch()
{
    if (player_ == nullptr || editorOutputOwned_) {
        runCommand(AG_INVALID_ARGUMENT);
        return false;
    }
    const ag_result result = ag_player_cancel_scratch(player_);
    runCommand(result);
    if (result != AG_OK) return false;
    setErrorMessage({});
    pollSnapshot();
    return true;
}

bool PlaybackController::applyTimePitch(const double ratio,
                                        const bool keepPitch)
{
    if (player_ == nullptr || editorOutputOwned_) {
        runCommand(AG_INVALID_ARGUMENT);
        return false;
    }
    const ag_playback_time_pitch_config config{
        ratio, keepPitch ? 1 : 0};
    const ag_result result = ag_player_set_time_pitch(player_, &config);
    runCommand(result);
    return result == AG_OK;
}

void PlaybackController::syncTimePitchFromCore()
{
    if (player_ == nullptr) return;
    ag_playback_time_pitch_config config{};
    if (ag_player_get_time_pitch(player_, &config) != AG_OK) return;
    if (qFuzzyCompare(speedRatio_, config.speed_ratio)
        && keepPitch_ == (config.keep_pitch != 0)) {
        return;
    }
    speedRatio_ = config.speed_ratio;
    keepPitch_ = config.keep_pitch != 0;
    emit tempoChanged();
}

void PlaybackController::refreshSourceBpm()
{
    const double oldGridBpm = beatGridBpm();
    const qint64 oldGridOffset = beatGridOffsetMs();
    const bool oldEstimated = beatGridEstimatedBpm();
    double nextBpm = 0.0;
    if (library_ != nullptr && !currentTrackId_.isEmpty()) {
        const TrackRecord* const track = library_->recordForId(currentTrackId_);
        if (track != nullptr && std::isfinite(track->bpm)
            && track->bpm >= 20.0 && track->bpm <= 400.0) {
            nextBpm = track->bpm;
        }
    }
    if (qFuzzyCompare(sourceBpm_ + 1.0, nextBpm + 1.0)) return;
    sourceBpm_ = nextBpm;
    refreshAutomaticBeatGrid();
    emit tempoChanged();
    if (!qFuzzyCompare(oldGridBpm + 1.0, beatGridBpm() + 1.0)
        || oldGridOffset != beatGridOffsetMs()
        || oldEstimated != beatGridEstimatedBpm()) {
        emit beatGridChanged();
    }
    tryAlignBeatGridStart();
}

bool PlaybackController::validBeatGridBpm(const double bpm) noexcept
{
    return std::isfinite(bpm) && bpm >= 20.0 && bpm <= 400.0;
}

bool PlaybackController::deckStateIsEmpty(const DeckState& deck) noexcept
{
    return deck.cuePositionMs < 0
        && !validBeatGridBpm(deck.beatGridBpmOverride)
        && deck.beatGridOffsetMs == 0 && !deck.beatGridCalibrated
        && std::none_of(deck.hotCuePositions.cbegin(),
                        deck.hotCuePositions.cend(),
                        [](const qint64 position) { return position >= 0; });
}

QString PlaybackController::deckStateFilePath() const
{
    return QDir(QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation)).filePath(
            QStringLiteral("deck-state.json"));
}

PlaybackController::DeckState PlaybackController::currentDeckState() const noexcept
{
    if (currentTrackId_.isEmpty()) return {};
    return deckStates_.value(currentTrackId_);
}

bool PlaybackController::hasPersistentCurrentTrack() const noexcept
{
    return library_ != nullptr && !currentTrackId_.isEmpty()
        && library_->recordForId(currentTrackId_) != nullptr;
}

void PlaybackController::loadDeckStateStore()
{
    QFile input(deckStateFilePath());
    if (!input.open(QIODevice::ReadOnly)) return;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(
        input.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError
        || !document.isObject()) {
        return;
    }
    const QJsonObject tracks = document.object()
        .value(QStringLiteral("tracks")).toObject();
    for (auto iterator = tracks.constBegin(); iterator != tracks.constEnd();
         ++iterator) {
        if (!iterator.value().isObject() || iterator.key().isEmpty()) continue;
        const QJsonObject saved = iterator.value().toObject();
        DeckState deck;
        const qint64 cue = saved.value(
            QStringLiteral("cuePositionMs")).toInteger(-1);
        if (cue >= 0) deck.cuePositionMs = cue;
        const double bpm = saved.value(
            QStringLiteral("beatGridBpm")).toDouble(0.0);
        if (validBeatGridBpm(bpm)) deck.beatGridBpmOverride = bpm;
        deck.beatGridOffsetMs = saved.value(
            QStringLiteral("beatGridOffsetMs")).toInteger(0);
        deck.beatGridCalibrated = saved.value(
            QStringLiteral("beatGridCalibrated")).toBool(false);
        const QJsonArray hotCues = saved.value(
            QStringLiteral("hotCuePositions")).toArray();
        const qsizetype hotCueCount = std::min(
            hotCues.size(), static_cast<qsizetype>(deck.hotCuePositions.size()));
        for (qsizetype slot = 0; slot < hotCueCount; ++slot) {
            const qint64 position = hotCues.at(slot).toInteger(-1);
            if (position >= 0) {
                deck.hotCuePositions[static_cast<size_t>(slot)] =
                    position;
            }
        }
        if (!deckStateIsEmpty(deck)) {
            deckStates_.insert(iterator.key(), deck);
        }
    }
}

bool PlaybackController::saveDeckStateStore()
{
    const QString path = deckStateFilePath();
    const QFileInfo destination(path);
    if (!QDir().mkpath(destination.absolutePath())) return false;

    QJsonObject tracks;
    QStringList trackIds = deckStates_.keys();
    std::sort(trackIds.begin(), trackIds.end());
    for (const QString& trackId : trackIds) {
        const DeckState deck = deckStates_.value(trackId);
        QJsonObject saved;
        if (deck.cuePositionMs >= 0) {
            saved.insert(QStringLiteral("cuePositionMs"), deck.cuePositionMs);
        }
        if (validBeatGridBpm(deck.beatGridBpmOverride)) {
            saved.insert(QStringLiteral("beatGridBpm"),
                         deck.beatGridBpmOverride);
        }
        if (deck.beatGridOffsetMs != 0) {
            saved.insert(QStringLiteral("beatGridOffsetMs"),
                         deck.beatGridOffsetMs);
        }
        if (deck.beatGridCalibrated) {
            saved.insert(QStringLiteral("beatGridCalibrated"), true);
        }
        QJsonArray hotCues;
        const bool hasHotCues = std::any_of(
            deck.hotCuePositions.cbegin(), deck.hotCuePositions.cend(),
            [](const qint64 position) { return position >= 0; });
        if (hasHotCues) {
            for (const qint64 position : deck.hotCuePositions) {
                hotCues.push_back(position);
            }
            saved.insert(QStringLiteral("hotCuePositions"), hotCues);
        }
        if (!saved.isEmpty()) tracks.insert(trackId, saved);
    }
    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("tracks"), tracks);
    const QByteArray payload = QJsonDocument(root).toJson(
        QJsonDocument::Compact);

    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly)
        || output.write(payload) != payload.size()) {
        output.cancelWriting();
        return false;
    }
    return output.commit();
}

void PlaybackController::invalidateCueHoldForTrackChange()
{
    cueHeld_ = false;
    if (!cueAuditioning_) {
        cueAuditionTrackId_.clear();
        cueAuditionReturnMs_ = -1;
        return;
    }
    cueAuditioning_ = false;
    cueAuditionTrackId_.clear();
    cueAuditionReturnMs_ = -1;
    emit cueChanged();
}

void PlaybackController::finishCueAudition(const bool returnToCue)
{
    if (!cueHeld_) return;
    cueHeld_ = false;
    const bool wasAuditioning = cueAuditioning_;
    const QString auditionTrack = cueAuditionTrackId_;
    const qint64 returnPosition = cueAuditionReturnMs_;
    cueAuditioning_ = false;
    cueAuditionTrackId_.clear();
    cueAuditionReturnMs_ = -1;
    if (wasAuditioning) emit cueChanged();
    if (!returnToCue || !wasAuditioning || player_ == nullptr
        || auditionTrack != currentTrackId_ || returnPosition < 0) {
        return;
    }
    ag_playback_snapshot snapshot{};
    if (ag_player_snapshot(player_, &snapshot) != AG_OK
        || snapshot.track_index >= static_cast<size_t>(queueTrackIds_.size())
        || queueTrackIds_.at(static_cast<qsizetype>(snapshot.track_index))
            != auditionTrack) {
        return;
    }
    const ag_result pauseResult = ag_player_pause(player_);
    runCommand(pauseResult);
    if (pauseResult == AG_OK) seek(returnPosition);
}

bool PlaybackController::applyReplayGainForTrack(const QString& trackId)
{
    if (player_ == nullptr) return false;
    float gainDb = 0.0F;
    float peak = 0.0F;
    bool warning = false;
    if (replayGainMode_ != 0 && library_ != nullptr && !trackId.isEmpty()) {
        const int row = library_->indexForTrackId(trackId);
        if (row >= 0) {
            const TrackRecord& track = library_->tracks().at(row);
            if (track.replayGainScanned) {
                gainDb = static_cast<float>(replayGainMode_ == 2
                                                ? track.replayGainAlbumDb
                                                : track.replayGainTrackDb);
                peak = static_cast<float>(track.replayPeak);
                warning = peak > 0.0F
                          && peak * std::pow(10.0F, gainDb / 20.0F) > 1.0F;
            }
        }
    }
    const bool ok = ag_player_set_replay_gain(
                        player_, gainDb, peak,
                        replayGainClipProtection_ ? 1 : 0) == AG_OK;
    if (replayGainClippingWarning_ != warning) {
        replayGainClippingWarning_ = warning;
        emit replayGainClippingWarningChanged();
    }
    return ok;
}

void PlaybackController::loadRow(int row)
{
    prepareRow(row);
}

void PlaybackController::playRow(int row)
{
    if (!prepareRow(row)) {
        return;
    }
    const ag_result playResult = ag_player_play(player_);
    runCommand(playResult);
    if (playResult == AG_OK) {
        const QString trackId = library_->tracks().at(row).trackId;
        library_->markPlayed(trackId);
        lastHistoryTrackId_ = trackId;
    }
}

void PlaybackController::toggleFavorite()
{
    if (library_ == nullptr || currentTrackId_.isEmpty()) {
        return;
    }
    for (int row = 0; row < library_->tracks().size(); ++row) {
        if (library_->tracks().at(row).trackId == currentTrackId_) {
            toggleFavorite(row);
            return;
        }
    }
}

void PlaybackController::toggleFavorite(int row)
{
    if (library_ == nullptr || row < 0 || row >= library_->tracks().size()) {
        return;
    }
    library_->setFavorite(row, !library_->tracks().at(row).favorite);
}

void PlaybackController::retryDevice()
{
    if (player_ == nullptr) {
        return;
    }
    const ag_result result = ag_player_retry_device(player_);
    if (result != AG_OK) {
        RuntimeLog::log(result, QStringLiteral("Playback"),
                        QStringLiteral("retry device failed"));
        setErrorMessage(RuntimeLog::mapResult(result));
    } else {
        setErrorMessage(QString());
    }
    pollSnapshot();
}

void PlaybackController::refreshOutputDevices()
{
    QStringList devices;
    QStringList ids;
    if (player_ != nullptr) {
        size_t count = 0U;
        if (ag_player_output_device_count(player_, &count) != AG_OK) {
            setErrorMessage(playerError(player_));
            return;
        }
        devices.reserve(static_cast<qsizetype>(count));
        ids.reserve(static_cast<qsizetype>(count));
        for (size_t index = 0U; index < count; ++index) {
            size_t idRequired = 0U;
            size_t required = 0U;
            if (ag_player_output_device_id(
                    player_, index, nullptr, 0U, &idRequired)
                    != AG_OK
                || idRequired <= 1U
                || ag_player_output_device_name(
                    player_, index, nullptr, 0U, &required)
                    != AG_OK
                || required <= 1U) {
                continue;
            }
            QByteArray id(static_cast<qsizetype>(idRequired), '\0');
            QByteArray name(static_cast<qsizetype>(required), '\0');
            if (ag_player_output_device_id(
                    player_, index, id.data(),
                    static_cast<size_t>(id.size()), &idRequired)
                    == AG_OK
                && ag_player_output_device_name(
                    player_, index, name.data(),
                    static_cast<size_t>(name.size()), &required)
                    == AG_OK) {
                devices.append(QString::fromUtf8(name.constData()));
                ids.append(QString::fromUtf8(id.constData()));
            }
        }
    }
    if (devices != outputDevices_ || ids != outputDeviceIds_) {
        outputDevices_ = devices;
        outputDeviceIds_ = ids;
        emit outputDevicesChanged();
    }
}

bool PlaybackController::setOutputDevice(const QString& deviceId,
                                         const bool exclusive)
{
    if (player_ == nullptr) {
        setErrorMessage(QStringLiteral("Playback core is unavailable"));
        return false;
    }
    const QByteArray utf8Id = deviceId.toUtf8();
    const ag_result result =
        ag_player_set_output_device(player_, utf8Id.constData(),
                                    exclusive ? 1 : 0);
    if (result != AG_OK) {
        runCommand(result);
        return false;
    }
    const bool active = ag_player_exclusive_mode_active(player_) != 0;
    if (active != exclusiveModeActive_) {
        exclusiveModeActive_ = active;
        emit exclusiveModeActiveChanged();
    }
    setErrorMessage({});
    return true;
}

bool PlaybackController::setTransitionFadeMs(const int milliseconds)
{
    if (player_ == nullptr) {
        setErrorMessage(QStringLiteral("Playback core is unavailable"));
        return false;
    }
    const ag_result result =
        ag_player_set_transition_fade_ms(player_, milliseconds);
    if (result != AG_OK) {
        runCommand(result);
        return false;
    }
    setErrorMessage({});
    return true;
}

bool PlaybackController::setMatchTrackSampleRate(const bool enabled)
{
    if (player_ == nullptr) {
        return false;
    }
    const ag_result result =
        ag_player_set_match_track_sample_rate(player_, enabled ? 1 : 0);
    if (result != AG_OK) {
        runCommand(result);
        return false;
    }
    setErrorMessage({});
    return true;
}

void PlaybackController::pollSnapshot()
{
    if (player_ == nullptr || editorOutputOwned_) {
        return;
    }
    ag_playback_snapshot snapshot{};
    const ag_result result = ag_player_snapshot(player_, &snapshot);
    if (result != AG_OK) {
        RuntimeLog::log(result, QStringLiteral("Playback"),
                        QStringLiteral("snapshot failed"));
        setErrorMessage(playerError(player_));
        return;
    }
    const bool activeExclusive =
        ag_player_exclusive_mode_active(player_) != 0;
    if (activeExclusive != exclusiveModeActive_) {
        exclusiveModeActive_ = activeExclusive;
        emit exclusiveModeActiveChanged();
    }

    const State nextState = toState(snapshot.state);
    ag_scratch_status scratch{};
    const ag_result scratchResult = ag_player_scratch_status(player_, &scratch);
    if (scratchResult != AG_OK) {
        RuntimeLog::log(scratchResult, QStringLiteral("Playback"),
                        QStringLiteral("scratch status failed"));
        runCommand(scratchResult);
        return;
    }
    const bool nextScratchActive = scratch.active != 0;
    const bool nextScratchReady = scratch.ready != 0;
    const bool nextScratchBuffering = scratch.buffering != 0;
    if (scratchActive_ != nextScratchActive
        || scratchReady_ != nextScratchReady
        || scratchBuffering_ != nextScratchBuffering) {
        scratchActive_ = nextScratchActive;
        scratchReady_ = nextScratchReady;
        scratchBuffering_ = nextScratchBuffering;
        emit scratchStatusChanged();
    }
    const int desiredPollInterval =
        nextState == Playing || nextScratchActive
            ? PollIntervalMs : IdlePollIntervalMs;
    if (pollTimer_.interval() != desiredPollInterval) {
        pollTimer_.setInterval(desiredPollInterval);
    }
    const qint64 nextTrackIndex = checkedSize(snapshot.track_index);
    const qint64 nextTrackCount = checkedSize(snapshot.track_count);
    const Mode nextMode = toMode(snapshot.mode);
    const bool nextDeviceLost = ag_player_device_lost(player_) != 0;
    syncTimePitchFromCore();
    QString nextTrackId;
    if (nextTrackIndex >= 0 && nextTrackIndex < queueTrackIds_.size()) {
        nextTrackId = queueTrackIds_.at(nextTrackIndex);
    }
    const bool trackChanged = currentTrackId_ != nextTrackId;
    if (trackChanged) {
        invalidateCueHoldForTrackChange();
        clearSelection();
    }
    if (!trackChanged && selectionLoopEnabled_ && nextState == Playing
        && !nextScratchActive
        && snapshot.position_ms >= selectionEndMs_) {
        const ag_result loopResult = ag_player_seek(player_, selectionStartMs_);
        runCommand(loopResult);
        if (loopResult == AG_OK) {
            snapshot.position_ms = selectionStartMs_;
        }
    }

    if (state_ != nextState) {
        state_ = nextState;
        emit stateChanged();
    }
    if (positionMs_ != snapshot.position_ms) {
        positionMs_ = snapshot.position_ms;
        emit positionMsChanged();
    }
    const qint64 nextDurationMs = snapshot.duration_ms;
    if (durationMs_ != nextDurationMs) {
        durationMs_ = nextDurationMs;
        emit durationMsChanged();
    }
    if (volume_ != snapshot.volume) {
        volume_ = snapshot.volume;
        emit volumeChanged();
    }
    const bool nextMuted = snapshot.muted != 0;
    if (muted_ != nextMuted) {
        muted_ = nextMuted;
        emit mutedChanged();
    }
    if (mode_ != nextMode) {
        mode_ = nextMode;
        emit modeChanged();
    }
    if (trackIndex_ != nextTrackIndex) {
        trackIndex_ = nextTrackIndex;
        emit trackIndexChanged();
    }
    if (trackCount_ != nextTrackCount) {
        trackCount_ = nextTrackCount;
        emit trackCountChanged();
    }
    if (currentTrackId_ != nextTrackId) {
        currentTrackId_ = std::move(nextTrackId);
        beatGridPeaks_.clear();
        beatGridWaveformDurationMs_ = 0;
        beatGridWaveformBpm_ = 0;
        automaticBeatGrid_ = {};
        automaticCuePositionMs_ = -1;
        automaticCueSuppressed_ =
            beatGridAutoPositionCancelledTrackId_ == currentTrackId_;
        beatGridAutoPositionPending_ = beatGridAutoPositionEnabled_
            && !automaticCueSuppressed_;
        // This marker only bridges a core track switch and its next UI poll;
        // it must not suppress a later visit to the same song.
        beatGridAutoPositionCancelledTrackId_.clear();
        applyReplayGainForTrack(currentTrackId_);
        refreshSourceBpm();
        emit currentTrackIdChanged();
        emit cueChanged();
        emit beatGridChanged();
        emit hotCuePositionsChanged();

        QString nextLyrics;
        if (!currentTrackId_.isEmpty() && library_ != nullptr) {
            const int row = library_->indexForTrackId(currentTrackId_);
            if (row >= 0) {
                nextLyrics = library_->tracks().at(row).lyrics;
            }
        }
        if (lyrics_ != nextLyrics) {
            lyrics_ = std::move(nextLyrics);
            emit lyricsChanged();
        }
    }
    if (nextState == Playing && !currentTrackId_.isEmpty()
        && currentTrackId_ != lastHistoryTrackId_ && library_ != nullptr) {
        library_->markPlayed(currentTrackId_);
        lastHistoryTrackId_ = currentTrackId_;
    }
    if (deviceLost_ != nextDeviceLost) {
        deviceLost_ = nextDeviceLost;
        emit deviceLostChanged();
    }
    if (nextDeviceLost) {
        setErrorMessage(RuntimeLog::mapResult(AG_DEVICE_ERROR));
    } else if (nextState == Error) {
        const QString detail = playerError(player_);
        setErrorMessage(detail.isEmpty() ? RuntimeLog::mapResult(AG_INTERNAL_ERROR) : detail);
    } else {
        setErrorMessage(QString());
    }
    if (nextState == Playing || nextScratchActive) {
        pollSpectrum();
    }
}

void PlaybackController::pollSpectrum()
{
    if (player_ == nullptr) {
        return;
    }
#ifdef Q_OS_MACOS
    if (qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
        const auto windows = QGuiApplication::allWindows();
        // Minimized/hidden players still play audio; no spectrum consumer needs
        // a fresh QVariantList until a window is exposed again.
        if (!windows.isEmpty() && std::none_of(windows.cbegin(), windows.cend(),
                [](const QWindow* window) { return window->isExposed(); })) {
            return;
        }
    }
#endif
    std::array<float, 128U> bins{};
    if (ag_player_spectrum(player_, bins.data(), bins.size()) != AG_OK) {
        return;
    }

    bool changed = spectrum_.size() != static_cast<qsizetype>(bins.size());
    for (std::size_t index = 0U; index < bins.size(); ++index) {
        if (!changed
            && std::abs(spectrum_.at(static_cast<qsizetype>(index)).toFloat()
                        - bins[index])
                   > 0.002F) {
            changed = true;
            break;
        }
    }
    if (changed) {
        QVariantList next;
        next.reserve(static_cast<qsizetype>(bins.size()));
        for (const float bin : bins) next.append(bin);
        spectrum_ = std::move(next);
        emit spectrumChanged();
    }
}

void PlaybackController::setErrorMessage(QString message)
{
    if (errorMessage_ != message) {
        errorMessage_ = std::move(message);
        emit errorMessageChanged();
    }
}

void PlaybackController::runCommand(int result)
{
    if (result != AG_OK) {
        const auto agRes = static_cast<ag_result>(result);
        RuntimeLog::log(agRes, QStringLiteral("Playback"),
                        player_ != nullptr ? playerError(player_)
                                           : QStringLiteral("core unavailable"));
        setErrorMessage(player_ != nullptr
                            ? RuntimeLog::mapResult(agRes)
                            : QStringLiteral("Playback core is unavailable"));
    }
}
