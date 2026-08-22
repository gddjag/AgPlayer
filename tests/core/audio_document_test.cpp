#include "audio_editor/audio_document.hpp"

#include <QtTest>

#include <limits>

using namespace agplayer::editor;

class AudioDocumentTest final : public QObject {
    Q_OBJECT

    static AudioDocument document(const SampleFrame frames = 1'000)
    {
        return AudioDocument::fromSource(
            AudioSource{"fixture.wav", 48'000, 2, frames});
    }

private slots:
    void selectionDoesNotChangeTimelineRevision()
    {
        auto value = document();
        const auto revision = value.timelineSnapshot().revision;
        QVERIFY(value.setSelection({100, 200}));
        QCOMPARE(value.timelineSnapshot().revision, revision);
        QVERIFY(value.clearSelection());
        QCOMPARE(value.timelineSnapshot().revision, revision);
    }

    void rejectsInvalidSelectionWithoutChangingState()
    {
        auto value = document();
        QVERIFY(!value.setSelection({-1, 100}));
        QVERIFY(!value.setSelection({100, 100}));
        QVERIFY(!value.setSelection({900, 1'100}));
        QVERIFY(!value.selection().has_value());
    }

    void markersRemainIndependentFromTimelineEvents()
    {
        auto value = document();
        QVERIFY(value.addMarker({"intro", 100}));
        QVERIFY(value.addMarker({"outro", 900}));
        QCOMPARE(value.markers().size(), std::size_t{2});
        QVERIFY(value.renameMarker(0, "verse"));
        QCOMPARE(value.markers().front().name, std::string{"verse"});
        QVERIFY(value.removeMarker(1));
        QVERIFY(!value.addMarker({"invalid", 1'001}));
    }

    void moveAndTrimUseTheDocumentTimeline()
    {
        auto value = document();
        QVERIFY(value.trimEvent(1, 100, 800, 100));
        QVERIFY(value.moveEvent(1, 200));
        const auto event = value.timelineSnapshot().events.front();
        QCOMPARE(event.sourceStart, SampleFrame{100});
        QCOMPARE(event.sourceEnd, SampleFrame{800});
        QCOMPARE(event.timelineStart, SampleFrame{200});
        QCOMPARE(value.totalFrames(), SampleFrame{900});
    }

    void tailDeleteAndCutNormalizeEditorState_data()
    {
        QTest::addColumn<bool>("cut");
        QTest::newRow("delete") << false;
        QTest::newRow("cut") << true;
    }

    void tailDeleteAndCutNormalizeEditorState()
    {
        QFETCH(bool, cut);
        auto value = document();
        QVERIFY(value.splitEventAt(1, 600));
        QVERIFY(value.addMarker({"tail", 900}));
        QVERIFY(value.setSelection({600, 1'000}));

        QVERIFY(cut ? value.cutSelection() : value.deleteSelection());
        QCOMPARE(value.totalFrames(), SampleFrame{600});
        QVERIFY(!value.selection().has_value());
        QCOMPARE(value.markers(), (std::vector<Marker>{{"tail", 600}}));

        QVERIFY(value.undo());
        QCOMPARE(value.totalFrames(), SampleFrame{1'000});
        QVERIFY(!value.selection().has_value());
        QCOMPARE(value.markers(), (std::vector<Marker>{{"tail", 600}}));
        QVERIFY(value.redo());
        QCOMPARE(value.totalFrames(), SampleFrame{600});
        QCOMPARE(value.markers(), (std::vector<Marker>{{"tail", 600}}));
    }

    void trimMoveUndoRedoKeepSelectionAndMarkersSerializable()
    {
        auto trimmed = document();
        QVERIFY(trimmed.addMarker({"tail", 900}));
        QVERIFY(trimmed.setSelection({500, 900}));
        QVERIFY(trimmed.trimEvent(1, 0, 700, 0));
        QCOMPARE(trimmed.totalFrames(), SampleFrame{700});
        QCOMPARE(trimmed.selection(), (std::optional<Selection>{{500, 700}}));
        QCOMPARE(trimmed.markers(), (std::vector<Marker>{{"tail", 700}}));
        QVERIFY(trimmed.undo());
        QCOMPARE(trimmed.totalFrames(), SampleFrame{1'000});
        QCOMPARE(trimmed.selection(), (std::optional<Selection>{{500, 700}}));
        QCOMPARE(trimmed.markers(), (std::vector<Marker>{{"tail", 700}}));
        QVERIFY(trimmed.redo());
        QCOMPARE(trimmed.selection(), (std::optional<Selection>{{500, 700}}));

        auto shared = std::make_shared<const AudioSource>(AudioSource{
            "fixture.wav", 48'000, 2, 500});
        auto moved = AudioDocument::fromEvents({{7, shared, 0, 500, 100}});
        QVERIFY(moved.addMarker({"end", 600}));
        QVERIFY(moved.setSelection({550, 600}));
        QVERIFY(moved.moveEvent(7, 0));
        QCOMPARE(moved.totalFrames(), SampleFrame{500});
        QVERIFY(!moved.selection().has_value());
        QCOMPARE(moved.markers(), (std::vector<Marker>{{"end", 500}}));
        QVERIFY(moved.undo());
        QCOMPARE(moved.totalFrames(), SampleFrame{600});
        QVERIFY(!moved.selection().has_value());
        QCOMPARE(moved.markers(), (std::vector<Marker>{{"end", 500}}));
        QVERIFY(moved.redo());
        QCOMPARE(moved.totalFrames(), SampleFrame{500});
    }

    void clearingTimelineClampsMarkersToZeroWithoutHistoryRewindingThem()
    {
        auto value = document();
        QVERIFY(value.addMarker({"tail", 900}));
        QVERIFY(value.setSelection({0, 1'000}));
        QVERIFY(value.deleteSelection());
        QCOMPARE(value.totalFrames(), SampleFrame{0});
        QVERIFY(!value.selection().has_value());
        QCOMPARE(value.markers(), (std::vector<Marker>{{"tail", 0}}));
        QVERIFY(value.undo());
        QCOMPARE(value.totalFrames(), SampleFrame{1'000});
        QCOMPARE(value.markers(), (std::vector<Marker>{{"tail", 0}}));
        QVERIFY(value.redo());
        QCOMPARE(value.totalFrames(), SampleFrame{0});
        QCOMPARE(value.markers(), (std::vector<Marker>{{"tail", 0}}));
    }

    void cropToSelectionPreservesInteriorGapAndUsesOneUndoStep()
    {
        const auto source = std::make_shared<const AudioSource>(AudioSource{
            "fixture.wav", 48'000, 2, 1'000});
        auto value = AudioDocument::fromEvents({
            AudioEvent{1, source, 0, 300, 100},
            AudioEvent{2, source, 400, 700, 500}});
        QVERIFY(value.setSelection({200, 600}));

        QVERIFY(value.cropToSelection());
        const auto cropped = value.timelineSnapshot();
        QCOMPARE(cropped.events.size(), std::size_t{2});
        QCOMPARE(cropped.events[0].sourceStart, SampleFrame{100});
        QCOMPARE(cropped.events[0].sourceEnd, SampleFrame{300});
        QCOMPARE(cropped.events[0].timelineStart, SampleFrame{0});
        QCOMPARE(cropped.events[1].sourceStart, SampleFrame{400});
        QCOMPARE(cropped.events[1].sourceEnd, SampleFrame{500});
        QCOMPARE(cropped.events[1].timelineStart, SampleFrame{300});
        QCOMPARE(cropped.totalFrames, SampleFrame{400});
        QVERIFY(!value.selection().has_value());

        QVERIFY(value.undo());
        QCOMPARE(value.timelineSnapshot().events.size(), std::size_t{2});
        QCOMPARE(value.totalFrames(), SampleFrame{800});
        QVERIFY(!value.canUndo());
    }

    void silenceSelectionSplitsOnlyTheSelectedAudioAndUsesOneUndoStep()
    {
        auto value = document();
        QVERIFY(value.setSelection({200, 400}));

        QVERIFY(value.silenceSelection());
        const auto silenced = value.timelineSnapshot();
        QCOMPARE(silenced.events.size(), std::size_t{3});
        QVERIFY(!silenced.events[0].mute);
        QVERIFY(silenced.events[1].mute);
        QVERIFY(!silenced.events[2].mute);
        QCOMPARE(silenced.events[1].sourceStart, SampleFrame{200});
        QCOMPARE(silenced.events[1].sourceEnd, SampleFrame{400});

        QVERIFY(value.undo());
        QCOMPARE(value.timelineSnapshot().events.size(), std::size_t{1});
        QVERIFY(!value.timelineSnapshot().events.front().mute);
        QVERIFY(!value.canUndo());
    }

    void fadeSelectionOperationsSetTheSelectedEventAndRemainSingleCommands_data()
    {
        QTest::addColumn<bool>("fadeIn");
        QTest::newRow("fade-in") << true;
        QTest::newRow("fade-out") << false;
    }

    void fadeSelectionOperationsSetTheSelectedEventAndRemainSingleCommands()
    {
        QFETCH(bool, fadeIn);
        auto value = document();
        QVERIFY(value.setSelection({200, 400}));

        QVERIFY(fadeIn ? value.fadeIn() : value.fadeOut());
        const auto faded = value.timelineSnapshot();
        QCOMPARE(faded.events.size(), std::size_t{3});
        QCOMPARE(faded.events[1].fadeIn, fadeIn ? SampleFrame{200}
                                                : SampleFrame{0});
        QCOMPARE(faded.events[1].fadeOut, fadeIn ? SampleFrame{0}
                                                 : SampleFrame{200});
        QCOMPARE(faded.events[0].fadeIn, SampleFrame{0});
        QCOMPARE(faded.events[0].fadeOut, SampleFrame{0});
        QCOMPARE(faded.events[2].fadeIn, SampleFrame{0});
        QCOMPARE(faded.events[2].fadeOut, SampleFrame{0});

        QVERIFY(value.undo());
        QCOMPARE(value.timelineSnapshot().events.size(), std::size_t{1});
        QVERIFY(!value.canUndo());
    }

    void fadeSelectionPreservesTheOppositeFade_data()
    {
        QTest::addColumn<bool>("fadeIn");
        QTest::newRow("fade-in-preserves-out") << true;
        QTest::newRow("fade-out-preserves-in") << false;
    }

    void fadeSelectionPreservesTheOppositeFade()
    {
        QFETCH(bool, fadeIn);
        const auto source = std::make_shared<const AudioSource>(AudioSource{
            "fixture.wav", 48'000, 2, 1'000});
        AudioEvent event{1, source, 0, 1'000, 0};
        event.fadeIn = fadeIn ? 0 : 200;
        event.fadeOut = fadeIn ? 200 : 0;
        auto value = AudioDocument::fromEvents({event});
        QVERIFY(value.setSelection({0, 1'000}));

        QVERIFY(fadeIn ? value.fadeIn() : value.fadeOut());

        const AudioEvent& faded = value.timelineSnapshot().events.front();
        QCOMPARE(faded.fadeIn, fadeIn ? SampleFrame{800} : SampleFrame{200});
        QCOMPARE(faded.fadeOut, fadeIn ? SampleFrame{200} : SampleFrame{800});
        QVERIFY(value.undo());
        QCOMPARE(value.timelineSnapshot().events.front().fadeIn, event.fadeIn);
        QCOMPARE(value.timelineSnapshot().events.front().fadeOut, event.fadeOut);
        QVERIFY(!value.canUndo());
    }

    void replaceSelectionWithSourceCommitsAsOneUndoStep()
    {
        auto value = document();
        QVERIFY(value.setSelection({200, 400}));
        const auto before = value.timelineSnapshot();
        const auto beforeState = value.historyStateId();
        AudioSource replacement{"reduced.wav", 48'000, 2, 200};

        QVERIFY(value.replaceSelectionWithSource(replacement));

        const auto replaced = value.timelineSnapshot();
        QCOMPARE(replaced.events.size(), std::size_t{3});
        QCOMPARE(replaced.events[0].id, EventId{1});
        QCOMPARE(replaced.events[0].sourceEnd, SampleFrame{200});
        QCOMPARE(replaced.events[1].id, EventId{4});
        QCOMPARE(replaced.events[1].source->path,
                 std::filesystem::path{"reduced.wav"});
        QCOMPARE(replaced.events[1].sourceStart, SampleFrame{0});
        QCOMPARE(replaced.events[1].sourceEnd, SampleFrame{200});
        QCOMPARE(replaced.events[1].timelineStart, SampleFrame{200});
        QCOMPARE(replaced.events[2].sourceStart, SampleFrame{400});
        QCOMPARE(value.historyStateId(), beforeState + 1);
        QVERIFY(!value.selection().has_value());

        QVERIFY(value.undo());
        QCOMPARE(value.timelineSnapshot().events.size(), before.events.size());
        QCOMPARE(value.timelineSnapshot().events.front().sourceStart,
                 before.events.front().sourceStart);
        QCOMPARE(value.timelineSnapshot().events.front().sourceEnd,
                 before.events.front().sourceEnd);
        QVERIFY(!value.canUndo());
    }

    void rejectedSelectionReplacementLeavesDocumentAndHistoryUntouched_data()
    {
        QTest::addColumn<AudioSource>("replacement");
        QTest::newRow("wrong-frame-count")
            << AudioSource{"short.wav", 48'000, 2, 199};
        QTest::newRow("wrong-sample-rate")
            << AudioSource{"rate.wav", 44'100, 2, 200};
        QTest::newRow("wrong-channel-count")
            << AudioSource{"mono.wav", 48'000, 1, 200};
    }

    void rejectedSelectionReplacementLeavesDocumentAndHistoryUntouched()
    {
        QFETCH(AudioSource, replacement);
        auto value = document();
        QVERIFY(value.setSelection({200, 400}));
        const auto before = value.timelineSnapshot();
        const auto beforeSelection = value.selection();
        const auto beforeState = value.historyStateId();

        QVERIFY(!value.replaceSelectionWithSource(std::move(replacement)));

        QCOMPARE(value.timelineSnapshot().revision, before.revision);
        QCOMPARE(value.timelineSnapshot().events.size(), before.events.size());
        QCOMPARE(value.selection(), beforeSelection);
        QCOMPARE(value.historyStateId(), beforeState);
        QVERIFY(!value.canUndo());
    }

    void insertSourceAtCursorIsTransactionalAndUsesOneUndoStep()
    {
        const auto source = std::make_shared<const AudioSource>(AudioSource{
            "fixture.wav", 48'000, 2, 300});
        auto value = AudioDocument::fromEvents({
            AudioEvent{1, source, 0, 100, 0},
            AudioEvent{2, source, 200, 300, 200}});
        const auto before = value.timelineSnapshot();
        const auto beforeState = value.historyStateId();

        QVERIFY(!value.insertSourceAtCursor(
            AudioSource{"overlap.wav", 48'000, 2, 50}, 50));
        QCOMPARE(value.timelineSnapshot().revision, before.revision);
        QCOMPARE(value.historyStateId(), beforeState);
        QVERIFY(!value.canUndo());

        QVERIFY(value.insertSourceAtCursor(
            AudioSource{"recording.wav", 48'000, 2, 50}, 100));
        const auto inserted = value.timelineSnapshot();
        QCOMPARE(inserted.events.size(), std::size_t{3});
        QCOMPARE(inserted.events[1].id, EventId{3});
        QCOMPARE(inserted.events[1].timelineStart, SampleFrame{100});
        QCOMPARE(inserted.events[1].sourceEnd, SampleFrame{50});
        QCOMPARE(value.historyStateId(), beforeState + 1);

        QVERIFY(value.undo());
        QCOMPARE(value.timelineSnapshot().events.size(), before.events.size());
        QVERIFY(!value.canUndo());
    }

    void setEventFadeOutUsesOneUndoStepAndRejectsInvalidLength()
    {
        const auto source = std::make_shared<const AudioSource>(AudioSource{
            "fixture.wav", 48'000, 2, 1'000});
        AudioEvent event{1, source, 0, 1'000, 0};
        event.fadeIn = 750;
        auto value = AudioDocument::fromEvents({event});
        const auto beforeState = value.historyStateId();

        QVERIFY(value.setEventFadeOut(1, 250));
        QCOMPARE(value.timelineSnapshot().events.front().fadeOut,
                 SampleFrame{250});
        QCOMPARE(value.historyStateId(), beforeState + 1);

        const auto changed = value.timelineSnapshot();
        const auto changedState = value.historyStateId();
        QVERIFY(!value.setEventFadeOut(1, 251));
        QVERIFY(!value.setEventFadeOut(99, 1));
        QCOMPARE(value.timelineSnapshot().revision, changed.revision);
        QCOMPARE(value.historyStateId(), changedState);

        QVERIFY(value.undo());
        QCOMPARE(value.timelineSnapshot().events.front().fadeOut,
                 SampleFrame{0});
        QVERIFY(!value.canUndo());
    }

    void addEnvelopePointKeepsOrderingAndEachPointIsOneUndoStep()
    {
        auto value = document();
        const auto beforeState = value.historyStateId();

        QVERIFY(value.addEnvelopePoint(1, 700, 0.7F));
        QCOMPARE(value.historyStateId(), beforeState + 1);
        QVERIFY(value.addEnvelopePoint(1, 200, 0.2F));
        QCOMPARE(value.historyStateId(), beforeState + 2);
        const auto points = value.timelineSnapshot().events.front().envelope;
        QCOMPARE(points.size(), std::size_t{2});
        QCOMPARE(points[0].offset, SampleFrame{200});
        QCOMPARE(points[1].offset, SampleFrame{700});

        const auto changed = value.timelineSnapshot();
        const auto changedState = value.historyStateId();
        QVERIFY(!value.addEnvelopePoint(1, 200, 0.8F));
        QVERIFY(!value.addEnvelopePoint(1, 1'000, 0.8F));
        QVERIFY(!value.addEnvelopePoint(
            1, 300, std::numeric_limits<float>::infinity()));
        QCOMPARE(value.timelineSnapshot().revision, changed.revision);
        QCOMPARE(value.historyStateId(), changedState);

        QVERIFY(value.undo());
        const auto afterUndo = value.timelineSnapshot().events.front().envelope;
        QCOMPARE(afterUndo.size(), std::size_t{1});
        QCOMPARE(afterUndo.front().offset, SampleFrame{700});
    }
};

QTEST_APPLESS_MAIN(AudioDocumentTest)

#include "audio_document_test.moc"
