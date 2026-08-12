#include "audio_editor/time_pixel_mapper.hpp"

#include <QtTest>

#include <cstdlib>

using namespace agplayer::editor;

class TimePixelMapperTest final : public QObject {
    Q_OBJECT

private slots:
    void roundTripsAtFourKAndExtremeZoom()
    {
        TimePixelMapper mapper{SampleFrame{172'800'000}, 3'840.0, 64.0};
        for (const SampleFrame frame : {
                 0LL, 1LL, 47'999LL, 86'399'999LL, 172'799'999LL}) {
            const double pixel = mapper.frameToPixel(frame);
            QVERIFY(std::llabs(mapper.pixelToFrame(pixel) - frame) <= 1);
        }
    }

    void visibleRangeClampsAtDocumentEnd()
    {
        TimePixelMapper mapper{1'000, 100.0, 2.0};
        QCOMPARE(mapper.visibleFrameRange().start, SampleFrame{0});
        QCOMPARE(mapper.visibleFrameRange().end, SampleFrame{200});
        mapper.setVisibleStart(950);
        QCOMPARE(mapper.visibleFrameRange().start, SampleFrame{800});
        QCOMPARE(mapper.visibleFrameRange().end, SampleFrame{1'000});
        QCOMPARE(mapper.frameToPixel(800), 0.0);
        QCOMPARE(mapper.pixelToFrame(100.0), SampleFrame{1'000});
    }
};

QTEST_APPLESS_MAIN(TimePixelMapperTest)

#include "time_pixel_mapper_test.moc"
