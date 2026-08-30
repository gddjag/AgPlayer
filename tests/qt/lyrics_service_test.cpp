#include "library_model.hpp"
#include "lyrics_cache.hpp"
#include "lyrics_line_model.hpp"
#include "lyrics_provider.hpp"
#include "lyrics_service.hpp"
#include "playback_controller.hpp"
#include "settings_controller.hpp"

#include <QFile>
#include <QFileInfo>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QTemporaryDir>
#include <QUrlQuery>
#include <QtTest>

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
    void cancelTimeoutAndTechnicalFailuresDegradeWithoutCountingNotFound();
    void retryAfterBlocksManualRetryWithoutIncreasingFailures();
    void sameTitleAndArtistWithMateriallyWrongDurationIsRejected();
    void confirmedNotFoundResetsTechnicalFailureStreak();
    void disabledServiceDoesNotPublishPositionDrivenLineChanges();
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
    void threeInvalidProviderResponsesDegradeService();
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
    online->complete(online->exactRequests.constFirst().requestId,
                     LyricsProvider::Result::found(candidate));
    QCOMPARE(qobject_cast<LyricsLineModel*>(fetched.lines())->rowCount(), 1);
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
    QCOMPARE(qobject_cast<LyricsLineModel*>(reused.lines())->rowCount(), 1);
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

void LyricsServiceTest::sameTitleAndArtistWithMateriallyWrongDurationIsRejected()
{
    auto* provider = new FakeLyricsProvider;
    LyricsService service(nullptr, nullptr, nullptr, provider);
    service.setEnabled(true);
    TrackRecord track; track.trackId = QStringLiteral("duration");
    track.title = QStringLiteral("Same Song"); track.artist = QStringLiteral("Same Artist");
    track.durationMs = 180000;
    service.requestTrack(track);
    provider->complete(provider->exactRequests.constFirst().requestId,
                       LyricsProvider::Result::notFound());
    LyricsProvider::Candidate candidate;
    candidate.title = track.title; candidate.artist = track.artist;
    candidate.durationSeconds = 900; candidate.syncedLyrics = QStringLiteral("[00:01]Wrong cut");
    provider->complete(provider->searchRequests.constFirst().requestId,
                       LyricsProvider::Result::search({candidate}));
    QCOMPARE(service.status(), LyricsService::NotFound);
}

void LyricsServiceTest::confirmedNotFoundResetsTechnicalFailureStreak()
{
    auto* provider = new FakeLyricsProvider;
    LyricsService service(nullptr, nullptr, nullptr, provider);
    service.setEnabled(true);
    TrackRecord track; track.trackId = QStringLiteral("health"); track.title = QStringLiteral("Health");
    service.requestTrack(track);
    for (int attempt = 0; attempt < 2; ++attempt) {
        provider->complete(provider->exactRequests.constLast().requestId,
                           LyricsProvider::Result::technicalError(503));
        service.retry();
    }
    QCOMPARE(service.consecutiveTechnicalFailures(), 2);
    provider->complete(provider->exactRequests.constLast().requestId,
                       LyricsProvider::Result::notFound());
    provider->complete(provider->searchRequests.constLast().requestId,
                       LyricsProvider::Result::notFound());
    QCOMPARE(service.consecutiveTechnicalFailures(), 0);
    service.retry();
    provider->complete(provider->exactRequests.constLast().requestId,
                       LyricsProvider::Result::technicalError(503));
    QCOMPARE(service.consecutiveTechnicalFailures(), 1);
    QCOMPARE(service.degradedUntilMs(), 0LL);
}

void LyricsServiceTest::disabledServiceDoesNotPublishPositionDrivenLineChanges()
{
    PlaybackController playback;
    LyricsService service(nullptr, &playback, nullptr);
    QSignalSpy changed(&service, &LyricsService::currentLineChanged);
    emit playback.positionMsChanged();
    QCOMPARE(changed.count(), 0);
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
    provider->complete(provider->exactRequests.constFirst().requestId,
                       LyricsProvider::Result::found(candidate));
    QCOMPARE(service.status(), LyricsService::Ready);
    QVERIFY(service.instrumental());
    QCOMPARE(qobject_cast<LyricsLineModel*>(service.lines())->rowCount(), 0);
    const auto cached = LyricsCache(settings.cacheDirectory()).load(track);
    QVERIFY(cached.has_value());
    QCOMPARE(cached->source.providerId, QStringLiteral("lrclib"));
    QVERIFY(cached->instrumental);
    QVERIFY(!cached->synchronized);
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

void LyricsServiceTest::threeInvalidProviderResponsesDegradeService()
{
    auto* provider = new FakeLyricsProvider;
    LyricsService service(nullptr, nullptr, nullptr, provider);
    service.setEnabled(true);
    TrackRecord track; track.trackId = QStringLiteral("invalid"); track.title = QStringLiteral("Invalid");
    service.requestTrack(track);
    for (int attempt = 0; attempt < 3; ++attempt) {
        provider->complete(provider->exactRequests.constLast().requestId,
            LyricsProvider::Result::technicalError(200, false, QStringLiteral("invalid-response")));
        if (attempt < 2) service.retry();
    }
    QCOMPARE(service.consecutiveTechnicalFailures(), 3);
    QVERIFY(service.degradedUntilMs() >= service.clockMs() + 19 * 60 * 1000);
    QCOMPARE(service.status(), LyricsService::Offline);
}

QTEST_MAIN(LyricsServiceTest)
#include "lyrics_service_test.moc"
