#include "editor_timeline_math.hpp"

#include <QTest>

class EditorTimelineMathTest final : public QObject {
    Q_OBJECT

private slots:
    void computesBeatGrid();
    void snapsToNearestGridLine();
    void clampsAndNormalizesClipBounds();
};

void EditorTimelineMathTest::computesBeatGrid()
{
    QCOMPARE(beatGridMs(120.0, 4), 500);
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
