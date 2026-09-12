#include "lyrics_provider_chain.hpp"

#include <QtTest>

#include <cstdlib>

class FakeProvider final : public LyricsProvider {
    Q_OBJECT
public:
    struct Request final { quint64 id; Track track; bool exact; };
    QList<Request> requests;
    QList<quint64> cancelled;

    void requestExact(const quint64 requestId, const Track& track) override
    { requests.append({requestId, track, true}); }
    void requestSearch(const quint64 requestId, const Track& track) override
    { requests.append({requestId, track, false}); }
    void cancel(const quint64 requestId) override { cancelled.append(requestId); }
    void respond(const quint64 requestId, const Result& result) { complete(requestId, result); }
};

static LyricsProvider::Candidate candidate(const QString& text = QStringLiteral("lyrics"),
                                           const bool instrumental = false)
{
    LyricsProvider::Candidate value;
    value.plainLyrics = text;
    value.instrumental = instrumental;
    value.source.providerId = QStringLiteral("test");
    return value;
}

static LyricsProvider::RouteAttempt attemptAt(const LyricsProvider::Result& result,
                                               const int index)
{
    return result.attempts.at(index);
}

class LyricsProviderChainTest final : public QObject {
    Q_OBJECT

private slots:
    void routesExactRequestsInProvidedOrder();
    void preservesSearchStageAndUsesMatcherWithoutScoring();
    void exactMatcherRejectionsContinueWithoutOpeningCircuit();
    void emitsTypedFailuresAndTreatsRateLimitSeparately();
    void circuitBlocksOnlyFailingRouteAndAllowsOneProbe();
    void stopsOnFirstFoundIncludingInstrumental();
    void cancellationAndReusedExternalIdsIsolateLateResults();
    void partialOutageWithNormalNoMatchReturnsNotFound();
    void allTechnicalFailuresReturnTechnicalError();
    void ignoresPreviousProviderResultsAfterAdvance();
};

void LyricsProviderChainTest::routesExactRequestsInProvidedOrder()
{
    FakeProvider lrclib, unison, ovh;
    qint64 now = 0;
    LyricsProviderChain chain({{QStringLiteral("lrclib"), QStringLiteral("LRCLIB"), &lrclib},
                               {QStringLiteral("unison"), QStringLiteral("Unison"), &unison},
                               {QStringLiteral("lyrics-ovh"), QStringLiteral("lyrics.ovh"), &ovh}},
                              nullptr, [&now] { return now; });
    QList<LyricsProvider::Result> finished;
    connect(&chain, &LyricsProvider::finished, this,
            [&finished](quint64, const LyricsProvider::Result& result) { finished.append(result); });
    const LyricsProvider::Track track{QStringLiteral("Song"), QStringLiteral("Artist")};

    QCOMPARE(chain.routeIds(), QStringList({QStringLiteral("lrclib"), QStringLiteral("unison"),
                                             QStringLiteral("lyrics-ovh")}));
    chain.requestExact(91, track);
    QCOMPARE(lrclib.requests.size(), 1);
    QVERIFY(lrclib.requests.constLast().exact);
    QVERIFY(lrclib.requests.constLast().id != 91);
    lrclib.respond(lrclib.requests.constLast().id, LyricsProvider::Result::notFound());
    QCOMPARE(unison.requests.size(), 1);
    QVERIFY(unison.requests.constLast().exact);
    unison.respond(unison.requests.constLast().id, LyricsProvider::Result::notFound());
    QCOMPARE(ovh.requests.size(), 1);
    QVERIFY(ovh.requests.constLast().exact);
    ovh.respond(ovh.requests.constLast().id, LyricsProvider::Result::found(candidate()));
    QCOMPARE(finished.size(), 1);
    QCOMPARE(finished.constLast().kind, LyricsProvider::Result::Found);
    QCOMPARE(finished.constLast().attempts.size(), 2);
    QCOMPARE(attemptAt(finished.constLast(), 0).diagnostic, QStringLiteral("not-found"));
    QCOMPARE(attemptAt(finished.constLast(), 1).providerId, QStringLiteral("unison"));
}

