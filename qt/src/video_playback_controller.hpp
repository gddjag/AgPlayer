#pragma once

#include "library_model.hpp"

#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

struct ag_video_decoder;
class PlaybackController;

struct VideoFrameSnapshot final {
    QByteArray pixels;
    int width = 0;
    int height = 0;
    int stride = 0;
    qint64 ptsMs = 0;
    int sarNum = 1;
    int sarDen = 1;
    int rotationDegrees = 0;
    quint64 generation = 0;
    quint64 serial = 0;
};

bool videoFrameQueueCanAdmit(int queuedFrameCount, qint64 queuedBytes,
                             qint64 frameBytes) noexcept;

class VideoPlaybackController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool visible READ visible NOTIFY visibleChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    Q_PROPERTY(bool workerRunning READ workerRunning NOTIFY diagnosticsChanged)
    Q_PROPERTY(int queuedFrameCount READ queuedFrameCount NOTIFY diagnosticsChanged)
    Q_PROPERTY(qint64 queuedFrameBytes READ queuedFrameBytes NOTIFY diagnosticsChanged)
    Q_PROPERTY(quint64 frameSerial READ frameSerial NOTIFY frameChanged)

public:
    static constexpr int MaxQueuedFrames = 3;
    static constexpr qint64 MaxQueuedFrameBytes = 64LL * 1024LL * 1024LL;

    explicit VideoPlaybackController(LibraryModel* library,
                                     PlaybackController* playback,
                                     QObject* parent = nullptr);
    ~VideoPlaybackController() override;

    bool visible() const noexcept { return visible_; }
    bool loading() const noexcept { return loading_; }
    QString errorMessage() const { return errorMessage_; }
    bool workerRunning() const noexcept { return workerRunning_; }
    int queuedFrameCount() const;
    qint64 queuedFrameBytes() const;
    quint64 frameSerial() const noexcept { return frameSerial_; }
    std::shared_ptr<const VideoFrameSnapshot> currentFrame() const;

    Q_INVOKABLE void dismiss();

signals:
    void visibleChanged();
    void loadingChanged();
    void errorMessageChanged();
    void diagnosticsChanged();
    void frameChanged();

private:
    struct PendingSeek final {
        qint64 positionMs = 0;
        quint64 generation = 0;
    };

    void reevaluateCurrentTrack();
    void startWorker(const TrackRecord& track,
                     std::optional<MetadataProbeClaim> probeClaim);
    void stopWorkerAndClear();
    void runWorker(QString trackId, QString path, quint64 token,
                   std::optional<MetadataProbeClaim> probeClaim);
    void handleCommittedSeek(qint64 positionMs);
    void presentForCurrentClock();
    void handleFrameQueued(quint64 token);
    void handleProbeResult(quint64 token, const QString& trackId,
                           const std::optional<MetadataProbeClaim>& claim,
                           bool succeeded, const TrackRecord& probed);
    void handleWorkerError(quint64 token, bool knownVideo,
                           const QString& message);
    void handleWorkerFinished(quint64 token);
    void clearFrames(bool advanceSerial);
    void setVisible(bool value);
    void setLoading(bool value);
    void setErrorMessage(QString value);
    static bool hasSupportedVideoSuffix(const QString& path);

    QPointer<LibraryModel> library_;
    QPointer<PlaybackController> playback_;
    QSet<QString> verifiedVideoTrackIds_;
    QString activeTrackId_;
    std::optional<MetadataProbeClaim> activeProbeClaim_;
    std::thread worker_;

    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<std::shared_ptr<const VideoFrameSnapshot>> queue_;
    std::shared_ptr<const VideoFrameSnapshot> displayedFrame_;
    qint64 queuedBytes_ = 0;
    ag_video_decoder* decoder_ = nullptr;
    bool stopRequested_ = false;
    std::optional<PendingSeek> pendingSeek_;
    quint64 decodeGeneration_ = 0;
    quint64 serialCounter_ = 0;
    quint64 workerToken_ = 0;

    bool visible_ = false;
    bool loading_ = false;
    bool workerRunning_ = false;
    QString errorMessage_;
    quint64 frameSerial_ = 0;
};
