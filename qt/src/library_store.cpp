#include "library_store.hpp"

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
            {QStringLiteral("format"), track.format},
            {QStringLiteral("sampleRate"), track.sampleRate},
            {QStringLiteral("bitDepth"), track.bitDepth},
            {QStringLiteral("bitRate"), static_cast<double>(track.bitRate)},
            {QStringLiteral("durationMs"), static_cast<double>(track.durationMs)},
            {QStringLiteral("fileSize"), static_cast<double>(track.fileSize)},
            {QStringLiteral("coverUrl"), track.coverUrl.toString()},
            {QStringLiteral("favorite"), track.favorite},
            {QStringLiteral("available"), track.available},
            {QStringLiteral("importError"), track.importError}};
}

TrackRecord fromJson(const QJsonObject& object)
{
    TrackRecord track;
    track.trackId = object.value(QStringLiteral("trackId")).toString();
    track.path = object.value(QStringLiteral("path")).toString();
    track.title = object.value(QStringLiteral("title")).toString();
    track.artist = object.value(QStringLiteral("artist")).toString();
    track.album = object.value(QStringLiteral("album")).toString();
    track.format = object.value(QStringLiteral("format")).toString();
    track.sampleRate = object.value(QStringLiteral("sampleRate")).toInt();
    track.bitDepth = object.value(QStringLiteral("bitDepth")).toInt();
    track.bitRate = static_cast<qint64>(object.value(QStringLiteral("bitRate")).toDouble());
    track.durationMs =
        static_cast<qint64>(object.value(QStringLiteral("durationMs")).toDouble());
    track.fileSize = static_cast<qint64>(object.value(QStringLiteral("fileSize")).toDouble());
    track.coverUrl = QUrl(object.value(QStringLiteral("coverUrl")).toString());
    track.favorite = object.value(QStringLiteral("favorite")).toBool();
    track.available = QFileInfo(track.path).isFile();
    track.importError = object.value(QStringLiteral("importError")).toString();
    return track;
}
}

LibraryStore::LibraryStore(QString filePath, QObject* parent)
    : QObject(parent), filePath_(std::move(filePath))
{
    saveTimer_.setSingleShot(true);
    saveTimer_.setInterval(250);
    connect(&saveTimer_, &QTimer::timeout, this, [this] {
        emit saveFinished(save(pendingTracks_));
    });
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
    saveTimer_.start();
}
