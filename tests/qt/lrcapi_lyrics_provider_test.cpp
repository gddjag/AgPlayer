#include "lrcapi_lyrics_provider.hpp"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>
#include <QtTest>

#include <algorithm>
#include <cstring>
#include <utility>

class LrcApiFakeReply final : public QNetworkReply {
public:
    explicit LrcApiFakeReply(const QNetworkRequest& request, QObject* parent)
        : QNetworkReply(parent)
    {
        setRequest(request);
        setUrl(request.url());
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    }

    void respond(const int status, QByteArray body = {},
                 const QByteArray& contentType = QByteArrayLiteral("application/json"),
                 const bool includeContentLength = true)
    {
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        setHeader(QNetworkRequest::ContentTypeHeader, contentType);
        if (includeContentLength)
            setHeader(QNetworkRequest::ContentLengthHeader, body.size());
        body_ = std::move(body);
        setFinished(true);
        emit readyRead();
        emit finished();
    }

    void abort() override
    {
        setError(QNetworkReply::OperationCanceledError, QStringLiteral("aborted"));
        setFinished(true);
        emit finished();
    }

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

class LrcApiFakeManager final : public QNetworkAccessManager {
public:
    QList<QPair<QNetworkRequest, LrcApiFakeReply*>> requests;

protected:
    QNetworkReply* createRequest(const Operation, const QNetworkRequest& request,
                                 QIODevice*) override
    {
        auto* reply = new LrcApiFakeReply(request, this);
        requests.push_back({request, reply});
        return reply;
    }
};

class LrcApiLyricsProviderTest final : public QObject {
    Q_OBJECT

private slots:
    void sendsEncodedMetadataToPublicReadOnlyEndpoint();
    void mapsTimedAndPlainResponses();
    void mapsNoMatchAndCancellation();
    void rejectsNonTextHtmlAndOversizedResponses();
    void refreshRequestsBypassCachesAndHaveUniqueQueries();
    void timeoutAndRateLimitRemainObservable();
};

void LrcApiLyricsProviderTest::sendsEncodedMetadataToPublicReadOnlyEndpoint()
{
    LrcApiFakeManager manager;
    LrcApiLyricsProvider provider(&manager);
    const LyricsProvider::Track track{QStringLiteral("A/B Song"),
                                      QStringLiteral("AC DC"),
                                      QStringLiteral("Album + One")};
    provider.requestExact(1, track);
    QCOMPARE(manager.requests.size(), 1);
    const QNetworkRequest request = manager.requests.constFirst().first;
    QCOMPARE(request.url().scheme(), QStringLiteral("https"));
    QCOMPARE(request.url().host(), QStringLiteral("api.lrc.cx"));
    QCOMPARE(request.url().path(), QStringLiteral("/jsonapi"));
    QCOMPARE(QUrlQuery(request.url()).queryItemValue(QStringLiteral("title")), track.title);
    QCOMPARE(QUrlQuery(request.url()).queryItemValue(QStringLiteral("artist")), track.artist);
    QCOMPARE(QUrlQuery(request.url()).queryItemValue(QStringLiteral("album")), track.album);
    QCOMPARE(request.rawHeader("Accept"), QByteArrayLiteral("application/json"));
}

void LrcApiLyricsProviderTest::mapsTimedAndPlainResponses()
{
    LrcApiFakeManager manager;
    LrcApiLyricsProvider provider(&manager);
    QList<LyricsProvider::Result> results;
    connect(&provider, &LyricsProvider::finished, this,
            [&results](quint64, const LyricsProvider::Result& result) {
                results.push_back(result);
            });
    const LyricsProvider::Track track{QStringLiteral("Song"), QStringLiteral("Artist")};

    provider.requestExact(1, track);
    manager.requests.constLast().second->respond(200, QByteArrayLiteral(
        R"([{"title":"Real title","artist":"Real artist","album":"Actual album","duration":180.4,"lrc":"[00:01.20]line"}])"));
    QCOMPARE(results.constLast().kind, LyricsProvider::Result::SearchResults);
    const auto candidate = results.constLast().candidates.constFirst();
    QCOMPARE(candidate.title, QStringLiteral("Real title"));
    QCOMPARE(candidate.artist, QStringLiteral("Real artist"));
    QCOMPARE(candidate.album, QStringLiteral("Actual album"));
    QCOMPARE(candidate.durationSeconds, 180LL);
    QCOMPARE(candidate.syncedLyrics, QStringLiteral("[00:01.20]line"));
    QCOMPARE(candidate.source.providerId, QStringLiteral("lrcapi"));

    provider.requestSearch(2, track);
    manager.requests.constLast().second->respond(200, QByteArrayLiteral(
        R"([{"title":"Song","artist":"Artist","lyrics":"plain line"}])"));
    QCOMPARE(results.constLast().kind, LyricsProvider::Result::SearchResults);
    QCOMPARE(results.constLast().candidates.constFirst().plainLyrics,
             QStringLiteral("plain line"));
}

void LrcApiLyricsProviderTest::mapsNoMatchAndCancellation()
{
    LrcApiFakeManager manager;
    LrcApiLyricsProvider provider(&manager);
    QList<LyricsProvider::Result> results;
    connect(&provider, &LyricsProvider::finished, this,
            [&results](quint64, const LyricsProvider::Result& result) {
                results.push_back(result);
            });
    const LyricsProvider::Track track{QStringLiteral("Song"), QStringLiteral("Artist")};
    provider.requestExact(1, track);
    manager.requests.constLast().second->respond(404);
    QCOMPARE(results.constLast().kind, LyricsProvider::Result::NotFound);

    provider.requestExact(2, track);
    provider.cancel(2);
    QCOMPARE(results.size(), 1);
}

void LrcApiLyricsProviderTest::refreshRequestsBypassCachesAndHaveUniqueQueries()
{
    LrcApiFakeManager manager;
    LrcApiLyricsProvider provider(&manager);
    LyricsProvider::Track track{QStringLiteral("Song"), QStringLiteral("Artist")};
    track.forceRefresh = true;
    provider.requestSearch(1, track);
    provider.requestSearch(2, track);
    const auto first = manager.requests.constFirst().first;
    const auto second = manager.requests.constLast().first;
    QVERIFY(first.url() != second.url());
    QVERIFY(!QUrlQuery(first.url()).queryItemValue(QStringLiteral("_refresh")).isEmpty());
    QCOMPARE(first.attribute(QNetworkRequest::CacheLoadControlAttribute).toInt(),
             int(QNetworkRequest::AlwaysNetwork));
    QCOMPARE(first.attribute(QNetworkRequest::CacheSaveControlAttribute).toBool(), false);
    QCOMPARE(first.rawHeader("Cache-Control"), QByteArray("no-cache, no-store"));
}

void LrcApiLyricsProviderTest::timeoutAndRateLimitRemainObservable()
{
    LrcApiFakeManager manager;
    LrcApiLyricsProvider provider(&manager, nullptr, 10);
    QList<LyricsProvider::Result> results;
    connect(&provider, &LyricsProvider::finished, this,
            [&results](quint64, const LyricsProvider::Result& result) { results.append(result); });
    const LyricsProvider::Track track{QStringLiteral("Song"), QStringLiteral("Artist")};
    provider.requestSearch(1, track);
    QTRY_COMPARE(results.size(), 1);
    QCOMPARE(results.constLast().diagnostic, QStringLiteral("timeout"));
    provider.requestSearch(2, track);
    manager.requests.constLast().second->respond(429);
    QCOMPARE(results.constLast().kind, LyricsProvider::Result::RateLimited);
    QVERIFY(results.constLast().retryAfterMs > 0);
}

void LrcApiLyricsProviderTest::rejectsNonTextHtmlAndOversizedResponses()
{
    LrcApiFakeManager manager;
    LrcApiLyricsProvider provider(&manager);
    QList<LyricsProvider::Result> results;
    connect(&provider, &LyricsProvider::finished, this,
            [&results](quint64, const LyricsProvider::Result& result) {
                results.push_back(result);
            });
    const LyricsProvider::Track track{QStringLiteral("Song"),
                                      QStringLiteral("Artist")};

    provider.requestExact(1, track);
    manager.requests.constLast().second->respond(
        200, QByteArrayLiteral("{\"error\":true}"),
        QByteArrayLiteral("application/json"));
    QCOMPARE(results.constLast().kind, LyricsProvider::Result::TechnicalError);

    provider.requestExact(2, track);
    manager.requests.constLast().second->respond(
        200, QByteArrayLiteral("<!doctype html><title>proxy error</title>"));
    QCOMPARE(results.constLast().kind, LyricsProvider::Result::TechnicalError);

    provider.requestExact(3, track);
    manager.requests.constLast().second->respond(
        200, QByteArray(512 * 1024 + 1, 'x'),
        QByteArrayLiteral("text/plain"), false);
    QCOMPARE(results.constLast().kind, LyricsProvider::Result::TechnicalError);
}

QTEST_GUILESS_MAIN(LrcApiLyricsProviderTest)

#include "lrcapi_lyrics_provider_test.moc"
