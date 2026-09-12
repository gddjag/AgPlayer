#include "waveform_coordinate_mapper.hpp"

#include <QTest>

#include <cmath>

class WaveformCoordinateMapperTest final : public QObject {
    Q_OBJECT

private slots:
    void timeToPixelMatchesTimeline_data();
    void timeToPixelMatchesTimeline();
    void pixelToTimeRoundTripsWithinHalfPixel_data();
    void pixelToTimeRoundTripsWithinHalfPixel();
    void clampsInvalidAndOutOfRangeInputs();
    void mapsTimeThroughSamplesAndPeaks();
    void preservesPhysicalPixelPrecisionAcrossDpiScales();
};

void WaveformCoordinateMapperTest::timeToPixelMatchesTimeline_data()
{
    QTest::addColumn<qint64>("positionMs");
    QTest::addColumn<double>("width");
    QTest::addColumn<double>("expectedPixel");

    for (const double width : {500.0, 1000.0, 3840.0}) {
        QTest::newRow(qPrintable(QStringLiteral("zero-%1").arg(width)))
            << qint64{0} << width << 0.0;
        QTest::newRow(qPrintable(QStringLiteral("minute-%1").arg(width)))
            << qint64{60000} << width << width * 0.2;
        QTest::newRow(qPrintable(QStringLiteral("half-%1").arg(width)))
            << qint64{150000} << width << width * 0.5;
        QTest::newRow(qPrintable(QStringLiteral("last-ms-%1").arg(width)))
            << qint64{299999} << width
            << width * (299999.0 / 300000.0);
    }
}

void WaveformCoordinateMapperTest::timeToPixelMatchesTimeline()
{
    QFETCH(qint64, positionMs);
    QFETCH(double, width);
    QFETCH(double, expectedPixel);

    const double actual = WaveformCoordinateMapper::timeToPixel(
        positionMs, 300000, width);
    QVERIFY2(std::abs(actual - expectedPixel) <= 0.5,
             qPrintable(QStringLiteral("actual=%1 expected=%2")
                            .arg(actual, 0, 'f', 9)
                            .arg(expectedPixel, 0, 'f', 9)));
}

void WaveformCoordinateMapperTest::pixelToTimeRoundTripsWithinHalfPixel_data()
{
    timeToPixelMatchesTimeline_data();
}

void WaveformCoordinateMapperTest::pixelToTimeRoundTripsWithinHalfPixel()
{
    QFETCH(qint64, positionMs);
    QFETCH(double, width);
    QFETCH(double, expectedPixel);

    const qint64 mappedTime = WaveformCoordinateMapper::pixelToTime(
        expectedPixel, width, 300000);
    const qint64 halfPixelTimeTolerance = static_cast<qint64>(
        std::ceil(0.5 * 300000.0 / width));
    QVERIFY(std::abs(mappedTime - positionMs) <= halfPixelTimeTolerance);
    const double roundTripPixel = WaveformCoordinateMapper::timeToPixel(
        mappedTime, 300000, width);
    QVERIFY(std::abs(roundTripPixel - expectedPixel) <= 0.5);
}

void WaveformCoordinateMapperTest::clampsInvalidAndOutOfRangeInputs()
{
    QCOMPARE(WaveformCoordinateMapper::timeToPixel(-1, 300000, 1000.0), 0.0);
    QCOMPARE(WaveformCoordinateMapper::timeToPixel(400000, 300000, 1000.0), 1000.0);
    QCOMPARE(WaveformCoordinateMapper::timeToPixel(1, 0, 1000.0), 0.0);
    QCOMPARE(WaveformCoordinateMapper::timeToPixel(1, 10, -1.0), 0.0);
    QCOMPARE(WaveformCoordinateMapper::pixelToTime(-1.0, 1000.0, 300000), qint64{0});
    QCOMPARE(WaveformCoordinateMapper::pixelToTime(1001.0, 1000.0, 300000), qint64{300000});
    QCOMPARE(WaveformCoordinateMapper::pixelToTime(1.0, 0.0, 300000), qint64{0});
}

void WaveformCoordinateMapperTest::mapsTimeThroughSamplesAndPeaks()
{
    QCOMPARE(WaveformCoordinateMapper::timeToSample(150000, 300000, 14400000),
             qint64{7200000});
    QCOMPARE(WaveformCoordinateMapper::sampleToPeak(7200000, 14400000, 3001),
             1500.0);
    QCOMPARE(WaveformCoordinateMapper::timeToPeak(
                 150000, 300000, 14400000, 3001),
             1500.0);
    QCOMPARE(WaveformCoordinateMapper::timeToPixelFromSamples(
                 150000, 300000, 14400000, 3001, 3840.0),
             1920.0);
}

void WaveformCoordinateMapperTest::preservesPhysicalPixelPrecisionAcrossDpiScales()
{
    for (const double devicePixelRatio : {1.0, 1.25, 1.5}) {
        for (const double logicalWidth : {500.0, 1000.0, 3840.0}) {
            const double logicalPixel = WaveformCoordinateMapper::timeToPixel(
                299999, 300000, logicalWidth);
            const double actualPhysicalPixel = logicalPixel * devicePixelRatio;
            const double expectedPhysicalPixel = logicalWidth * devicePixelRatio
                * (299999.0 / 300000.0);
            QVERIFY(std::abs(actualPhysicalPixel - expectedPhysicalPixel) <= 0.5);
        }
    }
}

QTEST_GUILESS_MAIN(WaveformCoordinateMapperTest)
#include "waveform_coordinate_mapper_test.moc"
