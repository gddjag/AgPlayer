#include "unison_lyrics_provider.hpp"

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
#include <QUrlQuery>

#include <algorithm>
#include <utility>

namespace {

constexpr qint64 kDefaultRetryAfterMs = 20 * 60 * 1000;

LyricsProvider::Source unisonSource()
{
    return {QStringLiteral("unison"), QStringLiteral("Unison"),
            QUrl(QStringLiteral("https://unison.boidu.dev")),
            QStringLiteral("Lyrics from Unison (https://unison.boidu.dev)"), true};
}

bool optionalString(const QJsonObject& object, const QString& key)
{
    const QJsonValue value = object.value(key);
    return value.isUndefined() || value.isString() || value.isNull();
}

bool optionalNumber(const QJsonObject& object, const QString& key)
{
    const QJsonValue value = object.value(key);
    return value.isUndefined() || value.isDouble() || value.isNull();
}

enum class CandidateParseStatus { Parsed, UnsupportedFormat, Invalid };

CandidateParseStatus parseCandidate(const QJsonObject& object,
                                    LyricsProvider::Candidate* candidate)
{
    const QJsonValue id = object.value(QStringLiteral("id"));
    const QJsonValue song = object.value(QStringLiteral("song"));
    const QJsonValue artist = object.value(QStringLiteral("artist"));
    const QJsonValue lyrics = object.value(QStringLiteral("lyrics"));
    const QJsonValue format = object.value(QStringLiteral("format"));
    const QJsonValue syncType = object.value(QStringLiteral("syncType"));
    if (!id.isDouble() || !song.isString() || song.toString().isEmpty()
        || !artist.isString() || artist.toString().isEmpty()
        || !lyrics.isString() || lyrics.toString().isEmpty()
        || !format.isString() || !syncType.isString() || syncType.toString().isEmpty()
        || !optionalString(object, QStringLiteral("album"))
        || !optionalNumber(object, QStringLiteral("duration"))) {
        return CandidateParseStatus::Invalid;
    }

    candidate->source = unisonSource();
    candidate->title = song.toString();
    candidate->artist = artist.toString();
    candidate->album = object.value(QStringLiteral("album")).toString();
    candidate->durationSeconds = object.value(QStringLiteral("duration")).toVariant().toLongLong();
    if (format.toString() == QStringLiteral("lrc")) {
        candidate->syncedLyrics = lyrics.toString();
        candidate->plainLyrics.clear();
    } else if (format.toString() == QStringLiteral("plain")) {
        candidate->syncedLyrics.clear();
        candidate->plainLyrics = lyrics.toString();
    } else if (format.toString() == QStringLiteral("ttml")) {
        return CandidateParseStatus::UnsupportedFormat;
    } else {
        return CandidateParseStatus::Invalid;
    }
    return CandidateParseStatus::Parsed;
}

qint64 retryAfterMs(QNetworkReply* reply)
{
    const QByteArray value = reply->rawHeader("Retry-After").trimmed();
    bool isSeconds = false;
    const qint64 seconds = value.toLongLong(&isSeconds);
    if (isSeconds) {
        return std::max<qint64>(0, seconds) * 1000;
    }
    const QDateTime date = QDateTime::fromString(
        QString::fromLatin1(value), Qt::RFC2822Date).toUTC();
    if (date.isValid()) {
        return std::max<qint64>(0, QDateTime::currentDateTimeUtc().msecsTo(date));
    }
    return kDefaultRetryAfterMs;
}

} // namespace

UnisonLyricsProvider::UnisonLyricsProvider(QNetworkAccessManager* manager, QObject* parent,
                                           const int requestTimeoutMs)
    : LyricsProvider(parent), manager_(manager), requestTimeoutMs_(std::max(0, requestTimeoutMs))
{
}

void UnisonLyricsProvider::requestExact(const quint64 requestId, const Track& track)
{
    request(requestId, track, true);
}

void UnisonLyricsProvider::requestSearch(const quint64 requestId, const Track& track)
{
    request(requestId, track, false);
}

void UnisonLyricsProvider::cancel(const quint64 requestId)
{
    if (QNetworkReply* reply = replies_.take(requestId)) {
        reply->abort();
        reply->deleteLater();
    }
}

