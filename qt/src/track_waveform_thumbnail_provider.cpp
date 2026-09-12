#include "track_waveform_thumbnail_provider.hpp"
#include "waveform_cache.hpp"

#include <QDir>
#include <QPointer>
#include <QThread>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <string>

namespace {

constexpr auto kNegativeCooldown = std::chrono::milliseconds(200);

std::filesystem::path filesystemPath(const QString& path)
{
#ifdef Q_OS_WIN
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::u8path(path.toUtf8().constData());
#endif
}

QString sourceLookupKey(const QString& path)
{
    const QString clean = QDir::fromNativeSeparators(QDir::cleanPath(path));
#ifdef Q_OS_WIN
    return clean.toCaseFolded();
#else
    return clean;
#endif
}

} // namespace

TrackWaveformThumbnailProvider::TrackWaveformThumbnailProvider(
    QString cacheDirectory, QObject* parent)
    : QObject(parent)
    , cacheDirectory_(std::move(cacheDirectory))
{
    // One canceled cache read may still be inside filesystem code.  Reserve a
    // second low-priority worker so a newly visible row can start immediately.
    workerPool_.setMaxThreadCount(2);
    workerPool_.setThreadPriority(QThread::LowPriority);
}

TrackWaveformThumbnailProvider::~TrackWaveformThumbnailProvider()
{
    for (const ActiveLoad& active : activeLoads_) {
        active.watcher->disconnect(this);
    }
    activeLoads_.clear();
    pending_.clear();
    workerPool_.clear();
    workerPool_.waitForDone();
}

QByteArray TrackWaveformThumbnailProvider::quantizeMixPeaks(
    const std::vector<float>& mix)
{
    QByteArray result(kPeakDataSize, static_cast<char>(128));
    if (mix.empty()) {
        return result;
    }

    for (int bucket = 0; bucket < kPeakCount; ++bucket) {
        const auto first = mix.size() * static_cast<std::size_t>(bucket)
            / static_cast<std::size_t>(kPeakCount);
        const auto numerator = mix.size()
            * static_cast<std::size_t>(bucket + 1);
        const auto last = std::min(
            mix.size(),
            (numerator + static_cast<std::size_t>(kPeakCount - 1))
                / static_cast<std::size_t>(kPeakCount));

        float maximum = 0.0F;
        for (std::size_t index = first; index < last; ++index) {
            const float sample = mix[index];
            if (std::isfinite(sample)) {
                maximum = std::max(maximum, std::abs(sample));
            }
        }
        result[bucket * 2] = static_cast<char>(quantizeSigned(-maximum));
        result[bucket * 2 + 1] = static_cast<char>(quantizeSigned(maximum));
    }
    return result;
}

QByteArray TrackWaveformThumbnailProvider::quantizeBandEnergy(
    const std::vector<float>& energy,
    const std::vector<float>& mix)
{
    if (energy.empty() || energy.size() != mix.size()) {
        return {};
    }
    QByteArray result(kPeakCount, 0);
    for (int bucket = 0; bucket < kPeakCount; ++bucket) {
        const double sourceStart = static_cast<double>(bucket)
            * energy.size() / kPeakCount;
        const double sourceEnd = static_cast<double>(bucket + 1)
            * energy.size() / kPeakCount;
        const std::size_t first = std::min(
            energy.size() - 1U,
            static_cast<std::size_t>(std::floor(sourceStart)));
        const std::size_t last = std::min(
            energy.size(),
            std::max(first + 1U,
                     static_cast<std::size_t>(std::ceil(sourceEnd))));
        double weighted = 0.0;
        double weights = 0.0;
        for (std::size_t index = first; index < last; ++index) {
            const double weight = std::max(0.001,
                static_cast<double>(std::abs(mix[index])));
            weighted += std::clamp(static_cast<double>(energy[index]),
                                   0.0, 1.0) * 255.0 * weight;
            weights += weight;
        }
        result[bucket] = static_cast<char>(std::lround(weighted / weights));
    }
    return result;
}

