#include "library_model.hpp"
#include "lyrics_cache.hpp"
#include "lyrics_line_model.hpp"
#include "lyrics_provider.hpp"
#include "lyrics_provider_chain.hpp"
#include "lyrics_service.hpp"
#include "playback_controller.hpp"
#include "settings_controller.hpp"

#include <QFile>
#include <QFileInfo>
#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QTemporaryDir>
#include <QUrlQuery>
#include <QtTest>

#include <agplayer/c_api.h>

#include <algorithm>
#include <cstring>

namespace {

LyricsProvider::Source manualSource()
{
    return {QStringLiteral("manual"), QStringLiteral("Manual"), {}, {}, true};
}

LyricsProvider::Source lrclibSource()
{
    return {QStringLiteral("lrclib"), QStringLiteral("LRCLIB"),
            QUrl(QStringLiteral("https://lrclib.net")), {}, true};
}

LyricsProvider::Source unisonSource()
{
    return {QStringLiteral("unison"), QStringLiteral("Unison"),
            QUrl(QStringLiteral("https://unison.boidu.dev")),
            QStringLiteral("Lyrics from Unison (https://unison.boidu.dev)"), true};
}

LyricsProvider::Source lyricsOvhSource()
{
    return {QStringLiteral("lyrics-ovh"), QStringLiteral("lyrics.ovh"),
            QUrl(QStringLiteral("https://api.lyrics.ovh")), {}, false};
}

} // namespace

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
        for (const auto& header : headers) setRawHeader(header.first, header.second);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        body_ = std::move(body);
        setFinished(true);
        emit readyRead();
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
    struct Request final { QNetworkRequest request; FakeNetworkReply* reply = nullptr; };
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
    void cacheRoundTripsTypedSourceAndWritesVersionTwoMetadata();
    void cacheReadsLegacyVersionOneSourcesAndInfersSynchronization();
    void onlineResultWritesReusableCacheWithTimedLines();
    void modelSelectsCurrentLineWithOffsetAndFollowPause();
    void providerFallsBackFromExactToSearchAndRejectsMismatches();
    void canceledRequestsAndRepeatedTechnicalFailuresRemainRetryable();
    void rateLimitedResultRemainsManuallyRetryable();
    void sameTitleAndArtistWithMateriallyWrongDurationIsRejected();
    void notFoundFollowedByTechnicalFailureRemainsRetryable();
    void disabledServiceDoesNotPublishPositionDrivenLineChanges();
    void enabledServiceDoesNotRepublishAnUnchangedLineOnEveryPositionTick();
    void disablingLyricsHostPreservesResolvedPresentationState();
    void emptyEmbeddedLyricsFallsThroughToProvider();
    void embeddedLyricsRetainLocalSourceWithoutProviderRequest();
    void sidecarLyricsRetainLocalSourceWithoutProviderRequest();
    void instrumentalResultIsCachedWithoutAFalseTimeline();
    void cacheRejectsCorruptAndWrongSchemaEntries();
    void cacheRejectsMalformedVersionTwoMetadata();
    void pauseFollowDefaultsToFiveSecondsAndNotifies();
    void injectedProviderRemainsBorrowed();
    void lrclibTransportBuildsRequestsAndHandlesCancelTimeoutAndRetryAfter();
    void lrclibCandidatesCarryStableSourceMetadata();
    void malformedLrclibResponsesAreTechnicalErrors();
    void repeatedInvalidResponsesDoNotGloballyBlackoutRetry();
    void defaultServiceExposesOrderedProviderRoutesWithoutRequest();
    void lowMatchLrclibSearchFallsThroughToUnison();
    void plainLyricsExposeUntimedSourceState();
    void prefetchFailureAndSuccessDoNotMutateForegroundPresentation();
    void canceledLatePreviousTrackResultCannotMutateReplacementTrack();
    void previousGenerationWithSameTrackIdCannotOverwriteCurrentRequest();
    void newerRouteNoticeOutlivesOlderNoticeTimer();
    void resultWithRouteAttemptsSurvivesProviderSignal();
    void serviceIgnoresAttemptResultWithoutPendingRequest();
    void finalNotFoundKeepsCollapsedSafeRouteAttempts();
    void providerChainPreservesChildRouteAttempts();
    void partialExactOutageStillFallsBackToSearch();
    void chainRouteFailureIsForegroundOnlyAndSuccessPreservesSource();
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
    entry.source = manualSource();
    entry.document.lines = {{1250, QStringLiteral("first")}};
    entry.synchronized = true;
    QVERIFY(cache.save(track, entry));
    QVERIFY(QFileInfo::exists(cache.pathFor(track)));

    const auto cached = cache.load(track);
    QVERIFY(cached.has_value());
    QCOMPARE(cached->document.lines.at(0).text, QStringLiteral("first"));

    track.durationMs++;
    QVERIFY(!cache.load(track).has_value());
}

void LyricsServiceTest::cacheRoundTripsTypedSourceAndWritesVersionTwoMetadata()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    TrackRecord track;
    track.trackId = QStringLiteral("unison-cache");
    track.path = directory.filePath(QStringLiteral("unison.flac"));
    { QFile file(track.path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("x"); }

    LyricsProvider::Candidate candidate;
    candidate.source = unisonSource();
    candidate.syncedLyrics = QStringLiteral("[00:01.00]line");
    LyricsCache cache(directory.path());
    const LyricsCache::Entry entry{LyricsLineModel::parseLrc(candidate.syncedLyrics.toUtf8()),
                                   candidate.source, false, true};
    QVERIFY(cache.save(track, entry));

    const auto loaded = cache.load(track);
    QVERIFY(loaded.has_value());
    QCOMPARE(loaded->source.providerId, QStringLiteral("unison"));
    QCOMPARE(loaded->source.providerName, QStringLiteral("Unison"));
    QCOMPARE(loaded->source.sourceUrl, QUrl(QStringLiteral("https://unison.boidu.dev")));
    QCOMPARE(loaded->source.attribution,
             QStringLiteral("Lyrics from Unison (https://unison.boidu.dev)"));
    QVERIFY(loaded->source.supportsSyncedLyrics);
    QVERIFY(loaded->synchronized);

    QFile file(cache.pathFor(track));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QJsonObject json = QJsonDocument::fromJson(file.readAll()).object();
    QCOMPARE(json.value(QStringLiteral("version")).toInt(), 2);
    QCOMPARE(json.value(QStringLiteral("providerId")).toString(), QStringLiteral("unison"));
    QCOMPARE(json.value(QStringLiteral("providerName")).toString(), QStringLiteral("Unison"));
    QCOMPARE(json.value(QStringLiteral("sourceUrl")).toString(),
             QStringLiteral("https://unison.boidu.dev"));
    QCOMPARE(json.value(QStringLiteral("attribution")).toString(),
             QStringLiteral("Lyrics from Unison (https://unison.boidu.dev)"));
    QCOMPARE(json.value(QStringLiteral("supportsSyncedLyrics")).toBool(), true);
    QCOMPARE(json.value(QStringLiteral("synchronized")).toBool(), true);

    LyricsProvider::RouteAttempt attempt{QStringLiteral("lrclib"), QStringLiteral("LRCLIB"),
                                         QStringLiteral("timeout"), 0, 0, true};
    LyricsProvider::Result result = LyricsProvider::Result::found(candidate);
    result.attempts.append(attempt);
    QCOMPARE(result.attempts.constFirst().providerId, QStringLiteral("lrclib"));
    QCOMPARE(result.attempts.constFirst().diagnostic, QStringLiteral("timeout"));
    QVERIFY(result.attempts.constFirst().offline);
}

