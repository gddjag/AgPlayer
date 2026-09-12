#include "lyrics_ovh_provider.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
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

    void respond(const int status, QByteArray body = {},
                 const QList<QPair<QByteArray, QByteArray>>& headers = {})
    {
        for (const auto& header : headers) setRawHeader(header.first, header.second);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        body_ = std::move(body);
        setFinished(true);
        emit readyRead();
        emit finished();
    }

    void respondError(const QNetworkReply::NetworkError error, const int status = 0)
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
    qint64 readData(char* data, const qint64 maxSize) override
    {
        const qint64 count = std::min(maxSize, body_.size() - offset_);
        if (count <= 0) return -1;
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
    QNetworkReply* createRequest(const Operation operation, const QNetworkRequest& request,
                                 QIODevice* outgoingData) override
    {
        Q_UNUSED(operation);
        Q_UNUSED(outgoingData);
        auto* reply = new FakeNetworkReply(request, this);
        requests.append({request, reply});
        return reply;
    }
};

class LyricsOvhProviderTest final : public QObject {
    Q_OBJECT

private slots:
    void encodesArtistAndTitlePathSegments();
    void exactAndSearchUseSameReadOnlyEndpoint();
    void mapsOnlyNonEmptyPlainTextToCandidates();
    void mapsLocalAndRemoteNoMatchToNotFound();
    void classifiesRateLimitsTimeoutsAndNetworkErrors();
    void rejectsServerAndInvalidResponseShapes();
    void cancellationSuppressesFinishedIncludingLateReplies();
    void repeatedRequestIdSupersedesStaleExactReply();
    void repeatedSearchRequestIdKeepsCancellationIsolated();
};

void LyricsOvhProviderTest::encodesArtistAndTitlePathSegments()
{
    FakeNetworkAccessManager manager;
    LyricsOvhProvider provider(&manager);
    QCoreApplication::setApplicationVersion(QStringLiteral("9.9.9-test"));
    const LyricsProvider::Track track{QStringLiteral("A/B Song"), QStringLiteral("AC DC"), {}, 0, false};

    provider.requestExact(1, track);
    QCOMPARE(manager.requests.size(), 1);
    const QNetworkRequest request = manager.requests.constLast().request;
    QCOMPARE(request.url().scheme(), QStringLiteral("https"));
    QCOMPARE(request.url().host(), QStringLiteral("api.lyrics.ovh"));
    QCOMPARE(request.url().path(QUrl::FullyEncoded), QStringLiteral("/v1/AC%20DC/A%2FB%20Song"));
    QCOMPARE(request.rawHeader("Accept"), QByteArrayLiteral("application/json"));
    QVERIFY(request.rawHeader("User-Agent").contains("AgPlayer/9.9.9-test"));
    QCOMPARE(request.priority(), QNetworkRequest::HighPriority);
}

void LyricsOvhProviderTest::exactAndSearchUseSameReadOnlyEndpoint()
{
    FakeNetworkAccessManager manager;
    LyricsOvhProvider provider(&manager);
    LyricsProvider::Track track{QStringLiteral("100%/Ready"), QStringLiteral("Artist")};

    provider.requestExact(1, track);
    const QUrl exactUrl = manager.requests.constLast().request.url();
    QCOMPARE(exactUrl.path(QUrl::FullyEncoded), QStringLiteral("/v1/Artist/100%25%2FReady"));
    QVERIFY(!exactUrl.toEncoded().contains("%2525"));

    track.lowPriority = true;
    provider.requestSearch(2, track);
    const QNetworkRequest search = manager.requests.constLast().request;
    QCOMPARE(search.url(), exactUrl);
    QCOMPARE(search.priority(), QNetworkRequest::LowPriority);
}

void LyricsOvhProviderTest::mapsOnlyNonEmptyPlainTextToCandidates()
{
    FakeNetworkAccessManager manager;
    LyricsOvhProvider provider(&manager);
    QList<LyricsProvider::Result> results;
    connect(&provider, &LyricsProvider::finished, this,
            [&results](const quint64, const LyricsProvider::Result& result) { results.append(result); });
    const LyricsProvider::Track track{QStringLiteral("Song"), QStringLiteral("Artist")};

    provider.requestExact(1, track);
    manager.requests.constLast().reply->respond(200, QByteArrayLiteral(R"({"lyrics":"first line\nsecond line"})"));
    QCOMPARE(results.constLast().kind, LyricsProvider::Result::Found);
    const LyricsProvider::Candidate exact = results.constLast().candidate;
    QCOMPARE(exact.title, QStringLiteral("Song"));
    QCOMPARE(exact.artist, QStringLiteral("Artist"));
    QCOMPARE(exact.plainLyrics, QStringLiteral("first line\nsecond line"));
    QVERIFY(exact.syncedLyrics.isEmpty());
    QCOMPARE(exact.source.providerId, QStringLiteral("lyrics-ovh"));
    QCOMPARE(exact.source.providerName, QStringLiteral("lyrics.ovh"));
    QCOMPARE(exact.source.sourceUrl, QUrl(QStringLiteral("https://api.lyrics.ovh")));
    QVERIFY(exact.source.attribution.isEmpty());
    QVERIFY(!exact.source.supportsSyncedLyrics);

    provider.requestSearch(2, track);
    manager.requests.constLast().reply->respond(200, QByteArrayLiteral(R"({"lyrics":"plain only"})"));
    QCOMPARE(results.constLast().kind, LyricsProvider::Result::SearchResults);
    QCOMPARE(results.constLast().candidates.size(), 1);
    QVERIFY(results.constLast().candidates.constFirst().syncedLyrics.isEmpty());
    QCOMPARE(results.constLast().candidates.constFirst().plainLyrics, QStringLiteral("plain only"));
}