void LyricsProviderChainTest::preservesSearchStageAndUsesMatcherWithoutScoring()
{
    FakeProvider first, second, third;
    int matcherCalls = 0;
    LyricsProviderChain chain({{QStringLiteral("first"), QStringLiteral("First"), &first},
                               {QStringLiteral("second"), QStringLiteral("Second"), &second},
                               {QStringLiteral("third"), QStringLiteral("Third"), &third}}, nullptr, {},
        [&matcherCalls](const LyricsProvider::Track&, const QList<LyricsProvider::Candidate>& values) {
            ++matcherCalls;
            return values.constFirst().plainLyrics == QStringLiteral("accepted");
        });
    QList<LyricsProvider::Result> finished;
    QList<LyricsProvider::RouteAttempt> failures;
    connect(&chain, &LyricsProvider::finished, this,
            [&finished](quint64, const LyricsProvider::Result& result) { finished.append(result); });
    connect(&chain, &LyricsProviderChain::routeFailed, this,
            [&failures](quint64, const LyricsProvider::RouteAttempt& value) { failures.append(value); });
    const LyricsProvider::Track track{QStringLiteral("Song"), QStringLiteral("Artist")};

    chain.requestSearch(1, track);
    QVERIFY(!first.requests.constLast().exact);
    first.respond(first.requests.constLast().id, LyricsProvider::Result::search({}));
    QVERIFY(!second.requests.constLast().exact);
    second.respond(second.requests.constLast().id, LyricsProvider::Result::search({candidate("rejected")}));
    QVERIFY(!third.requests.constLast().exact);
    third.respond(third.requests.constLast().id, LyricsProvider::Result::search({candidate("accepted")}));
    QCOMPARE(matcherCalls, 2);
    QCOMPARE(failures.size(), 0);
    QCOMPARE(finished.constLast().kind, LyricsProvider::Result::SearchResults);
    QCOMPARE(finished.constLast().candidates.constFirst().plainLyrics, QStringLiteral("accepted"));
    QCOMPARE(finished.constLast().attempts.size(), 2);
    QCOMPARE(attemptAt(finished.constLast(), 0).diagnostic, QStringLiteral("empty-search"));
    QCOMPARE(attemptAt(finished.constLast(), 1).diagnostic, QStringLiteral("no-acceptable-match"));
}

void LyricsProviderChainTest::exactMatcherRejectionsContinueWithoutOpeningCircuit()
{
    FakeProvider first, fallback;
    LyricsProviderChain chain({{QStringLiteral("first"), QStringLiteral("First"), &first},
                               {QStringLiteral("fallback"), QStringLiteral("Fallback"), &fallback}},
                              nullptr, {},
        [](const LyricsProvider::Track& track,
           const QList<LyricsProvider::Candidate>& values) {
            if (values.size() != 1) return false;
            const LyricsProvider::Candidate& value = values.constFirst();
            return value.title == track.title && value.artist == track.artist
                && std::llabs(value.durationSeconds - track.durationMs / 1000) <= 3;
        });
    QList<LyricsProvider::Result> finished;
    connect(&chain, &LyricsProvider::finished, this,
            [&finished](quint64, const LyricsProvider::Result& result) { finished.append(result); });
    const LyricsProvider::Track track{QStringLiteral("Song"), QStringLiteral("Artist"),
                                       {}, 180000};

    for (quint64 requestId = 1; requestId <= 4; ++requestId) {
        chain.requestExact(requestId, track);
        LyricsProvider::Candidate mismatch;
        mismatch.title = requestId == 1 ? QStringLiteral("Wrong Song") : track.title;
        mismatch.artist = requestId == 2 ? QStringLiteral("Wrong Artist") : track.artist;
        mismatch.durationSeconds = requestId >= 3 ? 240 : 180;
        mismatch.plainLyrics = QStringLiteral("wrong lyrics");
        first.respond(first.requests.constLast().id,
                      LyricsProvider::Result::found(std::move(mismatch)));

        QCOMPARE(fallback.requests.size(), static_cast<qsizetype>(requestId));
        LyricsProvider::Candidate accepted;
        accepted.title = track.title;
        accepted.artist = track.artist;
        accepted.durationSeconds = 180;
        accepted.plainLyrics = QStringLiteral("accepted lyrics");
        fallback.respond(fallback.requests.constLast().id,
                         LyricsProvider::Result::found(std::move(accepted)));
        QCOMPARE(finished.size(), static_cast<qsizetype>(requestId));
        QCOMPARE(finished.constLast().attempts.size(), 1);
        QCOMPARE(finished.constLast().attempts.constFirst().diagnostic,
                 QStringLiteral("no-acceptable-match"));
    }

    QCOMPARE(first.requests.size(), 4);
}

