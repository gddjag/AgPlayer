#include "lyrics_cache.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <cmath>

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

bool isLegacySource(const QString& source)
{
    return source == QStringLiteral("manual") || source == QStringLiteral("lrclib");
}

LyricsProvider::Source legacySource(const QString& source)
{
    if (source == QStringLiteral("manual")) {
        return {QStringLiteral("manual"), QStringLiteral("Manual"), {}, {}, true};
    }
    return {QStringLiteral("lrclib"), QStringLiteral("LRCLIB"),
            QUrl(QStringLiteral("https://lrclib.net")), {}, true};
}

bool isLocalSource(const QString& providerId)
{
    return providerId == QStringLiteral("manual")
        || providerId == QStringLiteral("embedded")
        || providerId == QStringLiteral("sidecar")
        || providerId == QStringLiteral("local");
}

bool isValidSource(const LyricsProvider::Source& source)
{
    if (source.providerId.trimmed().isEmpty() || source.providerName.trimmed().isEmpty()) {
        return false;
    }
    if (source.sourceUrl.isEmpty()) return isLocalSource(source.providerId);
    const QString scheme = source.sourceUrl.scheme().toCaseFolded();
    return source.sourceUrl.isValid() && !source.sourceUrl.isRelative()
        && (scheme == QStringLiteral("https") || scheme == QStringLiteral("http"))
        && !source.sourceUrl.host().isEmpty();
}

bool isInteger(const QJsonValue& value)
{
    if (!value.isDouble()) return false;
    const double number = value.toDouble();
    return std::isfinite(number) && std::floor(number) == number;
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
    const QJsonValue version = object.value(QStringLiteral("version"));
    const QJsonValue key = object.value(QStringLiteral("key"));
    const QJsonValue instrumental = object.value(QStringLiteral("instrumental"));
    const QJsonValue offsetMs = object.value(QStringLiteral("offsetMs"));
    const QJsonValue untimedText = object.value(QStringLiteral("untimedText"));
    const QJsonValue lines = object.value(QStringLiteral("lines"));
    const QJsonValue metadata = object.value(QStringLiteral("metadata"));
    if (!isInteger(version)
        || !key.isString() || key.toString() != keyFor(track)
        || !instrumental.isBool() || !isInteger(offsetMs)
        || !untimedText.isString() || !lines.isArray() || !metadata.isObject()) {
        return std::nullopt;
    }

    Entry entry;
    entry.instrumental = instrumental.toBool();
    entry.document.offsetMs = offsetMs.toInteger();
    entry.document.untimedText = untimedText.toString();
    for (const QJsonValue value : lines.toArray()) {
        if (!value.isObject()) return std::nullopt;
        const QJsonObject line = value.toObject();
        const QJsonValue timeMs = line.value(QStringLiteral("timeMs"));
        const QJsonValue text = line.value(QStringLiteral("text"));
        if (!isInteger(timeMs) || !text.isString()) {
            return std::nullopt;
        }
        entry.document.lines.append({timeMs.toInteger(), text.toString()});
    }
    const QJsonObject metadataObject = metadata.toObject();
    for (auto it = metadataObject.begin(); it != metadataObject.end(); ++it) {
        if (!it.value().isString()) return std::nullopt;
        entry.document.metadata.insert(it.key(), it.value().toString());
    }
    if (!entry.instrumental && entry.document.lines.isEmpty()
        && entry.document.untimedText.isEmpty()) return std::nullopt;

    if (version.toInteger() == 1) {
        const QJsonValue source = object.value(QStringLiteral("source"));
        if (!source.isString() || !isLegacySource(source.toString())) return std::nullopt;
        entry.source = legacySource(source.toString());
        entry.synchronized = !entry.document.lines.isEmpty();
        return entry;
    }
    if (version.toInteger() != 2) return std::nullopt;

    const QJsonValue providerId = object.value(QStringLiteral("providerId"));
    const QJsonValue providerName = object.value(QStringLiteral("providerName"));
    const QJsonValue sourceUrl = object.value(QStringLiteral("sourceUrl"));
    const QJsonValue attribution = object.value(QStringLiteral("attribution"));
    const QJsonValue supportsSyncedLyrics = object.value(QStringLiteral("supportsSyncedLyrics"));
    const QJsonValue synchronized = object.value(QStringLiteral("synchronized"));
    if (!providerId.isString() || !providerName.isString() || !sourceUrl.isString()
        || !attribution.isString() || !supportsSyncedLyrics.isBool()
        || !synchronized.isBool()) {
        return std::nullopt;
    }
    const QUrl parsedUrl(sourceUrl.toString(), QUrl::StrictMode);
    entry.source = {providerId.toString(), providerName.toString(), parsedUrl,
                    attribution.toString(), supportsSyncedLyrics.toBool()};
    entry.synchronized = synchronized.toBool();
    if (!isValidSource(entry.source)
        || entry.synchronized != !entry.document.lines.isEmpty()) {
        return std::nullopt;
    }
    return entry;
}

bool LyricsCache::save(const TrackRecord& track, const Entry& entry) const
{
    if (!isValidSource(entry.source)
        || entry.synchronized != !entry.document.lines.isEmpty()
        || (!entry.instrumental && entry.document.lines.isEmpty()
            && entry.document.untimedText.isEmpty())) {
        return false;
    }
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
    const QJsonObject object{{QStringLiteral("version"), 2},
                             {QStringLiteral("key"), keyFor(track)},
                             {QStringLiteral("providerId"), entry.source.providerId},
                             {QStringLiteral("providerName"), entry.source.providerName},
                             {QStringLiteral("sourceUrl"), entry.source.sourceUrl.toString()},
                             {QStringLiteral("attribution"), entry.source.attribution},
                             {QStringLiteral("supportsSyncedLyrics"),
                              entry.source.supportsSyncedLyrics},
                             {QStringLiteral("synchronized"), entry.synchronized},
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
