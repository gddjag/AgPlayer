#include "settings_controller.hpp"
#include "waveform_provider.hpp"
#include "waveform_provider_test_access.hpp"
#include "bpm_fixture.hpp"
#include "cache_janitor.hpp"
#include "../../core/src/frequency_color_waveform_cache.hpp"
#include "../../core/src/waveform_cache.hpp"

#include <QFile>
#include <QDir>
#include <QCoreApplication>
#include <QDateTime>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <agplayer/c_api.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <vector>

namespace agplayer::testing {

void reset_waveform_provider_counters() noexcept;
std::uint64_t waveform_provider_frequency_cache_loads() noexcept;
std::uint64_t waveform_provider_frequency_jobs_started() noexcept;
std::uint64_t waveform_provider_mix_jobs_started() noexcept;
std::uint64_t waveform_provider_max_frequency_jobs() noexcept;
std::uint64_t waveform_provider_frequency_progress_callbacks() noexcept;
std::uint64_t waveform_provider_power_queries() noexcept;
std::uint64_t waveform_provider_decoder_opens() noexcept;
std::uint64_t waveform_provider_max_worker_tasks() noexcept;
using WaveformProviderTaskHook = void (*)(bool prefetch,
                                          const char* utf8Path,
                                          void* context);
void set_waveform_provider_task_hook(WaveformProviderTaskHook hook,
                                     void* context) noexcept;

} // namespace agplayer::testing

struct FrequencyPowerStateTestAccess final {
    static void forceEnergySaverActive(WaveformProvider& provider)
    {
        provider.energySaverActive_ = true;
        provider.lastPowerQueryElapsedMs_ = provider.powerQueryClock_.elapsed();
    }

    static bool energySaverActive(const WaveformProvider& provider)
    {
        return provider.energySaverActive_;
    }

    static qint64 lastPowerQueryElapsedMs(const WaveformProvider& provider)
    {
        return provider.lastPowerQueryElapsedMs_;
    }
};

namespace {

void finishProviderAnalysis(WaveformProvider& provider)
{
    WaveformProviderTestAccess::waitForAnalysis(provider);
    QCoreApplication::processEvents();
}

struct SchedulingBarrier {
    std::mutex mutex;
    std::condition_variable condition;
    QString firstPrefetchPath;
    bool firstPrefetchStarted = false;
    bool releaseFirstPrefetch = false;
    std::vector<QString> startOrder;
};

void schedulingHook(const bool prefetch, const char* utf8Path, void* context)
{
    auto& barrier = *static_cast<SchedulingBarrier*>(context);
    const QString path = QString::fromUtf8(utf8Path);
    std::unique_lock lock(barrier.mutex);
    barrier.startOrder.push_back(path);
    if (prefetch && path == barrier.firstPrefetchPath) {
        barrier.firstPrefetchStarted = true;
        barrier.condition.notify_all();
        barrier.condition.wait(lock, [&barrier] {
            return barrier.releaseFirstPrefetch;
        });
    }
}

class SchedulingHookGuard final {
public:
    explicit SchedulingHookGuard(SchedulingBarrier& barrier)
    {
        agplayer::testing::set_waveform_provider_task_hook(
            schedulingHook, &barrier);
    }
    ~SchedulingHookGuard()
    {
        agplayer::testing::set_waveform_provider_task_hook(nullptr, nullptr);
    }
};

} // namespace

class WaveformProviderTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void emptyPathEmitsEmptyPeaks();
    void cacheHitEmitsPeaksImmediately();
    void duplicateCurrentRequestReusesGenerationAndResult();
    void analysisEmitsPeaksForFixture();
    void clickBeatsKeepTheirDecodedTimelinePositions();
    void unicodePathAnalyzesAndCaches();
    void aggregationChangeUsesSeparateCache();
    void newerTrackSuppressesStaleAnalysisResult();
    void resultCarriesTrackIdentityAndGeneration();
    void prefetchTracksWarmsCacheWithoutChangingCurrentTrack();
    void currentAnalysisPreemptsQueuedPrefetchOnTheSingleWorker();
    void successfulAnalysisAnnouncesWrittenCache();
    void failedAnalysisEmitsATerminalSignalWithIdentity();
    void cancellingAnalysisReleasesNativeResourcesWithoutTerminal();
    void replacingAnalysisReleasesSupersededResourcesWithoutStaleTerminal();
    void destroyingProviderReleasesInFlightNativeResources();
    void nonFrequencyLoadsAndPrefetchNeverTouchFrequencyColor();
    void frequencyRequestPublishesMixBeforeBandsAndReusesBothCaches();
    void frequencyCacheHitWithoutAgwfRebuildsBothCachesInOneFrequencyJob();
    void frequencyCacheSaveFailureKeepsMixAndExistingFilesSafe();
    void corruptFrequencyCacheIsRebuilt();
    void switchingToMixOnlyCancelsFrequencyWithoutStaleBands();
    void frequencyPauseResumesWithoutBusyProgress();
    void cancellingFrequencyResetsPowerStateAndRestarts();
    void frequencyJobsAreSerializedAndDestructionJoins();
    void cacheCleanupBudgetsAgwfAndFcwWithoutCollateralDeletion();

private:
    QString fixturePath_;
};

void WaveformProviderTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    const QByteArray env = qgetenv("AGPLAYER_TEST_WAV");
    if (!env.isEmpty()) {
        fixturePath_ = QString::fromUtf8(env);
    }
}

void WaveformProviderTest::emptyPathEmitsEmptyPeaks()
{
    SettingsController settings;
    WaveformProvider provider(&settings);
    QSignalSpy spy(&provider, &WaveformProvider::waveformReady);

    provider.loadForTrack(QString());

    QCOMPARE(spy.count(), 1);
    const QList<QVariant> args = spy.takeFirst();
    QCOMPARE(args.size(), 2);
    QCOMPARE(args[0].toString(), QString());
    QVERIFY(args[1].toMap().isEmpty());
    QCOMPARE(provider.analysisProgress(), 1.0);
}