void LyricsProviderChainTest::emitsTypedFailuresAndTreatsRateLimitSeparately()
{
    FakeProvider failing, fallback;
    qint64 now = 100;
    LyricsProviderChain chain({{QStringLiteral("failing"), QStringLiteral("Failing"), &failing},
                               {QStringLiteral("fallback"), QStringLiteral("Fallback"), &fallback}},
                              nullptr, [&now] { return now; });
    QList<LyricsProvider::RouteAttempt> failures;
    connect(&chain, &LyricsProviderChain::routeFailed, this,
            [&failures](quint64, const LyricsProvider::RouteAttempt& value) { failures.append(value); });
    const LyricsProvider::Track track{QStringLiteral("Song"), QStringLiteral("Artist")};

    chain.requestExact(1, track);
    failing.respond(failing.requests.constLast().id,
                    LyricsProvider::Result::technicalError(503, true, QStringLiteral("dns")));
    QCOMPARE(failures.size(), 1);
    QCOMPARE(failures.constLast().diagnostic, QStringLiteral("dns"));
    QCOMPARE(failures.constLast().httpStatus, 503);
    QVERIFY(failures.constLast().offline);
    fallback.respond(fallback.requests.constLast().id, LyricsProvider::Result::found(candidate()));

    chain.requestExact(2, track);
    failing.respond(failing.requests.constLast().id, LyricsProvider::Result::rateLimited(5000));
    QCOMPARE(failures.size(), 2);
    QCOMPARE(failures.constLast().diagnostic, QStringLiteral("rate-limited"));
    QCOMPARE(failures.constLast().httpStatus, 429);
    QCOMPARE(failures.constLast().retryAfterMs, 5000LL);
    fallback.respond(fallback.requests.constLast().id, LyricsProvider::Result::found(candidate()));

    chain.requestExact(3, track);
    QCOMPARE(failing.requests.size(), 2);
    QCOMPARE(fallback.requests.size(), 3);
    QCOMPARE(fallback.requests.constLast().track.lowPriority, false);
}

