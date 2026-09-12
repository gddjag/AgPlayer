#include "playlist_model.hpp"
#include "library_model.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUrl>
#include <QUuid>

#include <algorithm>

namespace {
QString localPath(const QString& pathOrUrl)
{
    const QUrl url(pathOrUrl);
    return url.isLocalFile() ? url.toLocalFile() : pathOrUrl;
}

QStringList playlistPaths(const QString& filePath, QString* importedName)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return {};
    const QByteArray bytes = file.readAll();
    const QString suffix = QFileInfo(filePath).suffix().toCaseFolded();
    const QDir baseDir = QFileInfo(filePath).dir();
    QStringList paths;
    if (suffix == QStringLiteral("json")) {
        const QJsonDocument document = QJsonDocument::fromJson(bytes);
        if (!document.isObject()) return {};
        const QJsonObject root = document.object();
        if (importedName != nullptr) *importedName = root.value(QStringLiteral("name")).toString();
        for (const QJsonValue& value : root.value(QStringLiteral("tracks")).toArray()) {
            const QString path = value.isObject()
                ? value.toObject().value(QStringLiteral("path")).toString()
                : value.toString();
            if (!path.isEmpty()) paths.append(QDir::isAbsolutePath(path) ? path : baseDir.filePath(path));
        }
        return paths;
    }
    const QStringList lines = QString::fromUtf8(bytes).split(QRegularExpression(QStringLiteral("[\\r\\n]+")),
                                                             Qt::SkipEmptyParts);
    for (const QString& raw : lines) {
        const QString line = raw.trimmed();
        QString path;
        if (suffix == QStringLiteral("pls")) {
            const QRegularExpressionMatch match = QRegularExpression(
                QStringLiteral("^File\\d+=(.*)$"), QRegularExpression::CaseInsensitiveOption).match(line);
            if (match.hasMatch()) path = match.captured(1).trimmed();
        } else if (!line.startsWith(QLatin1Char('#'))) {
            path = line;
        }
        if (!path.isEmpty()) paths.append(QDir::isAbsolutePath(path) ? path : baseDir.filePath(path));
    }
    return paths;
}

QString uniqueCopyPath(const QString& folder, const QString& fileName)
{
    QString target = QDir(folder).filePath(fileName);
    if (!QFileInfo::exists(target)) return target;
    const QFileInfo info(fileName);
    for (int copy = 2; copy < 10000; ++copy) {
        target = QDir(folder).filePath(info.completeBaseName() + QStringLiteral(" (%1)").arg(copy)
            + (info.suffix().isEmpty() ? QString() : QStringLiteral(".") + info.suffix()));
        if (!QFileInfo::exists(target)) return target;
    }
    return {};
}
}

PlaylistModel::PlaylistModel(QString filePath, QObject* parent)
    : QAbstractListModel(parent)
    , filePath_(std::move(filePath))
{
}

int PlaylistModel::count() const
{
    return playlists_.size();
}

int PlaylistModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : playlists_.size();
}

QVariant PlaylistModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= playlists_.size()) {
        return {};
    }
    const Playlist& playlist = playlists_.at(index.row());
    switch (role) {
    case PlaylistIdRole:
        return playlist.id;
    case NameRole:
        return playlist.name;
    case TrackCountRole:
        return playlist.trackIds.size();
    default:
        return {};
    }
}

QHash<int, QByteArray> PlaylistModel::roleNames() const
{
    return {{PlaylistIdRole, "playlistId"},
            {NameRole, "name"},
            {TrackCountRole, "trackCount"}};
}

QString PlaylistModel::createPlaylist(const QString& name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || hasName(trimmed)) {
        return {};
    }
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const int row = playlists_.size();
    beginInsertRows({}, row, row);
    playlists_.append({id, trimmed, {}});
    endInsertRows();
    emit countChanged();
    flush();
    return id;
}

bool PlaylistModel::renamePlaylist(const QString& playlistId, const QString& name)
{
    const int row = rowForId(playlistId);
    const QString trimmed = name.trimmed();
    if (row < 0 || trimmed.isEmpty() || hasName(trimmed, row)) {
        return false;
    }
    if (playlists_[row].name == trimmed) {
        return false;
    }
    playlists_[row].name = trimmed;
    emit dataChanged(index(row), index(row), {NameRole});
    flush();
    return true;
}

