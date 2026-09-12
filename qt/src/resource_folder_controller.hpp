#pragma once

#include "library_model.hpp"

#include <QObject>
#include <QFileSystemWatcher>
#include <QFutureWatcher>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>
#include <atomic>
#include <memory>

class ImportController;

// Resource-folder discovery only. Does not inspect, hash or modify track metadata.
class ResourceFolderController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(LibraryModel* libraryModel READ libraryModel WRITE setLibraryModel NOTIFY libraryModelChanged)
    Q_PROPERTY(QStringList monitoredFolders READ monitoredFolders NOTIFY monitoredFoldersChanged)
    Q_PROPERTY(QStringList resourceDirectories READ resourceDirectories NOTIFY resourceTopologyChanged)
    Q_PROPERTY(QString audioFileNameFilter READ audioFileNameFilter CONSTANT)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    Q_PROPERTY(ImportController* importController READ importController WRITE setImportController NOTIFY importControllerChanged)
    Q_PROPERTY(QString storagePath READ storagePath WRITE setStoragePath NOTIFY storagePathChanged)
    Q_PROPERTY(QString lastPersistenceError READ lastPersistenceError NOTIFY persistenceStateChanged)
public:
    enum class DropPathKind { Invalid, Directory, AudioFile, VideoFile, OtherFile };
    Q_ENUM(DropPathKind)
    explicit ResourceFolderController(QObject* parent = nullptr);
    ~ResourceFolderController() override;
    LibraryModel* libraryModel() const noexcept;
    void setLibraryModel(LibraryModel* model);
    QStringList monitoredFolders() const;
    QStringList resourceDirectories() const;
    QString audioFileNameFilter() const;
    Q_INVOKABLE bool addMonitoredFolder(const QString& folder);
    Q_INVOKABLE bool addMonitoredFolderUrl(const QUrl& folder);
    Q_INVOKABLE bool removeMonitoredFolder(const QString& folder);
    Q_INVOKABLE QVariantMap classifyDropUrl(const QUrl& url) const;
    Q_INVOKABLE bool pathIsWithin(const QString& candidate, const QString& root) const;
    Q_INVOKABLE bool removeTrackFromLibrary(const QString& trackId);
    Q_INVOKABLE void rescan();
    bool scanning() const noexcept;
    ImportController* importController() const noexcept;
    void setImportController(ImportController* controller);
    QString storagePath() const;
    void setStoragePath(const QString& path);
    QString lastPersistenceError() const;
signals:
    void libraryModelChanged();
    void monitoredFoldersChanged();
    void resourceRootsChanged();
    void resourceTopologyChanged();
    void scanningChanged();
    void scanFinished();
    void importControllerChanged();
    void storagePathChanged();
    void persistenceStateChanged();
private:
    void scheduleRescan();
    void rebuildDirectoryWatches();
    void applyDirectoryWatches(const QStringList& directories);
    void loadMonitoredFolders();
    bool saveMonitoredFolders();
    QPointer<LibraryModel> library_;
    QPointer<ImportController> importer_;
    QFileSystemWatcher watcher_;
    QTimer debounce_;
    QStringList monitoredRoots_;
    QHash<QString, QString> excludedPaths_;
    QStringList resourceDirectories_;
    QString storagePath_;
    QString lastPersistenceError_;
    bool scanning_ = false;
    QPointer<QFutureWatcher<QVariantMap>> scanWatcher_;
    std::shared_ptr<std::atomic_bool> scanCancel_;
    quint64 scanGeneration_ = 0;
};
