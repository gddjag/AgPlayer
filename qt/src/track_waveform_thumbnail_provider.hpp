#pragma once

#include <QByteArray>
#include <QFutureWatcher>
#include <QHash>
#include <QObject>
#include <QString>
#include <QThreadPool>
#include <QVariantMap>

#include <chrono>
#include <cstdint>
#include <list>
#include <optional>
#include <vector>

class TrackWaveformThumbnailProvider final : public QObject {
    Q_OBJECT

public:
    static constexpr int kPeakCount = 2048;
    static constexpr int kPeakDataSize = kPeakCount * 2;
    static constexpr int kMaxCacheEntries = 256;
    static constexpr int kMaxQueuedJobs = 256;

    explicit TrackWaveformThumbnailProvider(
        QString cacheDirectory, QObject* parent = nullptr);
    ~TrackWaveformThumbnailProvider() override;

    TrackWaveformThumbnailProvider(const TrackWaveformThumbnailProvider&) = delete;
    TrackWaveformThumbnailProvider& operator=(
        const TrackWaveformThumbnailProvider&) = delete;

    [[nodiscard]] static QByteArray quantizeMixPeaks(
        const std::vector<float>& mix);
    [[nodiscard]] static QByteArray quantizeBandEnergy(
        const std::vector<float>& energy,
        const std::vector<float>& mix);

    Q_INVOKABLE void request(const QString& trackId,
                             const QString& sourcePath,
                             quint64 generation,
                             bool visiblePriority = false);
    Q_INVOKABLE void cancel(const QString& trackId, quint64 generation);
    Q_INVOKABLE [[nodiscard]] QVariantMap diagnostics() const;
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void invalidateSourceCache(const QString& sourcePath);

    void setCacheDirectory(const QString& cacheDirectory);

signals:
    void thumbnailReady(const QString& trackId,
                        quint64 generation,
                        const QByteArray& peaks,
                        const QByteArray& bass,
                        const QByteArray& mid,
                        const QByteArray& high);
    void sourceCacheInvalidated(const QString& sourcePath);
    void analysisRequested(const QString& sourcePath);

private:
    struct Request final {
        QString trackId;
        QString sourcePath;
        quint64 generation = 0U;
        bool visiblePriority = false;
        bool canceled = false;
    };

    struct LoadResult final {
        QString trackId;
        QString sourcePath;
        quint64 cacheEpoch = 0U;
        QByteArray peaks;
        QByteArray bass;
        QByteArray mid;
        QByteArray high;
    };

    struct ActiveLoad final {
        Request request;
        QFutureWatcher<LoadResult>* watcher = nullptr;
        bool sourceInvalidated = false;
    };

    struct CacheEntry final {
        QString sourcePath;
        QByteArray peaks;
        QByteArray bass;
        QByteArray mid;
        QByteArray high;
        std::list<QString>::iterator order;
    };

    struct NegativeEntry final {
        std::chrono::steady_clock::time_point expiresAt;
        std::list<QString>::iterator order;
    };

    struct ThumbnailData final {
        QByteArray peaks;
        QByteArray bass;
        QByteArray mid;
        QByteArray high;
    };

    [[nodiscard]] static ThumbnailData loadFromCacheOnly(
        const QString& cacheDirectory, const QString& sourcePath);
    [[nodiscard]] static unsigned char quantizeSigned(float amplitude);
    void startNext();
    void finishActive(QFutureWatcher<LoadResult>* watcher);
    void touchLru(const QString& trackId);
    void insertCache(const QString& trackId,
                     const QString& sourcePath,
                     const QByteArray& peaks,
                     const QByteArray& bass,
                     const QByteArray& mid,
                     const QByteArray& high);
    [[nodiscard]] QString negativeKey(const QString& sourcePath) const;
    [[nodiscard]] bool hasNegativeCooldown(const QString& sourcePath);
    void insertNegativeCooldown(const QString& sourcePath);
    void removeNegativeCooldown(const QString& sourcePath);
    [[nodiscard]] int queuedIndexForTrack(const QString& trackId) const;

    QString cacheDirectory_;
    quint64 cacheEpoch_ = 0U;
    QList<Request> pending_;
    QList<ActiveLoad> activeLoads_;
    QHash<QString, CacheEntry> cache_;
    std::list<QString> lruOrder_;
    QHash<QString, NegativeEntry> negativeCache_;
    std::list<QString> negativeOrder_;
    QThreadPool workerPool_;
    int activeWorkers_ = 0;
    int maxActiveWorkers_ = 0;
    int maxInFlightTracks_ = 0;
    qulonglong cacheReadAttempts_ = 0U;
};
