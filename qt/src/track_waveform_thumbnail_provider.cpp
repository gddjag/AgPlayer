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

constexpr std::array<const char*, 36> kThumbnailPalette{{
    "#E11D48", "#DC2626", "#EA580C", "#F59E0B", "#CA8A04", "#65A30D",
    "#16A34A", "#059669", "#0D9488", "#0891B2", "#0284C7", "#2563EB",
    "#4F46E5", "#7C3AED", "#9333EA", "#C026D3", "#DB2777", "#BE185D",
    "#9F1239", "#B91C1C", "#C2410C", "#B45309", "#A16207", "#4D7C0F",
    "#15803D", "#047857", "#0F766E", "#155E75", "#1E40AF", "#3730A3",
    "#5B21B6", "#6B21A8", "#86198F", "#9D174D", "#9F2A2A", "#7C2D12",
}};

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
    workerPool_.setMaxThreadCount(1);
    workerPool_.setThreadPriority(QThread::LowPriority);
    connect(&watcher_, &QFutureWatcher<LoadResult>::finished,
            this, &TrackWaveformThumbnailProvider::finishActive);
}

TrackWaveformThumbnailProvider::~TrackWaveformThumbnailProvider()
{
    watcher_.disconnect(this);
    pending_.clear();
    workerPool_.clear();
    workerPool_.waitForDone();
}

QByteArray TrackWaveformThumbnailProvider::quantizeMixPeaks(
    const std::vector<float>& mix)
{
    QByteArray result(kPeakCount, '\0');
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
        result[bucket] = static_cast<char>(quantizeAmplitude(maximum));
    }
    return result;
}

std::uint32_t TrackWaveformThumbnailProvider::fnv1a32(
    const QString& trackId) noexcept
{
    constexpr std::uint32_t offsetBasis = 2166136261U;
    constexpr std::uint32_t prime = 16777619U;

    std::uint32_t hash = offsetBasis;
    const QByteArray utf8 = trackId.toUtf8();
    for (const char value : utf8) {
        hash ^= static_cast<unsigned char>(value);
        hash *= prime;
    }
    return hash;
}

QColor TrackWaveformThumbnailProvider::colorForTrackId(const QString& trackId)
{
    const std::size_t index = fnv1a32(trackId) % kThumbnailPalette.size();
    return QColor(QString::fromLatin1(kThumbnailPalette[index]));
}

void TrackWaveformThumbnailProvider::request(const QString& trackId,
                                             const QString& sourcePath,
                                             const quint64 generation)
{
    auto cached = cache_.find(trackId);
    if (cached != cache_.end() && cached->sourcePath == sourcePath) {
        touchLru(trackId);
        emit thumbnailReady(trackId, generation, cached->peaks);
        return;
    }
    if (cached != cache_.end()) {
        lruOrder_.erase(cached->order);
        cache_.erase(cached);
    }

    if (activeRequest_.has_value()
        && activeRequest_->trackId == trackId) {
        activeRequest_->sourcePath = sourcePath;
        activeRequest_->generation = generation;
        activeRequest_->canceled = false;
        return;
    }

    const int queuedIndex = queuedIndexForTrack(trackId);
    if (queuedIndex >= 0) {
        Request& queued = pending_[queuedIndex];
        queued.sourcePath = sourcePath;
        queued.generation = generation;
        queued.canceled = false;
        return;
    }

    if (sourcePath.isEmpty()) {
        emit thumbnailReady(trackId, generation, {});
        return;
    }

    if (hasNegativeCooldown(sourcePath)) {
        emit thumbnailReady(trackId, generation, {});
        return;
    }

    std::optional<Request> dropped;
    if (pending_.size() >= kMaxQueuedJobs) {
        dropped = pending_.takeFirst();
    }
    pending_.append(Request{trackId, sourcePath, generation, false});
    startNext();
    maxInFlightTracks_ = std::max(
        maxInFlightTracks_,
        static_cast<int>(pending_.size())
            + (activeRequest_.has_value() ? 1 : 0));
    if (dropped.has_value() && !dropped->canceled) {
        emit thumbnailReady(dropped->trackId, dropped->generation, {});
    }
}

