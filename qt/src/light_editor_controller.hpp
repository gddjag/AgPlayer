#pragma once

#include <agplayer/c_api.h>

#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>

#include <QMutex>
#include <QPointer>

#include <atomic>

template <typename T>
class QFutureWatcher;

// LightEditor: QML singleton for basic audio editing (trim, fade, gain).
// Wraps ag_light_edit. When a file is loaded, duration is read via
// ag_metadata_open so the QML trim sliders can be bounded correctly.
class LightEditor final : public QObject {
    Q_OBJECT
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString inputFileName READ inputFileName NOTIFY inputFileChanged)
    Q_PROPERTY(bool hasInput READ hasInput NOTIFY inputFileChanged)
    Q_PROPERTY(qint64 durationMs READ durationMs NOTIFY inputFileChanged)
    Q_PROPERTY(QString inputFormat READ inputFormat NOTIFY inputFileChanged)
    Q_PROPERTY(int inputSampleRate READ inputSampleRate NOTIFY inputFileChanged)
    Q_PROPERTY(int inputChannels READ inputChannels NOTIFY inputFileChanged)
    Q_PROPERTY(QVariantList waveformPeaks READ waveformPeaks NOTIFY waveformPeaksChanged)

public:
    explicit LightEditor(QObject* parent = nullptr);
    ~LightEditor() override;

    double progress() const noexcept;
    bool busy() const noexcept;
    QString inputFileName() const noexcept;
    bool hasInput() const noexcept;
    qint64 durationMs() const noexcept;
    QString inputFormat() const noexcept;
    int inputSampleRate() const noexcept;
    int inputChannels() const noexcept;
    QVariantList waveformPeaks() const noexcept;

    Q_INVOKABLE void loadFile(const QUrl& url);
    Q_INVOKABLE void start(qint64 trimStartMs, qint64 trimEndMs,
                           int fadeInMs, int fadeOutMs, double gain,
                           const QString& outputDir);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void clear();

signals:
    void progressChanged();
    void busyChanged();
    void inputFileChanged();
    void waveformPeaksChanged();
    void lightEditCompleted(const QString& outputPath);
    void errorOccurred(const QString& message);

private:
    QString inputPath_;
    QString inputFileName_;
    QString inputFormat_;
    qint64 durationMs_ = 0;
    int inputSampleRate_ = 0;
    int inputChannels_ = 0;
    QVariantList waveformPeaks_;
    std::atomic<bool> busy_{false};
    std::atomic<double> progress_{0.0};
    std::atomic<ag_cancel_token*> token_{nullptr};
    QMutex tokenMutex_;
    QPointer<QFutureWatcher<int>> watcher_;

    void setBusy(bool value);
    void setProgress(double value);
    QString computeOutputPath(const QString& inputPath,
                              const QString& outputDir) const;
};