bool PlaylistModel::removePlaylist(const QString& playlistId)
{
    const int row = rowForId(playlistId);
    if (row < 0) {
        return false;
    }
    beginRemoveRows({}, row, row);
    playlists_.removeAt(row);
    endRemoveRows();
    emit countChanged();
    flush();
    return true;
}

bool PlaylistModel::addTrack(const QString& playlistId, const QString& trackId)
{
    const int row = rowForId(playlistId);
    if (row < 0 || trackId.isEmpty() || playlists_[row].trackIds.contains(trackId)) {
        return false;
    }
    playlists_[row].trackIds.append(trackId);
    emit dataChanged(index(row), index(row), {TrackCountRole});
    emit membershipChanged(playlistId);
    flush();
    return true;
}

int PlaylistModel::addTracks(const QString& playlistId,
                             const QStringList& trackIds)
{
    const int row = rowForId(playlistId);
    if (row < 0) {
        return 0;
    }
    int added = 0;
    for (const QString& trackId : trackIds) {
        if (!trackId.isEmpty()
            && !playlists_[row].trackIds.contains(trackId)) {
            playlists_[row].trackIds.append(trackId);
            ++added;
        }
    }
    if (added > 0) {
        emit dataChanged(index(row), index(row), {TrackCountRole});
        emit membershipChanged(playlistId);
        flush();
    }
    return added;
}

bool PlaylistModel::removeTrack(const QString& playlistId, const QString& trackId)
{
    const int row = rowForId(playlistId);
    if (row < 0 || !playlists_[row].trackIds.removeOne(trackId)) {
        return false;
    }
    emit dataChanged(index(row), index(row), {TrackCountRole});
    emit membershipChanged(playlistId);
    flush();
    return true;
}

int PlaylistModel::removeTracks(const QString& playlistId,
                                const QStringList& trackIds)
{
    const int row = rowForId(playlistId);
    if (row < 0) {
        return 0;
    }
    int removed = 0;
    for (const QString& trackId : trackIds) {
        removed += playlists_[row].trackIds.removeOne(trackId) ? 1 : 0;
    }
    if (removed > 0) {
        emit dataChanged(index(row), index(row), {TrackCountRole});
        emit membershipChanged(playlistId);
        flush();
    }
    return removed;
}

int PlaylistModel::moveTracks(const QString& sourcePlaylistId,
                              const QString& targetPlaylistId,
                              const QStringList& trackIds)
{
    const int sourceRow = rowForId(sourcePlaylistId);
    const int targetRow = rowForId(targetPlaylistId);
    if (sourceRow < 0 || targetRow < 0 || sourceRow == targetRow) {
        return 0;
    }
    int moved = 0;
    bool targetChanged = false;
    for (const QString& trackId : trackIds) {
        if (trackId.isEmpty()
            || !playlists_[sourceRow].trackIds.contains(trackId)) {
            continue;
        }
        if (!playlists_[targetRow].trackIds.contains(trackId)) {
            playlists_[targetRow].trackIds.append(trackId);
            targetChanged = true;
        }
        playlists_[sourceRow].trackIds.removeOne(trackId);
        ++moved;
    }
    if (moved > 0) {
        emit dataChanged(index(sourceRow), index(sourceRow), {TrackCountRole});
        emit membershipChanged(sourcePlaylistId);
        if (targetChanged) {
            emit dataChanged(index(targetRow), index(targetRow),
                             {TrackCountRole});
            emit membershipChanged(targetPlaylistId);
        }
        flush();
    }
    return moved;
}

bool PlaylistModel::removeTrackFromAll(const QString& trackId)
{
    bool changed = false;
    for (int row = 0; row < playlists_.size(); ++row) {
        if (!playlists_[row].trackIds.removeOne(trackId)) {
            continue;
        }
        changed = true;
        emit dataChanged(index(row), index(row), {TrackCountRole});
        emit membershipChanged(playlists_.at(row).id);
    }
    if (changed) {
        flush();
    }
    return changed;
}

bool PlaylistModel::containsTrack(const QString& playlistId, const QString& trackId) const
{
    const int row = rowForId(playlistId);
    return row >= 0 && playlists_.at(row).trackIds.contains(trackId);
}

QString PlaylistModel::nameForId(const QString& playlistId) const
{
    const int row = rowForId(playlistId);
    return row >= 0 ? playlists_.at(row).name : QString();
}

QString PlaylistModel::idAt(int row) const
{
    return row >= 0 && row < playlists_.size() ? playlists_.at(row).id : QString();
}