void LyricsServiceTest::cacheReadsLegacyVersionOneSourcesAndInfersSynchronization()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    LyricsCache cache(directory.path());

    const auto writeLegacy = [&cache](const TrackRecord& track, const QByteArray& source,
                                      const QByteArray& untimedText,
                                      const QByteArray& lines) {
        const QString path = cache.pathFor(track);
        QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray payload = QByteArrayLiteral("{\"version\":1,\"key\":\"")
            + LyricsCache::keyFor(track).toUtf8()
            + QByteArrayLiteral("\",\"source\":\"") + source
            + QByteArrayLiteral("\",\"instrumental\":false,\"offsetMs\":0,"
                                "\"untimedText\":\"") + untimedText
            + QByteArrayLiteral("\",\"lines\":") + lines
            + QByteArrayLiteral(",\"metadata\":{\"ar\":\"Legacy Artist\"}}");
        QCOMPARE(file.write(payload), static_cast<qint64>(payload.size()));
    };

    TrackRecord manual;
    manual.trackId = QStringLiteral("legacy-manual");
    manual.path = directory.filePath(QStringLiteral("legacy-manual.flac"));
    { QFile file(manual.path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("x"); }
    writeLegacy(manual, QByteArrayLiteral("manual"), QByteArrayLiteral("plain legacy"),
                QByteArrayLiteral("[]"));
    const auto loadedManual = cache.load(manual);
    QVERIFY(loadedManual.has_value());
    QCOMPARE(loadedManual->source.providerId, QStringLiteral("manual"));
    QCOMPARE(loadedManual->source.providerName, QStringLiteral("Manual"));
    QVERIFY(loadedManual->source.sourceUrl.isEmpty());
    QVERIFY(!loadedManual->synchronized);

    TrackRecord lrclib;
    lrclib.trackId = QStringLiteral("legacy-lrclib");
    lrclib.path = directory.filePath(QStringLiteral("legacy-lrclib.flac"));
    { QFile file(lrclib.path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("x"); }
    writeLegacy(lrclib, QByteArrayLiteral("lrclib"), QByteArray(),
                QByteArrayLiteral("[{\"timeMs\":1000,\"text\":\"timed legacy\"}]"));
    const auto loadedLrclib = cache.load(lrclib);
    QVERIFY(loadedLrclib.has_value());
    QCOMPARE(loadedLrclib->source.providerId, QStringLiteral("lrclib"));
    QCOMPARE(loadedLrclib->source.providerName, QStringLiteral("LRCLIB"));
    QCOMPARE(loadedLrclib->source.sourceUrl, QUrl(QStringLiteral("https://lrclib.net")));
    QVERIFY(loadedLrclib->source.supportsSyncedLyrics);
    QVERIFY(loadedLrclib->synchronized);
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
    candidate.source = unisonSource();
    candidate.syncedLyrics = QStringLiteral("[00:01.00]Persisted");
    QCOMPARE(online->exactRequests.size(), 1);
    online->complete(online->exactRequests.constFirst().requestId,
                     LyricsProvider::Result::found(candidate));
    auto* fetchedLines = qobject_cast<LyricsLineModel*>(fetched.lines());
    QVERIFY(fetchedLines != nullptr);
    QCOMPARE(fetchedLines->rowCount(), 1);
    QCOMPARE(fetchedLines->lines().constFirst().text, QStringLiteral("Persisted"));
    QCOMPARE(fetched.sourceProvider(), QStringLiteral("Unison"));
    QCOMPARE(fetched.sourceAttribution(), candidate.source.attribution);
    QVERIFY(fetched.synchronizedLyrics());
    QVERIFY(fetched.untimedLyrics().isEmpty());
    LyricsCache cache(settings.cacheDirectory());
    const auto persisted = cache.load(track);
    QVERIFY(persisted.has_value());
    QCOMPARE(persisted->source.providerId, QStringLiteral("unison"));
    QCOMPARE(persisted->source.attribution,
             QStringLiteral("Lyrics from Unison (https://unison.boidu.dev)"));
    QVERIFY(persisted->synchronized);

    auto* offline = new FakeLyricsProvider;
    LyricsService reused(nullptr, nullptr, &settings, offline);
    reused.setEnabled(true);
    reused.requestTrack(track);
    QCOMPARE(reused.status(), LyricsService::Ready);
    auto* reusedLines = qobject_cast<LyricsLineModel*>(reused.lines());
    QVERIFY(reusedLines != nullptr);
    QCOMPARE(reusedLines->rowCount(), 1);
    QCOMPARE(reusedLines->lines().constFirst().timeMs, 1000LL);
    QCOMPARE(reusedLines->lines().constFirst().text, QStringLiteral("Persisted"));
    QCOMPARE(reused.sourceProvider(), QStringLiteral("Unison"));
    QCOMPARE(reused.sourceAttribution(), candidate.source.attribution);
    QVERIFY(reused.synchronizedLyrics());
    QVERIFY(reused.untimedLyrics().isEmpty());
    QCOMPARE(reused.diagnostics().value(QStringLiteral("source")).toString(),
             QStringLiteral("unison"));
    QVERIFY(offline->exactRequests.isEmpty());
}

void LyricsServiceTest::modelSelectsCurrentLineWithOffsetAndFollowPause()
{
    LyricsLineModel model;
    model.setLines({{1000, QStringLiteral("one")}, {2000, QStringLiteral("two")},
                    {3000, QStringLiteral("three")}});
    QCOMPARE(model.lineAt(2100, 0), QStringLiteral("two"));
    QCOMPARE(model.lineAt(2100, 250), QStringLiteral("one"));
    QCOMPARE(model.activeIndex(2100, 0), 1);
    QCOMPARE(model.activeIndex(2100, 250), 0);
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

void LyricsServiceTest::canceledRequestsAndRepeatedTechnicalFailuresRemainRetryable()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    auto* provider = new FakeLyricsProvider;
    LyricsService service(nullptr, nullptr, nullptr, provider);
    service.setEnabled(true);
    TrackRecord first; first.trackId = QStringLiteral("first"); first.title = QStringLiteral("First");
    TrackRecord second; second.trackId = QStringLiteral("second"); second.title = QStringLiteral("Second");
    first.path = directory.filePath(QStringLiteral("first.flac"));
    second.path = directory.filePath(QStringLiteral("second.flac"));
    service.requestTrack(first);
    QCOMPARE(provider->exactRequests.size(), 1);
    const quint64 firstRequest = provider->exactRequests.constFirst().requestId;
    service.requestTrack(second);
    QCOMPARE(provider->exactRequests.size(), 2);
    QVERIFY(provider->canceledRequests.contains(firstRequest));

    provider->complete(provider->exactRequests.constLast().requestId,
                       LyricsProvider::Result::notFound());
    QCOMPARE(provider->searchRequests.size(), 1);
    provider->complete(provider->searchRequests.constLast().requestId,
                       LyricsProvider::Result::notFound());

    for (int attempt = 0; attempt < 3; ++attempt) {
        const qsizetype beforeRetry = provider->exactRequests.size();
        service.retry();
        QCOMPARE(provider->exactRequests.size(), beforeRetry + 1);
        provider->complete(provider->exactRequests.constLast().requestId,
            LyricsProvider::Result::technicalError(attempt == 0 ? 0 : 503,
                                                    attempt == 0));
    }
    QCOMPARE(service.status(), LyricsService::Error);
}

void LyricsServiceTest::rateLimitedResultRemainsManuallyRetryable()
{
    auto* provider = new FakeLyricsProvider;
    LyricsService service(nullptr, nullptr, nullptr, provider);
    service.setEnabled(true);
    TrackRecord track; track.trackId = QStringLiteral("rate-limit"); track.title = QStringLiteral("Rate Limit");
    track.path = QStringLiteral("C:/private/music/rate-limit.flac");
    service.requestTrack(track);
    QCOMPARE(provider->exactRequests.size(), 1);
    provider->complete(provider->exactRequests.constFirst().requestId,
                       LyricsProvider::Result::rateLimited(60000));
    QCOMPARE(service.status(), LyricsService::Offline);
    service.retry();
    QCOMPARE(provider->exactRequests.size(), 2);
    const QVariantMap diagnostic = service.diagnostics();
    QVERIFY(!diagnostic.values().contains(track.path));
}

void LyricsServiceTest::sameTitleAndArtistWithMateriallyWrongDurationIsRejected()
{
    auto* provider = new FakeLyricsProvider;
    LyricsService service(nullptr, nullptr, nullptr, provider);
    service.setEnabled(true);
    TrackRecord track; track.trackId = QStringLiteral("duration");
    track.title = QStringLiteral("Same Song"); track.artist = QStringLiteral("Same Artist");
    track.durationMs = 180000;
    service.requestTrack(track);
    QCOMPARE(provider->exactRequests.size(), 1);
    provider->complete(provider->exactRequests.constFirst().requestId,
                       LyricsProvider::Result::notFound());
    LyricsProvider::Candidate candidate;
    candidate.title = track.title; candidate.artist = track.artist;
    candidate.durationSeconds = 900; candidate.syncedLyrics = QStringLiteral("[00:01]Wrong cut");
    QCOMPARE(provider->searchRequests.size(), 1);
    provider->complete(provider->searchRequests.constFirst().requestId,
                       LyricsProvider::Result::search({candidate}));
    QCOMPARE(service.status(), LyricsService::NotFound);
}

void LyricsServiceTest::notFoundFollowedByTechnicalFailureRemainsRetryable()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    auto* provider = new FakeLyricsProvider;
    LyricsService service(nullptr, nullptr, nullptr, provider);
    service.setEnabled(true);
    TrackRecord track; track.trackId = QStringLiteral("health"); track.title = QStringLiteral("Health");
    track.path = directory.filePath(QStringLiteral("health.flac"));
    service.requestTrack(track);
    for (int attempt = 0; attempt < 2; ++attempt) {
        QCOMPARE(provider->exactRequests.size(), attempt + 1);
        provider->complete(provider->exactRequests.constLast().requestId,
                           LyricsProvider::Result::technicalError(503));
        service.retry();
    }
    QCOMPARE(provider->exactRequests.size(), 3);
    provider->complete(provider->exactRequests.constLast().requestId,
                       LyricsProvider::Result::notFound());
    QCOMPARE(provider->searchRequests.size(), 1);
    provider->complete(provider->searchRequests.constLast().requestId,
                       LyricsProvider::Result::notFound());
    service.retry();
    QCOMPARE(provider->exactRequests.size(), 4);
    provider->complete(provider->exactRequests.constLast().requestId,
                       LyricsProvider::Result::technicalError(503));
    QCOMPARE(service.status(), LyricsService::Error);
}

