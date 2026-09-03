#include "library_manager_controller.hpp"
#include "audio_file_discovery.hpp"
#include "import_controller.hpp"
#include "library_store.hpp"
#include "resource_path.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
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

LibraryManagerController::LibraryManagerController(QObject* parent)
    : QAbstractListModel(parent)
{
    libraryDataPath_ = QDir(
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
        .filePath(QStringLiteral("library.json"));
    debounce_.setSingleShot(true);
    debounce_.setInterval(400);
    connect(&debounce_, &QTimer::timeout, this, &LibraryManagerController::rescan);
    connect(&watcher_, &QFileSystemWatcher::directoryChanged,
            this, &LibraryManagerController::scheduleRescan);
    connect(&watcher_, &QFileSystemWatcher::fileChanged,
            this, &LibraryManagerController::scheduleRescan);
}

int LibraryManagerController::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : rows_.size();
}

QVariant LibraryManagerController::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rows_.size()
        || library_ == nullptr) return {};
    const IssueRow& issue = rows_.at(index.row());
    const QVariantMap track = library_->trackForId(issue.trackId);
    switch (role) {
    case TrackIdRole: return issue.trackId;
    case PathRole: return track.value(QStringLiteral("path"));
    case TitleRole: return track.value(QStringLiteral("title"));
    case ArtistRole: return track.value(QStringLiteral("artist"));
    case AlbumRole: return track.value(QStringLiteral("album"));
    case FormatRole: return track.value(QStringLiteral("format"));
    case StatusRole: return track.value(QStringLiteral("fileStatus"));
    case ContentHashRole: return track.value(QStringLiteral("contentHash"));
    case DuplicateGroupRole: return issue.duplicateGroup;
    case CoverRole: return track.value(QStringLiteral("coverUrl"));
    case FavoriteRole: return track.value(QStringLiteral("favorite"));
    case RatingRole: return track.value(QStringLiteral("rating"));
    case BpmRole: return track.value(QStringLiteral("bpm"));
    case DurationRole: return track.value(QStringLiteral("durationMs"));
    case FileSizeRole: return track.value(QStringLiteral("fileSize"));
    case TagsRole: return track.value(QStringLiteral("tags"));
    default: return {};
    }
}

QHash<int, QByteArray> LibraryManagerController::roleNames() const
{
    return {{TrackIdRole, "trackId"}, {PathRole, "path"}, {TitleRole, "title"},
            {ArtistRole, "artist"}, {AlbumRole, "album"}, {FormatRole, "format"},
            {StatusRole, "status"}, {ContentHashRole, "contentHash"},
            {DuplicateGroupRole, "duplicateGroup"}, {CoverRole, "coverUrl"},
            {FavoriteRole, "favorite"}, {RatingRole, "rating"},
            {BpmRole, "bpm"}, {DurationRole, "durationMs"},
            {FileSizeRole, "fileSize"}, {TagsRole, "tags"}};
}

LibraryModel* LibraryManagerController::libraryModel() const noexcept { return library_; }

void LibraryManagerController::setLibraryModel(LibraryModel* model)
{
    if (library_ == model) return;
    library_ = model;
    if (library_ != nullptr) {
        connect(library_, &QAbstractItemModel::rowsInserted, this,
                &LibraryManagerController::scheduleRescan);
        connect(library_, &QAbstractItemModel::rowsRemoved, this,
                &LibraryManagerController::scheduleRescan);
        connect(library_, &QAbstractItemModel::modelReset, this,
                &LibraryManagerController::scheduleRescan);
        // Rating and favorite edits are immediate list interactions.  They
        // must refresh this view synchronously instead of waiting for the
        // 400 ms filesystem rescan debounce.
        connect(library_, &QAbstractItemModel::dataChanged, this,
                [this](const QModelIndex& first, const QModelIndex& last,
                       const QList<int>&) {
            // A heart/star edit must not reset the ListView while its button
            // is handling a pointer release.  Reset only when the active
            // filter can add/remove rows; otherwise repaint affected rows.
            const bool filterDependsOnEdit = exactRating_ > 0
                || activeCategory_ == QStringLiteral("unrated")
                || activeCategory_ == QStringLiteral("missing")
                || activeCategory_ == QStringLiteral("uncovered")
                || activeCategory_ == QStringLiteral("untagged");
            if (filterDependsOnEdit) {
                rebuildVisibleRows();
            } else {
                const int sourceFirst = qMax(0, first.row());
                const int sourceLast = qMax(sourceFirst, last.row());
                for (int sourceRow = sourceFirst;
                     sourceRow <= sourceLast; ++sourceRow) {
                    const QString trackId = library_->data(
                        library_->index(sourceRow, 0),
                        LibraryModel::TrackIdRole).toString();
                    if (trackId.isEmpty()) continue;
                    for (int row = 0; row < rows_.size(); ++row) {
                        if (rows_.at(row).trackId == trackId) {
                            const QModelIndex changed = index(row);
                            emit dataChanged(changed, changed);
                            break;
                        }
                    }
                }
            }
            emit summaryChanged();
        });
    }
    emit libraryModelChanged();
    rescan();
}

