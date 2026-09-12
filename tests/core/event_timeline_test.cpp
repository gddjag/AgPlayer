#include "audio_editor/event_timeline.hpp"
#include "audio_editor/audio_document.hpp"

#include <QtTest>

#include <limits>
#include <memory>

using namespace agplayer::editor;

class EventTimelineTest final : public QObject {
    Q_OBJECT

private:
    static std::shared_ptr<const AudioSource> source(
        const SampleFrame frames = 10'000)
    {
        return std::make_shared<const AudioSource>(AudioSource{
            "fixture.wav", 48'000, 2, frames});
    }

    static AudioEvent event(const EventId id, const SampleFrame timeline_start,
                            const SampleFrame source_start,
                            const SampleFrame source_end)
    {
        return {id, source(), source_start, source_end, timeline_start};
    }

    static void compareRetainedState(const TimelineSnapshot& before,
                                     const TimelineSnapshot& after)
    {
        QCOMPARE(after.totalFrames, before.totalFrames);
        QCOMPARE(after.revision, before.revision);
        QCOMPARE(after.events.size(), before.events.size());
        for (std::size_t index = 0; index < before.events.size(); ++index) {
            QCOMPARE(after.events.at(index).id, before.events.at(index).id);
            QCOMPARE(after.events.at(index).sourceStart,
                     before.events.at(index).sourceStart);
            QCOMPARE(after.events.at(index).sourceEnd,
                     before.events.at(index).sourceEnd);
            QCOMPARE(after.events.at(index).timelineStart,
                     before.events.at(index).timelineStart);
        }
    }

private slots:
    void gapsContributeToDurationAndHitTestsAreHalfOpen()
    {
        EventTimeline timeline;
        const AudioEvent first = event(1, 100, 0, 100);
        const AudioEvent later = event(2, 500, 200, 400);

        QVERIFY(timeline.insert(later));
        QVERIFY(timeline.insert(first));

        QCOMPARE(timeline.totalFrames(), SampleFrame{700});
        QVERIFY(timeline.eventsAt(99).empty());
        QCOMPARE(timeline.eventsAt(100), std::vector<EventId>{1});
        QVERIFY(timeline.eventsAt(200).empty());
        QVERIFY(timeline.eventsAt(499).empty());
        QCOMPARE(timeline.eventsAt(500), std::vector<EventId>{2});
        QVERIFY(timeline.eventsAt(700).empty());

        const TimelineSnapshot snapshot = timeline.snapshot();
        QCOMPARE(snapshot.events.size(), std::size_t{2});
        QCOMPARE(snapshot.events.at(0).id, EventId{1});
        QCOMPARE(snapshot.events.at(1).id, EventId{2});
        QCOMPARE(snapshot.totalFrames, SampleFrame{700});
        QCOMPARE(snapshot.revision, std::uint64_t{2});
    }

    void rejectedOverlapLeavesStateAndRevisionUnchanged()
    {
        EventTimeline timeline;
        QVERIFY(timeline.insert(event(1, 100, 0, 100)));
        const TimelineSnapshot before = timeline.snapshot();

        QVERIFY(!timeline.insert(event(2, 150, 0, 100)));
        QVERIFY(!timeline.insert(event(1, 300, 0, 100)));

        QCOMPARE(timeline.revision(), before.revision);
        QCOMPARE(timeline.totalFrames(), before.totalFrames);
        QCOMPARE(timeline.snapshot().events.size(), before.events.size());
        QCOMPARE(timeline.event(1)->timelineStart, SampleFrame{100});
        QVERIFY(timeline.event(2) == nullptr);
    }

    void rejectedInvalidAndOverflowEventsLeaveStateUnchanged()
    {
        EventTimeline timeline;
        QVERIFY(timeline.insert(event(1, 100, 0, 100)));

        const TimelineSnapshot before_invalid = timeline.snapshot();
        AudioEvent invalid = event(2, 300, 0, 100);
        invalid.sourceEnd = invalid.sourceStart;
        QVERIFY(!timeline.insert(invalid));
        compareRetainedState(before_invalid, timeline.snapshot());

        const TimelineSnapshot before_overflow = timeline.snapshot();
        const auto large_source = std::make_shared<const AudioSource>(AudioSource{
            "large.wav", 48'000, 2, std::numeric_limits<SampleFrame>::max()});
        const AudioEvent overflow{3, large_source, 0, 100,
                                  std::numeric_limits<SampleFrame>::max() - 50};
        QVERIFY(!timeline.insert(overflow));
        compareRetainedState(before_overflow, timeline.snapshot());
    }

    void singleEventSnapshotRetainsExactSourceCoordinates()
    {
        EventTimeline timeline;
        QVERIFY(timeline.insert(event(1, 0, 100, 600)));

        const auto snapshot = timeline.snapshot();
        QCOMPARE(snapshot.events.size(), std::size_t{1});
        QCOMPARE(snapshot.events.front().sourceStart, SampleFrame{100});
        QCOMPARE(audibleFrames(snapshot.events.front()), SampleFrame{500});
    }
};

QTEST_APPLESS_MAIN(EventTimelineTest)

#include "event_timeline_test.moc"