void LyricsServiceTest::disabledServiceDoesNotPublishPositionDrivenLineChanges()
{
    PlaybackController playback;
    LyricsService service(nullptr, &playback, nullptr);
    QSignalSpy changed(&service, &LyricsService::currentLineChanged);
    emit playback.positionMsChanged();
    QCOMPARE(changed.count(), 0);
}

void LyricsServiceTest::enabledServiceDoesNotRepublishAnUnchangedLineOnEveryPositionTick()
{
    PlaybackController playback;
    LyricsService service(nullptr, &playback, nullptr);
    service.setEnabled(true);
    QSignalSpy changed(&service, &LyricsService::currentLineChanged);

    emit playback.positionMsChanged();
    emit playback.positionMsChanged();
    emit playback.positionMsChanged();

    QCOMPARE(changed.count(), 0);
}

void LyricsServiceTest::disablingLyricsHostPreservesResolvedPresentationState()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    auto* provider = new FakeLyricsProvider;
    LyricsService service(nullptr, nullptr, nullptr, provider);
    service.setEnabled(true);

    TrackRecord track;
    track.trackId = QStringLiteral("preserved-host-state");
    track.title = QStringLiteral("Preserved Host State");
    track.artist = QStringLiteral("Artist");
    track.path = directory.filePath(QStringLiteral("preserved.flac"));
    service.requestTrack(track);
    QCOMPARE(provider->exactRequests.size(), 1);

    LyricsProvider::Candidate value;
    value.title = track.title;
    value.artist = track.artist;
    value.source = lrclibSource();
    value.syncedLyrics = QStringLiteral("[00:01.00]Keep this resolved line");
    provider->complete(provider->exactRequests.constFirst().requestId,
                       LyricsProvider::Result::found(value));
    QCOMPARE(service.status(), LyricsService::Ready);
    QCOMPARE(service.sourceProvider(), QStringLiteral("LRCLIB"));
    QCOMPARE(qobject_cast<LyricsLineModel*>(service.lines())->rowCount(), 1);

    service.setEnabled(false);
    QCOMPARE(service.enabled(), false);
    QCOMPARE(service.status(), LyricsService::Ready);
    QCOMPARE(service.sourceProvider(), QStringLiteral("LRCLIB"));
    QCOMPARE(qobject_cast<LyricsLineModel*>(service.lines())->rowCount(), 1);

    service.setEnabled(true);
    QCOMPARE(service.status(), LyricsService::Ready);
    QCOMPARE(provider->exactRequests.size(), 1);
}

void LyricsServiceTest::emptyEmbeddedLyricsFallsThroughToProvider()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    auto* provider = new FakeLyricsProvider;
    LyricsService service(nullptr, nullptr, nullptr, provider);
    service.setEnabled(true);
    TrackRecord track; track.trackId = QStringLiteral("empty-embedded");
    track.title = QStringLiteral("Fallback");
    track.path = directory.filePath(QStringLiteral("fallback.flac"));
    service.requestTrack(track, QStringLiteral("[ar: Metadata only]"));
    QCOMPARE(provider->exactRequests.size(), 1);
    QCOMPARE(service.status(), LyricsService::Loading);
}

void LyricsServiceTest::embeddedLyricsRetainLocalSourceWithoutProviderRequest()
{
    auto* provider = new FakeLyricsProvider;
    LyricsService service(nullptr, nullptr, nullptr, provider);
    service.setEnabled(true);
    TrackRecord track;
    track.trackId = QStringLiteral("embedded");
    track.title = QStringLiteral("Embedded");
    service.requestTrack(track, QStringLiteral("[00:01.00]Embedded line"));
    QCOMPARE(service.status(), LyricsService::Ready);
    QCOMPARE(service.diagnostics().value(QStringLiteral("source")).toString(),
             QStringLiteral("embedded"));
    QVERIFY(provider->exactRequests.isEmpty());
}

void LyricsServiceTest::sidecarLyricsRetainLocalSourceWithoutProviderRequest()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    TrackRecord track;
    track.trackId = QStringLiteral("sidecar");
    track.title = QStringLiteral("Sidecar");
    track.path = directory.filePath(QStringLiteral("sidecar.flac"));
    { QFile audio(track.path); QVERIFY(audio.open(QIODevice::WriteOnly)); audio.write("x"); }
    { QFile lrc(directory.filePath(QStringLiteral("sidecar.lrc")));
      QVERIFY(lrc.open(QIODevice::WriteOnly)); lrc.write("[00:01.00]Sidecar line"); }
    auto* provider = new FakeLyricsProvider;
    LyricsService service(nullptr, nullptr, nullptr, provider);
    service.setEnabled(true);
    service.requestTrack(track);
    QCOMPARE(service.status(), LyricsService::Ready);
    QCOMPARE(service.diagnostics().value(QStringLiteral("source")).toString(),
             QStringLiteral("sidecar"));
    QVERIFY(provider->exactRequests.isEmpty());
}

void LyricsServiceTest::instrumentalResultIsCachedWithoutAFalseTimeline()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    TrackRecord track;
    track.trackId = QStringLiteral("instrumental");
    track.title = QStringLiteral("Instrumental");
    track.path = directory.filePath(QStringLiteral("instrumental.flac"));
    { QFile audio(track.path); QVERIFY(audio.open(QIODevice::WriteOnly)); audio.write("x"); }
    SettingsController settings;
    settings.setCacheDirectory(directory.filePath(QStringLiteral("cache")));
    auto* provider = new FakeLyricsProvider;
    LyricsService service(nullptr, nullptr, &settings, provider);
    service.setEnabled(true);
    service.requestTrack(track);
    LyricsProvider::Candidate candidate;
    candidate.title = track.title;
    candidate.source = lrclibSource();
    candidate.instrumental = true;
    candidate.plainLyrics = QStringLiteral("must not surface for instrumental tracks");
    QCOMPARE(provider->exactRequests.size(), 1);
    provider->complete(provider->exactRequests.constFirst().requestId,
                       LyricsProvider::Result::found(candidate));
    QCOMPARE(service.status(), LyricsService::Ready);
    QVERIFY(service.instrumental());
    QVERIFY(service.untimedLyrics().isEmpty());
    QCOMPARE(qobject_cast<LyricsLineModel*>(service.lines())->rowCount(), 0);
    const auto cached = LyricsCache(settings.cacheDirectory()).load(track);
    QVERIFY(cached.has_value());
    QCOMPARE(cached->source.providerId, QStringLiteral("lrclib"));
    QVERIFY(cached->instrumental);
    QVERIFY(!cached->synchronized);
    QVERIFY(cached->document.untimedText.isEmpty());
}

