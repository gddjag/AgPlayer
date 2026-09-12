#include "audio_editor/audio_document.hpp"
#include "audio_editor/timeline_edit_command.hpp"
#include "audio_editor/timeline_undo_stack.hpp"

#include <QtTest>

#include <memory>
#include <optional>

using namespace agplayer::editor;

namespace {

std::shared_ptr<const AudioSource> source(const SampleFrame frames = 100'000)
{
    return std::make_shared<const AudioSource>(AudioSource{
        "fixture.wav", 48'000, 2, frames});
}

AudioEvent makeEvent(const EventId id, const SampleFrame timelineStart,
                     const SampleFrame sourceStart, const SampleFrame sourceEnd,
                     std::shared_ptr<const AudioSource> shared = source())
{
    return {id, std::move(shared), sourceStart, sourceEnd, timelineStart};
}

std::unique_ptr<TimelineEditCommand> command(
    std::optional<TimelineEditCommand> value)
{
    return value ? std::make_unique<TimelineEditCommand>(std::move(*value))
                 : nullptr;
}

void compareEvent(const AudioEvent& actual, const AudioEvent& expected)
{
    QCOMPARE(actual.id, expected.id);
    QCOMPARE(actual.source.get(), expected.source.get());
    QCOMPARE(actual.sourceStart, expected.sourceStart);
    QCOMPARE(actual.sourceEnd, expected.sourceEnd);
    QCOMPARE(actual.timelineStart, expected.timelineStart);
    QCOMPARE(actual.gain, expected.gain);
    QCOMPARE(actual.fadeIn, expected.fadeIn);
    QCOMPARE(actual.fadeOut, expected.fadeOut);
    QCOMPARE(actual.speedRatio, expected.speedRatio);
    QCOMPARE(actual.pitchSemitone, expected.pitchSemitone);
    QCOMPARE(actual.mute, expected.mute);
    QCOMPARE(actual.envelope.size(), expected.envelope.size());
    for (std::size_t index = 0; index < expected.envelope.size(); ++index) {
        QCOMPARE(actual.envelope[index].offset, expected.envelope[index].offset);
        QCOMPARE(actual.envelope[index].gain, expected.envelope[index].gain);
    }
}

void compareContent(const TimelineSnapshot& actual,
                    const TimelineSnapshot& expected)
{
    QCOMPARE(actual.totalFrames, expected.totalFrames);
    QCOMPARE(actual.events.size(), expected.events.size());
    for (std::size_t index = 0; index < expected.events.size(); ++index) {
        compareEvent(actual.events[index], expected.events[index]);
    }
}

} // namespace

class TimelineUndoStackTest final : public QObject {
    Q_OBJECT

private slots:
    void deltaCostDoesNotGrowWithUnrelatedEvents()
    {
        EventTimeline small;
        QVERIFY(small.insert(makeEvent(1, 0, 0, 100)));
        const auto smallCommand = TimelineEditCommand::move(small, 1, 100);
        QVERIFY(smallCommand.has_value());

        EventTimeline large;
        const auto shared = source();
        QVERIFY(large.insert(makeEvent(1, 0, 0, 100, shared)));
        for (EventId id = 2; id <= 100; ++id) {
            const SampleFrame offset = static_cast<SampleFrame>(id - 1) * 200;
            QVERIFY(large.insert(makeEvent(id, offset, offset, offset + 100, shared)));
        }
        const auto largeCommand = TimelineEditCommand::move(large, 1, 100);
        QVERIFY(largeCommand.has_value());

        QCOMPARE(largeCommand->affectedEventCount(), std::size_t{1});
        QCOMPARE(largeCommand->byteCost(), smallCommand->byteCost());
    }

    void moveAndTrimRoundTripWithOneRevisionPerTransition()
    {
        EventTimeline timeline;
        AudioEvent initial = makeEvent(1, 0, 0, 1'000);
        initial.envelope = {{10, 0.5F}, {500, 0.75F}};
        QVERIFY(timeline.insert(initial));
        const auto initialRevision = timeline.revision();
        TimelineUndoStack stack;

        QVERIFY(stack.executeAndPush(
            command(TimelineEditCommand::move(timeline, 1, 1'200)), timeline));
        QCOMPARE(timeline.revision(), initialRevision + 1);
        QVERIFY(stack.executeAndPush(command(TimelineEditCommand::trim(
            timeline, 1, 100, 900, 1'300)), timeline));
        QCOMPARE(timeline.revision(), initialRevision + 2);

        QVERIFY(stack.undo(timeline));
        QCOMPARE(timeline.event(1)->sourceStart, SampleFrame{0});
        QCOMPARE(timeline.revision(), initialRevision + 3);
        QVERIFY(stack.undo(timeline));
        QCOMPARE(timeline.event(1)->timelineStart, SampleFrame{0});
        QCOMPARE(timeline.revision(), initialRevision + 4);
        QVERIFY(stack.redo(timeline));
        QVERIFY(stack.redo(timeline));
        QCOMPARE(timeline.event(1)->sourceStart, SampleFrame{100});
        QCOMPARE(timeline.event(1)->timelineStart, SampleFrame{1'300});
        QCOMPARE(timeline.revision(), initialRevision + 6);
    }

    void documentSplitDeleteCutPasteAndMergeRoundTripExactly()
    {
        {
            auto document = AudioDocument::fromSource(
                AudioSource{"split.wav", 48'000, 2, 1'000});
            const auto before = document.timelineSnapshot();
            QVERIFY(document.splitEventAt(1, 400));
            const auto after = document.timelineSnapshot();
            QVERIFY(document.undo());
            compareContent(document.timelineSnapshot(), before);
            QVERIFY(document.redo());
            compareContent(document.timelineSnapshot(), after);
        }
        {
            auto document = AudioDocument::fromSource(
                AudioSource{"delete.wav", 48'000, 2, 1'000});
            QVERIFY(document.setSelection({100, 300}));
            const auto before = document.timelineSnapshot();
            QVERIFY(document.deleteSelection());
            const auto after = document.timelineSnapshot();
            QVERIFY(document.undo());
            compareContent(document.timelineSnapshot(), before);
            QVERIFY(document.redo());
            compareContent(document.timelineSnapshot(), after);
        }
        {
            auto document = AudioDocument::fromSource(
                AudioSource{"cut.wav", 48'000, 2, 1'000});
            QVERIFY(document.setSelection({100, 300}));
            const auto before = document.timelineSnapshot();
            QVERIFY(document.cutSelection());
            const auto after = document.timelineSnapshot();
            QVERIFY(document.hasClipboard());
            QVERIFY(document.undo());
            compareContent(document.timelineSnapshot(), before);
            QVERIFY(document.hasClipboard());
            QVERIFY(document.redo());
            compareContent(document.timelineSnapshot(), after);
            QVERIFY(document.hasClipboard());
        }
        {
            auto document = AudioDocument::fromSource(
                AudioSource{"paste.wav", 48'000, 2, 1'000});
            QVERIFY(document.setSelection({0, 100}));
            QVERIFY(document.copySelection());
            QVERIFY(document.deleteSelection());
            const auto beforePaste = document.timelineSnapshot();
            QVERIFY(document.pasteAt(0));
            const auto afterPaste = document.timelineSnapshot();
            QVERIFY(document.undo());
            compareContent(document.timelineSnapshot(), beforePaste);
            QVERIFY(document.redo());
            compareContent(document.timelineSnapshot(), afterPaste);
        }
        {
            auto document = AudioDocument::fromSource(
                AudioSource{"merge.wav", 48'000, 2, 1'000});
            QVERIFY(document.splitEventAt(1, 500));
            const auto split = document.timelineSnapshot();
            QVERIFY(document.mergeEvents(1, 2));
            const auto merged = document.timelineSnapshot();
            QVERIFY(document.undo());
            compareContent(document.timelineSnapshot(), split);
            QVERIFY(document.redo());
            compareContent(document.timelineSnapshot(), merged);
        }
    }

    void redoIsInvalidatedByANewSuccessfulEdit()
    {
        EventTimeline timeline;
        QVERIFY(timeline.insert(makeEvent(1, 0, 0, 100)));
        TimelineUndoStack stack;
        QVERIFY(stack.executeAndPush(
            command(TimelineEditCommand::move(timeline, 1, 200)), timeline));
        QVERIFY(stack.undo(timeline));
        QVERIFY(stack.canRedo());
        QVERIFY(stack.executeAndPush(
            command(TimelineEditCommand::move(timeline, 1, 400)), timeline));
        QVERIFY(!stack.canRedo());
        QVERIFY(!stack.redo(timeline));
    }

    void staleAndFailedTransactionsPreserveTimelineAndBothStacks()
    {
        EventTimeline timeline;
        QVERIFY(timeline.insert(makeEvent(1, 0, 0, 100)));
        TimelineUndoStack stack;
        auto stale = command(TimelineEditCommand::move(timeline, 1, 200));
        QVERIFY(stale != nullptr);
        QVERIFY(timeline.moveEvent(1, 400));
        const TimelineSnapshot beforeExecute = timeline.snapshot();
        QVERIFY(!stack.executeAndPush(std::move(stale), timeline));
        compareContent(timeline.snapshot(), beforeExecute);
        QCOMPARE(timeline.revision(), beforeExecute.revision);
        QCOMPARE(stack.undoCount(), std::size_t{0});
        QCOMPARE(stack.redoCount(), std::size_t{0});

        QVERIFY(stack.executeAndPush(
            command(TimelineEditCommand::move(timeline, 1, 600)), timeline));
        QVERIFY(timeline.moveEvent(1, 800));
        const TimelineSnapshot beforeUndo = timeline.snapshot();
        QVERIFY(!stack.undo(timeline));
        compareContent(timeline.snapshot(), beforeUndo);
        QCOMPARE(timeline.revision(), beforeUndo.revision);
        QCOMPARE(stack.undoCount(), std::size_t{1});
        QCOMPARE(stack.redoCount(), std::size_t{0});
    }

    void countAndByteLimitsEvictOldestCommands()
    {
        EventTimeline countTimeline;
        QVERIFY(countTimeline.insert(makeEvent(1, 0, 0, 100)));
        TimelineUndoStack countStack({2, 1024 * 1024});
        for (const SampleFrame position : {200, 400, 600}) {
            QVERIFY(countStack.executeAndPush(command(TimelineEditCommand::move(
                countTimeline, 1, position)), countTimeline));
        }
        QCOMPARE(countStack.undoCount(), std::size_t{2});
        QVERIFY(countStack.undo(countTimeline));
        QVERIFY(countStack.undo(countTimeline));
        QCOMPARE(countTimeline.event(1)->timelineStart, SampleFrame{200});
        QVERIFY(!countStack.undo(countTimeline));

        EventTimeline byteTimeline;
        QVERIFY(byteTimeline.insert(makeEvent(1, 0, 0, 100)));
        const auto probe = TimelineEditCommand::move(byteTimeline, 1, 200);
        QVERIFY(probe.has_value());
        TimelineUndoStack byteStack({10, probe->byteCost() * 2});
        for (const SampleFrame position : {200, 400, 600}) {
            QVERIFY(byteStack.executeAndPush(command(TimelineEditCommand::move(
                byteTimeline, 1, position)), byteTimeline));
        }
        QCOMPARE(byteStack.undoCount(), std::size_t{2});
        QVERIFY(byteStack.retainedBytes() <= probe->byteCost() * 2);
        QVERIFY(byteStack.undo(byteTimeline));
        QVERIFY(byteStack.undo(byteTimeline));
        QCOMPARE(byteTimeline.event(1)->timelineStart, SampleFrame{200});
        QVERIFY(!byteStack.undo(byteTimeline));
    }

    void oversizedSuccessfulCommandIsNotRetainedAndClearsHistory()
    {
        EventTimeline timeline;
        QVERIFY(timeline.insert(makeEvent(1, 0, 0, 1'000)));
        auto probe = TimelineEditCommand::move(timeline, 1, 1'200);
        QVERIFY(probe.has_value());
        const auto byteCost = probe->byteCost();
        TimelineUndoStack stack({10, byteCost - 1});
        QVERIFY(stack.executeAndPush(command(std::move(probe)), timeline));
        QCOMPARE(timeline.event(1)->timelineStart, SampleFrame{1'200});
        QVERIFY(!stack.canUndo());
        QVERIFY(!stack.canRedo());
        QCOMPARE(stack.retainedBytes(), std::size_t{0});
    }

    void oneHundredCompatiblePreviewsCoalesceIntoOneUndoItem()
    {
        EventTimeline timeline;
        QVERIFY(timeline.insert(makeEvent(1, 0, 0, 100)));
        TimelineUndoStack stack;
        stack.beginCoalescedEdit(1);
        for (int preview = 1; preview <= 100; ++preview) {
            QVERIFY(stack.executeAndPush(command(TimelineEditCommand::move(
                timeline, 1, preview * 200)), timeline));
        }
        stack.endCoalescedEdit();
        QCOMPARE(stack.undoCount(), std::size_t{1});
        QCOMPARE(timeline.event(1)->timelineStart, SampleFrame{20'000});
        QVERIFY(stack.undo(timeline));
        QCOMPARE(timeline.event(1)->timelineStart, SampleFrame{0});
        QVERIFY(stack.redo(timeline));
        QCOMPARE(timeline.event(1)->timelineStart, SampleFrame{20'000});

        stack.beginCoalescedEdit(1);
        QVERIFY(stack.executeAndPush(command(TimelineEditCommand::move(
            timeline, 1, 21'000)), timeline));
        stack.endCoalescedEdit();
        QCOMPARE(stack.undoCount(), std::size_t{2});
    }

    void failedPreviewDoesNotChangeActiveGestureOrHistory()
    {
        EventTimeline timeline;
        QVERIFY(timeline.insert(makeEvent(1, 0, 0, 100)));
        QVERIFY(timeline.insert(makeEvent(2, 400, 100, 200)));
        TimelineUndoStack stack;
        stack.beginCoalescedEdit(1);
        QVERIFY(stack.executeAndPush(
            command(TimelineEditCommand::move(timeline, 1, 200)), timeline));
        auto rejected = command(TimelineEditCommand::move(timeline, 1, 350));
        QVERIFY(rejected != nullptr);
        const TimelineSnapshot failedState = timeline.snapshot();
        QVERIFY(!stack.executeAndPush(std::move(rejected), timeline));
        compareContent(timeline.snapshot(), failedState);
        QCOMPARE(timeline.revision(), failedState.revision);
        QCOMPARE(stack.undoCount(), std::size_t{1});

        QVERIFY(stack.executeAndPush(
            command(TimelineEditCommand::move(timeline, 1, 300)), timeline));
        stack.endCoalescedEdit();
        QCOMPARE(stack.undoCount(), std::size_t{1});
        QVERIFY(stack.undo(timeline));
        QCOMPARE(timeline.event(1)->timelineStart, SampleFrame{0});
    }

    void oneHundredUndoRedoCyclesRemainExact()
    {
        EventTimeline timeline;
        QVERIFY(timeline.insert(makeEvent(1, 0, 0, 100)));
        TimelineUndoStack stack;
        const auto beforeRevision = timeline.revision();
        QVERIFY(stack.executeAndPush(
            command(TimelineEditCommand::move(timeline, 1, 200)), timeline));
        for (int cycle = 0; cycle < 100; ++cycle) {
            QVERIFY(stack.undo(timeline));
            QCOMPARE(timeline.event(1)->timelineStart, SampleFrame{0});
            QVERIFY(stack.redo(timeline));
            QCOMPARE(timeline.event(1)->timelineStart, SampleFrame{200});
        }
        QCOMPARE(timeline.revision(), beforeRevision + 201);
    }
};

QTEST_APPLESS_MAIN(TimelineUndoStackTest)

#include "timeline_undo_stack_test.moc"
