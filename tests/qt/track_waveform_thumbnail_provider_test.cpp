#include "track_waveform_thumbnail_provider.hpp"
#include "waveform_cache.hpp"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QVariantMap>

#include <array>
#include <cstdint>
#include <limits>
#include <vector>

class TrackWaveformThumbnailProviderTest final : public QObject {
    Q_OBJECT

private slots:
    void quantizesHighDensitySymmetricEnvelope();
    void ignoresNonFiniteSamplesAndClampsFiniteAmplitude();
    void downscalesBandEnergyWithAmplitudeWeighting();
    void readsExistingCacheWithoutChangingIt();
    void cacheMissRequestsLowPriorityAnalysis();
    void returnsEmptyForMissCorruptionMismatchAndEmptyMix();
    void coalescesSameTrackAndPublishesOnlyLatestGeneration();
    void cancelSuppressesMatchingGeneration();
    void canceledOffscreenReadDoesNotBlockVisibleRequest();
    void retainsAtMost256SuccessfulTracks();
    void destructionWaitsSafelyForOutstandingRead();
    void queueFullCallbackReentryPreservesBoundsAndLatestRequest();
    void repeatedMissUsesShortInvalidatableCooldown();
    void repeatedCorruptCacheUsesCooldown();
    void negativeCooldownRetainsAtMost256Sources();
    void cacheDirectoryEpochInvalidatesNegativeCooldown();
    void sourceInvalidationRefreshesOnlyMatchingMiss();
    void newerSourceForActiveTrackPublishesOnlyLatestFixture();
    void cacheDirectoryChangeSuppressesActiveOldFixture();
    void prefersAverageThenFallsBackToRmsAndLegacy();
    void sourceMismatchedAverageFallsBackToValidRms();
    void droppedGenerationHasNoDelayedPublicationAfterReplacement();
    void canceledCooldownRequestHasNoDelayedPublication();
    void refreshThenCacheHitHasNoStaleCooldownPublication();
    void tenThousandOverflowRequestsLeaveOnlyBoundedCompletionWork();
    void workerCompletionSlotCanDeleteProviderWithPendingRequest();
    void normalCompletionSourceGuardsPostSignalContinuation();
};

namespace {

constexpr int kExpectedThumbnailBuckets = 2048;
constexpr int kExpectedThumbnailBytes = kExpectedThumbnailBuckets * 2;

std::filesystem::path filesystemPath(const QString& path)
{
#ifdef Q_OS_WIN
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::u8path(path.toUtf8().constData());
#endif
}

QString createSource(QTemporaryDir& directory, const QString& name)
{
    const QString path = directory.filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write("source-audio", 12) != 12
        || !file.flush()) {
        return {};
    }
    return path;
}

QString cachePath(const QString& cacheDirectory,
                  const QString& sourcePath,
                  const QString& suffix = QStringLiteral("-average"))
{
    const std::string key =
        agplayer::WaveformCache::key_for(filesystemPath(sourcePath));
    if (key.empty()) {
        return {};
    }
    return QDir(cacheDirectory)
        .filePath(QString::fromStdString(key) + suffix
                  + QStringLiteral(".agwf"));
}

bool saveCache(const QString& cacheDirectory,
               const QString& sourcePath,
               const std::vector<float>& mix,
               const QString& suffix = QStringLiteral("-average"))
{
    if (!QDir().mkpath(cacheDirectory)) {
        return false;
    }
    agplayer::WaveformCacheData data;
    data.mix = mix;
    if (mix.empty()) {
        return agplayer::WaveformCache::save_v2(
            filesystemPath(cachePath(cacheDirectory, sourcePath, suffix)),
            filesystemPath(sourcePath), data);
    }
    data.bass.assign(mix.size(), 0.25F);
    data.mid.assign(mix.size(), 0.5F);
    data.high.assign(mix.size(), 0.75F);
    data.duration_ms = 1'000U;
    data.total_samples = 48'000U;
    data.sample_rate = 48'000U;
    return agplayer::WaveformCache::save_v4(
        filesystemPath(cachePath(cacheDirectory, sourcePath, suffix)),
        filesystemPath(sourcePath), data);
}

QList<QVariant> waitForResult(QSignalSpy& spy, int timeoutMs = 5000)
{
    if (spy.isEmpty() && !spy.wait(timeoutMs)) {
        return {};
    }
    return spy.takeFirst();
}

} // namespace

void TrackWaveformThumbnailProviderTest::quantizesHighDensitySymmetricEnvelope()
{
    std::vector<float> mix(kExpectedThumbnailBuckets, 0.0F);
    mix[1024] = -0.8F;

    const QByteArray bytes =
        TrackWaveformThumbnailProvider::quantizeMixPeaks(mix);

    QCOMPARE(bytes.size(), kExpectedThumbnailBytes);
    QCOMPARE(static_cast<unsigned char>(bytes.at(1024 * 2)), 25U);
    QCOMPARE(static_cast<unsigned char>(bytes.at(1024 * 2 + 1)), 230U);
    QCOMPARE(static_cast<unsigned char>(bytes.at(1023 * 2)), 128U);
    QCOMPARE(static_cast<unsigned char>(bytes.at(1023 * 2 + 1)), 128U);
}

void TrackWaveformThumbnailProviderTest::ignoresNonFiniteSamplesAndClampsFiniteAmplitude()
{
    std::vector<float> mix(kExpectedThumbnailBuckets, 0.0F);
    mix[10] = -0.25F;
    mix[20] = std::numeric_limits<float>::quiet_NaN();
    mix[30] = 1.5F;
    mix[40] = -std::numeric_limits<float>::infinity();

    const QByteArray bytes =
        TrackWaveformThumbnailProvider::quantizeMixPeaks(mix);

    QCOMPARE(bytes.size(), kExpectedThumbnailBytes);
    QCOMPARE(static_cast<unsigned char>(bytes.at(10 * 2)), 96U);
    QCOMPARE(static_cast<unsigned char>(bytes.at(10 * 2 + 1)), 159U);
    QCOMPARE(static_cast<unsigned char>(bytes.at(20 * 2)), 128U);
    QCOMPARE(static_cast<unsigned char>(bytes.at(20 * 2 + 1)), 128U);
    QCOMPARE(static_cast<unsigned char>(bytes.at(30 * 2)), 0U);
    QCOMPARE(static_cast<unsigned char>(bytes.at(30 * 2 + 1)), 255U);
    QCOMPARE(static_cast<unsigned char>(bytes.at(40 * 2)), 128U);
    QCOMPARE(static_cast<unsigned char>(bytes.at(40 * 2 + 1)), 128U);
    QCOMPARE(TrackWaveformThumbnailProvider::quantizeMixPeaks({}).size(),
             kExpectedThumbnailBytes);
}

