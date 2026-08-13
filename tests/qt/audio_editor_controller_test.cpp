#include "audio_editor/audio_editor_controller.hpp"
#include "library_model.hpp"
#include "playback_controller.hpp"

#include <QFileInfo>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QtTest>

class AudioEditorControllerTest final : public QObject {
    Q_OBJECT

private slots:
    void stopsMainPlaybackBeforeEditorPreview()
    {
        const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_EDITOR_FIXTURE"));
        if (fixture.isEmpty()) QSKIP("fixture not configured");
        ag_player* mainPlayer = nullptr;
        const ag_player_config config{AG_AUDIO_BACKEND_NULL, 0U};
        QCOMPARE(ag_player_create_with_config(&config, &mainPlayer), AG_OK);
        QCOMPARE(ag_player_load(mainPlayer, fixture.toUtf8().constData()), AG_OK);
        QCOMPARE(ag_player_play(mainPlayer), AG_OK);
        LibraryModel library;
        PlaybackController mainPlayback(mainPlayer, &library);
        QTRY_COMPARE_WITH_TIMEOUT(mainPlayback.state(),
                                  PlaybackController::Playing, 2'000);
        AudioEditorController editor(AG_AUDIO_BACKEND_NULL);
        editor.setMainPlaybackController(&mainPlayback);
        QVERIFY(editor.openFile(QUrl::fromLocalFile(fixture)));
        QVERIFY(editor.playPause());
        QTRY_COMPARE_WITH_TIMEOUT(mainPlayback.state(),
                                  PlaybackController::Stopped, 2'000);
        QVERIFY(editor.stopPlayback());
        ag_player_destroy(mainPlayer);
    }

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
            "editor.export"};
        QCOMPARE(controller.actions()->ids(), expected);
    }