void LyricsProviderChainTest::circuitBlocksOnlyFailingRouteAndAllowsOneProbe()
{
    FakeProvider failing, fallback;
    qint64 now = 0;
    LyricsProviderChain chain({{QStringLiteral("failing"), QStringLiteral("Failing"), &failing},
                               {QStringLiteral("fallback"), QStringLiteral("Fallback"), &fallback}},
                              nullptr, [&now] { return now; });
    QList<LyricsProvider::Result> finished;
    connect(&chain, &LyricsProvider::finished, this,
            [&finished](quint64, const LyricsProvider::Result& result) { finished.append(result); });
    const LyricsProvider::Track foreground{QStringLiteral("Song"), QStringLiteral("Artist")};

    for (quint64 id = 1; id <= 3; ++id) {
        chain.requestExact(id, foreground);
        failing.respond(failing.requests.constLast().id,
                        LyricsProvider::Result::technicalError(500, false, QStringLiteral("server")));
        fallback.respond(fallback.requests.constLast().id, LyricsProvider::Result::found(candidate()));
    }
    QCOMPARE(failing.requests.size(), 3);
    chain.requestExact(4, foreground);
    QCOMPARE(failing.requests.size(), 3);
    QCOMPARE(fallback.requests.size(), 4);
    QCOMPARE(fallback.requests.constLast().track.lowPriority, false);
    fallback.respond(fallback.requests.constLast().id, LyricsProvider::Result::found(candidate()));
    QCOMPARE(attemptAt(finished.constLast(), 0).diagnostic, QStringLiteral("circuit-open"));
    QCOMPARE(attemptAt(finished.constLast(), 0).retryAfterMs, 600000LL);

    now = 600000;
    chain.requestExact(5, foreground);
    QCOMPARE(failing.requests.size(), 4);
    const quint64 probe = failing.requests.constLast().id;
    LyricsProvider::Track prefetch = foreground;
    prefetch.lowPriority = true;
    chain.requestExact(6, prefetch);
    QCOMPARE(failing.requests.size(), 4);
    QCOMPARE(fallback.requests.size(), 5);
    QVERIFY(fallback.requests.constLast().track.lowPriority);
    fallback.respond(fallback.requests.constLast().id, LyricsProvider::Result::found(candidate()));
    failing.respond(probe, LyricsProvider::Result::notFound());
    QCOMPARE(fallback.requests.size(), 6);
    fallback.respond(fallback.requests.constLast().id, LyricsProvider::Result::found(candidate()));
    chain.requestExact(7, foreground);
    QCOMPARE(failing.requests.size(), 5);
}

void LyricsProviderChainTest::stopsOnFirstFoundIncludingInstrumental()
{
    FakeProvider first, second;
    LyricsProviderChain chain({{QStringLiteral("first"), QStringLiteral("First"), &first},
                               {QStringLiteral("second"), QStringLiteral("Second"), &second}});
    QList<LyricsProvider::Result> finished;
    connect(&chain, &LyricsProvider::finished, this,
            [&finished](quint64, const LyricsProvider::Result& result) { finished.append(result); });
    const LyricsProvider::Track track{QStringLiteral("Song"), QStringLiteral("Artist")};
    chain.requestExact(1, track);
    first.respond(first.requests.constLast().id, LyricsProvider::Result::found(candidate({}, true)));
    QCOMPARE(finished.constLast().candidate.instrumental, true);
    QCOMPARE(second.requests.size(), 0);
}

void LyricsProviderChainTest::cancellationAndReusedExternalIdsIsolateLateResults()
{
    FakeProvider first, second;
    LyricsProviderChain chain({{QStringLiteral("first"), QStringLiteral("First"), &first},
                               {QStringLiteral("second"), QStringLiteral("Second"), &second}});
    QList<LyricsProvider::Result> finished;
    connect(&chain, &LyricsProvider::finished, this,
            [&finished](quint64, const LyricsProvider::Result& result) { finished.append(result); });
    const LyricsProvider::Track track{QStringLiteral("Song"), QStringLiteral("Artist")};

    chain.requestExact(42, track);
    const quint64 oldInternal = first.requests.constLast().id;
    chain.requestExact(42, track);
    const quint64 newInternal = first.requests.constLast().id;
    QVERIFY(oldInternal != newInternal);
    QCOMPARE(first.cancelled, QList<quint64>{oldInternal});
    first.respond(oldInternal, LyricsProvider::Result::found(candidate("old")));
    QCOMPARE(finished.size(), 0);
    first.respond(newInternal, LyricsProvider::Result::notFound());
    QCOMPARE(second.requests.size(), 1);
    second.respond(second.requests.constLast().id, LyricsProvider::Result::found(candidate("new")));
    QCOMPARE(finished.size(), 1);
    QCOMPARE(finished.constLast().candidate.plainLyrics, QStringLiteral("new"));

    chain.requestExact(99, track);
    const quint64 active = first.requests.constLast().id;
    chain.cancel(99);
    QCOMPARE(first.cancelled.constLast(), active);
    first.respond(active, LyricsProvider::Result::found(candidate("late")));
    QCOMPARE(finished.size(), 1);
}