void TrackWaveformThumbnailProviderTest::downscalesBandEnergyWithAmplitudeWeighting()
{
    std::vector<float> mix(4096U, 0.1F);
    std::vector<float> energy(4096U, 10.0F / 255.0F);
    energy[1] = 250.0F / 255.0F;
    mix[1] = 1.0F;

    const QByteArray bytes =
        TrackWaveformThumbnailProvider::quantizeBandEnergy(energy, mix);

    QCOMPARE(bytes.size(), kExpectedThumbnailBuckets);
    QVERIFY(static_cast<unsigned char>(bytes.at(0)) > 200U);
    QCOMPARE(static_cast<unsigned char>(bytes.at(1)), 10U);
    QCOMPARE(TrackWaveformThumbnailProvider::quantizeBandEnergy({}, {}).size(), 0);
}

void TrackWaveformThumbnailProviderTest::readsExistingCacheWithoutChangingIt()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString sourcePath = createSource(directory, QStringLiteral("hit.wav"));
    QVERIFY(!sourcePath.isEmpty());
    const QString cacheDirectory = directory.filePath(QStringLiteral("cache"));
    std::vector<float> mix(256U, 0.0F);
    mix[129] = 0.8F;
    QVERIFY(saveCache(cacheDirectory, sourcePath, mix));

    const QString existingCache = cachePath(cacheDirectory, sourcePath);
    QFile cacheFile(existingCache);
    QVERIFY(cacheFile.open(QIODevice::ReadOnly));
    const QByteArray before = cacheFile.readAll();
    cacheFile.close();
    const QDateTime beforeModified = QFileInfo(existingCache).lastModified();

    TrackWaveformThumbnailProvider provider(cacheDirectory);
    QSignalSpy spy(&provider,
                   &TrackWaveformThumbnailProvider::thumbnailReady);
    QSignalSpy analysisSpy(&provider,
                           &TrackWaveformThumbnailProvider::analysisRequested);
    provider.request(QStringLiteral("persistent-track"), sourcePath, 7U);

    const QList<QVariant> result = waitForResult(spy);
    QCOMPARE(result.size(), 6);
    QCOMPARE(result.at(0).toString(), QStringLiteral("persistent-track"));
    QCOMPARE(result.at(1).toULongLong(), 7U);
    const QByteArray peaks = result.at(2).toByteArray();
    QCOMPARE(peaks.size(), kExpectedThumbnailBytes);
    QCOMPARE(static_cast<unsigned char>(peaks.at(1032 * 2)), 25U);
    QCOMPARE(static_cast<unsigned char>(peaks.at(1032 * 2 + 1)), 230U);
    QCOMPARE(result.at(3).toByteArray().size(), kExpectedThumbnailBuckets);
    QCOMPARE(result.at(4).toByteArray().size(), kExpectedThumbnailBuckets);
    QCOMPARE(result.at(5).toByteArray().size(), kExpectedThumbnailBuckets);

    QVERIFY(cacheFile.open(QIODevice::ReadOnly));
    QCOMPARE(cacheFile.readAll(), before);
    QCOMPARE(QFileInfo(existingCache).lastModified(), beforeModified);
    QCOMPARE(provider.diagnostics().value(QStringLiteral("cacheEntries")).toInt(),
             1);
    QCOMPARE(provider.diagnostics()
                 .value(QStringLiteral("maxActiveWorkers"))
                 .toInt(),
             1);
    QCOMPARE(analysisSpy.count(), 0);
}

void TrackWaveformThumbnailProviderTest::cacheMissRequestsLowPriorityAnalysis()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString sourcePath =
        createSource(directory, QStringLiteral("analysis-miss.wav"));
    TrackWaveformThumbnailProvider provider(
        directory.filePath(QStringLiteral("cache")));
    QSignalSpy readySpy(&provider,
                        &TrackWaveformThumbnailProvider::thumbnailReady);
    QSignalSpy analysisSpy(&provider,
                           &TrackWaveformThumbnailProvider::analysisRequested);

    provider.request(QStringLiteral("analysis-miss"), sourcePath, 1U, true);
    const QList<QVariant> result = waitForResult(readySpy);
    QCOMPARE(result.size(), 6);
    QVERIFY(result.at(2).toByteArray().isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(analysisSpy.count(), 1, 5000);
    QCOMPARE(analysisSpy.takeFirst().at(0).toString(), sourcePath);
}

