#include "audio_editor/audio_editor_controller.hpp"

#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

class AudioEditorControllerTest final : public QObject {
    Q_OBJECT

private slots:
    void emptyDocumentDisablesDocumentActions()
    {
        AudioEditorController controller;
        QVERIFY(controller.action(QStringLiteral("editor.open"))->enabled);
        QVERIFY(controller.actionEnabled(QStringLiteral("editor.open")));
        QVERIFY(!controller.action(QStringLiteral("editor.save"))->enabled);
        QVERIFY(!controller.action(QStringLiteral("editor.cut"))->enabled);
        QVERIFY(!controller.action(QStringLiteral("editor.export"))->enabled);
    }

    void readyDocumentEnablesOnlyApplicableActions()
    {
        AudioEditorController controller;
        QVERIFY(controller.createUntitledDocument(48'000, 2, 96'000));
        QVERIFY(controller.hasDocument());
        QVERIFY(controller.action(QStringLiteral("editor.save"))->enabled);
        QVERIFY(controller.action(QStringLiteral("editor.export"))->enabled);
        QVERIFY(!controller.action(QStringLiteral("editor.cut"))->enabled);
        QVERIFY(controller.setSelection(24'000, 48'000));
        QVERIFY(controller.action(QStringLiteral("editor.cut"))->enabled);
        QVERIFY(controller.action(QStringLiteral("editor.cropToSelection"))->enabled);
    }

    void editActionsMutateThroughOneControllerSeam()
    {
        AudioEditorController controller;
        QVERIFY(controller.createUntitledDocument(48'000, 2, 96'000));
        QVERIFY(controller.setSelection(24'000, 48'000));
        QVERIFY(controller.triggerAction(QStringLiteral("editor.deleteSelection")));
        QCOMPARE(controller.totalFrames(), qint64{72'000});
        QVERIFY(controller.modified());
        QVERIFY(controller.action(QStringLiteral("editor.undo"))->enabled);
        QVERIFY(controller.triggerAction(QStringLiteral("editor.undo")));
        QCOMPARE(controller.totalFrames(), qint64{96'000});
        QVERIFY(controller.action(QStringLiteral("editor.redo"))->enabled);
    }

    void exposesStableCommandOrder()
    {
        AudioEditorController controller;
        const QStringList expected{
            "editor.open", "editor.newRecording", "editor.save",
            "editor.undo", "editor.redo", "editor.cut", "editor.copy",
            "editor.paste", "editor.deleteSelection", "editor.cropToSelection",
            "editor.silenceSelection", "editor.fadeIn", "editor.fadeOut",
            "editor.moreMenu", "editor.export"};
        QCOMPARE(controller.actions()->ids(), expected);
    }

    void opensRealAudioAndPublishesSummaryAndWaveform()
    {
        const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_EDITOR_FIXTURE"));
        if (fixture.isEmpty()) {
            QSKIP("fixture not configured");
        }
        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY2(controller.openFile(QUrl::fromLocalFile(fixture)),
                 qPrintable(controller.errorMessage()));
        QVERIFY(controller.hasDocument());
        QVERIFY(controller.totalFrames() > 0);
        QVERIFY(controller.sampleRate() > 0);
        QVERIFY(controller.channels() > 0);
        QCOMPARE(controller.fileName(), QFileInfo(fixture).fileName());
        QVERIFY(!controller.channelPeaks().isEmpty());
    }

    void editedDocumentCanBeSavedAndPlayed()
    {
        const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_EDITOR_FIXTURE"));
        if (fixture.isEmpty()) {
            QSKIP("fixture not configured");
        }
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(controller.openFile(QUrl::fromLocalFile(fixture)));
        QVERIFY(controller.setSelection(100, 1'100));
        QVERIFY(controller.triggerAction(QStringLiteral("editor.fadeIn")));
        const QString output = directory.filePath(QStringLiteral("edited.wav"));
        QVERIFY2(controller.saveAs(QUrl::fromLocalFile(output)),
                 qPrintable(controller.errorMessage()));
        QVERIFY(QFileInfo::exists(output));
        QVERIFY(!controller.modified());
        QVERIFY(controller.playPause());
        QTRY_VERIFY_WITH_TIMEOUT(controller.playing(), 2'000);
        QVERIFY(controller.stopPlayback());
    }

    void timePitchParametersStayBidirectionalAndApplyAsOneEdit()
    {
        const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_EDITOR_FIXTURE"));
        if (fixture.isEmpty()) QSKIP("fixture not configured");
        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(controller.openFile(QUrl::fromLocalFile(fixture)));
        const qint64 originalFrames = controller.totalFrames();
        controller.setOriginalBpm(100.0);
        QVERIFY(controller.setTargetBpm(125.0));
        QCOMPARE(controller.speedPercent(), 125.0);
        QVERIFY(controller.setSpeedPercent(80.0));
        QCOMPARE(controller.targetBpm(), 80.0);
        QVERIFY(controller.setPitch(3, 25));
        QCOMPARE(controller.pitchCents(), 325);
        QVERIFY2(controller.applyTimePitch(), qPrintable(controller.errorMessage()));
        QVERIFY(controller.totalFrames() > originalFrames);
        QVERIFY(controller.modified());
        QVERIFY(controller.triggerAction(QStringLiteral("editor.undo")));
        QCOMPARE(controller.totalFrames(), originalFrames);
    }

    void timePitchPreviewPlaysWithoutMutatingTheDocument()
    {
        const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_EDITOR_FIXTURE"));
        if (fixture.isEmpty()) QSKIP("fixture not configured");
        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(controller.openFile(QUrl::fromLocalFile(fixture)));
        const qint64 originalFrames = controller.totalFrames();
        QVERIFY(controller.setSpeedPercent(125.0));
        QVERIFY(controller.setPitch(2, 0));
        QVERIFY2(controller.playPause(), qPrintable(controller.errorMessage()));
        QVERIFY(controller.timePitchPreviewActive());
        QCOMPARE(controller.totalFrames(), originalFrames);
        QVERIFY(!controller.modified());
        QVERIFY(controller.playing());
        QVERIFY(controller.stopPlayback());
    }

    void exposesRealRecordingDevicesAndRejectsInvalidRecordingPath()
    {
        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        const QVariantList devices = controller.recordingDevices();
        for (const QVariant& entry : devices) {
            const QVariantMap item = entry.toMap();
            QVERIFY(!item.value(QStringLiteral("id")).toString().isEmpty());
            QVERIFY(!item.value(QStringLiteral("name")).toString().isEmpty());
        }
        QVERIFY(!controller.startRecording(QUrl(), QString(), 48'000, 2,
                                           false, false));
        QVERIFY(!controller.errorMessage().isEmpty());
    }
};

QTEST_APPLESS_MAIN(AudioEditorControllerTest)

#include "audio_editor_controller_test.moc"