void LyricsOvhProviderTest::mapsLocalAndRemoteNoMatchToNotFound()
{
    FakeNetworkAccessManager manager;
    LyricsOvhProvider provider(&manager);
    QList<LyricsProvider::Result> results;
    connect(&provider, &LyricsProvider::finished, this,
            [&results](const quint64, const LyricsProvider::Result& result) { results.append(result); });
    const LyricsProvider::Track emptyTitle{QStringLiteral(" \t"), QStringLiteral("Artist")};

    provider.requestExact(1, emptyTitle);
    QCOMPARE(manager.requests.size(), 0);
    QCOMPARE(results.constLast().kind, LyricsProvider::Result::NotFound);

    const LyricsProvider::Track track{QStringLiteral("Song"), QStringLiteral("Artist")};
    provider.requestExact(2, track);
    manager.requests.constLast().reply->respond(400);
    QCOMPARE(results.constLast().kind, LyricsProvider::Result::NotFound);
    provider.requestSearch(3, track);
    manager.requests.constLast().reply->respond(404);
    QCOMPARE(results.constLast().kind, LyricsProvider::Result::NotFound);
    provider.requestExact(4, track);
    manager.requests.constLast().reply->respond(200, QByteArrayLiteral(R"({"lyrics":" \n\t "})"));
    QCOMPARE(results.constLast().kind, LyricsProvider::Result::NotFound);
}

void LyricsOvhProviderTest::classifiesRateLimitsTimeoutsAndNetworkErrors()
{
    FakeNetworkAccessManager manager;
    LyricsOvhProvider provider(&manager);
    QList<LyricsProvider::Result> results;
    connect(&provider, &LyricsProvider::finished, this,
            [&results](const quint64, const LyricsProvider::Result& result) { results.append(result); });
    const LyricsProvider::Track track{QStringLiteral("Song"), QStringLiteral("Artist")};

    provider.requestExact(1, track);
    manager.requests.constLast().reply->respond(429, {}, {{"Retry-After", "7"}});
    QCOMPARE(results.constLast().kind, LyricsProvider::Result::RateLimited);
    QCOMPARE(results.constLast().retryAfterMs, 7000LL);
    provider.requestExact(2, track);
    manager.requests.constLast().reply->respond(429);
    QCOMPARE(results.constLast().retryAfterMs, 20 * 60 * 1000LL);
    provider.requestExact(3, track);
    const QDateTime date = QDateTime::currentDateTimeUtc().addSecs(3);
    manager.requests.constLast().reply->respond(429, {}, {{"Retry-After", date.toString(Qt::RFC2822Date).toLatin1()}});
    QVERIFY(results.constLast().retryAfterMs >= 0);
    QVERIFY(results.constLast().retryAfterMs <= 4000);

    LyricsOvhProvider timeoutProvider(&manager, nullptr, 1);
    connect(&timeoutProvider, &LyricsProvider::finished, this,
            [&results](const quint64, const LyricsProvider::Result& result) { results.append(result); });
    timeoutProvider.requestExact(4, track);
    QTRY_VERIFY(manager.requests.constLast().reply->aborted);
    QCOMPARE(results.constLast().kind, LyricsProvider::Result::TechnicalError);
    QCOMPARE(results.constLast().diagnostic, QStringLiteral("timeout"));

    provider.requestExact(5, track);
    manager.requests.constLast().reply->respondError(QNetworkReply::HostNotFoundError);
    QVERIFY(results.constLast().offline);
    QCOMPARE(results.constLast().diagnostic, QStringLiteral("network-unavailable"));
    provider.requestExact(6, track);
    manager.requests.constLast().reply->respondError(QNetworkReply::SslHandshakeFailedError);
    QVERIFY(!results.constLast().offline);
    QCOMPARE(results.constLast().diagnostic, QStringLiteral("network-error"));
}

