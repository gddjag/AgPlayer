#include "unison_lyrics_provider.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>
#include <QtTest>

#include <algorithm>
#include <cstring>
#include <utility>

class FakeNetworkReply final : public QNetworkReply {
public:
    explicit FakeNetworkReply(const QNetworkRequest& request, QObject* parent)
        : QNetworkReply(parent)
    {
        setRequest(request);
        setUrl(request.url());
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    }

    void respond(int status, QByteArray body = {},
                 const QList<QPair<QByteArray, QByteArray>>& headers = {})
    {
        for (const auto& header : headers) {
            setRawHeader(header.first, header.second);
        }
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        body_ = std::move(body);
        setFinished(true);
        emit readyRead();
        emit finished();
    }

    void respondError(QNetworkReply::NetworkError error, int status = 0)
    {
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        setError(error, QStringLiteral("network failure"));
        setFinished(true);
        emit finished();
    }

    void abort() override
    {
        aborted = true;
        setError(QNetworkReply::OperationCanceledError, QStringLiteral("aborted"));
        setFinished(true);
        emit finished();
    }

    bool aborted = false;

protected:
    qint64 readData(char* data, qint64 maxSize) override
    {
        const qint64 remaining = body_.size() - offset_;
        const qint64 count = std::min(maxSize, remaining);
        if (count <= 0) {
            return -1;
        }
        std::memcpy(data, body_.constData() + offset_, static_cast<size_t>(count));
        offset_ += count;
        return count;
    }

private:
    QByteArray body_;
    qint64 offset_ = 0;
};

class FakeNetworkAccessManager final : public QNetworkAccessManager {
public:
    struct Request final {
        QNetworkRequest request;
        FakeNetworkReply* reply = nullptr;
    };

    QList<Request> requests;

protected:
    QNetworkReply* createRequest(Operation operation, const QNetworkRequest& request,
                                 QIODevice* outgoingData) override
    {
        Q_UNUSED(operation);
        Q_UNUSED(outgoingData);
        auto* reply = new FakeNetworkReply(request, this);
        requests.append({request, reply});
        return reply;
    }
};

class UnisonLyricsProviderTest final : public QObject {
    Q_OBJECT

private slots:
    void requestsUseOnlyOfficialReadOnlyRoutesAndMetadata();
    void parsesLrcAndPlainCandidatesWithoutResponseDuration();
    void exactTtmlIsANormalNoMatch();
    void searchSkipsTtmlAndKeepsSupportedCandidates();
    void treatsExact404AndEmptySearchAsNormalNoMatchOutcomes();
    void parsesRetryAfterOrUsesTheTwentyMinuteFallback();
    void classifiesTimeoutAndNetworkFailures();
    void rejectsServerAndInvalidResponseShapes();
    void cancellationSuppressesFinishedIncludingLateReplies();
    void repeatedExactRequestIdSupersedesOldReply();
    void repeatedSearchRequestIdKeepsCancellationIsolated();
};

void UnisonLyricsProviderTest::requestsUseOnlyOfficialReadOnlyRoutesAndMetadata()
{
    FakeNetworkAccessManager manager;
    UnisonLyricsProvider provider(&manager);
    QCoreApplication::setApplicationVersion(QStringLiteral("9.9.9-test"));
    const LyricsProvider::Track track{QStringLiteral("Song"), QStringLiteral("Artist"),
                                       QStringLiteral("Album"), 180234, false};

    provider.requestExact(1, track);
    QCOMPARE(manager.requests.size(), 1);
    const QNetworkRequest exact = manager.requests.constLast().request;
    QCOMPARE(exact.url().scheme(), QStringLiteral("https"));
    QCOMPARE(exact.url().host(), QStringLiteral("unison.boidu.dev"));
    QCOMPARE(exact.url().path(), QStringLiteral("/lyrics"));
    const QUrlQuery exactQuery(exact.url());
    QCOMPARE(exactQuery.queryItemValue(QStringLiteral("song")), QStringLiteral("Song"));
    QCOMPARE(exactQuery.queryItemValue(QStringLiteral("artist")), QStringLiteral("Artist"));
    QCOMPARE(exactQuery.queryItemValue(QStringLiteral("album")), QStringLiteral("Album"));
    QCOMPARE(exactQuery.queryItemValue(QStringLiteral("duration")), QStringLiteral("180"));
    QVERIFY(!exactQuery.hasQueryItem(QStringLiteral("q")));
    QCOMPARE(exact.rawHeader("Accept"), QByteArrayLiteral("application/json"));
    QVERIFY(exact.rawHeader("User-Agent").contains("AgPlayer/9.9.9-test"));
    QCOMPARE(exact.priority(), QNetworkRequest::HighPriority);

    LyricsProvider::Track searchTrack = track;
    searchTrack.lowPriority = true;
    provider.requestSearch(2, searchTrack);
    const QNetworkRequest search = manager.requests.constLast().request;
    QCOMPARE(search.url().path(), QStringLiteral("/lyrics/search"));
    const QUrlQuery searchQuery(search.url());
    QCOMPARE(searchQuery.queryItemValue(QStringLiteral("song")), QStringLiteral("Song"));
    QCOMPARE(searchQuery.queryItemValue(QStringLiteral("artist")), QStringLiteral("Artist"));
    QCOMPARE(searchQuery.queryItemValue(QStringLiteral("album")), QStringLiteral("Album"));
    QCOMPARE(searchQuery.queryItemValue(QStringLiteral("duration")), QStringLiteral("180"));
    QVERIFY(!searchQuery.hasQueryItem(QStringLiteral("q")));
    QCOMPARE(search.priority(), QNetworkRequest::LowPriority);
}

