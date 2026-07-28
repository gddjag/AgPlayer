#include "audio_preview_controller.hpp"

#include <QFileInfo>

#include <algorithm>

AudioPreviewController::AudioPreviewController(const ag_audio_backend backend,
                                               QObject* parent)
    : QObject(parent)
{
    const ag_player_config config{backend, 0U};
    if (ag_player_create_with_config(&config, &player_) != AG_OK) {
        player_ = nullptr;
    } else {
        ag_player_set_volume(player_, static_cast<float>(volume_));
    }
    pollTimer_.setInterval(40);
    connect(&pollTimer_, &QTimer::timeout,
            this, &AudioPreviewController::pollSnapshot);
}

AudioPreviewController::~AudioPreviewController()
{
    pollTimer_.stop();
    if (player_ != nullptr) {
        ag_player_stop(player_);
        ag_player_destroy(player_);
        player_ = nullptr;
    }
}

bool AudioPreviewController::hasSource() const noexcept
{
    return !sourcePath_.isEmpty();
}

bool AudioPreviewController::playing() const noexcept
{
    return playing_;
}

qint64 AudioPreviewController::positionMs() const noexcept
{
    return positionMs_;
}

qint64 AudioPreviewController::durationMs() const noexcept
{
    return durationMs_;
}

double AudioPreviewController::volume() const noexcept
{
    return volume_;
}

QString AudioPreviewController::sourcePath() const
{
    return sourcePath_;
}

void AudioPreviewController::toggle(const QUrl& source)
{
    if (isCurrentSource(source)) {
        if (playing_) {
            pause();
        } else if (player_ != nullptr && ag_player_play(player_) == AG_OK) {
            pollTimer_.start();
            pollSnapshot();
        } else {
            clearSourceState();
            setError(tr("无法开始预览播放"));
        }
        return;
    }
    play(source);
}

void AudioPreviewController::play(const QUrl& source)
{
    const QString path = source.toLocalFile();
    if (player_ == nullptr) {
        clearSourceState();
        setError(tr("预览播放器不可用"));
        return;
    }
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        clearSourceState();
        setError(tr("预览文件不存在"));
        return;
    }

    pollTimer_.stop();
    ag_player_stop(player_);
    clearSourceState();
    const QByteArray utf8 = path.toUtf8();
    const ag_result loadResult = ag_player_load(player_, utf8.constData());
    if (loadResult != AG_OK) {
        setError(tr("无法加载预览音频"));
        return;
    }
    if (ag_player_play(player_) != AG_OK) {
        setError(tr("无法开始预览播放"));
        return;
    }

    sourcePath_ = QFileInfo(path).absoluteFilePath();
    positionMs_ = 0;
    durationMs_ = 0;
    emit sourceChanged();
    pollTimer_.start();
    pollSnapshot();
}

void AudioPreviewController::pause()
{
    if (player_ == nullptr || !hasSource()) {
        return;
    }
    if (ag_player_pause(player_) == AG_OK) {
        pollSnapshot();
    }
}

void AudioPreviewController::stop()
{
    pollTimer_.stop();
    if (player_ != nullptr) {
        ag_player_stop(player_);
    }
    clearSourceState();
}

void AudioPreviewController::seek(const qint64 positionMs)
{
    if (player_ == nullptr || !hasSource()) {
        return;
    }
    const qint64 bounded = std::clamp<qint64>(
        positionMs, 0, std::max<qint64>(0, durationMs_));
    if (ag_player_seek(player_, bounded) == AG_OK) {
        pollSnapshot();
    }
}

void AudioPreviewController::setVolume(const double value)
{
    const double bounded = std::clamp(value, 0.0, 1.0);
    if (qFuzzyCompare(volume_, bounded)) {
        return;
    }
    volume_ = bounded;
    if (player_ != nullptr) {
        ag_player_set_volume(player_, static_cast<float>(volume_));
    }
    emit volumeChanged();
}

bool AudioPreviewController::isCurrentSource(const QUrl& source) const
{
    if (!hasSource()) {
        return false;
    }
    const QString candidate = source.toLocalFile();
    if (candidate.isEmpty()) {
        return false;
    }
    return QFileInfo(sourcePath_).canonicalFilePath()
        == QFileInfo(candidate).canonicalFilePath();
}

void AudioPreviewController::clearSourceState()
{
    const bool sourceWasSet = hasSource();
    const bool stateWasSet =
        playing_ || positionMs_ != 0 || durationMs_ != 0;
    sourcePath_.clear();
    playing_ = false;
    positionMs_ = 0;
    durationMs_ = 0;
    if (sourceWasSet) {
        emit sourceChanged();
    }
    if (stateWasSet) {
        emit stateChanged();
    }
}

void AudioPreviewController::pollSnapshot()
{
    if (player_ == nullptr || !hasSource()) {
        return;
    }
    ag_playback_snapshot snapshot{};
    if (ag_player_snapshot(player_, &snapshot) != AG_OK) {
        return;
    }
    const bool nextPlaying = snapshot.state == AG_PLAYING;
    const qint64 nextPosition = snapshot.position_ms;
    const qint64 nextDuration = snapshot.duration_ms;
    if (playing_ == nextPlaying
        && positionMs_ == nextPosition
        && durationMs_ == nextDuration) {
        return;
    }
    playing_ = nextPlaying;
    positionMs_ = nextPosition;
    durationMs_ = nextDuration;
    if (!nextPlaying) {
        pollTimer_.stop();
    }
    emit stateChanged();
}

void AudioPreviewController::setError(const QString& message)
{
    emit errorOccurred(message);
}
