#include "audio_editor/audio_document.hpp"

#include <QtTest>

#include <memory>

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
            QCOMPARE(event.fadeIn, valid.fadeIn);
            QCOMPARE(event.fadeOut, valid.fadeOut);
            QCOMPARE(event.speedRatio, valid.speedRatio);
            QCOMPARE(event.pitchSemitone, valid.pitchSemitone);
            QCOMPARE(event.mute, valid.mute);
            QCOMPARE(event.envelope.size(), valid.envelope.size());
        }

        AudioEvent invalid = valid;
        invalid.id = 8;
        invalid.fadeIn = 600;
        auto rejected = AudioDocument::fromEvents({invalid});
        const TimelineSnapshot before = rejected.timelineSnapshot();
        QVERIFY(!rejected.splitEventAt(8, 500));
        const TimelineSnapshot after = rejected.timelineSnapshot();
        QCOMPARE(after.revision, before.revision);
        QCOMPARE(after.events.front().sourceEnd, before.events.front().sourceEnd);
        QCOMPARE(after.events.front().fadeIn, before.events.front().fadeIn);
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
        QVERIFY(!value.pasteAt(450));
        const TimelineSnapshot rejected = value.timelineSnapshot();
        QCOMPARE(rejected.revision, pasted.revision);
        QCOMPARE(rejected.totalFrames, pasted.totalFrames);
        QCOMPARE(rejected.events.size(), pasted.events.size());
        for (std::size_t index = 0; index < pasted.events.size(); ++index) {
            QCOMPARE(rejected.events.at(index).id, pasted.events.at(index).id);
            QCOMPARE(rejected.events.at(index).timelineStart,
                     pasted.events.at(index).timelineStart);
        }

        auto cut = document();
        QVERIFY(cut.setSelection({100, 300}));
        QVERIFY(cut.cutSelection());
        QCOMPARE(cut.timelineSnapshot().events.size(), std::size_t{2});
        QCOMPARE(cut.timelineSnapshot().events.at(1).timelineStart, SampleFrame{300});
    }

    void multiEventClipboardPreservesGapsIdsAndRollback()
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
        QVERIFY(!value.pasteAt(450));
        const TimelineSnapshot afterCollision = value.timelineSnapshot();
        QCOMPARE(afterCollision.revision, beforeCollision.revision);
        QCOMPARE(afterCollision.events.size(), beforeCollision.events.size());

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
};

QTEST_APPLESS_MAIN(EventEditTest)

#include "event_edit_test.moc"
