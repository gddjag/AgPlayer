#include "waveform_provider.hpp"
#include "runtime_log.hpp"
#include "settings_controller.hpp"
#include "waveform_cache.hpp"

#include <QDir>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

namespace {

QString cacheFilePathFor(SettingsController* settings, const QString& sourcePath)
{
    if (settings == nullptr || sourcePath.isEmpty()) {
        return {};
    }
    const QString dir = settings->cacheDirectory();
    if (dir.isEmpty()) {
        return {};
    }
    if (!QDir().mkpath(dir)) {
        return {};
    }
    const std::string key =
        agplayer::WaveformCache::key_for(sourcePath.toStdString());
    if (key.empty()) {
        return {};
    }
    return QDir(dir).filePath(QString::fromStdString(key)
                              + QStringLiteral(".agwf"));
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

QVariantList peaksFromWaveform(const ag_waveform* waveform)
{
    if (waveform == nullptr) {
        return {};
    }
    const std::size_t count = ag_waveform_count(waveform);
    QVariantList result;
    result.reserve(static_cast<int>(count));
    for (std::size_t index = 0; index < count; ++index) {
        result.append(static_cast<double>(ag_waveform_peak(waveform, index)));
    }
    return result;
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
    data.bpm = ag_waveform_bpm(waveform);
    if (!agplayer::WaveformCache::save_v2(
            cachePath.toStdString(), sourcePath.toStdString(), data)) {
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
        disconnect(watcher_, nullptr, this, nullptr);
        delete watcher_;
        watcher_ = nullptr;
    }
    activeProgress_.reset();
}

double WaveformProvider::analysisProgress() const noexcept
{
    return analysisProgress_;
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
    // Cancel and discard any running analysis. The background task keeps its
    // own copy of the cancel token, so we only cancel here and let it destroy
    // the token when it finishes.
    if (watcher_ != nullptr) {
        if (activeCancelToken_ != nullptr) {
            ag_cancel_token_cancel(activeCancelToken_);
            activeCancelToken_ = nullptr;
        }
        disconnect(watcher_, nullptr, this, nullptr);
        delete watcher_;
        watcher_ = nullptr;
    }
    activeProgress_.reset();
    progressTimer_->stop();

    currentPath_ = path;
    setAnalysisProgress(path.isEmpty() ? 1.0 : 0.0);

    if (path.isEmpty()) {
        emit waveformReady(path, {});
        return;
    }

    // Try cache first: v2 multi-layer format, then v1 legacy format.
    const QString cachePath = cacheFilePathFor(settings_, path);
    if (!cachePath.isEmpty()) {
        const std::string source = path.toStdString();
        const std::string cache = cachePath.toStdString();
        agplayer::WaveformCacheData data;
        if (agplayer::WaveformCache::load_v2(cache, source, data)) {
            setAnalysisProgress(1.0);
            emit waveformReady(path, peaksFromVector(data.mix));
            return;
        }
        std::vector<float> peaks;
        if (agplayer::WaveformCache::load(cache, source, peaks)) {
            setAnalysisProgress(1.0);
            emit waveformReady(path, peaksFromVector(peaks));
            return;
        }
    }

    // Start background analysis.
    const auto progress = std::make_shared<std::atomic<double>>(0.0);
    activeProgress_ = progress;
    ag_cancel_token* cancelToken = ag_cancel_token_create();
    activeCancelToken_ = cancelToken;

    const std::string source = path.toStdString();
    constexpr std::size_t targetPoints = 2000;

    watcher_ = new QFutureWatcher<Job>(this);
    connect(watcher_, &QFutureWatcher<Job>::finished,
            this, &WaveformProvider::onAnalysisFinished);
    QFuture<Job> future = QtConcurrent::run(
        [source, targetPoints, cancelToken, progress]() mutable {
            Job job;
            job.path = QString::fromStdString(source);
            job.cancelToken = cancelToken;
            job.progress = progress;
            job.result = ag_track_analysis(
                source.c_str(),
                targetPoints,
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
        disconnect(watcher_, nullptr, this, nullptr);
        delete watcher_;
        watcher_ = nullptr;
    }
    activeProgress_.reset();
    progressTimer_->stop();
    currentPath_.clear();
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
        || job.path != currentPath_) {
        if (job.waveform != nullptr) {
            ag_waveform_destroy(job.waveform);
        }
        if (job.path == currentPath_) {
            setAnalysisProgress(0.0);
        }
        return;
    }

    const QString cachePath = cacheFilePathFor(settings_, currentPath_);
    if (!cachePath.isEmpty()) {
        saveWaveformCache(cachePath, currentPath_, job.waveform);
        if (settings_ != nullptr) {
            settings_->onWaveformCacheSaved();
        }
    }

    setAnalysisProgress(1.0);
    emit waveformReady(currentPath_, waveformToVariantList(job.waveform));
    ag_waveform_destroy(job.waveform);
}

QVariantList WaveformProvider::waveformToVariantList(
    const ag_waveform* waveform) const
{
    return peaksFromWaveform(waveform);
}
