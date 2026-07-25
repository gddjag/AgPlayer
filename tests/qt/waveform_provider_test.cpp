#include "settings_controller.hpp"
#include "waveform_provider.hpp"

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
    SettingsController settings;

    // First pass: analyze and save cache.
    {
        WaveformProvider provider(&settings);
        QSignalSpy spy(&provider, &WaveformProvider::waveformReady);
        provider.loadForTrack(fixturePath_);
        QVERIFY2(spy.wait(5000), "analysis did not finish in time");
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
    SettingsController settings;
    WaveformProvider provider(&settings);
    QSignalSpy spy(&provider, &WaveformProvider::waveformReady);

    provider.loadForTrack(fixturePath_);

    QVERIFY2(spy.wait(5000), "analysis did not finish in time");
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

QTEST_MAIN(WaveformProviderTest)
#include "waveform_provider_test.moc"
