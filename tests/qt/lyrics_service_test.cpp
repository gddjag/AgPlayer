#include "library_model.hpp"
#include "lyrics_cache.hpp"
#include "lyrics_line_model.hpp"
#include "lyrics_provider.hpp"
#include "lyrics_service.hpp"
#include "settings_controller.hpp"

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

class FakeLyricsProvider final : public LyricsProvider {
    Q_OBJECT

public:
    using LyricsProvider::LyricsProvider;

    void requestExact(quint64 requestId, const Track& track) override
    {
        exactRequests.append({requestId, track});
    }

    void requestSearch(quint64 requestId, const Track& track) override
    {
        searchRequests.append({requestId, track});
    }

    void cancel(quint64 requestId) override { canceledRequests.append(requestId); }

    struct Request final { quint64 requestId; Track track; };
    QList<Request> exactRequests;
    QList<Request> searchRequests;
    QList<quint64> canceledRequests;
};

class LyricsServiceTest final : public QObject {
    Q_OBJECT

private slots:
    void parseLrcPreservesTimingMetadataAndUntimedText();
    void cacheUsesTrackIdentityAndRejectsStaleEntry();
    void onlineResultWritesReusableCacheWithTimedLines();
    void modelSelectsCurrentLineWithOffsetAndFollowPause();
    void providerFallsBackFromExactToSearchAndRejectsMismatches();
    void cancelTimeoutAndTechnicalFailuresDegradeWithoutCountingNotFound();
    void retryAfterBlocksManualRetryWithoutIncreasingFailures();
};

void LyricsServiceTest::parseLrcPreservesTimingMetadataAndUntimedText()
{
    const auto document = LyricsLineModel::parseLrc(
        QByteArrayLiteral("\xEF\xBB\xBF[ar: Artist]\r\n[ti: Song]\r\n"
                          "[offset:-120]\r\n[00:01.25][00:02.345]Hello\r\n"
                          "[00:02.345]Again\r\nplain text\r\n[bad]not timed\r\n"));

    QCOMPARE(document.metadata.value(QStringLiteral("ar")), QStringLiteral("Artist"));
    QCOMPARE(document.offsetMs, -120LL);
    QCOMPARE(document.lines.size(), 3);
    QCOMPARE(document.lines.at(0).timeMs, 1250LL);
    QCOMPARE(document.lines.at(1).timeMs, 2345LL);
    QCOMPARE(document.lines.at(1).text, QStringLiteral("Hello"));
    QCOMPARE(document.lines.at(2).text, QStringLiteral("Again"));
    QCOMPARE(document.untimedText, QStringLiteral("plain text\n[bad]not timed"));
}

void LyricsServiceTest::cacheUsesTrackIdentityAndRejectsStaleEntry()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    TrackRecord track;
    track.trackId = QStringLiteral("one");
    track.path = directory.filePath(QStringLiteral("song.flac"));
    track.fileSize = 42;
    track.durationMs = 123456;
    { QFile file(track.path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("x"); }

    LyricsCache cache(directory.path());
    LyricsCache::Entry entry;
    entry.source = QStringLiteral("manual");
    entry.document.lines = {{1250, QStringLiteral("first")}};
    QVERIFY(cache.save(track, entry));
    QVERIFY(QFileInfo::exists(cache.pathFor(track)));

    const auto cached = cache.load(track);
    QVERIFY(cached.has_value());
    QCOMPARE(cached->document.lines.at(0).text, QStringLiteral("first"));

    track.durationMs++;
    QVERIFY(!cache.load(track).has_value());
}

void LyricsServiceTest::onlineResultWritesReusableCacheWithTimedLines()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    TrackRecord track;
    track.trackId = QStringLiteral("cached-online");
    track.title = QStringLiteral("Cached Song");
    track.path = directory.filePath(QStringLiteral("song.flac"));
    { QFile audio(track.path); QVERIFY(audio.open(QIODevice::WriteOnly)); audio.write("x"); }

    SettingsController settings;
    settings.setCacheDirectory(directory.filePath(QStringLiteral("cache")));
    auto* online = new FakeLyricsProvider;
    LyricsService fetched(nullptr, nullptr, &settings, online);
    fetched.setEnabled(true);
    fetched.requestTrack(track);
    LyricsProvider::Candidate candidate;
    candidate.title = track.title;
    candidate.syncedLyrics = QStringLiteral("[00:01.00]Persisted");
    online->complete(online->exactRequests.constFirst().requestId,
                     LyricsProvider::Result::found(candidate));
    QCOMPARE(qobject_cast<LyricsLineModel*>(fetched.lines())->rowCount(), 1);

    auto* offline = new FakeLyricsProvider;
    LyricsService reused(nullptr, nullptr, &settings, offline);
    reused.setEnabled(true);
    reused.requestTrack(track);
    QCOMPARE(reused.status(), LyricsService::Ready);
    QCOMPARE(qobject_cast<LyricsLineModel*>(reused.lines())->rowCount(), 1);
    QVERIFY(offline->exactRequests.isEmpty());
}

