#include "waveform_provider.hpp"
#include "runtime_log.hpp"
#include "settings_controller.hpp"
#include "waveform_cache.hpp"

#include <QDir>
#include <QFile>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QtConcurrent/QtConcurrent>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

struct ProviderCounters final {
    std::atomic<std::uint64_t> frequencyJobsStarted{0U};
    std::atomic<std::uint64_t> mixJobsStarted{0U};
};

ProviderCounters providerCounters;

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
    const ag_waveform_aggregation aggregation,
    const bool legacyV2 = false)
{
    if (directory.isEmpty() || sourcePath.isEmpty()) {
        return {};
    }
    if (!QDir().mkpath(directory)) {
        return {};
    }
    const std::string key = legacyV2
        ? agplayer::WaveformCache::legacy_v2_key_for(
              filesystemPath(sourcePath))
        : agplayer::WaveformCache::key_for(filesystemPath(sourcePath));
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

QVariantList spectralIndexFromWaveform(const ag_waveform* waveform)
{
    QVariantList result;
    const std::size_t count = ag_waveform_spectral_index_count(waveform);
    result.reserve(static_cast<int>(count));
    for (std::size_t index = 0U; index < count; ++index) {
        result.append(ag_waveform_spectral_index(waveform, index));
    }
    return result;
}

QVariantList spectralIndexFromBytes(
    const std::vector<std::uint8_t>& spectralIndex)
{
    QVariantList result;
    result.reserve(static_cast<int>(spectralIndex.size()));
    for (const std::uint8_t value : spectralIndex) {
        result.append(value);
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
    layers[QStringLiteral("spectralIndex")] =
        spectralIndexFromWaveform(waveform);
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
    layers[QStringLiteral("spectralIndex")] =
        spectralIndexFromBytes(data.spectral_index);
    layers[QStringLiteral("_durationMs")] =
        static_cast<qlonglong>(data.duration_ms);
    layers[QStringLiteral("_bpm")] = data.bpm;
    return layers;
}

void addSpectralIndex(QVariantMap& layers,
                      const QVariantList& spectralIndex)
{
    layers[QStringLiteral("spectralIndex")] = spectralIndex;
    layers[QStringLiteral("_frequencyRequested")] = true;
    layers[QStringLiteral("_frequencyReady")] = true;
    layers[QStringLiteral("_frequencyCacheVersion")] = 3;
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
    layers[QStringLiteral("_frequencyCacheVersion")] = frequencyReady ? 3 : 0;
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
                       const bool mixOnly = false,
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
    data.bass.resize(mixOnly ? 0U
                             : ag_waveform_layer_count(
                                   waveform, AG_WAVEFORM_LAYER_BASS));
    for (std::size_t index = 0; index < data.bass.size(); ++index) {
        data.bass[index] = ag_waveform_layer_peak(waveform, AG_WAVEFORM_LAYER_BASS, index);
    }
    data.mid.resize(mixOnly ? 0U
                            : ag_waveform_layer_count(
                                  waveform, AG_WAVEFORM_LAYER_MID));
    for (std::size_t index = 0; index < data.mid.size(); ++index) {
        data.mid[index] = ag_waveform_layer_peak(waveform, AG_WAVEFORM_LAYER_MID, index);
    }
    data.high.resize(mixOnly ? 0U
                             : ag_waveform_layer_count(
                                   waveform, AG_WAVEFORM_LAYER_HIGH));
    for (std::size_t index = 0; index < data.high.size(); ++index) {
        data.high[index] = ag_waveform_layer_peak(waveform, AG_WAVEFORM_LAYER_HIGH, index);
    }
    data.spectral_index.resize(
        ag_waveform_spectral_index_count(waveform));
    for (std::size_t index = 0U; index < data.spectral_index.size(); ++index) {
        data.spectral_index[index] = ag_waveform_spectral_index(waveform, index);
    }
    const double analyzedBpm = ag_waveform_bpm(waveform);
    data.bpm = analyzedBpm > 0.0 ? analyzedBpm : fallbackBpm;
    data.duration_ms = ag_waveform_duration_ms(waveform);
    data.total_samples = ag_waveform_total_samples(waveform);
    data.sample_rate = static_cast<std::uint32_t>(
        std::max(0, ag_waveform_sample_rate(waveform)));
    if (!agplayer::WaveformCache::save_v3(
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
    providerCounters.frequencyJobsStarted.store(0U, std::memory_order_relaxed);
    providerCounters.mixJobsStarted.store(0U, std::memory_order_relaxed);
}

std::uint64_t waveform_provider_frequency_jobs_started() noexcept
{
    return providerCounters.frequencyJobsStarted.load(std::memory_order_relaxed);
}

std::uint64_t waveform_provider_mix_jobs_started() noexcept
{
    return providerCounters.mixJobsStarted.load(std::memory_order_relaxed);
}

} // namespace agplayer::testing

WaveformProvider::WaveformProvider(SettingsController* settings, QObject* parent)
    : QObject(parent)
    , settings_(settings)
{
    currentAnalysisPool_.setMaxThreadCount(1);
    currentAnalysisPool_.setThreadPriority(QThread::LowestPriority);
    powerQueryClock_.start();
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

void WaveformProvider::AnalysisResources::setPaused(const bool paused) const
{
    if (cancelToken != nullptr) {
        ag_cancel_token_set_paused(cancelToken, paused ? 1 : 0);
    }
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
    if (watcher_ != nullptr && activeJobKind_ == JobKind::FrequencyColor) {
        updateFrequencyPause(true);
    }
}

void WaveformProvider::updateFrequencyPause(const bool queryPowerState)
{
    if (activeResources_ == nullptr
        || activeJobKind_ != JobKind::FrequencyColor) {
        return;
    }
    if (queryPowerState) {
        const qint64 now = powerQueryClock_.elapsed();
        if (lastPowerQueryElapsedMs_ < 0
            || now - lastPowerQueryElapsedMs_ >= 5'000) {
            lastPowerQueryElapsedMs_ = now;
#ifdef Q_OS_WIN
            SYSTEM_POWER_STATUS status{};
            if (GetSystemPowerStatus(&status) != FALSE) {
                energySaverActive_ = status.SystemStatusFlag != 0;
            }
#else
            energySaverActive_ = false;
#endif
        }
    }
    activeResources_->setPaused(audioResourcePressure_ || energySaverActive_);
}

void WaveformProvider::setAudioResourcePressure(const bool pressured)
{
    if (audioResourcePressure_ == pressured) return;
    audioResourcePressure_ = pressured;
    updateFrequencyPause(false);
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
    energySaverActive_ = false;
    lastPowerQueryElapsedMs_ = -1;
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
        && requestedAggregation == currentAggregation_
        && frequencyColor == currentFrequencyRequested_) {
        if (watcher_ != nullptr) {
            return activeGeneration_;
        }
        if (!currentLayers_.isEmpty()) {
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
    currentMixLayers_.clear();
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
    const QString legacyCachePath = settings_ != nullptr
        ? cacheFilePathForDirectory(settings_->cacheDirectory(), path,
                                    currentAggregation_, true)
        : QString();
    const QStringList cacheCandidates = cachePath == legacyCachePath
        ? QStringList{cachePath}
        : QStringList{cachePath, legacyCachePath};
    for (const QString& candidate : cacheCandidates) {
        if (candidate.isEmpty()) continue;
        const std::filesystem::path source = filesystemPath(path);
        const std::filesystem::path cache = filesystemPath(candidate);
        agplayer::WaveformCacheData data;
        bool cacheV3 = agplayer::WaveformCache::load_v3(cache, source, data);
        if (cacheV3 || agplayer::WaveformCache::load_v2(cache, source, data)) {
            if (data.duration_ms > 0U && data.total_samples > 0U
                && data.sample_rate > 0U && !data.mix.empty()) {
                QVariantMap layers = layersFromCache(data);
                addTimelineMetadata(layers, data.total_samples,
                                    static_cast<int>(data.sample_rate));
                const bool spectralReady = cacheV3
                    && data.spectral_index.size() == data.mix.size();
                layers[QStringLiteral("_cacheVersion")] = cacheV3 ? 3 : 2;
                addRequestMetadata(layers, currentTrackId_, activeGeneration_,
                                   frequencyColor,
                                   frequencyColor && spectralReady);
                currentMixLayers_ = layers;
                currentLayers_ = layers;
                emit waveformReady(path, layers);
                if (!frequencyColor || spectralReady) {
                    setAnalysisProgress(1.0);
                    return activeGeneration_;
                }
            }
        }
    }

    if (frequencyColor) {
        startAnalysis(true);
        return activeGeneration_;
    }

    startAnalysis(false);
    return activeGeneration_;
}

void WaveformProvider::startAnalysis(const bool frequencyColor)
{
    const auto progress = std::make_shared<std::atomic<double>>(0.0);
    activeProgress_ = progress;
    const auto resources = std::make_shared<AnalysisResources>();
    resources->cancelToken = ag_cancel_token_create();
    activeResources_ = resources;
    activeJobKind_ = frequencyColor ? JobKind::FrequencyColor
                                    : JobKind::MixOnly;
    if (frequencyColor) updateFrequencyPause(false);

    const QByteArray source = currentPath_.toUtf8();
    const QString sourcePath = currentPath_;
    const QString sourceTrackId = currentTrackId_;
    const quint64 sourceGeneration = activeGeneration_;
    constexpr std::size_t targetPoints = 2000;

    watcher_ = new QFutureWatcher<Job>(this);
    connect(watcher_, &QFutureWatcher<Job>::finished,
            this, &WaveformProvider::onAnalysisFinished);
    QFuture<Job> future = QtConcurrent::task(
        [source, sourcePath, sourceTrackId, sourceGeneration, targetPoints,
         resources, progress, aggregation = currentAggregation_,
         kind = activeJobKind_,
         frequencyRequested = currentFrequencyRequested_]() mutable {
            Job job;
            job.path = sourcePath;
            job.trackId = sourceTrackId;
            job.generation = sourceGeneration;
            job.kind = kind;
            job.frequencyRequested = frequencyRequested;
            job.aggregation = aggregation;
            job.resources = resources;
            job.progress = progress;
            if (kind == JobKind::FrequencyColor) {
                providerCounters.frequencyJobsStarted.fetch_add(
                    1U, std::memory_order_relaxed);
                job.result = ag_waveform_analyze_with_spectral_index(
                    source.constData(), targetPoints, aggregation,
                    resources->cancelToken,
                    [](const float p, void* userData) {
                        static_cast<std::atomic<double>*>(userData)->store(
                            static_cast<double>(p), std::memory_order_relaxed);
                    }, progress.get(), &resources->waveform);
            } else {
                providerCounters.mixJobsStarted.fetch_add(
                    1U, std::memory_order_relaxed);
                job.result = ag_track_analysis_with_aggregation(
                    source.constData(), targetPoints, aggregation,
                    resources->cancelToken,
                    [](const float p, void* userData) {
                        static_cast<std::atomic<double>*>(userData)->store(
                            static_cast<double>(p), std::memory_order_relaxed);
                    }, progress.get(), &resources->waveform, nullptr);
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

    QSet<QString> queuedPaths;
    for (const QString& path : paths) {
        if (path.isEmpty() || queuedPaths.contains(path)) {
            continue;
        }
        queuedPaths.insert(path);
        const QPointer<WaveformProvider> guard(this);
        QtConcurrent::task([path, cacheDirectory, aggregation, guard] {
            constexpr std::size_t targetPoints = 2000;
            const QString cachePath = cacheFilePathForDirectory(
                cacheDirectory, path, aggregation);
            if (cachePath.isEmpty()) {
                return;
            }
            const std::filesystem::path sourcePath = filesystemPath(path);
            const std::filesystem::path cacheFile = filesystemPath(cachePath);
            agplayer::WaveformCacheData cached;
            if (agplayer::WaveformCache::load_v3(
                    cacheFile, sourcePath, cached)
                && !cached.mix.empty()
                && cached.spectral_index.size() == cached.mix.size()) {
                return;
            }

            ag_waveform* waveform = nullptr;
            const QByteArray encodedPath = path.toUtf8();
            const ag_result result = ag_waveform_analyze_with_spectral_index(
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
    currentMixLayers_.clear();
    currentFrequencyRequested_ = false;
    ++activeGeneration_;
    emit activeGenerationChanged();
    setAnalysisProgress(0.0);
}

void WaveformProvider::cancelFrequencyForTrack(const QString& path)
{
    if (path != currentPath_ || !currentFrequencyRequested_) return;
    if (watcher_ != nullptr && activeJobKind_ == JobKind::FrequencyColor
        && activeResources_ != nullptr) {
        activeResources_->cancel();
    }
    if (watcher_ != nullptr) {
        watcher_->disconnect(this);
        delete watcher_;
        watcher_ = nullptr;
    }
    activeResources_.reset();
    activeProgress_.reset();
    progressTimer_->stop();
    energySaverActive_ = false;
    lastPowerQueryElapsedMs_ = -1;
    currentFrequencyRequested_ = false;
    ++activeGeneration_;
    emit activeGenerationChanged();
    if (!currentMixLayers_.isEmpty()) {
        addRequestMetadata(currentMixLayers_, currentTrackId_,
                           activeGeneration_, false, false);
        currentLayers_ = currentMixLayers_;
        emit waveformReady(currentPath_, currentLayers_);
    }
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
        || job.aggregation != currentAggregation_
        || job.kind != activeJobKind_
        || job.frequencyRequested != currentFrequencyRequested_) {
        if (job.path == currentPath_ && job.trackId == currentTrackId_
            && job.generation == activeGeneration_
            && currentMixLayers_.isEmpty()) {
            emit waveformFailed(job.path, job.trackId, job.generation,
                                static_cast<int>(job.result));
        } else if (!currentMixLayers_.isEmpty()) {
            setAnalysisProgress(1.0);
        }
        return;
    }

    const QString cachePath = cacheFilePathFor(
        settings_, currentPath_, job.aggregation);
    if (!cachePath.isEmpty()) {
        if (saveWaveformCache(
                cachePath, currentPath_, waveform, false,
                currentMixLayers_.value(QStringLiteral("_bpm")).toDouble())) {
            if (settings_ != nullptr) {
                settings_->onWaveformCacheSaved();
            }
            emit waveformCacheReady(currentPath_);
            const QString legacyPath = settings_ != nullptr
                ? cacheFilePathForDirectory(
                      settings_->cacheDirectory(), currentPath_,
                      job.aggregation, true)
                : QString();
            if (!legacyPath.isEmpty() && legacyPath != cachePath) {
                QFile::remove(legacyPath);
            }
        }
    }

    QVariantMap result = currentMixLayers_;
    if (result.isEmpty()) {
        result = waveformToVariantMap(waveform);
        if (job.frequencyRequested) {
            result[QStringLiteral("bass")] = QVariantList{};
            result[QStringLiteral("mid")] = QVariantList{};
            result[QStringLiteral("high")] = QVariantList{};
        }
        addTimelineMetadata(result,
                            ag_waveform_total_samples(waveform),
                            ag_waveform_sample_rate(waveform));
        result[QStringLiteral("_cacheVersion")] = 3;
        addRequestMetadata(result, currentTrackId_, activeGeneration_,
                           job.frequencyRequested, false);
        currentMixLayers_ = result;
        currentLayers_ = result;
        emit waveformReady(currentPath_, result);
    }

    if (!job.frequencyRequested) {
        setAnalysisProgress(1.0);
        return;
    }

    const QVariantList spectralIndex = spectralIndexFromWaveform(waveform);
    const std::size_t mixCount = static_cast<std::size_t>(
        result.value(QStringLiteral("mix")).toList().size());
    if (mixCount == 0U
        || static_cast<std::size_t>(spectralIndex.size()) != mixCount) {
        setAnalysisProgress(1.0);
        return;
    }
    QVariantMap complete = result;
    addSpectralIndex(complete, spectralIndex);
    currentLayers_ = complete;
    setAnalysisProgress(1.0);
    emit waveformReady(currentPath_, complete);
}

QVariantMap WaveformProvider::waveformToVariantMap(
    const ag_waveform* waveform) const
{
    return layersFromWaveform(waveform);
}