void LyricsServiceTest::cacheRejectsCorruptAndWrongSchemaEntries()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    TrackRecord track; track.trackId = QStringLiteral("schema");
    track.path = directory.filePath(QStringLiteral("schema.flac"));
    { QFile source(track.path); QVERIFY(source.open(QIODevice::WriteOnly)); source.write("x"); }
    LyricsCache cache(directory.path());
    LyricsCache::Entry entry; entry.source = manualSource();
    entry.document.lines = {{1000, QStringLiteral("ok")}};
    entry.synchronized = true;
    QVERIFY(cache.save(track, entry));

    const auto writeInvalid = [&cache, &track](const QByteArray& payload) {
        QFile file(cache.pathFor(track)); QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(file.write(payload), static_cast<qint64>(payload.size()));
    };
    writeInvalid(QByteArrayLiteral("{"));
    QVERIFY(!cache.load(track).has_value());
    writeInvalid(QByteArrayLiteral(R"({"version":2,"key":"bad"})"));
    QVERIFY(!cache.load(track).has_value());
    const QByteArray key = LyricsCache::keyFor(track).toUtf8();
    writeInvalid(QByteArrayLiteral("{\"version\":1,\"key\":\"") + key
                 + QByteArrayLiteral("\",\"source\":\"manual\",\"instrumental\":false,"
                                     "\"offsetMs\":0,\"untimedText\":\"\",\"lines\":\"bad\","
                                     "\"metadata\":{}}"));
    QVERIFY(!cache.load(track).has_value());
}

void LyricsServiceTest::cacheRejectsMalformedVersionTwoMetadata()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    TrackRecord track;
    track.trackId = QStringLiteral("invalid-v2");
    track.path = directory.filePath(QStringLiteral("invalid-v2.flac"));
    { QFile source(track.path); QVERIFY(source.open(QIODevice::WriteOnly)); source.write("x"); }
    LyricsCache cache(directory.path());

    const auto validObject = [&track] {
        return QJsonObject{{QStringLiteral("version"), 2},
                           {QStringLiteral("key"), LyricsCache::keyFor(track)},
                           {QStringLiteral("providerId"), QStringLiteral("unison")},
                           {QStringLiteral("providerName"), QStringLiteral("Unison")},
                           {QStringLiteral("sourceUrl"), QStringLiteral("https://unison.boidu.dev")},
                           {QStringLiteral("attribution"),
                            QStringLiteral("Lyrics from Unison (https://unison.boidu.dev)")},
                           {QStringLiteral("supportsSyncedLyrics"), true},
                           {QStringLiteral("synchronized"), true},
                           {QStringLiteral("instrumental"), false},
                           {QStringLiteral("offsetMs"), 0},
                           {QStringLiteral("untimedText"), QString()},
                           {QStringLiteral("lines"), QJsonArray{
                                QJsonObject{{QStringLiteral("timeMs"), 1000},
                                            {QStringLiteral("text"), QStringLiteral("line")}}}},
                           {QStringLiteral("metadata"), QJsonObject{}}};
    };
    const auto reject = [&cache, &track](const QJsonObject& object) {
        const QString path = cache.pathFor(track);
        QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        const QByteArray payload = QJsonDocument(object).toJson(QJsonDocument::Compact);
        QCOMPARE(file.write(payload), static_cast<qint64>(payload.size()));
        file.close();
        QVERIFY(!cache.load(track).has_value());
    };

    QJsonObject invalid = validObject();
    invalid.remove(QStringLiteral("providerId"));
    reject(invalid);
    invalid = validObject();
    invalid.insert(QStringLiteral("providerId"), QString());
    reject(invalid);
    invalid = validObject();
    invalid.insert(QStringLiteral("providerName"), 7);
    reject(invalid);
    invalid = validObject();
    invalid.insert(QStringLiteral("sourceUrl"), QJsonArray{});
    reject(invalid);
    invalid = validObject();
    invalid.insert(QStringLiteral("sourceUrl"), QStringLiteral("http://[invalid"));
    reject(invalid);
    invalid = validObject();
    invalid.insert(QStringLiteral("sourceUrl"), QString());
    reject(invalid);
    invalid = validObject();
    invalid.insert(QStringLiteral("attribution"), false);
    reject(invalid);
    invalid = validObject();
    invalid.insert(QStringLiteral("supportsSyncedLyrics"), QStringLiteral("true"));
    reject(invalid);
    invalid = validObject();
    invalid.insert(QStringLiteral("synchronized"), 1);
    reject(invalid);
    invalid = validObject();
    invalid.insert(QStringLiteral("metadata"), QJsonArray{});
    reject(invalid);

    LyricsCache::Entry invalidEntry;
    invalidEntry.document.lines = {{1000, QStringLiteral("line")}};
    invalidEntry.source = {QStringLiteral("unison"), QString(),
                           QUrl(QStringLiteral("https://unison.boidu.dev")), {}, true};
    invalidEntry.synchronized = true;
    QVERIFY(!cache.save(track, invalidEntry));
    invalidEntry.source = {QStringLiteral("unison"), QStringLiteral("Unison"), {}, {}, true};
    QVERIFY(!cache.save(track, invalidEntry));
}

void LyricsServiceTest::pauseFollowDefaultsToFiveSecondsAndNotifies()
{
    LyricsService service(nullptr, nullptr, nullptr);
    QSignalSpy changed(&service, &LyricsService::followPausedChanged);
    const qint64 before = service.clockMs();
    service.pauseFollow();
    QCOMPARE(changed.count(), 1);
    QVERIFY(service.followPausedUntilMs() >= before + 4900);
}

void LyricsServiceTest::injectedProviderRemainsBorrowed()
{
    auto* provider = new FakeLyricsProvider(this);
    {
        LyricsService service(nullptr, nullptr, nullptr, provider);
        QCOMPARE(provider->parent(), this);
    }
    QVERIFY(provider != nullptr);
}

void LyricsServiceTest::lrclibTransportBuildsRequestsAndHandlesCancelTimeoutAndRetryAfter()
{
    FakeNetworkAccessManager manager;
    LrclibProvider provider(&manager, nullptr, 1);
    QList<LyricsProvider::Result> results;
    connect(&provider, &LyricsProvider::finished, this,
            [&results](quint64, const LyricsProvider::Result& result) { results.append(result); });
    QCoreApplication::setApplicationVersion(QStringLiteral("9.9.9-test"));
    const LyricsProvider::Track track{QStringLiteral("Song"), QStringLiteral("Artist"),
                                       QStringLiteral("Album"), 201234, false};
    provider.requestExact(1, track);
    QCOMPARE(manager.requests.size(), 1);
    const QNetworkRequest exact = manager.requests.constLast().request;
    QCOMPARE(exact.url().host(), QStringLiteral("lrclib.net"));
    QCOMPARE(exact.url().path(), QStringLiteral("/api/get"));
    const QUrlQuery exactQuery(exact.url());
    QCOMPARE(exactQuery.queryItemValue(QStringLiteral("track_name")), track.title);
    QCOMPARE(exactQuery.queryItemValue(QStringLiteral("duration")), QStringLiteral("201"));
    QVERIFY(exact.rawHeader("User-Agent").contains("AgPlayer/9.9.9-test"));
    QCOMPARE(exact.priority(), QNetworkRequest::HighPriority);
    manager.requests.constLast().reply->respond(429, {}, {{"Retry-After", "7"}});
    QCOMPARE(results.constLast().kind, LyricsProvider::Result::RateLimited);
    QCOMPARE(results.constLast().retryAfterMs, 7000LL);

    provider.requestExact(4, track);
    const QDateTime retryDate = QDateTime::currentDateTimeUtc().addSecs(3);
    manager.requests.constLast().reply->respond(
        429, {}, {{"Retry-After", retryDate.toString(Qt::RFC2822Date).toLatin1()}});
    QCOMPARE(results.constLast().kind, LyricsProvider::Result::RateLimited);
    QVERIFY(results.constLast().retryAfterMs >= 0);
    QVERIFY(results.constLast().retryAfterMs <= 4000);

    LyricsProvider::Track lowPriority = track;
    lowPriority.lowPriority = true;
    provider.requestSearch(2, lowPriority);
    QCOMPARE(manager.requests.constLast().request.url().path(), QStringLiteral("/api/search"));
    QCOMPARE(manager.requests.constLast().request.priority(), QNetworkRequest::LowPriority);
    provider.cancel(2);
    QVERIFY(manager.requests.constLast().reply->aborted);

    provider.requestExact(3, track);
    QTRY_VERIFY(manager.requests.constLast().reply->aborted);
    QTRY_VERIFY(!results.isEmpty());
    QCOMPARE(results.constLast().diagnostic, QStringLiteral("timeout"));
}

