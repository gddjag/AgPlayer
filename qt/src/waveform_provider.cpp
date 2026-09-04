#include "waveform_provider.hpp"
#include "runtime_log.hpp"
#include "settings_controller.hpp"
#include "waveform_cache.hpp"

#include <QDir>
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
};

ProviderCounters providerCounters;

// Four float layers are about 512 KiB per cached track. This is dense enough
// for a few-beat rolling viewport without keeping the full library in memory.
constexpr std::size_t kWaveformAnalysisPoints = 32768;

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
}

std::uint64_t waveform_provider_jobs_started() noexcept
{
    return providerCounters.jobsStarted.load(std::memory_order_relaxed);
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

void WaveformProvider::AnalysisResources::cancel() const
{
    if (cancelToken != nullptr) ag_cancel_token_cancel(cancelToken);
}

WaveformProvider::~WaveformProvider()
{
    if (activeResources_ != nullptr) activeResources_->cancel();
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
    currentAnalysisPool_.clear();

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
    if (!cachePath.isEmpty()) {
        const std::filesystem::path source = filesystemPath(path);
        const std::filesystem::path cache = filesystemPath(cachePath);
        agplayer::WaveformCacheData data;
        if (agplayer::WaveformCache::load_v4(cache, source, data)) {
            if (data.duration_ms > 0U && data.total_samples > 0U
                && data.sample_rate > 0U && !data.mix.empty()) {
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
    constexpr std::size_t targetPoints = kWaveformAnalysisPoints;

    watcher_ = new QFutureWatcher<Job>(this);
    connect(watcher_, &QFutureWatcher<Job>::finished,
            this, &WaveformProvider::onAnalysisFinished);
    QFuture<Job> future = QtConcurrent::task(
        [source, sourcePath, sourceTrackId, sourceGeneration, targetPoints,
         resources, progress, aggregation = currentAggregation_]() mutable {
            Job job;
            job.path = sourcePath;
            job.trackId = sourceTrackId;
            job.generation = sourceGeneration;
            job.aggregation = aggregation;
            job.resources = resources;
            job.progress = progress;
            providerCounters.jobsStarted.fetch_add(
                1U, std::memory_order_relaxed);
            job.result = ag_waveform_analyze_with_aggregation(
                source.constData(), targetPoints, aggregation,
                resources->cancelToken,
                [](const float p, void* userData) {
                    static_cast<std::atomic<double>*>(userData)->store(
                        static_cast<double>(p), std::memory_order_relaxed);
                }, progress.get(), &resources->waveform);
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

    QSet<QString> queuedPaths;
    for (const QString& path : paths) {
        if (path.isEmpty() || queuedPaths.contains(path)) {
            continue;
        }
        queuedPaths.insert(path);
        const QPointer<WaveformProvider> guard(this);
        QtConcurrent::task([path, cacheDirectory, aggregation, guard] {
            constexpr std::size_t targetPoints = kWaveformAnalysisPoints;
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
                && !cached.mix.empty()) {
                return;
            }

            ag_waveform* waveform = nullptr;
            const QByteArray encodedPath = path.toUtf8();
            const ag_result result = ag_waveform_analyze_with_aggregation(
                encodedPath.constData(), targetPoints, aggregation, nullptr,
                nullptr, nullptr, &waveform);
            if (result == AG_OK && waveform != nullptr) {
                if (saveWaveformCache(cachePath, path, waveform)
                    && !guard.isNull()) {
                    QMetaObject::invokeMethod(
                        guard, [guard, path] {
                            if (!guard.isNull()) {
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

    ag_waveform* const waveform = job.resources == nullptr
        ? nullptr : job.resources->waveform;
    if (job.result != AG_OK || waveform == nullptr
        || job.path != currentPath_
        || job.trackId != currentTrackId_
        || job.generation != activeGeneration_
        || job.aggregation != currentAggregation_) {
        if (job.path == currentPath_ && job.trackId == currentTrackId_
            && job.generation == activeGeneration_
            && currentLayers_.isEmpty()) {
            emit waveformFailed(job.path, job.trackId, job.generation,
                                static_cast<int>(job.result));
        } else if (!currentLayers_.isEmpty()) {
            setAnalysisProgress(1.0);
        }
        return;
    }

    const QString cachePath = cacheFilePathFor(
        settings_, currentPath_, job.aggregation);
    if (!cachePath.isEmpty()) {
        if (saveWaveformCache(
                cachePath, currentPath_, waveform,
                currentLayers_.value(QStringLiteral("_bpm")).toDouble())) {
            if (settings_ != nullptr) {
                settings_->onWaveformCacheSaved();
            }
            emit waveformCacheReady(currentPath_);
        }
    }

    QVariantMap result = waveformToVariantMap(waveform);
    addTimelineMetadata(result, ag_waveform_total_samples(waveform),
                        ag_waveform_sample_rate(waveform));
    result[QStringLiteral("_cacheVersion")] = 4;
    addRequestMetadata(result, currentTrackId_, activeGeneration_,
                       currentFrequencyRequested_,
                       currentFrequencyRequested_);
    currentLayers_ = result;
    setAnalysisProgress(1.0);
    emit waveformReady(currentPath_, result);
}

QVariantMap WaveformProvider::waveformToVariantMap(
    const ag_waveform* waveform) const
{
    return layersFromWaveform(waveform);
}
