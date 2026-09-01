#include "settings_controller.hpp"
#include "waveform_provider.hpp"
#include "waveform_provider_test_access.hpp"
#include "../../core/src/waveform_cache.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <cstdint>
#include <filesystem>

namespace agplayer::testing {

void reset_waveform_provider_counters() noexcept;
std::uint64_t waveform_provider_frequency_jobs_started() noexcept;
std::uint64_t waveform_provider_mix_jobs_started() noexcept;

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

std::filesystem::path filesystemPath(const QString& path)
{
#ifdef Q_OS_WIN
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::u8path(path.toUtf8().constData());
#endif
}

QString cachePath(const QString& directory, const QString& sourcePath,
                  const bool legacyV2)
{
    const std::filesystem::path source = filesystemPath(sourcePath);
    const std::string key = legacyV2
        ? agplayer::WaveformCache::legacy_v2_key_for(source)
        : agplayer::WaveformCache::key_for(source);
    return QDir(directory).filePath(
        QString::fromStdString(key) + QStringLiteral("-average.agwf"));
}

} // namespace

class WaveformProviderTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void emptyPathEmitsEmptyWaveform();
    void plainAnalysisPreservesAmplitudeAndRgbBands();
    void plainCacheHitDoesNotStartAnalysis();
    void plainModeNeverStartsSpectralAnalysis();
    void spectralRequestUpgradesTheExistingAgwfCache();
    void spectralCacheHitPublishesWithoutDecode();
    void switchingBackToPlainDoesNotPublishStaleSpectralData();
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
    QVERIFY(pointCount > 0);
    QCOMPARE(layers.value(QStringLiteral("bass")).toList().size(), pointCount);
    QCOMPARE(layers.value(QStringLiteral("mid")).toList().size(), pointCount);
    QCOMPARE(layers.value(QStringLiteral("high")).toList().size(), pointCount);
    QVERIFY(layers.value(QStringLiteral("spectralIndex")).toList().isEmpty());
    QVERIFY(!layers.value(QStringLiteral("_frequencyReady")).toBool());
    QCOMPARE(layers.value(QStringLiteral("_cacheVersion")).toInt(), 3);
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
    QCOMPARE(agplayer::testing::waveform_provider_mix_jobs_started(), 0U);
    QCOMPARE(agplayer::testing::waveform_provider_frequency_jobs_started(), 0U);
}

void WaveformProviderTest::plainModeNeverStartsSpectralAnalysis()
{
    if (fixturePath_.isEmpty()) QSKIP("AGPLAYER_TEST_WAV not set");
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    SettingsController settings;
    settings.setCacheDirectory(directory.filePath(QStringLiteral("cache")));
    WaveformProvider provider(&settings);
    agplayer::testing::reset_waveform_provider_counters();
    provider.loadForTrack(QStringLiteral("plain"), fixturePath_, false);
    finishProviderAnalysis(provider);
    QCOMPARE(agplayer::testing::waveform_provider_frequency_jobs_started(), 0U);
    QCOMPARE(agplayer::testing::waveform_provider_mix_jobs_started(), 1U);
}

void WaveformProviderTest::spectralRequestUpgradesTheExistingAgwfCache()
{
    if (fixturePath_.isEmpty()) QSKIP("AGPLAYER_TEST_WAV not set");
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    SettingsController settings;
    const QString cacheDirectory = directory.filePath(QStringLiteral("cache"));
    settings.setCacheDirectory(cacheDirectory);
    {
        WaveformProvider warm(&settings);
        warm.loadForTrack(QStringLiteral("warm"), fixturePath_, false);
        finishProviderAnalysis(warm);
    }
    const QString currentCache = cachePath(cacheDirectory, fixturePath_, false);
    const QString legacyCache = cachePath(cacheDirectory, fixturePath_, true);
    agplayer::WaveformCacheData legacyData;
    QVERIFY(agplayer::WaveformCache::load_v3(
        filesystemPath(currentCache), filesystemPath(fixturePath_), legacyData));
    legacyData.spectral_index.clear();
    QVERIFY(agplayer::WaveformCache::save_v2(
        filesystemPath(legacyCache), filesystemPath(fixturePath_), legacyData));
    QVERIFY(QFile::remove(currentCache));

    agplayer::testing::reset_waveform_provider_counters();
    WaveformProvider provider(&settings);
    QSignalSpy ready(&provider, &WaveformProvider::waveformReady);
    provider.loadForTrack(QStringLiteral("spectral"), fixturePath_, true);
    QCOMPARE(ready.count(), 1);
    QVERIFY(!lastLayers(ready).value(QStringLiteral("_frequencyReady")).toBool());
    finishProviderAnalysis(provider);

    QCOMPARE(ready.count(), 2);
    const QVariantMap complete = lastLayers(ready);
    const int pointCount = complete.value(QStringLiteral("mix")).toList().size();
    QCOMPARE(complete.value(QStringLiteral("spectralIndex")).toList().size(),
             pointCount);
    QVERIFY(complete.value(QStringLiteral("_frequencyReady")).toBool());
    QCOMPARE(complete.value(QStringLiteral("_frequencyCacheVersion")).toInt(), 3);
    QCOMPARE(agplayer::testing::waveform_provider_frequency_jobs_started(), 1U);
    QCOMPARE(QDir(cacheDirectory).entryList(
                 {QStringLiteral("*.agwf")}, QDir::Files).size(), 1);
    QCOMPARE(QDir(cacheDirectory).entryList(
                 {QStringLiteral("*.fcw1")}, QDir::Files).size(), 0);
}

void WaveformProviderTest::spectralCacheHitPublishesWithoutDecode()
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
    QCOMPARE(layers.value(QStringLiteral("spectralIndex")).toList().size(),
             layers.value(QStringLiteral("mix")).toList().size());
    QVERIFY(!layers.value(QStringLiteral("bass")).toList().isEmpty());
    QVERIFY(!layers.value(QStringLiteral("mid")).toList().isEmpty());
    QVERIFY(!layers.value(QStringLiteral("high")).toList().isEmpty());
    QCOMPARE(agplayer::testing::waveform_provider_frequency_jobs_started(), 0U);
    QCOMPARE(agplayer::testing::waveform_provider_mix_jobs_started(), 0U);
}

void WaveformProviderTest::switchingBackToPlainDoesNotPublishStaleSpectralData()
{
    if (fixturePath_.isEmpty()) QSKIP("AGPLAYER_TEST_WAV not set");
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    SettingsController settings;
    settings.setCacheDirectory(directory.filePath(QStringLiteral("cache")));
    WaveformProvider provider(&settings);
    QSignalSpy ready(&provider, &WaveformProvider::waveformReady);

    provider.loadForTrack(QStringLiteral("same"), fixturePath_, true);
    provider.loadForTrack(QStringLiteral("same"), fixturePath_, false);
    finishProviderAnalysis(provider);

    QVERIFY(!ready.isEmpty());
    for (const QList<QVariant>& emission : ready) {
        QVERIFY(!emission.at(1).toMap()
                     .value(QStringLiteral("_frequencyReady")).toBool());
    }
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
