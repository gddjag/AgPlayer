#pragma once

#include "library_model.hpp"

#include <QAbstractListModel>
#include <QFileSystemWatcher>
#include <QFutureWatcher>
#include <QPointer>
#include <QHash>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>
#include <memory>

class ImportController;

class LibraryManagerController : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(LibraryModel* libraryModel READ libraryModel WRITE setLibraryModel NOTIFY libraryModelChanged)
    Q_PROPERTY(QStringList monitoredFolders READ monitoredFolders NOTIFY monitoredFoldersChanged)
    Q_PROPERTY(QStringList resourceDirectories READ resourceDirectories
                   NOTIFY resourceTopologyChanged)
    Q_PROPERTY(QString audioFileNameFilter READ audioFileNameFilter CONSTANT)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY summaryChanged)
    Q_PROPERTY(int missingCount READ missingCount NOTIFY summaryChanged)
    Q_PROPERTY(int duplicateCount READ duplicateCount NOTIFY summaryChanged)
    Q_PROPERTY(int uncoveredCount READ uncoveredCount NOTIFY summaryChanged)
    Q_PROPERTY(int untaggedCount READ untaggedCount NOTIFY summaryChanged)
    Q_PROPERTY(int damagedCount READ damagedCount NOTIFY summaryChanged)
    Q_PROPERTY(qint64 totalBytes READ totalBytes NOTIFY summaryChanged)
    Q_PROPERTY(int recentAddedCount READ recentAddedCount NOTIFY summaryChanged)
    Q_PROPERTY(int recentPlayedCount READ recentPlayedCount NOTIFY summaryChanged)
    Q_PROPERTY(int highFrequencyCount READ highFrequencyCount NOTIFY summaryChanged)
    Q_PROPERTY(int lowFrequencyCount READ lowFrequencyCount NOTIFY summaryChanged)
    Q_PROPERTY(int neverPlayedCount READ neverPlayedCount NOTIFY summaryChanged)
    Q_PROPERTY(int unratedCount READ unratedCount NOTIFY summaryChanged)
    Q_PROPERTY(ImportController* importController READ importController
                   WRITE setImportController NOTIFY importControllerChanged)
    Q_PROPERTY(QString storagePath READ storagePath WRITE setStoragePath
                   NOTIFY storagePathChanged)
    Q_PROPERTY(QString libraryDataPath READ libraryDataPath WRITE setLibraryDataPath
                   NOTIFY libraryDataPathChanged)
    Q_PROPERTY(QString lastBackupPath READ lastBackupPath NOTIFY backupStateChanged)
    Q_PROPERTY(QString lastBackupError READ lastBackupError NOTIFY backupStateChanged)
    Q_PROPERTY(QUrl defaultBackupUrl READ defaultBackupUrl CONSTANT)
    Q_PROPERTY(QString keyword READ keyword WRITE setKeyword NOTIFY filterChanged)
    Q_PROPERTY(QString formatFilter READ formatFilter WRITE setFormatFilter NOTIFY filterChanged)
    Q_PROPERTY(int minBpm READ minBpm NOTIFY filterChanged)
    Q_PROPERTY(int maxBpm READ maxBpm NOTIFY filterChanged)
    Q_PROPERTY(int exactRating READ exactRating WRITE setExactRating NOTIFY filterChanged)
    Q_PROPERTY(QString activeCategory READ activeCategory WRITE setActiveCategory NOTIFY filterChanged)
    Q_PROPERTY(int currentPage READ currentPage WRITE setCurrentPage NOTIFY pageChanged)
    Q_PROPERTY(int pageSize READ pageSize CONSTANT)
    Q_PROPERTY(int filteredCount READ filteredCount NOTIFY filterChanged)
    Q_PROPERTY(int pageCount READ pageCount NOTIFY filterChanged)