void LyricsServiceTest::lrclibCandidatesCarryStableSourceMetadata()
{
    FakeNetworkAccessManager manager;
    LrclibProvider provider(&manager, nullptr, 1000);
    QList<LyricsProvider::Result> results;
    connect(&provider, &LyricsProvider::finished, this,
            [&results](quint64, const LyricsProvider::Result& result) { results.append(result); });
    const LyricsProvider::Track track{QStringLiteral("Song"), QStringLiteral("Artist")};
    provider.requestExact(1, track);
    manager.requests.constLast().reply->respond(200, QByteArrayLiteral(
        R"({"trackName":"Song","artistName":"Artist","albumName":"Album","duration":201,"instrumental":false,"syncedLyrics":"[00:01.00]line","plainLyrics":"line"})"));
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.constFirst().kind, LyricsProvider::Result::Found);
    const LyricsProvider::Source source = results.constFirst().candidate.source;
    QCOMPARE(source.providerId, QStringLiteral("lrclib"));
    QCOMPARE(source.providerName, QStringLiteral("LRCLIB"));
    QCOMPARE(source.sourceUrl, QUrl(QStringLiteral("https://lrclib.net")));
    QVERIFY(source.attribution.isEmpty());
    QVERIFY(source.supportsSyncedLyrics);
}

void LyricsServiceTest::malformedLrclibResponsesAreTechnicalErrors()
{
    FakeNetworkAccessManager manager;
    LrclibProvider provider(&manager, nullptr, 1000);
    QList<LyricsProvider::Result> results;
    connect(&provider, &LyricsProvider::finished, this,
            [&results](quint64, const LyricsProvider::Result& result) { results.append(result); });
    const LyricsProvider::Track track{QStringLiteral("Song"), QStringLiteral("Artist")};
    const auto verifyInvalid = [&provider, &manager, &results, &track](quint64 requestId,
                                                                         QByteArray payload) {
        provider.requestExact(requestId, track);
        manager.requests.constLast().reply->respond(200, std::move(payload));
        QCOMPARE(results.constLast().kind, LyricsProvider::Result::TechnicalError);
        QCOMPARE(results.constLast().diagnostic, QStringLiteral("invalid-response"));
    };
    verifyInvalid(1, QByteArrayLiteral("{}"));
    verifyInvalid(2, QByteArrayLiteral(
        R"({"trackName":"Song","artistName":"Artist","instrumental":false,"syncedLyrics":null})"));
    verifyInvalid(3, QByteArrayLiteral(
        R"({"trackName":"Song","artistName":"Artist","instrumental":"false","syncedLyrics":null,"plainLyrics":null})"));

    provider.requestExact(4, track);
    manager.requests.constLast().reply->respond(200, QByteArrayLiteral(
        R"({"trackName":"Song","artistName":"Artist","instrumental":false,"syncedLyrics":null,"plainLyrics":null})"));
    QCOMPARE(results.constLast().kind, LyricsProvider::Result::Found);
}

void LyricsServiceTest::repeatedInvalidResponsesDoNotGloballyBlackoutRetry()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    auto* provider = new FakeLyricsProvider;
    LyricsService service(nullptr, nullptr, nullptr, provider);
    service.setEnabled(true);
    TrackRecord track; track.trackId = QStringLiteral("invalid"); track.title = QStringLiteral("Invalid");
    track.path = directory.filePath(QStringLiteral("invalid.flac"));
    service.requestTrack(track);
    for (int attempt = 0; attempt < 3; ++attempt) {
        QCOMPARE(provider->exactRequests.size(), attempt + 1);
        provider->complete(provider->exactRequests.constLast().requestId,
            LyricsProvider::Result::technicalError(200, false, QStringLiteral("invalid-response")));
        if (attempt < 2) service.retry();
    }
    QCOMPARE(service.status(), LyricsService::Error);
    QCOMPARE(provider->exactRequests.size(), 3);
    service.retry();
    QCOMPARE(provider->exactRequests.size(), 4);
    QCOMPARE(service.status(), LyricsService::Loading);
}

void LyricsServiceTest::defaultServiceExposesOrderedProviderRoutesWithoutRequest()
{
    LyricsService service(nullptr, nullptr, nullptr);
    QCOMPARE(service.diagnostics().value(QStringLiteral("providerRoutes")).toStringList(),
             QStringList({QStringLiteral("lrcapi"), QStringLiteral("lrclib"),
                          QStringLiteral("unison"),
                          QStringLiteral("lyrics-ovh")}));
}

void LyricsServiceTest::lowMatchLrclibSearchFallsThroughToUnison()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    FakeLyricsProvider lrclib;
    FakeLyricsProvider unison;
    FakeLyricsProvider ovh;
    const auto matcher = [](const LyricsProvider::Track& track,
                            const QList<LyricsProvider::Candidate>& candidates) {
        return std::any_of(candidates.cbegin(), candidates.cend(),
                           [&track](const LyricsProvider::Candidate& candidate) {
            return candidate.title.compare(track.title, Qt::CaseInsensitive) == 0
                && candidate.artist.compare(track.artist, Qt::CaseInsensitive) == 0;
        });
    };
    LyricsProviderChain chain({{QStringLiteral("lrclib"), QStringLiteral("LRCLIB"), &lrclib},
                               {QStringLiteral("unison"), QStringLiteral("Unison"), &unison},
                               {QStringLiteral("lyrics-ovh"), QStringLiteral("lyrics.ovh"), &ovh}},
                              nullptr, {}, matcher);
    LyricsService service(nullptr, nullptr, nullptr, &chain);
    service.setEnabled(true);
    TrackRecord track;
    track.trackId = QStringLiteral("search-fallback");
    track.title = QStringLiteral("Wanted Song");
    track.artist = QStringLiteral("Wanted Artist");
    track.durationMs = 180000;
    track.path = directory.filePath(QStringLiteral("search-fallback.flac"));
    service.requestTrack(track);

    QCOMPARE(lrclib.exactRequests.size(), 1);
    lrclib.complete(lrclib.exactRequests.constFirst().requestId,
                    LyricsProvider::Result::notFound());
    QCOMPARE(unison.exactRequests.size(), 1);
    unison.complete(unison.exactRequests.constFirst().requestId,
                    LyricsProvider::Result::notFound());
    QCOMPARE(ovh.exactRequests.size(), 1);
    ovh.complete(ovh.exactRequests.constFirst().requestId,
                 LyricsProvider::Result::notFound());
    QCOMPARE(lrclib.searchRequests.size(), 1);

    LyricsProvider::Candidate lowMatch;
    lowMatch.title = QStringLiteral("Wrong Song");
    lowMatch.artist = track.artist;
    lowMatch.durationSeconds = 180;
    lowMatch.syncedLyrics = QStringLiteral("[00:01.00]Wrong match");
    lrclib.complete(lrclib.searchRequests.constFirst().requestId,
                    LyricsProvider::Result::search({lowMatch}));
    QCOMPARE(unison.searchRequests.size(), 1);

    LyricsProvider::Candidate accepted;
    accepted.title = track.title;
    accepted.artist = track.artist;
    accepted.durationSeconds = 180;
    accepted.source = unisonSource();
    accepted.syncedLyrics = QStringLiteral("[00:01.00]Accepted by Unison");
    unison.complete(unison.searchRequests.constFirst().requestId,
                    LyricsProvider::Result::search({accepted}));
    QCOMPARE(service.status(), LyricsService::Ready);
    QCOMPARE(service.sourceProvider(), QStringLiteral("Unison"));
    QCOMPARE(service.sourceAttribution(), accepted.source.attribution);
    QVERIFY(service.synchronizedLyrics());
}

