#include "library_model.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>

namespace {
QString normalizedCanonicalKey(const QString& canonicalPath)
{
#ifdef Q_OS_WIN
    return canonicalPath.toCaseFolded();
#else
    return canonicalPath;
#endif
}

QString pathKey(const QString& path)
{
    return normalizedCanonicalKey(canonicalLibraryPath(path));
}
}

QString canonicalLibraryPath(const QString& path)
{
    const QFileInfo info(path);
    QString canonical = info.canonicalFilePath();
    if (canonical.isEmpty()) {
        canonical = info.absoluteFilePath();
    }
    return QDir::fromNativeSeparators(QDir::cleanPath(canonical));
}

QString trackIdForPath(const QString& path)
{
    const QString canonical = canonicalLibraryPath(path);
    const QFileInfo info(canonical);
    QByteArray identity = canonical.toUtf8();
    identity.append('\0');
    identity.append(QByteArray::number(info.exists() ? info.size() : -1));
    identity.append('\0');
    identity.append(QByteArray::number(info.exists()
                                           ? info.lastModified().toMSecsSinceEpoch()
                                           : 0));
    return QString::fromLatin1(QCryptographicHash::hash(identity, QCryptographicHash::Sha256)
                                   .toHex());
}

LibraryModel::LibraryModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int LibraryModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : tracks_.size();
}

QVariant LibraryModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= tracks_.size()) {
        return {};
    }

    const TrackRecord& track = tracks_.at(index.row());
    switch (role) {
    case TrackIdRole:
        return track.trackId;
    case PathRole:
        return track.path;
    case TitleRole:
        return track.title;
    case ArtistRole:
        return track.artist;
    case AlbumRole:
        return track.album;
    case FormatRole:
        return track.format;
    case SampleRateRole:
        return track.sampleRate;
    case BitDepthRole:
        return track.bitDepth;
    case BitRateRole:
        return track.bitRate;
    case DurationMsRole:
        return track.durationMs;
    case FileSizeRole:
        return track.fileSize;
    case CoverUrlRole:
        return track.coverUrl;
    case FavoriteRole:
        return track.favorite;
    case AvailableRole:
        return track.available;
    case ImportErrorRole:
        return track.importError;
    default:
        return {};
    }
}

QHash<int, QByteArray> LibraryModel::roleNames() const
{
    return {{TrackIdRole, "trackId"},
            {PathRole, "path"},
            {TitleRole, "title"},
            {ArtistRole, "artist"},
            {AlbumRole, "album"},
            {FormatRole, "format"},
            {SampleRateRole, "sampleRate"},
            {BitDepthRole, "bitDepth"},
            {BitRateRole, "bitRate"},
            {DurationMsRole, "durationMs"},
            {FileSizeRole, "fileSize"},
            {CoverUrlRole, "coverUrl"},
            {FavoriteRole, "favorite"},
            {AvailableRole, "available"},
            {ImportErrorRole, "importError"}};
}

bool LibraryModel::append(TrackRecord track)
{
    track.path = canonicalLibraryPath(track.path);
    const QString key = normalizedCanonicalKey(track.path);
    if (pathKeys_.contains(key)) {
        return false;
    }
    if (track.trackId.isEmpty()) {
        track.trackId = trackIdForPath(track.path);
    }
    const int row = tracks_.size();
    beginInsertRows({}, row, row);
    tracks_.append(std::move(track));
    pathKeys_.insert(key);
    endInsertRows();
    return true;
}

void LibraryModel::replaceAll(QList<TrackRecord> tracks)
{
    beginResetModel();
    tracks_.clear();
    pathKeys_.clear();
    tracks_.reserve(tracks.size());
    pathKeys_.reserve(tracks.size());
    for (TrackRecord& track : tracks) {
        track.path = canonicalLibraryPath(track.path);
        const QString key = normalizedCanonicalKey(track.path);
        if (pathKeys_.contains(key)) {
            continue;
        }
        if (track.trackId.isEmpty()) {
            track.trackId = trackIdForPath(track.path);
        }
        pathKeys_.insert(key);
        tracks_.append(std::move(track));
    }
    endResetModel();
}

const QList<TrackRecord>& LibraryModel::tracks() const noexcept
{
    return tracks_;
}

bool LibraryModel::containsPath(const QString& path) const
{
    return pathKeys_.contains(pathKey(path));
}

bool LibraryModel::setFavorite(int row, bool favorite)
{
    if (row < 0 || row >= tracks_.size()) {
        return false;
    }
    TrackRecord& track = tracks_[row];
    if (track.favorite != favorite) {
        track.favorite = favorite;
        const QModelIndex changed = index(row);
        emit dataChanged(changed, changed, {FavoriteRole});
    }
    return true;
}

void LibraryModel::playRow(int row)
{
    if (row >= 0 && row < tracks_.size() && tracks_.at(row).available) {
        emit playRequested(row);
    }
}

void LibraryModel::flush()
{
    emit flushRequested();
}
