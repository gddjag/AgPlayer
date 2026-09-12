#include "lyrics_ovh_provider.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <utility>

namespace {

constexpr qint64 kDefaultRetryAfterMs = 20 * 60 * 1000;

LyricsProvider::Source lyricsOvhSource()
{
    return {QStringLiteral("lyrics-ovh"), QStringLiteral("lyrics.ovh"),
            QUrl(QStringLiteral("https://api.lyrics.ovh")), {}, false};
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
    const QByteArray artist = QUrl::toPercentEncoding(track.artist, {}, "/");
    const QByteArray title = QUrl::toPercentEncoding(track.title, {}, "/");
    return QUrl::fromEncoded("https://api.lyrics.ovh/v1/" + artist + '/' + title);
}

} // namespace

LyricsOvhProvider::LyricsOvhProvider(QNetworkAccessManager* manager, QObject* parent,
                                     const int requestTimeoutMs)
    : LyricsProvider(parent), manager_(manager), requestTimeoutMs_(std::max(0, requestTimeoutMs))
{
}

void LyricsOvhProvider::requestExact(const quint64 requestId, const Track& track)
{
    request(requestId, track, true);
}

void LyricsOvhProvider::requestSearch(const quint64 requestId, const Track& track)
{
    request(requestId, track, false);
}

void LyricsOvhProvider::cancel(const quint64 requestId)
{
    if (QNetworkReply* reply = replies_.take(requestId)) {
        reply->abort();
        reply->deleteLater();
    }
}

void LyricsOvhProvider::request(const quint64 requestId, const Track& track, const bool exact)
{
    cancel(requestId);
    if (track.artist.trimmed().isEmpty() || track.title.trimmed().isEmpty()) {
        complete(requestId, Result::notFound());
        return;
    }
    if (manager_ == nullptr) {
        complete(requestId, Result::technicalError());
        return;
    }

    QNetworkRequest request(requestUrl(track));
    request.setPriority(track.lowPriority ? QNetworkRequest::LowPriority
                                          : QNetworkRequest::HighPriority);
    const QString version = QCoreApplication::applicationVersion().isEmpty()
        ? QStringLiteral("dev") : QCoreApplication::applicationVersion();
    request.setRawHeader("User-Agent", QStringLiteral("AgPlayer/%1").arg(version).toUtf8());
    request.setRawHeader("Accept", "application/json");

    QNetworkReply* reply = manager_->get(request);
    replies_.insert(requestId, reply);
    reply->setProperty("lyricsRequestId", QVariant::fromValue(requestId));
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, exact, track] { handleReply(reply, exact, track); });
    QTimer::singleShot(requestTimeoutMs_, this,
                       [this, requestId, reply = QPointer<QNetworkReply>(reply)] {
        if (reply.isNull() || replies_.value(requestId) != reply) return;
        reply->setProperty("lyricsTimedOut", true);
        reply->abort();
    });
}

void LyricsOvhProvider::handleReply(QNetworkReply* reply, const bool exact, const Track& track)
{
    const quint64 requestId = reply->property("lyricsRequestId").toULongLong();
    if (replies_.value(requestId) != reply) {
        reply->deleteLater();
        return;
    }
    replies_.remove(requestId);
    if (reply->property("lyricsTimedOut").toBool()) {
        complete(requestId, Result::technicalError(0, false, QStringLiteral("timeout")));
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
        complete(requestId, Result::technicalError(status, offline,
            offline ? QStringLiteral("network-unavailable") : QStringLiteral("network-error")));
    } else if (status < 200 || status >= 300) {
        complete(requestId, Result::technicalError(status));
    } else {
        const QJsonDocument document = QJsonDocument::fromJson(reply->readAll());
        const QJsonValue lyrics = document.isObject()
            ? document.object().value(QStringLiteral("lyrics")) : QJsonValue();
        if (!lyrics.isString()) {
            complete(requestId,
                     Result::technicalError(status, false, QStringLiteral("invalid-response")));
        } else if (lyrics.toString().trimmed().isEmpty()) {
            complete(requestId, Result::notFound());
        } else {
            Candidate candidate;
            candidate.source = lyricsOvhSource();
            candidate.title = track.title;
            candidate.artist = track.artist;
            candidate.plainLyrics = lyrics.toString();
            if (exact) {
                complete(requestId, Result::found(std::move(candidate)));
            } else {
                complete(requestId, Result::search({std::move(candidate)}));
            }
        }
    }
    reply->deleteLater();
}
