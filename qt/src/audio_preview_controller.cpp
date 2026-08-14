#include "audio_preview_controller.hpp"

#include "playback_controller.hpp"

#include <QFileInfo>
#include <QFutureWatcher>
#include <QTemporaryDir>
#include <QtConcurrent>

#include <algorithm>
#include <cmath>

AudioPreviewController::AudioPreviewController(
    const ag_audio_backend backend,
    PlaybackController* mainPlayback,
    QObject* parent)
    : QObject(parent),
      backend_(backend),
      mainPlayback_(mainPlayback),
      previewTempDir_(new QTemporaryDir)
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
    if (dspCancelToken_ != nullptr) {
        ag_cancel_token_cancel(dspCancelToken_);
    }
    if (dspWatcher_ != nullptr) {
        dspWatcher_->waitForFinished();
    }
    if (dspCancelToken_ != nullptr) {
        ag_cancel_token_destroy(dspCancelToken_);
        dspCancelToken_ = nullptr;
    }
    if (player_ != nullptr) {
        ag_player_stop(player_);
        ag_player_destroy(player_);
        player_ = nullptr;
    }
    delete previewTempDir_;
    previewTempDir_ = nullptr;
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

bool AudioPreviewController::processing() const noexcept
{
    return processing_;
}

double AudioPreviewController::speedRatio() const noexcept
{
    return speedRatio_;
}

int AudioPreviewController::pitchCents() const noexcept
{
    return pitchCents_;
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
            stopPlaybackAndClear();
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
        stopPlaybackAndClear();
        setError(tr("预览播放器不可用"));
        return;
    }
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        stopPlaybackAndClear();
        setError(tr("预览文件不存在"));
        return;
    }

    stopPlaybackAndClear();
    if (mainPlayback_ != nullptr
        && mainPlayback_->state() != PlaybackController::Stopped) {
        mainPlayback_->stop();
    }
    const QString absolutePath = QFileInfo(path).absoluteFilePath();
    if (!loadPlaybackPath(absolutePath, absolutePath, 0.0, true)) {
        return;
    }
    if (!hasNeutralDspParameters()) {
        scheduleDspPreview();
    }
}

void AudioPreviewController::resume()
{
    if (!hasSource()) {
        return;
    }
    if (player_ != nullptr && ag_player_play(player_) == AG_OK) {
        pollTimer_.start();
        pollSnapshot();
    }
}

void AudioPreviewController::pause()
{
    if (!hasSource()) {
        return;
    }
    if (player_ == nullptr) {
        return;
    }
    if (ag_player_pause(player_) == AG_OK) {
        pollSnapshot();
    }
}

void AudioPreviewController::stop()
{
    stopPlaybackAndClear();
}

