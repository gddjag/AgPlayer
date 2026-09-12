#include "video_playback_controller.hpp"

#include "playback_controller.hpp"
#include "runtime_log.hpp"

#include <agplayer/c_api.h>

#include <QAbstractItemModel>
#include <QFileInfo>
#include <QMetaObject>

#include <algorithm>
#include <limits>
#include <utility>

namespace {

constexpr qint64 kClockToleranceMs = 50;

} // namespace

bool videoFrameQueueCanAdmit(const int queuedFrameCount,
                             const qint64 queuedBytes,
                             const qint64 frameBytes) noexcept
{
    if (queuedFrameCount < 0 || queuedBytes < 0 || frameBytes <= 0) {
        return false;
    }
    if (queuedFrameCount == 0) {
        return queuedBytes == 0;
    }
    return queuedFrameCount < VideoPlaybackController::MaxQueuedFrames
        && frameBytes <= VideoPlaybackController::MaxQueuedFrameBytes
        && queuedBytes
            <= VideoPlaybackController::MaxQueuedFrameBytes - frameBytes;
}

VideoPlaybackController::VideoPlaybackController(LibraryModel* library,
                                                 PlaybackController* playback,
                                                 QObject* parent)
    : QObject(parent), library_(library), playback_(playback)
{
    if (playback_ != nullptr) {
        connect(playback_, &PlaybackController::currentTrackIdChanged,
                this, &VideoPlaybackController::reevaluateCurrentTrack);
        connect(playback_, &PlaybackController::stateChanged,
                this, &VideoPlaybackController::reevaluateCurrentTrack);
        connect(playback_, &PlaybackController::positionMsChanged,
                this, &VideoPlaybackController::presentForCurrentClock);
        connect(playback_, &PlaybackController::tempoChanged,
                this, &VideoPlaybackController::presentForCurrentClock);
        connect(playback_, &PlaybackController::seekCommitted,
                this, &VideoPlaybackController::handleCommittedSeek);
    }
    if (library_ != nullptr) {
        connect(library_, &QAbstractItemModel::dataChanged, this,
                [this](const QModelIndex&, const QModelIndex&,
                       const QList<int>& roles) {
                    if (roles.isEmpty()
                        || roles.contains(LibraryModel::HasVideoRole)
                        || roles.contains(
                            LibraryModel::MetadataProbeAttemptedRole)) {
                        reevaluateCurrentTrack();
                    }
                });
        connect(library_, &QAbstractItemModel::modelReset,
                this, &VideoPlaybackController::reevaluateCurrentTrack);
        connect(library_, &QObject::destroyed, this, [this] {
            library_ = nullptr;
            stopWorkerAndClear();
        });
    }
    if (playback_ != nullptr) {
        connect(playback_, &QObject::destroyed, this, [this] {
            playback_ = nullptr;
            stopWorkerAndClear();
        });
    }
    reevaluateCurrentTrack();
}

VideoPlaybackController::~VideoPlaybackController()
{
    stopWorkerAndClear();
}

int VideoPlaybackController::queuedFrameCount() const
{
    const std::lock_guard lock(mutex_);
    return static_cast<int>(queue_.size());
}

qint64 VideoPlaybackController::queuedFrameBytes() const
{
    const std::lock_guard lock(mutex_);
    return queuedBytes_;
}

std::shared_ptr<const VideoFrameSnapshot>
VideoPlaybackController::currentFrame() const
{
    const std::lock_guard lock(mutex_);
    return displayedFrame_;
}

void VideoPlaybackController::dismiss()
{
    stopWorkerAndClear();
}

bool VideoPlaybackController::hasSupportedVideoSuffix(const QString& path)
{
    static const QSet<QString> extensions{
        QStringLiteral("mp4"), QStringLiteral("mkv"),
        QStringLiteral("webm"), QStringLiteral("mov"),
        QStringLiteral("avi"), QStringLiteral("m4v")};
    return extensions.contains(QFileInfo(path).suffix().toLower());
}

