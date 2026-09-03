#include "lrcapi_lyrics_provider.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRegularExpression>
#include <QTimer>
#include <QUrlQuery>

#include <algorithm>
#include <utility>

namespace {

constexpr qint64 kDefaultRetryAfterMs = 20 * 60 * 1000;
constexpr qint64 kMaximumLyricsBytes = 512 * 1024;

LyricsProvider::Source lrcApiSource()
{
    return {QStringLiteral("lrcapi"), QStringLiteral("LrcAPI"),
            QUrl(QStringLiteral("https://api.lrc.cx/lyrics")),
            QStringLiteral("HisAtri/LrcApi"), true};
}

qint64 retryAfterMs(QNetworkReply* reply)
{
    const QByteArray value = reply->rawHeader("Retry-After").trimmed();
    bool isSeconds = false;
    const qint64 seconds = value.toLongLong(&isSeconds);
    if (isSeconds) return std::max<qint64>(0, seconds) * 1000;
    const QDateTime date = QDateTime::fromString(
        QString::fromLatin1(value), Qt::RFC2822Date).toUTC();
    if (date.isValid()) {
        return std::max<qint64>(0, QDateTime::currentDateTimeUtc().msecsTo(date));
    }
    return kDefaultRetryAfterMs;
}

QUrl requestUrl(const LyricsProvider::Track& track)
{
    QUrl url(QStringLiteral("https://api.lrc.cx/lyrics"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("title"), track.title.trimmed());
    query.addQueryItem(QStringLiteral("artist"), track.artist.trimmed());
    if (!track.album.trimmed().isEmpty()) {
        query.addQueryItem(QStringLiteral("album"), track.album.trimmed());
    }
    url.setQuery(query);
    return url;
}

bool containsTimestamp(const QString& lyrics)
{
    static const QRegularExpression timestamp(
        QStringLiteral(R"(\[(?:\d{1,2}:)?\d{1,2}:\d{1,2}(?:[.:]\d{1,3})?\])"));
    return timestamp.match(lyrics).hasMatch();
}

bool isTextResponse(QNetworkReply* reply)
{
    const QString contentType = reply->header(QNetworkRequest::ContentTypeHeader)
                                    .toString().trimmed().toLower();
    return contentType.isEmpty() || contentType.startsWith(QStringLiteral("text/"));
}

bool looksLikeHtml(const QByteArray& body)
{
    const QByteArray prefix = body.left(512).trimmed().toLower();
    return prefix.startsWith("<!doctype html") || prefix.startsWith("<html")
        || prefix.startsWith("<head") || prefix.startsWith("<body");
}

} // namespace

LrcApiLyricsProvider::LrcApiLyricsProvider(QNetworkAccessManager* manager,
                                           QObject* parent,
                                           const int requestTimeoutMs)
    : LyricsProvider(parent)
    , manager_(manager)
    , requestTimeoutMs_(std::max(0, requestTimeoutMs))
{
}

void LrcApiLyricsProvider::requestExact(const quint64 requestId, const Track& track)
{
    request(requestId, track, true);
}

void LrcApiLyricsProvider::requestSearch(const quint64 requestId, const Track& track)
{
    request(requestId, track, false);
}

void LrcApiLyricsProvider::cancel(const quint64 requestId)
{
    replyBodies_.remove(requestId);
    if (QNetworkReply* reply = replies_.take(requestId)) {
        reply->abort();
        reply->deleteLater();
    }
}

void LrcApiLyricsProvider::request(const quint64 requestId, const Track& track,
                                   const bool exact)
{
    cancel(requestId);
    if (track.title.trimmed().isEmpty()) {
        complete(requestId, Result::notFound());
        return;
    }
    if (manager_ == nullptr) {
        complete(requestId, Result::technicalError());
        return;
    }
    QNetworkRequest networkRequest(requestUrl(track));
    networkRequest.setPriority(track.lowPriority ? QNetworkRequest::LowPriority
                                                  : QNetworkRequest::HighPriority);
    const QString version = QCoreApplication::applicationVersion().isEmpty()
        ? QStringLiteral("dev") : QCoreApplication::applicationVersion();
    networkRequest.setRawHeader(
        "User-Agent", QStringLiteral("AgPlayer/%1").arg(version).toUtf8());
    networkRequest.setRawHeader("Accept", "text/plain");

    QNetworkReply* reply = manager_->get(networkRequest);
    reply->setReadBufferSize(kMaximumLyricsBytes + 1);
    replies_.insert(requestId, reply);
    replyBodies_.insert(requestId, {});
    reply->setProperty("lyricsRequestId", QVariant::fromValue(requestId));
    connect(reply, &QNetworkReply::readyRead, this,
            [this, reply] { consumeReply(reply); });
    connect(reply, &QNetworkReply::metaDataChanged, this,
            [this, reply] { consumeReply(reply); });
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, exact, track] { handleReply(reply, exact, track); });
    QTimer::singleShot(requestTimeoutMs_, this,
                       [this, requestId, reply = QPointer<QNetworkReply>(reply)] {
        if (reply.isNull() || replies_.value(requestId) != reply) return;
        reply->setProperty("lyricsTimedOut", true);
        reply->abort();
    });
}