public:
    enum class DropPathKind { Invalid, Directory, AudioFile, OtherFile };
    Q_ENUM(DropPathKind)

    enum Role { TrackIdRole = Qt::UserRole + 1, PathRole, TitleRole, ArtistRole,
                AlbumRole, FormatRole, StatusRole, ContentHashRole,
                DuplicateGroupRole, CoverRole, FavoriteRole, RatingRole,
                BpmRole, DurationRole, FileSizeRole, TagsRole };
    Q_ENUM(Role)

    explicit LibraryManagerController(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    LibraryModel* libraryModel() const noexcept;
    void setLibraryModel(LibraryModel* model);
    QStringList monitoredFolders() const;
    QStringList resourceDirectories() const;
    QString audioFileNameFilter() const;
    Q_INVOKABLE bool addMonitoredFolder(const QString& folder);
    Q_INVOKABLE bool addMonitoredFolderUrl(const QUrl& folder);
    Q_INVOKABLE QVariantMap classifyDropUrl(const QUrl& url) const;
    Q_INVOKABLE bool pathIsWithin(const QString& candidate,
                                  const QString& root) const;
    Q_INVOKABLE bool removeMonitoredFolder(const QString& folder);
    Q_INVOKABLE bool removeTrackFromLibrary(const QString& trackId);
    Q_INVOKABLE void rescan();
    Q_INVOKABLE void cancelScan();
    Q_INVOKABLE QVariantList duplicateGroups() const;
    ImportController* importController() const noexcept;
    void setImportController(ImportController* controller);
    QString storagePath() const;
    void setStoragePath(const QString& path);
    QString libraryDataPath() const;
    void setLibraryDataPath(const QString& path);
    QString lastBackupPath() const;
    QString lastBackupError() const;
    QUrl defaultBackupUrl() const;
    Q_INVOKABLE bool backupLibraryData(const QUrl& destination);
    Q_INVOKABLE bool importLibraryBackup(const QUrl& source);
    QString keyword() const;
    void setKeyword(const QString& keyword);
    QString formatFilter() const;
    void setFormatFilter(const QString& format);
    int minBpm() const noexcept;
    int maxBpm() const noexcept;
    Q_INVOKABLE void setBpmRange(int minimum, int maximum);
    int exactRating() const noexcept;
    void setExactRating(int rating);
    QString activeCategory() const;
    void setActiveCategory(const QString& category);
    int currentPage() const noexcept;
    void setCurrentPage(int page);
    int pageSize() const noexcept;
    int filteredCount() const noexcept;
    int pageCount() const noexcept;

    bool scanning() const noexcept;
    int progress() const noexcept;
    int totalCount() const noexcept;
    int missingCount() const noexcept;
    int duplicateCount() const noexcept;
    int uncoveredCount() const noexcept;
    int untaggedCount() const noexcept;
    int damagedCount() const noexcept;
    qint64 totalBytes() const noexcept;
    int recentAddedCount() const noexcept;
    int recentPlayedCount() const noexcept;
    int highFrequencyCount() const noexcept;
    int lowFrequencyCount() const noexcept;
    int neverPlayedCount() const noexcept;
    int unratedCount() const noexcept;

signals:
    void libraryModelChanged();
    void monitoredFoldersChanged();
    void resourceRootsChanged();
    void resourceTopologyChanged();
    void scanningChanged();
    void progressChanged();
    void summaryChanged();
    void scanFinished();
    void importControllerChanged();
    void storagePathChanged();
    void libraryDataPathChanged();
    void backupStateChanged();
    void filterChanged();
    void pageChanged();

private:
    struct IssueRow {
        QString trackId;
        QString duplicateGroup;
    };
    static QString fileHash(const QString& path,
                            const std::atomic_bool* cancelled = nullptr);
    void scheduleRescan();
    void rebuildDirectoryWatches();
    void applyDirectoryWatches(const QStringList& directories);
    void loadMonitoredFolders();
    void saveMonitoredFolders() const;
    QStringList discoverAudioFiles() const;
    void rebuildVisibleRows();
    bool matchesFilter(const IssueRow& issue) const;

    QPointer<LibraryModel> library_;
    QPointer<ImportController> importer_;
    QFileSystemWatcher watcher_;
    QTimer debounce_;
    QList<IssueRow> rows_;
    QList<IssueRow> allRows_;
    QVariantList duplicateGroups_;
    bool scanning_ = false;
    int progress_ = 0;
    int missingCount_ = 0;
    int duplicateCount_ = 0;
    int uncoveredCount_ = 0;
    int untaggedCount_ = 0;
    int damagedCount_ = 0;
    QStringList monitoredRoots_;
    QHash<QString, QString> excludedPaths_;
    QStringList resourceDirectories_;
    QString storagePath_;
    QString libraryDataPath_;
    QString lastBackupPath_;
    QString lastBackupError_;
    qint64 totalBytes_ = 0;
    int recentAddedCount_ = 0;
    int recentPlayedCount_ = 0;
    int highFrequencyCount_ = 0;
    int lowFrequencyCount_ = 0;
    int neverPlayedCount_ = 0;
    int unratedCount_ = 0;
    QPointer<QFutureWatcher<QVariantMap>> scanWatcher_;
    std::shared_ptr<std::atomic_bool> scanCancel_;
    quint64 scanGeneration_ = 0;
    QString keyword_;
    QString formatFilter_;
    int minBpm_ = 60;
    int maxBpm_ = 160;
    int exactRating_ = 0;
    QString activeCategory_ = QStringLiteral("all");
    int currentPage_ = 0;
    int filteredCount_ = 0;
    static constexpr int kPageSize = 50;
};