void TrackWaveformThumbnailProviderTest::returnsEmptyForMissCorruptionMismatchAndEmptyMix()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString cacheDirectory = directory.filePath(QStringLiteral("cache"));
    QVERIFY(QDir().mkpath(cacheDirectory));

    TrackWaveformThumbnailProvider provider(cacheDirectory);
    QSignalSpy spy(&provider,
                   &TrackWaveformThumbnailProvider::thumbnailReady);

    const QString missPath = createSource(directory, QStringLiteral("miss.wav"));
    QVERIFY(!missPath.isEmpty());
    provider.request(QStringLiteral("miss"), missPath, 1U);
    QList<QVariant> result = waitForResult(spy);
    QCOMPARE(result.size(), 6);
    QVERIFY(result.at(2).toByteArray().isEmpty());
    QVERIFY(QDir(cacheDirectory).entryList(QDir::Files).isEmpty());

    const QString corruptPath =
        createSource(directory, QStringLiteral("corrupt.wav"));
    QVERIFY(!corruptPath.isEmpty());
    QFile corruptCache(cachePath(cacheDirectory, corruptPath));
    QVERIFY(corruptCache.open(QIODevice::WriteOnly));
    QCOMPARE(corruptCache.write("not-an-agwf", 11), 11);
    corruptCache.close();
    provider.request(QStringLiteral("corrupt"), corruptPath, 2U);
    result = waitForResult(spy);
    QCOMPARE(result.size(), 6);
    QVERIFY(result.at(2).toByteArray().isEmpty());

    const QString mismatchPath =
        createSource(directory, QStringLiteral("mismatch.wav"));
    QVERIFY(!mismatchPath.isEmpty());
    QVERIFY(saveCache(cacheDirectory, mismatchPath, {0.5F}));
    const QString oldCachePath = cachePath(cacheDirectory, mismatchPath);
    QFile mismatchSource(mismatchPath);
    QVERIFY(mismatchSource.open(QIODevice::Append));
    QCOMPARE(mismatchSource.write("changed", 7), 7);
    mismatchSource.close();
    const QString currentCachePath = cachePath(cacheDirectory, mismatchPath);
    QVERIFY(QFile::copy(oldCachePath, currentCachePath));
    provider.request(QStringLiteral("mismatch"), mismatchPath, 3U);
    result = waitForResult(spy);
    QCOMPARE(result.size(), 6);
    QVERIFY(result.at(2).toByteArray().isEmpty());

    const QString emptyPath =
        createSource(directory, QStringLiteral("empty.wav"));
    QVERIFY(!emptyPath.isEmpty());
    QVERIFY(saveCache(cacheDirectory, emptyPath, {}));
    provider.request(QStringLiteral("empty"), emptyPath, 4U);
    result = waitForResult(spy);
    QCOMPARE(result.size(), 6);
    QVERIFY(result.at(2).toByteArray().isEmpty());

    QCOMPARE(provider.diagnostics().value(QStringLiteral("cacheEntries")).toInt(),
             0);
}

void TrackWaveformThumbnailProviderTest::coalescesSameTrackAndPublishesOnlyLatestGeneration()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString sourcePath =
        createSource(directory, QStringLiteral("coalesced.wav"));
    const QString cacheDirectory = directory.filePath(QStringLiteral("cache"));
    QVERIFY(saveCache(cacheDirectory, sourcePath, {0.75F}));

    TrackWaveformThumbnailProvider provider(cacheDirectory);
    QSignalSpy spy(&provider,
                   &TrackWaveformThumbnailProvider::thumbnailReady);
    for (quint64 generation = 1U; generation <= 100U; ++generation) {
        provider.request(QStringLiteral("same-track"), sourcePath, generation);
    }

    const QVariantMap running = provider.diagnostics();
    QCOMPARE(running.value(QStringLiteral("inFlightTracks")).toInt(), 1);
    QCOMPARE(running.value(QStringLiteral("queuedJobs")).toInt(), 0);
    const QList<QVariant> result = waitForResult(spy);
    QCOMPARE(result.size(), 6);
    QCOMPARE(result.at(1).toULongLong(), 100U);
    QCOMPARE(result.at(2).toByteArray().size(),
             kExpectedThumbnailBytes);
    QTest::qWait(20);
    QCOMPARE(spy.count(), 0);
}

void TrackWaveformThumbnailProviderTest::cancelSuppressesMatchingGeneration()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString sourcePath =
        createSource(directory, QStringLiteral("cancel.wav"));
    const QString cacheDirectory = directory.filePath(QStringLiteral("cache"));
    QVERIFY(saveCache(cacheDirectory, sourcePath, {0.5F}));

    TrackWaveformThumbnailProvider provider(cacheDirectory);
    QSignalSpy spy(&provider,
                   &TrackWaveformThumbnailProvider::thumbnailReady);
    provider.request(QStringLiteral("cancelled-track"), sourcePath, 9U);
    provider.cancel(QStringLiteral("cancelled-track"), 9U);

    QTRY_COMPARE_WITH_TIMEOUT(
        provider.diagnostics().value(QStringLiteral("inFlightTracks")).toInt(),
        0, 5000);
    QCOMPARE(spy.count(), 0);
    QCOMPARE(provider.diagnostics().value(QStringLiteral("cacheEntries")).toInt(),
             0);
}

void TrackWaveformThumbnailProviderTest::canceledOffscreenReadDoesNotBlockVisibleRequest()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString cacheDirectory = directory.filePath(QStringLiteral("cache"));
    const QString stale = createSource(directory, QStringLiteral("stale.wav"));
    const QString visible = createSource(directory, QStringLiteral("visible.wav"));
    QVERIFY(saveCache(cacheDirectory, stale, {0.25F}));
    QVERIFY(saveCache(cacheDirectory, visible, {0.75F}));
    TrackWaveformThumbnailProvider provider(cacheDirectory);
    QSignalSpy spy(&provider, &TrackWaveformThumbnailProvider::thumbnailReady);
    provider.request(QStringLiteral("offscreen"), stale, 1U);
    provider.cancel(QStringLiteral("offscreen"), 1U);
    provider.request(QStringLiteral("visible"), visible, 2U, true);
    const QList<QVariant> result = waitForResult(spy);
    QCOMPARE(result.at(0).toString(), QStringLiteral("visible"));
    QCOMPARE(result.at(1).toULongLong(), 2U);
    QCOMPARE(result.at(2).toByteArray().size(), kExpectedThumbnailBytes);
    QTest::qWait(20);
    QCOMPARE(spy.count(), 0);
}

