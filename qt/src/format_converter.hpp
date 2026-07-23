#pragma once

#include <QList>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>

#include <atomic>

// FormatConverter: batch audio transcoder. Manages a list of input files and
// transcodes them sequentially in a background thread. Each file exposes its
// format, size, duration and status so the QML table can display progress.
class FormatConverter final : public QObject {
    Q_OBJECT
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(int fileCount READ fileCount NOTIFY fileCountChanged)
    Q_PROPERTY(int completedCount READ completedCount NOTIFY completedCountChanged)
    Q_PROPERTY(int failedCount READ failedCount NOTIFY failedCountChanged)
    Q_PROPERTY(QVariantList files READ files NOTIFY filesChanged)

public:
    explicit FormatConverter(QObject* parent = nullptr);

    double progress() const noexcept;
    bool busy() const noexcept;
    int fileCount() const noexcept;
    int completedCount() const noexcept;
    int failedCount() const noexcept;
    QVariantList files() const;

    Q_INVOKABLE void loadFiles(const QList<QUrl>& urls);
    Q_INVOKABLE QString entryAt(int index) const;
    Q_INVOKABLE void removeFile(int index);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void start(const QString& outputFormat,
                           int bitRate,
                           int sampleRate,
                           int channels,
                           const QString& outputDir,
                           bool keepMetadata,
                           bool volumeNormalize,
                           bool extractAudio);
    Q_INVOKABLE void cancel();

    Q_INVOKABLE QString formatFileSize(qint64 bytes) const;
    Q_INVOKABLE QString formatDuration(qint64 ms) const;

signals:
    void progressChanged();
    void busyChanged();
    void fileCountChanged();
    void completedCountChanged();
    void failedCountChanged();
    void filesChanged();
    void transcodeCompleted(int successCount, int failureCount);
    void errorOccurred(const QString& message);
    void warningOccurred(const QString& message);

private:
    enum class FileStatus {
        Waiting,
        Converting,
        Done,
        Error
    };

    static QString statusString(FileStatus status);

    struct FileEntry {
        QString path;
        QString fileName;
        QString format;
        qint64 fileSize = 0;
        qint64 durationMs = 0;
        FileStatus status = FileStatus::Waiting;
        QString errorMessage;
    };

    mutable QMutex mutex_;
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
    void setEntryStatus(int index, FileStatus status);
    void setEntryError(int index, const QString& error);

    // Generate a non-colliding output path for the given source and format.
    QString computeOutputPath(const QString& inputPath,
                              const QString& outputFormat,
                              const QString& outputDir) const;

    // Sequential transcode worker. Runs in a background thread.
    void runTranscode(const QString& outputFormat,
                      int bitRate,
                      int sampleRate,
                      int channels,
                      const QString& outputDir,
                      bool keepMetadata);
};
