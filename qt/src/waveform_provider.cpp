#include "waveform_provider.hpp"
#include "runtime_log.hpp"
#include "settings_controller.hpp"
#include "waveform_cache.hpp"

#include <QDir>
#include <QFileInfo>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QtConcurrent/QtConcurrent>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace {

struct ProviderCounters final {
    std::atomic<std::uint64_t> jobsStarted{0U};
    std::atomic<std::uint64_t> prefetchJobsStarted{0U};
};

ProviderCounters providerCounters;

// Minimum analysis density for short clips. Longer tracks use a bounded
// per-second budget so an eight-beat window retains independent detail.
constexpr std::size_t kWaveformAnalysisPoints = 32768;

std::size_t analysisPointsFor(const QByteArray& path)
{
    ag_metadata* metadata = nullptr;
    const auto result = ag_metadata_open(path.constData(), &metadata);
    const auto duration = result == AG_OK && metadata
        ? ag_metadata_duration_ms(metadata) : 0;
    if (metadata) ag_metadata_destroy(metadata);
    // Keep 512 source buckets/second for zoomed views, bounded to 12 MiB
    // of six float layers. Short clips keep the established minimum density.
    return static_cast<std::size_t>(std::clamp(
        static_cast<double>(duration) * 0.512,
        static_cast<double>(kWaveformAnalysisPoints), 524288.0));
}

std::filesystem::path filesystemPath(const QString& path)
{
#ifdef Q_OS_WIN
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::u8path(path.toUtf8().constData());
#endif
}

ag_waveform_aggregation aggregationForSettings(
    const SettingsController* settings) noexcept
{
    return settings != nullptr && settings->waveformPeakAlgorithm() == 1
               ? AG_WAVEFORM_AGGREGATION_RMS
               : AG_WAVEFORM_AGGREGATION_AVERAGE_ABSOLUTE;
}

QString cacheFilePathForDirectory(
    const QString& directory,
    const QString& sourcePath,
    const ag_waveform_aggregation aggregation)
{
    if (directory.isEmpty() || sourcePath.isEmpty()) {
        return {};
    }
    if (!QDir().mkpath(directory)) {
        return {};
    }
    const std::string key =
        agplayer::WaveformCache::key_for(filesystemPath(sourcePath));
    if (key.empty()) {
        return {};
    }
    const QString suffix =
        aggregation == AG_WAVEFORM_AGGREGATION_RMS
            ? QStringLiteral("-rms")
            : aggregation == AG_WAVEFORM_AGGREGATION_AVERAGE_ABSOLUTE
                  ? QStringLiteral("-average")
                  : QString();
    return QDir(directory).filePath(QString::fromStdString(key) + suffix
                                    + QStringLiteral(".agwf"));
}

QString cacheFilePathFor(SettingsController* settings,
                         const QString& sourcePath,
                         const ag_waveform_aggregation aggregation)
{
    return settings != nullptr
               ? cacheFilePathForDirectory(
                     settings->cacheDirectory(), sourcePath, aggregation)
               : QString();
}

QVariantList peaksFromVector(const std::vector<float>& peaks)
{
    QVariantList result;
    result.reserve(static_cast<int>(peaks.size()));
    for (const float value : peaks) {
        result.append(static_cast<double>(value));
    }
    return result;
}

QVariantMap layersFromWaveform(const ag_waveform* waveform)
{
    if (waveform == nullptr) {
        return {};
    }

    const auto layerToList = [](const ag_waveform* w,
                                ag_waveform_layer layer) {
        const std::size_t count = ag_waveform_layer_count(w, layer);
        QVariantList result;
        result.reserve(static_cast<int>(count));
        for (std::size_t index = 0; index < count; ++index) {
            result.append(static_cast<double>(ag_waveform_layer_peak(w, layer, index)));
        }
        return result;
    };

    QVariantMap layers;
    layers[QStringLiteral("mix")] = layerToList(waveform, AG_WAVEFORM_LAYER_MIX);
    layers[QStringLiteral("bass")] = layerToList(waveform, AG_WAVEFORM_LAYER_BASS);
    layers[QStringLiteral("mid")] = layerToList(waveform, AG_WAVEFORM_LAYER_MID);
    layers[QStringLiteral("high")] = layerToList(waveform, AG_WAVEFORM_LAYER_HIGH);
    layers[QStringLiteral("peak")] = layerToList(waveform, AG_WAVEFORM_LAYER_PEAK);
    layers[QStringLiteral("rms")] = layerToList(waveform, AG_WAVEFORM_LAYER_RMS);
    layers[QStringLiteral("_complete")] = true;
    layers[QStringLiteral("_durationMs")] =
        static_cast<qlonglong>(ag_waveform_duration_ms(waveform));
    layers[QStringLiteral("_bpm")] = ag_waveform_bpm(waveform);
    return layers;
}

