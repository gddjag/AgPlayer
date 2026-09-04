#include "settings_controller.hpp"
#include "waveform_provider.hpp"
#include "waveform_provider_test_access.hpp"
#include "../../core/src/waveform_cache.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <cstdint>

namespace agplayer::testing {

void reset_waveform_provider_counters() noexcept;
std::uint64_t waveform_provider_jobs_started() noexcept;

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
    QCOMPARE(pointCount, 32768);
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

QTEST_GUILESS_MAIN(WaveformProviderTest)

#include "waveform_provider_test.moc"