void LyricsServiceTest::plainLyricsExposeUntimedSourceState()
{
    QTemporaryDir trackDirectory;
    QVERIFY(trackDirectory.isValid());
    SettingsController settings;
    settings.setCacheDirectory(trackDirectory.filePath(QStringLiteral("cache")));
    auto* provider = new FakeLyricsProvider;
    LyricsService service(nullptr, nullptr, &settings, provider);
    service.setEnabled(true);
    TrackRecord track; track.trackId = QStringLiteral("plain"); track.title = QStringLiteral("Plain");
    track.artist = QStringLiteral("Artist");
    track.path = trackDirectory.filePath(QStringLiteral("plain.flac"));
    service.requestTrack(track);
    LyricsProvider::Candidate value;
    value.source = lyricsOvhSource();
    value.title = track.title;
    value.artist = track.artist;
    value.plainLyrics = QStringLiteral("first complete line\nsecond complete line");
    QCOMPARE(provider->exactRequests.size(), 1);
    provider->complete(provider->exactRequests.constFirst().requestId,
                       LyricsProvider::Result::found(value));
    QCOMPARE(service.status(), LyricsService::Ready);
    QCOMPARE(service.sourceProvider(), QStringLiteral("lyrics.ovh"));
    QVERIFY(service.sourceAttribution().isEmpty());
    QVERIFY(!service.synchronizedLyrics());
    QCOMPARE(service.untimedLyrics(), value.plainLyrics);

    auto* cachedProvider = new FakeLyricsProvider;
    LyricsService cached(nullptr, nullptr, &settings, cachedProvider);
    cached.setEnabled(true);
    cached.requestTrack(track);
    QCOMPARE(cached.status(), LyricsService::Ready);
    QCOMPARE(cached.sourceProvider(), QStringLiteral("lyrics.ovh"));
    QVERIFY(cached.sourceAttribution().isEmpty());
    QVERIFY(!cached.synchronizedLyrics());
    QCOMPARE(cached.untimedLyrics(), value.plainLyrics);
    QCOMPARE(qobject_cast<LyricsLineModel*>(cached.lines())->rowCount(), 0);
    QVERIFY(cachedProvider->exactRequests.isEmpty());

    service.retry();
    QVERIFY(service.sourceProvider().isEmpty());
    QVERIFY(service.untimedLyrics().isEmpty());
    QVERIFY(service.routeAttempts().isEmpty());
}

void LyricsServiceTest::prefetchFailureAndSuccessDoNotMutateForegroundPresentation()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString fixture = QDir(QCoreApplication::applicationDirPath())
                                .filePath(QStringLiteral("fixtures/sine-440hz.wav"));
    QVERIFY2(QFileInfo::exists(fixture), qPrintable(fixture));
    LibraryModel library;
    TrackRecord current;
    current.trackId = QStringLiteral("foreground");
    current.title = QStringLiteral("Foreground");
    current.artist = QStringLiteral("Artist");
    current.path = directory.filePath(QStringLiteral("foreground.wav"));
    current.available = true;
    TrackRecord next;
    next.trackId = QStringLiteral("prefetch");
    next.title = QStringLiteral("Prefetch");
    next.artist = QStringLiteral("Artist");
    next.path = directory.filePath(QStringLiteral("prefetch.wav"));
    next.available = true;
    QVERIFY(QFile::copy(fixture, current.path));
    QVERIFY(QFile::copy(fixture, next.path));
    QVERIFY(library.append(current));
    QVERIFY(library.append(next));

    ag_player_config config{AG_AUDIO_BACKEND_NULL, 2048};
    ag_player* core = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &core), AG_OK);
    {
        PlaybackController playback(core, &library);
        QVERIFY(playback.restoreQueue({current.trackId, next.trackId}, current.trackId));
        QTRY_COMPARE(playback.currentTrackId(), current.trackId);

        FakeLyricsProvider lrclib;
        FakeLyricsProvider unison;
        FakeLyricsProvider ovh;
        LyricsProviderChain chain({{QStringLiteral("lrclib"), QStringLiteral("LRCLIB"), &lrclib},
                                   {QStringLiteral("unison"), QStringLiteral("Unison"), &unison},
                                   {QStringLiteral("lyrics-ovh"), QStringLiteral("lyrics.ovh"), &ovh}});
        SettingsController settings;
        settings.setCacheDirectory(directory.filePath(QStringLiteral("cache")));
        LyricsService service(&library, &playback, &settings, &chain);
        service.setEnabled(true);
        QCOMPARE(lrclib.exactRequests.size(), 2);
        const auto prefetchRequest = std::find_if(
            lrclib.exactRequests.cbegin(), lrclib.exactRequests.cend(),
            [](const FakeLyricsProvider::Request& request) { return request.track.lowPriority; });
        QVERIFY(prefetchRequest != lrclib.exactRequests.cend());
        const auto foregroundRequest = std::find_if(
            lrclib.exactRequests.cbegin(), lrclib.exactRequests.cend(),
            [](const FakeLyricsProvider::Request& request) { return !request.track.lowPriority; });
        QVERIFY(foregroundRequest != lrclib.exactRequests.cend());

        lrclib.complete(prefetchRequest->requestId,
                        LyricsProvider::Result::technicalError(
                            503, false, QStringLiteral("provider-error")));
        QVERIFY(service.routeNotice().isEmpty());
        QCOMPARE(service.status(), LyricsService::Loading);
        QCOMPARE(unison.exactRequests.size(), 1);
        QVERIFY(unison.exactRequests.constFirst().track.lowPriority);
        LyricsProvider::Candidate prefetched;
        prefetched.title = next.title;
        prefetched.artist = next.artist;
        prefetched.source = unisonSource();
        prefetched.syncedLyrics = QStringLiteral("[00:01.00]Prefetched only");
        QCOMPARE(unison.exactRequests.size(), 1);
        unison.complete(unison.exactRequests.constFirst().requestId,
                        LyricsProvider::Result::found(prefetched));
        QCOMPARE(service.status(), LyricsService::Loading);
        QVERIFY(service.sourceProvider().isEmpty());
        QCOMPARE(qobject_cast<LyricsLineModel*>(service.lines())->rowCount(), 0);
        QVERIFY(service.routeNotice().isEmpty());
        const auto cachedPrefetch = LyricsCache(settings.cacheDirectory()).load(next);
        QVERIFY(cachedPrefetch.has_value());
        QCOMPARE(cachedPrefetch->document.lines.constFirst().text,
                 QStringLiteral("Prefetched only"));

        LyricsProvider::Candidate foreground;
        foreground.title = current.title;
        foreground.artist = current.artist;
        foreground.source = lrclibSource();
        foreground.syncedLyrics = QStringLiteral("[00:01.00]Foreground result");
        lrclib.complete(foregroundRequest->requestId,
                        LyricsProvider::Result::found(foreground));
        QCOMPARE(service.status(), LyricsService::Ready);
        QCOMPARE(service.sourceProvider(), QStringLiteral("LRCLIB"));
    }
    ag_player_destroy(core);
}

