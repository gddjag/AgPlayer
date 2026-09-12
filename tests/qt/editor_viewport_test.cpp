#include "audio_editor/editor_viewport.hpp"

#include <QtTest>

#include <cmath>

class EditorViewportTest final : public QObject {
    Q_OBJECT

private slots:
    void viewportWidthSetterIsInvokableFromQml()
    {
        EditorViewport viewport;
        QVERIFY(QMetaObject::invokeMethod(&viewport, "setViewportWidth",
                                          Q_ARG(qreal, 640.0)));
        QCOMPARE(viewport.viewportWidth(), 640.0);
    }

    void longFileMappingStaysAlignedAtVisibleBoundaries()
    {
        EditorViewport viewport;
        constexpr qint64 twoHoursAt96Khz = 2LL * 60LL * 60LL * 96'000LL;
        constexpr qint64 visibleStart = 400'000'123LL;
        constexpr qint64 visibleEnd = 400'960'123LL;
        viewport.setDocumentFrames(twoHoursAt96Khz);
        viewport.setViewportWidth(1'167.0);
        QVERIFY(viewport.setVisibleRange(visibleStart, visibleEnd));

        QCOMPARE(viewport.frameAtPixel(0.0), visibleStart);
        QCOMPARE(viewport.frameAtPixel(1'167.0), visibleEnd);
        QCOMPARE(viewport.pixelAtFrame(visibleStart), 0.0);
        QCOMPARE(viewport.pixelAtFrame(visibleEnd), 1'167.0);
        for (const qreal pixel : {0.0, 91.25, 583.5, 1'166.0, 1'167.0}) {
            const qint64 frame = viewport.frameAtPixel(pixel);
            QVERIFY(std::abs(viewport.frameAtPixel(viewport.pixelAtFrame(frame))
                             - frame) <= 1);
        }
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

    void panByPixelsUsesTheSameMappingAndClampsAtBothEnds()
    {
        EditorViewport viewport;
        viewport.setDocumentFrames(480'000);
        viewport.setViewportWidth(1'200.0);
        QVERIFY(viewport.setVisibleRange(120'000, 240'000));

        viewport.panByPixels(300.0);
        QCOMPARE(viewport.visibleStartFrame(), qint64{150'000});
        QCOMPARE(viewport.visibleEndFrame(), qint64{270'000});
        QCOMPARE(viewport.frameAtPixel(0.0), qint64{150'000});

        viewport.panByPixels(-10'000.0);
        QCOMPARE(viewport.visibleStartFrame(), qint64{0});
        QCOMPARE(viewport.visibleEndFrame(), qint64{120'000});

        viewport.panByPixels(10'000.0);
        QCOMPARE(viewport.visibleStartFrame(), qint64{360'000});
        QCOMPARE(viewport.visibleEndFrame(), qint64{480'000});
    }

    void timelineScrollbarMappingStaysStableAcrossTheWholeDocument()
    {
        EditorViewport viewport;
        viewport.setDocumentFrames(192'000);
        viewport.setViewportWidth(1'000.0);
        QVERIFY(viewport.setVisibleRange(0, 48'000));

        QCOMPARE(viewport.timelineContentWidth(), 4'000.0);
        QCOMPARE(viewport.scrollOffsetPixels(), 0.0);

        viewport.panToScrollOffset(3'000.0);
        QCOMPARE(viewport.visibleStartFrame(), qint64{144'000});
        QCOMPARE(viewport.visibleEndFrame(), qint64{192'000});
        QCOMPARE(viewport.scrollOffsetPixels(), 3'000.0);

        viewport.panToScrollOffset(-500.0);
        QCOMPARE(viewport.visibleStartFrame(), qint64{0});
        QCOMPARE(viewport.scrollOffsetPixels(), 0.0);
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
