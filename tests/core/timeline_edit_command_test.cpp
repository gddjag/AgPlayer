#include "audio_editor/timeline_edit_command.hpp"

#include <QtTest>

#include <limits>
#include <memory>

using namespace agplayer::editor;

class TimelineEditCommandTest final : public QObject {
    Q_OBJECT

private:
    static std::shared_ptr<const AudioSource> source()
    {
        return std::make_shared<const AudioSource>(AudioSource{
            "fixture.wav", 48'000, 2, 1'000});
    }

    static AudioEvent event(const EventId id, const SampleFrame timeline_start,
                            const SampleFrame source_start,
                            const SampleFrame source_end)
    {
        return {id, source(), source_start, source_end, timeline_start};
    }

    static void compareSnapshot(const TimelineSnapshot& actual,
                                const TimelineSnapshot& expected)
    {
        QCOMPARE(actual.totalFrames, expected.totalFrames);
        QCOMPARE(actual.revision, expected.revision);
        QCOMPARE(actual.events.size(), expected.events.size());
        for (std::size_t index = 0; index < expected.events.size(); ++index) {
            const AudioEvent& left = actual.events.at(index);
            const AudioEvent& right = expected.events.at(index);
            QCOMPARE(left.id, right.id);
            QCOMPARE(left.source.get(), right.source.get());
            QCOMPARE(left.sourceStart, right.sourceStart);
            QCOMPARE(left.sourceEnd, right.sourceEnd);
            QCOMPARE(left.timelineStart, right.timelineStart);
            QCOMPARE(left.gain, right.gain);
            QCOMPARE(left.fadeIn, right.fadeIn);
            QCOMPARE(left.fadeOut, right.fadeOut);
            QCOMPARE(left.speedRatio, right.speedRatio);
            QCOMPARE(left.pitchSemitone, right.pitchSemitone);
            QCOMPARE(left.mute, right.mute);
            QCOMPARE(left.envelope.size(), right.envelope.size());
            for (std::size_t point = 0; point < right.envelope.size(); ++point) {
                QCOMPARE(left.envelope.at(point).offset,
                         right.envelope.at(point).offset);
                QCOMPARE(left.envelope.at(point).gain,
                         right.envelope.at(point).gain);
            }
        }
    }

private slots:
    void moveChangesOnlyTimelineStartAndIsReversible()
    {
        EventTimeline timeline;
        AudioEvent initial = event(1, 100, 50, 250);
        initial.gain = 0.75F;
        initial.fadeIn = 10;
        initial.fadeOut = 20;
        initial.speedRatio = 1.25;
        initial.pitchSemitone = -2;
        initial.mute = true;
        initial.envelope = {{10, 0.5F}, {100, 0.75F}};
        QVERIFY(timeline.insert(initial));
        const AudioEvent before = *timeline.event(1);
        const std::uint64_t initial_revision = timeline.revision();

        const auto command = TimelineEditCommand::move(timeline, 1, 500);
        QVERIFY(command.has_value());
        QVERIFY(command->execute(timeline));

        const AudioEvent after = *timeline.event(1);
        QCOMPARE(after.timelineStart, SampleFrame{500});
        QCOMPARE(after.source.get(), before.source.get());
        QCOMPARE(after.sourceStart, before.sourceStart);
        QCOMPARE(after.sourceEnd, before.sourceEnd);
        QCOMPARE(after.gain, before.gain);
        QCOMPARE(after.fadeIn, before.fadeIn);
        QCOMPARE(after.fadeOut, before.fadeOut);
        QCOMPARE(after.speedRatio, before.speedRatio);
        QCOMPARE(after.pitchSemitone, before.pitchSemitone);
        QCOMPARE(after.mute, before.mute);
        QCOMPARE(after.envelope.size(), before.envelope.size());
        QCOMPARE(after.envelope.at(0).offset, before.envelope.at(0).offset);
        QCOMPARE(after.envelope.at(0).gain, before.envelope.at(0).gain);
        QCOMPARE(timeline.revision(), initial_revision + 1U);

        QVERIFY(command->undo(timeline));
        QCOMPARE(timeline.event(1)->timelineStart, before.timelineStart);
        QCOMPARE(timeline.revision(), initial_revision + 2U);
        QVERIFY(command->execute(timeline));
        QCOMPARE(timeline.event(1)->timelineStart, SampleFrame{500});
        QCOMPARE(timeline.revision(), initial_revision + 3U);
    }

