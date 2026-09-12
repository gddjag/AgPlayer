#include "editor_timeline_math.hpp"

#include <QTest>
#include <limits>

class EditorTimelineMathTest final : public QObject {
    Q_OBJECT

private slots:
    void computesBeatGrid();
    void snapsToNearestGridLine();
    void estimatesFirstBeatOffset();
    void alignsRecurringBeatsDespiteAnIsolatedIntro();
    void refinesRoundedTempoWithoutLongTermDrift();
    void rejectsUnreliableBeatPhase();
    void clampsAndNormalizesClipBounds();
};

void EditorTimelineMathTest::computesBeatGrid()
{
    QCOMPARE(beatGridMs(120.0, 4), 500);
    QCOMPARE(beatGridMs(300.0, 4), 200);
    QCOMPARE(beatGridMs(128.0, 4), 469);
    QCOMPARE(beatGridMs(20.0, 4), 0);
    QCOMPARE(beatGridMs(120.0, 0), 0);
}

void EditorTimelineMathTest::snapsToNearestGridLine()
{
    QCOMPARE(snapMs(740, 120.0, 4), 500);
    QCOMPARE(snapMs(760, 120.0, 4), 1000);
    QCOMPARE(snapMs(-80, 120.0, 4), 0);
}

void EditorTimelineMathTest::estimatesFirstBeatOffset()
{
    const QVariantList delayedBeat{
        0.01, 0.02, 0.04, 0.82, 0.20, 0.70, 0.18, 0.65, 0.15
    };
    QCOMPARE(estimateFirstBeatOffsetMs(delayedBeat, 8'000, 120.0), 3'000);
    QCOMPARE(estimateFirstBeatOffsetMs({0.01, 0.01, 0.01}, 2'000, 120.0), 0);
    QCOMPARE(estimateFirstBeatOffsetMs({}, 2'000, 120.0), 0);
}

void EditorTimelineMathTest::alignsRecurringBeatsDespiteAnIsolatedIntro()
{
    QVariantList peaks;
    peaks.fill(0.0, 10000);
    peaks[50] = 1.0; // Intro accent at 100 ms is not on the recurring beat.
    for (int beat = 0; beat < 38; ++beat)
        peaks[125 + beat * 250] = 0.6;
    const auto result = estimateBeatGrid(peaks, 20000, 120.0);
    QVERIFY(result.reliable);
    QVERIFY(qAbs(result.offsetMs - 250) <= 3);
    QVERIFY(qAbs(result.bpm - 120.0) < 0.02);
}

void EditorTimelineMathTest::refinesRoundedTempoWithoutLongTermDrift()
{
    QVariantList peaks;
    peaks.fill(0.0, 30000);
    for (int beat = 0; beat < 125; ++beat)
        peaks[qRound((250.0 + beat * 60000.0 / 128.25) / 2.0)] = 0.7;
    const auto result = estimateBeatGrid(peaks, 60000, 128.0);
    QVERIFY(result.reliable);
    QVERIFY(qAbs(result.offsetMs - 250) <= 3);
    QVERIFY(qAbs(result.bpm - 128.25) < 0.01);
    // Independent expected timestamp for beat 124, rounded to milliseconds.
    QVERIFY(qAbs(result.offsetMs + 124 * 60000.0 / result.bpm - 58262) < 4);
}

void EditorTimelineMathTest::rejectsUnreliableBeatPhase()
{
    QVariantList peaks;
    peaks.fill(0.0, 10000);
    QVERIFY(!estimateBeatGrid(peaks, 20000, 120).reliable);
    peaks[125] = 0.8;
    QVERIFY(!estimateBeatGrid(peaks, 20000, 120).reliable);
    peaks.fill(0.5, 10000);
    QVERIFY(!estimateBeatGrid(peaks, 20000, 120).reliable);
    QVERIFY(!estimateBeatGrid(peaks, 20000,
        std::numeric_limits<double>::quiet_NaN()).reliable);
    QVERIFY(!estimateBeatGrid({0.0, 0.8, 0.0}, 20000, 120).reliable);
    peaks[10] = std::numeric_limits<double>::infinity();
    QVERIFY(!estimateBeatGrid(peaks, 20000, 120).reliable);
}

void EditorTimelineMathTest::clampsAndNormalizesClipBounds()
{
    const ClipBounds normalized = normalizeClip(ClipBounds{1000, 900, 950}, 1000);
    QCOMPARE(normalized.inMs, 800);
    QCOMPARE(normalized.outMs, 1000);
    QCOMPARE(normalized.timelineStartMs, 1000);

    const ClipBounds shortSource = normalizeClip(ClipBounds{-50, 80, 20}, 120);
    QCOMPARE(shortSource.inMs, 0);
    QCOMPARE(shortSource.outMs, 120);
    QCOMPARE(shortSource.timelineStartMs, 0);
}

QTEST_MAIN(EditorTimelineMathTest)
#include "editor_timeline_math_test.moc"