QVariantMap layersFromCache(const agplayer::WaveformCacheData& data)
{
    QVariantMap layers;
    layers[QStringLiteral("mix")] = peaksFromVector(data.mix);
    layers[QStringLiteral("bass")] = peaksFromVector(data.bass);
    layers[QStringLiteral("mid")] = peaksFromVector(data.mid);
    layers[QStringLiteral("high")] = peaksFromVector(data.high);
    layers[QStringLiteral("peak")] = peaksFromVector(data.peak);
    layers[QStringLiteral("rms")] = peaksFromVector(data.rms);
    layers[QStringLiteral("_complete")] = true;
    layers[QStringLiteral("_durationMs")] =
        static_cast<qlonglong>(data.duration_ms);
    layers[QStringLiteral("_bpm")] = data.bpm;
    return layers;
}

void addRequestMetadata(QVariantMap& layers,
                        const QString& trackId,
                        const quint64 generation,
                        const bool frequencyRequested,
                        const bool frequencyReady)
{
    layers[QStringLiteral("_trackId")] = trackId;
    layers[QStringLiteral("_generation")] = generation;
    layers[QStringLiteral("_frequencyRequested")] = frequencyRequested;
    layers[QStringLiteral("_frequencyReady")] = frequencyReady;
    layers[QStringLiteral("_frequencyCacheVersion")] = frequencyReady ? 4 : 0;
}

void addTimelineMetadata(QVariantMap& layers,
                         std::uint64_t totalSamples,
                         int sampleRate)
{
    const int peakCount = layers.value(QStringLiteral("mix")).toList().size();
    layers[QStringLiteral("_sampleCount")] = peakCount;
    layers[QStringLiteral("_peakCount")] = peakCount;
    if (totalSamples > 0U && sampleRate > 0) {
        layers[QStringLiteral("_totalSamples")] =
            static_cast<qulonglong>(totalSamples);
        layers[QStringLiteral("_sampleRate")] = sampleRate;
    }
}

bool saveWaveformCache(const QString& cachePath,
                       const QString& sourcePath,
                       const ag_waveform* waveform,
                       const double fallbackBpm = 0.0)
{
    if (waveform == nullptr || cachePath.isEmpty() || sourcePath.isEmpty()) {
        return false;
    }
    agplayer::WaveformCacheData data;
    const std::size_t count = ag_waveform_count(waveform);
    data.mix.resize(count);
    for (std::size_t index = 0; index < count; ++index) {
        data.mix[index] = ag_waveform_peak(waveform, index);
    }
    data.bass.resize(
        ag_waveform_layer_count(waveform, AG_WAVEFORM_LAYER_BASS));
    for (std::size_t index = 0; index < data.bass.size(); ++index) {
        data.bass[index] = ag_waveform_layer_peak(waveform, AG_WAVEFORM_LAYER_BASS, index);
    }
    data.mid.resize(ag_waveform_layer_count(waveform, AG_WAVEFORM_LAYER_MID));
    for (std::size_t index = 0; index < data.mid.size(); ++index) {
        data.mid[index] = ag_waveform_layer_peak(waveform, AG_WAVEFORM_LAYER_MID, index);
    }
    data.high.resize(
        ag_waveform_layer_count(waveform, AG_WAVEFORM_LAYER_HIGH));
    for (std::size_t index = 0; index < data.high.size(); ++index) {
        data.high[index] = ag_waveform_layer_peak(waveform, AG_WAVEFORM_LAYER_HIGH, index);
    }
    data.peak.resize(ag_waveform_layer_count(waveform, AG_WAVEFORM_LAYER_PEAK));
    data.rms.resize(ag_waveform_layer_count(waveform, AG_WAVEFORM_LAYER_RMS));
    for (std::size_t index = 0; index < data.peak.size(); ++index)
        data.peak[index] = ag_waveform_layer_peak(waveform, AG_WAVEFORM_LAYER_PEAK, index);
    for (std::size_t index = 0; index < data.rms.size(); ++index)
        data.rms[index] = ag_waveform_layer_peak(waveform, AG_WAVEFORM_LAYER_RMS, index);
    const double analyzedBpm = ag_waveform_bpm(waveform);
    data.bpm = analyzedBpm > 0.0 ? analyzedBpm : fallbackBpm;
    data.duration_ms = ag_waveform_duration_ms(waveform);
    data.total_samples = ag_waveform_total_samples(waveform);
    data.sample_rate = static_cast<std::uint32_t>(
        std::max(0, ag_waveform_sample_rate(waveform)));
    if (!agplayer::WaveformCache::save_v4(
            filesystemPath(cachePath), filesystemPath(sourcePath), data)) {
        RuntimeLog::log(AG_IO_ERROR, QStringLiteral("Waveform"),
            QStringLiteral("Failed to save waveform cache for %1")
                .arg(sourcePath));
        return false;
    }
    return true;
}

} // namespace

