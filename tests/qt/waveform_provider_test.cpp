#include "settings_controller.hpp"
#include "waveform_provider.hpp"
#include "waveform_provider_test_access.hpp"
#include "../../core/src/waveform_cache.hpp"

#include <QCoreApplication>
#include <QDataStream>
#include <QFile>
#include <QDir>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <cstdint>
#include <algorithm>

struct WaveformPrefetchTestAccess {
    static std::shared_ptr<ag_cancel_token> preparePrefetch(WaveformProvider& provider)
    {
        auto resources = std::make_shared<WaveformProvider::AnalysisResources>();
        resources->cancelToken = ag_cancel_token_create();
        provider.prefetchResources_ = resources;
        return {resources, resources->cancelToken};
    }
};

namespace agplayer::testing {

void reset_waveform_provider_counters() noexcept;
std::uint64_t waveform_provider_jobs_started() noexcept;
std::uint64_t waveform_provider_prefetch_jobs_started() noexcept;

} // namespace agplayer::testing

namespace {

void finishProviderAnalysis(WaveformProvider& provider)
{
    WaveformProviderTestAccess::waitForAnalysis(provider);
    QCoreApplication::processEvents();
}

QVariantMap lastLayers(const QSignalSpy& spy)
{
    return spy.last().at(1).toMap();
}

} // namespace

class WaveformProviderTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void emptyPathEmitsEmptyWaveform();
    void plainAnalysisPreservesAmplitudeAndRgbBands();
    void plainCacheHitDoesNotStartAnalysis();
    void frequencyModeUsesTheSameFourLayerAnalysis();
    void displayModeChangeKeepsOneActiveAnalysis();
    void frequencyCacheHitPublishesWithoutDecode();
    void switchingBackToPlainReusesTheSameCache();
    void destroyingProviderJoinsTheExistingWorkerPool();
    void longTrackRetainsDetailAndNonzeroTail();
    void switchingTrackCancelsPrefetch();
    void destroyingProviderCancelsPrefetch();
    void canceledRunningPrefetchDoesNotPublishCache();
    void canceledCompletedPrefetchDoesNotPublishQueuedNotification();
    void pendingPreviewHasFixedTimelineAndIsSupersededByFinalData();
    void errorAfterPreviewClearsPublishedWaveform();
    void maximumDensityCacheLoadIsMeasuredWithoutDecode();

private:
    QString fixturePath_;
};

void WaveformProviderTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    fixturePath_ = QString::fromUtf8(qgetenv("AGPLAYER_TEST_WAV"));
}

void WaveformProviderTest::emptyPathEmitsEmptyWaveform()
{
    WaveformProvider provider;
    QSignalSpy ready(&provider, &WaveformProvider::waveformReady);
    provider.loadForTrack(QString());
    QCOMPARE(ready.count(), 1);
    QVERIFY(ready.first().at(1).toMap().isEmpty());
    QCOMPARE(provider.analysisProgress(), 1.0);
}

void WaveformProviderTest::plainAnalysisPreservesAmplitudeAndRgbBands()
{
    if (fixturePath_.isEmpty()) QSKIP("AGPLAYER_TEST_WAV not set");
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    SettingsController settings;
    settings.setCacheDirectory(directory.filePath(QStringLiteral("cache")));
    WaveformProvider provider(&settings);
    QSignalSpy ready(&provider, &WaveformProvider::waveformReady);

    provider.loadForTrack(QStringLiteral("plain"), fixturePath_, false);
    finishProviderAnalysis(provider);

    QCOMPARE(ready.count(), 1);
    const QVariantMap layers = lastLayers(ready);
    const int pointCount = layers.value(QStringLiteral("mix")).toList().size();
    // Single-pass integer frame buckets stay within the requested bound;
    // the final partial bucket and exact PCM duration survive rounding.
    QVERIFY(pointCount >= 16384 && pointCount <= 32768);
    QCOMPARE(layers.value(QStringLiteral("bass")).toList().size(), pointCount);
    QCOMPARE(layers.value(QStringLiteral("mid")).toList().size(), pointCount);
    QCOMPARE(layers.value(QStringLiteral("high")).toList().size(), pointCount);
    QVERIFY(!layers.contains(QStringLiteral("spectralIndex")));
    QVERIFY(!layers.value(QStringLiteral("_frequencyReady")).toBool());
    QCOMPARE(layers.value(QStringLiteral("_cacheVersion")).toInt(), 4);
}