void UnisonLyricsProviderTest::parsesLrcAndPlainCandidatesWithoutResponseDuration()
{
    FakeNetworkAccessManager manager;
    UnisonLyricsProvider provider(&manager);
    QList<LyricsProvider::Result> results;
    connect(&provider, &LyricsProvider::finished, this,
            [&results](quint64, const LyricsProvider::Result& result) { results.append(result); });
    const LyricsProvider::Track track{QStringLiteral("Song"), QStringLiteral("Artist")};

    provider.requestExact(1, track);
    manager.requests.constLast().reply->respond(200, QByteArrayLiteral(
        R"({"success":true,"data":{"id":7,"song":"Song","artist":"Artist","album":"Album","lyrics":"[00:01.00]Line","format":"lrc","syncType":"linesync"}})"));
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.constLast().kind, LyricsProvider::Result::Found);
    const LyricsProvider::Candidate lrc = results.constLast().candidate;
    QCOMPARE(lrc.title, QStringLiteral("Song"));
    QCOMPARE(lrc.artist, QStringLiteral("Artist"));
    QCOMPARE(lrc.album, QStringLiteral("Album"));
    QCOMPARE(lrc.durationSeconds, 0LL);
    QCOMPARE(lrc.syncedLyrics, QStringLiteral("[00:01.00]Line"));
    QVERIFY(lrc.plainLyrics.isEmpty());
    QCOMPARE(lrc.source.providerId, QStringLiteral("unison"));
    QCOMPARE(lrc.source.providerName, QStringLiteral("Unison"));
    QCOMPARE(lrc.source.sourceUrl, QUrl(QStringLiteral("https://unison.boidu.dev")));
    QCOMPARE(lrc.source.attribution,
             QStringLiteral("Lyrics from Unison (https://unison.boidu.dev)"));
    QVERIFY(lrc.source.supportsSyncedLyrics);

    provider.requestSearch(2, track);
    manager.requests.constLast().reply->respond(200, QByteArrayLiteral(
        R"({"success":true,"data":[{"id":8,"song":"Song","artist":"Artist","lyrics":"plain line","format":"plain","syncType":"unsynced"}]})"));
    QCOMPARE(results.size(), 2);
    QCOMPARE(results.constLast().kind, LyricsProvider::Result::SearchResults);
    QCOMPARE(results.constLast().candidates.size(), 1);
    const LyricsProvider::Candidate plain = results.constLast().candidates.constFirst();
    QVERIFY(plain.syncedLyrics.isEmpty());
    QCOMPARE(plain.plainLyrics, QStringLiteral("plain line"));
    QCOMPARE(plain.durationSeconds, 0LL);
    QCOMPARE(plain.source.attribution,
             QStringLiteral("Lyrics from Unison (https://unison.boidu.dev)"));
}

