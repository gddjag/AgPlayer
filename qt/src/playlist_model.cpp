#include "playlist_model.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>

#include <algorithm>

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
    playlists_[row].trackIds.insert(trackId);
    emit dataChanged(index(row), index(row), {TrackCountRole});
    emit membershipChanged(playlistId);
    flush();
    return true;
}

bool PlaylistModel::removeTrack(const QString& playlistId, const QString& trackId)
{
    const int row = rowForId(playlistId);
    if (row < 0 || !playlists_[row].trackIds.remove(trackId)) {
        return false;
    }
    emit dataChanged(index(row), index(row), {TrackCountRole});
    emit membershipChanged(playlistId);
    flush();
    return true;
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
    if (root.value(QStringLiteral("version")).toInt() != 1
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
            playlist.trackIds.insert(trackId.toString());
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
        QStringList trackIds(playlist.trackIds.cbegin(), playlist.trackIds.cend());
        std::sort(trackIds.begin(), trackIds.end());
        QJsonArray trackArray;
        for (const QString& trackId : trackIds) {
            trackArray.append(trackId);
        }
        array.append(QJsonObject{{QStringLiteral("id"), playlist.id},
                                 {QStringLiteral("name"), playlist.name},
                                 {QStringLiteral("trackIds"), trackArray}});
    }
    const QJsonDocument document(QJsonObject{{QStringLiteral("version"), 1},
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