void WaveformProviderTest::plainCacheHitDoesNotStartAnalysis()
{
    if (fixturePath_.isEmpty()) QSKIP("AGPLAYER_TEST_WAV not set");
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    SettingsController settings;
    settings.setCacheDirectory(directory.filePath(QStringLiteral("cache")));
    {
        WaveformProvider warm(&settings);
        warm.loadForTrack(QStringLiteral("warm"), fixturePath_, false);
        finishProviderAnalysis(warm);
    }
    agplayer::testing::reset_waveform_provider_counters();
    WaveformProvider cached(&settings);
    QSignalSpy ready(&cached, &WaveformProvider::waveformReady);
    cached.loadForTrack(QStringLiteral("cached"), fixturePath_, false);
    QCOMPARE(ready.count(), 1);
    QCOMPARE(agplayer::testing::waveform_provider_jobs_started(), 0U);
}

void WaveformProviderTest::frequencyModeUsesTheSameFourLayerAnalysis()
{
    if (fixturePath_.isEmpty()) QSKIP("AGPLAYER_TEST_WAV not set");
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    SettingsController settings;
    settings.setCacheDirectory(directory.filePath(QStringLiteral("cache")));
    WaveformProvider provider(&settings);
    agplayer::testing::reset_waveform_provider_counters();
    QSignalSpy ready(&provider, &WaveformProvider::waveformReady);
    provider.loadForTrack(QStringLiteral("frequency"), fixturePath_, true);
    finishProviderAnalysis(provider);
    QCOMPARE(agplayer::testing::waveform_provider_jobs_started(), 1U);
    const QVariantMap layers = lastLayers(ready);
    const int pointCount = layers.value(QStringLiteral("mix")).toList().size();
    QCOMPARE(layers.value(QStringLiteral("bass")).toList().size(), pointCount);
    QCOMPARE(layers.value(QStringLiteral("mid")).toList().size(), pointCount);
    QCOMPARE(layers.value(QStringLiteral("high")).toList().size(), pointCount);
    QVERIFY(!layers.contains(QStringLiteral("spectralIndex")));
    QVERIFY(layers.value(QStringLiteral("_frequencyReady")).toBool());
}

void WaveformProviderTest::displayModeChangeKeepsOneActiveAnalysis()
{
    if (fixturePath_.isEmpty()) QSKIP("AGPLAYER_TEST_WAV not set");
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    SettingsController settings;
    const QString cacheDirectory = directory.filePath(QStringLiteral("cache"));
    settings.setCacheDirectory(cacheDirectory);
    agplayer::testing::reset_waveform_provider_counters();
    WaveformProvider provider(&settings);
    QSignalSpy ready(&provider, &WaveformProvider::waveformReady);
    const qulonglong generation = provider.loadForTrack(
        QStringLiteral("same"), fixturePath_, true);
    QCOMPARE(provider.loadForTrack(QStringLiteral("same"), fixturePath_, false),
             generation);
    finishProviderAnalysis(provider);

    QCOMPARE(agplayer::testing::waveform_provider_jobs_started(), 1U);
    QCOMPARE(ready.count(), 1);
    QVERIFY(!lastLayers(ready).value(QStringLiteral("_frequencyReady")).toBool());
    QCOMPARE(QDir(cacheDirectory).entryList(
                 {QStringLiteral("*.agwf")}, QDir::Files).size(), 1);
}

void WaveformProviderTest::frequencyCacheHitPublishesWithoutDecode()
{
    if (fixturePath_.isEmpty()) QSKIP("AGPLAYER_TEST_WAV not set");
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    SettingsController settings;
    settings.setCacheDirectory(directory.filePath(QStringLiteral("cache")));
    {
        WaveformProvider warm(&settings);
        warm.loadForTrack(QStringLiteral("warm"), fixturePath_, true);
        finishProviderAnalysis(warm);
    }

    agplayer::testing::reset_waveform_provider_counters();
    WaveformProvider cached(&settings);
    QSignalSpy ready(&cached, &WaveformProvider::waveformReady);
    cached.loadForTrack(QStringLiteral("cached"), fixturePath_, true);
    QCOMPARE(ready.count(), 1);
    const QVariantMap layers = lastLayers(ready);
    QVERIFY(layers.value(QStringLiteral("_frequencyReady")).toBool());
    QVERIFY(!layers.contains(QStringLiteral("spectralIndex")));
    QVERIFY(!layers.value(QStringLiteral("bass")).toList().isEmpty());
    QVERIFY(!layers.value(QStringLiteral("mid")).toList().isEmpty());
    QVERIFY(!layers.value(QStringLiteral("high")).toList().isEmpty());
    QCOMPARE(agplayer::testing::waveform_provider_jobs_started(), 0U);
}

