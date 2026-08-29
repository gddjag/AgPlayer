#include "settings_controller.hpp"
#include "waveform_provider.hpp"
#include "waveform_provider_test_access.hpp"
#include "bpm_fixture.hpp"

#include <QFile>
#include <QDir>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <agplayer/c_api.h>

#include <algorithm>
#include <cmath>
#include <vector>

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
    void failedAnalysisEmitsATerminalSignalWithIdentity();
    void cancellingAnalysisReleasesNativeResourcesWithoutTerminal();
    void replacingAnalysisReleasesSupersededResourcesWithoutStaleTerminal();
    void destroyingProviderReleasesInFlightNativeResources();

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
        if (spy.isEmpty()) {
            QVERIFY2(spy.wait(5000),
                     qPrintable(QStringLiteral("analysis did not finish; progress=%1")
                                    .arg(provider.analysisProgress())));
        }
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

    if (spy.isEmpty()) {
        QVERIFY2(spy.wait(5000),
                 qPrintable(QStringLiteral("analysis did not finish; progress=%1")
                                .arg(provider.analysisProgress())));
    }
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
    if (spy.isEmpty()) {
        QVERIFY2(spy.wait(10'000), "click-track waveform analysis did not finish");
    }

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
    if (spy.isEmpty()) {
        QVERIFY(spy.wait(5000));
    }
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

    if (spy.isEmpty()) {
        QVERIFY2(spy.wait(5000), "Unicode-path waveform analysis did not finish");
    }
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
        if (spy.isEmpty()) {
            QVERIFY2(spy.wait(5000), "waveform analysis did not finish");
        }
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

    if (spy.isEmpty()) {
        QVERIFY2(spy.wait(5000), "current waveform analysis did not finish");
    }
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), secondPath);
    QTest::qWait(100);
    QCOMPARE(spy.count(), 1);
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
    if (spy.isEmpty()) {
        QVERIFY2(spy.wait(5000), "waveform analysis did not finish");
    }

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

    QTRY_COMPARE_WITH_TIMEOUT(
        QDir(cacheDir).entryList({QStringLiteral("*.agwf")}, QDir::Files).size(),
        1,
        5000);
    QCOMPARE(waveformSpy.count(), 0);

    provider.loadForTrack(prefetchedPath);
    QCOMPARE(waveformSpy.count(), 1);
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
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 5'000);
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

    QTRY_VERIFY_WITH_TIMEOUT(resources.expired(), 2'000);
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
    WaveformProviderTestAccess::waitForAnalysis(provider);

    QTRY_VERIFY_WITH_TIMEOUT(firstResources.expired(), 2'000);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 2'000);
    QCOMPARE(ready.first().at(0).toString(), second);
    QCOMPARE(failed.count(), 0);
    QTRY_VERIFY_WITH_TIMEOUT(secondResources.expired(), 2'000);
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

QTEST_MAIN(WaveformProviderTest)
#include "waveform_provider_test.moc"
