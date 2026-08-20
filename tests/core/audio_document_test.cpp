#include "audio_editor/audio_document.hpp"

#include <QtTest>

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
};

QTEST_APPLESS_MAIN(AudioDocumentTest)

#include "audio_document_test.moc"