void VideoPlaybackController::reevaluateCurrentTrack()
{
    if (library_ == nullptr || playback_ == nullptr
        || playback_->state() == PlaybackController::Stopped
        || playback_->state() == PlaybackController::Error
        || playback_->currentTrackId().isEmpty()) {
        stopWorkerAndClear();
        return;
    }

    const QString trackId = playback_->currentTrackId();
    const TrackRecord* const record = library_->recordForId(trackId);
    if (record == nullptr) {
        stopWorkerAndClear();
        return;
    }
    const bool knownVideo = record->hasVideo
        || verifiedVideoTrackIds_.contains(trackId);
    const bool legacyCandidate = !knownVideo
        && !record->metadataProbeAttempted
        && hasSupportedVideoSuffix(record->path);
    if (!knownVideo && !legacyCandidate) {
        stopWorkerAndClear();
        return;
    }

    if (activeTrackId_ == trackId && worker_.joinable()) {
        if (knownVideo) setVisible(true);
        return;
    }

    std::optional<MetadataProbeClaim> claim;
    if (legacyCandidate) claim = library_->beginMetadataProbe(trackId);
    if (legacyCandidate && !claim.has_value()) {
        stopWorkerAndClear();
        return;
    }
    startWorker(*record, std::move(claim));
}

void VideoPlaybackController::startWorker(
    const TrackRecord& track, std::optional<MetadataProbeClaim> probeClaim)
{
    stopWorkerAndClear();
    activeTrackId_ = track.trackId;
    activeProbeClaim_ = probeClaim;
    setErrorMessage({});
    setLoading(true);
    if (!probeClaim.has_value()) setVisible(true);

    quint64 token = 0;
    {
        const std::lock_guard lock(mutex_);
        stopRequested_ = false;
        pendingSeek_.reset();
        decodeGeneration_++;
        workerToken_++;
        token = workerToken_;
    }
    workerRunning_ = true;
    emit diagnosticsChanged();
    worker_ = std::thread(&VideoPlaybackController::runWorker, this,
                          track.trackId, track.path, token,
                          std::move(probeClaim));
}

void VideoPlaybackController::stopWorkerAndClear()
{
    {
        const std::lock_guard lock(mutex_);
        stopRequested_ = true;
        pendingSeek_.reset();
        condition_.notify_all();
        if (decoder_ != nullptr) ag_video_decoder_cancel(decoder_);
    }
    if (worker_.joinable()) worker_.join();
    if (library_ != nullptr && activeProbeClaim_.has_value()) {
        library_->abandonMetadataProbe(*activeProbeClaim_);
    }
    activeProbeClaim_.reset();
    {
        const std::lock_guard lock(mutex_);
        workerToken_++;
        decoder_ = nullptr;
    }
    const bool wasRunning = workerRunning_;
    workerRunning_ = false;
    activeTrackId_.clear();
    clearFrames(true);
    setLoading(false);
    setVisible(false);
    setErrorMessage({});
    if (wasRunning) emit diagnosticsChanged();
}