void WaveformProviderTest::cacheHitEmitsPeaksImmediately()
{
    if (fixturePath_.isEmpty()) {
        QSKIP("AGPLAYER_TEST_WAV not set");
    }

    QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir cacheDir;
    QVERIFY(cacheDir.isValid());
    SettingsController settings;
    settings.setCacheDirectory(cacheDir.path());

    // First pass: analyze and save cache.
    {
        WaveformProvider provider(&settings);
        QSignalSpy spy(&provider, &WaveformProvider::waveformReady);
        provider.loadForTrack(fixturePath_);
        finishProviderAnalysis(provider);
        QCOMPARE(spy.count(), 1);
    }

    // Second pass: must hit cache and emit synchronously.
    WaveformProvider provider(&settings);
    QSignalSpy spy(&provider, &WaveformProvider::waveformReady);
    provider.loadForTrack(fixturePath_);

    QCOMPARE(spy.count(), 1);
    const QList<QVariant> args = spy.takeFirst();
    QCOMPARE(args.size(), 2);
    QCOMPARE(args[0].toString(), fixturePath_);
    const QVariantMap layers = args[1].toMap();
    QVERIFY(!layers.isEmpty());
    QVERIFY(!layers.value(QStringLiteral("mix")).toList().isEmpty());
    QVERIFY(!layers.value(QStringLiteral("bass")).toList().isEmpty());
    QVERIFY(!layers.value(QStringLiteral("mid")).toList().isEmpty());
    QVERIFY(!layers.value(QStringLiteral("high")).toList().isEmpty());
    QCOMPARE(layers.value(QStringLiteral("_durationMs")).toLongLong(), 2000);
    QCOMPARE(provider.analysisProgress(), 1.0);
}

