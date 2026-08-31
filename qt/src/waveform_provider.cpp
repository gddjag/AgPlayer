#include "waveform_provider.hpp"
#include "runtime_log.hpp"
#include "settings_controller.hpp"
#include "frequency_color_waveform_analyzer.hpp"
#include "frequency_color_waveform_cache.hpp"
#include "decoder.hpp"
#include "waveform_cache.hpp"

#include <QDir>
#include <QSet>
#include <QTimer>
#include <QtConcurrent/QtConcurrent>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <mutex>
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
    std::atomic<std::uint64_t> frequencyCacheLoads{0U};
    std::atomic<std::uint64_t> frequencyJobsStarted{0U};
    std::atomic<std::uint64_t> mixJobsStarted{0U};
    std::atomic<std::uint64_t> activeFrequencyJobs{0U};
    std::atomic<std::uint64_t> maxFrequencyJobs{0U};
    std::atomic<std::uint64_t> frequencyProgressCallbacks{0U};
    std::atomic<std::uint64_t> powerQueries{0U};
    std::atomic<std::uint64_t> decoderOpens{0U};
    std::atomic<std::uint64_t> activeWorkerTasks{0U};
    std::atomic<std::uint64_t> maxWorkerTasks{0U};
};

ProviderCounters providerCounters;

using ProviderTaskHook = void (*)(bool, const char*, void*);

struct ProviderTaskHookRegistration final {
    ProviderTaskHook hook = nullptr;
    void* context = nullptr;
};

std::mutex providerTaskHookMutex;
ProviderTaskHookRegistration providerTaskHook;

void invokeProviderTaskHook(const bool prefetch, const QString& path)
{
    ProviderTaskHookRegistration registration;
    {
        std::lock_guard lock(providerTaskHookMutex);
        registration = providerTaskHook;
    }
    if (registration.hook != nullptr) {
        const QByteArray encoded = path.toUtf8();
        registration.hook(prefetch, encoded.constData(), registration.context);
    }
}

void noteWorkerTaskStarted() noexcept
{
    const std::uint64_t active = providerCounters.activeWorkerTasks.fetch_add(
        1U, std::memory_order_relaxed) + 1U;
    std::uint64_t maximum = providerCounters.maxWorkerTasks.load(
        std::memory_order_relaxed);
    while (maximum < active
           && !providerCounters.maxWorkerTasks.compare_exchange_weak(
               maximum, active, std::memory_order_relaxed)) {
    }
}

class WorkerTaskCounter final {
public:
    WorkerTaskCounter() noexcept { noteWorkerTaskStarted(); }
    ~WorkerTaskCounter()
    {
        providerCounters.activeWorkerTasks.fetch_sub(
            1U, std::memory_order_relaxed);
    }
};

void noteFrequencyJobStarted() noexcept
{
    providerCounters.frequencyJobsStarted.fetch_add(1U, std::memory_order_relaxed);
    const std::uint64_t active = providerCounters.activeFrequencyJobs.fetch_add(
        1U, std::memory_order_relaxed) + 1U;
    std::uint64_t maximum = providerCounters.maxFrequencyJobs.load(
        std::memory_order_relaxed);
    while (maximum < active
           && !providerCounters.maxFrequencyJobs.compare_exchange_weak(
               maximum, active, std::memory_order_relaxed)) {
    }
}

class FrequencyJobCounter final {
public:
    FrequencyJobCounter() noexcept { noteFrequencyJobStarted(); }
    ~FrequencyJobCounter()
    {
        providerCounters.activeFrequencyJobs.fetch_sub(
            1U, std::memory_order_relaxed);
    }
};

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

QString frequencyCacheFilePathForDirectory(const QString& directory,
                                           const QString& sourcePath)
{
    if (directory.isEmpty() || sourcePath.isEmpty() || !QDir().mkpath(directory)) {
        return {};
    }
    const std::string key =
        agplayer::WaveformCache::key_for(filesystemPath(sourcePath));
    return key.empty()
        ? QString()
        : QDir(directory).filePath(QString::fromStdString(key)
                                   + QStringLiteral(".fcw1"));
}

