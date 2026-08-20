#pragma once

#include <QByteArray>
#include <QColor>
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
    static constexpr int kPeakCount = 128;
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
    [[nodiscard]] static std::uint32_t fnv1a32(const QString& trackId) noexcept;
    Q_INVOKABLE [[nodiscard]] static QColor colorForTrackId(
        const QString& trackId);

    Q_INVOKABLE void request(const QString& trackId,
                             const QString& sourcePath,
                             quint64 generation);
    Q_INVOKABLE void cancel(const QString& trackId, quint64 generation);
    Q_INVOKABLE [[nodiscard]] QVariantMap diagnostics() const;
    Q_INVOKABLE void refresh();

    void setCacheDirectory(const QString& cacheDirectory);

signals:
    void thumbnailReady(const QString& trackId,
                        quint64 generation,
                        const QByteArray& peaks);

private:
    struct Request final {
        QString trackId;
        QString sourcePath;
        quint64 generation = 0U;
        bool canceled = false;
    };

    struct LoadResult final {
        QString trackId;
        QString sourcePath;
        quint64 cacheEpoch = 0U;
        QByteArray peaks;
    };

    struct CacheEntry final {
        QString sourcePath;
        QByteArray peaks;
        std::list<QString>::iterator order;
    };

    struct NegativeEntry final {
        std::chrono::steady_clock::time_point expiresAt;
        std::list<QString>::iterator order;
    };

    [[nodiscard]] static QByteArray loadFromV2CacheOnly(
        const QString& cacheDirectory, const QString& sourcePath);
    [[nodiscard]] static unsigned char quantizeAmplitude(float amplitude);
    void startNext();
    void finishActive();
    void touchLru(const QString& trackId);
    void insertCache(const QString& trackId,
                     const QString& sourcePath,
                     const QByteArray& peaks);
    [[nodiscard]] QString negativeKey(const QString& sourcePath) const;
    [[nodiscard]] bool hasNegativeCooldown(const QString& sourcePath);
    void insertNegativeCooldown(const QString& sourcePath);
    void removeNegativeCooldown(const QString& sourcePath);
    void postThumbnailReady(const Request& request,
                            const QByteArray& peaks = {});
    [[nodiscard]] int queuedIndexForTrack(const QString& trackId) const;

    QString cacheDirectory_;
    quint64 cacheEpoch_ = 0U;
    QList<Request> pending_;
    std::optional<Request> activeRequest_;
    QHash<QString, CacheEntry> cache_;
    std::list<QString> lruOrder_;
    QHash<QString, NegativeEntry> negativeCache_;
    std::list<QString> negativeOrder_;
    QThreadPool workerPool_;
    QFutureWatcher<LoadResult> watcher_;
    int activeWorkers_ = 0;
    int maxActiveWorkers_ = 0;
    int maxInFlightTracks_ = 0;
    qulonglong cacheReadAttempts_ = 0U;
};
