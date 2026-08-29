#include "playback_controller.hpp"

#include "library_model.hpp"
#include "runtime_log.hpp"

#include <agplayer/c_api.h>

#include <QByteArray>

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
    setLibraryModel(library);
    pollTimer_.setInterval(PollIntervalMs);
    pollTimer_.setTimerType(Qt::PreciseTimer);
    connect(&pollTimer_, &QTimer::timeout, this, &PlaybackController::pollSnapshot);
    refreshOutputDevices();
    pollSnapshot();
    pollTimer_.start();
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

void PlaybackController::setLibraryModel(LibraryModel* library)
{
    disconnect(playRequestedConnection_);
    library_ = library;
    if (library_ != nullptr) {
        playRequestedConnection_ = connect(
            library_, &LibraryModel::playRequested, this, &PlaybackController::playRow);
    }
}

void PlaybackController::setPlayer(ag_player* player)
{
    editorOutputOwned_ = false;
    editorSessionSnapshot_.reset();
    player_ = player;
    refreshOutputDevices();
}

bool PlaybackController::acquireEditorOutput() noexcept
{
    if (player_ == nullptr) return false;
    if (editorOutputOwned_) return editorSessionSnapshot_.has_value();
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
        // A fresh player and an already-stopped loaded player both report
        // AG_STOPPED.  Neither needs a stop command before the editor stream
        // replaces its source; an unloaded core rejects stop as invalid.
        // Active/error states still take the real stop path and propagate any
        // failure instead of claiming the output was acquired.
        if (snapshot.state != AG_STOPPED
            && ag_player_stop(player_) != AG_OK) {
            return false;
        }
        editorSessionSnapshot_ = std::move(saved);
        editorOutputOwned_ = true;
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

void PlaybackController::releaseEditorOutput() noexcept
{
    if (!editorOutputOwned_) return;
    (void)ag_player_stop(player_);
    editorOutputOwned_ = false;
    if (!editorSessionSnapshot_ || editorSessionSnapshot_->queueTrackIds.isEmpty()
        || library_ == nullptr) {
        editorSessionSnapshot_.reset();
        return;
    }
    try {
        const PlaybackSessionSnapshot saved = std::move(*editorSessionSnapshot_);
        editorSessionSnapshot_.reset();
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
                return;
            }
            utf8Paths.push_back(library_->tracks().at(row).path.toUtf8());
            paths.push_back(utf8Paths.back().constData());
        }
        const qsizetype current = saved.queueTrackIds.indexOf(saved.currentTrackId);
        const std::size_t startIndex = current >= 0
            ? static_cast<std::size_t>(current) : 0U;
        const std::size_t scopeSize = static_cast<std::size_t>(std::clamp<qsizetype>(
            saved.scopeSize, 1, saved.queueTrackIds.size()));
        ag_result result = ag_player_set_scoped_queue(
            player_, paths.data(), paths.size(), startIndex, scopeSize,
            saved.allowFallback ? 1 : 0);
        if (result == AG_OK) result = ag_player_set_mode(player_, toCoreMode(saved.mode));
        if (result == AG_OK && saved.positionMs > 0) {
            result = ag_player_seek(player_, saved.positionMs);
        }
        if (result == AG_OK && (saved.state == Playing || saved.state == Paused)) {
            result = ag_player_play(player_);
        }
        if (result == AG_OK && saved.state == Paused) {
            result = ag_player_pause(player_);
        }
        if (result != AG_OK) {
            runCommand(result);
            return;
        }
        queueTrackIds_ = saved.queueTrackIds;
        activeScopeSize_ = saved.scopeSize;
        activeScopeAllowsFallback_ = saved.allowFallback;
        emit queueTrackIdsChanged();
        pollSnapshot();
    } catch (...) {
        editorSessionSnapshot_.reset();
        setErrorMessage(QStringLiteral("Unable to restore the playback session"));
    }
}

