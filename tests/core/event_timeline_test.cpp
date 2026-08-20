#include "audio_editor/event_timeline.hpp"
#include "audio_editor/audio_document.hpp"

#include <QtTest>

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

    void singleEventSnapshotAdaptsToTheLegacyRenderInput()
    {
        EventTimeline timeline;
        QVERIFY(timeline.insert(event(1, 0, 100, 600)));

        const auto legacy = singleEventDocumentSnapshot(timeline.snapshot());

        QVERIFY(legacy.has_value());
        QCOMPARE(legacy->spans.size(), std::size_t{1});
        QCOMPARE(legacy->spans.front().source_start, SampleFrame{100});
        QCOMPARE(legacy->spans.front().frame_count, SampleFrame{500});
    }
};

QTEST_APPLESS_MAIN(EventTimelineTest)

#include "event_timeline_test.moc"
