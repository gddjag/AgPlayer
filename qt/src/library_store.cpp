#include "library_store.hpp"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace {
QJsonObject toJson(const TrackRecord& track)
{
    return {{QStringLiteral("trackId"), track.trackId},
            {QStringLiteral("path"), track.path},
            {QStringLiteral("title"), track.title},
            {QStringLiteral("artist"), track.artist},
            {QStringLiteral("album"), track.album},
            {QStringLiteral("albumArtist"), track.albumArtist},
            {QStringLiteral("genre"), track.genre},
            {QStringLiteral("year"), track.year},
            {QStringLiteral("date"), track.date},
            {QStringLiteral("composer"), track.composer},
            {QStringLiteral("format"), track.format},
            {QStringLiteral("sampleRate"), track.sampleRate},
            {QStringLiteral("bitDepth"), track.bitDepth},
            {QStringLiteral("channels"), track.channels},
            {QStringLiteral("hasAudio"), track.hasAudio},
            {QStringLiteral("hasVideo"), track.hasVideo},
            {QStringLiteral("metadataProbeAttempted"),
             track.metadataProbeAttempted},
            {QStringLiteral("bitRate"), QJsonValue(track.bitRate)},
            {QStringLiteral("durationMs"), QJsonValue(track.durationMs)},
            {QStringLiteral("fileSize"), QJsonValue(track.fileSize)},
            {QStringLiteral("coverUrl"), track.coverUrl.toString()},
            {QStringLiteral("favorite"), track.favorite},
            {QStringLiteral("rating"), track.rating},
            {QStringLiteral("bpm"), track.bpm},
            {QStringLiteral("available"), track.available},
            {QStringLiteral("importError"), track.importError},
            {QStringLiteral("lyrics"), track.lyrics},
            {QStringLiteral("playCount"), track.playCount},
            {QStringLiteral("lastPlayedAtMs"), QJsonValue(track.lastPlayedAtMs)},
            {QStringLiteral("tags"), QJsonArray::fromStringList(track.tags)},
            {QStringLiteral("addedAtMs"), QJsonValue(track.addedAtMs)},
            {QStringLiteral("fileStatus"), track.fileStatus},
            {QStringLiteral("contentHash"), track.contentHash},
            {QStringLiteral("audioFingerprint"),
             QString::fromLatin1(track.audioFingerprint.toBase64())},
            {QStringLiteral("replayGainScanned"), track.replayGainScanned},
            {QStringLiteral("replayGainTrackDb"), track.replayGainTrackDb},
            {QStringLiteral("replayGainAlbumDb"), track.replayGainAlbumDb},
            {QStringLiteral("replayPeak"), track.replayPeak}};
}