void WaveformProviderTest::switchingBackToPlainReusesTheSameCache()
{
    if (fixturePath_.isEmpty()) QSKIP("AGPLAYER_TEST_WAV not set");
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    SettingsController settings;
    settings.setCacheDirectory(directory.filePath(QStringLiteral("cache")));
    WaveformProvider provider(&settings);
    QSignalSpy ready(&provider, &WaveformProvider::waveformReady);

    provider.loadForTrack(QStringLiteral("same"), fixturePath_, true);
    finishProviderAnalysis(provider);
    agplayer::testing::reset_waveform_provider_counters();
    provider.loadForTrack(QStringLiteral("same"), fixturePath_, false);

    QVERIFY(!ready.isEmpty());
    QVERIFY(!lastLayers(ready).value(QStringLiteral("_frequencyReady")).toBool());
    QCOMPARE(agplayer::testing::waveform_provider_jobs_started(), 0U);
}

void WaveformProviderTest::destroyingProviderJoinsTheExistingWorkerPool()
{
    if (fixturePath_.isEmpty()) QSKIP("AGPLAYER_TEST_WAV not set");
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    SettingsController settings;
    settings.setCacheDirectory(directory.filePath(QStringLiteral("cache")));
    std::weak_ptr<void> resources;
    {
        auto provider = std::make_unique<WaveformProvider>(&settings);
        provider->loadForTrack(QStringLiteral("destroy"), fixturePath_, true);
        resources = WaveformProviderTestAccess::activeResources(*provider);
        QVERIFY(!resources.expired());
    }
    QVERIFY(resources.expired());
}