void AudioPreviewController::seek(const qint64 positionMs)
{
    if (!hasSource()) {
        return;
    }
    const qint64 bounded = std::clamp<qint64>(
        positionMs, 0, std::max<qint64>(0, durationMs_));
    if (player_ != nullptr && ag_player_seek(player_, bounded) == AG_OK) {
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

void AudioPreviewController::setDspParameters(
    const double speedRatio,
    const int pitchCents,
    const bool keepPitch,
    const bool keepDuration,
    const bool vocalProtection,
    const bool smoothTransition)
{
    const double boundedRatio = std::clamp(speedRatio, 0.5, 2.0);
    const int boundedPitch = std::clamp(pitchCents, -1200, 1200);
    if (qFuzzyCompare(speedRatio_, boundedRatio)
        && pitchCents_ == boundedPitch
        && keepPitch_ == keepPitch
        && keepDuration_ == keepDuration
        && vocalProtection_ == vocalProtection
        && smoothTransition_ == smoothTransition) {
        return;
    }
    speedRatio_ = boundedRatio;
    pitchCents_ = boundedPitch;
    keepPitch_ = keepPitch;
    keepDuration_ = keepDuration;
    vocalProtection_ = vocalProtection;
    smoothTransition_ = smoothTransition;
    ++dspRevision_;
    emit dspParametersChanged();
    if (hasSource()) {
        scheduleDspPreview();
    }
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

void AudioPreviewController::stopPlaybackAndClear()
{
    pollTimer_.stop();
    if (player_ != nullptr) {
        ag_player_stop(player_);
    }
    clearSourceState();
}

void AudioPreviewController::clearSourceState()
{
    const bool sourceWasSet = hasSource();
    const bool stateWasSet =
        playing_ || positionMs_ != 0 || durationMs_ != 0;
    sourcePath_.clear();
    playbackPath_.clear();
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
    if (!hasSource()) {
        return;
    }
    if (player_ == nullptr) {
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

bool AudioPreviewController::loadPlaybackPath(
    const QString& logicalPath,
    const QString& playbackPath,
    const double resumeFraction,
    const bool startPlaying)
{
    if (player_ == nullptr) {
        setError(tr("预览播放器不可用"));
        return false;
    }
    pollTimer_.stop();
    ag_player_stop(player_);
    const QByteArray utf8 = playbackPath.toUtf8();
    if (ag_player_load(player_, utf8.constData()) != AG_OK) {
        stopPlaybackAndClear();
        setError(tr("无法加载预览音频"));
        return false;
    }

    const QString absoluteLogical = QFileInfo(logicalPath).absoluteFilePath();
    const bool sourceChangedValue = sourcePath_ != absoluteLogical;
    sourcePath_ = absoluteLogical;
    playbackPath_ = QFileInfo(playbackPath).absoluteFilePath();
    positionMs_ = 0;
    durationMs_ = 0;
    ag_playback_snapshot snapshot{};
    if (ag_player_snapshot(player_, &snapshot) == AG_OK) {
        durationMs_ = snapshot.duration_ms;
        if (resumeFraction > 0.0 && durationMs_ > 0) {
            ag_player_seek(
                player_,
                static_cast<qint64>(resumeFraction * durationMs_));
        }
    }
    if (startPlaying && ag_player_play(player_) != AG_OK) {
        stopPlaybackAndClear();
        setError(tr("无法开始预览播放"));
        return false;
    }
    if (sourceChangedValue) {
        emit sourceChanged();
    }
    pollTimer_.start();
    pollSnapshot();
    return true;
}

bool AudioPreviewController::hasNeutralDspParameters() const noexcept
{
    return qFuzzyCompare(speedRatio_, 1.0) && pitchCents_ == 0;
}

void AudioPreviewController::scheduleDspPreview()
{
    if (!hasSource()) {
        return;
    }
    if (dspWatcher_ != nullptr) {
        dspDirty_ = true;
        if (dspCancelToken_ != nullptr) {
            ag_cancel_token_cancel(dspCancelToken_);
        }
        return;
    }

    if (hasNeutralDspParameters()) {
        if (QFileInfo(playbackPath_).canonicalFilePath()
            == QFileInfo(sourcePath_).canonicalFilePath()) {
            return;
        }
        const double fraction = durationMs_ > 0
            ? static_cast<double>(positionMs_) / durationMs_ : 0.0;
        loadPlaybackPath(sourcePath_, sourcePath_, fraction, playing_);
        return;
    }
    if (previewTempDir_ == nullptr || !previewTempDir_->isValid()) {
        setError(tr("无法创建预览缓存目录"));
        return;
    }

    processing_ = true;
    dspDirty_ = false;
    emit processingChanged();
    const int revision = dspRevision_;
    const QString logicalSource = sourcePath_;
    const QString outputPath = previewTempDir_->filePath(
        QStringLiteral("preview-%1.wav").arg(revision));
    const int compensationCents = keepPitch_
        ? 0
        : static_cast<int>(std::lround(
              1200.0 * std::log2(speedRatio_)));
    const int effectivePitch =
        std::clamp(pitchCents_ + compensationCents, -1200, 1200);
    const int keepTempo = (keepPitch_ || keepDuration_) ? 1 : 0;
    const double pitchRate = std::pow(2.0, effectivePitch / 1200.0);
    const double tempoRatio = keepTempo
        ? speedRatio_
        : speedRatio_ / pitchRate;
    const ag_pitch_shift_options options{
        vocalProtection_ ? 1 : 0,
        smoothTransition_ ? 1 : 0,
        0};
    ag_cancel_token* const token = ag_cancel_token_create();
    if (token == nullptr) {
        processing_ = false;
        emit processingChanged();
        setError(tr("无法创建预览处理任务"));
        return;
    }
    dspCancelToken_ = token;

    auto* watcher = new QFutureWatcher<int>(this);
    dspWatcher_ = watcher;
    connect(watcher, &QFutureWatcher<int>::finished, this,
            [this, watcher, token, revision, logicalSource, outputPath]() {
        const int result = watcher->result();
        watcher->deleteLater();
        dspWatcher_.clear();
        if (dspCancelToken_ == token) {
            dspCancelToken_ = nullptr;
        }
        ag_cancel_token_destroy(token);
        processing_ = false;
        emit processingChanged();

        if (dspDirty_ || revision != dspRevision_) {
            dspDirty_ = false;
            scheduleDspPreview();
            return;
        }
        if (result != AG_OK) {
            if (result != AG_CANCELLED) {
                setError(tr("实时预览处理失败"));
            }
            return;
        }
        if (QFileInfo(sourcePath_).canonicalFilePath()
            != QFileInfo(logicalSource).canonicalFilePath()) {
            return;
        }
        const double fraction = durationMs_ > 0
            ? static_cast<double>(positionMs_) / durationMs_ : 0.0;
        loadPlaybackPath(
            logicalSource, outputPath, fraction, playing_);
    });

    watcher->setFuture(QtConcurrent::run(
        [logicalSource, outputPath, effectivePitch, keepTempo,
         tempoRatio, options, token]() {
            const QByteArray input = logicalSource.toUtf8();
            const QByteArray output = outputPath.toUtf8();
            return static_cast<int>(ag_pitch_shift_ex(
                input.constData(), output.constData(),
                effectivePitch, keepTempo, tempoRatio, "pcm_s16le",
                &options, token, nullptr, nullptr));
        }));
}