QStringList LibraryManagerController::monitoredFolders() const
{
    return monitoredRoots_;
}

QStringList LibraryManagerController::resourceDirectories() const
{
    return resourceDirectories_;
}

QString LibraryManagerController::audioFileNameFilter() const
{
    QStringList patterns;
    const QStringList extensions = agplayer::qt::supportedAudioExtensions();
    patterns.reserve(extensions.size());
    for (const QString& extension : extensions)
        patterns.append(QStringLiteral("*.%1").arg(extension));
    return tr("Audio files (%1)").arg(patterns.join(QLatin1Char(' ')));
}

QString LibraryManagerController::libraryDataPath() const
{
    return libraryDataPath_;
}

void LibraryManagerController::setLibraryDataPath(const QString& path)
{
    const QString cleanPath = QDir::cleanPath(path);
    if (libraryDataPath_ == cleanPath) return;
    libraryDataPath_ = cleanPath;
    emit libraryDataPathChanged();
}

QString LibraryManagerController::lastBackupPath() const
{
    return lastBackupPath_;
}

QString LibraryManagerController::lastBackupError() const
{
    return lastBackupError_;
}

QUrl LibraryManagerController::defaultBackupUrl() const
{
    const QString desktop = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    const QString date = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
    return QUrl::fromLocalFile(QDir(desktop).filePath(
        QStringLiteral("AgPlayer曲库数据-%1.db").arg(date)));
}

bool LibraryManagerController::backupLibraryData(const QUrl& destination)
{
    lastBackupPath_.clear();
    lastBackupError_.clear();

    if (!destination.isLocalFile() || libraryDataPath_.isEmpty()) {
        lastBackupError_ = tr("请选择有效的备份位置");
        emit backupStateChanged();
        return false;
    }
    if (library_ != nullptr) library_->flush();

    QFile source(libraryDataPath_);
    if (!source.open(QIODevice::ReadOnly)) {
        lastBackupError_ = tr("无法读取曲库数据：%1").arg(source.errorString());
        emit backupStateChanged();
        return false;
    }

    QString destinationPath = QDir::cleanPath(destination.toLocalFile());
    if (QFileInfo(destinationPath).suffix().isEmpty()) destinationPath += QStringLiteral(".db");
    QSaveFile output(destinationPath);
    if (!output.open(QIODevice::WriteOnly)) {
        lastBackupError_ = tr("无法创建备份：%1").arg(output.errorString());
        emit backupStateChanged();
        return false;
    }

    while (!source.atEnd()) {
        const QByteArray chunk = source.read(1024 * 1024);
        if (chunk.isEmpty() && source.error() != QFileDevice::NoError) {
            output.cancelWriting();
            lastBackupError_ = tr("读取曲库数据失败：%1").arg(source.errorString());
            emit backupStateChanged();
            return false;
        }
        if (output.write(chunk) != chunk.size()) {
            output.cancelWriting();
            lastBackupError_ = tr("写入备份失败：%1").arg(output.errorString());
            emit backupStateChanged();
            return false;
        }
    }
    if (!output.commit()) {
        lastBackupError_ = tr("保存备份失败：%1").arg(output.errorString());
        emit backupStateChanged();
        return false;
    }
    lastBackupPath_ = destinationPath;
    emit backupStateChanged();
    return true;
}

