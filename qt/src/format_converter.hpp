#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>

#include <atomic>

// FormatConverter: batch audio transcoder. Manages a list of input files and
// transcodes them in parallel using QtConcurrent. The CPU core count controls
// the QThreadPool max thread count (parallelism = concurrent files). Each
// individual transcode uses a single-threaded FFmpeg encoder. Transcoding
// runs in background threads and does not block playback.
class FormatConverter final : public QObject {
    Q_OBJECT
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(int fileCount READ fileCount NOTIFY fileCountChanged)
    Q_PROPERTY(int completedCount READ completedCount NOTIFY completedCountChanged)
    Q_PROPERTY(int failedCount READ failedCount NOTIFY failedCountChanged)

public:
    explicit FormatConverter(QObject* parent = nullptr);

    double progress() const noexcept;
    bool busy() const noexcept;
    int fileCount() const noexcept;
    int completedCount() const noexcept;
    int failedCount() const noexcept;

    Q_INVOKABLE void loadFiles(const QList<QUrl>& urls);
    Q_INVOKABLE QString entryAt(int index) const;
    Q_INVOKABLE void start(const QString& outputFormat,
                           int bitRate,
                           int sampleRate,
                           int channels,
                           int cpuCores,
                           const QString& outputDir);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void clear();

signals:
    void progressChanged();
    void busyChanged();
    void fileCountChanged();
    void completedCountChanged();
    void failedCountChanged();
    void transcodeCompleted(int successCount, int failureCount);
    void errorOccurred(const QString& message);

private:
    struct FileEntry {
        QString path;
        QString fileName;
    };

    QList<FileEntry> entries_;
    std::atomic<bool> cancelFlag_{false};
    std::atomic<double> progress_{0.0};
    std::atomic<bool> busy_{false};
    std::atomic<int> completedCount_{0};
    std::atomic<int> failedCount_{0};

    void setBusy(bool value);
    void setProgress(double value);
    void setCompletedCount(int value);
    void setFailedCount(int value);

    // Generate a non-colliding output path for the given source and format.
    QString computeOutputPath(const QString& inputPath,
                              const QString& outputFormat,
                              const QString& outputDir) const;
};
