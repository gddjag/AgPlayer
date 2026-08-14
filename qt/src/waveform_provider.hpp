#pragma once

#include <QFutureWatcher>
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QThreadPool>
#include <QVariantList>
#include <QVariantMap>

#include <agplayer/c_api.h>

#include <atomic>
#include <memory>

class QTimer;
class SettingsController;

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
                                        const QString& path);
    Q_INVOKABLE void prefetchTracks(const QStringList& paths);
    Q_INVOKABLE void cancelForTrack(const QString& path);
    Q_INVOKABLE qulonglong loadForRequest(const QString& requestId,
                                          const QString& path);
    Q_INVOKABLE void cancelRequest(const QString& requestId);

signals:
    void waveformReady(const QString& path, const QVariantMap& layers);
    void requestWaveformReady(const QString& requestId,
                              const QString& path,
                              const QVariantMap& layers);
    void analysisProgressChanged();
    void activeGenerationChanged();

private:
    void onAnalysisFinished();
    void setAnalysisProgress(double progress);
    QVariantMap waveformToVariantMap(const ag_waveform* waveform) const;

    struct Job {
        QString path;
        QString trackId;
        quint64 generation = 0;
        ag_waveform* waveform = nullptr;
        ag_result result = AG_OK;
        ag_waveform_aggregation aggregation =
            AG_WAVEFORM_AGGREGATION_AVERAGE_ABSOLUTE;
        ag_cancel_token* cancelToken = nullptr;
        std::shared_ptr<std::atomic<double>> progress;
    };

    struct RequestJob {
        QString path;
        ag_result result = AG_OK;
        ag_waveform_aggregation aggregation =
            AG_WAVEFORM_AGGREGATION_AVERAGE_ABSOLUTE;
        std::shared_ptr<ag_waveform> waveform;
    };

    struct RequestState {
        quint64 generation = 0;
        QFutureWatcher<RequestJob>* watcher = nullptr;
        std::shared_ptr<ag_cancel_token> cancelToken;
    };

    SettingsController* settings_ = nullptr;
    QFutureWatcher<Job>* watcher_ = nullptr;
    QTimer* progressTimer_ = nullptr;
    std::shared_ptr<std::atomic<double>> activeProgress_;
    ag_cancel_token* activeCancelToken_ = nullptr;
    QString currentPath_;
    QString currentTrackId_;
    QVariantMap currentLayers_;
    quint64 activeGeneration_ = 0;
    ag_waveform_aggregation currentAggregation_ =
        AG_WAVEFORM_AGGREGATION_AVERAGE_ABSOLUTE;
    double analysisProgress_ = 0.0;
    quint64 nextRequestGeneration_ = 0;
    QHash<QString, RequestState> requestStates_;
    QThreadPool currentAnalysisPool_;
    QThreadPool requestAnalysisPool_;
    QThreadPool prefetchPool_;
};