void TrackWaveformThumbnailProvider::request(const QString& trackId,
                                             const QString& sourcePath,
                                             const quint64 generation,
                                             const bool visiblePriority)
{
    auto cached = cache_.find(trackId);
    if (cached != cache_.end() && cached->sourcePath == sourcePath) {
        touchLru(trackId);
        emit thumbnailReady(trackId, generation, cached->peaks,
                            cached->bass, cached->mid, cached->high);
        return;
    }
    if (cached != cache_.end()) {
        lruOrder_.erase(cached->order);
        cache_.erase(cached);
    }

    for (ActiveLoad& active : activeLoads_) {
        if (active.request.trackId == trackId) {
            active.request.sourcePath = sourcePath;
            active.request.generation = generation;
            active.request.visiblePriority = visiblePriority;
            active.request.canceled = false;
            return;
        }
    }

    const int queuedIndex = queuedIndexForTrack(trackId);
    if (queuedIndex >= 0) {
        Request& queued = pending_[queuedIndex];
        queued.sourcePath = sourcePath;
        queued.generation = generation;
        queued.visiblePriority = visiblePriority;
        queued.canceled = false;
        if (visiblePriority) {
            const Request promoted = queued;
            pending_.removeAt(queuedIndex);
            pending_.prepend(promoted);
        }
        return;
    }

    if (sourcePath.isEmpty()) {
        emit thumbnailReady(trackId, generation, {}, {}, {}, {});
        return;
    }

    if (hasNegativeCooldown(sourcePath)) {
        emit thumbnailReady(trackId, generation, {}, {}, {}, {});
        return;
    }

    std::optional<Request> dropped;
    // Two active watchers require reserving one queue slot to keep the
    // historical total work bound (256 queued plus one active) intact.
    if (pending_.size() >= kMaxQueuedJobs - 1) {
        // Front is reserved for visible rows; discard the oldest background
        // work first so an off-screen burst cannot evict viewport work.
        dropped = pending_.takeLast();
    }
    const Request request{trackId, sourcePath, generation, visiblePriority, false};
    if (visiblePriority) pending_.prepend(request);
    else pending_.append(request);
    startNext();
    maxInFlightTracks_ = std::max(
        maxInFlightTracks_,
        static_cast<int>(pending_.size())
            + static_cast<int>(activeLoads_.size()));
    if (dropped.has_value() && !dropped->canceled) {
        emit thumbnailReady(dropped->trackId, dropped->generation,
                            {}, {}, {}, {});
    }
}

void TrackWaveformThumbnailProvider::cancel(const QString& trackId,
                                            const quint64 generation)
{
    for (ActiveLoad& active : activeLoads_) {
        if (active.request.trackId == trackId
            && active.request.generation == generation) {
            active.request.canceled = true;
            // The cache read has no interruption points, so cancelling its
            // QFuture would publish a result-less completion. Keep it running
            // but suppress publication; the second bounded worker can serve
            // the next visible row immediately.
            startNext();
            return;
        }
    }

    const int queuedIndex = queuedIndexForTrack(trackId);
    if (queuedIndex >= 0 && pending_[queuedIndex].generation == generation) {
        pending_.removeAt(queuedIndex);
    }
}

QVariantMap TrackWaveformThumbnailProvider::diagnostics() const
{
    return {
        {QStringLiteral("cacheEntries"), cache_.size()},
        {QStringLiteral("inFlightTracks"),
         pending_.size() + activeLoads_.size()},
        {QStringLiteral("queuedJobs"), pending_.size()},
        {QStringLiteral("activeWorkers"), activeWorkers_},
        {QStringLiteral("maxActiveWorkers"), maxActiveWorkers_},
        {QStringLiteral("maxInFlightTracks"), maxInFlightTracks_},
        {QStringLiteral("maxQueuedJobs"), kMaxQueuedJobs},
        {QStringLiteral("negativeCacheEntries"), negativeCache_.size()},
        {QStringLiteral("cacheReadAttempts"), cacheReadAttempts_},
    };
}

void TrackWaveformThumbnailProvider::setCacheDirectory(
    const QString& cacheDirectory)
{
    if (cacheDirectory_ == cacheDirectory) {
        return;
    }
    cacheDirectory_ = cacheDirectory;
    refresh();
}

void TrackWaveformThumbnailProvider::refresh()
{
    ++cacheEpoch_;
    cache_.clear();
    lruOrder_.clear();
    negativeCache_.clear();
    negativeOrder_.clear();
}

