#include "audio_editor/audio_document.hpp"

#include <QtTest>

#include <memory>
#include <limits>

using namespace agplayer::editor;

class EventEditTest final : public QObject {
    Q_OBJECT

    static AudioDocument document()
    {
        return AudioDocument::fromSource(
            AudioSource{"fixture.wav", 48'000, 2, 1'000});
    }

private slots:
    void rejectedEditsLeaveTimelineAndSelectionUntouched()
    {
        auto value = document();
        QVERIFY(value.setSelection({100, 300}));
        const TimelineSnapshot before = value.timelineSnapshot();
        const auto selection_before = value.selection();

        QVERIFY(!value.splitEventAt(1, 0));
        QVERIFY(!value.moveEvent(1, -1));

        const TimelineSnapshot after = value.timelineSnapshot();
        QCOMPARE(after.revision, before.revision);
        QCOMPARE(after.totalFrames, before.totalFrames);
        QCOMPARE(after.events.size(), before.events.size());
        QCOMPARE(after.events.front().id, before.events.front().id);
        QCOMPARE(after.events.front().timelineStart,
                 before.events.front().timelineStart);
        QCOMPARE(after.events.front().sourceStart, before.events.front().sourceStart);
        QCOMPARE(after.events.front().sourceEnd, before.events.front().sourceEnd);
        QCOMPARE(value.selection(), selection_before);
    }

