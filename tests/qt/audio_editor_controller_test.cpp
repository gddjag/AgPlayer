#include "audio_editor/audio_editor_controller.hpp"

#include <QDir>
#include <QFileInfo>
#include <QtTest>

class AudioEditorControllerTest final : public QObject {
    Q_OBJECT

private slots:
    void emptyDocumentDisablesEditActions()
    {
        AudioEditorController controller;
        QVERIFY(controller.actionEnabled(QStringLiteral("editor.open")));
        QVERIFY(!controller.actionEnabled(QStringLiteral("editor.cut")));
        QVERIFY(!controller.actionEnabled(QStringLiteral("editor.paste")));
        QVERIFY(!controller.actionEnabled(QStringLiteral("editor.undo")));
    }

    void actionRoutingDeletesWithoutRipple()
    {
        AudioEditorController controller;
        QVERIFY(controller.createUntitledDocument(48'000, 2, 1'000));
        QVERIFY(controller.setSelection(96, 288));
        QVERIFY(controller.triggerAction(QStringLiteral("editor.deleteSelection")));
        QCOMPARE(controller.totalFrames(), qint64{1'000});
        QVERIFY(controller.modified());
        QCOMPARE(controller.selectionFrames(), qint64{0});
    }

    void moveAndTrimUseCanonicalDocumentTimeline()
    {
        AudioEditorController controller;
        QVERIFY(controller.createUntitledDocument(48'000, 2, 1'000));
        QVERIFY(controller.trimEvent(1, 100, 800, 100));
        QVERIFY(controller.moveEvent(1, 200));
        QCOMPARE(controller.totalFrames(), qint64{900});
        QVERIFY(controller.modified());
    }

    void splitAndMergeRouteThroughController()
    {
        AudioEditorController controller;
        QVERIFY(controller.createUntitledDocument(48'000, 2, 1'000));
        QVERIFY(controller.splitEvent(1, 400));
        QVERIFY(controller.mergeEvents(1, 2));
        QCOMPARE(controller.totalFrames(), qint64{1'000});
    }

    void splitAndMergeActionsUseDeterministicPlayheadAndSelectionRules()
    {
        AudioEditorController controller;
        QVERIFY(controller.createUntitledDocument(48'000, 2, 48'000));
        const EditorActionModel* const actions = controller.actions();
        QVERIFY(actions->action(QStringLiteral("editor.split")) != nullptr);
        QVERIFY(actions->action(QStringLiteral("editor.merge")) != nullptr);
        QVERIFY(!controller.actionEnabled(QStringLiteral("editor.split")));

        QVERIFY(controller.seekMs(500));
        QVERIFY(controller.actionEnabled(QStringLiteral("editor.split")));
        QVERIFY(controller.triggerAction(QStringLiteral("editor.split")));
        QVERIFY(!controller.actionEnabled(QStringLiteral("editor.merge")));

        QVERIFY(controller.setSelection(0, 48'000));
        QVERIFY(controller.actionEnabled(QStringLiteral("editor.merge")));
        QVERIFY(controller.triggerAction(QStringLiteral("editor.merge")));
        QCOMPARE(controller.totalFrames(), qint64{48'000});
    }

    void copyCutAndPasteActionsUseMetadataClipboard()
    {
        AudioEditorController controller;
        QVERIFY(controller.createUntitledDocument(48'000, 2, 1'000));
        QVERIFY(controller.setSelection(96, 288));
        QVERIFY(controller.triggerAction(QStringLiteral("editor.copy")));
        QVERIFY(controller.actionEnabled(QStringLiteral("editor.paste")));
        QVERIFY(controller.triggerAction(QStringLiteral("editor.cut")));
        QVERIFY(controller.seekMs(2));
        QVERIFY(controller.triggerAction(QStringLiteral("editor.paste")));
        QCOMPARE(controller.totalFrames(), qint64{1'000});
    }

    void opensRealAudioAndPublishesSummary()
    {
        const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_EDITOR_FIXTURE"));
        if (fixture.isEmpty()) QSKIP("fixture not configured");
        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY2(controller.openFile(QUrl::fromLocalFile(fixture)),
                 qPrintable(controller.errorMessage()));
        QVERIFY(controller.hasDocument());
        QVERIFY(controller.totalFrames() > 0);
        QCOMPARE(controller.fileName(), QFileInfo(fixture).fileName());
    }

    void metadataEditKeepsFixturePeaksAndDefersViewportDecode()
    {
        const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_EDITOR_FIXTURE"));
        if (fixture.isEmpty()) QSKIP("fixture not configured");
        const QDir fixtureDirectory = QFileInfo(fixture).dir();
        const QStringList filesBefore = fixtureDirectory.entryList(
            QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY2(controller.openFile(QUrl::fromLocalFile(fixture)),
                 qPrintable(controller.errorMessage()));
        const QVariantList sourcePeaks = controller.channelPeaks();
        QVERIFY(!sourcePeaks.isEmpty());
        QSignalSpy stateChanges(&controller, &AudioEditorController::stateChanged);
        QSignalSpy waveformChanges(&controller, &AudioEditorController::waveformChanged);

        QVERIFY(controller.setSelection(96, 288));
        QVERIFY(controller.triggerAction(QStringLiteral("editor.deleteSelection")));
        QCOMPARE(controller.channelPeaks(), sourcePeaks);
        QVERIFY(!controller.busy());
        QCOMPARE(controller.state(), EditorSessionState::Ready);
        QCOMPARE(stateChanges.count(), 0);
        QCOMPARE(fixtureDirectory.entryList(QDir::Files | QDir::NoDotAndDotDot,
                                            QDir::Name), filesBefore);
        QVERIFY(controller.viewportChannelPeaks().isEmpty());

        controller.viewport()->setViewportWidth(48'000.0);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.viewportChannelPeaks().isEmpty(), 10'000);
        QVERIFY(waveformChanges.count() >= 2);
        QVERIFY(!controller.busy());
        QCOMPARE(controller.state(), EditorSessionState::Ready);
        QCOMPARE(stateChanges.count(), 0);
    }

    void obsoleteReplacementOperationsAreAbsent()
    {
        const QMetaObject& meta = AudioEditorController::staticMetaObject;
        QCOMPARE(meta.indexOfMethod("reduceNoise()"), -1);
        QCOMPARE(meta.indexOfMethod("applyTimePitch()"), -1);
        QCOMPARE(meta.indexOfProperty("noiseReductionActive"), -1);
    }
};

QTEST_GUILESS_MAIN(AudioEditorControllerTest)

#include "audio_editor_controller_test.moc"
