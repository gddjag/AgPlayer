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
#include <vector>

class QTimer;
class SettingsController;
struct WaveformProviderTestAccess;

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
    Q_INVOKABLE void cancelFrequencyForTrack(const QString& path);
    void setAudioResourcePressure(bool pressured);

signals:
    void waveformReady(const QString& path, const QVariantMap& layers);
    void waveformCacheReady(const QString& sourcePath);
    void waveformFailed(const QString& path, const QString& trackId,
                        qulonglong generation, int errorCode);
    void analysisProgressChanged();
    void activeGenerationChanged();

private:
    friend struct WaveformProviderTestAccess;

    void onAnalysisFinished();
    void onProgressTimer();
    void setAnalysisProgress(double progress);
    QVariantMap waveformToVariantMap(const ag_waveform* waveform) const;
    void cancelActiveJob();
    void startAnalysis(bool frequencyColor,
                       std::vector<std::uint8_t> cachedLow = {},
                       std::vector<std::uint8_t> cachedMid = {},
                       std::vector<std::uint8_t> cachedHigh = {});
    void updateFrequencyPause(bool queryPowerState);

    enum class JobKind {
        MixOnly,
        FrequencyColor,
    };

    struct AnalysisResources {
        ag_cancel_token* cancelToken = nullptr;
        ag_waveform* waveform = nullptr;

        ~AnalysisResources();
        void cancel() const;
        void setPaused(bool paused) const;
    };

    struct Job {
        QString path;
        QString trackId;
        quint64 generation = 0;
        JobKind kind = JobKind::MixOnly;
        bool frequencyRequested = false;
        ag_result result = AG_OK;
        ag_waveform_aggregation aggregation =
            AG_WAVEFORM_AGGREGATION_AVERAGE_ABSOLUTE;
        std::shared_ptr<AnalysisResources> resources;
        std::shared_ptr<std::atomic<double>> progress;
        std::vector<std::uint8_t> cachedLow;
        std::vector<std::uint8_t> cachedMid;
        std::vector<std::uint8_t> cachedHigh;
    };

    SettingsController* settings_ = nullptr;
    QFutureWatcher<Job>* watcher_ = nullptr;
    QTimer* progressTimer_ = nullptr;
    std::shared_ptr<std::atomic<double>> activeProgress_;
    std::shared_ptr<AnalysisResources> activeResources_;
    QString currentPath_;
    QString currentTrackId_;
    QVariantMap currentLayers_;
    QVariantMap currentMixLayers_;
    quint64 activeGeneration_ = 0;
    ag_waveform_aggregation currentAggregation_ =
        AG_WAVEFORM_AGGREGATION_AVERAGE_ABSOLUTE;
    double analysisProgress_ = 0.0;
    JobKind activeJobKind_ = JobKind::MixOnly;
    bool currentFrequencyRequested_ = false;
    bool audioResourcePressure_ = false;
    bool energySaverActive_ = false;
    qint64 lastPowerQueryMs_ = 0;
    QThreadPool currentAnalysisPool_;
};