bool LibraryManagerController::importLibraryBackup(const QUrl& source)
{
    lastBackupPath_.clear();
    lastBackupError_.clear();
    if (!source.isLocalFile() || library_ == nullptr || libraryDataPath_.isEmpty()) {
        lastBackupError_ = tr("请选择有效的曲库备份");
        emit backupStateChanged();
        return false;
    }

    QFile input(source.toLocalFile());
    if (!input.open(QIODevice::ReadOnly)) {
        lastBackupError_ = tr("无法读取曲库备份：%1").arg(input.errorString());
        emit backupStateChanged();
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(input.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        lastBackupError_ = tr("曲库备份格式无效");
        emit backupStateChanged();
        return false;
    }

    LibraryStore backupStore(source.toLocalFile());
    QList<TrackRecord> tracks = backupStore.load();
    if (!document.array().isEmpty() && tracks.isEmpty()) {
        lastBackupError_ = tr("曲库备份内容无效");
        emit backupStateChanged();
        return false;
    }
    LibraryStore liveStore(libraryDataPath_);
    if (!liveStore.save(tracks)) {
        lastBackupError_ = tr("无法写入曲库数据");
        emit backupStateChanged();
        return false;
    }

    library_->replaceAll(std::move(tracks));
    lastBackupPath_ = QDir::cleanPath(source.toLocalFile());
    emit backupStateChanged();
    return true;
}

bool LibraryManagerController::addMonitoredFolder(const QString& folder)
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

bool LibraryManagerController::addMonitoredFolderUrl(const QUrl& folder)
{
    return folder.isLocalFile() && addMonitoredFolder(folder.toLocalFile());
}

QVariantMap LibraryManagerController::classifyDropUrl(const QUrl& url) const
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

bool LibraryManagerController::pathIsWithin(const QString& candidate,
                                            const QString& root) const
{
    return agplayer::qt::resourcePathIsWithin(candidate, root);
}

bool LibraryManagerController::removeMonitoredFolder(const QString& folder)
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
    return true;
}

bool LibraryManagerController::removeTrackFromLibrary(const QString& trackId)
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

