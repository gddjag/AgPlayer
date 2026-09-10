#include "resource_folder_controller.hpp"
#include "audio_file_discovery.hpp"
#include "import_controller.hpp"
#include "resource_path.hpp"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QtConcurrent>
#include <algorithm>
#include <utility>

namespace {

QString resourceLookupKey(const QString& path)
{
    const QString identity = QDir::fromNativeSeparators(QDir::cleanPath(path));
    return agplayer::qt::resourcePathCaseSensitivity() == Qt::CaseInsensitive
        ? identity.toCaseFolded() : identity;
}

QStringList normalizedResourcePaths(const QStringList& paths)
{
    QStringList result;
    QSet<QString> seen;
    for (const QString& path : paths) {
        const QString identity = agplayer::qt::resourcePathIdentity(path);
        const QString key = resourceLookupKey(identity);
        if (identity.isEmpty() || seen.contains(key)) continue;
        seen.insert(key);
        result.append(identity);
    }
    std::sort(result.begin(), result.end(), [](const QString& left,
                                               const QString& right) {
        return left.compare(right, agplayer::qt::resourcePathCaseSensitivity()) < 0;
    });
    return result;
}

} // namespace


ResourceFolderController::ResourceFolderController(QObject* parent)
    : QObject(parent)
{
    debounce_.setSingleShot(true);
    debounce_.setInterval(400);
    connect(&debounce_, &QTimer::timeout, this, &ResourceFolderController::rescan);
    connect(&watcher_, &QFileSystemWatcher::directoryChanged,
            this, &ResourceFolderController::scheduleRescan);
}

ResourceFolderController::~ResourceFolderController()
{
    if (scanCancel_) scanCancel_->store(true);
    // Discovery captures values only. Its parentless watcher owns the completion
    // callback and deletes itself, so closing the app never blocks on a slow or
    // unavailable filesystem.
    scanWatcher_.clear();
}

void ResourceFolderController::setLibraryModel(LibraryModel* model)
{
    if (library_ == model) return;
    library_ = model;
    emit libraryModelChanged();
    if (!monitoredRoots_.isEmpty()) scheduleRescan();
}

bool ResourceFolderController::scanning() const noexcept { return scanning_; }

void ResourceFolderController::rescan()
{
    debounce_.stop();
    if (scanCancel_) scanCancel_->store(true);
    const quint64 generation = ++scanGeneration_;
    const auto cancelled = std::make_shared<std::atomic_bool>(false);
    scanCancel_ = cancelled;
    const QStringList roots = monitoredRoots_;
    if (!scanning_) {
        scanning_ = true;
        emit scanningChanged();
    }
    auto* watcher = new QFutureWatcher<QVariantMap>();
    scanWatcher_ = watcher;
    const QPointer<ResourceFolderController> controller(this);
    connect(watcher, &QFutureWatcher<QVariantMap>::finished, watcher,
            [controller, watcher, cancelled, generation] {
        const QVariantMap result = watcher->result();
        watcher->deleteLater();
        if (controller.isNull()) return;
        if (controller->scanWatcher_ == watcher) controller->scanWatcher_.clear();
        if (cancelled->load() || generation != controller->scanGeneration_) return;
        const QStringList directories = normalizedResourcePaths(
            result.value(QStringLiteral("directories")).toStringList()
            + controller->monitoredRoots_);
        const bool changed = directories != controller->resourceDirectories_;
        controller->resourceDirectories_ = directories;
        controller->applyDirectoryWatches(directories);
        if (changed) {
            emit controller->resourceTopologyChanged();
            if (controller.isNull()) return;
        }
        controller->scanning_ = false;
        emit controller->scanningChanged();
        if (controller.isNull()) return;
        emit controller->scanFinished();
        if (controller.isNull()) return;

        if (controller->importer_ != nullptr && !controller->importer_->busy()) {
            QStringList newFiles;
            for (const QString& path : result.value(QStringLiteral("files")).toStringList()) {
                if (!controller->excludedPaths_.contains(resourceLookupKey(path))
                    && (controller->library_ == nullptr
                        || !controller->library_->containsPath(path)))
                    newFiles.append(path);
            }
            if (!newFiles.isEmpty()) controller->importer_->importPaths(newFiles);
        }
    });
    watcher->setFuture(QtConcurrent::run([roots, cancelled] {
        QStringList directories, files;
        for (const QString& root : roots) {
            if (cancelled->load()) break;
            QDirIterator iterator(root, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
                                  QDirIterator::Subdirectories);
            while (!cancelled->load() && iterator.hasNext()) {
                const QString path = iterator.next();
                const QFileInfo info(path);
                if (info.isDir()) directories.append(path);
                else if (agplayer::qt::isSupportedAudioFile(info)) files.append(path);
            }
        }
        files.removeDuplicates();
        return QVariantMap{{QStringLiteral("directories"), directories},
                           {QStringLiteral("files"), files}};
    }));
}

LibraryModel* ResourceFolderController::libraryModel() const noexcept { return library_; }