void UnisonLyricsProviderTest::exactTtmlIsANormalNoMatch()
{
    FakeNetworkAccessManager manager;
    UnisonLyricsProvider provider(&manager);
    QList<LyricsProvider::Result> results;
    connect(&provider, &LyricsProvider::finished, this,
            [&results](quint64, const LyricsProvider::Result& result) { results.append(result); });
    const LyricsProvider::Track track{QStringLiteral("Song"), QStringLiteral("Artist")};

    provider.requestExact(1, track);
    manager.requests.constLast().reply->respond(200, QByteArrayLiteral(
        R"({"success":true,"data":{"id":7,"song":"Song","artist":"Artist","lyrics":"<tt>Line</tt>","format":"ttml","syncType":"linesync"}})"));

    QCOMPARE(results.size(), 1);
    QCOMPARE(results.constLast().kind, LyricsProvider::Result::NotFound);
}

void UnisonLyricsProviderTest::searchSkipsTtmlAndKeepsSupportedCandidates()
{
    FakeNetworkAccessManager manager;
    UnisonLyricsProvider provider(&manager);
    QList<LyricsProvider::Result> results;
    connect(&provider, &LyricsProvider::finished, this,
            [&results](quint64, const LyricsProvider::Result& result) { results.append(result); });
    const LyricsProvider::Track track{QStringLiteral("Song"), QStringLiteral("Artist")};

    provider.requestSearch(1, track);
    manager.requests.constLast().reply->respond(200, QByteArrayLiteral(
        R"({"success":true,"data":[{"id":7,"song":"Song","artist":"Artist","lyrics":"<tt>Line</tt>","format":"ttml","syncType":"linesync"},{"id":8,"song":"Song","artist":"Artist","lyrics":"plain line","format":"plain","syncType":"plain"}]})"));

    QCOMPARE(results.size(), 1);
    QCOMPARE(results.constLast().kind, LyricsProvider::Result::SearchResults);
    QCOMPARE(results.constLast().candidates.size(), 1);
    QCOMPARE(results.constLast().candidates.constFirst().plainLyrics,
             QStringLiteral("plain line"));
}

void UnisonLyricsProviderTest::treatsExact404AndEmptySearchAsNormalNoMatchOutcomes()
{
    FakeNetworkAccessManager manager;
    UnisonLyricsProvider provider(&manager);
    QList<LyricsProvider::Result> results;
    connect(&provider, &LyricsProvider::finished, this,
            [&results](quint64, const LyricsProvider::Result& result) { results.append(result); });
    const LyricsProvider::Track track{QStringLiteral("Song"), QStringLiteral("Artist")};

    provider.requestExact(1, track);
    manager.requests.constLast().reply->respond(404);
    QCOMPARE(results.constLast().kind, LyricsProvider::Result::NotFound);

    provider.requestSearch(2, track);
    manager.requests.constLast().reply->respond(200,
        QByteArrayLiteral(R"({"success":true,"data":[]})"));
    QCOMPARE(results.constLast().kind, LyricsProvider::Result::SearchResults);
    QVERIFY(results.constLast().candidates.isEmpty());
}

void UnisonLyricsProviderTest::parsesRetryAfterOrUsesTheTwentyMinuteFallback()
{
    FakeNetworkAccessManager manager;
    UnisonLyricsProvider provider(&manager);
    QList<LyricsProvider::Result> results;
    connect(&provider, &LyricsProvider::finished, this,
            [&results](quint64, const LyricsProvider::Result& result) { results.append(result); });
    const LyricsProvider::Track track{QStringLiteral("Song"), QStringLiteral("Artist")};

    provider.requestExact(1, track);
    manager.requests.constLast().reply->respond(429, {}, {{"Retry-After", "7"}});
    QCOMPARE(results.constLast().kind, LyricsProvider::Result::RateLimited);
    QCOMPARE(results.constLast().retryAfterMs, 7000LL);

    provider.requestExact(2, track);
    const QDateTime date = QDateTime::currentDateTimeUtc().addSecs(3);
    manager.requests.constLast().reply->respond(
        429, {}, {{"Retry-After", date.toString(Qt::RFC2822Date).toLatin1()}});
    QCOMPARE(results.constLast().kind, LyricsProvider::Result::RateLimited);
    QVERIFY(results.constLast().retryAfterMs >= 0);
    QVERIFY(results.constLast().retryAfterMs <= 4000);

    provider.requestExact(3, track);
    manager.requests.constLast().reply->respond(429);
    QCOMPARE(results.constLast().kind, LyricsProvider::Result::RateLimited);
    QCOMPARE(results.constLast().retryAfterMs, 20 * 60 * 1000LL);
}