    void splitSharesSourceAndKeepsBothRangesContiguous()
    {
        auto value = document();
        QVERIFY(value.splitEventAt(1, 400));
        const TimelineSnapshot snapshot = value.timelineSnapshot();
        QCOMPARE(snapshot.events.size(), std::size_t{2});
        QCOMPARE(snapshot.events.at(0).source.get(), snapshot.events.at(1).source.get());
        QCOMPARE(snapshot.events.at(0).sourceStart, SampleFrame{0});
        QCOMPARE(snapshot.events.at(0).sourceEnd, SampleFrame{400});
        QCOMPARE(snapshot.events.at(1).sourceStart, SampleFrame{400});
        QCOMPARE(snapshot.events.at(1).sourceEnd, SampleFrame{1'000});
        QCOMPARE(snapshot.events.at(1).timelineStart, SampleFrame{400});
        QCOMPARE(snapshot.revision, std::uint64_t{2});
    }

    void clipGainIsAnUndoableTimelineEdit()
    {
        auto value = document();
        const auto before = value.historyStateId();

        QVERIFY(value.setEventGain(1, 1.5F));
        QCOMPARE(value.timelineSnapshot().events.front().gain, 1.5F);
        QCOMPARE(value.historyStateId(), before + 1);
        QVERIFY(value.undo());
        QCOMPARE(value.timelineSnapshot().events.front().gain, 1.0F);
        QVERIFY(value.redo());
        QCOMPARE(value.timelineSnapshot().events.front().gain, 1.5F);

        QVERIFY(!value.setEventGain(1,
            std::numeric_limits<float>::infinity()));
        QVERIFY(!value.setEventGain(999, 0.5F));
    }

    void splitPreservesValidParametersAndRejectsInvalidCopiedParameters()
    {
        auto source = std::make_shared<const AudioSource>(
            AudioSource{"fixture.wav", 48'000, 2, 1'000});
        AudioEvent valid{7, source, 0, 1'000, 0};
        valid.gain = 0.5F;
        valid.fadeIn = 100;
        valid.fadeOut = 100;
        valid.speedRatio = 1.25;
        valid.pitchSemitone = 3;
        valid.mute = true;
        valid.envelope = {{100, 0.25F}, {200, 0.75F}};
        auto value = AudioDocument::fromEvents({valid});
        QVERIFY(value.splitEventAt(7, 500));
        const TimelineSnapshot split = value.timelineSnapshot();
        QCOMPARE(split.events.size(), std::size_t{2});
        for (const AudioEvent& event : split.events) {
            QCOMPARE(event.source, source);
            QCOMPARE(event.gain, valid.gain);
            QCOMPARE(event.speedRatio, valid.speedRatio);
            QCOMPARE(event.pitchSemitone, valid.pitchSemitone);
            QCOMPARE(event.mute, valid.mute);
            QVERIFY(isValid(event));
        }
        QCOMPARE(split.events[0].fadeIn, valid.fadeIn);
        QCOMPARE(split.events[0].fadeOut, SampleFrame{0});
        QCOMPARE(split.events[1].fadeIn, SampleFrame{0});
        QCOMPARE(split.events[1].fadeOut, valid.fadeOut);
        QCOMPARE(split.events[0].envelope.size(), std::size_t{2});
        QCOMPARE(split.events[1].envelope.size(), std::size_t{1});

        AudioEvent longFade = valid;
        longFade.id = 8;
        longFade.fadeIn = 600;
        auto reframed = AudioDocument::fromEvents({longFade});
        QVERIFY(reframed.splitEventAt(8, 500));
        const TimelineSnapshot after = reframed.timelineSnapshot();
        QCOMPARE(after.events.size(), std::size_t{2});
        QVERIFY(isValid(after.events[0]));
        QVERIFY(isValid(after.events[1]));
        QCOMPARE(after.events[0].fadeIn, SampleFrame{500});
        QCOMPARE(after.events[1].fadeIn, SampleFrame{100});
    }

    void deleteSelectionRetainsTheLaterEventTimelineStart()
    {
        auto value = document();
        QVERIFY(value.splitEventAt(1, 400));
        QVERIFY(value.setSelection({0, 400}));
        QVERIFY(value.deleteSelection());
        const TimelineSnapshot snapshot = value.timelineSnapshot();
        QCOMPARE(snapshot.events.size(), std::size_t{1});
        QCOMPARE(snapshot.events.front().timelineStart, SampleFrame{400});
        QCOMPARE(snapshot.totalFrames, SampleFrame{1'000});
    }

    void copyCutPasteAreMetadataOnlyAtomicAndFreshId()
    {
        auto value = document();
        QVERIFY(value.setSelection({100, 300}));
        const TimelineSnapshot beforeCopy = value.timelineSnapshot();
        QVERIFY(value.copySelection());
        QCOMPARE(value.timelineSnapshot().revision, beforeCopy.revision);
        const TimelineSnapshot copied = value.timelineSnapshot();
        QVERIFY(value.cutSelection());
        QVERIFY(value.pasteAt(100));
        const TimelineSnapshot pasted = value.timelineSnapshot();
        QCOMPARE(pasted.events.size(), std::size_t{3});
        QCOMPARE(pasted.events.at(0).source.get(), pasted.events.at(1).source.get());
        QVERIFY(pasted.events.at(1).id != copied.events.front().id);
        QVERIFY(value.pasteAt(450));
        const TimelineSnapshot rippled = value.timelineSnapshot();
        QCOMPARE(rippled.revision, pasted.revision + 1);
        QCOMPARE(rippled.totalFrames, pasted.totalFrames + SampleFrame{200});
        QCOMPARE(rippled.events.size(), std::size_t{5});
        QCOMPARE(rippled.events.at(2).timelineStart, SampleFrame{300});
        QCOMPARE(rippled.events.at(3).timelineStart, SampleFrame{450});
        QCOMPARE(rippled.events.at(4).timelineStart, SampleFrame{650});

        auto cut = document();
        QVERIFY(cut.setSelection({100, 300}));
        QVERIFY(cut.cutSelection());
        QCOMPARE(cut.timelineSnapshot().events.size(), std::size_t{2});
        QCOMPARE(cut.timelineSnapshot().events.at(1).timelineStart, SampleFrame{300});
    }

    void eventCommandsChangeOnlyTheExplicitSplitClip()
    {
        auto copied = document();
        QVERIFY(copied.splitEventAt(1, 400));
        const TimelineSnapshot beforeCopy = copied.timelineSnapshot();
        const AudioEvent right = beforeCopy.events.at(1);
        const auto historyBeforeCopy = copied.historyStateId();
        QVERIFY(copied.copyEvent(right.id));
        QCOMPARE(copied.historyStateId(), historyBeforeCopy);
        QCOMPARE(copied.timelineSnapshot().events.size(), std::size_t{2});
        QCOMPARE(copied.timelineSnapshot().events.at(0).sourceStart, SampleFrame{0});
        QCOMPARE(copied.timelineSnapshot().events.at(0).sourceEnd, SampleFrame{400});
        QVERIFY(copied.pasteAt(1'200));
        const TimelineSnapshot copiedSnapshot = copied.timelineSnapshot();
        QCOMPARE(copiedSnapshot.events.size(), std::size_t{3});
        QCOMPARE(copiedSnapshot.events.at(2).sourceStart, SampleFrame{400});
        QCOMPARE(copiedSnapshot.events.at(2).sourceEnd, SampleFrame{1'000});
        QCOMPARE(copiedSnapshot.events.at(2).timelineStart, SampleFrame{1'200});

        auto cut = document();
        QVERIFY(cut.splitEventAt(1, 400));
        const auto cutHistory = cut.historyStateId();
        QVERIFY(cut.cutEvent(2));
        QCOMPARE(cut.historyStateId(), cutHistory + 1);
        QCOMPARE(cut.timelineSnapshot().events.size(), std::size_t{1});
        QCOMPARE(cut.timelineSnapshot().events.front().id, EventId{1});
        QCOMPARE(cut.timelineSnapshot().events.front().sourceEnd, SampleFrame{400});
        QVERIFY(cut.undo());
        QCOMPARE(cut.timelineSnapshot().events.size(), std::size_t{2});

        auto deleted = document();
        QVERIFY(deleted.splitEventAt(1, 400));
        const auto deleteHistory = deleted.historyStateId();
        QVERIFY(deleted.deleteEvent(2));
        QCOMPARE(deleted.historyStateId(), deleteHistory + 1);
        QCOMPARE(deleted.timelineSnapshot().events.size(), std::size_t{1});
        QCOMPARE(deleted.timelineSnapshot().events.front().id, EventId{1});
        QCOMPARE(deleted.timelineSnapshot().events.front().timelineStart, SampleFrame{0});

        auto silenced = document();
        QVERIFY(silenced.splitEventAt(1, 400));
        const auto silenceHistory = silenced.historyStateId();
        QVERIFY(silenced.silenceEvent(2));
        QCOMPARE(silenced.historyStateId(), silenceHistory + 1);
        QVERIFY(!silenced.timelineSnapshot().events.at(0).mute);
        QVERIFY(silenced.timelineSnapshot().events.at(1).mute);

        for (const bool fadeIn : {true, false}) {
            auto faded = document();
            QVERIFY(faded.splitEventAt(1, 400));
            const auto fadeHistory = faded.historyStateId();
            QVERIFY(faded.fadeEvent(2, fadeIn));
            QCOMPARE(faded.historyStateId(), fadeHistory + 1);
            const TimelineSnapshot fadedSnapshot = faded.timelineSnapshot();
            QCOMPARE(fadedSnapshot.events.at(0).fadeIn, SampleFrame{0});
            QCOMPARE(fadedSnapshot.events.at(0).fadeOut, SampleFrame{0});
            QCOMPARE(fadedSnapshot.events.at(1).fadeIn,
                     fadeIn ? SampleFrame{600} : SampleFrame{0});
            QCOMPARE(fadedSnapshot.events.at(1).fadeOut,
                     fadeIn ? SampleFrame{0} : SampleFrame{600});
        }
    }

    void pasteAtClipBoundaryRipplesOnlyFollowingEventsAndIsOneUndoStep()
    {
        auto value = document();
        QVERIFY(value.splitEventAt(1, 400));
        const TimelineSnapshot split = value.timelineSnapshot();
        const AudioEvent right = split.events.at(1);
        const auto beforePasteHistory = value.historyStateId();

        QVERIFY(value.copyEvent(right.id));
        QVERIFY(value.pasteAt(400));

        const TimelineSnapshot pasted = value.timelineSnapshot();
        QCOMPARE(pasted.events.size(), std::size_t{3});
        QCOMPARE(pasted.totalFrames, SampleFrame{1'600});
        QCOMPARE(pasted.events.at(0).id, EventId{1});
        QCOMPARE(pasted.events.at(0).timelineStart, SampleFrame{0});
        QCOMPARE(pasted.events.at(0).sourceStart, SampleFrame{0});
        QCOMPARE(pasted.events.at(0).sourceEnd, SampleFrame{400});
        QCOMPARE(pasted.events.at(1).timelineStart, SampleFrame{400});
        QCOMPARE(pasted.events.at(1).sourceStart, SampleFrame{400});
        QCOMPARE(pasted.events.at(1).sourceEnd, SampleFrame{1'000});
        QCOMPARE(pasted.events.at(2).id, right.id);
        QCOMPARE(pasted.events.at(2).timelineStart, SampleFrame{1'000});
        QCOMPARE(pasted.events.at(2).sourceStart, SampleFrame{400});
        QCOMPARE(pasted.events.at(2).sourceEnd, SampleFrame{1'000});
        QCOMPARE(value.historyStateId(), beforePasteHistory + 1);

        QVERIFY(value.undo());
        const TimelineSnapshot undone = value.timelineSnapshot();
        QCOMPARE(undone.events.size(), std::size_t{2});
        QCOMPARE(undone.totalFrames, SampleFrame{1'000});
        QCOMPARE(undone.events.at(1).id, right.id);
        QCOMPARE(undone.events.at(1).timelineStart, SampleFrame{400});
    }

    void pasteInsideEventSplitsTheExistingClipAfterTheClipboardClone()
    {
        auto value = document();
        QVERIFY(value.copyEvent(1));

        QVERIFY(value.pasteAt(400));

        const TimelineSnapshot pasted = value.timelineSnapshot();
        QCOMPARE(pasted.events.size(), std::size_t{3});
        QCOMPARE(pasted.totalFrames, SampleFrame{2'000});
        QCOMPARE(pasted.events.at(0).id, EventId{1});
        QCOMPARE(pasted.events.at(0).timelineStart, SampleFrame{0});
        QCOMPARE(pasted.events.at(0).sourceStart, SampleFrame{0});
        QCOMPARE(pasted.events.at(0).sourceEnd, SampleFrame{400});
        QCOMPARE(pasted.events.at(1).id, EventId{2});
        QCOMPARE(pasted.events.at(1).timelineStart, SampleFrame{400});
        QCOMPARE(pasted.events.at(1).sourceStart, SampleFrame{0});
        QCOMPARE(pasted.events.at(1).sourceEnd, SampleFrame{1'000});
        QCOMPARE(pasted.events.at(2).id, EventId{3});
        QCOMPARE(pasted.events.at(2).timelineStart, SampleFrame{1'400});
        QCOMPARE(pasted.events.at(2).sourceStart, SampleFrame{400});
        QCOMPARE(pasted.events.at(2).sourceEnd, SampleFrame{1'000});
    }

    void multiEventClipboardPreservesGapsIdsAndRipplesCollisions()
    {
        auto value = document();
        QVERIFY(value.splitEventAt(1, 200));
        QVERIFY(value.moveEvent(2, 400));
        QVERIFY(value.splitEventAt(2, 600));
        QVERIFY(value.setSelection({100, 800}));
        QVERIFY(value.cutSelection());
        const TimelineSnapshot cut = value.timelineSnapshot();
        QCOMPARE(cut.events.size(), std::size_t{2});
        QCOMPARE(cut.events.at(0).timelineStart, SampleFrame{0});
        QCOMPARE(cut.events.at(1).timelineStart, SampleFrame{800});
        QCOMPARE(cut.revision, std::uint64_t{5});

        QVERIFY(value.pasteAt(100));
        const TimelineSnapshot pasted = value.timelineSnapshot();
        QCOMPARE(pasted.events.size(), std::size_t{5});
        QCOMPARE(pasted.events.at(1).timelineStart, SampleFrame{100});
        QCOMPARE(pasted.events.at(2).timelineStart, SampleFrame{400});
        QCOMPARE(pasted.events.at(3).timelineStart, SampleFrame{600});
        QCOMPARE(pasted.events.at(1).id, EventId{6});
        QCOMPARE(pasted.events.at(2).id, EventId{7});
        QCOMPARE(pasted.events.at(3).id, EventId{8});
        QCOMPARE(pasted.revision, cut.revision + 1);
        for (std::size_t first = 0; first < pasted.events.size(); ++first) {
            for (std::size_t second = first + 1; second < pasted.events.size(); ++second) {
                QVERIFY(pasted.events.at(first).id != pasted.events.at(second).id);
            }
        }

        const TimelineSnapshot beforeCollision = value.timelineSnapshot();
        QVERIFY(value.pasteAt(450));
        const TimelineSnapshot afterCollision = value.timelineSnapshot();
        QCOMPARE(afterCollision.revision, beforeCollision.revision + 1);
        QCOMPARE(afterCollision.totalFrames,
                 beforeCollision.totalFrames + SampleFrame{700});
        QCOMPARE(afterCollision.events.size(), std::size_t{9});

        auto preserveClipboard = document();
        QVERIFY(preserveClipboard.splitEventAt(1, 200));
        QVERIFY(preserveClipboard.moveEvent(2, 400));
        QVERIFY(preserveClipboard.setSelection({100, 200}));
        QVERIFY(preserveClipboard.copySelection());
        QVERIFY(preserveClipboard.setSelection({200, 400}));
        const TimelineSnapshot beforeFailedCut = preserveClipboard.timelineSnapshot();
        QVERIFY(!preserveClipboard.cutSelection());
        QCOMPARE(preserveClipboard.timelineSnapshot().revision, beforeFailedCut.revision);
        QVERIFY(preserveClipboard.pasteAt(200));

        auto deleted = document();
        QVERIFY(deleted.splitEventAt(1, 200));
        QVERIFY(deleted.moveEvent(2, 400));
        QVERIFY(deleted.splitEventAt(2, 600));
        QVERIFY(deleted.setSelection({100, 800}));
        const TimelineSnapshot beforeDelete = deleted.timelineSnapshot();
        QVERIFY(deleted.deleteSelection());
        const TimelineSnapshot afterDelete = deleted.timelineSnapshot();
        QCOMPARE(afterDelete.revision, beforeDelete.revision + 1);
        QCOMPARE(afterDelete.events.size(), std::size_t{2});
        QCOMPARE(afterDelete.events.at(0).timelineStart, SampleFrame{0});
        QCOMPARE(afterDelete.events.at(1).timelineStart, SampleFrame{800});
    }

    void mergeRejectsDifferentMetadataAndMergesOnlyExactNeighbors()
    {
        auto value = document();
        QVERIFY(value.splitEventAt(1, 400));
        const TimelineSnapshot before = value.timelineSnapshot();
        QVERIFY(value.mergeEvents(before.events.at(0).id, before.events.at(1).id));
        QCOMPARE(value.timelineSnapshot().revision, before.revision + 1);
        QCOMPARE(value.timelineSnapshot().events.size(), std::size_t{1});

        auto rejected = document();
        QVERIFY(rejected.splitEventAt(1, 400));
        const auto parts = rejected.timelineSnapshot();
        QVERIFY(rejected.moveEvent(parts.events.at(1).id, 500));
        const TimelineSnapshot rejected_before = rejected.timelineSnapshot();
        QVERIFY(!rejected.mergeEvents(parts.events.at(0).id, parts.events.at(1).id));
        QCOMPARE(rejected.timelineSnapshot().revision, rejected_before.revision);
    }

    void mergeRejectsEveryMismatchedParameter()
    {
        const auto source = std::make_shared<const AudioSource>(
            AudioSource{"fixture.wav", 48'000, 2, 1'000});
        const auto other = std::make_shared<const AudioSource>(
            AudioSource{"other.wav", 48'000, 2, 1'000});
        for (int mismatch = 0; mismatch < 7; ++mismatch) {
            AudioEvent left{1, source, 0, 500, 0};
            AudioEvent right{2, source, 500, 1'000, 500};
            switch (mismatch) {
            case 0: right.source = other; break;
            case 1: right.gain = 0.5F; break;
            case 2: right.fadeIn = 1; break;
            case 3: right.speedRatio = 1.25; break;
            case 4: right.pitchSemitone = 1; break;
            case 5: right.mute = true; break;
            case 6: right.envelope = {{1, 0.5F}}; break;
            default: Q_UNREACHABLE();
            }
            auto value = AudioDocument::fromEvents({left, right});
            const TimelineSnapshot before = value.timelineSnapshot();
            QVERIFY(!value.mergeEvents(1, 2));
            QCOMPARE(value.timelineSnapshot().revision, before.revision);
            QCOMPARE(value.timelineSnapshot().events.size(), before.events.size());
        }
    }

    void exhaustedEventIdsRejectSplitAndPasteWithoutMutation()
    {
        const auto sharedSource = std::make_shared<const AudioSource>(
            AudioSource{"fixture.wav", 48'000, 2, 1'000});
        constexpr EventId lastUsable = std::numeric_limits<EventId>::max() - 1;
        AudioEvent event{lastUsable, sharedSource, 0, 1'000, 0};
        auto value = AudioDocument::fromEvents({event});
        const TimelineSnapshot before = value.timelineSnapshot();

        QVERIFY(!value.splitEventAt(lastUsable, 500));
        QCOMPARE(value.timelineSnapshot().revision, before.revision);
        QCOMPARE(value.timelineSnapshot().events.size(), before.events.size());

        QVERIFY(value.setSelection({0, 1'000}));
        QVERIFY(value.copySelection());
        QVERIFY(!value.pasteAt(2'000));
        QCOMPARE(value.timelineSnapshot().revision, before.revision);
        QCOMPARE(value.timelineSnapshot().events.size(), before.events.size());
        QCOMPARE(value.timelineSnapshot().events.front().id, lastUsable);
    }
};

QTEST_APPLESS_MAIN(EventEditTest)

#include "event_edit_test.moc"