    void trimChangesOnlySourceBoundsAndExplicitAnchor()
    {
        EventTimeline timeline;
        QVERIFY(timeline.insert(event(1, 100, 50, 350)));
        const AudioEvent before = *timeline.event(1);

        const auto command = TimelineEditCommand::trim(
            timeline, 1, 100, 300, 150);
        QVERIFY(command.has_value());
        QVERIFY(command->execute(timeline));

        const AudioEvent after = *timeline.event(1);
        QCOMPARE(after.source.get(), before.source.get());
        QCOMPARE(after.sourceStart, SampleFrame{100});
        QCOMPARE(after.sourceEnd, SampleFrame{300});
        QCOMPARE(after.timelineStart, SampleFrame{150});
        QCOMPARE(after.gain, before.gain);
        QCOMPARE(after.fadeIn, before.fadeIn);
        QCOMPARE(after.fadeOut, before.fadeOut);
        QCOMPARE(after.speedRatio, before.speedRatio);
        QCOMPARE(after.pitchSemitone, before.pitchSemitone);
        QCOMPARE(after.mute, before.mute);
        QCOMPARE(after.envelope.size(), before.envelope.size());

        QVERIFY(command->undo(timeline));
        QCOMPARE(timeline.event(1)->sourceStart, before.sourceStart);
        QCOMPARE(timeline.event(1)->sourceEnd, before.sourceEnd);
        QCOMPARE(timeline.event(1)->timelineStart, before.timelineStart);
    }

    void rejectedMoveAndTrimRetainStateAndRevision()
    {
        EventTimeline timeline;
        QVERIFY(timeline.insert(event(1, 0, 0, 200)));
        QVERIFY(timeline.insert(event(2, 400, 200, 400)));
        const TimelineSnapshot before = timeline.snapshot();

        QVERIFY(!timeline.moveEvent(1, 300));
        compareSnapshot(timeline.snapshot(), before);

        QVERIFY(!timeline.trimEvent(1, 0, 300, 200));
        compareSnapshot(timeline.snapshot(), before);
    }

    void moveResortsEventsAndUpdatesTotalFrames()
    {
        EventTimeline timeline;
        QVERIFY(timeline.insert(event(1, 0, 0, 100)));
        QVERIFY(timeline.insert(event(2, 200, 100, 200)));

        QVERIFY(timeline.moveEvent(1, 400));
        const TimelineSnapshot snapshot = timeline.snapshot();
        QCOMPARE(snapshot.events.at(0).id, EventId{2});
        QCOMPARE(snapshot.events.at(1).id, EventId{1});
        QCOMPARE(snapshot.totalFrames, SampleFrame{500});
    }

    void adjacentMoveIsAcceptedAndOneFrameOverlapIsRejected()
    {
        EventTimeline timeline;
        QVERIFY(timeline.insert(event(1, 0, 0, 100)));
        QVERIFY(timeline.insert(event(2, 200, 100, 200)));

        QVERIFY(timeline.moveEvent(1, 100));
        QCOMPARE(timeline.event(1)->timelineStart, SampleFrame{100});
        QVERIFY(!timeline.moveEvent(1, 101));
        QCOMPARE(timeline.event(1)->timelineStart, SampleFrame{100});
    }

    void trimSupportsLeftAnchorAndSingleFrameEvents()
    {
        EventTimeline timeline;
        QVERIFY(timeline.insert(event(1, 100, 10, 310)));

        QVERIFY(timeline.trimEvent(1, 100, 101, 190));
        const AudioEvent* const trimmed = timeline.event(1);
        QVERIFY(trimmed != nullptr);
        QCOMPARE(trimmed->sourceStart, SampleFrame{100});
        QCOMPARE(trimmed->sourceEnd, SampleFrame{101});
        QCOMPARE(trimmed->timelineStart, SampleFrame{190});
        QCOMPARE(audibleFrames(*trimmed), SampleFrame{1});
    }