QStringList PlaylistModel::trackIdsForPlaylist(const QString& playlistId) const
{
    const int row = rowForId(playlistId);
    return row >= 0 ? playlists_.at(row).trackIds : QStringList{};
}

bool PlaylistModel::movePlaylist(int fromRow, int toRow)
{
    if (fromRow < 0 || fromRow >= playlists_.size() || toRow < 0
        || toRow >= playlists_.size() || fromRow == toRow) {
        return false;
    }
    const int destination = toRow > fromRow ? toRow + 1 : toRow;
    if (!beginMoveRows({}, fromRow, fromRow, {}, destination)) {
        return false;
    }
    playlists_.move(fromRow, toRow);
    endMoveRows();
    flush();
    return true;
}

bool PlaylistModel::moveTrack(const QString& playlistId, int fromRow, int toRow)
{
    const int row = rowForId(playlistId);
    if (row < 0 || fromRow < 0 || fromRow >= playlists_[row].trackIds.size()
        || toRow < 0 || toRow >= playlists_[row].trackIds.size()
        || fromRow == toRow) {
        return false;
    }
    playlists_[row].trackIds.move(fromRow, toRow);
    emit membershipChanged(playlistId);
    flush();
    return true;
}

int PlaylistModel::reorderTracks(const QString& playlistId,
                                 const QStringList& trackIds,
                                 const QString& beforeTrackId)
{
    const int row = rowForId(playlistId);
    if (row < 0) return 0;
    const QStringList original = playlists_.at(row).trackIds;
    QSet<QString> requested;
    for (const QString& id : trackIds) {
        if (original.contains(id)) requested.insert(id);
    }
    if (requested.isEmpty()) return 0;

    QStringList selected;
    QStringList remaining;
    for (const QString& id : original) {
        (requested.contains(id) ? selected : remaining).append(id);
    }
    int destination = remaining.indexOf(beforeTrackId);
    if (destination < 0) destination = remaining.size();
    for (int index = 0; index < selected.size(); ++index) {
        remaining.insert(destination + index, selected.at(index));
    }
    playlists_[row].trackIds = std::move(remaining);
    emit membershipChanged(playlistId);
    flush();
    return selected.size();
}

QString PlaylistModel::importPlaylist(const QString& filePath)
{
    const QString path = localPath(filePath);
    QString name;
    const QStringList paths = playlistPaths(path, &name);
    if (paths.isEmpty()) return {};
    if (name.trimmed().isEmpty()) name = QFileInfo(path).completeBaseName();
    QString uniqueName = name;
    for (int copy = 2; hasName(uniqueName); ++copy)
        uniqueName = name + QStringLiteral(" (%1)").arg(copy);
    const QString id = createPlaylist(uniqueName);
    if (id.isEmpty()) return {};
    QStringList trackIds;
    trackIds.reserve(paths.size());
    for (const QString& sourcePath : paths)
        trackIds.append(trackIdForPath(sourcePath));
    addTracks(id, trackIds);
    return id;
}

QStringList PlaylistModel::pathsFromPlaylist(const QString& filePath) const
{
    return playlistPaths(localPath(filePath), nullptr);
}

bool PlaylistModel::exportPlaylist(const QString& playlistId,
                                   const QString& filePath,
                                   const QVariantMap& trackPathsById,
                                   bool copyFiles) const
{
    const QString outputFilePath = localPath(filePath);
    const int row = rowForId(playlistId);
    if (row < 0 || outputFilePath.isEmpty()) return false;
    const Playlist& playlist = playlists_.at(row);
    const QFileInfo destination(outputFilePath);
    QString copyFolder;
    if (copyFiles) {
        copyFolder = destination.dir().filePath(destination.completeBaseName() + QStringLiteral("_files"));
        if (!QDir().mkpath(copyFolder)) return false;
    }
    QStringList exportedPaths;
    QJsonArray jsonTracks;
    for (const QString& trackId : playlist.trackIds) {
        const QString source = trackPathsById.value(trackId).toString();
        if (!QFileInfo(source).isFile()) continue;
        QString outputPath = source;
        if (copyFiles) {
            const QString target = uniqueCopyPath(copyFolder, QFileInfo(source).fileName());
            if (target.isEmpty() || !QFile::copy(source, target)) continue;
            outputPath = destination.dir().relativeFilePath(target);
        }
        exportedPaths.append(QDir::fromNativeSeparators(outputPath));
        jsonTracks.append(QJsonObject{{QStringLiteral("trackId"), trackId},
                                      {QStringLiteral("path"), QDir::fromNativeSeparators(outputPath)}});
    }
    const QString suffix = destination.suffix().toCaseFolded();
    QByteArray bytes;
    if (suffix == QStringLiteral("json")) {
        bytes = QJsonDocument(QJsonObject{{QStringLiteral("version"), 1},
                                          {QStringLiteral("name"), playlist.name},
                                          {QStringLiteral("tracks"), jsonTracks}})
                    .toJson(QJsonDocument::Indented);
    } else if (suffix == QStringLiteral("pls")) {
        QString text = QStringLiteral("[playlist]\nNumberOfEntries=%1\n").arg(exportedPaths.size());
        for (int index = 0; index < exportedPaths.size(); ++index)
            text += QStringLiteral("File%1=%2\n").arg(index + 1).arg(exportedPaths.at(index));
        text += QStringLiteral("Version=2\n");
        bytes = text.toUtf8();
    } else {
        QString text = QStringLiteral("#EXTM3U\n");
        for (const QString& path : exportedPaths) text += path + QLatin1Char('\n');
        bytes = text.toUtf8();
    }
    QSaveFile file(outputFilePath);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}