void TrackWaveformThumbnailProvider::invalidateSourceCache(
    const QString& sourcePath)
{
    if (sourcePath.isEmpty()) return;
    const QString sourceKey = sourceLookupKey(sourcePath);
    for (auto entry = cache_.begin(); entry != cache_.end();) {
        if (sourceLookupKey(entry->sourcePath) == sourceKey) {
            lruOrder_.erase(entry->order);
            entry = cache_.erase(entry);
        } else {
            ++entry;
        }
    }
    removeNegativeCooldown(sourcePath);
    for (ActiveLoad& active : activeLoads_) {
        if (sourceLookupKey(active.request.sourcePath) == sourceKey) {
            active.sourceInvalidated = true;
        }
    }
    emit sourceCacheInvalidated(sourcePath);
}

TrackWaveformThumbnailProvider::ThumbnailData
TrackWaveformThumbnailProvider::loadFromCacheOnly(
    const QString& cacheDirectory, const QString& sourcePath)
{
    if (cacheDirectory.isEmpty() || sourcePath.isEmpty()) {
        return {};
    }

    const std::filesystem::path source = filesystemPath(sourcePath);
    const std::string key = agplayer::WaveformCache::key_for(source);
    if (key.empty()) {
        return {};
    }

    static constexpr std::array<const char*, 3> suffixes{{
        "-average.agwf", "-rms.agwf", ".agwf",
    }};
    for (const char* suffix : suffixes) {
        const QString filename = QString::fromStdString(key)
            + QString::fromLatin1(suffix);
        agplayer::WaveformCacheData data;
        const std::filesystem::path cachePath = filesystemPath(
            QDir(cacheDirectory).filePath(filename));
        if (agplayer::WaveformCache::load_v4(cachePath, source, data)) {
            return {quantizeMixPeaks(data.mix),
                    quantizeBandEnergy(data.bass, data.mix),
                    quantizeBandEnergy(data.mid, data.mix),
                    quantizeBandEnergy(data.high, data.mix)};
        }
    }
    return {};
}

unsigned char TrackWaveformThumbnailProvider::quantizeSigned(
    const float amplitude)
{
    const float clamped = std::clamp(amplitude, -1.0F, 1.0F);
    return static_cast<unsigned char>(
        std::lround((clamped + 1.0F) * 127.5F));
}

void TrackWaveformThumbnailProvider::startNext()
{
    while (activeLoads_.size() < workerPool_.maxThreadCount()
           && !pending_.isEmpty()) {
        const Request cooled = pending_.takeFirst();
        if (hasNegativeCooldown(cooled.sourcePath)) {
            if (!cooled.canceled) {
                const QPointer<TrackWaveformThumbnailProvider> guard(this);
                emit thumbnailReady(cooled.trackId, cooled.generation,
                                    {}, {}, {}, {});
                if (guard.isNull()) {
                    return;
                }
            }
            continue;
        }

        auto* watcher = new QFutureWatcher<LoadResult>(this);
        activeLoads_.append(ActiveLoad{cooled, watcher, false});
        const QString cacheDirectory = cacheDirectory_;
        const quint64 cacheEpoch = cacheEpoch_;
        ++cacheReadAttempts_;
        activeWorkers_ = static_cast<int>(activeLoads_.size());
        maxActiveWorkers_ = std::max(maxActiveWorkers_, activeWorkers_);
        connect(watcher, &QFutureWatcher<LoadResult>::finished, this,
                [this, watcher] { finishActive(watcher); });
        watcher->setFuture(QtConcurrent::run(
            &workerPool_,
            [cacheDirectory, cacheEpoch, cooled] {
                const ThumbnailData data = loadFromCacheOnly(
                    cacheDirectory, cooled.sourcePath);
                return LoadResult{
                    cooled.trackId,
                    cooled.sourcePath,
                    cacheEpoch,
                    data.peaks,
                    data.bass,
                    data.mid,
                    data.high,
                };
            }));
    }
}

