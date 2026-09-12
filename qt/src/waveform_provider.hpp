#pragma once

#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QThreadPool>
#include <QVariantList>
#include <QVariantMap>

#include <agplayer/c_api.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

class QTimer;
class SettingsController;
struct WaveformProviderTestAccess;
struct FrequencyPowerStateTestAccess;

class WaveformProvider : public QObject {
    Q_OBJECT
    Q_PROPERTY(double analysisProgress READ analysisProgress
                   NOTIFY analysisProgressChanged)
    Q_PROPERTY(qulonglong activeGeneration READ activeGeneration
                   NOTIFY activeGenerationChanged)

public:
    explicit WaveformProvider(SettingsController* settings = nullptr,
                              QObject* parent = nullptr);
    ~WaveformProvider() override;

    double analysisProgress() const noexcept;
    qulonglong activeGeneration() const noexcept;

    Q_INVOKABLE void loadForTrack(const QString& path);
    Q_INVOKABLE qulonglong loadForTrack(const QString& trackId,
                                        const QString& path,
                                        bool frequencyColor = false);
    Q_INVOKABLE void prefetchTracks(const QStringList& paths);
    Q_INVOKABLE void cancelForTrack(const QString& path);

signals:
    void waveformReady(const QString& path, const QVariantMap& layers);
    void waveformCacheReady(const QString& sourcePath);
    void waveformFailed(const QString& path, const QString& trackId,
                        qulonglong generation, int errorCode);
    void analysisProgressChanged();
    void activeGenerationChanged();

private:
    friend struct WaveformProviderTestAccess;
    friend struct WaveformPrefetchTestAccess;
    friend struct FrequencyPowerStateTestAccess;

    void onAnalysisFinished();
    void onProgressTimer();
    void setAnalysisProgress(double progress);
    void cancelActiveJob();
    void cancelPrefetchJobs();
    void startAnalysis();

    struct AnalysisResources {
        ag_cancel_token* cancelToken = nullptr;
        ag_waveform* waveform = nullptr;
        std::atomic_bool canceled{false};
        std::mutex snapshotMutex;
        QVariantMap pendingSnapshot;

        ~AnalysisResources();
        void cancel();
    };

    struct Job {
        QString path;
        QString trackId;
        quint64 generation = 0;
        ag_result result = AG_OK;
        ag_waveform_aggregation aggregation =
            AG_WAVEFORM_AGGREGATION_AVERAGE_ABSOLUTE;
        std::shared_ptr<AnalysisResources> resources;
        std::shared_ptr<std::atomic<double>> progress;
        QVariantMap layers;
        bool cacheSaved = false;
    };

    SettingsController* settings_ = nullptr;
    QFutureWatcher<Job>* watcher_ = nullptr;
    QTimer* progressTimer_ = nullptr;
    std::shared_ptr<std::atomic<double>> activeProgress_;
    std::shared_ptr<AnalysisResources> activeResources_;
    std::shared_ptr<AnalysisResources> prefetchResources_;
    QString currentPath_;
    QString currentTrackId_;
    QVariantMap currentLayers_;
    quint64 activeGeneration_ = 0;
    ag_waveform_aggregation currentAggregation_ =
        AG_WAVEFORM_AGGREGATION_AVERAGE_ABSOLUTE;
    double analysisProgress_ = 0.0;
    bool currentFrequencyRequested_ = false;
    QThreadPool currentAnalysisPool_;
};