void LyricsProviderChainTest::partialOutageWithNormalNoMatchReturnsNotFound()
{
    FakeProvider first, second;
    LyricsProviderChain chain({{QStringLiteral("first"), QStringLiteral("First"), &first},
                               {QStringLiteral("second"), QStringLiteral("Second"), &second},
                               {QStringLiteral("missing"), QStringLiteral("Missing"), nullptr}});
    QList<LyricsProvider::Result> finished;
    connect(&chain, &LyricsProvider::finished, this,
            [&finished](quint64, const LyricsProvider::Result& result) { finished.append(result); });
    const LyricsProvider::Track track{QStringLiteral("Song"), QStringLiteral("Artist")};

    chain.requestExact(1, track);
    first.respond(first.requests.constLast().id,
                  LyricsProvider::Result::technicalError(
                      503, false, QStringLiteral("provider-error")));
    second.respond(second.requests.constLast().id, LyricsProvider::Result::notFound());
    QCOMPARE(finished.constLast().kind, LyricsProvider::Result::NotFound);
    QCOMPARE(finished.constLast().attempts.size(), 3);
    QCOMPARE(attemptAt(finished.constLast(), 0).diagnostic,
             QStringLiteral("provider-error"));
    QCOMPARE(attemptAt(finished.constLast(), 1).diagnostic, QStringLiteral("not-found"));
    QCOMPARE(attemptAt(finished.constLast(), 2).diagnostic, QStringLiteral("provider-unavailable"));
}

void LyricsProviderChainTest::allTechnicalFailuresReturnTechnicalError()
{
    FakeProvider first, second;
    LyricsProviderChain chain({{QStringLiteral("first"), QStringLiteral("First"), &first},
                               {QStringLiteral("second"), QStringLiteral("Second"), &second}});
    QList<LyricsProvider::Result> finished;
    connect(&chain, &LyricsProvider::finished, this,
            [&finished](quint64, const LyricsProvider::Result& result) { finished.append(result); });
    const LyricsProvider::Track track{QStringLiteral("Song"), QStringLiteral("Artist")};

    chain.requestExact(2, track);
    first.respond(first.requests.constLast().id,
                  LyricsProvider::Result::technicalError(
                      503, false, QStringLiteral("server-error")));
    second.respond(second.requests.constLast().id,
                   LyricsProvider::Result::technicalError(
                       0, true, QStringLiteral("network-unavailable")));
    QCOMPARE(finished.constLast().kind, LyricsProvider::Result::TechnicalError);
    QCOMPARE(finished.constLast().diagnostic, QStringLiteral("all-routes-failed"));
    QCOMPARE(finished.constLast().attempts.size(), 2);
}

void LyricsProviderChainTest::ignoresPreviousProviderResultsAfterAdvance()
{
    FakeProvider first, second;
    LyricsProviderChain chain({{QStringLiteral("first"), QStringLiteral("First"), &first},
                               {QStringLiteral("second"), QStringLiteral("Second"), &second}});
    QList<LyricsProvider::Result> finished;
    connect(&chain, &LyricsProvider::finished, this,
            [&finished](quint64, const LyricsProvider::Result& result) { finished.append(result); });
    const LyricsProvider::Track track{QStringLiteral("Song"), QStringLiteral("Artist")};
    chain.requestExact(1, track);
    const quint64 old = first.requests.constLast().id;
    first.respond(old, LyricsProvider::Result::notFound());
    const quint64 active = second.requests.constLast().id;
    first.respond(old, LyricsProvider::Result::technicalError(500));
    QCOMPARE(second.cancelled.size(), 0);
    QCOMPARE(finished.size(), 0);
    second.respond(active, LyricsProvider::Result::found(candidate()));
    QCOMPARE(finished.size(), 1);
}

QTEST_MAIN(LyricsProviderChainTest)
#include "lyrics_provider_chain_test.moc"