QString frequencyCacheFilePathFor(SettingsController* settings,
                                  const QString& sourcePath)
{
    return settings == nullptr
        ? QString()
        : frequencyCacheFilePathForDirectory(settings->cacheDirectory(),
                                             sourcePath);
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

QVariantMap layersFromCache(const agplayer::WaveformCacheData& data,
                            const bool includeLegacyBands)
{
    QVariantMap layers;
    layers[QStringLiteral("mix")] = peaksFromVector(data.mix);
    layers[QStringLiteral("bass")] = includeLegacyBands
        ? peaksFromVector(data.bass) : QVariantList{};
    layers[QStringLiteral("mid")] = includeLegacyBands
        ? peaksFromVector(data.mid) : QVariantList{};
    layers[QStringLiteral("high")] = includeLegacyBands
        ? peaksFromVector(data.high) : QVariantList{};
    layers[QStringLiteral("_durationMs")] =
        static_cast<qlonglong>(data.duration_ms);
    layers[QStringLiteral("_bpm")] = data.bpm;
    return layers;
}

QVariantList peaksFromBytes(const std::vector<std::uint8_t>& peaks)
{
    QVariantList result;
    result.reserve(static_cast<int>(peaks.size()));
    for (const std::uint8_t value : peaks) {
        result.append(static_cast<double>(value) * 0.98 / 255.0);
    }
    return result;
}

void addFrequencyLayers(QVariantMap& layers,
                        const std::vector<std::uint8_t>& low,
                        const std::vector<std::uint8_t>& mid,
                        const std::vector<std::uint8_t>& high)
{
    layers[QStringLiteral("bass")] = peaksFromBytes(low);
    layers[QStringLiteral("mid")] = peaksFromBytes(mid);
    layers[QStringLiteral("high")] = peaksFromBytes(high);
    layers[QStringLiteral("_frequencyRequested")] = true;
    layers[QStringLiteral("_frequencyReady")] = true;
    layers[QStringLiteral("_frequencyCacheVersion")] = 1;
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
    layers[QStringLiteral("_frequencyCacheVersion")] = frequencyReady ? 1 : 0;
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
                       const bool mixOnly = false)
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
        return false;
    }
    return true;
}

agplayer::FrequencyColorCacheData frequencyCacheFromWaveform(
    const ag_waveform* waveform)
{
    agplayer::FrequencyColorCacheData data;
    if (waveform == nullptr) return data;
    const auto quantizedLayer = [waveform](const ag_waveform_layer layer) {
        const std::size_t count = ag_waveform_layer_count(waveform, layer);
        std::vector<std::uint8_t> result(count);
        for (std::size_t index = 0; index < count; ++index) {
            result[index] = agplayer::quantize_frequency_color_peak(
                ag_waveform_layer_peak(waveform, layer, index));
        }
        return result;
    };
    data.low = quantizedLayer(AG_WAVEFORM_LAYER_BASS);
    data.mid = quantizedLayer(AG_WAVEFORM_LAYER_MID);
    data.high = quantizedLayer(AG_WAVEFORM_LAYER_HIGH);
    data.point_count = static_cast<std::uint32_t>(data.low.size());
    data.sample_rate = static_cast<std::uint32_t>(
        std::max(0, ag_waveform_sample_rate(waveform)));
    data.timeline_frames = ag_waveform_total_samples(waveform);
    data.algorithm_version =
        agplayer::FrequencyColorWaveformAnalyzer::kAlgorithmVersion;
    return data;
}

} // namespace