void TrackWaveformThumbnailProviderTest::retainsAtMost256SuccessfulTracks()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString sourcePath = createSource(directory, QStringLiteral("lru.wav"));
    const QString cacheDirectory = directory.filePath(QStringLiteral("cache"));
    QVERIFY(saveCache(cacheDirectory, sourcePath, {0.25F, 0.75F}));

    TrackWaveformThumbnailProvider provider(cacheDirectory);
    QSignalSpy spy(&provider,
                   &TrackWaveformThumbnailProvider::thumbnailReady);
    for (int track = 0; track < 257; ++track) {
        provider.request(QStringLiteral("track-%1").arg(track), sourcePath,
                         static_cast<quint64>(track + 1));
        const QList<QVariant> result = waitForResult(spy);
        QCOMPARE(result.size(), 6);
        QCOMPARE(result.at(2).toByteArray().size(),
                 kExpectedThumbnailBytes);
    }

    QCOMPARE(provider.diagnostics().value(QStringLiteral("cacheEntries")).toInt(),
             256);
    QVERIFY(QFile::remove(cachePath(cacheDirectory, sourcePath)));
    provider.request(QStringLiteral("track-0"), sourcePath, 999U);
    const QList<QVariant> evictedResult = waitForResult(spy);
    QCOMPARE(evictedResult.size(), 6);
    QVERIFY(evictedResult.at(2).toByteArray().isEmpty());
}

void TrackWaveformThumbnailProviderTest::destructionWaitsSafelyForOutstandingRead()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString sourcePath =
        createSource(directory, QStringLiteral("destruction.wav"));
    const QString cacheDirectory = directory.filePath(QStringLiteral("cache"));
    QVERIFY(saveCache(cacheDirectory, sourcePath,
                      std::vector<float>(1'000'000U, 0.5F)));

    auto* provider = new TrackWaveformThumbnailProvider(cacheDirectory);
    provider->request(QStringLiteral("destroyed-track"), sourcePath, 1U);
    delete provider;
    QVERIFY(true);
}

void TrackWaveformThumbnailProviderTest::queueFullCallbackReentryPreservesBoundsAndLatestRequest()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString sourcePath =
        createSource(directory, QStringLiteral("reentry-miss.wav"));
    QVERIFY(!sourcePath.isEmpty());
    TrackWaveformThumbnailProvider provider(
        directory.filePath(QStringLiteral("cache")));

    bool reentered = false;
    bool latestReady = false;
    connect(&provider, &TrackWaveformThumbnailProvider::thumbnailReady,
            &provider,
            [&](const QString& trackId, quint64 generation,
                const QByteArray& peaks) {
                if (trackId == QStringLiteral("queued-track-0")
                    && !reentered) {
                    reentered = true;
                    provider.request(QStringLiteral("reentrant-latest"),
                                     sourcePath, 999U);
                }
                if (trackId == QStringLiteral("reentrant-latest")) {
                    QCOMPARE(generation, 999U);
                    QVERIFY(peaks.isEmpty());
                    latestReady = true;
                }
            });

    provider.request(QStringLiteral("active-track"), sourcePath, 1U);
    for (int queued = 0;
         queued < TrackWaveformThumbnailProvider::kMaxQueuedJobs;
         ++queued) {
        provider.request(QStringLiteral("queued-track-%1").arg(queued),
                         sourcePath, static_cast<quint64>(queued + 2));
    }
    provider.request(QStringLiteral("overflow-track"), sourcePath, 500U);

    QVariantMap state = provider.diagnostics();
    QVERIFY(state.value(QStringLiteral("queuedJobs")).toInt() <= 256);
    QVERIFY(state.value(QStringLiteral("inFlightTracks")).toInt() <= 257);
    QTRY_VERIFY_WITH_TIMEOUT(reentered, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(latestReady, 5000);
    state = provider.diagnostics();
    QVERIFY(state.value(QStringLiteral("queuedJobs")).toInt() <= 256);
    QVERIFY(state.value(QStringLiteral("inFlightTracks")).toInt() <= 257);
}

void TrackWaveformThumbnailProviderTest::repeatedMissUsesShortInvalidatableCooldown()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString sourcePath =
        createSource(directory, QStringLiteral("cooldown.wav"));
    const QString cacheDirectory = directory.filePath(QStringLiteral("cache"));
    TrackWaveformThumbnailProvider provider(cacheDirectory);
    QSignalSpy spy(&provider,
                   &TrackWaveformThumbnailProvider::thumbnailReady);

    provider.request(QStringLiteral("miss-one"), sourcePath, 1U);
    QVERIFY(!waitForResult(spy).isEmpty());
    const qulonglong firstAttempts = provider.diagnostics()
        .value(QStringLiteral("cacheReadAttempts"))
        .toULongLong();
    QCOMPARE(firstAttempts, 1U);

    provider.request(QStringLiteral("miss-two"), sourcePath, 2U);
    const QList<QVariant> cooled = waitForResult(spy);
    QCOMPARE(cooled.size(), 6);
    QVERIFY(cooled.at(2).toByteArray().isEmpty());
    QCOMPARE(provider.diagnostics()
                 .value(QStringLiteral("cacheReadAttempts"))
                 .toULongLong(),
             firstAttempts);
    QVERIFY(provider.diagnostics()
                .value(QStringLiteral("negativeCacheEntries"))
                .toInt()
            <= 256);

    QVERIFY(saveCache(cacheDirectory, sourcePath, {0.75F}));
    provider.request(QStringLiteral("still-cooled"), sourcePath, 3U);
    const QList<QVariant> stillCooled = waitForResult(spy);
    QCOMPARE(stillCooled.size(), 6);
    QVERIFY(stillCooled.at(2).toByteArray().isEmpty());
    QCOMPARE(provider.diagnostics()
                 .value(QStringLiteral("cacheReadAttempts"))
                 .toULongLong(),
             firstAttempts);

    provider.refresh();
    provider.request(QStringLiteral("after-refresh"), sourcePath, 4U);
    const QList<QVariant> refreshed = waitForResult(spy);
    QCOMPARE(refreshed.size(), 6);
    QCOMPARE(refreshed.at(2).toByteArray().size(),
             kExpectedThumbnailBytes);
    QCOMPARE(provider.diagnostics()
                 .value(QStringLiteral("cacheReadAttempts"))
                 .toULongLong(),
             firstAttempts + 1U);

    QVERIFY(QFile::remove(cachePath(cacheDirectory, sourcePath)));
    provider.refresh();
    provider.request(QStringLiteral("expires"), sourcePath, 5U);
    QVERIFY(!waitForResult(spy).isEmpty());
    const qulonglong beforeExpiry = provider.diagnostics()
        .value(QStringLiteral("cacheReadAttempts"))
        .toULongLong();
    QVERIFY(saveCache(cacheDirectory, sourcePath, {0.5F}));
    QTest::qWait(300);
    provider.request(QStringLiteral("after-expiry"), sourcePath, 6U);
    const QList<QVariant> expired = waitForResult(spy);
    QCOMPARE(expired.size(), 6);
    QCOMPARE(expired.at(2).toByteArray().size(),
             kExpectedThumbnailBytes);
    QCOMPARE(provider.diagnostics()
                 .value(QStringLiteral("cacheReadAttempts"))
                 .toULongLong(),
             beforeExpiry + 1U);
}