    void recordingDocumentAndClearUseAnUntitledLifecycle()
    {
        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(controller.createRecordingDocument(48'000, 2));
        QVERIFY(controller.hasDocument());
        QVERIFY(controller.filePath().isEmpty());
        QVERIFY(controller.totalFrames() > 0);
        QVERIFY(controller.clearDocument());
        QVERIFY(!controller.hasDocument());
        QCOMPARE(controller.state(), EditorSessionState::Empty);
        QCOMPARE(controller.totalFrames(), qint64{0});
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
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(output), 10'000);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.modified(), 10'000);
        QVERIFY(controller.playPause());
        QTRY_VERIFY_WITH_TIMEOUT(controller.playing(), 2'000);
        QVERIFY(controller.stopPlayback());
    }

    void rejectsInvalidIndependentExportParameters()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(controller.createUntitledDocument(48'000, 2, 96'000));
        const QUrl output = QUrl::fromLocalFile(
            directory.filePath(QStringLiteral("invalid.wav")));
        QVERIFY(!controller.exportTo(output, false, QStringLiteral("pcm_s24le"),
                                     1, 2, 0, true, false, 80));
        QVERIFY(!controller.errorMessage().isEmpty());
    }

    void exposesOnlyAvailableExportFormats()
    {
        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        const QVariantList formats = controller.exportFormats();
        QVERIFY(!formats.isEmpty());
        for (const QVariant& value : formats) {
            const QVariantMap format = value.toMap();
            QVERIFY(!format.value(QStringLiteral("text")).toString().isEmpty());
            QVERIFY(!format.value(QStringLiteral("extension")).toString().isEmpty());
            const QByteArray codec = format.value(QStringLiteral("codec"))
                                         .toString().toLatin1();
            QVERIFY(!codec.isEmpty());
            QCOMPARE(ag_encoder_available(codec.constData()), 1);
        }
    }

    void independentExportAppliesRequestedSampleRateAndChannels()
    {
        const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_EDITOR_FIXTURE"));
        if (fixture.isEmpty()) QSKIP("fixture not configured");
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(controller.openFile(QUrl::fromLocalFile(fixture)));
        const QString output = directory.filePath(QStringLiteral("export.flac"));
        QVERIFY2(controller.exportTo(QUrl::fromLocalFile(output), false,
                                     QStringLiteral("flac"), 48'000, 1,
                                     0, true, false, 80),
                 qPrintable(controller.errorMessage()));
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 10'000);
        QVERIFY2(QFileInfo::exists(output), qPrintable(controller.errorMessage()));
        ag_metadata* metadata = nullptr;
        const QByteArray path = output.toUtf8();
        QCOMPARE(ag_metadata_open(path.constData(), &metadata), AG_OK);
        QVERIFY(metadata != nullptr);
        QCOMPARE(ag_metadata_sample_rate(metadata), 48'000);
        QCOMPARE(ag_metadata_channels(metadata), 1);
        ag_metadata_destroy(metadata);
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
        QTRY_VERIFY_WITH_TIMEOUT(controller.totalFrames() > originalFrames, 10'000);
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
        QTRY_VERIFY_WITH_TIMEOUT(controller.timePitchPreviewActive(), 10'000);
        QCOMPARE(controller.totalFrames(), originalFrames);
        QVERIFY(!controller.modified());
        QVERIFY(controller.playing());
        QVERIFY(controller.stopPlayback());
    }

    void selectionPreviewStartsInsideSelectionAndStopsAtItsEnd()
    {
        const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_EDITOR_FIXTURE"));
        if (fixture.isEmpty()) QSKIP("fixture not configured");
        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(controller.openFile(QUrl::fromLocalFile(fixture)));
        const qint64 startFrame = controller.sampleRate() / 10;
        const qint64 endFrame = controller.sampleRate() * 3 / 10;
        QVERIFY(controller.setSelection(startFrame, endFrame));
        QVERIFY(controller.seekMs(0));
        QVERIFY(controller.playPause());
        QCOMPARE(controller.positionMs(), qint64{100});
        QTRY_VERIFY_WITH_TIMEOUT(!controller.playing(), 2'000);
        QCOMPARE(controller.positionMs(), qint64{300});
    }

    void bpmAnalysisRunsOffTheGuiThreadAgainstTheEditedDocument()
    {
        const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_EDITOR_FIXTURE"));
        if (fixture.isEmpty()) QSKIP("fixture not configured");
        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(controller.openFile(QUrl::fromLocalFile(fixture)));
        QVERIFY(controller.setSelection(0, controller.sampleRate() / 2));
        QElapsedTimer elapsed;
        elapsed.start();
        QVERIFY(controller.detectBpm());
        QVERIFY2(elapsed.elapsed() < 250,
                 "BPM analysis must not block the GUI thread");
        QCOMPARE(controller.state(), EditorSessionState::Processing);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 10'000);
    }

    void markerNavigationUsesTheNearestMarker()
    {
        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(controller.createUntitledDocument(48'000, 2, 192'000));
        QVERIFY(controller.addMarker(QStringLiteral("A"), 48'000));
        QVERIFY(controller.addMarker(QStringLiteral("B"), 144'000));
        QVERIFY(controller.seekMs(2'000));
        QVERIFY(controller.seekPreviousMarker());
        QCOMPARE(controller.positionMs(), qint64{1'000});
        QVERIFY(controller.seekNextMarker());
        QCOMPARE(controller.positionMs(), qint64{3'000});
    }

    void markerManagementIsExposedToTheEditorUi()
    {
        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(controller.createUntitledDocument(48'000, 2, 192'000));
        QVERIFY(controller.addMarker(QStringLiteral("A"), 48'000));
        QVERIFY(controller.renameMarker(0, QStringLiteral("Intro")));
        QCOMPARE(controller.markers().at(0).toMap().value(QStringLiteral("name")),
                 QStringLiteral("Intro"));
        QVERIFY(controller.removeMarker(0));
        QVERIFY(controller.markers().isEmpty());
        QVERIFY(!controller.renameMarker(0, QStringLiteral("Missing")));
        QVERIFY(!controller.removeMarker(0));
    }

    void openingAnotherFileRequiresDiscardConfirmation()
    {
        const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_EDITOR_FIXTURE"));
        if (fixture.isEmpty()) QSKIP("fixture not configured");
        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(controller.createUntitledDocument(48'000, 2, 96'000));
        QVERIFY(controller.setSelection(100, 1'000));
        QVERIFY(controller.triggerAction(QStringLiteral("editor.deleteSelection")));
        const qint64 editedFrames = controller.totalFrames();
        QSignalSpy confirmation(&controller,
            &AudioEditorController::discardConfirmationRequested);
        QVERIFY(!controller.openFile(QUrl::fromLocalFile(fixture)));
        QCOMPARE(confirmation.count(), 1);
        QCOMPARE(controller.totalFrames(), editedFrames);
        QVERIFY(controller.modified());
        QVERIFY(controller.confirmDiscardAndOpen());
        QVERIFY(!controller.modified());
        QCOMPARE(controller.fileName(), QFileInfo(fixture).fileName());
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

    void recordingDeviceStartupFailureReturnsAsynchronously()
    {
        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QElapsedTimer elapsed;
        elapsed.start();
        QVERIFY(controller.startRecording(
            QUrl::fromLocalFile(directory.filePath(QStringLiteral("capture.wav"))),
            QStringLiteral("capture:missing-device"), 48'000, 2,
            false, false));
        QVERIFY2(elapsed.elapsed() < 250,
                 "recording device startup must not block the GUI thread");
        QCOMPARE(controller.state(), EditorSessionState::Processing);
        QTRY_VERIFY_WITH_TIMEOUT(
            controller.state() != EditorSessionState::Processing, 5'000);
        QVERIFY(!controller.errorMessage().isEmpty());
    }
};

QTEST_GUILESS_MAIN(AudioEditorControllerTest)

#include "audio_editor_controller_test.moc"