void UnisonLyricsProvider::request(const quint64 requestId, const Track& track, const bool exact)
{
    if (manager_ == nullptr) {
        complete(requestId, Result::technicalError());
        return;
    }

    cancel(requestId);

    QUrl url(QStringLiteral("https://unison.boidu.dev")
             + (exact ? QStringLiteral("/lyrics") : QStringLiteral("/lyrics/search")));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("song"), track.title);
    query.addQueryItem(QStringLiteral("artist"), track.artist);
    if (!track.album.isEmpty()) {
        query.addQueryItem(QStringLiteral("album"), track.album);
    }
    if (track.durationMs > 0) {
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
    reply->setProperty("lyricsRequestId", QVariant::fromValue(requestId));
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, exact] { handleReply(reply, exact); });
    QTimer::singleShot(requestTimeoutMs_, this, [this, requestId, reply = QPointer<QNetworkReply>(reply)] {
        if (reply.isNull() || replies_.value(requestId) != reply) {
            return;
        }
        reply->setProperty("lyricsTimedOut", true);
        reply->abort();
    });
}

void UnisonLyricsProvider::handleReply(QNetworkReply* reply, const bool exact)
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
    if (reply->error() != QNetworkReply::NoError && status != 404 && status != 429) {
        const bool offline = reply->error() == QNetworkReply::HostNotFoundError
            || reply->error() == QNetworkReply::NetworkSessionFailedError;
        complete(requestId, Result::technicalError(status, offline,
            offline ? QStringLiteral("network-unavailable") : QStringLiteral("network-error")));
        reply->deleteLater();
        return;
    }
    if (status == 404) {
        complete(requestId, Result::notFound());
        reply->deleteLater();
        return;
    }
    if (status == 429) {
        complete(requestId, Result::rateLimited(retryAfterMs(reply)));
        reply->deleteLater();
        return;
    }
    if (status < 200 || status >= 300) {
        complete(requestId, Result::technicalError(status));
        reply->deleteLater();
        return;
    }

    const QJsonDocument document = QJsonDocument::fromJson(reply->readAll());
    if (!document.isObject()) {
        complete(requestId, Result::technicalError(status, false, QStringLiteral("invalid-response")));
        reply->deleteLater();
        return;
    }
    const QJsonObject root = document.object();
    if (!root.value(QStringLiteral("success")).isBool()
        || !root.value(QStringLiteral("success")).toBool()) {
        complete(requestId, Result::technicalError(status, false, QStringLiteral("invalid-response")));
        reply->deleteLater();
        return;
    }

    if (exact && root.value(QStringLiteral("data")).isObject()) {
        Candidate candidate;
        const CandidateParseStatus parseStatus =
            parseCandidate(root.value(QStringLiteral("data")).toObject(), &candidate);
        if (parseStatus == CandidateParseStatus::Parsed) {
            complete(requestId, Result::found(std::move(candidate)));
        } else if (parseStatus == CandidateParseStatus::UnsupportedFormat) {
            complete(requestId, Result::notFound());
        } else {
            complete(requestId, Result::technicalError(status, false, QStringLiteral("invalid-response")));
        }
    } else if (!exact && root.value(QStringLiteral("data")).isArray()) {
        QList<Candidate> candidates;
        bool valid = true;
        for (const QJsonValue& value : root.value(QStringLiteral("data")).toArray()) {
            Candidate candidate;
            if (!value.isObject()) {
                valid = false;
                break;
            }
            const CandidateParseStatus parseStatus = parseCandidate(value.toObject(), &candidate);
            if (parseStatus == CandidateParseStatus::UnsupportedFormat) continue;
            if (parseStatus == CandidateParseStatus::Invalid) {
                valid = false;
                break;
            }
            candidates.append(std::move(candidate));
        }
        if (valid) {
            complete(requestId, Result::search(std::move(candidates)));
        } else {
            complete(requestId, Result::technicalError(status, false, QStringLiteral("invalid-response")));
        }
    } else {
        complete(requestId, Result::technicalError(status, false, QStringLiteral("invalid-response")));
    }
    reply->deleteLater();
}