void LibraryManagerController::rescan()
{
    debounce_.stop();
    rebuildDirectoryWatches();
    if (scanCancel_ != nullptr) scanCancel_->store(true);
    const quint64 generation = ++scanGeneration_;
    const QList<TrackRecord> snapshot = library_ != nullptr
        ? library_->tracks() : QList<TrackRecord>{};
    const QStringList monitoredRoots = monitoredRoots_;
    const auto cancelToken = std::make_shared<std::atomic_bool>(false);
    scanCancel_ = cancelToken;

    progress_ = 0;
    if (!scanning_) {
        scanning_ = true;
        emit scanningChanged();
    }
    emit progressChanged();

    auto* watcher = new QFutureWatcher<QVariantMap>(this);
    scanWatcher_ = watcher;
    connect(watcher, &QFutureWatcher<QVariantMap>::finished, this,
            [this, watcher, cancelToken, generation]() {
        watcher->deleteLater();
        if (generation != scanGeneration_ || cancelToken->load()) return;
        const QVariantMap result = watcher->result();
        scanWatcher_.clear();

        const QVariantList scanRows = result.value(QStringLiteral("rows")).toList();
        allRows_.clear();
        allRows_.reserve(scanRows.size());
        for (const QVariant& value : scanRows) {
            const QVariantMap row = value.toMap();
            const QString trackId = row.value(QStringLiteral("trackId")).toString();
            allRows_.append({trackId,
                             row.value(QStringLiteral("duplicateGroup")).toString()});
        }
        if (library_ != nullptr) library_->applyMaintenanceResults(scanRows);
        duplicateGroups_ = result.value(QStringLiteral("groups")).toList();
        const QStringList directories = normalizedResourcePaths(
            result.value(QStringLiteral("directories")).toStringList()
            + monitoredRoots_);
        const bool topologyChanged = directories != resourceDirectories_;
        resourceDirectories_ = directories;
        applyDirectoryWatches(resourceDirectories_);
        if (topologyChanged) emit resourceTopologyChanged();
        missingCount_ = result.value(QStringLiteral("missing")).toInt();
        duplicateCount_ = result.value(QStringLiteral("duplicates")).toInt();
        uncoveredCount_ = result.value(QStringLiteral("uncovered")).toInt();
        untaggedCount_ = result.value(QStringLiteral("untagged")).toInt();
        damagedCount_ = result.value(QStringLiteral("damaged")).toInt();
        totalBytes_ = result.value(QStringLiteral("totalBytes")).toLongLong();
        recentAddedCount_ = result.value(QStringLiteral("recentAdded")).toInt();
        recentPlayedCount_ = result.value(QStringLiteral("recentPlayed")).toInt();
        highFrequencyCount_ = result.value(QStringLiteral("highFrequency")).toInt();
        lowFrequencyCount_ = result.value(QStringLiteral("lowFrequency")).toInt();
        neverPlayedCount_ = result.value(QStringLiteral("neverPlayed")).toInt();
        unratedCount_ = result.value(QStringLiteral("unrated")).toInt();
        rebuildVisibleRows();

        scanning_ = false;
        progress_ = 100;
        emit scanningChanged();
        emit progressChanged();
        emit summaryChanged();
        emit scanFinished();

        if (importer_ != nullptr && !importer_->busy()) {
            QStringList newFiles;
            const QStringList discovered =
                result.value(QStringLiteral("discovered")).toStringList();
            for (const QString& path : discovered) {
                if (!excludedPaths_.contains(resourceLookupKey(path))
                    && (library_ == nullptr || !library_->containsPath(path)))
                    newFiles.append(path);
            }
            if (!newFiles.isEmpty()) importer_->importPaths(newFiles);
        }
    });

    const QPointer<LibraryManagerController> self(this);
    const auto reportProgress = [self, generation, cancelToken](int value) {
        if (self.isNull() || cancelToken->load()) return;
        QMetaObject::invokeMethod(
            self,
            [self, generation, cancelToken, value] {
                if (self.isNull() || cancelToken->load()
                    || generation != self->scanGeneration_) return;
                const int bounded = qBound(0, value, 99);
                if (bounded == self->progress_) return;
                self->progress_ = bounded;
                emit self->progressChanged();
            },
            Qt::QueuedConnection);
    };

    watcher->setFuture(QtConcurrent::run(
        [snapshot, monitoredRoots, cancelToken, reportProgress]() -> QVariantMap {
        QHash<qint64, QStringList> bySize;
        QHash<QString, QStringList> byHash;
        QHash<QByteArray, QStringList> byFingerprint;
        QHash<QString, QString> pathForTrack;
        QHash<QString, int> rowForTrack;
        QVariantList rows;
        rows.reserve(snapshot.size());
        int missing = 0;
        int uncovered = 0;
        int untagged = 0;
        int damaged = 0;
        qint64 totalBytes = 0;
        int recentAdded = 0;
        int recentPlayed = 0;
        int highFrequency = 0;
        int lowFrequency = 0;
        int neverPlayed = 0;
        int unrated = 0;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        constexpr qint64 kDayMs = 24LL * 60 * 60 * 1000;

        int scannedTracks = 0;
        for (const TrackRecord& track : snapshot) {
            if (cancelToken->load()) return {};
            const QFileInfo info(track.path);
            const bool exists = info.isFile();
            const QString hash = exists ? track.contentHash : QString();
            const QString status = !exists ? QStringLiteral("missing")
                : track.coverUrl.isEmpty() ? QStringLiteral("uncovered")
                : track.tags.isEmpty() ? QStringLiteral("untagged")
                : QStringLiteral("normal");
            if (!exists) ++missing;
            if (track.coverUrl.isEmpty()) ++uncovered;
            if (track.tags.isEmpty()) ++untagged;
            totalBytes += track.fileSize > 0 ? track.fileSize : info.size();
            if (track.addedAtMs > 0 && now - track.addedAtMs <= 30 * kDayMs)
                ++recentAdded;
            if (track.lastPlayedAtMs > 0 && now - track.lastPlayedAtMs <= 7 * kDayMs)
                ++recentPlayed;
            if (track.playCount >= 10) ++highFrequency;
            else if (track.playCount >= 1 && track.playCount <= 2) ++lowFrequency;
            if (track.playCount == 0) ++neverPlayed;
            if (track.rating == 0) ++unrated;
            if (exists) {
                bySize[info.size()].append(track.trackId);
                pathForTrack.insert(track.trackId, track.path);
                if (!track.audioFingerprint.isEmpty())
                    byFingerprint[track.audioFingerprint].append(track.trackId);
            }
            rowForTrack.insert(track.trackId, rows.size());
            rows.append(QVariantMap{
                {QStringLiteral("trackId"), track.trackId},
                {QStringLiteral("exists"), exists},
                {QStringLiteral("status"), status},
                {QStringLiteral("hash"), hash},
                {QStringLiteral("size"), info.size()},
                {QStringLiteral("duplicateGroup"), QString()}});
            ++scannedTracks;
            reportProgress(5 + (snapshot.isEmpty()
                ? 55 : (55 * scannedTracks / snapshot.size())));
        }

        // Only same-size candidates can be exact duplicates. Hashing every
        // file made large scans disk-bound and blocked useful incremental work.
        for (auto sizeIt = bySize.cbegin(); sizeIt != bySize.cend(); ++sizeIt) {
            if (cancelToken->load()) return {};
            if (sizeIt.value().size() < 2) continue;
            for (const QString& trackId : sizeIt.value()) {
                const int rowIndex = rowForTrack.value(trackId, -1);
                if (rowIndex < 0) continue;
                QVariantMap row = rows.at(rowIndex).toMap();
                QString hash = row.value(QStringLiteral("hash")).toString();
                if (hash.isEmpty()) {
                    hash = LibraryManagerController::fileHash(
                        pathForTrack.value(trackId), cancelToken.get());
                }
                if (hash.isEmpty()) {
                    ++damaged;
                    row[QStringLiteral("status")] = QStringLiteral("damaged");
                } else {
                    row[QStringLiteral("hash")] = hash;
                    byHash[QString::number(sizeIt.key()) + QLatin1Char(':') + hash]
                        .append(trackId);
                }
                rows[rowIndex] = row;
            }
        }

        QVariantList groups;
        QSet<QString> duplicateIds;
        QSet<QString> exactIds;
        QHash<QString, QString> groupForTrack;
        int groupNumber = 0;
        for (auto it = byHash.cbegin(); it != byHash.cend(); ++it) {
            if (cancelToken->load()) return {};
            if (it.key().isEmpty() || it.value().size() < 2) continue;
            const QString groupId = QStringLiteral("exact-%1").arg(++groupNumber);
            groups.append(QVariantMap{{QStringLiteral("kind"), QStringLiteral("exact")},
                                      {QStringLiteral("trackIds"), it.value()}});
            for (const QString& id : it.value()) {
                duplicateIds.insert(id);
                exactIds.insert(id);
                groupForTrack.insert(id, groupId);
            }
        }
        for (auto it = byFingerprint.cbegin(); it != byFingerprint.cend(); ++it) {
            if (cancelToken->load()) return {};
            QStringList ids;
            for (const QString& id : it.value())
                if (!exactIds.contains(id)) ids.append(id);
            if (ids.size() < 2) continue;
            const QString groupId =
                QStringLiteral("fingerprint-%1").arg(++groupNumber);
            groups.append(QVariantMap{{QStringLiteral("kind"), QStringLiteral("fingerprint")},
                                      {QStringLiteral("trackIds"), ids}});
            for (const QString& id : ids) {
                duplicateIds.insert(id);
                groupForTrack.insert(id, groupId);
            }
        }
        reportProgress(72);
        for (QVariant& value : rows) {
            QVariantMap row = value.toMap();
            row[QStringLiteral("duplicateGroup")] = groupForTrack.value(
                row.value(QStringLiteral("trackId")).toString());
            value = row;
        }

        QStringList discovered;
        QStringList discoveredDirectories;
        int scannedRoots = 0;
        for (const QString& root : monitoredRoots) {
            if (!QFileInfo(root).isDir()) continue;
            discoveredDirectories.append(root);
            QDirIterator iterator(root,
                                  QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
                                  QDirIterator::Subdirectories);
            while (iterator.hasNext() && !cancelToken->load()) {
                const QString path = iterator.next();
                const QFileInfo info(path);
                if (info.isDir())
                    discoveredDirectories.append(path);
                else if (agplayer::qt::isSupportedAudioFile(info))
                    discovered.append(path);
            }
            ++scannedRoots;
            reportProgress(75 + (monitoredRoots.isEmpty()
                ? 20 : (20 * scannedRoots / monitoredRoots.size())));
        }
        discovered.removeDuplicates();
        discoveredDirectories.removeDuplicates();
        return QVariantMap{
            {QStringLiteral("rows"), rows},
            {QStringLiteral("groups"), groups},
            {QStringLiteral("missing"), missing},
            {QStringLiteral("duplicates"), duplicateIds.size()},
            {QStringLiteral("uncovered"), uncovered},
            {QStringLiteral("untagged"), untagged},
            {QStringLiteral("damaged"), damaged},
            {QStringLiteral("totalBytes"), totalBytes},
            {QStringLiteral("recentAdded"), recentAdded},
            {QStringLiteral("recentPlayed"), recentPlayed},
            {QStringLiteral("highFrequency"), highFrequency},
            {QStringLiteral("lowFrequency"), lowFrequency},
            {QStringLiteral("neverPlayed"), neverPlayed},
            {QStringLiteral("unrated"), unrated},
            {QStringLiteral("discovered"), discovered},
            {QStringLiteral("directories"), discoveredDirectories}};
    }));
}