bool PlaylistModel::load()
{
    if (filePath_.isEmpty() || !QFileInfo::exists(filePath_)) {
        return true;
    }
    QFile file(filePath_);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return false;
    }
    const QJsonObject root = document.object();
    const int version = root.value(QStringLiteral("version")).toInt();
    if ((version != 1 && version != 2)
        || !root.value(QStringLiteral("playlists")).isArray()) {
        return false;
    }
    QList<Playlist> loaded;
    QSet<QString> ids;
    QSet<QString> names;
    const QJsonArray array = root.value(QStringLiteral("playlists")).toArray();
    loaded.reserve(array.size());
    for (const QJsonValue& value : array) {
        if (!value.isObject()) {
            return false;
        }
        const QJsonObject object = value.toObject();
        Playlist playlist;
        playlist.id = object.value(QStringLiteral("id")).toString();
        playlist.name = object.value(QStringLiteral("name")).toString().trimmed();
        const QString nameKey = playlist.name.toCaseFolded();
        if (playlist.id.isEmpty() || playlist.name.isEmpty() || ids.contains(playlist.id)
            || names.contains(nameKey) || !object.value(QStringLiteral("trackIds")).isArray()) {
            return false;
        }
        ids.insert(playlist.id);
        names.insert(nameKey);
        const QJsonArray trackIds = object.value(QStringLiteral("trackIds")).toArray();
        for (const QJsonValue& trackId : trackIds) {
            if (!trackId.isString() || trackId.toString().isEmpty()) {
                return false;
            }
            if (!playlist.trackIds.contains(trackId.toString())) {
                playlist.trackIds.append(trackId.toString());
            }
        }
        loaded.append(std::move(playlist));
    }
    beginResetModel();
    playlists_ = std::move(loaded);
    endResetModel();
    emit countChanged();
    return true;
}

bool PlaylistModel::flush() const
{
    if (filePath_.isEmpty()) {
        return true;
    }
    const QFileInfo info(filePath_);
    if (!QDir().mkpath(info.absolutePath())) {
        return false;
    }
    QJsonArray array;
    for (const Playlist& playlist : playlists_) {
        QJsonArray trackArray;
        for (const QString& trackId : playlist.trackIds) {
            trackArray.append(trackId);
        }
        array.append(QJsonObject{{QStringLiteral("id"), playlist.id},
                                 {QStringLiteral("name"), playlist.name},
                                 {QStringLiteral("trackIds"), trackArray}});
    }
    const QJsonDocument document(QJsonObject{{QStringLiteral("version"), 2},
                                              {QStringLiteral("playlists"), array}});
    const QByteArray bytes = document.toJson(QJsonDocument::Compact);
    QSaveFile file(filePath_);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

int PlaylistModel::rowForId(const QString& playlistId) const
{
    for (int row = 0; row < playlists_.size(); ++row) {
        if (playlists_.at(row).id == playlistId) {
            return row;
        }
    }
    return -1;
}

bool PlaylistModel::hasName(const QString& name, int exceptRow) const
{
    for (int row = 0; row < playlists_.size(); ++row) {
        if (row != exceptRow
            && playlists_.at(row).name.compare(name, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}
