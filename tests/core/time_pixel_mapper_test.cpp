#include "audio_editor/event_timeline.hpp"
#include "audio_editor/time_pixel_mapper.hpp"

#include <QtTest>

#include <cstdlib>
#include <memory>

using namespace agplayer::editor;

class TimePixelMapperTest final : public QObject {
    Q_OBJECT

private slots:
    void mapsTimelineSnapshotAtCommonSampleRatesWithinOneFrame()
    {
        for (const std::uint32_t sample_rate : {44'100U, 48'000U, 96'000U}) {
            EventTimeline timeline;
            const SampleFrame two_hours = static_cast<SampleFrame>(sample_rate)
                * 60 * 60 * 2;
            QVERIFY(timeline.insert(AudioEvent{
                1, std::make_shared<const AudioSource>(AudioSource{
                       "fixture.wav", sample_rate, 2, two_hours}),
                0, two_hours, 0}));

            TimePixelMapper mapper{timeline.snapshot(), 3'841.5, 73.25};
            mapper.setVisibleStart(two_hours / 3 + 17);
            const Selection middle = mapper.visibleFrameRange();
            QVERIFY(middle.start > 0);
            for (const SampleFrame frame : {middle.start, middle.start + 1,
                                             middle.end - 1, middle.end}) {
                const double pixel = mapper.frameToPixel(frame);
                QVERIFY(std::llabs(mapper.pixelToFrame(pixel) - frame) <= 1);
            }

            mapper.setVisibleStart(two_hours);
            const Selection final = mapper.visibleFrameRange();
            QVERIFY(final.start > 0);
            QCOMPARE(final.end, two_hours);
            for (const SampleFrame frame : {two_hours - 1, two_hours}) {
                const double pixel = mapper.frameToPixel(frame);
                QVERIFY(std::llabs(mapper.pixelToFrame(pixel) - frame) <= 1);
            }
        }
    }

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
