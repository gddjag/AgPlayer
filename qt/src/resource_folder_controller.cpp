#include "resource_folder_controller.hpp"
#include "audio_file_discovery.hpp"
#include "import_controller.hpp"
#include "resource_path.hpp"

#include <QDir>
#include <QDateTime>
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

QString fileSignature(const QString& path)
{
    const QFileInfo info(path);
    return info.isFile() ? QString::number(info.size()) + QLatin1Char(':')
        + QString::number(info.lastModified().toMSecsSinceEpoch()) : QString{};
}

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

void ResourceFolderController::rescanAll()
{
    // Explicit discovery restores on-disk songs; startup/watcher scans retain
    // removal choices. Never clear records belonging to unmonitored folders.
    if (!prepareManualScan(monitoredRoots_)) return;
    rescan();
}

bool ResourceFolderController::prepareManualScan(const QStringList& roots)
{
    const auto excludedBefore = excludedPaths_;
    const auto rejectedBefore = rejectedFileSignatures_;
    const auto belongsToRoots = [&roots](const QString& path) {
        return std::any_of(roots.cbegin(), roots.cend(), [&path](const QString& root) {
            return agplayer::qt::resourcePathIsWithin(path, root);
        });
    };
    for (auto it = excludedPaths_.begin(); it != excludedPaths_.end();) {
        if (belongsToRoots(it.value())) it = excludedPaths_.erase(it);
        else ++it;
    }
    for (auto it = rejectedFileSignatures_.begin(); it != rejectedFileSignatures_.end();) {
        if (belongsToRoots(it.key())) it = rejectedFileSignatures_.erase(it);
        else ++it;
    }
    if (excludedBefore == excludedPaths_ && rejectedBefore == rejectedFileSignatures_)
        return true;
    if (saveMonitoredFolders()) return true;
    excludedPaths_ = excludedBefore;
    rejectedFileSignatures_ = rejectedBefore;
    scanSummary_ = tr("无法恢复资源文件夹中的歌曲：%1").arg(lastPersistenceError_);
    emit scanFinished();
    return false;
}

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
        QStringList newFiles;
        const QStringList files = result.value(QStringLiteral("files")).toStringList();
        for (const QString& path : files) {
            if (!controller->excludedPaths_.contains(resourceLookupKey(path))
                && (controller->library_ == nullptr || !controller->library_->containsPath(path))) {
                const QString key = resourceLookupKey(canonicalLibraryPath(path));
                const QString signature = fileSignature(path);
                if (!signature.isEmpty()
                    && controller->rejectedFileSignatures_.value(key) == signature)
                    continue;
                controller->pendingFileSignatures_.insert(key, signature);
                newFiles.append(path);
            }
        }
        controller->scanSummary_ = controller->monitoredRoots_.isEmpty()
            ? tr("尚未添加资源文件夹，请先添加文件夹。")
            : tr("扫描完成：发现 %1 个音频文件，%2 个文件已提交导入，歌曲数量将在导入后更新。已有歌曲不会重复加入；手动重新扫描会恢复目录中曾移除的歌曲。")
                .arg(files.size()).arg(controller->importer_ ? newFiles.size() : 0);
        if (!newFiles.isEmpty() && !controller->importer_)
            controller->scanSummary_ += tr("\n导入服务未就绪，请稍后重试。");
        const QStringList unavailable = result.value(QStringLiteral("unavailable")).toStringList();
        if (!unavailable.isEmpty())
            controller->scanSummary_ += tr("\n以下文件夹不可访问，请检查磁盘连接或权限：\n%1").arg(unavailable.join('\n'));
        controller->scanning_ = false;
        emit controller->scanningChanged();
        if (controller.isNull()) return;
        emit controller->scanFinished();
        if (controller.isNull()) return;

        if (controller->importer_ != nullptr) {
            // ImportController queues requests while busy; never discard discovery.
            if (!newFiles.isEmpty()) controller->importer_->importResourcePaths(newFiles);
        }
    });
    watcher->setFuture(QtConcurrent::run([roots, cancelled] {
        QStringList directories, files, unavailable;
        for (const QString& root : roots) {
            if (cancelled->load()) break;
            const QFileInfo rootInfo(root);
            if (!rootInfo.isDir() || !rootInfo.isReadable()) {
                unavailable.append(root);
                continue;
            }
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
                           {QStringLiteral("files"), files},
                           {QStringLiteral("unavailable"), unavailable}};
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
    if (!QFileInfo(folder).isDir()) return false;
    if (!prepareManualScan({path})) return false;
    if (std::any_of(monitoredRoots_.cbegin(), monitoredRoots_.cend(),
                       [&path](const QString& candidate) {
            return agplayer::qt::resourcePathsEqual(candidate, path);
        })) {
        scheduleRescan();
        return false; // Existing root: retry its songs without duplicating it.
    }
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
    pendingFileSignatures_.clear();
    if (importer_ != nullptr) {
        connect(importer_, &ImportController::fileRejected, this,
                [this](const QString& path, int result) {
            // Permission and temporary I/O failures must remain retryable.
            if (result != AG_UNSUPPORTED_FORMAT && result != AG_DECODE_ERROR) return;
            const QString key = resourceLookupKey(path);
            const QString before = pendingFileSignatures_.take(key);
            if (before.isEmpty() || before != fileSignature(path)) return;
            rejectedFileSignatures_.insert(key, before);
            rejectedStateDirty_ = true;
        });
        connect(importer_, &ImportController::finished, this, [this] {
            // A finished batch can have a resource batch queued behind it.
            // Keep those signatures until their own rejection/success arrives.
            if (rejectedStateDirty_) rejectedStateDirty_ = !saveMonitoredFolders();
        });
        connect(importer_, &ImportController::importedTrackIdsChanged, this,
                [this] {
            if (library_ == nullptr || importer_ == nullptr) return;
            QHash<QString, QString> removedExclusions;
            for (const QString& trackId : importer_->importedTrackIds()) {
                const TrackRecord* const track = library_->recordForId(trackId);
                if (track == nullptr) continue;
                const QString key = resourceLookupKey(track->path);
                pendingFileSignatures_.remove(key);
                if (rejectedFileSignatures_.remove(key) > 0) rejectedStateDirty_ = true;
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
    rejectedFileSignatures_.clear();
    pendingFileSignatures_.clear();
    rejectedStateDirty_ = false;
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
            const QJsonObject rejected = document.object()
                .value(QStringLiteral("rejectedFiles")).toObject();
            for (auto it = rejected.begin(); it != rejected.end(); ++it) {
                if (it.value().isString())
                    rejectedFileSignatures_.insert(resourceLookupKey(it.key()), it.value().toString());
            }
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
    QJsonObject rejected;
    for (auto it = rejectedFileSignatures_.cbegin(); it != rejectedFileSignatures_.cend(); ++it)
        rejected.insert(it.key(), it.value());
    const QByteArray data = QJsonDocument(QJsonObject{
        {QStringLiteral("version"), 1},
        {QStringLiteral("folders"), QJsonArray::fromStringList(monitoredRoots_)},
        {QStringLiteral("excludedPaths"), QJsonArray::fromStringList(excluded)},
        {QStringLiteral("rejectedFiles"), rejected},
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
