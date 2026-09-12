#pragma once

#include "library_model.hpp"

#include <QList>
#include <QObject>
#include <QPair>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>

template <typename T>
class QFutureWatcher;
class LibraryModel;

struct MetadataEntry {
    QString path;
    QString fileName;
    QString title;
    QString artist;
    QString album;
    QString albumArtist;
    QString year;
    QString customTag;
    QString date;
    QString genre;
    QString track;
    QString disc;
    QString composer;
    QString comment;
    QString bpm;
    QString copyright;
    QString encoder;
    QString lyrics;
    QString format;
    qint64 durationMs = 0;
    qint64 fileSize = 0;
    qint64 sourceLastModifiedMs = 0;
    QString canonicalPath;
    QString stableSourceId;
    bool hasCover = false;
    QString coverPreview;
    QString coverInfo;
    QString coverFingerprint;
    QString coverFileName;
    QString coverMimeType;
    int coverWidth = 0;
    int coverHeight = 0;
    qint64 coverSizeBytes = 0;
    bool hasError = false;
    QString error;
};

struct MetadataApplySummary {
    int successCount = 0;
    int failureCount = 0;
    int supportedCount = 0;
    int unsupportedCount = 0;
    int cancelledCount = 0;
    QList<MetadataEntry> entries;
    QVariantList results;
    QList<int> supportedTargets;
    QList<LibraryMetadataRefresh> libraryRefreshes;
};

class MetadataEditor final : public QObject {
    Q_OBJECT
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(int fileCount READ fileCount NOTIFY fileCountChanged)
    Q_PROPERTY(QString coverImage READ coverImage NOTIFY coverImageChanged)
    Q_PROPERTY(QVariantList results READ results NOTIFY resultsChanged)
    Q_PROPERTY(bool requiresPreflightDecision READ requiresPreflightDecision
               NOTIFY preflightDecisionChanged)
    Q_PROPERTY(int successCount READ successCount NOTIFY statisticsChanged)
    Q_PROPERTY(int failedCount READ failedCount NOTIFY statisticsChanged)
    Q_PROPERTY(int supportedCount READ supportedCount NOTIFY statisticsChanged)
    Q_PROPERTY(int unsupportedCount READ unsupportedCount NOTIFY statisticsChanged)
    Q_PROPERTY(int cancelledCount READ cancelledCount NOTIFY statisticsChanged)

public:
    explicit MetadataEditor(QObject* parent = nullptr);
    ~MetadataEditor() override;

    double progress() const noexcept;
    bool busy() const noexcept;
    int fileCount() const noexcept;
    QString coverImage() const;
    QVariantList results() const { return results_; }
    bool requiresPreflightDecision() const noexcept { return requiresPreflightDecision_; }
    int successCount() const noexcept { return successCount_; }
    int failedCount() const noexcept { return failedCount_; }
    int supportedCount() const noexcept { return supportedCount_; }
    int unsupportedCount() const noexcept { return unsupportedCount_; }
    int cancelledCount() const noexcept { return cancelledCount_; }
    void setLibraryModel(LibraryModel* model) noexcept { libraryModel_ = model; }

    Q_INVOKABLE void loadFiles(const QList<QUrl>& urls);
    Q_INVOKABLE QVariantMap entryAt(int index) const;
    Q_INVOKABLE QVariantMap aggregateMetadata(const QList<int>& indices) const;
    Q_INVOKABLE QVariantMap replacementCoverDetails() const
    {
        return replacementCoverDetails_;
    }
    Q_INVOKABLE void removeFiles(const QList<int>& indices);
    Q_INVOKABLE void applyMetadata(const QVariantMap& fields,
                                   const QList<int>& indices);
    Q_INVOKABLE void preflightMetadata(const QVariantMap& fields,
                                       const QList<int>& indices);
    Q_INVOKABLE void applyPreflightDecision(const QString& policy);
    Q_INVOKABLE bool exportResults(const QUrl& destination);
    Q_INVOKABLE bool exportCurrentList(const QUrl& destination,
                                       const QList<int>& indices) const;
    Q_INVOKABLE void setCoverImage(const QUrl& url);
    Q_INVOKABLE void clearCoverImage();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void clear();

signals:
    void progressChanged();
    void busyChanged();
    void fileCountChanged();
    void entriesLoaded();
    void entriesChanged();
    void coverImageChanged();
    void resultsChanged();
    void statisticsChanged();
    void preflightDecisionChanged();
    void preflightCompleted(int supportedCount, int unsupportedCount);
    void preflightDecisionRequired(int supportedCount, int unsupportedCount);
    void metadataApplied(int successCount, int failureCount);
    void errorOccurred(const QString& message);

private:
    QList<MetadataEntry> entries_;
    std::atomic<bool> cancelFlag_{false};
    std::atomic<double> progress_{0.0};
    std::atomic<bool> busy_{false};
    QPointer<QFutureWatcher<QList<QUrl>>> discoveryWatcher_;
    QPointer<QFutureWatcher<QList<MetadataEntry>>> loadWatcher_;
    QPointer<QFutureWatcher<MetadataApplySummary>> operationWatcher_;
    QPointer<LibraryModel> libraryModel_;
    QVariantList results_;
    QVariantMap pendingFields_;
    QList<int> pendingTargets_;
    QList<MetadataEntry> pendingEntrySnapshot_;
    QList<int> pendingSupportedTargets_;
    QVariantList pendingUnsupportedResults_;
    bool requiresPreflightDecision_ = false;
    int successCount_ = 0;
    int failedCount_ = 0;
    int supportedCount_ = 0;
    int unsupportedCount_ = 0;
    int cancelledCount_ = 0;

    QString coverPath_;
    QByteArray coverData_;
    QString coverMime_;
    QVariantMap replacementCoverDetails_;

    void setBusy(bool value);
    void setProgress(double value);
    void startMetadataLoad(QList<QUrl> expandedUrls);
    void startPreflight(const QVariantMap& fields, const QList<int>& indices,
                        bool applyWhenSupported);
    void startApply(const QVariantMap& fields, const QList<int>& indices,
                    const QList<MetadataEntry>& snapshot);
    void resetOperationState();
    void resetCover();
    static QString mimeTypeForFormat(const QByteArray& format);
};

QVariantMap aggregate_metadata_entries(const QList<MetadataEntry>& entries,
                                       const QList<int>& indices);