void TrackWaveformThumbnailProviderTest::cacheDirectoryEpochInvalidatesNegativeCooldown()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString sourcePath =
        createSource(directory, QStringLiteral("epoch.wav"));
    const QString oldDirectory = directory.filePath(QStringLiteral("old"));
    const QString newDirectory = directory.filePath(QStringLiteral("new"));
    QVERIFY(saveCache(newDirectory, sourcePath, {0.75F}));

    TrackWaveformThumbnailProvider provider(oldDirectory);
    QSignalSpy spy(&provider,
                   &TrackWaveformThumbnailProvider::thumbnailReady);
    provider.request(QStringLiteral("old-miss"), sourcePath, 1U);
    QVERIFY(!waitForResult(spy).isEmpty());
    const qulonglong oldAttempts = provider.diagnostics()
        .value(QStringLiteral("cacheReadAttempts"))
        .toULongLong();

    provider.setCacheDirectory(newDirectory);
    provider.request(QStringLiteral("new-hit"), sourcePath, 2U);
    const QList<QVariant> result = waitForResult(spy);
    QCOMPARE(result.size(), 6);
    QCOMPARE(result.at(2).toByteArray().size(),
             kExpectedThumbnailBytes);
    QCOMPARE(provider.diagnostics()
                 .value(QStringLiteral("cacheReadAttempts"))
                 .toULongLong(),
             oldAttempts + 1U);
}

void TrackWaveformThumbnailProviderTest::sourceInvalidationRefreshesOnlyMatchingMiss()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString missedSource =
        createSource(directory, QStringLiteral("new-main-cache.wav"));
    const QString retainedSource =
        createSource(directory, QStringLiteral("retained-cache.wav"));
    const QString cacheDirectory = directory.filePath(QStringLiteral("cache"));
    QVERIFY(saveCache(cacheDirectory, retainedSource, {0.25F}));

    TrackWaveformThumbnailProvider provider(cacheDirectory);
    QSignalSpy readySpy(&provider,
                        &TrackWaveformThumbnailProvider::thumbnailReady);
    QSignalSpy invalidatedSpy(
        &provider, &TrackWaveformThumbnailProvider::sourceCacheInvalidated);

    provider.request(QStringLiteral("retained"), retainedSource, 1U);
    const QList<QVariant> retained = waitForResult(readySpy);
    QCOMPARE(retained.at(2).toByteArray().size(),
             kExpectedThumbnailBytes);
    provider.request(QStringLiteral("missed"), missedSource, 2U);
    const QList<QVariant> missed = waitForResult(readySpy);
    QVERIFY(missed.at(2).toByteArray().isEmpty());
    QCOMPARE(provider.diagnostics().value(QStringLiteral("cacheEntries")).toInt(),
             1);
    QCOMPARE(provider.diagnostics()
                 .value(QStringLiteral("negativeCacheEntries")).toInt(),
             1);
    const qulonglong attemptsBeforeInvalidation = provider.diagnostics()
        .value(QStringLiteral("cacheReadAttempts")).toULongLong();

    QVERIFY(saveCache(cacheDirectory, missedSource, {0.75F}));
    provider.invalidateSourceCache(missedSource);
    QCOMPARE(invalidatedSpy.count(), 1);
    QCOMPARE(invalidatedSpy.takeFirst().at(0).toString(), missedSource);
    QCOMPARE(provider.diagnostics().value(QStringLiteral("cacheEntries")).toInt(),
             1);
    QCOMPARE(provider.diagnostics()
                 .value(QStringLiteral("negativeCacheEntries")).toInt(),
             0);

    provider.request(QStringLiteral("missed"), missedSource, 3U);
    const QList<QVariant> refreshed = waitForResult(readySpy);
    QCOMPARE(refreshed.at(2).toByteArray().size(),
             kExpectedThumbnailBytes);
    QCOMPARE(provider.diagnostics()
                 .value(QStringLiteral("cacheReadAttempts")).toULongLong(),
             attemptsBeforeInvalidation + 1U);

    const qulonglong attemptsBeforeRetained = provider.diagnostics()
        .value(QStringLiteral("cacheReadAttempts")).toULongLong();
    provider.request(QStringLiteral("retained"), retainedSource, 4U);
    const QList<QVariant> stillRetained = waitForResult(readySpy);
    QCOMPARE(stillRetained.at(2).toByteArray().size(),
             kExpectedThumbnailBytes);
    QCOMPARE(provider.diagnostics()
                 .value(QStringLiteral("cacheReadAttempts")).toULongLong(),
             attemptsBeforeRetained);

    provider.invalidateSourceCache(missedSource);
    QCOMPARE(provider.diagnostics().value(QStringLiteral("cacheEntries")).toInt(),
             1);
    provider.request(QStringLiteral("retained"), retainedSource, 5U);
    QCOMPARE(waitForResult(readySpy).at(2).toByteArray().size(),
             kExpectedThumbnailBytes);
    QCOMPARE(provider.diagnostics()
                 .value(QStringLiteral("cacheReadAttempts")).toULongLong(),
             attemptsBeforeRetained);
}