void LyricsServiceTest::canceledLatePreviousTrackResultCannotMutateReplacementTrack()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    auto* provider = new FakeLyricsProvider;
    LyricsService service(nullptr, nullptr, nullptr, provider);
    service.setEnabled(true);
    TrackRecord first;
    first.trackId = QStringLiteral("first-late");
    first.title = QStringLiteral("First Late");
    first.path = directory.filePath(QStringLiteral("first-late.flac"));
    TrackRecord second;
    second.trackId = QStringLiteral("second-current");
    second.title = QStringLiteral("Second Current");
    second.path = directory.filePath(QStringLiteral("second-current.flac"));
    service.requestTrack(first);
    QCOMPARE(provider->exactRequests.size(), 1);
    const quint64 staleRequest = provider->exactRequests.constFirst().requestId;
    service.requestTrack(second);
    QCOMPARE(provider->exactRequests.size(), 2);
    QVERIFY(provider->canceledRequests.contains(staleRequest));

    LyricsProvider::Candidate stale;
    stale.title = first.title;
    stale.source = lrclibSource();
    stale.syncedLyrics = QStringLiteral("[00:01.00]Stale previous track");
    provider->complete(staleRequest, LyricsProvider::Result::found(stale));
    QCOMPARE(service.status(), LyricsService::Loading);
    QVERIFY(service.sourceProvider().isEmpty());
    QCOMPARE(qobject_cast<LyricsLineModel*>(service.lines())->rowCount(), 0);

    LyricsProvider::Candidate current;
    current.title = second.title;
    current.source = unisonSource();
    current.syncedLyrics = QStringLiteral("[00:01.00]Current track");
    QCOMPARE(provider->exactRequests.size(), 2);
    provider->complete(provider->exactRequests.constLast().requestId,
                       LyricsProvider::Result::found(current));
    QCOMPARE(service.status(), LyricsService::Ready);
    QCOMPARE(service.sourceProvider(), QStringLiteral("Unison"));
}

void LyricsServiceTest::previousGenerationWithSameTrackIdCannotOverwriteCurrentRequest()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    auto* provider = new FakeLyricsProvider;
    LyricsService service(nullptr, nullptr, nullptr, provider);
    service.setEnabled(true);
    TrackRecord oldGeneration;
    oldGeneration.trackId = QStringLiteral("same-id");
    oldGeneration.title = QStringLiteral("Old Generation");
    oldGeneration.path = directory.filePath(QStringLiteral("old.flac"));
    TrackRecord currentGeneration = oldGeneration;
    currentGeneration.title = QStringLiteral("Current Generation");
    currentGeneration.path = directory.filePath(QStringLiteral("current.flac"));
    service.requestTrack(oldGeneration);
    QCOMPARE(provider->exactRequests.size(), 1);
    const quint64 staleRequest = provider->exactRequests.constFirst().requestId;
    service.requestTrack(currentGeneration);
    QCOMPARE(provider->exactRequests.size(), 2);

    LyricsProvider::Candidate stale;
    stale.title = oldGeneration.title;
    stale.source = lrclibSource();
    stale.plainLyrics = QStringLiteral("stale generation text");
    provider->complete(staleRequest, LyricsProvider::Result::found(stale));
    QCOMPARE(service.status(), LyricsService::Loading);
    QVERIFY(service.untimedLyrics().isEmpty());
    QVERIFY(service.sourceProvider().isEmpty());
}

void LyricsServiceTest::newerRouteNoticeOutlivesOlderNoticeTimer()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    FakeLyricsProvider lrclib;
    FakeLyricsProvider unison;
    FakeLyricsProvider ovh;
    LyricsProviderChain chain({{QStringLiteral("lrclib"), QStringLiteral("LRCLIB"), &lrclib},
                               {QStringLiteral("unison"), QStringLiteral("Unison"), &unison},
                               {QStringLiteral("lyrics-ovh"), QStringLiteral("lyrics.ovh"), &ovh}});
    LyricsService service(nullptr, nullptr, nullptr, &chain);
    service.setEnabled(true);
    TrackRecord track;
    track.trackId = QStringLiteral("notice-timer");
    track.title = QStringLiteral("Notice Timer");
    track.path = directory.filePath(QStringLiteral("notice-timer.flac"));
    service.requestTrack(track);
    QCOMPARE(lrclib.exactRequests.size(), 1);
    lrclib.complete(lrclib.exactRequests.constFirst().requestId,
                    LyricsProvider::Result::technicalError(
                        503, false, QStringLiteral("provider-error")));
    QCOMPARE(service.routeNotice().value(QStringLiteral("providerId")).toString(),
             QStringLiteral("lrclib"));
    QTest::qWait(3100);
    QCOMPARE(unison.exactRequests.size(), 1);
    unison.complete(unison.exactRequests.constFirst().requestId,
                    LyricsProvider::Result::technicalError(
                        502, false, QStringLiteral("provider-error")));
    QCOMPARE(service.routeNotice().value(QStringLiteral("providerId")).toString(),
             QStringLiteral("unison"));
    QTest::qWait(1200);
    QCOMPARE(service.routeNotice().value(QStringLiteral("providerId")).toString(),
             QStringLiteral("unison"));
    QStringList actualKeys = service.routeNotice().keys();
    actualKeys.sort();
    QStringList safeKeys{QStringLiteral("diagnostic"), QStringLiteral("httpStatus"),
                         QStringLiteral("offline"), QStringLiteral("providerId"),
                         QStringLiteral("providerName"), QStringLiteral("retryAfterMs")};
    safeKeys.sort();
    QCOMPARE(actualKeys, safeKeys);
}

void LyricsServiceTest::resultWithRouteAttemptsSurvivesProviderSignal()
{
    FakeLyricsProvider provider;
    QList<LyricsProvider::Result> received;
    connect(&provider, &LyricsProvider::finished, this,
            [&received](quint64, const LyricsProvider::Result& result) { received.append(result); });
    LyricsProvider::Result result = LyricsProvider::Result::notFound();
    result.attempts = {{QStringLiteral("lrclib"), QStringLiteral("LRCLIB"),
                        QStringLiteral("not-found"), 404}};
    provider.complete(7, result);
    QCOMPARE(received.size(), 1);
    QCOMPARE(received.constFirst().attempts.size(), 1);
    QCOMPARE(received.constFirst().attempts.constFirst().providerId,
             QStringLiteral("lrclib"));
}

void LyricsServiceTest::serviceIgnoresAttemptResultWithoutPendingRequest()
{
    auto* provider = new FakeLyricsProvider;
    LyricsService service(nullptr, nullptr, nullptr, provider);
    LyricsProvider::Result result = LyricsProvider::Result::notFound();
    result.attempts = {{QStringLiteral("lrclib"), QStringLiteral("LRCLIB"),
                        QStringLiteral("not-found"), 404}};
    provider->complete(77, result);
    QCOMPARE(service.status(), LyricsService::Idle);
}

