#include "lyrics_provider.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>

namespace {

bool isStringOrNull(const QJsonObject& object, const QString& key)
{
    if (!object.contains(key)) return false;
    const QJsonValue value = object.value(key);
    return value.isString() || value.isNull();
}

bool parseCandidate(const QJsonObject& object, LyricsProvider::Candidate* candidate)
{
    const QJsonValue trackName = object.value(QStringLiteral("trackName"));
    const QJsonValue artistName = object.value(QStringLiteral("artistName"));
    const QJsonValue instrumental = object.value(QStringLiteral("instrumental"));
    if (!trackName.isString() || !artistName.isString() || !instrumental.isBool()
        || !isStringOrNull(object, QStringLiteral("syncedLyrics"))
        || !isStringOrNull(object, QStringLiteral("plainLyrics"))) {
        return false;
    }
    const QJsonValue albumName = object.value(QStringLiteral("albumName"));
    const QJsonValue duration = object.value(QStringLiteral("duration"));
    if ((!albumName.isUndefined() && !albumName.isString() && !albumName.isNull())
        || (!duration.isUndefined() && !duration.isDouble() && !duration.isNull())) {
        return false;
    }
    candidate->source = {QStringLiteral("lrclib"), QStringLiteral("LRCLIB"),
                         QUrl(QStringLiteral("https://lrclib.net")), {}, true};
    candidate->title = trackName.toString();
    candidate->artist = artistName.toString();
    candidate->album = albumName.toString();
    candidate->durationSeconds = duration.toVariant().toLongLong();
    candidate->syncedLyrics = object.value(QStringLiteral("syncedLyrics")).toString();
    candidate->plainLyrics = object.value(QStringLiteral("plainLyrics")).toString();
    candidate->instrumental = instrumental.toBool();
    return true;
}

} // namespace

LyricsProvider::Result LyricsProvider::Result::found(Candidate candidate)
{
    Result result; result.kind = Found; result.candidate = std::move(candidate); return result;
}
LyricsProvider::Result LyricsProvider::Result::notFound() { return {}; }
LyricsProvider::Result LyricsProvider::Result::search(QList<Candidate> candidates)
{
    Result result; result.kind = SearchResults; result.candidates = std::move(candidates); return result;
}
LyricsProvider::Result LyricsProvider::Result::technicalError(const int httpStatus,
                                                               const bool offline,
                                                               QString diagnostic)
{
    Result result; result.kind = TechnicalError; result.httpStatus = httpStatus;
    result.offline = offline; result.diagnostic = std::move(diagnostic); return result;
}
LyricsProvider::Result LyricsProvider::Result::rateLimited(const qint64 retryAfterMs)
{
    Result result; result.kind = RateLimited; result.retryAfterMs = retryAfterMs; return result;
}

LrclibProvider::LrclibProvider(QNetworkAccessManager* manager, QObject* parent,
                               const int requestTimeoutMs)
    : LyricsProvider(parent), manager_(manager), requestTimeoutMs_(std::max(0, requestTimeoutMs)) {}

void LrclibProvider::requestExact(const quint64 requestId, const Track& track)
{
    request(requestId, track, true);
}

void LrclibProvider::requestSearch(const quint64 requestId, const Track& track)
{
    request(requestId, track, false);
}

void LrclibProvider::cancel(const quint64 requestId)
{
    if (QNetworkReply* reply = replies_.take(requestId)) {
        reply->abort();
        reply->deleteLater();
    }
}