void UnisonLyricsProviderTest::classifiesTimeoutAndNetworkFailures()
{
    FakeNetworkAccessManager manager;
    UnisonLyricsProvider timeoutProvider(&manager, nullptr, 1);
    QList<LyricsProvider::Result> results;
    connect(&timeoutProvider, &LyricsProvider::finished, this,
            [&results](quint64, const LyricsProvider::Result& result) { results.append(result); });
    const LyricsProvider::Track track{QStringLiteral("Song"), QStringLiteral("Artist")};

    timeoutProvider.requestExact(1, track);
    QTRY_VERIFY(manager.requests.constLast().reply->aborted);
    QCOMPARE(results.constLast().kind, LyricsProvider::Result::TechnicalError);
    QCOMPARE(results.constLast().diagnostic, QStringLiteral("timeout"));

    UnisonLyricsProvider provider(&manager);
    connect(&provider, &LyricsProvider::finished, this,
            [&results](quint64, const LyricsProvider::Result& result) { results.append(result); });
    provider.requestExact(2, track);
    manager.requests.constLast().reply->respondError(QNetworkReply::HostNotFoundError);
    QCOMPARE(results.constLast().kind, LyricsProvider::Result::TechnicalError);
    QVERIFY(results.constLast().offline);
    QCOMPARE(results.constLast().diagnostic, QStringLiteral("network-unavailable"));

    provider.requestExact(3, track);
    manager.requests.constLast().reply->respondError(QNetworkReply::SslHandshakeFailedError);
    QCOMPARE(results.constLast().kind, LyricsProvider::Result::TechnicalError);
    QVERIFY(!results.constLast().offline);
    QCOMPARE(results.constLast().diagnostic, QStringLiteral("network-error"));

    provider.requestExact(4, track);
    manager.requests.constLast().reply->respondError(QNetworkReply::NetworkSessionFailedError);
    QCOMPARE(results.constLast().kind, LyricsProvider::Result::TechnicalError);
    QVERIFY(results.constLast().offline);
    QCOMPARE(results.constLast().diagnostic, QStringLiteral("network-unavailable"));

    provider.requestExact(5, track);
    manager.requests.constLast().reply->respondError(QNetworkReply::UnknownNetworkError);
    QCOMPARE(results.constLast().kind, LyricsProvider::Result::TechnicalError);
    QVERIFY(!results.constLast().offline);
    QCOMPARE(results.constLast().diagnostic, QStringLiteral("network-error"));
}

void UnisonLyricsProviderTest::rejectsServerAndInvalidResponseShapes()
{
    FakeNetworkAccessManager manager;
    UnisonLyricsProvider provider(&manager);
    QList<LyricsProvider::Result> results;
    connect(&provider, &LyricsProvider::finished, this,
            [&results](quint64, const LyricsProvider::Result& result) { results.append(result); });
    const LyricsProvider::Track track{QStringLiteral("Song"), QStringLiteral("Artist")};
    const auto invalid = [&provider, &manager, &results, &track](quint64 requestId,
                                                                   QByteArray payload) {
        provider.requestExact(requestId, track);
        manager.requests.constLast().reply->respond(200, std::move(payload));
        QCOMPARE(results.constLast().kind, LyricsProvider::Result::TechnicalError);
        QCOMPARE(results.constLast().diagnostic, QStringLiteral("invalid-response"));
    };

    provider.requestExact(1, track);
    manager.requests.constLast().reply->respond(503);
    QCOMPARE(results.constLast().kind, LyricsProvider::Result::TechnicalError);
    QCOMPARE(results.constLast().httpStatus, 503);

    invalid(2, QByteArrayLiteral("{"));
    invalid(3, QByteArrayLiteral(R"({"success":false,"data":{}})"));
    invalid(4, QByteArrayLiteral(R"({"success":true,"data":[]})"));
    invalid(5, QByteArrayLiteral(
        R"({"success":true,"data":{"id":7,"song":"Song","artist":"Artist","lyrics":"x","format":"lrc"}})"));
    invalid(6, QByteArrayLiteral(
        R"({"success":true,"data":{"id":7,"song":"Song","artist":"Artist","lyrics":"x","format":"unknown","syncType":"linesync"}})"));

    provider.requestSearch(8, track);
    manager.requests.constLast().reply->respond(200,
        QByteArrayLiteral(R"({"success":true,"data":{}})"));
    QCOMPARE(results.constLast().kind, LyricsProvider::Result::TechnicalError);
    QCOMPARE(results.constLast().diagnostic, QStringLiteral("invalid-response"));
}