void LibraryManagerController::cancelScan()
{
    ++scanGeneration_;
    if (scanCancel_ != nullptr) scanCancel_->store(true);
    if (scanWatcher_ != nullptr) scanWatcher_->cancel();
    scanWatcher_.clear();
    if (scanning_) {
        scanning_ = false;
        emit scanningChanged();
    }
}

QVariantList LibraryManagerController::duplicateGroups() const { return duplicateGroups_; }
bool LibraryManagerController::scanning() const noexcept { return scanning_; }
int LibraryManagerController::progress() const noexcept { return progress_; }
int LibraryManagerController::totalCount() const noexcept { return library_ ? library_->count() : 0; }
int LibraryManagerController::missingCount() const noexcept { return missingCount_; }
int LibraryManagerController::duplicateCount() const noexcept { return duplicateCount_; }
int LibraryManagerController::uncoveredCount() const noexcept { return uncoveredCount_; }
int LibraryManagerController::untaggedCount() const noexcept { return untaggedCount_; }
int LibraryManagerController::damagedCount() const noexcept { return damagedCount_; }
qint64 LibraryManagerController::totalBytes() const noexcept { return totalBytes_; }
int LibraryManagerController::recentAddedCount() const noexcept { return recentAddedCount_; }
int LibraryManagerController::recentPlayedCount() const noexcept { return recentPlayedCount_; }
int LibraryManagerController::highFrequencyCount() const noexcept { return highFrequencyCount_; }
int LibraryManagerController::lowFrequencyCount() const noexcept { return lowFrequencyCount_; }
int LibraryManagerController::neverPlayedCount() const noexcept { return neverPlayedCount_; }
int LibraryManagerController::unratedCount() const noexcept { return unratedCount_; }