void TrackWaveformThumbnailProvider::cancel(const QString& trackId,
                                            const quint64 generation)
{
    if (activeRequest_.has_value()
        && activeRequest_->trackId == trackId
        && activeRequest_->generation == generation) {
        activeRequest_->canceled = true;
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
         pending_.size() + (activeRequest_.has_value() ? 1 : 0)},
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
    if (activeRequest_.has_value()
        && sourceLookupKey(activeRequest_->sourcePath) == sourceKey) {
        activeSourceInvalidated_ = true;
    }
    emit sourceCacheInvalidated(sourcePath);
}

QByteArray TrackWaveformThumbnailProvider::loadFromV2CacheOnly(
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
        if (agplayer::WaveformCache::load_v2(
                filesystemPath(QDir(cacheDirectory).filePath(filename)),
                source, data)
            && !data.mix.empty()) {
            return quantizeMixPeaks(data.mix);
        }
    }
    return {};
}

unsigned char TrackWaveformThumbnailProvider::quantizeAmplitude(
    const float amplitude)
{
    const float clamped = std::clamp(amplitude, 0.0F, 1.0F);
    return static_cast<unsigned char>(std::lround(clamped * 255.0F));
}

void TrackWaveformThumbnailProvider::startNext()
{
    if (activeRequest_.has_value()) {
        return;
    }

    while (!pending_.isEmpty()) {
        const Request cooled = pending_.takeFirst();
        if (!hasNegativeCooldown(cooled.sourcePath)) {
            activeRequest_ = cooled;
            activeSourceInvalidated_ = false;
            break;
        }
        if (!cooled.canceled) {
            const QPointer<TrackWaveformThumbnailProvider> guard(this);
            emit thumbnailReady(cooled.trackId, cooled.generation, {});
            if (guard.isNull() || activeRequest_.has_value()) {
                return;
            }
        }
    }
    if (!activeRequest_.has_value()) {
        return;
    }

    const Request started = *activeRequest_;
    const QString cacheDirectory = cacheDirectory_;
    const quint64 cacheEpoch = cacheEpoch_;
    activeWorkers_ = 1;
    maxActiveWorkers_ = std::max(maxActiveWorkers_, activeWorkers_);
    ++cacheReadAttempts_;

    watcher_.setFuture(QtConcurrent::run(
        &workerPool_,
        [cacheDirectory, cacheEpoch, started] {
            return LoadResult{
                started.trackId,
                started.sourcePath,
                cacheEpoch,
                loadFromV2CacheOnly(cacheDirectory, started.sourcePath),
            };
        }));
}

void TrackWaveformThumbnailProvider::finishActive()
{
    const LoadResult loaded = watcher_.result();
    activeWorkers_ = 0;
    const bool sourceInvalidated = activeSourceInvalidated_;
    activeSourceInvalidated_ = false;
    if (!activeRequest_.has_value()
        || activeRequest_->trackId != loaded.trackId) {
        activeRequest_.reset();
        startNext();
        return;
    }

    const Request current = *activeRequest_;
    activeRequest_.reset();
    if (sourceInvalidated || loaded.cacheEpoch != cacheEpoch_
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
            insertCache(current.trackId, current.sourcePath, loaded.peaks);
        }
        const QPointer<TrackWaveformThumbnailProvider> guard(this);
        emit thumbnailReady(current.trackId, current.generation, loaded.peaks);
        if (guard.isNull()) {
            return;
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
                                                 const QByteArray& peaks)
{
    auto existing = cache_.find(trackId);
    if (existing != cache_.end()) {
        lruOrder_.erase(existing->order);
        cache_.erase(existing);
    }

    lruOrder_.push_front(trackId);
    cache_.insert(trackId,
                  CacheEntry{sourcePath, peaks, lruOrder_.begin()});
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
