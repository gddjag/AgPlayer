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
        QVERIFY(value.selection().has_value());
        QCOMPARE(value.selection()->start, SampleFrame{0});
        QCOMPARE(value.selection()->end, SampleFrame{400});
        for (const auto& event : cropped.events) {
            QCOMPARE(event.envelope.size(), std::size_t{2});
            QCOMPARE(event.envelope.front().offset, SampleFrame{0});
            QCOMPARE(event.envelope.front().gain, 1.0F);
            QCOMPARE(event.envelope.back().offset,
                     agplayer::editor::audibleFrames(event) - 1);
            QCOMPARE(event.envelope.back().gain, 1.0F);
        }

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

        const AudioEvent faded = value.timelineSnapshot().events.front();
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

    void setEventFadeInUsesOneUndoStepAndRejectsInvalidLength()
    {
        const auto source = std::make_shared<const AudioSource>(AudioSource{
            "fixture.wav", 48'000, 2, 1'000});
        AudioEvent event{1, source, 0, 1'000, 0};
        event.fadeOut = 750;
        auto value = AudioDocument::fromEvents({event});
        const auto beforeState = value.historyStateId();

        QVERIFY(value.setEventFadeIn(1, 250));
        QCOMPARE(value.timelineSnapshot().events.front().fadeIn,
                 SampleFrame{250});
        QCOMPARE(value.historyStateId(), beforeState + 1);

        const auto changed = value.timelineSnapshot();
        const auto changedState = value.historyStateId();
        QVERIFY(!value.setEventFadeIn(1, 251));
        QVERIFY(!value.setEventFadeIn(99, 1));
        QCOMPARE(value.timelineSnapshot().revision, changed.revision);
        QCOMPARE(value.historyStateId(), changedState);

        QVERIFY(value.undo());
        QCOMPARE(value.timelineSnapshot().events.front().fadeIn,
                 SampleFrame{0});
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

    void setEventFadeCurveIsUndoableAndRejectsNoOpsOrInvalidCurves()
    {
        auto value = document();
        const auto initialState = value.historyStateId();

        QVERIFY(value.setEventFadeCurve(1, true, FadeCurve::Exponential));
        QCOMPARE(value.timelineSnapshot().events.front().fadeInCurve,
                 FadeCurve::Exponential);
        QCOMPARE(value.historyStateId(), initialState + 1);

        const auto changed = value.timelineSnapshot();
        const auto changedState = value.historyStateId();
        QVERIFY(!value.setEventFadeCurve(1, true, FadeCurve::Exponential));
        QVERIFY(!value.setEventFadeCurve(99, true, FadeCurve::Linear));
        QVERIFY(!value.setEventFadeCurve(1, false,
            static_cast<FadeCurve>(99)));
        QCOMPARE(value.timelineSnapshot().revision, changed.revision);
        QCOMPARE(value.historyStateId(), changedState);

        QVERIFY(value.undo());
        QCOMPARE(value.timelineSnapshot().events.front().fadeInCurve,
                 FadeCurve::Smooth);
        QVERIFY(value.redo());
        QCOMPARE(value.timelineSnapshot().events.front().fadeInCurve,
                 FadeCurve::Exponential);
    }

    void mergeRejectsEventsWithDifferentFadeCurves()
    {
        const auto source = std::make_shared<const AudioSource>(AudioSource{
            "fixture.wav", 48'000, 2, 1'000});
        AudioEvent left{1, source, 0, 500, 0};
        AudioEvent right{2, source, 500, 1'000, 500};
        right.fadeInCurve = FadeCurve::Linear;
        auto value = AudioDocument::fromEvents({left, right});

        QVERIFY(!value.mergeEvents(1, 2));
        QCOMPARE(value.timelineSnapshot().events.size(), std::size_t{2});
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

    void addEnvelopePointBoundsGainToTheEditorRange()
    {
        auto value = document();

        QVERIFY(value.addEnvelopePoint(1, 100, -1.0F));
        QVERIFY(value.addEnvelopePoint(1, 200, 3.0F));

        const auto points = value.timelineSnapshot().events.front().envelope;
        QCOMPARE(points.size(), std::size_t{2});
        QCOMPARE(points[0].gain, 0.0F);
        QCOMPARE(points[1].gain, 2.0F);
    }

    void eventGainIsBoundedTransactionalAndUndoable()
    {
        auto value = document();
        const auto initialState = value.historyStateId();

        QVERIFY(value.setEventGain(1, 3.0F));
        QCOMPARE(value.timelineSnapshot().events.front().gain, 2.0F);
        QCOMPARE(value.historyStateId(), initialState + 1);
        QVERIFY(value.setEventGain(1, -0.5F));
        QCOMPARE(value.timelineSnapshot().events.front().gain, 0.0F);
        QCOMPARE(value.historyStateId(), initialState + 2);

        QVERIFY(value.undo());
        QCOMPARE(value.timelineSnapshot().events.front().gain, 2.0F);
        QVERIFY(value.undo());
        QCOMPARE(value.timelineSnapshot().events.front().gain, 1.0F);
        QVERIFY(value.redo());
        QCOMPARE(value.timelineSnapshot().events.front().gain, 2.0F);
    }

    void envelopePointMoveRemoveClampCollisionAndLimitAreTransactional()
    {
        auto value = document(1'000);
        QVERIFY(value.addEnvelopePoint(1, 200, 0.5F));
        QVERIFY(value.addEnvelopePoint(1, 700, 1.5F));
        const auto beforeMove = value.historyStateId();

        QVERIFY(value.moveEnvelopePoint(1, 200, -50, 3.0F));
        const auto moved = value.timelineSnapshot().events.front().envelope;
        QCOMPARE(moved.size(), std::size_t{2});
        QCOMPARE(moved[0].offset, SampleFrame{0});
        QCOMPARE(moved[0].gain, 2.0F);
        QCOMPARE(value.historyStateId(), beforeMove + 1);

        const auto collisionSnapshot = value.timelineSnapshot();
        const auto collisionState = value.historyStateId();
        QVERIFY(!value.moveEnvelopePoint(1, 0, 700, 0.75F));
        QCOMPARE(value.timelineSnapshot().revision, collisionSnapshot.revision);
        QCOMPARE(value.historyStateId(), collisionState);

        QVERIFY(value.removeEnvelopePoint(1, 700));
        QCOMPARE(value.timelineSnapshot().events.front().envelope.size(),
                 std::size_t{1});
        QVERIFY(value.undo());
        QCOMPARE(value.timelineSnapshot().events.front().envelope.size(),
                 std::size_t{2});

        auto limited = document(1'000);
        for (std::size_t index = 0; index < kMaxEnvelopePoints; ++index) {
            QVERIFY(limited.addEnvelopePoint(
                1, static_cast<SampleFrame>(index * 10), 1.0F));
        }
        const auto full = limited.timelineSnapshot();
        const auto fullState = limited.historyStateId();
        QVERIFY(!limited.addEnvelopePoint(1, 999, 1.0F));
        QCOMPARE(limited.timelineSnapshot().revision, full.revision);
        QCOMPARE(limited.historyStateId(), fullState);
    }

    void splitClipsAndRebasesEnvelopeWithInterpolatedBoundary()
    {
        const auto source = std::make_shared<const AudioSource>(AudioSource{
            "fixture.wav", 48'000, 2, 1'000});
        AudioEvent event{1, source, 0, 1'000, 0};
        event.envelope = {{100, 0.5F}, {500, 1.5F}, {900, 0.25F}};
        auto value = AudioDocument::fromEvents({event});

        QVERIFY(value.splitEventAt(1, 400));
        const auto split = value.timelineSnapshot();
        QCOMPARE(split.events.size(), std::size_t{2});
        QCOMPARE(split.events[0].envelope.size(), std::size_t{2});
        QCOMPARE(split.events[0].envelope[0].offset, SampleFrame{100});
        QCOMPARE(split.events[0].envelope[1].offset, SampleFrame{399});
        QVERIFY(std::abs(split.events[0].envelope[1].gain - 1.2475F)
                < 0.0001F);
        QCOMPARE(split.events[1].envelope.size(), std::size_t{3});
        QCOMPARE(split.events[1].envelope[0].offset, SampleFrame{0});
        QVERIFY(std::abs(split.events[1].envelope[0].gain - 1.25F) < 0.0001F);
        QCOMPARE(split.events[1].envelope[1].offset, SampleFrame{100});
        QCOMPARE(split.events[1].envelope[1].gain, 1.5F);
        QCOMPARE(split.events[1].envelope[2].offset, SampleFrame{500});
        QCOMPARE(split.events[1].envelope[2].gain, 0.25F);

        QVERIFY(value.undo());
        const auto restored = value.timelineSnapshot().events.front().envelope;
        QCOMPARE(restored.size(), event.envelope.size());
        QCOMPARE(restored[0].offset, event.envelope[0].offset);
        QCOMPARE(restored[1].gain, event.envelope[1].gain);
        QCOMPARE(restored[2].offset, event.envelope[2].offset);
        QVERIFY(value.redo());
        QCOMPARE(value.timelineSnapshot().events.size(), std::size_t{2});
    }

    void leftAndRightTrimPreserveEnvelopeShapeWithinRemainingAudio()
    {
        const auto source = std::make_shared<const AudioSource>(AudioSource{
            "fixture.wav", 48'000, 2, 1'000});
        AudioEvent event{1, source, 0, 1'000, 0};
        event.envelope = {{100, 0.5F}, {500, 1.5F}, {900, 0.25F}};

        auto leftTrimmed = AudioDocument::fromEvents({event});
        QVERIFY(leftTrimmed.trimEvent(1, 400, 1'000, 400));
        const auto left = leftTrimmed.timelineSnapshot().events.front().envelope;
        QCOMPARE(left.size(), std::size_t{3});
        QCOMPARE(left[0].offset, SampleFrame{0});
        QVERIFY(std::abs(left[0].gain - 1.25F) < 0.0001F);
        QCOMPARE(left[1].offset, SampleFrame{100});
        QCOMPARE(left[2].offset, SampleFrame{500});

        auto rightTrimmed = AudioDocument::fromEvents({event});
        QVERIFY(rightTrimmed.trimEvent(1, 0, 600, 0));
        const auto right = rightTrimmed.timelineSnapshot().events.front().envelope;
        QCOMPARE(right.size(), std::size_t{3});
        QCOMPARE(right[0].offset, SampleFrame{100});
        QCOMPARE(right[1].offset, SampleFrame{500});
        QCOMPARE(right[2].offset, SampleFrame{599});
        QVERIFY(std::abs(right[2].gain - 1.190625F) < 0.0001F);
    }

    void cropRebasesEnvelopeAndInsertsSelectionBoundaryValue()
    {
        const auto source = std::make_shared<const AudioSource>(AudioSource{
            "fixture.wav", 48'000, 2, 1'000});
        AudioEvent event{1, source, 0, 1'000, 0};
        event.envelope = {{100, 0.5F}, {500, 1.5F}, {900, 0.25F}};
        auto value = AudioDocument::fromEvents({event});
        QVERIFY(value.setSelection({400, 800}));

        QVERIFY(value.cropToSelection());
        const auto cropped = value.timelineSnapshot();
        QCOMPARE(cropped.events.size(), std::size_t{1});
        QCOMPARE(cropped.events.front().timelineStart, SampleFrame{0});
        QCOMPARE(cropped.events.front().sourceStart, SampleFrame{400});
        QCOMPARE(cropped.events.front().sourceEnd, SampleFrame{800});
        const auto envelope = cropped.events.front().envelope;
        QCOMPARE(envelope.size(), std::size_t{3});
        QCOMPARE(envelope[0].offset, SampleFrame{0});
        QVERIFY(std::abs(envelope[0].gain - 1.25F) < 0.0001F);
        QCOMPARE(envelope[1].offset, SampleFrame{100});
        QCOMPARE(envelope[1].gain, 1.5F);
        QCOMPARE(envelope[2].offset, SampleFrame{399});
        QVERIFY(std::abs(envelope[2].gain - 0.565625F) < 0.0001F);
    }

    void trimOfFullEnvelopeKeepsBoundaryAndDeterministicallyFitsLimit()
    {
        const auto source = std::make_shared<const AudioSource>(AudioSource{
            "fixture.wav", 48'000, 2, 1'000});
        AudioEvent event{1, source, 0, 1'000, 0};
        for (std::size_t index = 0; index < kMaxEnvelopePoints; ++index) {
            event.envelope.push_back({
                static_cast<SampleFrame>(10 + index * 10),
                0.5F + static_cast<float>(index) / 128.0F});
        }
        QVERIFY(isValid(event));

        auto first = AudioDocument::fromEvents({event});
        auto second = AudioDocument::fromEvents({event});
        QVERIFY(first.trimEvent(1, 5, 1'000, 5));
        QVERIFY(second.trimEvent(1, 5, 1'000, 5));
        const auto firstSnapshot = first.timelineSnapshot();
        const auto secondSnapshot = second.timelineSnapshot();
        const AudioEvent& trimmed = firstSnapshot.events.front();
        const AudioEvent& repeated = secondSnapshot.events.front();
        QCOMPARE(trimmed.envelope.size(), kMaxEnvelopePoints);
        QCOMPARE(trimmed.envelope.size(), repeated.envelope.size());
        for (std::size_t index = 0; index < trimmed.envelope.size(); ++index) {
            QCOMPARE(trimmed.envelope[index].offset,
                     repeated.envelope[index].offset);
            QCOMPARE(trimmed.envelope[index].gain,
                     repeated.envelope[index].gain);
        }
        QCOMPARE(trimmed.envelope.front().offset, SampleFrame{0});
        QVERIFY(std::abs(trimmed.envelope.front().gain - 0.75F) < 0.0001F);
        QCOMPARE(trimmed.envelope.back().offset, SampleFrame{635});
        QCOMPARE(trimmed.envelope.back().gain, event.envelope.back().gain);
        QVERIFY(isValid(trimmed));
        for (SampleFrame offset = 0; offset < audibleFrames(trimmed);
             offset += 5) {
            QVERIFY(std::abs(envelopeGainAt(trimmed, offset)
                - envelopeGainAt(event, offset + 5)) < 0.0001F);
        }
    }

    void clearTimelineIsUndoableAndRetainsDocumentShell()
    {
        auto value = document();
        const auto original = value.timelineSnapshot();
        QVERIFY(value.setSelection({100, 200}));

        QVERIFY(value.clearTimeline());
        QCOMPARE(value.totalFrames(), SampleFrame{0});
        QVERIFY(value.timelineSnapshot().events.empty());
        QVERIFY(!value.selection().has_value());

        QVERIFY(value.undo());
        QCOMPARE(value.timelineSnapshot().events.size(), original.events.size());
        QCOMPARE(value.totalFrames(), SampleFrame{1'000});
    }

    void sharedSplitBoundaryReframesBothClipsInOneUndoStep()
    {
        const auto source = std::make_shared<const AudioSource>(AudioSource{
            "fixture.wav", 48'000, 2, 1'000});
        AudioEvent event{1, source, 0, 1'000, 0};
        event.envelope = {{100, 0.5F}, {500, 1.5F}, {900, 0.25F}};
        auto value = AudioDocument::fromEvents({event});
        QVERIFY(value.splitEventAt(1, 400));
        const auto historyAfterSplit = value.historyStateId();

        QVERIFY(value.trimSharedBoundary(1, 2, 250));
        const auto movedLeft = value.timelineSnapshot().events[0];
        const auto movedRight = value.timelineSnapshot().events[1];
        QCOMPARE(movedLeft.sourceStart, SampleFrame{0});
        QCOMPARE(movedLeft.sourceEnd, SampleFrame{250});
        QCOMPARE(movedRight.sourceStart, SampleFrame{250});
        QCOMPARE(movedRight.sourceEnd, SampleFrame{1'000});
        QCOMPARE(movedLeft.timelineStart + audibleFrames(movedLeft),
                 movedRight.timelineStart);
        QVERIFY(isValid(movedLeft));
        QVERIFY(isValid(movedRight));
        QCOMPARE(value.historyStateId(), historyAfterSplit + 1);

        QVERIFY(value.undo());
        const auto restored = value.timelineSnapshot();
        QCOMPARE(restored.events[0].sourceEnd, SampleFrame{400});
        QCOMPARE(restored.events[1].sourceStart, SampleFrame{400});
        QVERIFY(value.redo());
        QVERIFY(value.trimSharedBoundary(1, 2, 640));
        const auto expanded = value.timelineSnapshot();
        QCOMPARE(expanded.events[0].sourceEnd, SampleFrame{640});
        QCOMPARE(expanded.events[1].sourceStart, SampleFrame{640});
        QCOMPARE(expanded.events[0].timelineStart
                     + audibleFrames(expanded.events[0]),
                 expanded.events[1].timelineStart);
    }

    void sharedBoundaryPreservesEnvelopeAcrossBothDirections()
    {
        const auto source = std::make_shared<const AudioSource>(AudioSource{
            "fixture.wav", 48'000, 2, 1'000});
        AudioEvent original{1, source, 0, 1'000, 0};
        original.envelope = {{100, 0.2F}, {400, 0.8F},
                             {700, 1.6F}, {900, 0.4F}};
        const auto expectedGain = [&original](const SampleFrame sourceFrame) {
            return envelopeGainAt(original, sourceFrame);
        };
        const auto gainAtSource = [](const AudioEvent& event,
                                     const SampleFrame sourceFrame) {
            return envelopeGainAt(event, sourceFrame - event.sourceStart);
        };
        const auto verifyGain = [&](const AudioEvent& event,
                                    const SampleFrame sourceFrame) {
            QVERIFY(std::abs(gainAtSource(event, sourceFrame)
                             - expectedGain(sourceFrame)) < 0.0001F);
        };

        auto value = AudioDocument::fromEvents({original});
        QVERIFY(value.splitEventAt(1, 500));
        QVERIFY(value.trimSharedBoundary(1, 2, 250));
        auto events = value.timelineSnapshot().events;
        QVERIFY(events.size() == 2);
        verifyGain(events[0], 100);
        verifyGain(events[0], 249);
        verifyGain(events[1], 250);
        verifyGain(events[1], 400);
        verifyGain(events[1], 700);
        verifyGain(events[1], 900);

        QVERIFY(value.trimSharedBoundary(1, 2, 750));
        events = value.timelineSnapshot().events;
        verifyGain(events[0], 100);
        verifyGain(events[0], 400);
        verifyGain(events[0], 700);
        verifyGain(events[0], 749);
        verifyGain(events[1], 750);
        verifyGain(events[1], 900);
    }

    void sharedBoundaryRejectsIncompatiblePairs()
    {
        const auto first = std::make_shared<const AudioSource>(AudioSource{
            "first.wav", 48'000, 2, 1'000});
        const auto second = std::make_shared<const AudioSource>(AudioSource{
            "second.wav", 48'000, 2, 1'000});
        const AudioEvent left{1, first, 0, 400, 0};
        const AudioEvent right{2, first, 400, 1'000, 400};

        auto value = AudioDocument::fromEvents({left, right});
        QVERIFY(!value.trimSharedBoundary(1, 2, 0));
        QVERIFY(!value.trimSharedBoundary(1, 2, 1'000));

        auto differentSource = right;
        differentSource.source = second;
        value = AudioDocument::fromEvents({left, differentSource});
        QVERIFY(!value.trimSharedBoundary(1, 2, 500));

        auto gapped = right;
        gapped.timelineStart = 401;
        value = AudioDocument::fromEvents({left, gapped});
        QVERIFY(!value.trimSharedBoundary(1, 2, 500));

        auto overlapping = right;
        overlapping.timelineStart = 399;
        value = AudioDocument::fromEvents({left, overlapping});
        QVERIFY(!value.trimSharedBoundary(1, 2, 500));

        auto differentGain = right;
        differentGain.gain = 0.5F;
        value = AudioDocument::fromEvents({left, differentGain});
        QVERIFY(!value.trimSharedBoundary(1, 2, 500));

        auto differentSpeed = right;
        differentSpeed.speedRatio = 1.25;
        value = AudioDocument::fromEvents({left, differentSpeed});
        QVERIFY(!value.trimSharedBoundary(1, 2, 500));

        auto differentPitch = right;
        differentPitch.pitchSemitone = 2;
        value = AudioDocument::fromEvents({left, differentPitch});
        QVERIFY(!value.trimSharedBoundary(1, 2, 500));

        auto differentMute = right;
        differentMute.mute = true;
        value = AudioDocument::fromEvents({left, differentMute});
        QVERIFY(!value.trimSharedBoundary(1, 2, 500));
    }
};

QTEST_APPLESS_MAIN(AudioDocumentTest)

#include "audio_document_test.moc"