void TrackWaveformThumbnailProvider::finishActive(
    QFutureWatcher<LoadResult>* watcher)
{
    const LoadResult loaded = watcher->result();
    int activeIndex = -1;
    for (int index = 0; index < activeLoads_.size(); ++index) {
        if (activeLoads_.at(index).watcher == watcher) {
            activeIndex = index;
            break;
        }
    }
    if (activeIndex < 0) {
        watcher->deleteLater();
        return;
    }

    const ActiveLoad active = activeLoads_.takeAt(activeIndex);
    watcher->deleteLater();
    activeWorkers_ = static_cast<int>(activeLoads_.size());
    const Request current = active.request;
    if (active.sourceInvalidated || loaded.cacheEpoch != cacheEpoch_
        || loaded.sourcePath != current.sourcePath) {
        if (!current.canceled) {
            pending_.prepend(current);
        }
        startNext();
        return;
    }

    if (loaded.peaks.isEmpty()) {
        insertNegativeCooldown(current.sourcePath);
    } else {
        removeNegativeCooldown(current.sourcePath);
    }
    if (!current.canceled) {
        if (!loaded.peaks.isEmpty()) {
            insertCache(current.trackId, current.sourcePath, loaded.peaks,
                        loaded.bass, loaded.mid, loaded.high);
        }
        const QPointer<TrackWaveformThumbnailProvider> guard(this);
        emit thumbnailReady(current.trackId, current.generation, loaded.peaks,
                            loaded.bass, loaded.mid, loaded.high);
        if (guard.isNull()) {
            return;
        }
        if (loaded.peaks.isEmpty()) {
            emit analysisRequested(current.sourcePath);
            if (guard.isNull()) {
                return;
            }
        }
    }
    startNext();
}

void TrackWaveformThumbnailProvider::touchLru(const QString& trackId)
{
    auto entry = cache_.find(trackId);
    if (entry == cache_.end()) {
        return;
    }
    lruOrder_.erase(entry->order);
    lruOrder_.push_front(trackId);
    entry->order = lruOrder_.begin();
}

void TrackWaveformThumbnailProvider::insertCache(const QString& trackId,
                                                 const QString& sourcePath,
                                                 const QByteArray& peaks,
                                                 const QByteArray& bass,
                                                 const QByteArray& mid,
                                                 const QByteArray& high)
{
    auto existing = cache_.find(trackId);
    if (existing != cache_.end()) {
        lruOrder_.erase(existing->order);
        cache_.erase(existing);
    }

    lruOrder_.push_front(trackId);
    cache_.insert(trackId,
                  CacheEntry{sourcePath, peaks, bass, mid, high,
                             lruOrder_.begin()});
    while (cache_.size() > kMaxCacheEntries) {
        const QString evicted = lruOrder_.back();
        lruOrder_.pop_back();
        cache_.remove(evicted);
    }
}

QString TrackWaveformThumbnailProvider::negativeKey(
    const QString& sourcePath) const
{
    return QString::number(cacheEpoch_) + QChar(u'\0')
        + sourceLookupKey(sourcePath);
}

bool TrackWaveformThumbnailProvider::hasNegativeCooldown(
    const QString& sourcePath)
{
    const QString key = negativeKey(sourcePath);
    auto entry = negativeCache_.find(key);
    if (entry == negativeCache_.end()) {
        return false;
    }
    if (entry->expiresAt <= std::chrono::steady_clock::now()) {
        negativeOrder_.erase(entry->order);
        negativeCache_.erase(entry);
        return false;
    }

    negativeOrder_.erase(entry->order);
    negativeOrder_.push_front(key);
    entry->order = negativeOrder_.begin();
    return true;
}

void TrackWaveformThumbnailProvider::insertNegativeCooldown(
    const QString& sourcePath)
{
    const QString key = negativeKey(sourcePath);
    auto existing = negativeCache_.find(key);
    if (existing != negativeCache_.end()) {
        negativeOrder_.erase(existing->order);
        negativeCache_.erase(existing);
    }

    negativeOrder_.push_front(key);
    negativeCache_.insert(
        key,
        NegativeEntry{std::chrono::steady_clock::now() + kNegativeCooldown,
                      negativeOrder_.begin()});
    while (negativeCache_.size() > kMaxCacheEntries) {
        const QString evicted = negativeOrder_.back();
        negativeOrder_.pop_back();
        negativeCache_.remove(evicted);
    }
}

void TrackWaveformThumbnailProvider::removeNegativeCooldown(
    const QString& sourcePath)
{
    const QString key = negativeKey(sourcePath);
    auto existing = negativeCache_.find(key);
    if (existing == negativeCache_.end()) {
        return;
    }
    negativeOrder_.erase(existing->order);
    negativeCache_.erase(existing);
}

int TrackWaveformThumbnailProvider::queuedIndexForTrack(
    const QString& trackId) const
{
    for (int index = 0; index < pending_.size(); ++index) {
        if (pending_.at(index).trackId == trackId) {
            return index;
        }
    }
    return -1;
}
