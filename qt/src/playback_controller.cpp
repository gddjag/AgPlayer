#include "playback_controller.hpp"

#include "library_model.hpp"

#include <agplayer/c_api.h>

#include <QByteArray>

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
QString PlaybackController::errorMessage() const { return errorMessage_; }

void PlaybackController::setLibraryModel(LibraryModel* library)
{
    disconnect(playRequestedConnection_);
    library_ = library;
    if (library_ != nullptr) {
        playRequestedConnection_ = connect(
            library_, &LibraryModel::playRequested, this, &PlaybackController::playRow);
    }
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
    runCommand(player_ != nullptr ? ag_player_seek(player_, positionMs) : AG_INVALID_ARGUMENT);
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

void PlaybackController::toggleMuted()
{
    runCommand(player_ != nullptr ? ag_player_set_muted(player_, muted_ ? 0 : 1)
                                  : AG_INVALID_ARGUMENT);
}

void PlaybackController::cycleMode()
{
    const auto nextMode = static_cast<Mode>((static_cast<int>(mode_) + 1) % 3);
    runCommand(player_ != nullptr ? ag_player_set_mode(player_, toCoreMode(nextMode))
                                  : AG_INVALID_ARGUMENT);
}

void PlaybackController::playRow(int row)
{
    if (player_ == nullptr || library_ == nullptr || row < 0 ||
        row >= library_->tracks().size() || !library_->tracks().at(row).available) {
        return;
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
        return;
    }
    queueTrackIds_ = std::move(trackIds);
    runCommand(ag_player_play(player_));
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

void PlaybackController::pollSnapshot()
{
    if (player_ == nullptr) {
        return;
    }
    ag_playback_snapshot snapshot{};
    const ag_result result = ag_player_snapshot(player_, &snapshot);
    if (result != AG_OK) {
        setErrorMessage(playerError(player_));
        return;
    }

    const State nextState = toState(snapshot.state);
    const qint64 nextTrackIndex = checkedSize(snapshot.track_index);
    const qint64 nextTrackCount = checkedSize(snapshot.track_count);
    const Mode nextMode = toMode(snapshot.mode);
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
    }
    setErrorMessage(nextState == Error ? playerError(player_) : QString());
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
        setErrorMessage(player_ != nullptr ? playerError(player_)
                                           : QStringLiteral("Playback core is unavailable"));
    }
}