void LyricsServiceTest::finalNotFoundKeepsCollapsedSafeRouteAttempts()
{
    QTemporaryDir trackDirectory;
    QVERIFY(trackDirectory.isValid());
    auto* provider = new FakeLyricsProvider;
    LyricsService service(nullptr, nullptr, nullptr, provider);
    service.setEnabled(true);
    TrackRecord track; track.trackId = QStringLiteral("attempts"); track.title = QStringLiteral("Attempts");
    track.path = trackDirectory.filePath(QStringLiteral("attempts.flac"));
    service.requestTrack(track);
    QCOMPARE(provider->exactRequests.size(), 1);

    LyricsProvider::Result exact = LyricsProvider::Result::notFound();
    exact.attempts = {
        {QStringLiteral("lrclib"), QStringLiteral("LRCLIB"),
         QStringLiteral("not-found"), 404},
        {QStringLiteral("unison"), QStringLiteral("Unison"),
         QStringLiteral("not-found"), 404},
        {QStringLiteral("lyrics-ovh"), QStringLiteral("lyrics.ovh"),
         QStringLiteral("not-found"), 404}};
    provider->complete(provider->exactRequests.constFirst().requestId, exact);
    QCOMPARE(provider->searchRequests.size(), 1);
    LyricsProvider::Result search = LyricsProvider::Result::notFound();
    search.attempts = {
        {QStringLiteral("lrclib"), QStringLiteral("LRCLIB"),
         QStringLiteral("not-found"), 404},
        {QStringLiteral("unison"), QStringLiteral("Unison"),
         QStringLiteral("empty-search"), 200},
        {QStringLiteral("lyrics-ovh"), QStringLiteral("lyrics.ovh"),
         QStringLiteral("provider-error"), 503}};
    provider->complete(provider->searchRequests.constFirst().requestId, search);

    QCOMPARE(service.status(), LyricsService::NotFound);
    const QVariantList rows = service.routeAttempts();
    QCOMPARE(rows.size(), 3);
    const QVariantMap lrclib = rows.constFirst().toMap();
    QCOMPARE(lrclib.value(QStringLiteral("diagnostic")).toString(),
             QStringLiteral("not-found"));
    QVERIFY(!lrclib.contains(QStringLiteral("url")));
    QVERIFY(!lrclib.contains(QStringLiteral("path")));
    const QVariantMap ovh = rows.constLast().toMap();
    QCOMPARE(ovh.value(QStringLiteral("providerId")).toString(),
             QStringLiteral("lyrics-ovh"));
    QCOMPARE(ovh.value(QStringLiteral("diagnostic")).toString(),
             QStringLiteral("provider-error"));
    for (const QVariant& row : rows) {
        QStringList actualKeys = row.toMap().keys();
        actualKeys.sort();
        QStringList safeKeys{QStringLiteral("diagnostic"), QStringLiteral("httpStatus"),
                             QStringLiteral("offline"), QStringLiteral("providerId"),
                             QStringLiteral("providerName"), QStringLiteral("retryAfterMs")};
        safeKeys.sort();
        QCOMPARE(actualKeys, safeKeys);
    }
}

void LyricsServiceTest::providerChainPreservesChildRouteAttempts()
{
    FakeLyricsProvider first;
    FakeLyricsProvider second;
    LyricsProviderChain chain({{QStringLiteral("first"), QStringLiteral("First"), &first},
                               {QStringLiteral("second"), QStringLiteral("Second"), &second}});
    QList<LyricsProvider::Result> results;
    connect(&chain, &LyricsProvider::finished, this,
            [&results](quint64, const LyricsProvider::Result& result) {
        results.append(result);
    });
    const LyricsProvider::Track track{QStringLiteral("Child Attempts"),
                                       QStringLiteral("Artist")};
    chain.requestExact(91, track);
    QCOMPARE(first.exactRequests.size(), 1);
    LyricsProvider::Result firstResult = LyricsProvider::Result::notFound();
    firstResult.attempts = {{QStringLiteral("first-transport"),
                             QStringLiteral("First transport"),
                             QStringLiteral("network-unavailable"), 0, 0, true}};
    first.complete(first.exactRequests.constFirst().requestId, firstResult);
    QCOMPARE(second.exactRequests.size(), 1);
    LyricsProvider::Candidate candidate;
    candidate.title = track.title;
    candidate.artist = track.artist;
    candidate.source = unisonSource();
    candidate.plainLyrics = QStringLiteral("child success");
    LyricsProvider::Result secondResult = LyricsProvider::Result::found(candidate);
    secondResult.attempts = {{QStringLiteral("second-transport"),
                              QStringLiteral("Second transport"),
                              QStringLiteral("recovered"), 200}};
    second.complete(second.exactRequests.constFirst().requestId, secondResult);
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.constFirst().attempts.size(), 3);
    QCOMPARE(results.constFirst().attempts.at(0).providerId, QStringLiteral("first"));
    QCOMPARE(results.constFirst().attempts.at(1).providerId,
             QStringLiteral("first-transport"));
    QCOMPARE(results.constFirst().attempts.at(2).providerId,
             QStringLiteral("second-transport"));
}

void LyricsServiceTest::partialExactOutageStillFallsBackToSearch()
{
    QTemporaryDir trackDirectory;
    QVERIFY(trackDirectory.isValid());
    FakeLyricsProvider first;
    FakeLyricsProvider second;
    LyricsProviderChain chain({{QStringLiteral("first"), QStringLiteral("First"), &first},
                               {QStringLiteral("second"), QStringLiteral("Second"), &second}});
    LyricsService service(nullptr, nullptr, nullptr, &chain);
    service.setEnabled(true);
    TrackRecord track;
    track.trackId = QStringLiteral("partial-exact-outage");
    track.title = QStringLiteral("Fallback Search");
    track.artist = QStringLiteral("Artist");
    track.durationMs = 180000;
    track.path = trackDirectory.filePath(QStringLiteral("partial-exact-outage.flac"));

    service.requestTrack(track);
    QCOMPARE(first.exactRequests.size(), 1);
    first.complete(first.exactRequests.constFirst().requestId,
                   LyricsProvider::Result::technicalError(
                       503, false, QStringLiteral("provider-error")));
    QCOMPARE(service.status(), LyricsService::Loading);
    QCOMPARE(service.routeNotice().value(QStringLiteral("providerId")).toString(),
             QStringLiteral("first"));
    QCOMPARE(second.exactRequests.size(), 1);

    second.complete(second.exactRequests.constFirst().requestId,
                    LyricsProvider::Result::notFound());
    QCOMPARE(first.searchRequests.size(), 1);

    LyricsProvider::Candidate value;
    value.source = unisonSource();
    value.title = track.title;
    value.artist = track.artist;
    value.durationSeconds = 180;
    value.syncedLyrics = QStringLiteral("[00:01.00]Found by broad search");
    first.complete(first.searchRequests.constFirst().requestId,
                   LyricsProvider::Result::search({value}));
    QCOMPARE(service.status(), LyricsService::Ready);
    QCOMPARE(service.sourceProvider(), QStringLiteral("Unison"));
}

void LyricsServiceTest::chainRouteFailureIsForegroundOnlyAndSuccessPreservesSource()
{
    QTemporaryDir trackDirectory;
    QVERIFY(trackDirectory.isValid());
    FakeLyricsProvider lrclib;
    FakeLyricsProvider unison;
    FakeLyricsProvider ovh;
    LyricsProviderChain chain({{QStringLiteral("lrclib"), QStringLiteral("LRCLIB"), &lrclib},
                               {QStringLiteral("unison"), QStringLiteral("Unison"), &unison},
                               {QStringLiteral("lyrics-ovh"), QStringLiteral("lyrics.ovh"), &ovh}});
    LyricsService service(nullptr, nullptr, nullptr, &chain);
    service.setEnabled(true);
    TrackRecord track; track.trackId = QStringLiteral("chain"); track.title = QStringLiteral("Chain");
    track.artist = QStringLiteral("Artist");
    track.path = trackDirectory.filePath(QStringLiteral("chain.flac"));
    service.requestTrack(track);
    QCOMPARE(lrclib.exactRequests.size(), 1);
    lrclib.complete(lrclib.exactRequests.constFirst().requestId,
                    LyricsProvider::Result::technicalError(503, false,
                                                          QStringLiteral("provider-error")));
    QCOMPARE(service.status(), LyricsService::Loading);
    QCOMPARE(service.routeNotice().value(QStringLiteral("providerId")).toString(),
             QStringLiteral("lrclib"));
    QCOMPARE(unison.exactRequests.size(), 1);

    LyricsProvider::Candidate value;
    value.source = unisonSource();
    value.title = track.title;
    value.artist = track.artist;
    value.syncedLyrics = QStringLiteral("[00:01.00]Found by Unison");
    unison.complete(unison.exactRequests.constFirst().requestId,
                    LyricsProvider::Result::found(value));
    QCOMPARE(service.status(), LyricsService::Ready);
    QCOMPARE(service.sourceProvider(), QStringLiteral("Unison"));
    QCOMPARE(service.sourceAttribution(), value.source.attribution);
    QVERIFY(service.synchronizedLyrics());
}

QTEST_MAIN(LyricsServiceTest)
#include "lyrics_service_test.moc"