ImportController* LibraryManagerController::importController() const noexcept
{
    return importer_;
}

void LibraryManagerController::setImportController(ImportController* controller)
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

QString LibraryManagerController::storagePath() const
{
    return storagePath_;
}

QString LibraryManagerController::lastPersistenceError() const
{
    return lastPersistenceError_;
}

QString LibraryManagerController::keyword() const { return keyword_; }

void LibraryManagerController::setKeyword(const QString& keyword)
{
    const QString normalized = keyword.trimmed();
    if (keyword_ == normalized) return;
    keyword_ = normalized;
    currentPage_ = 0;
    rebuildVisibleRows();
}

QString LibraryManagerController::formatFilter() const { return formatFilter_; }

void LibraryManagerController::setFormatFilter(const QString& format)
{
    const QString normalized = format.trimmed().toUpper();
    if (formatFilter_ == normalized) return;
    formatFilter_ = normalized;
    currentPage_ = 0;
    rebuildVisibleRows();
}

int LibraryManagerController::minBpm() const noexcept { return minBpm_; }
int LibraryManagerController::maxBpm() const noexcept { return maxBpm_; }

void LibraryManagerController::setBpmRange(int minimum, int maximum)
{
    minimum = qBound(60, minimum, 160);
    maximum = qBound(minimum, maximum, 160);
    if (minBpm_ == minimum && maxBpm_ == maximum) return;
    minBpm_ = minimum;
    maxBpm_ = maximum;
    currentPage_ = 0;
    rebuildVisibleRows();
}