namespace agplayer::testing {

void reset_waveform_provider_counters() noexcept
{
    providerCounters.jobsStarted.store(0U, std::memory_order_relaxed);
    providerCounters.prefetchJobsStarted.store(0U, std::memory_order_relaxed);
}

std::uint64_t waveform_provider_jobs_started() noexcept
{
    return providerCounters.jobsStarted.load(std::memory_order_relaxed);
}

std::uint64_t waveform_provider_prefetch_jobs_started() noexcept
{
    return providerCounters.prefetchJobsStarted.load(std::memory_order_relaxed);
}

} // namespace agplayer::testing

WaveformProvider::WaveformProvider(SettingsController* settings, QObject* parent)
    : QObject(parent)
    , settings_(settings)
{
    currentAnalysisPool_.setMaxThreadCount(1);
    currentAnalysisPool_.setThreadPriority(QThread::LowestPriority);
    progressTimer_ = new QTimer(this);
    progressTimer_->setInterval(100);
    connect(progressTimer_, &QTimer::timeout,
            this, &WaveformProvider::onProgressTimer);
}

WaveformProvider::AnalysisResources::~AnalysisResources()
{
    if (waveform != nullptr) ag_waveform_destroy(waveform);
    if (cancelToken != nullptr) ag_cancel_token_destroy(cancelToken);
}

void WaveformProvider::AnalysisResources::cancel()
{
    canceled.store(true, std::memory_order_relaxed);
    if (cancelToken != nullptr) ag_cancel_token_cancel(cancelToken);
}

WaveformProvider::~WaveformProvider()
{
    if (activeResources_ != nullptr) activeResources_->cancel();
    cancelPrefetchJobs();
    currentAnalysisPool_.waitForDone();
    if (watcher_ != nullptr) {
        watcher_->disconnect(this);
        delete watcher_;
        watcher_ = nullptr;
    }
    activeResources_.reset();
    activeProgress_.reset();
}

double WaveformProvider::analysisProgress() const noexcept
{
    return analysisProgress_;
}

qulonglong WaveformProvider::activeGeneration() const noexcept
{
    return activeGeneration_;
}

void WaveformProvider::setAnalysisProgress(double progress)
{
    const double finite = std::isfinite(progress) ? progress : 0.0;
    const double clamped = std::clamp(finite, 0.0, 1.0);
    if (qFuzzyCompare(clamped, analysisProgress_)) {
        return;
    }
    analysisProgress_ = clamped;
    emit analysisProgressChanged();
}

void WaveformProvider::onProgressTimer()
{
    if (activeProgress_ != nullptr) {
        setAnalysisProgress(activeProgress_->load(std::memory_order_relaxed));
    }
    if (activeResources_ && !activeResources_->canceled.load(std::memory_order_relaxed)) {
        QVariantMap snapshot;
        {
            std::lock_guard lock(activeResources_->snapshotMutex);
            snapshot.swap(activeResources_->pendingSnapshot);
        }
        if (!snapshot.isEmpty()) {
            addRequestMetadata(snapshot, currentTrackId_, activeGeneration_,
                               currentFrequencyRequested_, currentFrequencyRequested_);
            currentLayers_ = snapshot;
            emit waveformReady(currentPath_, snapshot);
        }
    }
}

void WaveformProvider::cancelActiveJob()
{
    if (activeResources_ != nullptr) activeResources_->cancel();
    if (watcher_ != nullptr) {
        watcher_->disconnect(this);
        delete watcher_;
        watcher_ = nullptr;
    }
    activeResources_.reset();
    activeProgress_.reset();
    progressTimer_->stop();
}