void LyricsServiceTest::modelSelectsCurrentLineWithOffsetAndFollowPause()
{
    LyricsLineModel model;
    model.setLines({{1000, QStringLiteral("one")}, {2000, QStringLiteral("two")},
                    {3000, QStringLiteral("three")}});
    QCOMPARE(model.lineAt(2100, 0), QStringLiteral("two"));
    QCOMPARE(model.lineAt(2100, 250), QStringLiteral("one"));
    QCOMPARE(model.previousLine(2100, 0), QStringLiteral("one"));
    QCOMPARE(model.nextLine(2100, 0), QStringLiteral("three"));
}

void LyricsServiceTest::providerFallsBackFromExactToSearchAndRejectsMismatches()
{
    auto* provider = new FakeLyricsProvider;
    LyricsService service(nullptr, nullptr, nullptr, provider);
    service.setEnabled(true);

    TrackRecord track;
    track.trackId = QStringLiteral("track");
    track.title = QStringLiteral("The Song");
    track.artist = QStringLiteral("The Artist");
    track.durationMs = 200000;
    service.requestTrack(track);
    QCOMPARE(provider->exactRequests.size(), 1);

    provider->complete(provider->exactRequests.constFirst().requestId,
                       LyricsProvider::Result::notFound());
    QCOMPARE(provider->searchRequests.size(), 1);

    LyricsProvider::Candidate wrong;
    wrong.title = QStringLiteral("Other Song");
    wrong.artist = QStringLiteral("Other Artist");
    wrong.durationSeconds = 200;
    wrong.syncedLyrics = QStringLiteral("[00:01.00]Wrong");
    provider->complete(provider->searchRequests.constFirst().requestId,
                       LyricsProvider::Result::search({wrong}));
    QCOMPARE(service.status(), LyricsService::NotFound);
}

void LyricsServiceTest::cancelTimeoutAndTechnicalFailuresDegradeWithoutCountingNotFound()
{
    auto* provider = new FakeLyricsProvider;
    LyricsService service(nullptr, nullptr, nullptr, provider);
    service.setEnabled(true);
    TrackRecord first; first.trackId = QStringLiteral("first"); first.title = QStringLiteral("First");
    TrackRecord second; second.trackId = QStringLiteral("second"); second.title = QStringLiteral("Second");
    service.requestTrack(first);
    const quint64 firstRequest = provider->exactRequests.constFirst().requestId;
    service.requestTrack(second);
    QVERIFY(provider->canceledRequests.contains(firstRequest));

    provider->complete(provider->exactRequests.constLast().requestId,
                       LyricsProvider::Result::notFound());
    provider->complete(provider->searchRequests.constLast().requestId,
                       LyricsProvider::Result::notFound());
    QCOMPARE(service.consecutiveTechnicalFailures(), 0);

    for (int attempt = 0; attempt < 3; ++attempt) {
        service.retry();
        provider->complete(provider->exactRequests.constLast().requestId,
            LyricsProvider::Result::technicalError(attempt == 0 ? 0 : 503,
                                                    attempt == 0));
    }
    QVERIFY(service.degradedUntilMs() > service.clockMs());
    QCOMPARE(service.status(), LyricsService::Offline);
}

void LyricsServiceTest::retryAfterBlocksManualRetryWithoutIncreasingFailures()
{
    auto* provider = new FakeLyricsProvider;
    LyricsService service(nullptr, nullptr, nullptr, provider);
    service.setEnabled(true);
    TrackRecord track; track.trackId = QStringLiteral("rate-limit"); track.title = QStringLiteral("Rate Limit");
    track.path = QStringLiteral("C:/private/music/rate-limit.flac");
    service.requestTrack(track);
    provider->complete(provider->exactRequests.constFirst().requestId,
                       LyricsProvider::Result::rateLimited(60000));
    QCOMPARE(service.status(), LyricsService::Offline);
    QCOMPARE(service.consecutiveTechnicalFailures(), 0);
    service.retry();
    QCOMPARE(provider->exactRequests.size(), 1);
    const QVariantMap diagnostic = service.diagnostics();
    QVERIFY(!diagnostic.values().contains(track.path));
}

QTEST_MAIN(LyricsServiceTest)
#include "lyrics_service_test.moc"
