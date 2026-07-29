#include "playback_controller.hpp"

#include "library_model.hpp"
#include "runtime_log.hpp"

#include <agplayer/c_api.h>

#include <QByteArray>

#include <algorithm>
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
QString PlaybackController::lyrics() const { return lyrics_; }
QString PlaybackController::errorMessage() const { return errorMessage_; }
bool PlaybackController::deviceLost() const noexcept { return deviceLost_; }
QStringList PlaybackController::outputDevices() const { return outputDevices_; }
QStringList PlaybackController::outputDeviceIds() const { return outputDeviceIds_; }
bool PlaybackController::exclusiveModeActive() const noexcept
{
    return exclusiveModeActive_;
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
    player_ = player;
    refreshOutputDevices();
}

void PlaybackController::play()
{
    runCommand(player_ != nullptr ? ag_player_play(player_) : AG_INVALID_ARGUMENT);
}

void PlaybackController::pause()
{
    runCommand(player_ != nullptr ? ag_player_pause(player_) : AG_INVALID_ARGUMENT);
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
    const qint64 durationMs = std::max<qint64>(0, snapshot.duration_ms);
    runCommand(ag_player_seek(
        player_, std::clamp(positionMs, qint64{0}, durationMs)));
}

void PlaybackController::next()
{
    runCommand(player_ != nullptr ? ag_player_next(player_) : AG_INVALID_ARGUMENT);
}

void PlaybackController::previous()
{
    runCommand(player_ != nullptr ? ag_player_previous(player_) : AG_INVALID_ARGUMENT);
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

    const ag_result queueResult =
        ag_player_set_queue(player_, paths.data(), paths.size(), requestedIndex);
    if (queueResult != AG_OK) {
        runCommand(queueResult);
        return false;
    }
    queueTrackIds_ = std::move(trackIds);
    return true;
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

void PlaybackController::pollSnapshot()
{
    if (player_ == nullptr) {
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
    if (durationMs_ != snapshot.duration_ms) {
        durationMs_ = snapshot.duration_ms;
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