void WaveformProvider::cancelPrefetchJobs()
{
    if (prefetchResources_ != nullptr) prefetchResources_->cancel();
    prefetchResources_.reset();
    currentAnalysisPool_.clear();
}

void WaveformProvider::loadForTrack(const QString& path)
{
    loadForTrack(path, path);
}

qulonglong WaveformProvider::loadForTrack(const QString& trackId,
                                          const QString& path,
                                          const bool frequencyColor)
{
    const ag_waveform_aggregation requestedAggregation =
        aggregationForSettings(settings_);
    if (!path.isEmpty() && path == currentPath_
        && trackId == currentTrackId_
        && requestedAggregation == currentAggregation_) {
        currentFrequencyRequested_ = frequencyColor;
        if (watcher_ != nullptr) {
            return activeGeneration_;
        }
        if (!currentLayers_.isEmpty()) {
            addRequestMetadata(currentLayers_, currentTrackId_,
                               activeGeneration_, frequencyColor,
                               frequencyColor);
            setAnalysisProgress(1.0);
            emit waveformReady(path, currentLayers_);
            return activeGeneration_;
        }
    }

    cancelActiveJob();
    cancelPrefetchJobs();

    currentPath_ = path;
    currentTrackId_ = trackId;
    currentLayers_.clear();
    ++activeGeneration_;
    emit activeGenerationChanged();
    currentAggregation_ = requestedAggregation;
    currentFrequencyRequested_ = frequencyColor;
    setAnalysisProgress(path.isEmpty() ? 1.0 : 0.0);

    if (path.isEmpty()) {
        emit waveformReady(path, QVariantMap{});
        return activeGeneration_;
    }

    const QString cachePath =
        cacheFilePathFor(settings_, path, currentAggregation_);
    // Small warm caches remain immediate. Large six-layer payloads expand
    // into millions of variants, so read and convert them on the existing pool.
    if (!cachePath.isEmpty() && QFileInfo(cachePath).size() > 1024 * 1024) {
        startAnalysis();
        return activeGeneration_;
    }
    if (!cachePath.isEmpty()) {
        const std::filesystem::path source = filesystemPath(path);
        const std::filesystem::path cache = filesystemPath(cachePath);
        agplayer::WaveformCacheData data;
        if (agplayer::WaveformCache::load_v4(cache, source, data)) {
            if (data.duration_ms > 0U && data.total_samples > 0U
                && data.sample_rate > 0U && !data.mix.empty()
                && data.peak.size() == data.mix.size()
                && data.rms.size() == data.mix.size()) {
                QVariantMap layers = layersFromCache(data);
                addTimelineMetadata(layers, data.total_samples,
                                    static_cast<int>(data.sample_rate));
                layers[QStringLiteral("_cacheVersion")] = 4;
                addRequestMetadata(layers, currentTrackId_, activeGeneration_,
                                   frequencyColor, frequencyColor);
                currentLayers_ = layers;
                emit waveformReady(path, layers);
                setAnalysisProgress(1.0);
                return activeGeneration_;
            }
        }
    }

    startAnalysis();
    return activeGeneration_;
}