void VideoPlaybackController::runWorker(
    QString trackId, QString path, quint64 token,
    std::optional<MetadataProbeClaim> probeClaim)
{
    bool knownVideo = !probeClaim.has_value();
    ag_video_decoder* decoder = nullptr;
    ag_result result = ag_video_decoder_create(&decoder);
    if (result == AG_OK) {
        const std::lock_guard lock(mutex_);
        if (stopRequested_ || token != workerToken_) {
            result = AG_CANCELLED;
        } else {
            decoder_ = decoder;
        }
    }
    ag_video_media_info mediaInfo{};
    mediaInfo.struct_size = sizeof(mediaInfo);
    if (result == AG_OK) {
        const QByteArray utf8 = path.toUtf8();
        result = ag_video_decoder_open_with_media_info(
            decoder, utf8.constData(), &mediaInfo);
    }
    if (probeClaim.has_value() && result != AG_CANCELLED) {
        TrackRecord probed;
        probed.path = path;
        const bool probeSucceeded = mediaInfo.valid != 0;
        probed.hasAudio = probeSucceeded && mediaInfo.has_audio != 0;
        probed.hasVideo = probeSucceeded && mediaInfo.has_video != 0;
        knownVideo = probed.hasVideo;
        bool publishProbe = true;
        {
            const std::lock_guard lock(mutex_);
            publishProbe = !stopRequested_ && token == workerToken_;
        }
        if (publishProbe) {
            QMetaObject::invokeMethod(
                this,
                [this, token, trackId, claim = probeClaim, probeSucceeded,
                 probed] {
                    handleProbeResult(token, trackId, claim,
                                      probeSucceeded, probed);
                },
                Qt::QueuedConnection);
        }
    }
    if (result != AG_OK) {
        if (result != AG_CANCELLED) {
            RuntimeLog::log(result, QStringLiteral("Video"),
                            QStringLiteral("video decoder open failed"));
            QMetaObject::invokeMethod(
                this,
                [this, token, knownVideo] {
                    handleWorkerError(
                        token, knownVideo,
                        QStringLiteral("无法解码视频画面"));
                },
                Qt::QueuedConnection);
        }
    } else {
        while (true) {
            std::optional<PendingSeek> seek;
            {
                const std::lock_guard lock(mutex_);
                if (stopRequested_ || token != workerToken_) break;
                if (pendingSeek_.has_value()) {
                    seek = pendingSeek_;
                    pendingSeek_.reset();
                }
            }
            if (seek.has_value()) {
                result = ag_video_decoder_seek(decoder, seek->positionMs);
                if (result != AG_OK) {
                    RuntimeLog::log(result, QStringLiteral("Video"),
                                    QStringLiteral("video seek failed"));
                    QMetaObject::invokeMethod(
                        this,
                        [this, token] {
                            handleWorkerError(
                                token, true,
                                QStringLiteral("无法定位视频画面"));
                        },
                        Qt::QueuedConnection);
                    std::unique_lock lock(mutex_);
                    condition_.wait(lock, [this, token] {
                        return stopRequested_ || token != workerToken_
                            || pendingSeek_.has_value();
                    });
                    continue;
                }
            }

            ag_video_frame frame{};
            frame.struct_size = sizeof(frame);
            result = ag_video_decoder_read(decoder, &frame);
            if (result != AG_OK) {
                if (result != AG_CANCELLED) {
                    RuntimeLog::log(result, QStringLiteral("Video"),
                                    QStringLiteral("video frame read failed"));
                    QMetaObject::invokeMethod(
                        this,
                        [this, token] {
                            handleWorkerError(
                                token, true,
                                QStringLiteral("无法解码视频画面"));
                        },
                        Qt::QueuedConnection);
                }
                break;
            }
            if (frame.end_of_stream != 0) break;
            if (frame.data == nullptr || frame.data_size == 0
                || frame.data_size
                    > static_cast<size_t>(
                        std::numeric_limits<int>::max())) {
                QMetaObject::invokeMethod(
                    this,
                    [this, token] {
                        handleWorkerError(
                            token, true,
                            QStringLiteral("视频画面数据无效"));
                    },
                    Qt::QueuedConnection);
                break;
            }

            std::unique_lock lock(mutex_);
            const qint64 frameBytes = static_cast<qint64>(frame.data_size);
            condition_.wait(lock, [this, token, frameBytes] {
                if (stopRequested_ || token != workerToken_
                    || pendingSeek_.has_value()) {
                    return true;
                }
                return videoFrameQueueCanAdmit(
                    static_cast<int>(queue_.size()), queuedBytes_, frameBytes);
            });
            if (stopRequested_ || token != workerToken_) break;
            if (pendingSeek_.has_value()) continue;

            auto snapshot = std::make_shared<VideoFrameSnapshot>();
            snapshot->pixels = QByteArray(
                reinterpret_cast<const char*>(frame.data),
                static_cast<int>(frame.data_size));
            snapshot->width = frame.width;
            snapshot->height = frame.height;
            snapshot->stride = frame.stride;
            snapshot->ptsMs = frame.pts_ms;
            snapshot->sarNum = std::max(1, frame.sar_num);
            snapshot->sarDen = std::max(1, frame.sar_den);
            snapshot->rotationDegrees = frame.rotation_degrees;
            snapshot->generation = decodeGeneration_;
            snapshot->serial = ++serialCounter_;
            queue_.push_back(snapshot);
            queuedBytes_ += frameBytes;
            lock.unlock();
            QMetaObject::invokeMethod(
                this, [this, token] { handleFrameQueued(token); },
                Qt::QueuedConnection);
        }
    }

    {
        const std::lock_guard lock(mutex_);
        if (decoder_ == decoder) {
            ag_video_decoder_destroy(decoder);
            decoder_ = nullptr;
            decoder = nullptr;
        }
    }
    if (decoder != nullptr) ag_video_decoder_destroy(decoder);
    QMetaObject::invokeMethod(
        this, [this, token] { handleWorkerFinished(token); },
        Qt::QueuedConnection);
}