void TrackWaveformThumbnailProviderTest::repeatedCorruptCacheUsesCooldown()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString sourcePath =
        createSource(directory, QStringLiteral("corrupt-cooldown.wav"));
    const QString cacheDirectory = directory.filePath(QStringLiteral("cache"));
    QVERIFY(QDir().mkpath(cacheDirectory));
    QFile corrupt(cachePath(cacheDirectory, sourcePath));
    QVERIFY(corrupt.open(QIODevice::WriteOnly));
    QCOMPARE(corrupt.write("broken", 6), 6);
    corrupt.close();

    TrackWaveformThumbnailProvider provider(cacheDirectory);
    QSignalSpy spy(&provider,
                   &TrackWaveformThumbnailProvider::thumbnailReady);
    provider.request(QStringLiteral("corrupt-one"), sourcePath, 1U);
    QVERIFY(!waitForResult(spy).isEmpty());
    const qulonglong firstAttempts = provider.diagnostics()
        .value(QStringLiteral("cacheReadAttempts"))
        .toULongLong();
    QCOMPARE(firstAttempts, 1U);

    provider.request(QStringLiteral("corrupt-two"), sourcePath, 2U);
    const QList<QVariant> result = waitForResult(spy);
    QCOMPARE(result.size(), 6);
    QVERIFY(result.at(2).toByteArray().isEmpty());
    QCOMPARE(provider.diagnostics()
                 .value(QStringLiteral("cacheReadAttempts"))
                 .toULongLong(),
             firstAttempts);
}

void TrackWaveformThumbnailProviderTest::negativeCooldownRetainsAtMost256Sources()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString cacheDirectory = directory.filePath(QStringLiteral("cache"));
    TrackWaveformThumbnailProvider provider(cacheDirectory);
    QSignalSpy spy(&provider,
                   &TrackWaveformThumbnailProvider::thumbnailReady);
    QString firstSource;
    for (int index = 0; index < 257; ++index) {
        const QString sourcePath = createSource(
            directory, QStringLiteral("negative-%1.wav").arg(index));
        QVERIFY(!sourcePath.isEmpty());
        if (index == 0) {
            firstSource = sourcePath;
        }
        provider.request(QStringLiteral("negative-track-%1").arg(index),
                         sourcePath, static_cast<quint64>(index + 1));
        const QList<QVariant> result = waitForResult(spy);
        QCOMPARE(result.size(), 6);
        QVERIFY(result.at(2).toByteArray().isEmpty());
    }
    QCOMPARE(provider.diagnostics()
                 .value(QStringLiteral("negativeCacheEntries"))
                 .toInt(),
             256);

    QVERIFY(saveCache(cacheDirectory, firstSource, {0.75F}));
    const qulonglong beforeRetry = provider.diagnostics()
        .value(QStringLiteral("cacheReadAttempts"))
        .toULongLong();
    provider.request(QStringLiteral("negative-track-0-retry"), firstSource,
                     999U);
    const QList<QVariant> retried = waitForResult(spy);
    QCOMPARE(retried.size(), 6);
    QCOMPARE(retried.at(2).toByteArray().size(),
             kExpectedThumbnailBytes);
    QCOMPARE(provider.diagnostics()
                 .value(QStringLiteral("cacheReadAttempts"))
                 .toULongLong(),
             beforeRetry + 1U);
}

void TrackWaveformThumbnailProviderTest::newerSourceForActiveTrackPublishesOnlyLatestFixture()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString sourceA = createSource(directory, QStringLiteral("a.wav"));
    const QString sourceB = createSource(directory, QStringLiteral("b.wav"));
    const QString cacheDirectory = directory.filePath(QStringLiteral("cache"));
    QVERIFY(saveCache(cacheDirectory, sourceA, {0.25F}));
    QVERIFY(saveCache(cacheDirectory, sourceB, {0.75F}));

    TrackWaveformThumbnailProvider provider(cacheDirectory);
    QSignalSpy spy(&provider,
                   &TrackWaveformThumbnailProvider::thumbnailReady);
    provider.request(QStringLiteral("same-track"), sourceA, 10U);
    provider.request(QStringLiteral("same-track"), sourceB, 11U);

    const QList<QVariant> result = waitForResult(spy);
    QCOMPARE(result.size(), 6);
    QCOMPARE(result.at(0).toString(), QStringLiteral("same-track"));
    QCOMPARE(result.at(1).toULongLong(), 11U);
    QCOMPARE(static_cast<unsigned char>(result.at(2).toByteArray().at(0)),
             32U);
    QCOMPARE(static_cast<unsigned char>(result.at(2).toByteArray().at(1)),
             223U);
    QTest::qWait(20);
    QCOMPARE(spy.count(), 0);
}

void TrackWaveformThumbnailProviderTest::cacheDirectoryChangeSuppressesActiveOldFixture()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString sourcePath =
        createSource(directory, QStringLiteral("directory-change.wav"));
    const QString oldDirectory = directory.filePath(QStringLiteral("old"));
    const QString newDirectory = directory.filePath(QStringLiteral("new"));
    QVERIFY(saveCache(oldDirectory, sourcePath, {0.25F}));
    QVERIFY(saveCache(newDirectory, sourcePath, {0.75F}));

    TrackWaveformThumbnailProvider provider(oldDirectory);
    QSignalSpy spy(&provider,
                   &TrackWaveformThumbnailProvider::thumbnailReady);
    provider.request(QStringLiteral("epoch-track"), sourcePath, 21U);
    provider.setCacheDirectory(newDirectory);

    const QList<QVariant> result = waitForResult(spy);
    QCOMPARE(result.size(), 6);
    QCOMPARE(result.at(1).toULongLong(), 21U);
    QCOMPARE(static_cast<unsigned char>(result.at(2).toByteArray().at(0)),
             32U);
    QCOMPARE(static_cast<unsigned char>(result.at(2).toByteArray().at(1)),
             223U);
    QCOMPARE(provider.diagnostics().value(QStringLiteral("cacheEntries")).toInt(),
             1);
    QTest::qWait(20);
    QCOMPARE(spy.count(), 0);
}

