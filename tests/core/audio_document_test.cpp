#include "audio_editor/audio_document.hpp"

#include <QtTest>

using namespace agplayer::editor;

class AudioDocumentTest final : public QObject {
    Q_OBJECT

private:
    static AudioDocument document(const SampleFrame frames = 480'000)
    {
        return AudioDocument::fromSource(
            AudioSource{"fixture.wav", 48'000, 2, frames});
    }

private slots:
    void deleteSelectionUsesHalfOpenSampleFrames()
    {
        auto doc = document();
        QVERIFY(doc.setSelection({48'000, 96'000}));
        QVERIFY(doc.apply(EditCommand::deleteSelection()));
        QCOMPARE(doc.totalFrames(), SampleFrame{432'000});
        QCOMPARE(doc.spans().size(), std::size_t{2});
        QVERIFY(doc.undo());
        QCOMPARE(doc.totalFrames(), SampleFrame{480'000});
        QVERIFY(doc.redo());
        QCOMPARE(doc.totalFrames(), SampleFrame{432'000});
    }

    void failedEditLeavesDocumentUnchanged()
    {
        auto doc = document(48'000);
        const auto before = doc.snapshot();
        QVERIFY(!doc.apply(EditCommand::cropToSelection()));
        QVERIFY(doc.snapshot() == before);
        QVERIFY(!doc.canUndo());
    }

    void silenceAndFadeKeepExactDuration()
    {
        auto doc = document(96'000);
        QVERIFY(doc.setSelection({24'000, 72'000}));
        QVERIFY(doc.apply(EditCommand::silenceSelection()));
        QCOMPARE(doc.totalFrames(), SampleFrame{96'000});
        QCOMPARE(doc.spans().size(), std::size_t{3});
        QVERIFY(doc.spans().at(1).silent);
        QCOMPARE(doc.spans().at(1).frame_count, SampleFrame{48'000});

        QVERIFY(doc.undo());
        QVERIFY(doc.setSelection({24'000, 72'000}));
        QVERIFY(doc.apply(EditCommand::fadeIn()));
        QCOMPARE(doc.totalFrames(), SampleFrame{96'000});
        QCOMPARE(doc.spans().at(1).gain_start, 0.0F);
        QCOMPARE(doc.spans().at(1).gain_end, 1.0F);
    }

    void clipboardPasteAndInsertSilenceUseFrameCounts()
    {
        auto doc = document(96'000);
        QVERIFY(doc.setSelection({24'000, 48'000}));
        QVERIFY(doc.apply(EditCommand::copySelection()));
        QVERIFY(doc.hasClipboard());
        QVERIFY(doc.apply(EditCommand::pasteAt(96'000)));
        QCOMPARE(doc.totalFrames(), SampleFrame{120'000});
        QVERIFY(doc.apply(EditCommand::insertSilence(0, 12'000)));
        QCOMPARE(doc.totalFrames(), SampleFrame{132'000});
    }

    void fadeInIsContinuousAcrossExistingSpanBoundaries()
    {
        auto doc = document(96'000);
        QVERIFY(doc.apply(EditCommand::insertSilence(48'000, 24'000)));
        QVERIFY(doc.setSelection({24'000, 96'000}));
        QVERIFY(doc.apply(EditCommand::fadeIn()));
        QCOMPARE(doc.spans().size(), std::size_t{5});
        QCOMPARE(doc.spans().at(1).gain_start, 0.0F);
        QCOMPARE(doc.spans().at(1).gain_end, 1.0F / 3.0F);
        QCOMPARE(doc.spans().at(2).gain_start, 1.0F / 3.0F);
        QCOMPARE(doc.spans().at(2).gain_end, 2.0F / 3.0F);
        QCOMPARE(doc.spans().at(3).gain_start, 2.0F / 3.0F);
        QCOMPARE(doc.spans().at(3).gain_end, 1.0F);
    }

    void fadeOutIsContinuousAcrossExistingSpanBoundaries()
    {
        auto doc = document(96'000);
        QVERIFY(doc.apply(EditCommand::insertSilence(48'000, 24'000)));
        QVERIFY(doc.setSelection({24'000, 96'000}));
        QVERIFY(doc.apply(EditCommand::fadeOut()));
        QCOMPARE(doc.spans().size(), std::size_t{5});
        QCOMPARE(doc.spans().at(1).gain_start, 1.0F);
        QCOMPARE(doc.spans().at(1).gain_end, 2.0F / 3.0F);
        QCOMPARE(doc.spans().at(2).gain_start, 2.0F / 3.0F);
        QCOMPARE(doc.spans().at(2).gain_end, 1.0F / 3.0F);
        QCOMPARE(doc.spans().at(3).gain_start, 1.0F / 3.0F);
        QCOMPARE(doc.spans().at(3).gain_end, 0.0F);
    }

    void gainWithoutSelectionProcessesWholeDocument()
    {
        auto doc = document(96'000);
        QVERIFY(doc.apply(EditCommand::gain(0.5F)));
        QCOMPARE(doc.spans().size(), std::size_t{1});
        QCOMPARE(doc.spans().front().gain_start, 0.5F);
        QCOMPARE(doc.spans().front().gain_end, 0.5F);
    }

    void deletionMovesAndRemovesMarkersDeterministically()
    {
        auto doc = document(100'000);
        QVERIFY(doc.addMarker({"before", 10'000}));
        QVERIFY(doc.addMarker({"inside", 30'000}));
        QVERIFY(doc.addMarker({"after", 80'000}));
        QVERIFY(doc.setSelection({20'000, 50'000}));
        QVERIFY(doc.apply(EditCommand::deleteSelection()));
        QCOMPARE(doc.markers().size(), std::size_t{2});
        QCOMPARE(doc.markers().at(0).frame, SampleFrame{10'000});
        QCOMPARE(doc.markers().at(1).frame, SampleFrame{50'000});
    }

    void repeatedUndoRedoPreservesDocument()
    {
        auto doc = document(10'000);
        for (int index = 0; index < 100; ++index) {
            QVERIFY(doc.apply(EditCommand::insertSilence(doc.totalFrames(), 1)));
        }
        QCOMPARE(doc.totalFrames(), SampleFrame{10'100});
        for (int index = 0; index < 100; ++index) {
            QVERIFY(doc.undo());
        }
        QCOMPARE(doc.totalFrames(), SampleFrame{10'000});
        for (int index = 0; index < 100; ++index) {
            QVERIFY(doc.redo());
        }
        QCOMPARE(doc.totalFrames(), SampleFrame{10'100});
    }
};

QTEST_APPLESS_MAIN(AudioDocumentTest)

#include "audio_document_test.moc"