QStringList ResourceFolderController::monitoredFolders() const
{
    return monitoredRoots_;
}

QStringList ResourceFolderController::resourceDirectories() const
{
    return resourceDirectories_;
}

QString ResourceFolderController::audioFileNameFilter() const
{
    QStringList patterns;
    const QStringList extensions = agplayer::qt::supportedAudioExtensions();
    patterns.reserve(extensions.size());
    for (const QString& extension : extensions)
        patterns.append(QStringLiteral("*.%1").arg(extension));
    return tr("Audio files (%1)").arg(patterns.join(QLatin1Char(' ')));
}

bool ResourceFolderController::addMonitoredFolder(const QString& folder)
{
    const QString path = agplayer::qt::resourcePathIdentity(folder);
    if (!QFileInfo(folder).isDir()
        || std::any_of(monitoredRoots_.cbegin(), monitoredRoots_.cend(),
                       [&path](const QString& candidate) {
            return agplayer::qt::resourcePathsEqual(candidate, path);
        })) return false;
    monitoredRoots_.append(path);
    resourceDirectories_ = normalizedResourcePaths(
        resourceDirectories_ + QStringList{path});
    rebuildDirectoryWatches();
    saveMonitoredFolders();
    emit monitoredFoldersChanged();
    emit resourceRootsChanged();
    emit resourceTopologyChanged();
    scheduleRescan();
    return true;
}

bool ResourceFolderController::addMonitoredFolderUrl(const QUrl& folder)
{
    return folder.isLocalFile() && addMonitoredFolder(folder.toLocalFile());
}

QVariantMap ResourceFolderController::classifyDropUrl(const QUrl& url) const
{
    DropPathKind kind = DropPathKind::Invalid;
    QString canonicalPath;
    if (url.isLocalFile()) {
        const QFileInfo info(url.toLocalFile());
        canonicalPath = info.exists()
            ? agplayer::qt::resourcePathIdentity(info.absoluteFilePath())
            : QString{};
        if (!canonicalPath.isEmpty()) {
            if (info.isDir()) {
                kind = DropPathKind::Directory;
            } else if (info.isFile()) {
                if (agplayer::qt::isSupportedAudioFile(info)) {
                    kind = DropPathKind::AudioFile;
                } else if (agplayer::qt::isSupportedVideoExtension(info.suffix())) {
                    kind = DropPathKind::VideoFile;
                } else {
                    kind = DropPathKind::OtherFile;
                }
            }
        }
    }
    return {
        {QStringLiteral("kind"), QVariant::fromValue(kind)},
        {QStringLiteral("path"), canonicalPath},
        {QStringLiteral("url"), canonicalPath.isEmpty()
            ? QUrl{} : QUrl::fromLocalFile(canonicalPath)},
    };
}

bool ResourceFolderController::pathIsWithin(const QString& candidate,
                                            const QString& root) const
{
    return agplayer::qt::resourcePathIsWithin(candidate, root);
}

bool ResourceFolderController::removeMonitoredFolder(const QString& folder)
{
    const QString path = agplayer::qt::resourcePathIdentity(folder);
    const auto root = std::find_if(monitoredRoots_.cbegin(), monitoredRoots_.cend(),
                                   [&path](const QString& candidate) {
                                       return agplayer::qt::resourcePathsEqual(
                                           candidate, path);
                                   });
    const int index = root == monitoredRoots_.cend()
        ? -1 : static_cast<int>(std::distance(monitoredRoots_.cbegin(), root));
    if (index < 0) return false;
    if (scanCancel_) scanCancel_->store(true);
    monitoredRoots_.removeAt(index);
    QStringList retainedDirectories;
    for (const QString& directory : std::as_const(resourceDirectories_)) {
        const bool belongsToRemainingRoot = std::any_of(
            monitoredRoots_.cbegin(), monitoredRoots_.cend(),
            [&directory](const QString& candidate) {
                return agplayer::qt::resourcePathIsWithin(directory, candidate);
            });
        if (belongsToRemainingRoot) retainedDirectories.append(directory);
    }
    resourceDirectories_ = std::move(retainedDirectories);
    rebuildDirectoryWatches();
    saveMonitoredFolders();
    emit monitoredFoldersChanged();
    emit resourceRootsChanged();
    emit resourceTopologyChanged();
    scheduleRescan();
    return true;
}

bool ResourceFolderController::removeTrackFromLibrary(const QString& trackId)
{
    if (library_ == nullptr) return false;
    const TrackRecord* const track = library_->recordForId(trackId);
    if (track == nullptr) return false;
    const QString path = canonicalLibraryPath(track->path);
    if (path.isEmpty()) return false;
    const QString key = resourceLookupKey(path);
    excludedPaths_.insert(key, path);
    if (!saveMonitoredFolders()) {
        excludedPaths_.remove(key);
        return false;
    }
    if (library_->removeTrack(trackId)) return true;
    excludedPaths_.remove(key);
    saveMonitoredFolders();
    return false;
}

ImportController* ResourceFolderController::importController() const noexcept
{
    return importer_;
}

