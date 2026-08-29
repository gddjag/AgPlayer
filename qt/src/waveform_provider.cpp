#include "waveform_provider.hpp"
#include "runtime_log.hpp"
#include "settings_controller.hpp"
#include "waveform_cache.hpp"

#include <QDir>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace {

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

void saveWaveformCache(const QString& cachePath,
                       const QString& sourcePath,
                       const ag_waveform* waveform)
{
    if (waveform == nullptr || cachePath.isEmpty() || sourcePath.isEmpty()) {
        return;
    }
    agplayer::WaveformCacheData data;
    const std::size_t count = ag_waveform_count(waveform);
    data.mix.resize(count);
    for (std::size_t index = 0; index < count; ++index) {
        data.mix[index] = ag_waveform_peak(waveform, index);
    }
    data.bass.resize(ag_waveform_layer_count(waveform, AG_WAVEFORM_LAYER_BASS));
    for (std::size_t index = 0; index < data.bass.size(); ++index) {
        data.bass[index] = ag_waveform_layer_peak(waveform, AG_WAVEFORM_LAYER_BASS, index);
    }
    data.mid.resize(ag_waveform_layer_count(waveform, AG_WAVEFORM_LAYER_MID));
    for (std::size_t index = 0; index < data.mid.size(); ++index) {
        data.mid[index] = ag_waveform_layer_peak(waveform, AG_WAVEFORM_LAYER_MID, index);
    }
    data.high.resize(ag_waveform_layer_count(waveform, AG_WAVEFORM_LAYER_HIGH));
    for (std::size_t index = 0; index < data.high.size(); ++index) {
        data.high[index] = ag_waveform_layer_peak(waveform, AG_WAVEFORM_LAYER_HIGH, index);
    }
    data.bpm = ag_waveform_bpm(waveform);
    data.duration_ms = ag_waveform_duration_ms(waveform);
    data.total_samples = ag_waveform_total_samples(waveform);
    data.sample_rate = static_cast<std::uint32_t>(
        std::max(0, ag_waveform_sample_rate(waveform)));
    if (!agplayer::WaveformCache::save_v2(
            filesystemPath(cachePath), filesystemPath(sourcePath), data)) {
        RuntimeLog::log(AG_IO_ERROR, QStringLiteral("Waveform"),
            QStringLiteral("Failed to save waveform cache for %1")
                .arg(sourcePath));
    }
}

} // namespace

WaveformProvider::WaveformProvider(SettingsController* settings, QObject* parent)
    : QObject(parent)
    , settings_(settings)
{
    currentAnalysisPool_.setMaxThreadCount(1);
    currentAnalysisPool_.setThreadPriority(QThread::HighPriority);
    prefetchPool_.setMaxThreadCount(1);
    prefetchPool_.setThreadPriority(QThread::LowPriority);
    progressTimer_ = new QTimer(this);
    progressTimer_->setInterval(100);
    connect(progressTimer_, &QTimer::timeout, this, [this] {
        if (activeProgress_ == nullptr) {
            return;
        }
        setAnalysisProgress(
            activeProgress_->load(std::memory_order_relaxed));
    });
}