void WaveformProviderTest::analysisEmitsPeaksForFixture()
{
    if (fixturePath_.isEmpty()) {
        QSKIP("AGPLAYER_TEST_WAV not set");
    }

    QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir cacheDir;
    QVERIFY(cacheDir.isValid());
    SettingsController settings;
    settings.setCacheDirectory(cacheDir.path());
    WaveformProvider provider(&settings);
    QSignalSpy spy(&provider, &WaveformProvider::waveformReady);

    provider.loadForTrack(fixturePath_);
    finishProviderAnalysis(provider);
    QCOMPARE(spy.count(), 1);
    const QList<QVariant> args = spy.takeFirst();
    QCOMPARE(args.size(), 2);
    QCOMPARE(args[0].toString(), fixturePath_);
    const QVariantMap layers = args[1].toMap();
    QVERIFY(!layers.isEmpty());
    const QVariantList mix = layers.value(QStringLiteral("mix")).toList();
    QVERIFY(!mix.isEmpty());
    QVERIFY(std::all_of(mix.begin(), mix.end(), [](const QVariant& value) {
        bool ok = false;
        const double peak = value.toDouble(&ok);
        return ok && std::isfinite(peak) && peak >= 0.0 && peak <= 1.0;
    }));
    QVERIFY(!layers.value(QStringLiteral("bass")).toList().isEmpty());
    QVERIFY(!layers.value(QStringLiteral("mid")).toList().isEmpty());
    QVERIFY(!layers.value(QStringLiteral("high")).toList().isEmpty());
    QCOMPARE(layers.value(QStringLiteral("_durationMs")).toLongLong(), 2000);
    QCOMPARE(layers.value(QStringLiteral("_sampleRate")).toInt(), 44'100);
    QCOMPARE(layers.value(QStringLiteral("_totalSamples")).toLongLong(), 88'200);
    QCOMPARE(layers.value(QStringLiteral("_peakCount")).toInt(),
             layers.value(QStringLiteral("mix")).toList().size());
    QCOMPARE(provider.analysisProgress(), 1.0);
}

void WaveformProviderTest::clickBeatsKeepTheirDecodedTimelinePositions()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("click-120.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(path, 120, 8));

    SettingsController settings;
    settings.setCacheDirectory(directory.filePath(QStringLiteral("cache")));
    WaveformProvider provider(&settings);
    QSignalSpy spy(&provider, &WaveformProvider::waveformReady);
    provider.loadForTrack(QStringLiteral("click-120"), path);
    finishProviderAnalysis(provider);

    QCOMPARE(spy.count(), 1);
    const QVariantMap layers = spy.takeFirst().at(1).toMap();
    const QVariantList mix = layers.value(QStringLiteral("mix")).toList();
    QVERIFY(mix.size() >= 1'000);
    QCOMPARE(layers.value(QStringLiteral("_durationMs")).toLongLong(), 8'000);
    QCOMPARE(layers.value(QStringLiteral("_sampleRate")).toInt(), 16'000);
    QCOMPARE(layers.value(QStringLiteral("_totalSamples")).toLongLong(), 128'000);
    QVERIFY(std::abs(layers.value(QStringLiteral("_bpm")).toDouble() - 120.0) <= 1.0);

    for (int beatMs = 500; beatMs < 8'000; beatMs += 500) {
        const int expected = static_cast<int>(
            std::floor(static_cast<double>(beatMs) * mix.size() / 8'000.0));
        int strongest = expected;
        double strongestValue = -1.0;
        const int finalIndex = static_cast<int>(mix.size()) - 1;
        for (int index = std::max(0, expected - 3);
             index <= std::min(finalIndex, expected + 3); ++index) {
            const double value = mix.at(index).toDouble();
            if (value > strongestValue) {
                strongestValue = value;
                strongest = index;
            }
        }
        const double renderedBeatMs =
            (static_cast<double>(strongest) + 0.5) * 8'000.0 / mix.size();
        QVERIFY2(strongestValue > 0.05,
                 qPrintable(QStringLiteral("missing beat near %1 ms").arg(beatMs)));
        QVERIFY2(std::abs(renderedBeatMs - beatMs) <= 14.0,
                 qPrintable(QStringLiteral("beat %1 ms mapped to %2 ms")
                                .arg(beatMs).arg(renderedBeatMs)));
    }
}

void WaveformProviderTest::duplicateCurrentRequestReusesGenerationAndResult()
{
    if (fixturePath_.isEmpty()) {
        QSKIP("AGPLAYER_TEST_WAV not set");
    }

    QTemporaryDir cacheDir;
    QVERIFY(cacheDir.isValid());
    SettingsController settings;
    settings.setCacheDirectory(cacheDir.path());
    WaveformProvider provider(&settings);
    QSignalSpy spy(&provider, &WaveformProvider::waveformReady);

    const qulonglong first = provider.loadForTrack(
        QStringLiteral("same-track"), fixturePath_);
    const qulonglong duplicate = provider.loadForTrack(
        QStringLiteral("same-track"), fixturePath_);
    QCOMPARE(duplicate, first);
    QCOMPARE(provider.activeGeneration(), first);
    finishProviderAnalysis(provider);
    QCOMPARE(spy.count(), 1);

    const qulonglong retained = provider.loadForTrack(
        QStringLiteral("same-track"), fixturePath_);
    QCOMPARE(retained, first);
    QCOMPARE(spy.count(), 2);
    const QVariantMap layers = spy.last().at(1).toMap();
    QCOMPARE(layers.value(QStringLiteral("_generation")).toULongLong(), first);
}

void WaveformProviderTest::unicodePathAnalyzesAndCaches()
{
    if (fixturePath_.isEmpty()) {
        QSKIP("AGPLAYER_TEST_WAV not set");
    }

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString sourceDir =
        tempDir.filePath(QStringLiteral("中文音乐"));
    QVERIFY(QDir().mkpath(sourceDir));
    const QString sourcePath =
        QDir(sourceDir).filePath(QStringLiteral("测试歌曲.wav"));
    QVERIFY(QFile::copy(fixturePath_, sourcePath));

    SettingsController settings;
    const QString cacheDir =
        tempDir.filePath(QStringLiteral("中文缓存"));
    settings.setCacheDirectory(cacheDir);
    WaveformProvider provider(&settings);
    QSignalSpy spy(&provider, &WaveformProvider::waveformReady);

    provider.loadForTrack(sourcePath);

    finishProviderAnalysis(provider);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), sourcePath);
    QVERIFY(!spy.at(0).at(1).toMap()
                 .value(QStringLiteral("mix")).toList().isEmpty());
    QCOMPARE(QDir(cacheDir).entryList(
                 {QStringLiteral("*.agwf")}, QDir::Files).size(), 1);
}

void WaveformProviderTest::aggregationChangeUsesSeparateCache()
{
    if (fixturePath_.isEmpty()) {
        QSKIP("AGPLAYER_TEST_WAV not set");
    }

    QTemporaryDir cacheDir;
    QVERIFY(cacheDir.isValid());
    SettingsController settings;
    settings.setCacheDirectory(cacheDir.path());

    const auto analyze = [&](const int algorithm) {
        settings.setWaveformPeakAlgorithm(algorithm);
        WaveformProvider provider(&settings);
        QSignalSpy spy(&provider, &WaveformProvider::waveformReady);
        provider.loadForTrack(fixturePath_);
        finishProviderAnalysis(provider);
        QCOMPARE(spy.count(), 1);
    };

    analyze(0);
    analyze(1);
    const QStringList caches = QDir(cacheDir.path()).entryList(
        {QStringLiteral("*.agwf")}, QDir::Files);
    QCOMPARE(caches.size(), 2);
    QVERIFY(std::any_of(caches.begin(), caches.end(),
                        [](const QString& name) {
                            return name.endsWith(QStringLiteral("-average.agwf"));
                        }));
    QVERIFY(std::any_of(caches.begin(), caches.end(),
                        [](const QString& name) {
                            return name.endsWith(QStringLiteral("-rms.agwf"));
                        }));
}

void WaveformProviderTest::newerTrackSuppressesStaleAnalysisResult()
{
    if (fixturePath_.isEmpty()) {
        QSKIP("AGPLAYER_TEST_WAV not set");
    }

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString firstPath = tempDir.filePath(QStringLiteral("first.wav"));
    const QString secondPath = tempDir.filePath(QStringLiteral("second.wav"));
    QVERIFY(QFile::copy(fixturePath_, firstPath));
    QVERIFY(QFile::copy(fixturePath_, secondPath));

    SettingsController settings;
    settings.setCacheDirectory(tempDir.filePath(QStringLiteral("cache")));
    WaveformProvider provider(&settings);
    QSignalSpy spy(&provider, &WaveformProvider::waveformReady);

    provider.loadForTrack(firstPath);
    provider.loadForTrack(secondPath);
    finishProviderAnalysis(provider);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), secondPath);
}

void WaveformProviderTest::resultCarriesTrackIdentityAndGeneration()
{
    if (fixturePath_.isEmpty()) {
        QSKIP("AGPLAYER_TEST_WAV not set");
    }

    QTemporaryDir cacheDir;
    QVERIFY(cacheDir.isValid());
    SettingsController settings;
    settings.setCacheDirectory(cacheDir.path());
    WaveformProvider provider(&settings);
    QSignalSpy spy(&provider, &WaveformProvider::waveformReady);

    const quint64 generation =
        provider.loadForTrack(QStringLiteral("track-identity"), fixturePath_);
    QVERIFY(generation > 0);
    finishProviderAnalysis(provider);

    const QVariantMap result = spy.takeFirst().at(1).toMap();
    QCOMPARE(result.value(QStringLiteral("_trackId")).toString(),
             QStringLiteral("track-identity"));
    QCOMPARE(result.value(QStringLiteral("_generation")).toULongLong(),
             generation);
    QCOMPARE(result.value(QStringLiteral("_sampleCount")).toInt(),
             result.value(QStringLiteral("mix")).toList().size());
    QCOMPARE(result.value(QStringLiteral("_sampleRate")).toInt(), 44'100);
    QCOMPARE(result.value(QStringLiteral("_totalSamples")).toLongLong(), 88'200);
    QCOMPARE(result.value(QStringLiteral("_peakCount")).toInt(),
             result.value(QStringLiteral("mix")).toList().size());
    QCOMPARE(result.value(QStringLiteral("_cacheVersion")).toInt(), 2);
}

void WaveformProviderTest::prefetchTracksWarmsCacheWithoutChangingCurrentTrack()
{
    if (fixturePath_.isEmpty()) {
        QSKIP("AGPLAYER_TEST_WAV not set");
    }

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString prefetchedPath =
        tempDir.filePath(QStringLiteral("prefetched.wav"));
    QVERIFY(QFile::copy(fixturePath_, prefetchedPath));

    SettingsController settings;
    const QString cacheDir = tempDir.filePath(QStringLiteral("cache"));
    settings.setCacheDirectory(cacheDir);
    WaveformProvider provider(&settings);
    QSignalSpy waveformSpy(&provider, &WaveformProvider::waveformReady);

    provider.prefetchTracks({prefetchedPath});
    finishProviderAnalysis(provider);
    QCOMPARE(
        QDir(cacheDir).entryList({QStringLiteral("*.agwf")}, QDir::Files).size(),
        1);
    QCOMPARE(waveformSpy.count(), 0);

    provider.loadForTrack(prefetchedPath);
    QCOMPARE(waveformSpy.count(), 1);
}

void WaveformProviderTest::successfulAnalysisAnnouncesWrittenCache()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("cache-ready.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(path, 120, 2));

    SettingsController settings;
    const QString cacheDirectory =
        directory.filePath(QStringLiteral("waveform-cache"));
    settings.setCacheDirectory(cacheDirectory);
    WaveformProvider provider(&settings);
    QSignalSpy waveformSpy(&provider, &WaveformProvider::waveformReady);
    QSignalSpy cacheSpy(&provider, &WaveformProvider::waveformCacheReady);

    provider.loadForTrack(QStringLiteral("cache-ready-track"), path);
    finishProviderAnalysis(provider);
    QCOMPARE(waveformSpy.count(), 1);
    QCOMPARE(cacheSpy.count(), 1);
    QCOMPARE(cacheSpy.takeFirst().at(0).toString(), path);
    QCOMPARE(QDir(cacheDirectory).entryList(
                 {QStringLiteral("*.agwf")}, QDir::Files).size(), 1);

    WaveformProvider cacheHitProvider(&settings);
    QSignalSpy cacheHitWaveformSpy(
        &cacheHitProvider, &WaveformProvider::waveformReady);
    QSignalSpy cacheHitAnnouncementSpy(
        &cacheHitProvider, &WaveformProvider::waveformCacheReady);
    cacheHitProvider.loadForTrack(QStringLiteral("cache-hit-track"), path);
    QCOMPARE(cacheHitWaveformSpy.count(), 1);
    QCOMPARE(cacheHitAnnouncementSpy.count(), 0);
}

void WaveformProviderTest::
currentAnalysisPreemptsQueuedPrefetchOnTheSingleWorker()
{
    if (fixturePath_.isEmpty()) QSKIP("AGPLAYER_TEST_WAV not set");
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString prefetchOne = temporary.filePath(QStringLiteral("prefetch-1.wav"));
    const QString current = temporary.filePath(QStringLiteral("current.wav"));
    const QString prefetchTwo = temporary.filePath(QStringLiteral("prefetch-2.wav"));
    QVERIFY(QFile::copy(fixturePath_, prefetchOne));
    QVERIFY(QFile::copy(fixturePath_, current));
    QVERIFY(QFile::copy(fixturePath_, prefetchTwo));

    SettingsController settings;
    const QString cacheDirectory = temporary.filePath(QStringLiteral("cache"));
    settings.setCacheDirectory(cacheDirectory);
    SchedulingBarrier barrier;
    barrier.firstPrefetchPath = prefetchOne;
    SchedulingHookGuard hookGuard(barrier);
    WaveformProvider provider(&settings);
    agplayer::testing::reset_waveform_provider_counters();

    provider.prefetchTracks({prefetchOne, prefetchTwo});
    {
        std::unique_lock lock(barrier.mutex);
        const bool started = barrier.condition.wait_for(
            lock, std::chrono::seconds(5), [&barrier] {
                return barrier.firstPrefetchStarted;
            });
        if (!started) {
            barrier.releaseFirstPrefetch = true;
            lock.unlock();
            barrier.condition.notify_all();
            QFAIL("first prefetch task did not reach the scheduling barrier");
        }
    }
    provider.loadForTrack(QStringLiteral("current"), current, false);
    provider.prefetchTracks({prefetchTwo});
    {
        std::lock_guard lock(barrier.mutex);
        barrier.releaseFirstPrefetch = true;
    }
    barrier.condition.notify_all();
    finishProviderAnalysis(provider);

    const std::vector<QString> expected{prefetchOne, current, prefetchTwo};
    QCOMPARE(barrier.startOrder, expected);
    QCOMPARE(agplayer::testing::waveform_provider_max_worker_tasks(), 1U);
    QCOMPARE(agplayer::testing::waveform_provider_frequency_jobs_started(), 0U);
    QCOMPARE(agplayer::testing::waveform_provider_frequency_cache_loads(), 0U);
    QCOMPARE(QDir(cacheDirectory).entryList(
                 {QStringLiteral("*.agwf")}, QDir::Files).size(), 3);
}

void WaveformProviderTest::failedAnalysisEmitsATerminalSignalWithIdentity()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString invalid = temporary.filePath(QStringLiteral("损坏音频.wav"));
    QFile file(invalid);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("not audio"), qint64(9));
    file.close();

    WaveformProvider provider;
    QSignalSpy failed(&provider, &WaveformProvider::waveformFailed);
    const qulonglong generation = provider.loadForTrack(
        QStringLiteral("result-generation-7"), invalid);
    finishProviderAnalysis(provider);
    QCOMPARE(failed.count(), 1);
    QCOMPARE(failed.first().at(0).toString(), invalid);
    QCOMPARE(failed.first().at(1).toString(), QStringLiteral("result-generation-7"));
    QCOMPARE(failed.first().at(2).toULongLong(), generation);
}

void WaveformProviderTest::cancellingAnalysisReleasesNativeResourcesWithoutTerminal()
{
    if (fixturePath_.isEmpty()) QSKIP("AGPLAYER_TEST_WAV not set");
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString source = temporary.filePath(QStringLiteral("cancel.wav"));
    QVERIFY(QFile::copy(fixturePath_, source));
    WaveformProvider provider;
    QSignalSpy ready(&provider, &WaveformProvider::waveformReady);
    QSignalSpy failed(&provider, &WaveformProvider::waveformFailed);

    provider.loadForTrack(QStringLiteral("cancel-track"), source);
    const std::weak_ptr<void> resources =
        WaveformProviderTestAccess::activeResources(provider);
    QVERIFY(!resources.expired());
    provider.cancelForTrack(source);
    WaveformProviderTestAccess::waitForAnalysis(provider);

    QVERIFY(resources.expired());
    QCOMPARE(ready.count(), 0);
    QCOMPARE(failed.count(), 0);
}

void WaveformProviderTest::
replacingAnalysisReleasesSupersededResourcesWithoutStaleTerminal()
{
    if (fixturePath_.isEmpty()) QSKIP("AGPLAYER_TEST_WAV not set");
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString first = temporary.filePath(QStringLiteral("first.wav"));
    const QString second = temporary.filePath(QStringLiteral("second.wav"));
    QVERIFY(QFile::copy(fixturePath_, first));
    QVERIFY(QFile::copy(fixturePath_, second));
    WaveformProvider provider;
    QSignalSpy ready(&provider, &WaveformProvider::waveformReady);
    QSignalSpy failed(&provider, &WaveformProvider::waveformFailed);

    provider.loadForTrack(QStringLiteral("first-track"), first);
    const std::weak_ptr<void> firstResources =
        WaveformProviderTestAccess::activeResources(provider);
    QVERIFY(!firstResources.expired());
    provider.loadForTrack(QStringLiteral("second-track"), second);
    const std::weak_ptr<void> secondResources =
        WaveformProviderTestAccess::activeResources(provider);
    QVERIFY(!secondResources.expired());
    finishProviderAnalysis(provider);

    QVERIFY(firstResources.expired());
    QCOMPARE(ready.count(), 1);
    QCOMPARE(ready.first().at(0).toString(), second);
    QCOMPARE(failed.count(), 0);
    QVERIFY(secondResources.expired());
}

void WaveformProviderTest::destroyingProviderReleasesInFlightNativeResources()
{
    if (fixturePath_.isEmpty()) QSKIP("AGPLAYER_TEST_WAV not set");
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString source = temporary.filePath(QStringLiteral("destroy.wav"));
    QVERIFY(QFile::copy(fixturePath_, source));
    std::weak_ptr<void> resources;
    {
        auto provider = std::make_unique<WaveformProvider>();
        provider->loadForTrack(QStringLiteral("destroy-track"), source);
        resources = WaveformProviderTestAccess::activeResources(*provider);
        QVERIFY(!resources.expired());
    }
    QVERIFY(resources.expired());
}

void WaveformProviderTest::nonFrequencyLoadsAndPrefetchNeverTouchFrequencyColor()
{
    if (fixturePath_.isEmpty()) QSKIP("AGPLAYER_TEST_WAV not set");
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    SettingsController settings;
    settings.setCacheDirectory(directory.filePath(QStringLiteral("cache")));
    WaveformProvider provider(&settings);
    QSignalSpy ready(&provider, &WaveformProvider::waveformReady);

    agplayer::testing::reset_waveform_provider_counters();
    provider.loadForTrack(QStringLiteral("mix-only"), fixturePath_, false);
    finishProviderAnalysis(provider);
    QCOMPARE(ready.count(), 1);
    provider.prefetchTracks({fixturePath_});
    finishProviderAnalysis(provider);

    QCOMPARE(agplayer::testing::waveform_provider_frequency_cache_loads(), 0U);
    QCOMPARE(agplayer::testing::waveform_provider_frequency_jobs_started(), 0U);
    QCOMPARE(agplayer::testing::waveform_provider_power_queries(), 0U);
}

void WaveformProviderTest::frequencyRequestPublishesMixBeforeBandsAndReusesBothCaches()
{
    if (fixturePath_.isEmpty()) QSKIP("AGPLAYER_TEST_WAV not set");
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    SettingsController settings;
    const QString cacheDirectory = directory.filePath(QStringLiteral("cache"));
    settings.setCacheDirectory(cacheDirectory);

    // Warm only AGWF through the legacy/non-frequency path.
    {
        WaveformProvider warm(&settings);
        QSignalSpy ready(&warm, &WaveformProvider::waveformReady);
        warm.loadForTrack(QStringLiteral("warm"), fixturePath_, false);
        finishProviderAnalysis(warm);
        QCOMPARE(ready.count(), 1);
    }

    agplayer::testing::reset_waveform_provider_counters();
    {
        WaveformProvider provider(&settings);
        QSignalSpy ready(&provider, &WaveformProvider::waveformReady);
        provider.loadForTrack(QStringLiteral("frequency"), fixturePath_, true);
        QCOMPARE(ready.count(), 1);
        QVERIFY(!ready.first().at(1).toMap()
                     .value(QStringLiteral("_frequencyReady")).toBool());
        finishProviderAnalysis(provider);
        QCOMPARE(ready.count(), 2);
        const QVariantMap completed = ready.last().at(1).toMap();
        QVERIFY(completed.value(QStringLiteral("_frequencyReady")).toBool());
        QVERIFY(!completed.value(QStringLiteral("bass")).toList().isEmpty());
        QCOMPARE(completed.value(QStringLiteral("_frequencyCacheVersion")).toInt(), 1);
        QCOMPARE(agplayer::testing::waveform_provider_frequency_jobs_started(), 1U);
        QCOMPARE(agplayer::testing::waveform_provider_max_frequency_jobs(), 1U);
    }

    const QStringList fcwFiles = QDir(cacheDirectory).entryList(
        {QStringLiteral("*.fcw1")}, QDir::Files);
    QCOMPARE(fcwFiles.size(), 1);

    agplayer::testing::reset_waveform_provider_counters();
    WaveformProvider cached(&settings);
    QSignalSpy cachedReady(&cached, &WaveformProvider::waveformReady);
    cached.loadForTrack(QStringLiteral("cached-frequency"), fixturePath_, true);
    QCOMPARE(cachedReady.count(), 2);
    QVERIFY(!cachedReady.first().at(1).toMap()
                 .value(QStringLiteral("_frequencyReady")).toBool());
    QVERIFY(cachedReady.last().at(1).toMap()
                .value(QStringLiteral("_frequencyReady")).toBool());
    QCOMPARE(agplayer::testing::waveform_provider_frequency_cache_loads(), 1U);
    QCOMPARE(agplayer::testing::waveform_provider_frequency_jobs_started(), 0U);
}

void WaveformProviderTest::
frequencyCacheHitWithoutAgwfRebuildsBothCachesInOneFrequencyJob()
{
    if (fixturePath_.isEmpty()) QSKIP("AGPLAYER_TEST_WAV not set");
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    SettingsController settings;
    const QString cacheDirectory = directory.filePath(QStringLiteral("cache"));
    settings.setCacheDirectory(cacheDirectory);

    {
        WaveformProvider warm(&settings);
        QSignalSpy ready(&warm, &WaveformProvider::waveformReady);
        warm.loadForTrack(QStringLiteral("warm-frequency"), fixturePath_, true);
        finishProviderAnalysis(warm);
        QCOMPARE(ready.count(), 2);
    }
    const QStringList agwfFiles = QDir(cacheDirectory).entryList(
        {QStringLiteral("*.agwf")}, QDir::Files);
    QCOMPARE(agwfFiles.size(), 1);
    QVERIFY(QFile::remove(QDir(cacheDirectory).filePath(agwfFiles.first())));

    agplayer::testing::reset_waveform_provider_counters();
    WaveformProvider provider(&settings);
    QSignalSpy ready(&provider, &WaveformProvider::waveformReady);
    provider.loadForTrack(QStringLiteral("fcw-only"), fixturePath_, true);
    finishProviderAnalysis(provider);
    QCOMPARE(ready.count(), 2);
    QVERIFY(!ready.first().at(1).toMap()
                 .value(QStringLiteral("_frequencyReady")).toBool());
    const QVariantMap complete = ready.last().at(1).toMap();
    QVERIFY(complete.value(QStringLiteral("_frequencyReady")).toBool());
    const int mixCount = complete.value(QStringLiteral("mix")).toList().size();
    QVERIFY(mixCount > 0);
    QCOMPARE(complete.value(QStringLiteral("bass")).toList().size(), mixCount);
    QCOMPARE(complete.value(QStringLiteral("mid")).toList().size(), mixCount);
    QCOMPARE(complete.value(QStringLiteral("high")).toList().size(), mixCount);
    QCOMPARE(agplayer::testing::waveform_provider_frequency_jobs_started(), 1U);
    QCOMPARE(agplayer::testing::waveform_provider_mix_jobs_started(), 0U);
    QCOMPARE(agplayer::testing::waveform_provider_decoder_opens(), 1U);

    agplayer::testing::reset_waveform_provider_counters();
    WaveformProvider cached(&settings);
    QSignalSpy cachedReady(&cached, &WaveformProvider::waveformReady);
    cached.loadForTrack(QStringLiteral("rebuilt-cache-hit"), fixturePath_, true);
    QCOMPARE(cachedReady.count(), 2);
    QVERIFY(cachedReady.last().at(1).toMap()
                .value(QStringLiteral("_frequencyReady")).toBool());
    QCOMPARE(agplayer::testing::waveform_provider_frequency_jobs_started(), 0U);
    QCOMPARE(agplayer::testing::waveform_provider_mix_jobs_started(), 0U);
    QCOMPARE(agplayer::testing::waveform_provider_decoder_opens(), 0U);
}

void WaveformProviderTest::
frequencyCacheSaveFailureKeepsMixAndExistingFilesSafe()
{
    if (fixturePath_.isEmpty()) QSKIP("AGPLAYER_TEST_WAV not set");
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = directory.filePath(QStringLiteral("source.wav"));
    QVERIFY(QFile::copy(fixturePath_, source));
    SettingsController settings;
    const QString cacheDirectory = directory.filePath(QStringLiteral("cache"));
    settings.setCacheDirectory(cacheDirectory);

    {
        WaveformProvider warm(&settings);
        QSignalSpy ready(&warm, &WaveformProvider::waveformReady);
        warm.loadForTrack(QStringLiteral("warm-frequency"), source, true);
        finishProviderAnalysis(warm);
        QCOMPARE(ready.count(), 2);
    }
    QDir cache(cacheDirectory);
    const QStringList agwfFiles = cache.entryList(
        {QStringLiteral("*.agwf")}, QDir::Files);
    const QStringList fcwFiles = cache.entryList(
        {QStringLiteral("*.fcw1")}, QDir::Files);
    QCOMPARE(agwfFiles.size(), 1);
    QCOMPARE(fcwFiles.size(), 1);
    const QString agwfPath = cache.filePath(agwfFiles.first());
    const QString fcwPath = cache.filePath(fcwFiles.first());
    QFile sourceFile(source);
    QVERIFY(sourceFile.open(QIODevice::ReadOnly));
    const QByteArray sourceBefore = sourceFile.readAll();
    QFile fcwFile(fcwPath);
    QVERIFY(fcwFile.open(QIODevice::ReadOnly));
    const QByteArray fcwBefore = fcwFile.readAll();
    QVERIFY(!fcwBefore.isEmpty());
    QVERIFY(QFile::remove(agwfPath));

    agplayer::testing::fail_next_frequency_color_cache_replace();
    WaveformProvider provider(&settings);
    QSignalSpy ready(&provider, &WaveformProvider::waveformReady);
    provider.loadForTrack(QStringLiteral("save-failure"), source, true);
    finishProviderAnalysis(provider);

    QCOMPARE(ready.count(), 1);
    const QVariantMap mixOnly = ready.first().at(1).toMap();
    QVERIFY(!mixOnly.value(QStringLiteral("mix")).toList().isEmpty());
    QVERIFY(!mixOnly.value(QStringLiteral("_frequencyReady")).toBool());
    QCOMPARE(provider.analysisProgress(), 1.0);
    QVERIFY(QFileInfo::exists(agwfPath));
    QFile preservedFcw(fcwPath);
    QVERIFY(preservedFcw.open(QIODevice::ReadOnly));
    QCOMPARE(preservedFcw.readAll(), fcwBefore);
    QFile preservedSource(source);
    QVERIFY(preservedSource.open(QIODevice::ReadOnly));
    QCOMPARE(preservedSource.readAll(), sourceBefore);
    QVERIFY(cache.entryList(
        {QStringLiteral("*.tmp-*"), QStringLiteral("*.tmp")},
        QDir::Files).isEmpty());

    WaveformProvider cached(&settings);
    QSignalSpy cachedReady(&cached, &WaveformProvider::waveformReady);
    cached.loadForTrack(QStringLiteral("preserved-cache"), source, true);
    QCOMPARE(cachedReady.count(), 2);
    QVERIFY(cachedReady.last().at(1).toMap()
                .value(QStringLiteral("_frequencyReady")).toBool());
}

void WaveformProviderTest::corruptFrequencyCacheIsRebuilt()
{
    if (fixturePath_.isEmpty()) QSKIP("AGPLAYER_TEST_WAV not set");
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    SettingsController settings;
    const QString cacheDirectory = directory.filePath(QStringLiteral("cache"));
    settings.setCacheDirectory(cacheDirectory);
    {
        WaveformProvider warm(&settings);
        QSignalSpy ready(&warm, &WaveformProvider::waveformReady);
        warm.loadForTrack(QStringLiteral("warm-frequency"), fixturePath_, true);
        finishProviderAnalysis(warm);
        QCOMPARE(ready.count(), 2);
    }
    const QStringList fcwFiles = QDir(cacheDirectory).entryList(
        {QStringLiteral("*.fcw1")}, QDir::Files);
    QCOMPARE(fcwFiles.size(), 1);
    QFile corrupt(QDir(cacheDirectory).filePath(fcwFiles.first()));
    QVERIFY(corrupt.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(corrupt.write("bad"), qint64(3));
    corrupt.close();

    agplayer::testing::reset_waveform_provider_counters();
    WaveformProvider provider(&settings);
    QSignalSpy ready(&provider, &WaveformProvider::waveformReady);
    provider.loadForTrack(QStringLiteral("rebuild-frequency"), fixturePath_, true);
    QCOMPARE(ready.count(), 1);
    finishProviderAnalysis(provider);
    QCOMPARE(ready.count(), 2);
    QVERIFY(ready.last().at(1).toMap()
                .value(QStringLiteral("_frequencyReady")).toBool());
    QCOMPARE(agplayer::testing::waveform_provider_frequency_jobs_started(), 1U);
    QVERIFY(QFileInfo(corrupt.fileName()).size() > 64);
}

void WaveformProviderTest::switchingToMixOnlyCancelsFrequencyWithoutStaleBands()
{
    if (fixturePath_.isEmpty()) QSKIP("AGPLAYER_TEST_WAV not set");
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    SettingsController settings;
    settings.setCacheDirectory(directory.filePath(QStringLiteral("cache")));
    WaveformProvider provider(&settings);
    provider.setAudioResourcePressure(true);
    QSignalSpy ready(&provider, &WaveformProvider::waveformReady);

    provider.loadForTrack(QStringLiteral("same"), fixturePath_, true);
    QTRY_COMPARE_WITH_TIMEOUT(
        agplayer::testing::waveform_provider_frequency_jobs_started(), 1U, 5'000);
    provider.loadForTrack(QStringLiteral("same"), fixturePath_, false);
    provider.setAudioResourcePressure(false);
    finishProviderAnalysis(provider);

    QVERIFY(!ready.isEmpty());
    for (const QList<QVariant>& emission : ready) {
        QVERIFY(!emission.at(1).toMap()
                     .value(QStringLiteral("_frequencyReady")).toBool());
    }
}

void WaveformProviderTest::frequencyPauseResumesWithoutBusyProgress()
{
    if (fixturePath_.isEmpty()) QSKIP("AGPLAYER_TEST_WAV not set");
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    SettingsController settings;
    settings.setCacheDirectory(directory.filePath(QStringLiteral("cache")));
    WaveformProvider provider(&settings);
    QSignalSpy ready(&provider, &WaveformProvider::waveformReady);

    agplayer::testing::reset_waveform_provider_counters();
    provider.setAudioResourcePressure(true);
    provider.loadForTrack(QStringLiteral("paused"), fixturePath_, true);
    QCOMPARE(agplayer::testing::waveform_provider_power_queries(), 0U);
    QTRY_COMPARE_WITH_TIMEOUT(
        agplayer::testing::waveform_provider_frequency_jobs_started(), 1U, 5'000);
    QCOMPARE(agplayer::testing::waveform_provider_frequency_progress_callbacks(), 0U);
#ifdef Q_OS_WIN
    QTRY_COMPARE_WITH_TIMEOUT(
        agplayer::testing::waveform_provider_power_queries(), 1U, 1'000);
#else
    QCOMPARE(agplayer::testing::waveform_provider_power_queries(), 0U);
#endif
    provider.setAudioResourcePressure(false);
    finishProviderAnalysis(provider);
    QCOMPARE(ready.count(), 2);
    QVERIFY(agplayer::testing::waveform_provider_frequency_progress_callbacks() > 0U);
}

void WaveformProviderTest::cancellingFrequencyResetsPowerStateAndRestarts()
{
#ifndef Q_OS_WIN
    QSKIP("native power-state polling is Windows-only");
#else
    if (fixturePath_.isEmpty()) QSKIP("AGPLAYER_TEST_WAV not set");
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    SettingsController settings;
    settings.setCacheDirectory(directory.filePath(QStringLiteral("cache")));
    WaveformProvider provider(&settings);

    agplayer::testing::reset_waveform_provider_counters();
    provider.setAudioResourcePressure(true);
    provider.loadForTrack(QStringLiteral("first"), fixturePath_, true);
    QTRY_COMPARE_WITH_TIMEOUT(
        agplayer::testing::waveform_provider_power_queries(), 1U, 1'000);
    FrequencyPowerStateTestAccess::forceEnergySaverActive(provider);
    provider.cancelFrequencyForTrack(fixturePath_);
    QVERIFY(!FrequencyPowerStateTestAccess::energySaverActive(provider));
    QCOMPARE(FrequencyPowerStateTestAccess::lastPowerQueryElapsedMs(provider),
             qint64(-1));

    provider.setAudioResourcePressure(false);
    QSignalSpy restartedReady(&provider, &WaveformProvider::waveformReady);
    provider.loadForTrack(QStringLiteral("second"), fixturePath_, true);
    finishProviderAnalysis(provider);
    QCOMPARE(restartedReady.count(), 2);
    QVERIFY(!restartedReady.first().at(1).toMap()
                 .value(QStringLiteral("_frequencyReady")).toBool());
    QVERIFY(restartedReady.last().at(1).toMap()
                .value(QStringLiteral("_frequencyReady")).toBool());
#endif
}

void WaveformProviderTest::frequencyJobsAreSerializedAndDestructionJoins()
{
    if (fixturePath_.isEmpty()) QSKIP("AGPLAYER_TEST_WAV not set");
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    SettingsController settings;
    settings.setCacheDirectory(directory.filePath(QStringLiteral("cache")));
    agplayer::testing::reset_waveform_provider_counters();
    std::weak_ptr<void> resources;
    {
        auto provider = std::make_unique<WaveformProvider>(&settings);
        provider->setAudioResourcePressure(true);
        provider->loadForTrack(QStringLiteral("first"), fixturePath_, true);
        resources = WaveformProviderTestAccess::activeResources(*provider);
        QTRY_COMPARE_WITH_TIMEOUT(
            agplayer::testing::waveform_provider_frequency_jobs_started(), 1U, 5'000);
        provider->loadForTrack(QStringLiteral("second"), fixturePath_, true);
    }
    QVERIFY(resources.expired());
    QCOMPARE(agplayer::testing::waveform_provider_max_frequency_jobs(), 1U);
}

void WaveformProviderTest::cacheCleanupBudgetsAgwfAndFcwWithoutCollateralDeletion()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString directory = temporary.filePath(QStringLiteral("AgPlayer-cache"));
    QVERIFY(QDir().mkpath(directory));
    const auto writeSized = [&](const QString& name, const char fill) -> QString {
        QFile file(QDir(directory).filePath(name));
        if (!file.open(QIODevice::WriteOnly)
            || file.write(QByteArray(10, fill)) != qint64(10)) {
            return {};
        }
        file.close();
        return file.fileName();
    };
    const QString source = writeSized(QStringLiteral("source.wav"), 's');
    const QString agwf = writeSized(QStringLiteral("pair.agwf"), 'a');
    const QString fcw = writeSized(QStringLiteral("pair.fcw1"), 'f');
    const QString unrelated = writeSized(QStringLiteral("keep.bin"), 'k');
    QVERIFY(!source.isEmpty());
    QVERIFY(!agwf.isEmpty());
    QVERIFY(!fcw.isEmpty());
    QVERIFY(!unrelated.isEmpty());
    QFile oldFile(fcw);
    QVERIFY(oldFile.open(QIODevice::ReadWrite));
    QVERIFY(oldFile.setFileTime(QDateTime::currentDateTime().addDays(-1),
                                QFileDevice::FileAccessTime));
    QVERIFY(oldFile.setFileTime(QDateTime::currentDateTime().addDays(-1),
                                QFileDevice::FileModificationTime));
    oldFile.close();
    QFile newFile(agwf);
    QVERIFY(newFile.open(QIODevice::ReadWrite));
    QVERIFY(newFile.setFileTime(QDateTime::currentDateTime(),
                                QFileDevice::FileAccessTime));
    newFile.close();

    const CacheJanitor::TrimReport report = CacheJanitor::trimToSize(directory, 15);
    QCOMPARE(report.filesRemoved, 1);
    QVERIFY(!QFileInfo::exists(fcw));
    QVERIFY(QFileInfo::exists(agwf));
    QVERIFY(QFileInfo::exists(source));
    QVERIFY(QFileInfo::exists(unrelated));
}

QTEST_MAIN(WaveformProviderTest)
#include "waveform_provider_test.moc"
