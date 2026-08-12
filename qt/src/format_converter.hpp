#pragma once

#include <agplayer/c_api.h>

#include <QList>
#include <QHash>
#include <QMutex>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QVector>

#include <atomic>

template <typename T>
class QFutureWatcher;

// FormatConverter: batch audio transcoder. Manages a list of input files and
// transcodes them with bounded parallelism. Each file exposes its
// format, size, duration and status so the QML table can display progress.
class FormatConverter final : public QObject {
    Q_OBJECT
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(int fileCount READ fileCount NOTIFY fileCountChanged)
    Q_PROPERTY(int completedCount READ completedCount NOTIFY completedCountChanged)
    Q_PROPERTY(int failedCount READ failedCount NOTIFY failedCountChanged)
    Q_PROPERTY(QVariantList files READ files NOTIFY filesChanged)
    Q_PROPERTY(QVariantList supportedOutputFormats READ supportedOutputFormats CONSTANT)
    Q_PROPERTY(int parallelJobs READ parallelJobs WRITE setParallelJobs
                   NOTIFY parallelJobsChanged)
    Q_PROPERTY(QString bitrateMode READ bitrateMode WRITE setBitrateMode
                   NOTIFY bitrateModeChanged)
    Q_PROPERTY(QString conflictPolicy READ conflictPolicy WRITE setConflictPolicy
                   NOTIFY conflictPolicyChanged)

public:
    explicit FormatConverter(QObject* parent = nullptr);
    ~FormatConverter() override;

    double progress() const noexcept;
    bool busy() const noexcept;
    int fileCount() const noexcept;
    int completedCount() const noexcept;
    int failedCount() const noexcept;
    QVariantList files() const;
    QVariantList supportedOutputFormats() const;
    int parallelJobs() const noexcept { return parallelJobs_; }
    void setParallelJobs(int value);
    QString bitrateMode() const { return bitrateMode_; }
    void setBitrateMode(const QString& value);
    QString conflictPolicy() const { return conflictPolicy_; }
    void setConflictPolicy(const QString& value);
    void setOverwriteExisting(bool value) noexcept { overwriteExisting_ = value; }

    Q_INVOKABLE void loadFiles(const QList<QUrl>& urls);
    Q_INVOKABLE bool setMetadataEditPlan(const QVariantMap& fields,
                                         const QUrl& coverUrl);
    Q_INVOKABLE QVariantMap previewSelected(const QVariantList& indices,
                                            const QString& outputFormat,
                                            int bitRate,
                                            int sampleRate,
                                            int channels,
                                            const QString& outputDir,
                                            bool extractAudio);
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
    Q_INVOKABLE void startSelected(const QVariantList& indices,
                                   const QString& outputFormat,
                                   int bitRate,
                                   int sampleRate,
                                   int channels,
                                   const QString& outputDir,
                                   bool keepMetadata,
                                   bool volumeNormalize,
                                   bool extractAudio);
    Q_INVOKABLE void retryFailed(const QString& outputFormat,
                                 int bitRate,
                                 int sampleRate,
                                 int channels,
                                 const QString& outputDir,
                                 bool keepMetadata,
                                 bool volumeNormalize,
                                 bool extractAudio);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void cancelEntry(int index);

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
    void parallelJobsChanged();
    void bitrateModeChanged();
    void conflictPolicyChanged();

private:
    enum class FileStatus {
        Waiting,
        Ready,
        PendingConfirmation,
        Skipped,
        Converting,
        Done,
        Error,
        Cancelled
    };

    static QString statusString(FileStatus status);

    struct FileEntry {
        QString path;
        QString fileName;
        QString format;
        qint64 fileSize = 0;
        qint64 durationMs = 0;
        int sampleRate = 0;
        qint64 bitRate = 0;
        int channels = 0;
        double progress = 0.0;
        QString outputFormat;
        QString outputPath;
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
    QMutex tokenMutex_;
    QHash<int, ag_cancel_token*> activeTokens_;
    QSet<int> cancelledEntries_;
    QPointer<QFutureWatcher<QList<QUrl>>> discoveryWatcher_;
    QPointer<QFutureWatcher<QList<FileEntry>>> loadWatcher_;
    QPointer<QFutureWatcher<void>> watcher_;
    bool overwriteExisting_ = false;
    int parallelJobs_ = 4;
    QString bitrateMode_ = QStringLiteral("cbr");
    QString conflictPolicy_ = QStringLiteral("auto-number");
    QVariantMap metadataFields_;
    QByteArray metadataCoverData_;
    QString metadataCoverMime_;

    void setBusy(bool value);
    void setProgress(double value);
    void setCompletedCount(int value);
    void setFailedCount(int value);
    void setEntryStatus(int index, FileStatus status);
    void setEntryError(int index, const QString& error);
    void updateEntryProgress(int index, double value,
                             const QVector<int>& jobIndices);

    // Generate a non-colliding output path for the given source and format.
    QString computeOutputPath(const QString& inputPath,
                              const QString& outputFormat,
                              const QString& outputDir,
                              const QSet<QString>& reservedPaths,
                              bool overwriteExisting) const;

    void startJobs(const QVector<int>& jobIndices,
                   const QString& outputFormat,
                   int bitRate,
                   int sampleRate,
                   int channels,
                   const QString& outputDir,
                   bool keepMetadata,
                   bool volumeNormalize,
                   bool extractAudio);

    // Bounded parallel transcode worker. Runs in a background thread.
    void runTranscode(const QString& outputFormat,
                      int bitRate,
                      int sampleRate,
                      int channels,
                      const QString& outputDir,
                      bool keepMetadata,
                      bool volumeNormalize,
                   bool extractAudio,
                   bool overwriteExisting,
                   const QString& bitrateMode,
                   const QString& conflictPolicy,
                   const QVariantMap& metadataFields,
                   const QByteArray& metadataCoverData,
                   const QString& metadataCoverMime,
                   const QVector<int>& jobIndices);
};