void LrcApiLyricsProvider::consumeReply(QNetworkReply* reply)
{
    const quint64 requestId = reply->property("lyricsRequestId").toULongLong();
    if (replies_.value(requestId) != reply) return;
    const qint64 declaredSize = reply->header(
        QNetworkRequest::ContentLengthHeader).toLongLong();
    if (!isTextResponse(reply) || declaredSize > kMaximumLyricsBytes) {
        reply->setProperty("lyricsInvalidResponse", true);
        reply->abort();
        return;
    }
    QByteArray& body = replyBodies_[requestId];
    const qint64 remaining = kMaximumLyricsBytes + 1 - body.size();
    if (remaining > 0) body.append(reply->read(remaining));
    if (body.size() > kMaximumLyricsBytes || reply->bytesAvailable() > 0
        || looksLikeHtml(body)) {
        reply->setProperty("lyricsInvalidResponse", true);
        reply->abort();
    }
}

void LrcApiLyricsProvider::handleReply(QNetworkReply* reply, const bool exact,
                                       const Track& track)
{
    const quint64 requestId = reply->property("lyricsRequestId").toULongLong();
    if (replies_.value(requestId) != reply) {
        reply->deleteLater();
        return;
    }
    replies_.remove(requestId);
    QByteArray body = replyBodies_.take(requestId);
    if (reply->property("lyricsTimedOut").toBool()) {
        complete(requestId, Result::technicalError(0, false, QStringLiteral("timeout")));
        reply->deleteLater();
        return;
    }
    if (reply->property("lyricsInvalidResponse").toBool()) {
        complete(requestId, Result::technicalError(
            0, false, QStringLiteral("invalid-response")));
        reply->deleteLater();
        return;
    }

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 400 || status == 404) {
        complete(requestId, Result::notFound());
    } else if (status == 429) {
        complete(requestId, Result::rateLimited(retryAfterMs(reply)));
    } else if (reply->error() != QNetworkReply::NoError) {
        const bool offline = reply->error() == QNetworkReply::HostNotFoundError
            || reply->error() == QNetworkReply::NetworkSessionFailedError;
        complete(requestId, Result::technicalError(
            status, offline, offline ? QStringLiteral("network-unavailable")
                                     : QStringLiteral("network-error")));
    } else if (status < 200 || status >= 300) {
        complete(requestId, Result::technicalError(status));
    } else {
        const qint64 declaredSize = reply->header(
            QNetworkRequest::ContentLengthHeader).toLongLong();
        const qint64 remaining = kMaximumLyricsBytes + 1 - body.size();
        if (remaining > 0) body.append(reply->read(remaining));
        if (!isTextResponse(reply) || declaredSize > kMaximumLyricsBytes
            || body.size() > kMaximumLyricsBytes || reply->bytesAvailable() > 0
            || looksLikeHtml(body)) {
            complete(requestId, Result::technicalError(
                status, false, QStringLiteral("invalid-response")));
        } else if (const QString text = QString::fromUtf8(body).trimmed();
                   text.isEmpty()) {
            complete(requestId, Result::notFound());
        } else {
            Candidate candidate;
            candidate.source = lrcApiSource();
            candidate.title = track.title;
            candidate.artist = track.artist;
            candidate.album = track.album;
            if (containsTimestamp(text)) candidate.syncedLyrics = text;
            else candidate.plainLyrics = text;
            if (exact) complete(requestId, Result::found(std::move(candidate)));
            else complete(requestId, Result::search({std::move(candidate)}));
        }
    }
    reply->deleteLater();
}
