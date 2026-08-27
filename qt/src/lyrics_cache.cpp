#include "lyrics_cache.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace {

QString normalizedPath(const QString& path)
{
    QFileInfo info(path);
    QString normalized = info.exists() ? info.canonicalFilePath()
                                       : QDir::cleanPath(path);
    normalized = QDir::fromNativeSeparators(normalized);
#ifdef Q_OS_WIN
    normalized = normalized.toCaseFolded();
#endif
    return normalized;
}

} // namespace

LyricsCache::LyricsCache(QString cacheDirectory)
    : cacheDirectory_(std::move(cacheDirectory)) {}

void LyricsCache::setCacheDirectory(QString cacheDirectory)
{
    cacheDirectory_ = std::move(cacheDirectory);
}

QString LyricsCache::keyFor(const TrackRecord& track)
{
    const QFileInfo info(track.path);
    const qint64 size = info.exists() ? info.size() : track.fileSize;
    const qint64 modified = info.exists() ? info.lastModified().toMSecsSinceEpoch() : 0;
    const QByteArray material = normalizedPath(track.path).toUtf8() + '\0'
        + QByteArray::number(size) + '\0' + QByteArray::number(modified) + '\0'
        + QByteArray::number(track.durationMs);
    return QString::fromLatin1(QCryptographicHash::hash(
        material, QCryptographicHash::Sha256).toHex());
}

QString LyricsCache::pathFor(const TrackRecord& track) const
{
    return QDir(cacheDirectory_).filePath(
        QStringLiteral("lyrics/") + keyFor(track) + QStringLiteral(".json"));
}

std::optional<LyricsCache::Entry> LyricsCache::load(const TrackRecord& track) const
{
    QFile file(pathFor(track));
    if (!file.open(QIODevice::ReadOnly)) return std::nullopt;
    const QJsonDocument json = QJsonDocument::fromJson(file.readAll());
    if (!json.isObject()) return std::nullopt;
    const QJsonObject object = json.object();
    if (object.value(QStringLiteral("key")).toString() != keyFor(track)) return std::nullopt;

    Entry entry;
    entry.source = object.value(QStringLiteral("source")).toString();
    entry.instrumental = object.value(QStringLiteral("instrumental")).toBool();
    entry.document.offsetMs = object.value(QStringLiteral("offsetMs")).toVariant().toLongLong();
    entry.document.untimedText = object.value(QStringLiteral("untimedText")).toString();
    for (const QJsonValue value : object.value(QStringLiteral("lines")).toArray()) {
        const QJsonObject line = value.toObject();
        if (!line.contains(QStringLiteral("timeMs")) || !line.contains(QStringLiteral("text"))) {
            return std::nullopt;
        }
        entry.document.lines.append({line.value(QStringLiteral("timeMs")).toVariant().toLongLong(),
                                     line.value(QStringLiteral("text")).toString()});
    }
    const QJsonObject metadata = object.value(QStringLiteral("metadata")).toObject();
    for (auto it = metadata.begin(); it != metadata.end(); ++it) {
        entry.document.metadata.insert(it.key(), it.value().toString());
    }
    return entry;
}

bool LyricsCache::save(const TrackRecord& track, const Entry& entry) const
{
    const QString path = pathFor(track);
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QJsonArray lines;
    for (const LyricsLine& line : entry.document.lines) {
        lines.append(QJsonObject{{QStringLiteral("timeMs"), QJsonValue(line.timeMs)},
                                 {QStringLiteral("text"), line.text}});
    }
    QJsonObject metadata;
    for (auto it = entry.document.metadata.cbegin(); it != entry.document.metadata.cend(); ++it) {
        metadata.insert(it.key(), it.value());
    }
    const QJsonObject object{{QStringLiteral("version"), 1},
                             {QStringLiteral("key"), keyFor(track)},
                             {QStringLiteral("source"), entry.source},
                             {QStringLiteral("instrumental"), entry.instrumental},
                             {QStringLiteral("offsetMs"), QJsonValue(entry.document.offsetMs)},
                             {QStringLiteral("untimedText"), entry.document.untimedText},
                             {QStringLiteral("lines"), lines},
                             {QStringLiteral("metadata"), metadata}};
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    return file.write(QJsonDocument(object).toJson(QJsonDocument::Compact)) >= 0
        && file.commit();
}