WaveformProvider::~WaveformProvider()
{
    if (activeCancelToken_ != nullptr) {
        ag_cancel_token_cancel(activeCancelToken_);
        activeCancelToken_ = nullptr;
    }
    if (watcher_ != nullptr) {
        watcher_->disconnect(this);
        delete watcher_;
        watcher_ = nullptr;
    }
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

void WaveformProvider::loadForTrack(const QString& path)
{
    loadForTrack(path, path);
}

qulonglong WaveformProvider::loadForTrack(const QString& trackId,
                                          const QString& path)
{
    const ag_waveform_aggregation requestedAggregation =
        aggregationForSettings(settings_);
    if (!path.isEmpty() && path == currentPath_
        && trackId == currentTrackId_
        && requestedAggregation == currentAggregation_) {
        if (watcher_ != nullptr) {
            return activeGeneration_;
        }
        if (!currentLayers_.isEmpty()) {
            setAnalysisProgress(1.0);
            emit waveformReady(path, currentLayers_);
            return activeGeneration_;
        }
    }

    // Cancel and discard any running analysis. The background task keeps its
    // own copy of the cancel token, so we only cancel here and let it destroy
    // the token when it finishes.
    if (watcher_ != nullptr) {
        if (activeCancelToken_ != nullptr) {
            ag_cancel_token_cancel(activeCancelToken_);
            activeCancelToken_ = nullptr;
        }
        watcher_->disconnect(this);
        delete watcher_;
        watcher_ = nullptr;
    }
    activeProgress_.reset();
    progressTimer_->stop();

    currentPath_ = path;
    currentTrackId_ = trackId;
    currentLayers_.clear();
    ++activeGeneration_;
    emit activeGenerationChanged();
    currentAggregation_ = requestedAggregation;
    setAnalysisProgress(path.isEmpty() ? 1.0 : 0.0);

    if (path.isEmpty()) {
        emit waveformReady(path, QVariantMap{});
        return activeGeneration_;
    }

    // Try cache first: v2 multi-layer format, then v1 legacy format.
    const QString cachePath =
        cacheFilePathFor(settings_, path, currentAggregation_);
    if (!cachePath.isEmpty()) {
        const std::filesystem::path source = filesystemPath(path);
        const std::filesystem::path cache = filesystemPath(cachePath);
        agplayer::WaveformCacheData data;
        if (agplayer::WaveformCache::load_v2(cache, source, data)) {
            QVariantMap layers;
            layers[QStringLiteral("mix")] = peaksFromVector(data.mix);
            layers[QStringLiteral("bass")] = peaksFromVector(data.bass);
            layers[QStringLiteral("mid")] = peaksFromVector(data.mid);
            layers[QStringLiteral("high")] = peaksFromVector(data.high);
            layers[QStringLiteral("_trackId")] = currentTrackId_;
            layers[QStringLiteral("_generation")] = activeGeneration_;
            layers[QStringLiteral("_cacheVersion")] = 2;
            if (data.duration_ms > 0U && data.total_samples > 0U
                && data.sample_rate > 0U) {
                layers[QStringLiteral("_durationMs")] =
                    static_cast<qlonglong>(data.duration_ms);
                addTimelineMetadata(layers, data.total_samples,
                                    static_cast<int>(data.sample_rate));
                currentLayers_ = layers;
                setAnalysisProgress(1.0);
                emit waveformReady(path, layers);
                return activeGeneration_;
            }
        }
        // Legacy caches do not carry a decoded duration. Rebuild once so
        // drawing, progress and seek share the same exact timeline.
    }

    // Start background analysis.
    const auto progress = std::make_shared<std::atomic<double>>(0.0);
    activeProgress_ = progress;
    ag_cancel_token* cancelToken = ag_cancel_token_create();
    activeCancelToken_ = cancelToken;

    const std::string source = path.toUtf8().toStdString();
    const QString sourceTrackId = currentTrackId_;
    const quint64 sourceGeneration = activeGeneration_;
    constexpr std::size_t targetPoints = 2000;

    watcher_ = new QFutureWatcher<Job>(this);
    connect(watcher_, &QFutureWatcher<Job>::finished,
            this, &WaveformProvider::onAnalysisFinished);
    QFuture<Job> future = QtConcurrent::run(
        &currentAnalysisPool_,
        [source, sourceTrackId, sourceGeneration, targetPoints, cancelToken, progress,
         aggregation = currentAggregation_]() mutable {
            Job job;
            job.path = QString::fromStdString(source);
            job.trackId = sourceTrackId;
            job.generation = sourceGeneration;
            job.aggregation = aggregation;
            job.cancelToken = cancelToken;
            job.progress = progress;
            job.result = ag_track_analysis_with_aggregation(
                source.c_str(),
                targetPoints,
                aggregation,
                cancelToken,
                [](float p, void* userData) {
                    auto* atomicProgress =
                        static_cast<std::atomic<double>*>(userData);
                    atomicProgress->store(static_cast<double>(p),
                                          std::memory_order_relaxed);
                },
                progress.get(),
                &job.waveform,
                nullptr);
            return job;
        });
    watcher_->setFuture(future);
    progressTimer_->start();
    return activeGeneration_;
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

    (void)QtConcurrent::run(
        &prefetchPool_,
        [paths, cacheDirectory, aggregation] {
            constexpr std::size_t targetPoints = 2000;
            for (const QString& path : paths) {
                if (path.isEmpty()) {
                    continue;
                }
                const QString cachePath =
                    cacheFilePathForDirectory(
                        cacheDirectory, path, aggregation);
                if (cachePath.isEmpty()) {
                    continue;
                }
                const std::filesystem::path sourcePath = filesystemPath(path);
                const std::filesystem::path cacheFile =
                    filesystemPath(cachePath);
                agplayer::WaveformCacheData cached;
                std::vector<float> legacy;
                if (agplayer::WaveformCache::load_v2(
                        cacheFile, sourcePath, cached)
                    || agplayer::WaveformCache::load(
                        cacheFile, sourcePath, legacy)) {
                    continue;
                }

                ag_waveform* waveform = nullptr;
                const QByteArray encodedPath = path.toUtf8();
                const ag_result result = ag_track_analysis_with_aggregation(
                    encodedPath.constData(),
                    targetPoints,
                    aggregation,
                    nullptr,
                    nullptr,
                    nullptr,
                    &waveform,
                    nullptr);
                if (result == AG_OK && waveform != nullptr) {
                    saveWaveformCache(cachePath, path, waveform);
                }
                if (waveform != nullptr) {
                    ag_waveform_destroy(waveform);
                }
            }
        });
}

void WaveformProvider::cancelForTrack(const QString& path)
{
    if (path != currentPath_) {
        return;
    }
    if (activeCancelToken_ != nullptr) {
        ag_cancel_token_cancel(activeCancelToken_);
        activeCancelToken_ = nullptr;
    }
    if (watcher_ != nullptr) {
        watcher_->disconnect(this);
        delete watcher_;
        watcher_ = nullptr;
    }
    activeProgress_.reset();
    progressTimer_->stop();
    currentPath_.clear();
    currentTrackId_.clear();
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

    if (job.cancelToken != nullptr) {
        if (job.cancelToken == activeCancelToken_) {
            activeCancelToken_ = nullptr;
        }
        ag_cancel_token_destroy(job.cancelToken);
    }

    delete watcher_;
    watcher_ = nullptr;

    if (job.result != AG_OK || job.waveform == nullptr
        || job.path != currentPath_
        || job.trackId != currentTrackId_
        || job.generation != activeGeneration_
        || job.aggregation != currentAggregation_) {
        if (job.waveform != nullptr) {
            ag_waveform_destroy(job.waveform);
        }
        if (job.path == currentPath_ && job.trackId == currentTrackId_
            && job.generation == activeGeneration_) {
            setAnalysisProgress(0.0);
            emit waveformFailed(job.path, job.trackId, job.generation,
                                static_cast<int>(job.result));
        }
        return;
    }

    const QString cachePath =
        cacheFilePathFor(settings_, currentPath_, job.aggregation);
    if (!cachePath.isEmpty()) {
        saveWaveformCache(cachePath, currentPath_, job.waveform);
        if (settings_ != nullptr) {
            settings_->onWaveformCacheSaved();
        }
    }

    setAnalysisProgress(1.0);
    QVariantMap result = waveformToVariantMap(job.waveform);
    result[QStringLiteral("_trackId")] = currentTrackId_;
    result[QStringLiteral("_generation")] = activeGeneration_;
    addTimelineMetadata(result,
                        ag_waveform_total_samples(job.waveform),
                        ag_waveform_sample_rate(job.waveform));
    result[QStringLiteral("_cacheVersion")] = 2;
    currentLayers_ = result;
    emit waveformReady(currentPath_, result);
    ag_waveform_destroy(job.waveform);
}

QVariantMap WaveformProvider::waveformToVariantMap(
    const ag_waveform* waveform) const
{
    return layersFromWaveform(waveform);
}
