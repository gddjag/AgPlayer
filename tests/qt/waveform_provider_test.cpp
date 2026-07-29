#include "settings_controller.hpp"
#include "waveform_provider.hpp"

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
    void analysisEmitsPeaksForFixture();
    void aggregationChangeUsesSeparateCache();
    void newerTrackSuppressesStaleAnalysisResult();

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
    QCOMPARE(provider.analysisProgress(), 1.0);
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

QTEST_MAIN(WaveformProviderTest)
#include "waveform_provider_test.moc"