int LibraryManagerController::exactRating() const noexcept { return exactRating_; }

void LibraryManagerController::setExactRating(int rating)
{
    rating = qBound(0, rating, 5);
    if (exactRating_ == rating) return;
    exactRating_ = rating;
    currentPage_ = 0;
    rebuildVisibleRows();
}

QString LibraryManagerController::activeCategory() const { return activeCategory_; }

void LibraryManagerController::setActiveCategory(const QString& category)
{
    static const QSet<QString> categories{
        QStringLiteral("all"), QStringLiteral("storage"), QStringLiteral("missing"),
        QStringLiteral("duplicates"), QStringLiteral("uncovered"),
        QStringLiteral("untagged"), QStringLiteral("recentAdded"),
        QStringLiteral("recentPlayed"), QStringLiteral("highFrequency"),
        QStringLiteral("lowFrequency"), QStringLiteral("neverPlayed"),
        QStringLiteral("unrated")};
    const QString normalized = categories.contains(category)
        ? category : QStringLiteral("all");
    if (activeCategory_ == normalized) return;
    activeCategory_ = normalized;
    currentPage_ = 0;
    rebuildVisibleRows();
}

int LibraryManagerController::currentPage() const noexcept { return currentPage_; }

void LibraryManagerController::setCurrentPage(int page)
{
    page = qBound(0, page, qMax(0, pageCount() - 1));
    if (currentPage_ == page) return;
    currentPage_ = page;
    rebuildVisibleRows();
}

int LibraryManagerController::pageSize() const noexcept { return kPageSize; }
int LibraryManagerController::filteredCount() const noexcept { return filteredCount_; }
int LibraryManagerController::pageCount() const noexcept
{
    return qMax(1, (filteredCount_ + kPageSize - 1) / kPageSize);
}

bool LibraryManagerController::matchesFilter(const IssueRow& issue) const
{
    if (library_ == nullptr) return false;
    const TrackRecord* const track = library_->recordForId(issue.trackId);
    if (track == nullptr) return false;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 day = 24LL * 60 * 60 * 1000;
    if (activeCategory_ == QStringLiteral("missing")
        && track->available && track->fileStatus != QStringLiteral("missing")) return false;
    if (activeCategory_ == QStringLiteral("duplicates") && issue.duplicateGroup.isEmpty()) return false;
    if (activeCategory_ == QStringLiteral("uncovered") && !track->coverUrl.isEmpty()) return false;
    if (activeCategory_ == QStringLiteral("untagged") && !track->tags.isEmpty()) return false;
    if (activeCategory_ == QStringLiteral("recentAdded")
        && (track->addedAtMs <= 0 || track->addedAtMs < now - 30 * day)) return false;
    if (activeCategory_ == QStringLiteral("recentPlayed")
        && (track->lastPlayedAtMs <= 0 || track->lastPlayedAtMs < now - 7 * day)) return false;
    if (activeCategory_ == QStringLiteral("highFrequency") && track->playCount < 10) return false;
    if (activeCategory_ == QStringLiteral("lowFrequency")
        && (track->playCount < 1 || track->playCount > 2)) return false;
    if (activeCategory_ == QStringLiteral("neverPlayed") && track->playCount != 0) return false;
    if (activeCategory_ == QStringLiteral("unrated") && track->rating != 0) return false;
    if (!formatFilter_.isEmpty()
        && track->format.compare(formatFilter_, Qt::CaseInsensitive) != 0) {
        return false;
    }
    if (track->bpm > 0.0
        && (track->bpm < minBpm_ || track->bpm > maxBpm_)) {
        return false;
    }
    if (exactRating_ > 0 && track->rating != exactRating_) return false;
    if (keyword_.isEmpty()) return true;
    const QString haystack = QStringList{
        track->title, track->artist, track->album, track->path,
        track->tags.join(QLatin1Char(' '))}.join(QLatin1Char(' '));
    return haystack.contains(keyword_, Qt::CaseInsensitive);
}