void LyricsOvhProviderTest::rejectsServerAndInvalidResponseShapes()
{
    FakeNetworkAccessManager manager;
    LyricsOvhProvider provider(&manager);
    QList<LyricsProvider::Result> results;
    connect(&provider, &LyricsProvider::finished, this,
            [&results](const quint64, const LyricsProvider::Result& result) { results.append(result); });
    const LyricsProvider::Track track{QStringLiteral("Song"), QStringLiteral("Artist")};
    const auto invalid = [&provider, &manager, &results, &track](const quint64 requestId,
                                                                  QByteArray body) {
        provider.requestExact(requestId, track);
        manager.requests.constLast().reply->respond(200, std::move(body));
        QCOMPARE(results.constLast().kind, LyricsProvider::Result::TechnicalError);
        QCOMPARE(results.constLast().diagnostic, QStringLiteral("invalid-response"));
    };

    provider.requestExact(1, track);
    manager.requests.constLast().reply->respond(503);
    QCOMPARE(results.constLast().kind, LyricsProvider::Result::TechnicalError);
    QCOMPARE(results.constLast().httpStatus, 503);
    invalid(2, QByteArrayLiteral("{"));
    invalid(3, QByteArrayLiteral("[]"));
    invalid(4, QByteArrayLiteral(R"({})"));
    invalid(5, QByteArrayLiteral(R"({"lyrics":null})"));
    invalid(6, QByteArrayLiteral(R"({"lyrics":42})"));
}

void LyricsOvhProviderTest::cancellationSuppressesFinishedIncludingLateReplies()
{
    FakeNetworkAccessManager manager;
    LyricsOvhProvider provider(&manager);
    QList<LyricsProvider::Result> results;
    connect(&provider, &LyricsProvider::finished, this,
            [&results](const quint64, const LyricsProvider::Result& result) { results.append(result); });
    const LyricsProvider::Track track{QStringLiteral("Song"), QStringLiteral("Artist")};

    provider.requestExact(1, track);
    FakeNetworkReply* reply = manager.requests.constLast().reply;
    provider.cancel(1);
    QVERIFY(reply->aborted);
    QCOMPARE(results.size(), 0);
    reply->respond(200, QByteArrayLiteral(R"({"lyrics":"late"})"));
    QCOMPARE(results.size(), 0);
}

void LyricsOvhProviderTest::repeatedRequestIdSupersedesStaleExactReply()
{
    FakeNetworkAccessManager manager;
    LyricsOvhProvider provider(&manager, nullptr, 5);
    QList<LyricsProvider::Result> results;
    connect(&provider, &LyricsProvider::finished, this,
            [&results](const quint64, const LyricsProvider::Result& result) { results.append(result); });
    const LyricsProvider::Track track{QStringLiteral("Song"), QStringLiteral("Artist")};

    provider.requestExact(42, track);
    FakeNetworkReply* oldReply = manager.requests.constLast().reply;
    provider.requestExact(42, track);
    FakeNetworkReply* currentReply = manager.requests.constLast().reply;
    QVERIFY(oldReply->aborted);
    oldReply->respond(200, QByteArrayLiteral(R"({"lyrics":"old"})"));
    QCOMPARE(results.size(), 0);
    currentReply->respond(200, QByteArrayLiteral(R"({"lyrics":"new"})"));
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.constLast().candidate.plainLyrics, QStringLiteral("new"));
    QTest::qWait(10);
    QCOMPARE(results.size(), 1);
}

void LyricsOvhProviderTest::repeatedSearchRequestIdKeepsCancellationIsolated()
{
    FakeNetworkAccessManager manager;
    LyricsOvhProvider provider(&manager);
    QList<LyricsProvider::Result> results;
    connect(&provider, &LyricsProvider::finished, this,
            [&results](const quint64, const LyricsProvider::Result& result) { results.append(result); });
    const LyricsProvider::Track track{QStringLiteral("Song"), QStringLiteral("Artist")};

    provider.requestSearch(73, track);
    FakeNetworkReply* oldReply = manager.requests.constLast().reply;
    provider.requestSearch(73, track);
    FakeNetworkReply* cancelledReply = manager.requests.constLast().reply;
    QVERIFY(oldReply->aborted);
    provider.cancel(73);
    QVERIFY(cancelledReply->aborted);
    oldReply->respond(200, QByteArrayLiteral(R"({"lyrics":"old"})"));
    cancelledReply->respond(200, QByteArrayLiteral(R"({"lyrics":"cancelled"})"));
    QCOMPARE(results.size(), 0);
}

QTEST_MAIN(LyricsOvhProviderTest)
#include "lyrics_ovh_provider_test.moc"
