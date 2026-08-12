#include "audio_editor/editor_viewport.hpp"

#include <QtTest>

class EditorViewportTest final : public QObject {
    Q_OBJECT

private slots:
    void overviewAndMainViewShareOneRange()
    {
        EditorViewport viewport;
        viewport.setDocumentFrames(480'000);
        viewport.setViewportWidth(1'200.0);
        QVERIFY(viewport.setVisibleRange(120'000, 240'000));
        QCOMPARE(viewport.overviewStartRatio(), 0.25);
        QCOMPARE(viewport.overviewWidthRatio(), 0.25);
        viewport.moveOverviewWindow(0.5);
        QCOMPARE(viewport.visibleStartFrame(), qint64{240'000});
        QCOMPARE(viewport.visibleEndFrame(), qint64{360'000});
    }

    void zoomKeepsAnchorFrameStable()
    {
        EditorViewport viewport;
        viewport.setDocumentFrames(480'000);
        viewport.setViewportWidth(1'200.0);
        QVERIFY(viewport.setVisibleRange(120'000, 360'000));
        const auto anchor = viewport.frameAtPixel(600.0);
        viewport.zoomAt(2.0, 600.0);
        QCOMPARE(viewport.frameAtPixel(600.0), anchor);
        QCOMPARE(viewport.visibleFrameCount(), qint64{120'000});
    }

    void emptyDocumentNeverProducesInvalidRatios()
    {
        EditorViewport viewport;
        QCOMPARE(viewport.overviewStartRatio(), 0.0);
        QCOMPARE(viewport.overviewWidthRatio(), 0.0);
        QCOMPARE(viewport.frameAtPixel(50.0), qint64{0});
    }
};

QTEST_APPLESS_MAIN(EditorViewportTest)

#include "editor_viewport_test.moc"