void WaveformProviderTest::longTrackRetainsDetailAndNonzeroTail()
{
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("long-tail.wav"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    constexpr quint32 frames = 8000 * 180;
    stream.writeRawData("RIFF", 4);
    stream << quint32(36 + frames * 2);
    stream.writeRawData("WAVEfmt ", 8);
    stream << quint32(16) << quint16(1) << quint16(1) << quint32(8000)
           << quint32(16000) << quint16(2) << quint16(16);
    stream.writeRawData("data", 4);
    stream << quint32(frames * 2);
    for (quint32 i = 0; i < frames; ++i)
        stream << qint16(i % 16 < 8 ? 12000 : -12000);
    file.close();
    WaveformProvider provider;
    QSignalSpy ready(&provider, &WaveformProvider::waveformReady);
    provider.loadForTrack(QStringLiteral("long"), path, true);
    finishProviderAnalysis(provider);
    QCOMPARE(ready.count(), 1);
    const auto layers = lastLayers(ready);
    QCOMPARE(layers.value(QStringLiteral("_durationMs")).toLongLong(), qint64(180000));
    const auto mix = layers.value(QStringLiteral("mix")).toList();
    // Enough independent source samples for at least one bucket per pixel in
    // a 1600px, 8-beat (4sec) view. Interpolating 32K/full-song peaks cannot do this.
    QVERIFY(mix.size() * 4 / 180 >= 1600);
    QVERIFY(mix.last().toDouble() > 0.1);
}

void WaveformProviderTest::pendingPreviewHasFixedTimelineAndIsSupersededByFinalData()
{
    if (fixturePath_.isEmpty()) QSKIP("AGPLAYER_TEST_WAV not set");
    WaveformProvider provider;
    QSignalSpy ready(&provider, &WaveformProvider::waveformReady);
    provider.loadForTrack(QStringLiteral("progress"), fixturePath_, true);
    // Drain worker without processing completion events, then deliver its
    // conflated immutable preview through the same GUI timer path.
    WaveformProviderTestAccess::waitForAnalysis(provider);
    WaveformProviderTestAccess::publishPendingPreview(provider);
    QCOMPARE(ready.size(), 1);
    const auto preview = lastLayers(ready);
    QVERIFY(!preview.value(QStringLiteral("_complete")).toBool());
    const auto peak = preview.value(QStringLiteral("peak")).toList();
    QVERIFY(peak.size() <= 4096);
    QVERIFY(!peak.isEmpty() && peak.first().toDouble() > 0.0);
    QCOMPARE(peak.last().toDouble(), 0.0);
    QCOMPARE(preview.value(QStringLiteral("rms")).toList().size(), peak.size());
    QCoreApplication::processEvents();
    QCOMPARE(ready.size(), 2);
    const auto final = lastLayers(ready);
    QVERIFY(final.value(QStringLiteral("_complete")).toBool());
    QCOMPARE(final.value(QStringLiteral("_totalSamples")).toULongLong(), qulonglong(88200));
    QCOMPARE(final.value(QStringLiteral("_generation")), preview.value(QStringLiteral("_generation")));
    QCOMPARE(preview.value(QStringLiteral("peak")).toList().last().toDouble(), 0.0);
}

void WaveformProviderTest::maximumDensityCacheLoadIsMeasuredWithoutDecode()
{
    if (fixturePath_.isEmpty()) QSKIP("AGPLAYER_TEST_WAV not set");
    QTemporaryDir directory;
    SettingsController settings;
    settings.setCacheDirectory(directory.path());
    {
        WaveformProvider warm(&settings);
        warm.loadForTrack(QStringLiteral("warm"), fixturePath_, false);
        finishProviderAnalysis(warm);
    }
    const auto entries = QDir(directory.path()).entryList({QStringLiteral("*.agwf")}, QDir::Files);
    QCOMPARE(entries.size(), 1);
    const auto source = std::filesystem::u8path(fixturePath_.toUtf8().constData());
    const auto cache = std::filesystem::u8path(directory.filePath(entries.first()).toUtf8().constData());
    agplayer::WaveformCacheData data;
    QVERIFY(agplayer::WaveformCache::load_v4(cache, source, data));
    for (auto* layer : {&data.mix, &data.peak, &data.rms, &data.bass, &data.mid, &data.high})
        layer->assign(524288, 0.5F);
    QVERIFY(agplayer::WaveformCache::save_v4(cache, source, data));
    agplayer::testing::reset_waveform_provider_counters();
    QList<double> timings;
    for (int sample = 0; sample < 3; ++sample) {
        WaveformProvider provider(&settings);
        QSignalSpy ready(&provider, &WaveformProvider::waveformReady);
        QElapsedTimer timer;
        timer.start();
        provider.loadForTrack(QStringLiteral("cached"), fixturePath_, false);
        timings.append(timer.nsecsElapsed() / 1000000.0);
        QCOMPARE(ready.count(), 0); // a full six-layer cache never expands on GUI
        QTRY_COMPARE(ready.count(), 1);
        QCOMPARE(lastLayers(ready).value(QStringLiteral("peak")).toList().size(), 524288);
    }
    std::sort(timings.begin(), timings.end());
    QCOMPARE(agplayer::testing::waveform_provider_jobs_started(), 0U);
    qInfo("maximum waveform cache 6x524288 dispatch median_ms=%.3f", timings.at(1));
    WaveformProvider switched(&settings);
    QSignalSpy ready(&switched, &WaveformProvider::waveformReady);
    switched.loadForTrack(QStringLiteral("cached"), fixturePath_, false);
    WaveformProviderTestAccess::waitForAnalysis(switched);
    QCOMPARE(ready.count(), 0);
    switched.loadForTrack(QString());
    QCoreApplication::processEvents();
    QCOMPARE(ready.count(), 1);
    QVERIFY(lastLayers(ready).isEmpty());
}

void WaveformProviderTest::errorAfterPreviewClearsPublishedWaveform()
{
    WaveformProvider provider;
    QSignalSpy ready(&provider, &WaveformProvider::waveformReady);
    QSignalSpy failed(&provider, &WaveformProvider::waveformFailed);
    WaveformProviderTestAccess::failAfterPreview(provider);
    QCOMPARE(failed.size(), 1);
    QCOMPARE(ready.size(), 1);
    const auto cleared = lastLayers(ready);
    QVERIFY(cleared.value(QStringLiteral("mix")).toList().isEmpty());
    QVERIFY(!cleared.value(QStringLiteral("_frequencyReady")).toBool());
    QCOMPARE(cleared.value(QStringLiteral("_trackId")).toString(), QStringLiteral("broken"));
}

void WaveformProviderTest::switchingTrackCancelsPrefetch()
{
    if (fixturePath_.isEmpty()) QSKIP("AGPLAYER_TEST_WAV not set");
    QTemporaryDir directory;
    SettingsController settings;
    settings.setCacheDirectory(directory.filePath(QStringLiteral("cache")));
    WaveformProvider provider(&settings);
    const auto token = WaveformPrefetchTestAccess::preparePrefetch(provider);
    QVERIFY(token);
    provider.prefetchTracks({fixturePath_});
    provider.loadForTrack(QString());
    ag_waveform* waveform = nullptr;
    const auto result = ag_waveform_analyze_with_aggregation(
        fixturePath_.toUtf8().constData(), 32,
        AG_WAVEFORM_AGGREGATION_AVERAGE_ABSOLUTE, token.get(), nullptr, nullptr,
        &waveform);
    if (waveform) ag_waveform_destroy(waveform);
    QCOMPARE(result, AG_CANCELLED);
}

void WaveformProviderTest::destroyingProviderCancelsPrefetch()
{
    if (fixturePath_.isEmpty()) QSKIP("AGPLAYER_TEST_WAV not set");
    QTemporaryDir directory;
    SettingsController settings;
    settings.setCacheDirectory(directory.filePath(QStringLiteral("cache")));
    auto provider = std::make_unique<WaveformProvider>(&settings);
    const auto token = WaveformPrefetchTestAccess::preparePrefetch(*provider);
    QVERIFY(token);
    provider->prefetchTracks({fixturePath_});
    provider.reset();
    ag_waveform* waveform = nullptr;
    const auto result = ag_waveform_analyze_with_aggregation(
        fixturePath_.toUtf8().constData(), 32,
        AG_WAVEFORM_AGGREGATION_AVERAGE_ABSOLUTE, token.get(), nullptr, nullptr,
        &waveform);
    if (waveform) ag_waveform_destroy(waveform);
    QCOMPARE(result, AG_CANCELLED);
}

void WaveformProviderTest::canceledRunningPrefetchDoesNotPublishCache()
{
    if (fixturePath_.isEmpty()) QSKIP("AGPLAYER_TEST_WAV not set");
    QTemporaryDir directory;
    const QString nextPath = directory.filePath(QStringLiteral("next.wav"));
    QVERIFY(QFile::copy(fixturePath_, nextPath));
    SettingsController settings;
    const QString cachePath = directory.filePath(QStringLiteral("cache"));
    settings.setCacheDirectory(cachePath);
    WaveformProvider provider(&settings);
    const auto token = WaveformPrefetchTestAccess::preparePrefetch(provider);
    QVERIFY(token);
    ag_cancel_token_set_paused(token.get(), 1);
    QSignalSpy cached(&provider, &WaveformProvider::waveformCacheReady);
    QSignalSpy ready(&provider, &WaveformProvider::waveformReady);
    agplayer::testing::reset_waveform_provider_counters();
    provider.prefetchTracks({fixturePath_});
    QTRY_COMPARE(agplayer::testing::waveform_provider_prefetch_jobs_started(), 1U);
    const bool publishedWhilePaused = !cached.isEmpty()
        || !QDir(cachePath).entryList({QStringLiteral("*.agwf")}, QDir::Files).isEmpty();
    provider.loadForTrack(QStringLiteral("next"), nextPath, true);
    finishProviderAnalysis(provider);
    QVERIFY(!publishedWhilePaused);
    QCOMPARE(ready.count(), 1);
    QCOMPARE(ready.first().at(0).toString(), nextPath);
    QCOMPARE(cached.count(), 1);
    QCOMPARE(cached.first().at(0).toString(), nextPath);
    QCOMPARE(QDir(cachePath).entryList({QStringLiteral("*.agwf")}, QDir::Files).size(), 1);
}

void WaveformProviderTest::canceledCompletedPrefetchDoesNotPublishQueuedNotification()
{
    if (fixturePath_.isEmpty()) QSKIP("AGPLAYER_TEST_WAV not set");
    QTemporaryDir directory;
    const QString nextPath = directory.filePath(QStringLiteral("next.wav"));
    QVERIFY(QFile::copy(fixturePath_, nextPath));
    SettingsController settings;
    const QString cachePath = directory.filePath(QStringLiteral("cache"));
    settings.setCacheDirectory(cachePath);
    WaveformProvider provider(&settings);
    QSignalSpy cached(&provider, &WaveformProvider::waveformCacheReady);
    provider.prefetchTracks({fixturePath_});
    // Finish real decoding and saving, but leave its queued notification pending.
    WaveformProviderTestAccess::waitForAnalysis(provider);
    QCOMPARE(cached.count(), 0);
    QCOMPARE(QDir(cachePath).entryList({QStringLiteral("*.agwf")}, QDir::Files).size(), 1);

    provider.loadForTrack(QStringLiteral("next"), nextPath, true);
    finishProviderAnalysis(provider);
    QCOMPARE(cached.count(), 1);
    QCOMPARE(cached.first().at(0).toString(), nextPath);
    // A valid cache written before cancellation remains reusable.
    QCOMPARE(QDir(cachePath).entryList({QStringLiteral("*.agwf")}, QDir::Files).size(), 2);
}

QTEST_GUILESS_MAIN(WaveformProviderTest)

#include "waveform_provider_test.moc"