void WaveformProvider::startAnalysis()
{
    const auto progress = std::make_shared<std::atomic<double>>(0.0);
    activeProgress_ = progress;
    const auto resources = std::make_shared<AnalysisResources>();
    resources->cancelToken = ag_cancel_token_create();
    activeResources_ = resources;

    const QByteArray source = currentPath_.toUtf8();
    const QString sourcePath = currentPath_;
    const QString sourceTrackId = currentTrackId_;
    const quint64 sourceGeneration = activeGeneration_;
    const QString cachePath = cacheFilePathFor(settings_, sourcePath, currentAggregation_);

    watcher_ = new QFutureWatcher<Job>(this);
    connect(watcher_, &QFutureWatcher<Job>::finished,
            this, &WaveformProvider::onAnalysisFinished);
    QFuture<Job> future = QtConcurrent::task(
        [source, sourcePath, sourceTrackId, sourceGeneration, cachePath,
         resources, progress, aggregation = currentAggregation_]() mutable {
            Job job;
            job.path = sourcePath;
            job.trackId = sourceTrackId;
            job.generation = sourceGeneration;
            job.aggregation = aggregation;
            job.resources = resources;
            job.progress = progress;
            if (resources->canceled.load(std::memory_order_relaxed)) {
                job.result = AG_CANCELLED;
                return job;
            }
            agplayer::WaveformCacheData cached;
            if (!cachePath.isEmpty()
                && agplayer::WaveformCache::load_v4(filesystemPath(cachePath),
                                                   filesystemPath(sourcePath), cached)
                && cached.duration_ms > 0 && cached.total_samples > 0
                && cached.sample_rate > 0 && !cached.mix.empty()
                && cached.peak.size() == cached.mix.size()
                && cached.rms.size() == cached.mix.size()) {
                if (!resources->canceled.load(std::memory_order_relaxed)) {
                    job.layers = layersFromCache(cached);
                    addTimelineMetadata(job.layers, cached.total_samples,
                                        static_cast<int>(cached.sample_rate));
                    job.layers[QStringLiteral("_cacheVersion")] = 4;
                } else {
                    job.result = AG_CANCELLED;
                }
                return job;
            }
            if (resources->canceled.load(std::memory_order_relaxed)) {
                job.result = AG_CANCELLED;
                return job;
            }
            providerCounters.jobsStarted.fetch_add(
                1U, std::memory_order_relaxed);
            job.result = ag_waveform_analyze_progressive(
                source.constData(), analysisPointsFor(source), aggregation,
                resources->cancelToken,
                [](const float p, void* userData) {
                    static_cast<std::atomic<double>*>(userData)->store(
                        static_cast<double>(p), std::memory_order_relaxed);
                }, progress.get(),
                [](const ag_waveform_snapshot* snapshot, void* userData) {
                    auto& state = *static_cast<AnalysisResources*>(userData);
                    if (state.canceled.load(std::memory_order_relaxed)) return;
                    QVariantMap layers;
                    const auto layer = [snapshot](const float* values) {
                        QVariantList result;
                        result.reserve(static_cast<int>(snapshot->count));
                        for (std::size_t i = 0; i < snapshot->count; ++i)
                            result.append(static_cast<double>(values[i]));
                        return result;
                    };
                    layers[QStringLiteral("mix")] = layer(snapshot->mix);
                    layers[QStringLiteral("peak")] = layer(snapshot->peak);
                    layers[QStringLiteral("rms")] = layer(snapshot->rms);
                    layers[QStringLiteral("bass")] = layer(snapshot->bass);
                    layers[QStringLiteral("mid")] = layer(snapshot->mid);
                    layers[QStringLiteral("high")] = layer(snapshot->high);
                    layers[QStringLiteral("_complete")] = false;
                    layers[QStringLiteral("_durationMs")] = static_cast<qulonglong>(
                        snapshot->total_samples * 1000U / static_cast<unsigned>(snapshot->sample_rate));
                    addTimelineMetadata(layers, snapshot->total_samples, snapshot->sample_rate);
                    std::lock_guard lock(state.snapshotMutex);
                    state.pendingSnapshot = std::move(layers);
                }, resources.get(), &resources->waveform);
            if (job.result == AG_OK && resources->waveform != nullptr
                && !resources->canceled.load(std::memory_order_relaxed)) {
                job.layers = layersFromWaveform(resources->waveform);
                addTimelineMetadata(job.layers, ag_waveform_total_samples(resources->waveform),
                                    ag_waveform_sample_rate(resources->waveform));
                job.layers[QStringLiteral("_cacheVersion")] = 4;
                if (!cachePath.isEmpty()
                    && !resources->canceled.load(std::memory_order_relaxed)) {
                    job.cacheSaved = saveWaveformCache(cachePath, sourcePath, resources->waveform);
                }
            }
            return job;
        })
        .onThreadPool(currentAnalysisPool_)
        .withPriority(1)
        .spawn();
    watcher_->setFuture(future);
    progressTimer_->start();
}