namespace agplayer::testing {

void reset_waveform_provider_counters() noexcept
{
    providerCounters.frequencyCacheLoads.store(0U, std::memory_order_relaxed);
    providerCounters.frequencyJobsStarted.store(0U, std::memory_order_relaxed);
    providerCounters.mixJobsStarted.store(0U, std::memory_order_relaxed);
    providerCounters.maxFrequencyJobs.store(0U, std::memory_order_relaxed);
    providerCounters.frequencyProgressCallbacks.store(0U,
                                                       std::memory_order_relaxed);
    providerCounters.powerQueries.store(0U, std::memory_order_relaxed);
    providerCounters.decoderOpens.store(0U, std::memory_order_relaxed);
    providerCounters.maxWorkerTasks.store(0U, std::memory_order_relaxed);
}

std::uint64_t waveform_provider_frequency_cache_loads() noexcept
{
    return providerCounters.frequencyCacheLoads.load(std::memory_order_relaxed);
}

std::uint64_t waveform_provider_frequency_jobs_started() noexcept
{
    return providerCounters.frequencyJobsStarted.load(std::memory_order_relaxed);
}

std::uint64_t waveform_provider_mix_jobs_started() noexcept
{
    return providerCounters.mixJobsStarted.load(std::memory_order_relaxed);
}

std::uint64_t waveform_provider_max_frequency_jobs() noexcept
{
    return providerCounters.maxFrequencyJobs.load(std::memory_order_relaxed);
}

std::uint64_t waveform_provider_frequency_progress_callbacks() noexcept
{
    return providerCounters.frequencyProgressCallbacks.load(
        std::memory_order_relaxed);
}

std::uint64_t waveform_provider_power_queries() noexcept
{
    return providerCounters.powerQueries.load(std::memory_order_relaxed);
}

std::uint64_t waveform_provider_decoder_opens() noexcept
{
    return providerCounters.decoderOpens.load(std::memory_order_relaxed);
}

std::uint64_t waveform_provider_max_worker_tasks() noexcept
{
    return providerCounters.maxWorkerTasks.load(std::memory_order_relaxed);
}

void set_waveform_provider_task_hook(
    void (*hook)(bool, const char*, void*),
    void* context) noexcept
{
    try {
        std::lock_guard lock(providerTaskHookMutex);
        providerTaskHook = {hook, context};
    } catch (...) {
    }
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
            providerCounters.powerQueries.fetch_add(
                1U, std::memory_order_relaxed);
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

    bool mixCacheHit = false;
    const QString cachePath =
        cacheFilePathFor(settings_, path, currentAggregation_);
    if (!cachePath.isEmpty()) {
        const std::filesystem::path source = filesystemPath(path);
        const std::filesystem::path cache = filesystemPath(cachePath);
        agplayer::WaveformCacheData data;
        if (agplayer::WaveformCache::load_v2(cache, source, data)) {
            if (data.duration_ms > 0U && data.total_samples > 0U
                && data.sample_rate > 0U && !data.mix.empty()) {
                QVariantMap layers = layersFromCache(data, !frequencyColor);
                addTimelineMetadata(layers, data.total_samples,
                                    static_cast<int>(data.sample_rate));
                layers[QStringLiteral("_cacheVersion")] = 2;
                addRequestMetadata(layers, currentTrackId_, activeGeneration_,
                                   frequencyColor, false);
                currentMixLayers_ = layers;
                currentLayers_ = layers;
                mixCacheHit = true;
                emit waveformReady(path, layers);
                if (!frequencyColor) {
                    setAnalysisProgress(1.0);
                    return activeGeneration_;
                }
            }
        }
    }

    if (frequencyColor) {
        agplayer::FrequencyColorCacheData frequency;
        const QString frequencyPath = frequencyCacheFilePathFor(settings_, path);
        bool frequencyHit = false;
        if (!frequencyPath.isEmpty()) {
            providerCounters.frequencyCacheLoads.fetch_add(
                1U, std::memory_order_relaxed);
            frequencyHit = agplayer::FrequencyColorWaveformCache::load(
                filesystemPath(frequencyPath), filesystemPath(path),
                agplayer::FrequencyColorWaveformAnalyzer::kAlgorithmVersion,
                frequency);
        }
        if (frequencyHit && mixCacheHit) {
            const int mixCount = currentMixLayers_
                .value(QStringLiteral("mix")).toList().size();
            frequencyHit = mixCount > 0
                && static_cast<std::size_t>(mixCount) == frequency.low.size()
                && frequency.low.size() == frequency.mid.size()
                && frequency.mid.size() == frequency.high.size()
                && frequency.sample_rate
                    == currentMixLayers_.value(
                        QStringLiteral("_sampleRate")).toUInt()
                && frequency.timeline_frames
                    == currentMixLayers_.value(
                        QStringLiteral("_totalSamples")).toULongLong();
        }
        if (frequencyHit && mixCacheHit) {
            QVariantMap complete = currentMixLayers_;
            addFrequencyLayers(complete, frequency.low, frequency.mid,
                               frequency.high);
            currentLayers_ = complete;
            setAnalysisProgress(1.0);
            emit waveformReady(path, complete);
            return activeGeneration_;
        }
        startAnalysis(true);
        return activeGeneration_;
    }

    startAnalysis(false);
    return activeGeneration_;
}

void WaveformProvider::startAnalysis(
    const bool frequencyColor,
    std::vector<std::uint8_t> cachedLow,
    std::vector<std::uint8_t> cachedMid,
    std::vector<std::uint8_t> cachedHigh)
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
         frequencyRequested = currentFrequencyRequested_,
         cachedLow = std::move(cachedLow),
         cachedMid = std::move(cachedMid),
         cachedHigh = std::move(cachedHigh)]() mutable {
            WorkerTaskCounter workerCounter;
            invokeProviderTaskHook(false, sourcePath);
            Job job;
            job.path = sourcePath;
            job.trackId = sourceTrackId;
            job.generation = sourceGeneration;
            job.kind = kind;
            job.frequencyRequested = frequencyRequested;
            job.aggregation = aggregation;
            job.resources = resources;
            job.progress = progress;
            job.cachedLow = std::move(cachedLow);
            job.cachedMid = std::move(cachedMid);
            job.cachedHigh = std::move(cachedHigh);
            const std::uint64_t decoderOpensBefore =
                agplayer::Decoder::threadOpenCount();
            if (kind == JobKind::FrequencyColor) {
                FrequencyJobCounter counter;
                job.result = ag_track_frequency_color_analysis(
                    source.constData(), targetPoints, resources->cancelToken,
                    [](const float p, void* userData) {
                        providerCounters.frequencyProgressCallbacks.fetch_add(
                            1U, std::memory_order_relaxed);
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
            const std::uint64_t decoderOpensAfter =
                agplayer::Decoder::threadOpenCount();
            providerCounters.decoderOpens.fetch_add(
                decoderOpensAfter - decoderOpensBefore,
                std::memory_order_relaxed);
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
        QtConcurrent::task([path, cacheDirectory, aggregation] {
            WorkerTaskCounter workerCounter;
            invokeProviderTaskHook(true, path);
            constexpr std::size_t targetPoints = 2000;
            const QString cachePath = cacheFilePathForDirectory(
                cacheDirectory, path, aggregation);
            if (cachePath.isEmpty()) {
                return;
            }
            const std::filesystem::path sourcePath = filesystemPath(path);
            const std::filesystem::path cacheFile = filesystemPath(cachePath);
            agplayer::WaveformCacheData cached;
            std::vector<float> legacy;
            if (agplayer::WaveformCache::load_v2(cacheFile, sourcePath, cached)
                || agplayer::WaveformCache::load(
                    cacheFile, sourcePath, legacy)) {
                return;
            }

            ag_waveform* waveform = nullptr;
            const QByteArray encodedPath = path.toUtf8();
            const ag_result result = ag_track_analysis_with_aggregation(
                encodedPath.constData(), targetPoints, aggregation, nullptr,
                nullptr, nullptr, &waveform, nullptr);
            if (result == AG_OK && waveform != nullptr) {
                saveWaveformCache(cachePath, path, waveform);
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
    if (!cachePath.isEmpty()
        && (job.kind != JobKind::FrequencyColor
            || currentMixLayers_.isEmpty())) {
        if (saveWaveformCache(cachePath, currentPath_, waveform,
                              job.kind == JobKind::FrequencyColor)) {
            if (settings_ != nullptr) {
                settings_->onWaveformCacheSaved();
            }
            emit waveformCacheReady(currentPath_);
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
        result[QStringLiteral("_cacheVersion")] = 2;
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

    std::vector<std::uint8_t> low = job.cachedLow;
    std::vector<std::uint8_t> mid = job.cachedMid;
    std::vector<std::uint8_t> high = job.cachedHigh;
    if (job.kind == JobKind::FrequencyColor) {
        agplayer::FrequencyColorCacheData frequency =
            frequencyCacheFromWaveform(waveform);
        low = frequency.low;
        mid = frequency.mid;
        high = frequency.high;
        const QString frequencyPath =
            frequencyCacheFilePathFor(settings_, currentPath_);
        if (!frequencyPath.isEmpty()) {
            if (!agplayer::FrequencyColorWaveformCache::save_atomic(
                    filesystemPath(frequencyPath),
                    filesystemPath(currentPath_), frequency)) {
                RuntimeLog::log(
                    AG_IO_ERROR, QStringLiteral("WaveformProvider"),
                    QStringLiteral("Failed to save frequency waveform cache"));
                setAnalysisProgress(1.0);
                return;
            }
            if (settings_ != nullptr) {
                settings_->onWaveformCacheSaved();
            }
        }
    }
    const std::size_t mixCount = static_cast<std::size_t>(
        result.value(QStringLiteral("mix")).toList().size());
    if (mixCount == 0U || low.size() != mixCount || mid.size() != mixCount
        || high.size() != mixCount) {
        setAnalysisProgress(1.0);
        return;
    }
    QVariantMap complete = result;
    addFrequencyLayers(complete, low, mid, high);
    currentLayers_ = complete;
    setAnalysisProgress(1.0);
    emit waveformReady(currentPath_, complete);
}

QVariantMap WaveformProvider::waveformToVariantMap(
    const ag_waveform* waveform) const
{
    return layersFromWaveform(waveform);
}