void LibraryManagerController::rebuildVisibleRows()
{
    QList<IssueRow> filtered;
    filtered.reserve(allRows_.size());
    for (const IssueRow& issue : std::as_const(allRows_)) {
        if (matchesFilter(issue)) filtered.append(issue);
    }
    if (activeCategory_ == QStringLiteral("storage") && library_ != nullptr) {
        std::stable_sort(filtered.begin(), filtered.end(), [this](const IssueRow& left,
                                                                  const IssueRow& right) {
            const TrackRecord* const a = library_->recordForId(left.trackId);
            const TrackRecord* const b = library_->recordForId(right.trackId);
            return (a != nullptr ? a->fileSize : 0) > (b != nullptr ? b->fileSize : 0);
        });
    }
    const int previousPage = currentPage_;
    filteredCount_ = filtered.size();
    currentPage_ = qBound(0, currentPage_, qMax(0, pageCount() - 1));
    const int first = currentPage_ * kPageSize;
    const int count = qMin(kPageSize, filteredCount_ - first);
    beginResetModel();
    rows_ = count > 0 ? filtered.mid(first, count) : QList<IssueRow>{};
    endResetModel();
    emit filterChanged();
    if (previousPage != currentPage_) emit pageChanged();
}

void LibraryManagerController::setStoragePath(const QString& path)
{
    if (storagePath_ == path) return;
    storagePath_ = path;
    loadMonitoredFolders();
    emit storagePathChanged();
}

QString LibraryManagerController::fileHash(
    const QString& path, const std::atomic_bool* cancelled)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        if (cancelled != nullptr && cancelled->load()) return {};
        const QByteArray chunk = file.read(1024 * 1024);
        if (chunk.isEmpty() && file.error() != QFile::NoError) return {};
        hash.addData(chunk);
    }
    return QString::fromLatin1(hash.result().toHex());
}

void LibraryManagerController::scheduleRescan()
{
    debounce_.start();
}

void LibraryManagerController::rebuildDirectoryWatches()
{
    QStringList directories;
    for (const QString& root : monitoredRoots_) {
        if (QFileInfo(root).isDir()) directories.append(root);
    }
    applyDirectoryWatches(directories);
}

void LibraryManagerController::applyDirectoryWatches(
    const QStringList& directories)
{
    const QStringList watched = watcher_.directories();
    if (!watched.isEmpty()) watcher_.removePaths(watched);
    if (!directories.isEmpty()) watcher_.addPaths(directories);
}

void LibraryManagerController::loadMonitoredFolders()
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

bool LibraryManagerController::saveMonitoredFolders()
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

QStringList LibraryManagerController::discoverAudioFiles() const
{
    QStringList paths;
    for (const QString& root : monitoredRoots_) {
        QDirIterator iterator(root, QDir::Files, QDirIterator::Subdirectories);
        while (iterator.hasNext()) {
            const QString path = iterator.next();
            if (agplayer::qt::isSupportedAudioFile(QFileInfo(path)))
                paths.append(path);
        }
    }
    paths.removeDuplicates();
    return paths;
}