void UnisonLyricsProviderTest::cancellationSuppressesFinishedIncludingLateReplies()
{
    FakeNetworkAccessManager manager;
    UnisonLyricsProvider provider(&manager);
    QList<LyricsProvider::Result> results;
    connect(&provider, &LyricsProvider::finished, this,
            [&results](quint64, const LyricsProvider::Result& result) { results.append(result); });
    const LyricsProvider::Track track{QStringLiteral("Song"), QStringLiteral("Artist")};

    provider.requestExact(1, track);
    FakeNetworkReply* reply = manager.requests.constLast().reply;
    provider.cancel(1);
    QVERIFY(reply->aborted);
    QCOMPARE(results.size(), 0);
    reply->respond(200, QByteArrayLiteral(
        R"({"success":true,"data":{"id":7,"song":"Song","artist":"Artist","lyrics":"x","format":"plain","syncType":"unsynced"}})"));
    QCOMPARE(results.size(), 0);
}

void UnisonLyricsProviderTest::repeatedExactRequestIdSupersedesOldReply()
{
    FakeNetworkAccessManager manager;
    UnisonLyricsProvider provider(&manager, nullptr, 5);
    QList<quint64> requestIds;
    QList<LyricsProvider::Result> results;
    connect(&provider, &LyricsProvider::finished, this,
            [&requestIds, &results](quint64 requestId, const LyricsProvider::Result& result) {
                requestIds.append(requestId);
                results.append(result);
            });
    const LyricsProvider::Track track{QStringLiteral("Song"), QStringLiteral("Artist")};

    provider.requestExact(42, track);
    FakeNetworkReply* oldReply = manager.requests.constLast().reply;
    provider.requestExact(42, track);
    FakeNetworkReply* currentReply = manager.requests.constLast().reply;

    QVERIFY(oldReply->aborted);
    QCOMPARE(results.size(), 0);
    oldReply->respond(200, QByteArrayLiteral(
        R"({"success":true,"data":{"id":6,"song":"Old","artist":"Artist","lyrics":"old","format":"plain","syncType":"unsynced"}})"));
    QCOMPARE(results.size(), 0);

    currentReply->respond(200, QByteArrayLiteral(
        R"({"success":true,"data":{"id":7,"song":"Song","artist":"Artist","lyrics":"new","format":"plain","syncType":"unsynced"}})"));
    QCOMPARE(requestIds, QList<quint64>{42});
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.constFirst().kind, LyricsProvider::Result::Found);
    QCOMPARE(results.constFirst().candidate.plainLyrics, QStringLiteral("new"));

    QTest::qWait(10);
    QCOMPARE(results.size(), 1);
}

void UnisonLyricsProviderTest::repeatedSearchRequestIdKeepsCancellationIsolated()
{
    FakeNetworkAccessManager manager;
    UnisonLyricsProvider provider(&manager);
    QList<quint64> requestIds;
    QList<LyricsProvider::Result> results;
    connect(&provider, &LyricsProvider::finished, this,
            [&requestIds, &results](quint64 requestId, const LyricsProvider::Result& result) {
                requestIds.append(requestId);
                results.append(result);
            });
    const LyricsProvider::Track track{QStringLiteral("Song"), QStringLiteral("Artist")};

    provider.requestSearch(73, track);
    FakeNetworkReply* oldReply = manager.requests.constLast().reply;
    provider.requestSearch(73, track);
    FakeNetworkReply* cancelledReply = manager.requests.constLast().reply;

    QVERIFY(oldReply->aborted);
    provider.cancel(73);
    QVERIFY(cancelledReply->aborted);
    oldReply->respond(200, QByteArrayLiteral(R"({"success":true,"data":[]})"));
    cancelledReply->respond(200, QByteArrayLiteral(R"({"success":true,"data":[]})"));
    QCOMPARE(results.size(), 0);

    provider.requestSearch(73, track);
    manager.requests.constLast().reply->respond(200, QByteArrayLiteral(
        R"({"success":true,"data":[{"id":8,"song":"Song","artist":"Artist","lyrics":"plain line","format":"plain","syncType":"unsynced"}]})"));
    QCOMPARE(requestIds, QList<quint64>{73});
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.constFirst().kind, LyricsProvider::Result::SearchResults);
    QCOMPARE(results.constFirst().candidates.size(), 1);
}

QTEST_MAIN(UnisonLyricsProviderTest)
#include "unison_lyrics_provider_test.moc"