void VideoPlaybackController::handleCommittedSeek(qint64 positionMs)
{
    if (activeTrackId_.isEmpty()) return;
    if (!workerRunning_) {
        if (library_ == nullptr || playback_ == nullptr
            || playback_->state() == PlaybackController::Stopped
            || playback_->currentTrackId() != activeTrackId_) {
            return;
        }
        const TrackRecord* const record =
            library_->recordForId(activeTrackId_);
        if (record == nullptr
            || (!record->hasVideo
                && !verifiedVideoTrackIds_.contains(activeTrackId_))) {
            return;
        }
        startWorker(*record, std::nullopt);
    }
    if (!worker_.joinable()) return;
    {
        const std::lock_guard lock(mutex_);
        ++decodeGeneration_;
        queue_.clear();
        queuedBytes_ = 0;
        displayedFrame_.reset();
        pendingSeek_ = PendingSeek{positionMs, decodeGeneration_};
        frameSerial_ = ++serialCounter_;
        condition_.notify_all();
    }
    setErrorMessage({});
    setLoading(true);
    emit frameChanged();
    emit diagnosticsChanged();
}

void VideoPlaybackController::presentForCurrentClock()
{
    if (playback_ == nullptr) return;
    const qint64 clock = playback_->positionMs();
    std::shared_ptr<const VideoFrameSnapshot> selected;
    bool queueChanged = false;
    {
        const std::lock_guard lock(mutex_);
        while (!queue_.empty()
               && queue_.front()->ptsMs <= clock + kClockToleranceMs) {
            selected = queue_.front();
            queuedBytes_ -= selected->pixels.size();
            queue_.pop_front();
            queueChanged = true;
        }
        if (selected != nullptr) {
            displayedFrame_ = selected;
            frameSerial_ = selected->serial;
        }
        if (queueChanged) condition_.notify_all();
    }
    if (selected != nullptr) {
        setLoading(false);
        emit frameChanged();
    }
    if (queueChanged) emit diagnosticsChanged();
}

void VideoPlaybackController::handleFrameQueued(quint64 token)
{
    {
        const std::lock_guard lock(mutex_);
        if (token != workerToken_) return;
    }
    emit diagnosticsChanged();
    presentForCurrentClock();
}

void VideoPlaybackController::handleProbeResult(
    quint64 token, const QString& trackId,
    const std::optional<MetadataProbeClaim>& claim, bool succeeded,
    const TrackRecord& probed)
{
    {
        const std::lock_guard lock(mutex_);
        if (token != workerToken_) return;
    }
    if (library_ != nullptr && claim.has_value()) {
        library_->completeMediaKindProbe(*claim, succeeded, probed);
    }
    if (claim.has_value() && activeProbeClaim_.has_value()
        && activeProbeClaim_->generation == claim->generation
        && activeProbeClaim_->trackId == claim->trackId) {
        activeProbeClaim_.reset();
    }
    if (!succeeded || !probed.hasVideo) {
        setLoading(false);
        setVisible(false);
        return;
    }
    verifiedVideoTrackIds_.insert(trackId);
    if (playback_ != nullptr
        && playback_->state() != PlaybackController::Stopped
        && playback_->currentTrackId() == trackId) {
        setVisible(true);
    }
}

void VideoPlaybackController::handleWorkerError(
    quint64 token, bool knownVideo, const QString& message)
{
    {
        const std::lock_guard lock(mutex_);
        if (token != workerToken_) return;
    }
    setLoading(false);
    if (knownVideo) {
        setVisible(true);
        setErrorMessage(message);
    }
}

void VideoPlaybackController::handleWorkerFinished(quint64 token)
{
    {
        const std::lock_guard lock(mutex_);
        if (token != workerToken_) return;
    }
    if (workerRunning_) {
        workerRunning_ = false;
        emit diagnosticsChanged();
    }
    setLoading(false);
}

void VideoPlaybackController::clearFrames(bool advanceSerial)
{
    bool hadFrame = false;
    {
        const std::lock_guard lock(mutex_);
        hadFrame = displayedFrame_ != nullptr;
        queue_.clear();
        queuedBytes_ = 0;
        displayedFrame_.reset();
        if (advanceSerial && hadFrame) frameSerial_ = ++serialCounter_;
        condition_.notify_all();
    }
    if (hadFrame) emit frameChanged();
    emit diagnosticsChanged();
}

void VideoPlaybackController::setVisible(bool value)
{
    if (visible_ == value) return;
    visible_ = value;
    emit visibleChanged();
}

void VideoPlaybackController::setLoading(bool value)
{
    if (loading_ == value) return;
    loading_ = value;
    emit loadingChanged();
}

void VideoPlaybackController::setErrorMessage(QString value)
{
    if (errorMessage_ == value) return;
    errorMessage_ = std::move(value);
    emit errorMessageChanged();
}