void ResourceFolderController::setImportController(ImportController* controller)
{
    if (importer_ == controller) return;
    if (importer_ != nullptr) importer_->disconnect(this);
    importer_ = controller;
    if (importer_ != nullptr) {
        connect(importer_, &ImportController::importedTrackIdsChanged, this,
                [this] {
            if (library_ == nullptr || importer_ == nullptr) return;
            QHash<QString, QString> removedExclusions;
            for (const QString& trackId : importer_->importedTrackIds()) {
                const TrackRecord* const track = library_->recordForId(trackId);
                if (track == nullptr) continue;
                const QString key = resourceLookupKey(track->path);
                const auto exclusion = excludedPaths_.constFind(key);
                if (exclusion == excludedPaths_.cend()) continue;
                removedExclusions.insert(key, exclusion.value());
                excludedPaths_.remove(key);
            }
            if (!removedExclusions.isEmpty() && !saveMonitoredFolders()) {
                for (auto it = removedExclusions.cbegin();
                     it != removedExclusions.cend(); ++it)
                    excludedPaths_.insert(it.key(), it.value());
            }
        });
    }
    emit importControllerChanged();
}

QString ResourceFolderController::storagePath() const
{
    return storagePath_;
}

QString ResourceFolderController::lastPersistenceError() const
{
    return lastPersistenceError_;
}

void ResourceFolderController::setStoragePath(const QString& path)
{
    if (storagePath_ == path) return;
    storagePath_ = path;
    loadMonitoredFolders();
    emit storagePathChanged();
}

void ResourceFolderController::scheduleRescan()
{
    debounce_.start();
}

void ResourceFolderController::rebuildDirectoryWatches()
{
    QStringList directories;
    for (const QString& root : monitoredRoots_) {
        if (QFileInfo(root).isDir()) directories.append(root);
    }
    applyDirectoryWatches(directories);
}

void ResourceFolderController::applyDirectoryWatches(
    const QStringList& directories)
{
    const QStringList watched = watcher_.directories();
    if (!watched.isEmpty()) watcher_.removePaths(watched);
    if (!directories.isEmpty()) watcher_.addPaths(directories);
}

void ResourceFolderController::loadMonitoredFolders()
{
    monitoredRoots_.clear();
    excludedPaths_.clear();
    QFile file(storagePath_);
    if (file.open(QIODevice::ReadOnly)) {
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
        if (document.isObject()
            && document.object().value(QStringLiteral("version")).toInt() == 1) {
            const QJsonArray folders =
                document.object().value(QStringLiteral("folders")).toArray();
            for (const QJsonValue& value : folders) {
                const QString requestedPath = value.toString();
                const QString path = agplayer::qt::resourcePathIdentity(
                    requestedPath);
                if (QFileInfo(requestedPath).isDir()
                    && std::none_of(monitoredRoots_.cbegin(), monitoredRoots_.cend(),
                                    [&path](const QString& candidate) {
                        return agplayer::qt::resourcePathsEqual(candidate, path);
                    })) {
                    monitoredRoots_.append(path);
                }
            }
            const QJsonArray excluded = document.object()
                .value(QStringLiteral("excludedPaths")).toArray();
            for (const QJsonValue& value : excluded) {
                const QString path = canonicalLibraryPath(value.toString());
                if (!path.isEmpty())
                    excludedPaths_.insert(resourceLookupKey(path), path);
            }
        }
    }
    const QStringList nextDirectories = normalizedResourcePaths(monitoredRoots_);
    resourceDirectories_ = nextDirectories;
    rebuildDirectoryWatches();
    emit monitoredFoldersChanged();
    emit resourceRootsChanged();
    emit resourceTopologyChanged();
}

bool ResourceFolderController::saveMonitoredFolders()
{
    const auto setError = [this](const QString& error) {
        if (lastPersistenceError_ == error) return;
        lastPersistenceError_ = error;
        emit persistenceStateChanged();
    };
    if (storagePath_.isEmpty()) {
        setError({});
        return true;
    }
    QStringList excluded = excludedPaths_.values();
    std::sort(excluded.begin(), excluded.end(), [](const QString& left,
                                                   const QString& right) {
        return left.compare(right, agplayer::qt::resourcePathCaseSensitivity()) < 0;
    });
    const QByteArray data = QJsonDocument(QJsonObject{
        {QStringLiteral("version"), 1},
        {QStringLiteral("folders"), QJsonArray::fromStringList(monitoredRoots_)},
        {QStringLiteral("excludedPaths"), QJsonArray::fromStringList(excluded)},
    }).toJson(QJsonDocument::Compact);
    QSaveFile file(storagePath_);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(tr("无法保存曲库排除记录：%1").arg(file.errorString()));
        return false;
    }
    if (file.write(data) != data.size()) {
        file.cancelWriting();
        setError(tr("无法写入曲库排除记录：%1").arg(file.errorString()));
        return false;
    }
    if (!file.commit()) {
        setError(tr("无法提交曲库排除记录：%1").arg(file.errorString()));
        return false;
    }
    setError({});
    return true;
}