void WaveformProvider::prefetchTracks(const QStringList& paths)
{
    const QString cacheDirectory =
        settings_ != nullptr ? settings_->cacheDirectory() : QString();
    const ag_waveform_aggregation aggregation =
        aggregationForSettings(settings_);
    if (cacheDirectory.isEmpty() || paths.isEmpty()) {
        return;
    }

    // Share cancellation across this generation's background jobs. The running
    // decode must yield too: thread-pool priority only orders queued work.
    if (prefetchResources_ == nullptr) {
        prefetchResources_ = std::make_shared<AnalysisResources>();
        prefetchResources_->cancelToken = ag_cancel_token_create();
    }
    const auto resources = prefetchResources_;
    if (resources->cancelToken == nullptr) return;

    QSet<QString> queuedPaths;
    for (const QString& path : paths) {
        if (path.isEmpty() || queuedPaths.contains(path)) {
            continue;
        }
        queuedPaths.insert(path);
        const QPointer<WaveformProvider> guard(this);
        QtConcurrent::task([path, cacheDirectory, aggregation, guard, resources] {
            if (resources->canceled.load(std::memory_order_relaxed)) return;
            const QString cachePath = cacheFilePathForDirectory(
                cacheDirectory, path, aggregation);
            if (cachePath.isEmpty()) {
                return;
            }
            const std::filesystem::path sourcePath = filesystemPath(path);
            const std::filesystem::path cacheFile = filesystemPath(cachePath);
            agplayer::WaveformCacheData cached;
            if (agplayer::WaveformCache::load_v4(
                    cacheFile, sourcePath, cached)
                && !cached.mix.empty() && cached.peak.size() == cached.mix.size()
                && cached.rms.size() == cached.mix.size()) {
                return;
            }

            ag_waveform* waveform = nullptr;
            const QByteArray encodedPath = path.toUtf8();
            if (resources->canceled.load(std::memory_order_relaxed)) return;
            providerCounters.prefetchJobsStarted.fetch_add(
                1U, std::memory_order_relaxed);
            const ag_result result = ag_waveform_analyze_with_aggregation(
                encodedPath.constData(), analysisPointsFor(encodedPath), aggregation,
                resources->cancelToken,
                nullptr, nullptr, &waveform);
            if (result == AG_OK && waveform != nullptr
                && !resources->canceled.load(std::memory_order_relaxed)) {
                // An in-progress filesystem operation cannot be interrupted;
                // keep its valid cache but suppress obsolete notifications.
                if (saveWaveformCache(cachePath, path, waveform)
                    && !resources->canceled.load(std::memory_order_relaxed)
                    && !guard.isNull()) {
                    QMetaObject::invokeMethod(
                        guard, [guard, path, resources] {
                            if (!guard.isNull()
                                && !resources->canceled.load(std::memory_order_relaxed)) {
                                emit guard->waveformCacheReady(path);
                            }
                        }, Qt::QueuedConnection);
                }
            }
            if (waveform != nullptr) {
                ag_waveform_destroy(waveform);
            }
        })
            .onThreadPool(currentAnalysisPool_)
            .withPriority(-1)
            .spawn(QtConcurrent::FutureResult::Ignore);
    }
}

void WaveformProvider::cancelForTrack(const QString& path)
{
    if (path != currentPath_) {
        return;
    }
    cancelActiveJob();
    cancelPrefetchJobs();
    currentPath_.clear();
    currentTrackId_.clear();
    currentLayers_.clear();
    currentFrequencyRequested_ = false;
    ++activeGeneration_;
    emit activeGenerationChanged();
    setAnalysisProgress(0.0);
}

void WaveformProvider::onAnalysisFinished()
{
    if (watcher_ == nullptr) {
        return;
    }
    const Job job = watcher_->result();
    activeProgress_.reset();
    progressTimer_->stop();

    if (job.resources == activeResources_) activeResources_.reset();

    delete watcher_;
    watcher_ = nullptr;

    if (job.result != AG_OK || job.layers.isEmpty()
        || job.path != currentPath_
        || job.trackId != currentTrackId_
        || job.generation != activeGeneration_
        || job.aggregation != currentAggregation_) {
        if (job.path == currentPath_ && job.trackId == currentTrackId_
            && job.generation == activeGeneration_
            && !currentLayers_.value(QStringLiteral("_complete")).toBool()) {
            currentLayers_.clear();
            QVariantMap cleared;
            cleared[QStringLiteral("_complete")] = false;
            addRequestMetadata(cleared, job.trackId, job.generation,
                               currentFrequencyRequested_, false);
            emit waveformReady(job.path, cleared);
            emit waveformFailed(job.path, job.trackId, job.generation,
                                static_cast<int>(job.result));
        } else if (!currentLayers_.isEmpty()) {
            setAnalysisProgress(1.0);
        }
        return;
    }

    if (job.cacheSaved) {
        if (settings_ != nullptr) {
            settings_->onWaveformCacheSaved();
        }
        emit waveformCacheReady(currentPath_);
    }

    QVariantMap result = job.layers;
    addRequestMetadata(result, currentTrackId_, activeGeneration_,
                       currentFrequencyRequested_,
                       currentFrequencyRequested_);
    currentLayers_ = result;
    setAnalysisProgress(1.0);
    emit waveformReady(currentPath_, result);
}
