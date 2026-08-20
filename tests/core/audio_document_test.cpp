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
};

QTEST_APPLESS_MAIN(AudioDocumentTest)

#include "audio_document_test.moc"
