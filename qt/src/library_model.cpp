#include "library_model.hpp"
#include "metadata_text.hpp"

#include "agplayer/c_api.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThread>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace {
const QUrl kPackagedCover(
    QStringLiteral("qrc:/qt/qml/AgPlayer/assets/brand/logo-mark.png"));

QUrl displayCoverUrl(const QUrl& coverUrl)
{
    if (coverUrl.isLocalFile() && !QFileInfo::exists(coverUrl.toLocalFile())) {
        return kPackagedCover;
    }
    return coverUrl;
}

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

QString copiedMetadata(const char* value)
{
    return agplayer::qt::decodeMetadataText(value);
}

QString coverSuffix(const QString& mimeType)
{
    if (mimeType.compare(QStringLiteral("image/jpeg"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral(".jpg");
    }
    if (mimeType.compare(QStringLiteral("image/png"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral(".png");
    }
    if (mimeType.compare(QStringLiteral("image/webp"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral(".webp");
    }
    if (mimeType.compare(QStringLiteral("image/bmp"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral(".bmp");
    }
    return QStringLiteral(".bin");
}

QUrl cacheEmbeddedCover(const unsigned char* data,
                        const std::size_t size,
                        const QString& mimeType)
{
    if (data == nullptr || size == 0
        || size > static_cast<std::size_t>(
            (std::numeric_limits<qsizetype>::max)())) {
        return {};
    }
    const QByteArray bytes(reinterpret_cast<const char*>(data),
                           static_cast<qsizetype>(size));
    const QStringList cacheRoots{
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation),
        QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
            .filePath(QStringLiteral("AgPlayer"))};
    QString coverDirectory;
    for (const QString& cacheRoot : cacheRoots) {
        if (cacheRoot.isEmpty()) continue;
        const QString candidate = QDir(cacheRoot).filePath(QStringLiteral("covers"));
        if (QDir().mkpath(candidate)) {
            coverDirectory = candidate;
            break;
        }
    }
    if (coverDirectory.isEmpty()) return {};
    const QString digest = QString::fromLatin1(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    const QString coverPath = QDir(coverDirectory).filePath(
        digest + coverSuffix(mimeType));
    if (!QFileInfo::exists(coverPath)) {
        QSaveFile output(coverPath);
        if (!output.open(QIODevice::WriteOnly)
            || output.write(bytes) != bytes.size() || !output.commit()) {
            return {};
        }
    }
    return QUrl::fromLocalFile(coverPath);
}

QStringList normalizeTags(const QStringList& tags)
{
    QStringList normalized;
    QSet<QString> keys;
    normalized.reserve(tags.size());
    for (const QString& tag : tags) {
        const QString displayName = tag.trimmed();
        const QString key = displayName.toCaseFolded();
        if (!displayName.isEmpty() && !keys.contains(key)) {
            keys.insert(key);
            normalized.append(displayName);
        }
    }
    return normalized;
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

int LibraryModel::count() const noexcept
{
    return tracks_.size();
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
    case AlbumArtistRole:
        return track.albumArtist;
    case GenreRole:
        return track.genre;
    case YearRole:
        return track.year;
    case DateRole:
        return track.date;
    case ComposerRole:
        return track.composer;
    case FormatRole:
        return track.format;
    case SampleRateRole:
        return track.sampleRate;
    case BitDepthRole:
        return track.bitDepth;
    case ChannelsRole:
        return track.channels;
    case HasAudioRole:
        return track.hasAudio;
    case HasVideoRole:
        return track.hasVideo;
    case MetadataProbeAttemptedRole:
        return track.metadataProbeAttempted;
    case BitRateRole:
        return track.bitRate;
    case DurationMsRole:
        return track.durationMs;
    case FileSizeRole:
        return track.fileSize;
    case CoverUrlRole:
        return displayCoverUrl(track.coverUrl);
    case FavoriteRole:
        return track.favorite;
    case RatingRole:
        return track.rating;
    case BpmRole:
        return track.bpm;
    case AvailableRole:
        return track.available;
    case ImportErrorRole:
        return track.importError;
    case LyricsRole:
        return track.lyrics;
    case PlayCountRole:
        return track.playCount;
    case LastPlayedAtRole:
        return track.lastPlayedAtMs;
    case TagsRole:
        return track.tags;
    case AddedAtRole:
        return track.addedAtMs;
    case FileStatusRole:
        return track.fileStatus;
    case ContentHashRole:
        return track.contentHash;
    case AudioFingerprintRole:
        return track.audioFingerprint;
    case ReplayGainScannedRole:
        return track.replayGainScanned;
    case ReplayGainTrackDbRole:
        return track.replayGainTrackDb;
    case ReplayGainAlbumDbRole:
        return track.replayGainAlbumDb;
    case ReplayPeakRole:
        return track.replayPeak;
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
            {AlbumArtistRole, "albumArtist"},
            {GenreRole, "genre"},
            {YearRole, "year"},
            {DateRole, "date"},
            {ComposerRole, "composer"},
            {FormatRole, "format"},
            {SampleRateRole, "sampleRate"},
            {BitDepthRole, "bitDepth"},
            {ChannelsRole, "channels"},
            {HasAudioRole, "hasAudio"},
            {HasVideoRole, "hasVideo"},
            {MetadataProbeAttemptedRole, "metadataProbeAttempted"},
            {BitRateRole, "bitRate"},
            {DurationMsRole, "durationMs"},
            {FileSizeRole, "fileSize"},
            {CoverUrlRole, "coverUrl"},
            {FavoriteRole, "favorite"},
            {RatingRole, "rating"},
            {BpmRole, "bpm"},
            {AvailableRole, "available"},
            {ImportErrorRole, "importError"},
            {LyricsRole, "lyrics"},
            {PlayCountRole, "playCount"},
            {LastPlayedAtRole, "lastPlayedAtMs"},
            {TagsRole, "tags"},
            {AddedAtRole, "addedAtMs"},
            {FileStatusRole, "fileStatus"},
            {ContentHashRole, "contentHash"},
            {AudioFingerprintRole, "audioFingerprint"},
            {ReplayGainScannedRole, "replayGainScanned"},
            {ReplayGainTrackDbRole, "replayGainTrackDb"},
            {ReplayGainAlbumDbRole, "replayGainAlbumDb"},
            {ReplayPeakRole, "replayPeak"}};
}

bool LibraryModel::append(TrackRecord track)
{
    return !appendBatch({std::move(track)}).isEmpty();
}

QStringList LibraryModel::appendBatch(QList<TrackRecord> tracks)
{
    return insertBatch(tracks_.size(), std::move(tracks));
}

QStringList LibraryModel::insertBatch(int row, QList<TrackRecord> tracks)
{
    QList<TrackRecord> accepted;
    accepted.reserve(tracks.size());
    QSet<QString> batchKeys;
    const qint64 batchAddedAtMs = QDateTime::currentMSecsSinceEpoch();
    for (TrackRecord& track : tracks) {
        track.path = canonicalLibraryPath(track.path);
        const QString key = normalizedCanonicalKey(track.path);
        if (pathKeys_.contains(key) || batchKeys.contains(key)) {
            continue;
        }
        if (track.trackId.isEmpty()) {
            track.trackId = trackIdForPath(track.path);
        }
        if (track.addedAtMs <= 0) {
            track.addedAtMs = batchAddedAtMs;
        }
        batchKeys.insert(key);
        accepted.append(std::move(track));
    }
    if (accepted.isEmpty()) {
        return {};
    }

    const int firstRow = qBound(0, row, tracks_.size());
    const int lastRow = firstRow + accepted.size() - 1;
    bool favoriteAdded = false;
    QStringList insertedIds;
    insertedIds.reserve(accepted.size());
    beginInsertRows({}, firstRow, lastRow);
    for (const TrackRecord& track : std::as_const(accepted)) {
        favoriteAdded = favoriteAdded || track.favorite;
        insertedIds.append(track.trackId);
    }
    // A complete batch must move the existing tail once. Repeated individual
    // inserts at row zero made large folder imports quadratic on the GUI
    // thread and caused the visible freeze reported during library imports.
    tracks_.reserve(tracks_.size() + accepted.size());
    for (TrackRecord& track : accepted) {
        tracks_.append(std::move(track));
    }
    std::rotate(tracks_.begin() + firstRow,
                tracks_.end() - accepted.size(), tracks_.end());
    pathKeys_.reserve(tracks_.size());
    pathRows_.reserve(tracks_.size());
    trackRows_.reserve(tracks_.size());
    // The prefix before the insertion did not move. In particular, import
    // batches appended to an empty library should update only their new rows.
    for (int index = firstRow; index < tracks_.size(); ++index) {
        const QString key = normalizedCanonicalKey(tracks_.at(index).path);
        pathKeys_.insert(key);
        pathRows_.insert(key, index);
        trackRows_.insert(tracks_.at(index).trackId, index);
    }
    endInsertRows();
    emit countChanged();
    if (favoriteAdded) {
        emit favoriteCountChanged();
    }
    return insertedIds;
}

void LibraryModel::replaceAll(QList<TrackRecord> tracks)
{
    const int previousCount = tracks_.size();
    beginResetModel();
    metadataProbeInFlight_.clear();
    tracks_.clear();
    pathKeys_.clear();
    pathRows_.clear();
    trackRows_.clear();
    tracks_.reserve(tracks.size());
    pathKeys_.reserve(tracks.size());
    pathRows_.reserve(tracks.size());
    trackRows_.reserve(tracks.size());
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
        pathRows_.insert(key, tracks_.size() - 1);
        trackRows_.insert(tracks_.back().trackId, tracks_.size() - 1);
    }
    endResetModel();
    if (tracks_.size() != previousCount) {
        emit countChanged();
    }
    emit favoriteCountChanged();
    emit historyCountChanged();
    emit recentAddedCountChanged();
    emit neverPlayedCountChanged();
}

const QList<TrackRecord>& LibraryModel::tracks() const noexcept
{
    return tracks_;
}

const TrackRecord* LibraryModel::recordForId(const QString& trackId) const noexcept
{
    const int row = trackRows_.value(trackId, -1);
    return row >= 0 && row < tracks_.size() ? &tracks_.at(row) : nullptr;
}

bool LibraryModel::containsPath(const QString& path) const
{
    return pathKeys_.contains(pathKey(path));
}

int LibraryModel::indexForLocalFile(const QString& localFilePath) const
{
    return pathRows_.value(pathKey(localFilePath), -1);
}

int LibraryModel::indexForTrackId(const QString& trackId) const
{
    return trackRows_.value(trackId, -1);
}

QVariantMap LibraryModel::trackForId(const QString& trackId) const
{
    const int row = indexForTrackId(trackId);
    if (row < 0) {
        return {};
    }
    const QModelIndex modelIndex = index(row, 0);
    QVariantMap values;
    const QHash<int, QByteArray> roles = roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it) {
        values.insert(QString::fromLatin1(it.value()), data(modelIndex, it.key()));
    }
    return values;
}

bool LibraryModel::removeTrack(const QString& trackId)
{
    const int row = indexForTrackId(trackId);
    if (row < 0) {
        return false;
    }

    const TrackRecord removed = tracks_.at(row);
    metadataProbeInFlight_.remove(trackId);
    beginRemoveRows({}, row, row);
    tracks_.removeAt(row);
    pathKeys_.remove(pathKey(removed.path));
    pathRows_.remove(pathKey(removed.path));
    trackRows_.remove(trackId);
    for (int index = row; index < tracks_.size(); ++index) {
        pathRows_.insert(pathKey(tracks_.at(index).path), index);
        trackRows_.insert(tracks_.at(index).trackId, index);
    }
    endRemoveRows();

    emit countChanged();
    if (removed.favorite) {
        emit favoriteCountChanged();
    }
    if (removed.playCount > 0) {
        emit historyCountChanged();
    }
    emit trackRemoved(trackId);
    emit flushRequested();
    return true;
}

QUrl LibraryModel::containingFolderUrl(const QString& trackId) const
{
    const int row = indexForTrackId(trackId);
    if (row < 0) {
        return {};
    }
    return QUrl::fromLocalFile(QFileInfo(tracks_.at(row).path).absolutePath());
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
        emit favoriteCountChanged();
    }
    return true;
}

bool LibraryModel::setRating(int row, int rating)
{
    if (row < 0 || row >= tracks_.size()) {
        return false;
    }
    const int clamped = std::clamp(rating, 0, 5);
    TrackRecord& track = tracks_[row];
    if (track.rating == clamped) {
        return false;
    }
    track.rating = clamped;
    const QModelIndex changed = index(row, 0);
    emit dataChanged(changed, changed, {RatingRole});
    return true;
}

bool LibraryModel::setBpm(const QString& trackId, double bpm)
{
    const int row = indexForTrackId(trackId);
    if (row < 0 || !std::isfinite(bpm) || bpm < 20.0 || bpm > 400.0) {
        return false;
    }
    if (qFuzzyCompare(tracks_[row].bpm, bpm)) {
        return false;
    }
    tracks_[row].bpm = bpm;
    const QModelIndex changed = index(row, 0);
    emit dataChanged(changed, changed, {BpmRole});
    emit flushRequested();
    return true;
}

bool LibraryModel::setTags(const QString& trackId, const QStringList& tags)
{
    const int row = indexForTrackId(trackId);
    if (row < 0) {
        return false;
    }
    if (!applyTagsAtRow(row, normalizeTags(tags))) return false;
    emit flushRequested();
    return true;
}

int LibraryModel::setTagsForTracks(const QStringList& trackIds,
                                   const QStringList& tags)
{
    const QStringList normalized = normalizeTags(tags);
    QSet<QString> requestedIds;
    int changed = 0;
    for (const QString& trackId : trackIds) {
        if (trackId.isEmpty() || requestedIds.contains(trackId)) continue;
        requestedIds.insert(trackId);
        const int row = indexForTrackId(trackId);
        if (row >= 0 && applyTagsAtRow(row, normalized)) ++changed;
    }
    if (changed > 0) emit flushRequested();
    return changed;
}

int LibraryModel::addTagToTracks(const QStringList& trackIds,
                                 const QString& tag)
{
    const QStringList normalizedTag = normalizeTags({tag});
    if (normalizedTag.isEmpty()) return 0;

    const QString targetKey = normalizedTag.constFirst().toCaseFolded();
    QSet<QString> requestedIds;
    QList<int> requestedRows;
    requestedRows.reserve(trackIds.size());
    for (const QString& trackId : trackIds) {
        if (trackId.isEmpty() || requestedIds.contains(trackId)) continue;
        requestedIds.insert(trackId);
        const int row = indexForTrackId(trackId);
        if (row < 0) return 0;
        requestedRows.append(row);
    }

    int changed = 0;
    for (const int row : requestedRows) {
        QStringList next = tracks_.at(row).tags;
        const bool alreadyPresent = std::any_of(
            next.cbegin(), next.cend(), [&targetKey](const QString& existing) {
                return existing.toCaseFolded() == targetKey;
            });
        if (alreadyPresent) continue;
        next.append(normalizedTag.constFirst());
        if (applyTagsAtRow(row, normalizeTags(next))) ++changed;
    }
    if (changed > 0) emit flushRequested();
    return changed;
}

int LibraryModel::removeTagFromTracks(const QStringList& trackIds,
                                      const QString& tag)
{
    const QString targetKey = tag.trimmed().toCaseFolded();
    if (targetKey.isEmpty()) return 0;

    QSet<QString> requestedIds;
    int changed = 0;
    for (const QString& trackId : trackIds) {
        if (trackId.isEmpty() || requestedIds.contains(trackId)) continue;
        requestedIds.insert(trackId);
        const int row = indexForTrackId(trackId);
        if (row < 0) continue;
        QStringList next = tracks_.at(row).tags;
        next.erase(std::remove_if(next.begin(), next.end(),
                                  [&targetKey](const QString& existing) {
            return existing.toCaseFolded() == targetKey;
        }), next.end());
        if (applyTagsAtRow(row, next)) ++changed;
    }
    if (changed > 0) emit flushRequested();
    return changed;
}

int LibraryModel::renameTag(const QString& oldKey, const QString& displayName)
{
    const QString normalizedOldKey = oldKey.trimmed().toCaseFolded();
    const QStringList replacement = normalizeTags({displayName});
    if (normalizedOldKey.isEmpty() || replacement.isEmpty()) return 0;

    int changed = 0;
    for (int row = 0; row < tracks_.size(); ++row) {
        QStringList next = tracks_.at(row).tags;
        bool found = false;
        for (QString& tag : next) {
            if (tag.toCaseFolded() == normalizedOldKey) {
                tag = replacement.front();
                found = true;
            }
        }
        if (found && applyTagsAtRow(row, normalizeTags(next))) ++changed;
    }
    if (changed > 0) emit flushRequested();
    return changed;
}

int LibraryModel::removeTag(const QString& key)
{
    const QString normalizedKey = key.trimmed().toCaseFolded();
    if (normalizedKey.isEmpty()) return 0;

    int changed = 0;
    for (int row = 0; row < tracks_.size(); ++row) {
        QStringList next = tracks_.at(row).tags;
        next.erase(std::remove_if(next.begin(), next.end(),
                                  [&normalizedKey](const QString& tag) {
                                      return tag.toCaseFolded() == normalizedKey;
                                  }), next.end());
        if (applyTagsAtRow(row, next)) ++changed;
    }
    if (changed > 0) emit flushRequested();
    return changed;
}

bool LibraryModel::applyTagsAtRow(const int row, const QStringList& tags)
{
    if (row < 0 || row >= tracks_.size() || tracks_.at(row).tags == tags) return false;
    const QStringList oldTags = tracks_.at(row).tags;
    tracks_[row].tags = tags;
    const QModelIndex changed = index(row, 0);
    emit dataChanged(changed, changed, {TagsRole});
    emit tagsChanged(tracks_.at(row).trackId, oldTags, tags);
    return true;
}

bool LibraryModel::moveTrack(int fromRow, int toRow)
{
    if (fromRow < 0 || fromRow >= tracks_.size()
        || toRow < 0 || toRow >= tracks_.size() || fromRow == toRow) {
        return false;
    }

    const int destination = toRow > fromRow ? toRow + 1 : toRow;
    beginMoveRows({}, fromRow, fromRow, {}, destination);
    tracks_.move(fromRow, toRow);
    endMoveRows();
    for (int row = 0; row < tracks_.size(); ++row) {
        pathRows_.insert(pathKey(tracks_.at(row).path), row);
        trackRows_.insert(tracks_.at(row).trackId, row);
    }
    emit flushRequested();
    return true;
}

int LibraryModel::reorderTracks(const QStringList& trackIds,
                                const QString& beforeTrackId)
{
    QSet<QString> requested;
    for (const QString& id : trackIds) {
        if (trackRows_.contains(id)) requested.insert(id);
    }
    if (requested.isEmpty()) return 0;

    QList<TrackRecord> selected;
    QList<TrackRecord> remaining;
    selected.reserve(requested.size());
    remaining.reserve(tracks_.size() - requested.size());
    for (const TrackRecord& track : tracks_) {
        (requested.contains(track.trackId) ? selected : remaining).append(track);
    }
    int destination = remaining.size();
    for (int row = 0; row < remaining.size(); ++row) {
        if (remaining.at(row).trackId == beforeTrackId) {
            destination = row;
            break;
        }
    }
    for (int index = 0; index < selected.size(); ++index) {
        remaining.insert(destination + index, selected.at(index));
    }

    beginResetModel();
    tracks_ = std::move(remaining);
    pathRows_.clear();
    trackRows_.clear();
    for (int row = 0; row < tracks_.size(); ++row) {
        pathRows_.insert(pathKey(tracks_.at(row).path), row);
        trackRows_.insert(tracks_.at(row).trackId, row);
    }
    endResetModel();
    emit flushRequested();
    return selected.size();
}

std::optional<MetadataProbeClaim> LibraryModel::beginMetadataProbe(
    const QString& trackId)
{
    if (QThread::currentThread() != thread()) return std::nullopt;
    const int row = indexForTrackId(trackId);
    if (row < 0 || tracks_.at(row).metadataProbeAttempted
        || metadataProbeInFlight_.contains(trackId)) {
        return std::nullopt;
    }
    return beginMetadataRefresh(trackId);
}

std::optional<MetadataProbeClaim> LibraryModel::beginMetadataRefresh(
    const QString& trackId)
{
    if (QThread::currentThread() != thread()) return std::nullopt;
    const int row = indexForTrackId(trackId);
    if (row < 0) return std::nullopt;
    if (nextMetadataProbeGeneration_ == 0) return std::nullopt;
    const quint64 generation = nextMetadataProbeGeneration_;
    nextMetadataProbeGeneration_ = generation
            == std::numeric_limits<quint64>::max()
        ? 0 : generation + 1;
    metadataProbeInFlight_.insert(trackId, generation);
    return MetadataProbeClaim{trackId, tracks_.at(row).path, generation};
}

bool LibraryModel::completeMetadataProbe(const MetadataProbeClaim& claim,
                                         bool succeeded,
                                         const TrackRecord& probed)
{
    if (QThread::currentThread() != thread()) {
        return false;
    }
    const auto inFlight = metadataProbeInFlight_.constFind(claim.trackId);
    if (inFlight == metadataProbeInFlight_.cend()
        || inFlight.value() != claim.generation) {
        return false;
    }
    metadataProbeInFlight_.remove(claim.trackId);
    const int row = indexForTrackId(claim.trackId);
    if (row < 0 || pathKey(tracks_.at(row).path) != pathKey(claim.path)) {
        return false;
    }

    TrackRecord& track = tracks_[row];
    track.metadataProbeAttempted = true;
    QList<int> roles{MetadataProbeAttemptedRole};
    if (succeeded) {
        track.format = probed.format;
        track.sampleRate = probed.sampleRate;
        track.bitDepth = probed.bitDepth;
        track.channels = probed.channels;
        track.bitRate = probed.bitRate;
        track.durationMs = probed.durationMs;
        track.fileSize = probed.fileSize;
        roles.append({FormatRole, SampleRateRole, BitDepthRole, ChannelsRole,
                      BitRateRole, DurationMsRole, FileSizeRole});
    }
    const QModelIndex changed = index(row, 0);
    emit dataChanged(changed, changed, roles);
    return true;
}

bool LibraryModel::completeMediaKindProbe(const MetadataProbeClaim& claim,
                                          bool succeeded,
                                          const TrackRecord& probed)
{
    if (QThread::currentThread() != thread()) {
        return false;
    }
    const auto inFlight = metadataProbeInFlight_.constFind(claim.trackId);
    if (inFlight == metadataProbeInFlight_.cend()
        || inFlight.value() != claim.generation) {
        return false;
    }
    metadataProbeInFlight_.remove(claim.trackId);
    const int row = indexForTrackId(claim.trackId);
    if (row < 0 || pathKey(tracks_.at(row).path) != pathKey(claim.path)) {
        return false;
    }

    TrackRecord& track = tracks_[row];
    track.metadataProbeAttempted = true;
    QList<int> roles{MetadataProbeAttemptedRole};
    if (succeeded
        && (track.hasAudio != probed.hasAudio
            || track.hasVideo != probed.hasVideo)) {
        track.hasAudio = probed.hasAudio;
        track.hasVideo = probed.hasVideo;
        roles.append({HasAudioRole, HasVideoRole});
    }
    const QModelIndex changed = index(row, 0);
    emit dataChanged(changed, changed, roles);
    return true;
}

TrackRecord readLibraryMetadata(const QString& path, const ag_metadata* metadata,
                               const QUrl& previousCover)
{
    TrackRecord track;
    track.path = path;
    track.coverUrl = previousCover;
    track.title = copiedMetadata(ag_metadata_title(metadata));
    track.artist = copiedMetadata(ag_metadata_artist(metadata));
    track.album = copiedMetadata(ag_metadata_album(metadata));
    track.albumArtist = copiedMetadata(ag_metadata_album_artist(metadata));
    track.genre = copiedMetadata(ag_metadata_genre(metadata));
    track.year = copiedMetadata(ag_metadata_year(metadata));
    track.date = copiedMetadata(ag_metadata_date(metadata));
    track.composer = copiedMetadata(ag_metadata_composer(metadata));
    track.format = copiedMetadata(ag_metadata_format(metadata));
    track.sampleRate = ag_metadata_sample_rate(metadata);
    track.bitDepth = ag_metadata_bits_per_sample(metadata);
    track.channels = ag_metadata_channels(metadata);
    track.metadataProbeAttempted = true;
    track.bitRate = ag_metadata_bit_rate(metadata);
    track.durationMs = ag_metadata_duration_ms(metadata);
    track.fileSize = QFileInfo(path).size();
    bool bpmOk = false;
    const double bpm = copiedMetadata(ag_metadata_bpm_tag(metadata)).toDouble(&bpmOk);
    track.bpm = bpmOk && std::isfinite(bpm) ? bpm : 0.0;
    size_t coverSize = 0;
    const char* coverMime = nullptr;
    const unsigned char* cover = ag_metadata_cover(metadata, &coverSize, &coverMime);
    if (cover == nullptr || coverSize == 0) {
        track.coverUrl = {};
    } else {
        const QUrl cached = cacheEmbeddedCover(
            cover, coverSize, copiedMetadata(coverMime));
        if (cached.isValid()) {
            track.coverUrl = cached;
        } else if (track.coverUrl.isValid()) {
            QUrl refreshed = track.coverUrl;
            refreshed.setQuery(
                QStringLiteral("v=%1").arg(QDateTime::currentMSecsSinceEpoch()));
            track.coverUrl = refreshed;
        }
    }
    return track;
}

int LibraryModel::completeMetadataRefreshes(
    const QList<LibraryMetadataRefresh>& updates)
{
    if (QThread::currentThread() != thread()) return 0;
    int changedCount = 0;
    int firstChanged = tracks_.size();
    int lastChanged = -1;
    for (const LibraryMetadataRefresh& update : updates) {
        const MetadataProbeClaim& claim = update.claim;
        const auto inFlight = metadataProbeInFlight_.constFind(claim.trackId);
        if (inFlight == metadataProbeInFlight_.cend()
            || inFlight.value() != claim.generation) continue;
        const int row = indexForTrackId(claim.trackId);
        // Model and claim paths are stored canonical identities. Comparing
        // them must not trigger another filesystem probe on the GUI thread.
        if (row < 0 || normalizedCanonicalKey(tracks_.at(row).path)
                         != normalizedCanonicalKey(claim.path)
            || normalizedCanonicalKey(update.record.path)
                         != normalizedCanonicalKey(claim.path)) continue;
        metadataProbeInFlight_.remove(claim.trackId);
        TrackRecord& track = tracks_[row];
        const TrackRecord& source = update.record;
        track.title = source.title;
        track.artist = source.artist;
        track.album = source.album;
        track.albumArtist = source.albumArtist;
        track.genre = source.genre;
        track.year = source.year;
        track.date = source.date;
        track.composer = source.composer;
        track.format = source.format;
        track.sampleRate = source.sampleRate;
        track.bitDepth = source.bitDepth;
        track.channels = source.channels;
        track.metadataProbeAttempted = true;
        track.bitRate = source.bitRate;
        track.durationMs = source.durationMs;
        track.fileSize = source.fileSize;
        track.bpm = source.bpm;
        track.coverUrl = source.coverUrl;
        firstChanged = qMin(firstChanged, row);
        lastChanged = qMax(lastChanged, row);
        ++changedCount;
    }
    if (changedCount == 0) return 0;
    emit dataChanged(index(firstChanged, 0), index(lastChanged, 0),
                     {TitleRole, ArtistRole, AlbumRole, AlbumArtistRole,
                      GenreRole, YearRole, DateRole, ComposerRole, FormatRole,
                      SampleRateRole, BitDepthRole, ChannelsRole,
                      MetadataProbeAttemptedRole, BitRateRole, DurationMsRole,
                      FileSizeRole, CoverUrlRole, BpmRole});
    emit flushRequested();
    return changedCount;
}

bool LibraryModel::refreshMetadataForPath(const QString& path)
{
    const int row = indexForLocalFile(path);
    if (row < 0) return false;
    ag_metadata* metadata = nullptr;
    const QByteArray utf8 = canonicalLibraryPath(path).toUtf8();
    if (ag_metadata_open(utf8.constData(), &metadata) != AG_OK
        || metadata == nullptr) return false;
    const TrackRecord snapshot = readLibraryMetadata(
        tracks_.at(row).path, metadata, tracks_.at(row).coverUrl);
    ag_metadata_destroy(metadata);
    const auto claim = beginMetadataRefresh(tracks_.at(row).trackId);
    if (!claim) return false;
    completeMetadataRefreshes({{*claim, snapshot}});
    return true;
}

int LibraryModel::refreshMetadataForPaths(const QStringList& paths)
{
    int refreshed = 0;
    for (const QString& path : paths) {
        if (refreshMetadataForPath(path)) ++refreshed;
    }
    return refreshed;
}

bool LibraryModel::applyReplayGainResult(const QString& trackId,
                                         double trackGainDb,
                                         double albumGainDb,
                                         double peak)
{
    const int row = indexForTrackId(trackId);
    if (row < 0 || !std::isfinite(trackGainDb) || !std::isfinite(albumGainDb)
        || !std::isfinite(peak) || peak < 0.0) {
        return false;
    }
    TrackRecord& track = tracks_[row];
    track.replayGainScanned = true;
    track.replayGainTrackDb = trackGainDb;
    track.replayGainAlbumDb = albumGainDb;
    track.replayPeak = peak;
    const QModelIndex changed = index(row, 0);
    emit dataChanged(changed, changed,
                     {ReplayGainScannedRole, ReplayGainTrackDbRole,
                      ReplayGainAlbumDbRole, ReplayPeakRole});
    emit flushRequested();
    return true;
}

bool LibraryModel::updateTrackPath(const QString& trackId, const QString& newPath)
{
    const int row = indexForTrackId(trackId);
    if (row < 0) return false;
    const QString canonical = canonicalLibraryPath(newPath);
    const QString newKey = normalizedCanonicalKey(canonical);
    const QString oldKey = pathKey(tracks_.at(row).path);
    if (newKey != oldKey && pathKeys_.contains(newKey)) return false;
    pathKeys_.remove(oldKey);
    pathRows_.remove(oldKey);
    pathKeys_.insert(newKey);
    pathRows_.insert(newKey, row);
    TrackRecord& track = tracks_[row];
    metadataProbeInFlight_.remove(trackId);
    track.path = canonical;
    track.metadataProbeAttempted = false;
    track.available = QFileInfo::exists(canonical);
    track.fileStatus = track.available ? QStringLiteral("normal")
                                       : QStringLiteral("missing");
    const QModelIndex changed = index(row, 0);
    emit dataChanged(changed, changed,
                     {PathRole, AvailableRole, FileStatusRole,
                      MetadataProbeAttemptedRole});
    emit flushRequested();
    return true;
}

bool LibraryModel::updateTrackPaths(const QHash<QString, QString>& paths)
{
    if (paths.isEmpty()) return true;
    QSet<QString> replacementKeys;
    QSet<int> rows;
    for (auto it = paths.cbegin(); it != paths.cend(); ++it) {
        const int row = indexForTrackId(it.key());
        if (row < 0) return false;
        const QString key = normalizedCanonicalKey(canonicalLibraryPath(it.value()));
        if (replacementKeys.contains(key)) return false;
        replacementKeys.insert(key);
        rows.insert(row);
    }
    for (int row = 0; row < tracks_.size(); ++row) {
        if (!rows.contains(row) && replacementKeys.contains(pathKey(tracks_.at(row).path))) {
            return false;
        }
    }
    for (const int row : rows) {
        const QString oldKey = pathKey(tracks_.at(row).path);
        pathKeys_.remove(oldKey);
        pathRows_.remove(oldKey);
    }
    for (auto it = paths.cbegin(); it != paths.cend(); ++it) {
        const int row = indexForTrackId(it.key());
        const QString canonical = canonicalLibraryPath(it.value());
        const QString key = normalizedCanonicalKey(canonical);
        TrackRecord& track = tracks_[row];
        metadataProbeInFlight_.remove(track.trackId);
        track.path = canonical;
        track.metadataProbeAttempted = false;
        track.available = QFileInfo::exists(canonical);
        track.fileStatus = track.available ? QStringLiteral("normal")
                                          : QStringLiteral("missing");
        pathKeys_.insert(key);
        pathRows_.insert(key, row);
        const QModelIndex changed = index(row, 0);
        emit dataChanged(changed, changed,
                         {PathRole, AvailableRole, FileStatusRole,
                          MetadataProbeAttemptedRole});
    }
    emit flushRequested();
    return true;
}

bool LibraryModel::markPlayed(const QString& trackId, qint64 playedAtMs)
{
    const int row = indexForTrackId(trackId);
    if (row < 0) {
        return false;
    }
    TrackRecord& track = tracks_[row];
    const bool firstPlay = track.playCount == 0;
    ++track.playCount;
    track.lastPlayedAtMs = playedAtMs > 0
        ? playedAtMs : QDateTime::currentMSecsSinceEpoch();
    const QModelIndex changed = index(row, 0);
    emit dataChanged(changed, changed, {PlayCountRole, LastPlayedAtRole});
    if (firstPlay) {
        emit historyCountChanged();
        emit neverPlayedCountChanged();
    }
    return true;
}

bool LibraryModel::removeFromHistory(const QString& trackId)
{
    const int row = indexForTrackId(trackId);
    if (row < 0 || tracks_[row].playCount == 0) {
        return false;
    }
    TrackRecord& track = tracks_[row];
    track.playCount = 0;
    track.lastPlayedAtMs = 0;
    const QModelIndex changed = index(row, 0);
    emit dataChanged(changed, changed, {PlayCountRole, LastPlayedAtRole});
    emit historyCountChanged();
    emit flushRequested();
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

int LibraryModel::favoriteCount() const noexcept
{
    int count = 0;
    for (const TrackRecord& track : tracks_) {
        if (track.favorite) {
            ++count;
        }
    }
    return count;
}

int LibraryModel::historyCount() const noexcept
{
    return static_cast<int>(std::count_if(
        tracks_.cbegin(), tracks_.cend(),
        [](const TrackRecord& track) { return track.playCount > 0; }));
}

int LibraryModel::recentAddedCount() const noexcept
{
    const qint64 cutoff = QDateTime::currentMSecsSinceEpoch()
        - 30LL * 24 * 60 * 60 * 1'000;
    return static_cast<int>(std::count_if(
        tracks_.cbegin(), tracks_.cend(),
        [cutoff](const TrackRecord& track) {
            return track.addedAtMs >= cutoff;
        }));
}

int LibraryModel::neverPlayedCount() const noexcept
{
    return static_cast<int>(std::count_if(
        tracks_.cbegin(), tracks_.cend(),
        [](const TrackRecord& track) { return track.playCount == 0; }));
}