void TrackWaveformThumbnailProviderTest::prefersAverageThenFallsBackToRmsAndLegacy()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString sourcePath =
        createSource(directory, QStringLiteral("priority.wav"));
    const QString cacheDirectory = directory.filePath(QStringLiteral("cache"));
    QVERIFY(saveCache(cacheDirectory, sourcePath, {0.25F},
                      QStringLiteral("-average")));
    QVERIFY(saveCache(cacheDirectory, sourcePath, {0.5F},
                      QStringLiteral("-rms")));
    QVERIFY(saveCache(cacheDirectory, sourcePath, {0.75F}, QString()));

    TrackWaveformThumbnailProvider provider(cacheDirectory);
    QSignalSpy spy(&provider,
                   &TrackWaveformThumbnailProvider::thumbnailReady);
    provider.request(QStringLiteral("average"), sourcePath, 1U);
    QList<QVariant> result = waitForResult(spy);
    QCOMPARE(static_cast<unsigned char>(result.at(2).toByteArray().at(0)), 96U);
    QCOMPARE(static_cast<unsigned char>(result.at(2).toByteArray().at(1)), 159U);

    QFile average(cachePath(cacheDirectory, sourcePath,
                            QStringLiteral("-average")));
    QVERIFY(average.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(average.write("broken", 6), 6);
    average.close();
    provider.refresh();
    provider.request(QStringLiteral("rms"), sourcePath, 2U);
    result = waitForResult(spy);
    QCOMPARE(static_cast<unsigned char>(result.at(2).toByteArray().at(0)),
             64U);
    QCOMPARE(static_cast<unsigned char>(result.at(2).toByteArray().at(1)),
             191U);

    QFile rms(cachePath(cacheDirectory, sourcePath, QStringLiteral("-rms")));
    QVERIFY(rms.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(rms.write("broken", 6), 6);
    rms.close();
    provider.refresh();
    provider.request(QStringLiteral("legacy"), sourcePath, 3U);
    result = waitForResult(spy);
    QCOMPARE(static_cast<unsigned char>(result.at(2).toByteArray().at(0)),
             32U);
    QCOMPARE(static_cast<unsigned char>(result.at(2).toByteArray().at(1)),
             223U);
}

void TrackWaveformThumbnailProviderTest::sourceMismatchedAverageFallsBackToValidRms()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString sourcePath =
        createSource(directory, QStringLiteral("mismatched-average.wav"));
    const QString cacheDirectory = directory.filePath(QStringLiteral("cache"));
    QVERIFY(saveCache(cacheDirectory, sourcePath, {0.25F},
                      QStringLiteral("-average")));
    const QString oldAverage = cachePath(cacheDirectory, sourcePath,
                                         QStringLiteral("-average"));

    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::Append));
    QCOMPARE(source.write("changed", 7), 7);
    source.close();
    const QString mismatchedAverage = cachePath(
        cacheDirectory, sourcePath, QStringLiteral("-average"));
    QVERIFY(QFile::copy(oldAverage, mismatchedAverage));
    QVERIFY(saveCache(cacheDirectory, sourcePath, {0.5F},
                      QStringLiteral("-rms")));

    TrackWaveformThumbnailProvider provider(cacheDirectory);
    QSignalSpy spy(&provider,
                   &TrackWaveformThumbnailProvider::thumbnailReady);
    provider.request(QStringLiteral("fallback-rms"), sourcePath, 1U);
    const QList<QVariant> result = waitForResult(spy);
    QCOMPARE(result.size(), 6);
    QCOMPARE(static_cast<unsigned char>(result.at(2).toByteArray().at(0)),
             64U);
    QCOMPARE(static_cast<unsigned char>(result.at(2).toByteArray().at(1)),
             191U);
}

void TrackWaveformThumbnailProviderTest::droppedGenerationHasNoDelayedPublicationAfterReplacement()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString sourcePath =
        createSource(directory, QStringLiteral("dropped-generation.wav"));
    TrackWaveformThumbnailProvider provider(
        directory.filePath(QStringLiteral("cache")));
    QSignalSpy spy(&provider,
                   &TrackWaveformThumbnailProvider::thumbnailReady);

    provider.request(QStringLiteral("active"), sourcePath, 1U);
    provider.request(QStringLiteral("victim"), sourcePath, 10U);
    for (int index = 1;
         index < TrackWaveformThumbnailProvider::kMaxQueuedJobs;
         ++index) {
        provider.request(QStringLiteral("queued-%1").arg(index), sourcePath,
                         static_cast<quint64>(index + 10));
    }
    provider.request(QStringLiteral("overflow"), sourcePath, 500U);

    // A direct drop notification, if any, has completed by this point. Only
    // publications delayed until after the replacement request are relevant.
    spy.clear();
    provider.request(QStringLiteral("victim"), sourcePath, 11U);
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.diagnostics().value(QStringLiteral("inFlightTracks")).toInt(),
        0, 10000);
    QTest::qWait(30);

    bool sawLatest = false;
    for (const QList<QVariant>& publication : spy) {
        if (publication.at(0).toString() != QStringLiteral("victim")) {
            continue;
        }
        QVERIFY2(publication.at(1).toULongLong() != 10U,
                 "the dropped generation was published after replacement");
        sawLatest = sawLatest || publication.at(1).toULongLong() == 11U;
    }
    QVERIFY(sawLatest);
}

void TrackWaveformThumbnailProviderTest::canceledCooldownRequestHasNoDelayedPublication()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString sourcePath =
        createSource(directory, QStringLiteral("cancel-cooldown.wav"));
    TrackWaveformThumbnailProvider provider(
        directory.filePath(QStringLiteral("cache")));
    QSignalSpy spy(&provider,
                   &TrackWaveformThumbnailProvider::thumbnailReady);

    provider.request(QStringLiteral("prime-miss"), sourcePath, 1U);
    QVERIFY(!waitForResult(spy).isEmpty());
    spy.clear();

    provider.request(QStringLiteral("cooled-cancel"), sourcePath, 2U);
    // Synchronous cooldown publication is defined to precede this cancel and
    // is deliberately discarded; cancel must not leave a queued callback.
    spy.clear();
    provider.cancel(QStringLiteral("cooled-cancel"), 2U);
    QTest::qWait(30);
    QCOMPARE(spy.count(), 0);
}