void PlaybackController::play()
{
    if (editorOutputOwned_) return;
    runCommand(player_ != nullptr ? ag_player_play(player_) : AG_INVALID_ARGUMENT);
}

void PlaybackController::pause()
{
    if (editorOutputOwned_) return;
    runCommand(player_ != nullptr ? ag_player_pause(player_) : AG_INVALID_ARGUMENT);
}

void PlaybackController::stop()
{
    if (editorOutputOwned_) return;
    runCommand(player_ != nullptr ? ag_player_stop(player_) : AG_INVALID_ARGUMENT);
}

void PlaybackController::togglePlayback()
{
    if (state_ == Playing) {
        pause();
    } else {
        play();
    }
}

void PlaybackController::seek(qint64 positionMs)
{
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
    if (result == AG_OK && positionMs_ != targetMs) {
        positionMs_ = targetMs;
        emit positionMsChanged();
    }
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
    return true;
}

void PlaybackController::next()
{
    if (editorOutputOwned_) return;
    runCommand(player_ != nullptr ? ag_player_next(player_) : AG_INVALID_ARGUMENT);
}

void PlaybackController::previous()
{
    if (editorOutputOwned_) return;
    runCommand(player_ != nullptr ? ag_player_previous(player_) : AG_INVALID_ARGUMENT);
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
    for (const QString& trackId : trackIds) {
        if (scopeIds.contains(trackId)) {
            continue;
        }
        const int row = library_->indexForTrackId(trackId);
        if (row >= 0 && library_->tracks().at(row).available
            && !library_->tracks().at(row).path.isEmpty()) {
            scopeIds.append(trackId);
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
                && !queueIds.contains(track.trackId)) {
                queueIds.append(track.trackId);
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

    applyReplayGainForTrack(currentTrackId);
    const ag_result result = ag_player_set_scoped_queue(
        player_, paths.data(), paths.size(), 0U,
        static_cast<size_t>(scopeIds.size()), allowFallback ? 1 : 0);
    runCommand(result);
    if (result != AG_OK) {
        return false;
    }
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

    if (editorOutputOwned_) releaseEditorOutput();
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
    queueTrackIds_ = std::move(trackIds);
    activeScopeSize_ = queueTrackIds_.size();
    activeScopeAllowsFallback_ = false;
    emit queueTrackIdsChanged();
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
    const int desiredPollInterval =
        nextState == Playing ? PollIntervalMs : IdlePollIntervalMs;
    if (pollTimer_.interval() != desiredPollInterval) {
        pollTimer_.setInterval(desiredPollInterval);
    }
    const qint64 nextTrackIndex = checkedSize(snapshot.track_index);
    const qint64 nextTrackCount = checkedSize(snapshot.track_count);
    const Mode nextMode = toMode(snapshot.mode);
    const bool nextDeviceLost = ag_player_device_lost(player_) != 0;
    QString nextTrackId;
    if (nextTrackIndex >= 0 && nextTrackIndex < queueTrackIds_.size()) {
        nextTrackId = queueTrackIds_.at(nextTrackIndex);
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
        applyReplayGainForTrack(currentTrackId_);
        emit currentTrackIdChanged();

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
    if (nextState == Playing) {
        pollSpectrum();
    }
}

void PlaybackController::pollSpectrum()
{
    if (player_ == nullptr) {
        return;
    }
    std::array<float, 128U> bins{};
    if (ag_player_spectrum(player_, bins.data(), bins.size()) != AG_OK) {
        return;
    }

    bool changed = spectrum_.size() != static_cast<qsizetype>(bins.size());
    QVariantList next;
    next.reserve(static_cast<qsizetype>(bins.size()));
    for (std::size_t index = 0U; index < bins.size(); ++index) {
        next.append(bins[index]);
        if (!changed
            && std::abs(spectrum_.at(static_cast<qsizetype>(index)).toFloat()
                        - bins[index])
                   > 0.002F) {
            changed = true;
        }
    }
    if (changed) {
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