TrackRecord fromJson(const QJsonObject& object)
{
    TrackRecord track;
    track.trackId = object.value(QStringLiteral("trackId")).toString();
    track.path = object.value(QStringLiteral("path")).toString();
    track.title = object.value(QStringLiteral("title")).toString();
    track.artist = object.value(QStringLiteral("artist")).toString();
    track.album = object.value(QStringLiteral("album")).toString();
    track.albumArtist = object.value(QStringLiteral("albumArtist")).toString();
    track.genre = object.value(QStringLiteral("genre")).toString();
    track.year = object.value(QStringLiteral("year")).toString();
    track.date = object.value(QStringLiteral("date")).toString();
    track.composer = object.value(QStringLiteral("composer")).toString();
    track.format = object.value(QStringLiteral("format")).toString();
    track.sampleRate = object.value(QStringLiteral("sampleRate")).toInt();
    track.bitDepth = object.value(QStringLiteral("bitDepth")).toInt();
    track.channels = object.value(QStringLiteral("channels")).toInt();
    track.hasAudio = object.value(QStringLiteral("hasAudio")).toBool();
    track.hasVideo = object.value(QStringLiteral("hasVideo")).toBool();
    track.metadataProbeAttempted = object.value(
        QStringLiteral("metadataProbeAttempted")).toBool();
    track.bitRate = object.value(QStringLiteral("bitRate")).toInteger();
    track.durationMs = object.value(QStringLiteral("durationMs")).toInteger();
    track.fileSize = object.value(QStringLiteral("fileSize")).toInteger();
    track.coverUrl = QUrl(object.value(QStringLiteral("coverUrl")).toString());
    track.favorite = object.value(QStringLiteral("favorite")).toBool();
    track.rating = object.value(QStringLiteral("rating")).toInt();
    track.bpm = object.value(QStringLiteral("bpm")).toDouble();
    track.available = QFileInfo(track.path).isFile();
    track.importError = object.value(QStringLiteral("importError")).toString();
    track.lyrics = object.value(QStringLiteral("lyrics")).toString();
    track.playCount = object.value(QStringLiteral("playCount")).toInt();
    track.lastPlayedAtMs =
        object.value(QStringLiteral("lastPlayedAtMs")).toInteger();
    const QJsonArray tags = object.value(QStringLiteral("tags")).toArray();
    for (const QJsonValue& tag : tags) {
        if (tag.isString()) {
            track.tags.append(tag.toString());
        }
    }
    track.addedAtMs = object.value(QStringLiteral("addedAtMs")).toInteger();
    if (track.addedAtMs <= 0) {
        const QFileInfo fileInfo(track.path);
        const QDateTime timestamp = fileInfo.birthTime().isValid()
            ? fileInfo.birthTime() : fileInfo.lastModified();
        if (timestamp.isValid()) {
            track.addedAtMs = timestamp.toMSecsSinceEpoch();
        }
    }
    track.fileStatus = object.value(QStringLiteral("fileStatus"))
                           .toString(QStringLiteral("normal"));
    track.contentHash = object.value(QStringLiteral("contentHash")).toString();
    track.audioFingerprint = QByteArray::fromBase64(
        object.value(QStringLiteral("audioFingerprint")).toString().toLatin1());
    track.replayGainScanned =
        object.value(QStringLiteral("replayGainScanned")).toBool();
    track.replayGainTrackDb =
        object.value(QStringLiteral("replayGainTrackDb")).toDouble();
    track.replayGainAlbumDb =
        object.value(QStringLiteral("replayGainAlbumDb")).toDouble();
    track.replayPeak = object.value(QStringLiteral("replayPeak")).toDouble();
    return track;
}
}

LibraryStore::LibraryStore(QString filePath, QObject* parent)
    : QObject(parent), filePath_(std::move(filePath))
{
    saveTimer_.setSingleShot(true);
    saveTimer_.setInterval(250);
    connect(&saveTimer_, &QTimer::timeout, this, [this] { flush(); });
}

LibraryStore::~LibraryStore()
{
    if (hasPendingSave_) {
        saveTimer_.stop();
        save(pendingTracks_);
    }
}

bool LibraryStore::save(const QList<TrackRecord>& tracks) const
{
    QJsonArray array;
    for (const TrackRecord& track : tracks) {
        array.append(toJson(track));
    }
    const QByteArray bytes = QJsonDocument(array).toJson(QJsonDocument::Compact);
    QSaveFile file(filePath_);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    if (file.write(bytes) != bytes.size()) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

QList<TrackRecord> LibraryStore::load() const
{
    QFile file(filePath_);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isArray()) {
        return {};
    }

    QList<TrackRecord> tracks;
    const QJsonArray array = document.array();
    tracks.reserve(array.size());
    for (const QJsonValue& value : array) {
        if (value.isObject()) {
            tracks.append(fromJson(value.toObject()));
        }
    }
    return tracks;
}

void LibraryStore::requestSave(QList<TrackRecord> tracks)
{
    pendingTracks_ = std::move(tracks);
    hasPendingSave_ = true;
    saveTimer_.start();
}

bool LibraryStore::flush()
{
    if (!hasPendingSave_) {
        return true;
    }
    saveTimer_.stop();
    const bool success = save(pendingTracks_);
    if (success) {
        hasPendingSave_ = false;
    }
    emit saveFinished(success);
    return success;
}
