#pragma once

#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <QVariantList>

#include <agplayer/c_api.h>

#include <atomic>
#include <memory>

class QTimer;
class SettingsController;

class WaveformProvider : public QObject {
    Q_OBJECT
    Q_PROPERTY(double analysisProgress READ analysisProgress
                   NOTIFY analysisProgressChanged)

public:
    explicit WaveformProvider(SettingsController* settings = nullptr,
                              QObject* parent = nullptr);
    ~WaveformProvider() override;

    double analysisProgress() const noexcept;

    Q_INVOKABLE void loadForTrack(const QString& path);
    Q_INVOKABLE void cancelForTrack(const QString& path);

signals:
    void waveformReady(const QString& path, const QVariantMap& layers);
    void analysisProgressChanged();

private:
    void onAnalysisFinished();
    void setAnalysisProgress(double progress);
    QVariantMap waveformToVariantMap(const ag_waveform* waveform) const;

    struct Job {
        QString path;
        ag_waveform* waveform = nullptr;
        ag_result result = AG_OK;
        ag_waveform_aggregation aggregation =
            AG_WAVEFORM_AGGREGATION_AVERAGE_ABSOLUTE;
        ag_cancel_token* cancelToken = nullptr;
        std::shared_ptr<std::atomic<double>> progress;
    };

    SettingsController* settings_ = nullptr;
    QFutureWatcher<Job>* watcher_ = nullptr;
    QTimer* progressTimer_ = nullptr;
    std::shared_ptr<std::atomic<double>> activeProgress_;
    ag_cancel_token* activeCancelToken_ = nullptr;
    QString currentPath_;
    ag_waveform_aggregation currentAggregation_ =
        AG_WAVEFORM_AGGREGATION_AVERAGE_ABSOLUTE;
    double analysisProgress_ = 0.0;
};