void TrackWaveformThumbnailProviderTest::refreshThenCacheHitHasNoStaleCooldownPublication()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString sourcePath =
        createSource(directory, QStringLiteral("refresh-cooldown.wav"));
    const QString cacheDirectory = directory.filePath(QStringLiteral("cache"));
    TrackWaveformThumbnailProvider provider(cacheDirectory);
    QSignalSpy spy(&provider,
                   &TrackWaveformThumbnailProvider::thumbnailReady);

    provider.request(QStringLiteral("prime-miss"), sourcePath, 1U);
    QVERIFY(!waitForResult(spy).isEmpty());
    QVERIFY(saveCache(cacheDirectory, sourcePath, {0.75F}));
    spy.clear();

    provider.request(QStringLiteral("refresh-track"), sourcePath, 2U);
    // A synchronous cooldown result is complete here and cannot survive the
    // refresh. Discard it before observing the refreshed cache hit.
    spy.clear();
    provider.refresh();
    provider.request(QStringLiteral("refresh-track"), sourcePath, 3U);

    const QList<QVariant> result = waitForResult(spy);
    QCOMPARE(result.size(), 6);
    QCOMPARE(result.at(1).toULongLong(), 3U);
    QCOMPARE(result.at(2).toByteArray().size(),
             kExpectedThumbnailBytes);
    QTest::qWait(30);
    QCOMPARE(spy.count(), 0);
}

void TrackWaveformThumbnailProviderTest::tenThousandOverflowRequestsLeaveOnlyBoundedCompletionWork()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString sourcePath =
        createSource(directory, QStringLiteral("overflow-burst.wav"));
    int publications = 0;
    TrackWaveformThumbnailProvider provider(
        directory.filePath(QStringLiteral("cache")));
    connect(&provider, &TrackWaveformThumbnailProvider::thumbnailReady,
            &provider,
            [&publications] { ++publications; });

    for (int index = 0; index < 10000; ++index) {
        provider.request(QStringLiteral("burst-%1").arg(index), sourcePath,
                         static_cast<quint64>(index + 1));
        const QVariantMap state = provider.diagnostics();
        QVERIFY(state.value(QStringLiteral("queuedJobs")).toInt() <= 256);
        QVERIFY(state.value(QStringLiteral("inFlightTracks")).toInt() <= 257);
    }
    const int publicationsBeforeEvents = publications;

    QTRY_COMPARE_WITH_TIMEOUT(
        provider.diagnostics().value(QStringLiteral("inFlightTracks")).toInt(),
        0, 10000);
    QTest::qWait(50);
    const int completionPublications = publications - publicationsBeforeEvents;
    QVERIFY2(completionPublications <= 257,
             qPrintable(QStringLiteral("unexpected delayed publications: %1")
                            .arg(completionPublications)));
    const QVariantMap finalState = provider.diagnostics();
    QVERIFY(finalState.value(QStringLiteral("queuedJobs")).toInt() <= 256);
    QVERIFY(finalState.value(QStringLiteral("inFlightTracks")).toInt() <= 257);
}

void TrackWaveformThumbnailProviderTest::workerCompletionSlotCanDeleteProviderWithPendingRequest()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString firstSource =
        createSource(directory, QStringLiteral("delete-first.wav"));
    const QString secondSource =
        createSource(directory, QStringLiteral("delete-pending.wav"));
    const QString cacheDirectory = directory.filePath(QStringLiteral("cache"));
    QVERIFY(saveCache(cacheDirectory, firstSource, {0.25F}));
    QVERIFY(saveCache(cacheDirectory, secondSource, {0.75F}));

    QPointer<TrackWaveformThumbnailProvider> provider =
        new TrackWaveformThumbnailProvider(cacheDirectory);
    int publications = 0;
    connect(provider, &TrackWaveformThumbnailProvider::thumbnailReady,
            this,
            [&provider, &publications] {
                ++publications;
                delete provider.data();
            });

    provider->request(QStringLiteral("delete-active"), firstSource, 1U);
    provider->request(QStringLiteral("must-not-publish"), secondSource, 2U);
    QTRY_VERIFY_WITH_TIMEOUT(provider.isNull(), 5000);
    QTest::qWait(50);
    QCOMPARE(publications, 1);
}

void TrackWaveformThumbnailProviderTest::normalCompletionSourceGuardsPostSignalContinuation()
{
    const QDir testSourceDirectory = QFileInfo(QString::fromUtf8(__FILE__)).dir();
    QFile source(testSourceDirectory.absoluteFilePath(
        QStringLiteral("../../qt/src/track_waveform_thumbnail_provider.cpp")));
    QVERIFY(source.open(QIODevice::ReadOnly));
    const QByteArray implementation = source.readAll();
    const qsizetype finishBegin = implementation.indexOf(
        "void TrackWaveformThumbnailProvider::finishActive(");
    const qsizetype finishEnd = implementation.indexOf(
        "void TrackWaveformThumbnailProvider::touchLru", finishBegin);
    QVERIFY(finishBegin >= 0);
    QVERIFY(finishEnd > finishBegin);
    const QByteArray finishActive =
        implementation.mid(finishBegin, finishEnd - finishBegin);

    const qsizetype emitPosition = finishActive.indexOf(
        "emit thumbnailReady(current.trackId, current.generation, loaded.peaks,");
    const qsizetype guardDeclaration = finishActive.lastIndexOf(
        "QPointer<TrackWaveformThumbnailProvider> guard(this)", emitPosition);
    const qsizetype guardCheck =
        finishActive.indexOf("if (guard.isNull())", emitPosition);
    const qsizetype continuation =
        finishActive.indexOf("startNext();", emitPosition);
    QVERIFY(emitPosition >= 0);
    QVERIFY2(guardDeclaration >= 0 && guardDeclaration < emitPosition,
             "normal completion must establish a QObject lifetime guard");
    QVERIFY2(guardCheck > emitPosition && guardCheck < continuation,
             "normal completion must check lifetime before startNext");
}

QTEST_GUILESS_MAIN(TrackWaveformThumbnailProviderTest)

#include "track_waveform_thumbnail_provider_test.moc"
