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
        QVERIFY(value.copySelection());
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

    void mergeRejectsDifferentMetadataAndMergesOnlyExactNeighbors()
    {
        auto value = document();
        QVERIFY(value.splitEventAt(1, 400));
        const TimelineSnapshot before = value.timelineSnapshot();
        QVERIFY(value.mergeEvents(before.events.at(0).id, before.events.at(1).id));
        QCOMPARE(value.timelineSnapshot().events.size(), std::size_t{1});

        auto rejected = document();
        QVERIFY(rejected.splitEventAt(1, 400));
        const auto parts = rejected.timelineSnapshot();
        QVERIFY(rejected.moveEvent(parts.events.at(1).id, 500));
        const TimelineSnapshot rejected_before = rejected.timelineSnapshot();
        QVERIFY(!rejected.mergeEvents(parts.events.at(0).id, parts.events.at(1).id));
        QCOMPARE(rejected.timelineSnapshot().revision, rejected_before.revision);
    }
};

QTEST_APPLESS_MAIN(EventEditTest)

#include "event_edit_test.moc"