    void unknownNoopAndOverflowEditsRetainTheEntireSnapshot()
    {
        EventTimeline timeline;
        AudioEvent first = event(1, 0, 0, 200);
        first.gain = 0.75F;
        first.fadeIn = 20;
        first.fadeOut = 30;
        first.speedRatio = 1.25;
        first.pitchSemitone = -3;
        first.mute = true;
        first.envelope = {{10, 0.5F}, {100, 0.75F}};
        QVERIFY(timeline.insert(first));
        const TimelineSnapshot before = timeline.snapshot();

        QVERIFY(!timeline.moveEvent(99, 500));
        QVERIFY(!timeline.moveEvent(1, 0));
        QVERIFY(!timeline.moveEvent(1,
                                    std::numeric_limits<SampleFrame>::max() - 100));
        QVERIFY(!timeline.trimEvent(1, 0, 1'001, 0));
        compareSnapshot(timeline.snapshot(), before);
    }

    void metadataCommandsDoNotReplaceSourceOrRebuildPeaks()
    {
        EventTimeline timeline;
        QVERIFY(timeline.insert(event(1, 100, 0, 300)));
        const std::shared_ptr<const AudioSource> before_source = timeline.event(1)->source;
        const SampleFrame source_frames = before_source->total_frames;

        const auto move = TimelineEditCommand::move(timeline, 1, 500);
        QVERIFY(move.has_value());
        QVERIFY(move->execute(timeline));
        const auto trim = TimelineEditCommand::trim(timeline, 1, 50, 250, 550);
        QVERIFY(trim.has_value());
        QVERIFY(trim->execute(timeline));

        QCOMPARE(timeline.event(1)->source.get(), before_source.get());
        QCOMPARE(timeline.event(1)->source->total_frames, source_frames);
    }

    void singleEventCommandRoundTripPreservesLargeTimeline()
    {
        EventTimeline timeline;
        constexpr EventId eventCount = 2'048;
        for (EventId id = 1; id <= eventCount; ++id) {
            const SampleFrame timelineStart = static_cast<SampleFrame>(id - 1) * 4;
            QVERIFY(timeline.insert(event(id, timelineStart, 0, 1)));
        }
        const TimelineSnapshot before = timeline.snapshot();
        const EventId editedId = eventCount / 2;
        const SampleFrame movedStart = timeline.event(editedId)->timelineStart + 1;
        const auto move = TimelineEditCommand::move(timeline, editedId, movedStart);
        QVERIFY(move.has_value());
        QVERIFY(move->execute(timeline));
        QCOMPARE(timeline.event(editedId)->timelineStart, movedStart);
        QCOMPARE(timeline.event(1)->timelineStart,
                 before.events.front().timelineStart);
        QCOMPARE(timeline.event(eventCount)->timelineStart,
                 before.events.back().timelineStart);
        QVERIFY(move->undo(timeline));
        QCOMPARE(timeline.event(editedId)->timelineStart,
                 before.events.at(static_cast<std::size_t>(editedId - 1)).timelineStart);
        QCOMPARE(timeline.revision(), before.revision + 2);
    }

    void staleCommandExecutionAndUndoPreserveTheEntireSnapshot()
    {
        EventTimeline timeline;
        QVERIFY(timeline.insert(event(1, 0, 0, 200)));
        const auto stale_execute = TimelineEditCommand::move(timeline, 1, 500);
        QVERIFY(stale_execute.has_value());
        QVERIFY(timeline.moveEvent(1, 300));
        const TimelineSnapshot before_execute = timeline.snapshot();
        QVERIFY(!stale_execute->execute(timeline));
        compareSnapshot(timeline.snapshot(), before_execute);

        const auto stale_undo = TimelineEditCommand::move(timeline, 1, 700);
        QVERIFY(stale_undo.has_value());
        QVERIFY(stale_undo->execute(timeline));
        QVERIFY(timeline.moveEvent(1, 800));
        const TimelineSnapshot before_undo = timeline.snapshot();
        QVERIFY(!stale_undo->undo(timeline));
        compareSnapshot(timeline.snapshot(), before_undo);
    }
};

QTEST_APPLESS_MAIN(TimelineEditCommandTest)

#include "timeline_edit_command_test.moc"