void LrclibProvider::request(const quint64 requestId, const Track& track, const bool exact)
{
    if (manager_ == nullptr) {
        complete(requestId, Result::technicalError());
        return;
    }
    QUrl url(QStringLiteral("https://lrclib.net/")
             + (exact ? QStringLiteral("api/get") : QStringLiteral("api/search")));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("track_name"), track.title);
    query.addQueryItem(QStringLiteral("artist_name"), track.artist);
    if (!track.album.isEmpty()) query.addQueryItem(QStringLiteral("album_name"), track.album);
    if (exact && track.durationMs > 0) {
        query.addQueryItem(QStringLiteral("duration"), QString::number(track.durationMs / 1000));
    }
    url.setQuery(query);
    QNetworkRequest request(url);
    request.setPriority(track.lowPriority ? QNetworkRequest::LowPriority
                                          : QNetworkRequest::HighPriority);
    const QString version = QCoreApplication::applicationVersion().isEmpty()
        ? QStringLiteral("dev") : QCoreApplication::applicationVersion();
    request.setRawHeader("User-Agent", QStringLiteral("AgPlayer/%1").arg(version).toUtf8());
    request.setRawHeader("Accept", "application/json");
    QNetworkReply* reply = manager_->get(request);
    replies_.insert(requestId, reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, exact] { handleReply(reply, exact); });
    reply->setProperty("lyricsRequestId", QVariant::fromValue(requestId));
    QTimer::singleShot(requestTimeoutMs_, this, [this, requestId, reply = QPointer<QNetworkReply>(reply)] {
        if (reply.isNull() || replies_.value(requestId) != reply) return;
        reply->setProperty("lyricsTimedOut", true);
        reply->abort();
    });
}

void LrclibProvider::handleReply(QNetworkReply* reply, const bool exact)
{
    const quint64 requestId = reply->property("lyricsRequestId").toULongLong();
    if (replies_.take(requestId) != reply) { reply->deleteLater(); return; }
    if (reply->property("lyricsTimedOut").toBool()) {
        complete(requestId, Result::technicalError(0, false, QStringLiteral("timeout")));
        reply->deleteLater();
        return;
    }
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError && status != 404 && status != 429) {
        const bool offline = reply->error() == QNetworkReply::HostNotFoundError
            || reply->error() == QNetworkReply::NetworkSessionFailedError;
        complete(requestId, Result::technicalError(status, offline,
            offline ? QStringLiteral("network-unavailable") : QStringLiteral("network-error")));
        reply->deleteLater();
        return;
    }
    if (status == 404) { complete(requestId, Result::notFound()); reply->deleteLater(); return; }
    if (status == 429) {
        bool ok = false;
        const QByteArray retryAfter = reply->rawHeader("Retry-After").trimmed();
        const qint64 seconds = retryAfter.toLongLong(&ok);
        qint64 delayMs = 20 * 60 * 1000;
        if (ok) {
            delayMs = std::max<qint64>(0, seconds) * 1000;
        } else {
            const QDateTime date = QDateTime::fromString(
                QString::fromLatin1(retryAfter), Qt::RFC2822Date).toUTC();
            if (date.isValid()) {
                delayMs = std::max<qint64>(0, QDateTime::currentDateTimeUtc().msecsTo(date));
            }
        }
        complete(requestId, Result::rateLimited(delayMs));
        reply->deleteLater();
        return;
    }
    if (status < 200 || status >= 300) {
        complete(requestId, Result::technicalError(status)); reply->deleteLater(); return;
    }
    const QJsonDocument document = QJsonDocument::fromJson(reply->readAll());
    if (exact && document.isObject()) {
        Candidate candidate;
        if (parseCandidate(document.object(), &candidate)) {
            complete(requestId, Result::found(std::move(candidate)));
        } else {
            complete(requestId, Result::technicalError(
                status, false, QStringLiteral("invalid-response")));
        }
    } else if (!exact && document.isArray()) {
        QList<Candidate> candidates;
        bool valid = true;
        for (const QJsonValue value : document.array()) {
            Candidate candidate;
            if (!value.isObject() || !parseCandidate(value.toObject(), &candidate)) {
                valid = false;
                break;
            }
            candidates.append(std::move(candidate));
        }
        if (valid) {
            complete(requestId, Result::search(std::move(candidates)));
        } else {
            complete(requestId, Result::technicalError(
                status, false, QStringLiteral("invalid-response")));
        }
    } else {
        complete(requestId, Result::technicalError(status, false, QStringLiteral("invalid-response")));
    }
    reply->deleteLater();
}
