#include "audio_editor/audio_editor_controller.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
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

    void mergeActionIsDisabledForSelectionCoveringAVisibilityGap()
    {
        AudioEditorController controller;
        QVERIFY(controller.createUntitledDocument(48'000, 2, 48'000));
        QVERIFY(controller.splitEvent(1, 24'000));
        QVERIFY(controller.moveEvent(2, 30'000));
        QVERIFY(controller.setSelection(0, 54'000));
        QVERIFY(!controller.actionEnabled(QStringLiteral("editor.merge")));
        QVERIFY(!controller.triggerAction(QStringLiteral("editor.merge")));
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

    void tailRemovalClampsViewportAndProjectSave_data()
    {
        QTest::addColumn<QString>("actionId");
        QTest::newRow("delete") << QStringLiteral("editor.deleteSelection");
        QTest::newRow("cut") << QStringLiteral("editor.cut");
    }

    void tailRemovalClampsViewportAndProjectSave()
    {
        QFETCH(QString, actionId);
        const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_EDITOR_FIXTURE"));
        if (fixture.isEmpty()) QSKIP("fixture not configured");
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString source = temporary.filePath(QStringLiteral("source.wav"));
        QVERIFY(QFile::copy(fixture, source));
        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(controller.openFile(QUrl::fromLocalFile(source)));
        const qint64 originalFrames = controller.totalFrames();
        const qint64 splitFrame = originalFrames / 2;
        QVERIFY(splitFrame > 0);
        QVERIFY(controller.splitEvent(1, splitFrame));
        QVERIFY(controller.viewport()->setVisibleRange(splitFrame, originalFrames));
        QVERIFY(controller.seekFrame(originalFrames));
        QVERIFY(controller.setSelection(splitFrame, originalFrames));
        const QVariantList sourcePeaks = controller.channelPeaks();
        QSignalSpy stateChanges(&controller, &AudioEditorController::stateChanged);

        QVERIFY(controller.triggerAction(actionId));
        QCOMPARE(controller.totalFrames(), splitFrame);
        QCOMPARE(controller.viewport()->documentFrames(), splitFrame);
        QVERIFY(controller.viewport()->visibleEndFrame() <= splitFrame);
        QCOMPARE(controller.playheadFrame(), splitFrame);
        QCOMPARE(controller.positionMs(), controller.durationMs());
        QCOMPARE(controller.channelPeaks(), sourcePeaks);
        QCOMPARE(stateChanges.count(), 0);

        const QString project = temporary.filePath(actionId.endsWith(
            QStringLiteral("cut")) ? QStringLiteral("cut.agproj")
                                   : QStringLiteral("delete.agproj"));
        QVERIFY2(controller.saveProjectAs(QUrl::fromLocalFile(project)),
                 qPrintable(controller.errorMessage()));
        QCOMPARE(controller.state(), EditorSessionState::Ready);
        QCOMPARE(stateChanges.count(), 0);
        QCOMPARE(controller.channelPeaks(), sourcePeaks);
    }

    void undoThatShrinksTimelineClampsPlayheadAndViewportBeforeSave()
    {
        const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_EDITOR_FIXTURE"));
        if (fixture.isEmpty()) QSKIP("fixture not configured");
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString source = temporary.filePath(QStringLiteral("source.wav"));
        const QString project = temporary.filePath(QStringLiteral("undo-shrink.agproj"));
        QVERIFY(QFile::copy(fixture, source));

        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(controller.openFile(QUrl::fromLocalFile(source)));
        const qint64 originalFrames = controller.totalFrames();
        QVERIFY(controller.moveEvent(1, originalFrames / 2));
        const qint64 expandedFrames = controller.totalFrames();
        QVERIFY(expandedFrames > originalFrames);
        QVERIFY(controller.viewport()->setVisibleRange(originalFrames, expandedFrames));
        QVERIFY(controller.seekFrame(expandedFrames));

        QVERIFY(controller.undo());
        QCOMPARE(controller.totalFrames(), originalFrames);
        QCOMPARE(controller.viewport()->documentFrames(), originalFrames);
        QVERIFY(controller.viewport()->visibleEndFrame() <= originalFrames);
        QCOMPARE(controller.playheadFrame(), originalFrames);
        QCOMPARE(controller.positionMs(), controller.durationMs());
        QVERIFY2(controller.saveProjectAs(QUrl::fromLocalFile(project)),
                 qPrintable(controller.errorMessage()));
    }

    void clearingTimelinePublishesZeroViewport()
    {
        const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_EDITOR_FIXTURE"));
        if (fixture.isEmpty()) QSKIP("fixture not configured");
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString source = temporary.filePath(QStringLiteral("source.wav"));
        QVERIFY(QFile::copy(fixture, source));
        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(controller.openFile(QUrl::fromLocalFile(source)));
        QVERIFY(controller.setSelection(0, controller.totalFrames()));
        QVERIFY(controller.triggerAction(QStringLiteral("editor.deleteSelection")));
        QCOMPARE(controller.totalFrames(), qint64{0});
        QCOMPARE(controller.viewport()->documentFrames(), qint64{0});
        QCOMPARE(controller.viewport()->visibleStartFrame(), qint64{0});
        QCOMPARE(controller.viewport()->visibleEndFrame(), qint64{0});

        const QString project = temporary.filePath(QStringLiteral("empty.agproj"));
        QVERIFY2(controller.saveProjectAs(QUrl::fromLocalFile(project)),
                 qPrintable(controller.errorMessage()));
        AudioEditorController restored(AG_AUDIO_BACKEND_NULL);
        QVERIFY2(restored.openProject(QUrl::fromLocalFile(project)),
                 qPrintable(restored.errorMessage()));
        QCOMPARE(restored.totalFrames(), qint64{0});
        QCOMPARE(restored.viewport()->visibleStartFrame(), qint64{0});
        QCOMPARE(restored.viewport()->visibleEndFrame(), qint64{0});
    }

    void undoRedoActionsRouteThroughDocumentHistory()
    {
        AudioEditorController controller;
        QVERIFY(controller.createUntitledDocument(48'000, 2, 1'000));
        QCOMPARE(controller.action(QStringLiteral("editor.undo"))->shortcut,
                 QStringLiteral("Ctrl+Z"));
        QCOMPARE(controller.action(QStringLiteral("editor.redo"))->shortcut,
                 QStringLiteral("Ctrl+Y"));
        QVERIFY(!controller.actionEnabled(QStringLiteral("editor.undo")));
        QVERIFY(controller.trimEvent(1, 100, 800, 100));
        QCOMPARE(controller.totalFrames(), qint64{800});
        QVERIFY(controller.modified());
        QVERIFY(controller.actionEnabled(QStringLiteral("editor.undo")));
        QVERIFY(!controller.actionEnabled(QStringLiteral("editor.redo")));

        QVERIFY(controller.triggerAction(QStringLiteral("editor.undo")));
        QCOMPARE(controller.totalFrames(), qint64{1'000});
        QVERIFY(controller.modified());
        QVERIFY(controller.actionEnabled(QStringLiteral("editor.redo")));
        QVERIFY(controller.triggerAction(QStringLiteral("editor.redo")));
        QCOMPARE(controller.totalFrames(), qint64{800});
        QVERIFY(controller.modified());
        QVERIFY(controller.actionEnabled(QStringLiteral("editor.undo")));
    }

    void undoToSavedTimelineStaysDirtyWhenPersistedViewWasClamped()
    {
        const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_EDITOR_FIXTURE"));
        if (fixture.isEmpty()) QSKIP("fixture not configured");
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString source = temporary.filePath(QStringLiteral("source.wav"));
        const QString project = temporary.filePath(QStringLiteral("clamped-state.agproj"));
        QVERIFY(QFile::copy(fixture, source));

        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(controller.openFile(QUrl::fromLocalFile(source)));
        const qint64 total = controller.totalFrames();
        const qint64 split = total / 2;
        QVERIFY(controller.splitEvent(1, split));
        QVERIFY(controller.viewport()->setVisibleRange(split, total));
        QVERIFY(controller.seekFrame(total));
        QVERIFY(controller.saveProjectAs(QUrl::fromLocalFile(project)));
        QVERIFY(!controller.modified());

        QVERIFY(controller.setSelection(split, total));
        QVERIFY(controller.triggerAction(QStringLiteral("editor.deleteSelection")));
        QCOMPARE(controller.playheadFrame(), split);
        QCOMPARE(controller.viewport()->visibleEndFrame(), split);
        QVERIFY(controller.undo());
        QCOMPARE(controller.totalFrames(), total);
        QCOMPARE(controller.playheadFrame(), split);
        QVERIFY(controller.viewport()->visibleEndFrame() < total);
        QVERIFY(controller.modified());
    }

    void saveAndOpenProjectRoundTripsControllerStateWithoutRendering()
    {
        const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_EDITOR_FIXTURE"));
        if (fixture.isEmpty()) QSKIP("fixture not configured");
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString sourceDirectory = temporary.filePath(QString::fromUtf8("音频"));
        QVERIFY(QDir{}.mkpath(sourceDirectory));
        const QString source = QDir(sourceDirectory).filePath(QString::fromUtf8("源.wav"));
        QVERIFY2(QFile::copy(fixture, source), qPrintable(source));
        const QString project = temporary.filePath(QString::fromUtf8("会话.agproj"));

        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY2(controller.openFile(QUrl::fromLocalFile(source)),
                 qPrintable(controller.errorMessage()));
        controller.viewport()->setViewportWidth(512.0);
        const qint64 visibleEnd = qMin<qint64>(controller.totalFrames(), 10'000);
        QVERIFY(controller.viewport()->setVisibleRange(100, visibleEnd));
        QVERIFY(controller.seekFrame(123));
        const QVariantList sourcePeaks = controller.channelPeaks();
        QSignalSpy stateChanges(&controller, &AudioEditorController::stateChanged);
        QSignalSpy waveformChanges(&controller, &AudioEditorController::waveformChanged);

        QVERIFY2(controller.saveProjectAs(QUrl::fromLocalFile(project)),
                 qPrintable(controller.errorMessage()));
        QCOMPARE(controller.state(), EditorSessionState::Ready);
        QCOMPARE(stateChanges.count(), 0);
        QCOMPARE(waveformChanges.count(), 0);
        QCOMPARE(controller.channelPeaks(), sourcePeaks);
        QCOMPARE(controller.projectPath(), QFileInfo(project).absoluteFilePath());
        QVERIFY(!controller.modified());
        AudioEditorController loaded(AG_AUDIO_BACKEND_NULL);
        QVERIFY2(loaded.openProject(QUrl::fromLocalFile(project)),
                 qPrintable(loaded.errorMessage()));
        QCOMPARE(loaded.projectPath(), QFileInfo(project).absoluteFilePath());
        QCOMPARE(loaded.playheadFrame(), qint64{123});
        QCOMPARE(loaded.viewport()->visibleStartFrame(), qint64{100});
        QCOMPARE(loaded.viewport()->visibleEndFrame(), visibleEnd);
        QCOMPARE(loaded.totalFrames(), controller.totalFrames());
        QVERIFY(loaded.projectIssues().isEmpty());
        QVERIFY(!loaded.modified());
        QVERIFY(!loaded.actionEnabled(QStringLiteral("editor.undo")));
    }

    void saveActionRequestsProjectPathAndThenUsesIt()
    {
        const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_EDITOR_FIXTURE"));
        if (fixture.isEmpty()) QSKIP("fixture not configured");
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString sourceDirectory = temporary.filePath(QString::fromUtf8("项目源"));
        QVERIFY(QDir{}.mkpath(sourceDirectory));
        const QString source = QDir(sourceDirectory).filePath(QStringLiteral("source.wav"));
        QVERIFY(QFile::copy(fixture, source));
        const QString project = temporary.filePath(QStringLiteral("untitled.agproj"));

        AudioEditorController controller;
        QVERIFY(controller.createUntitledDocument(48'000, 2, 1'000));
        QSignalSpy requested(&controller,
                             &AudioEditorController::saveProjectAsRequested);
        QVERIFY(!controller.triggerAction(QStringLiteral("editor.save")));
        QCOMPARE(requested.count(), 1);
        QVERIFY(!controller.saveProjectAs(QUrl::fromLocalFile(project)));
        QVERIFY(!QFileInfo::exists(project));

        QVERIFY(controller.openFile(QUrl::fromLocalFile(source)));
        QSignalSpy stateChanges(&controller, &AudioEditorController::stateChanged);
        QVERIFY(controller.saveProjectAs(QUrl::fromLocalFile(project)));
        QCOMPARE(controller.state(), EditorSessionState::Ready);
        QCOMPARE(stateChanges.count(), 0);
        QFile projectFile(project);
        QVERIFY(projectFile.open(QIODevice::ReadOnly));
        const QJsonObject root = QJsonDocument::fromJson(projectFile.readAll()).object();
        projectFile.close();
        QCOMPARE(root.value(QStringLiteral("schemaVersion")).toInt(), 1);
        QVERIFY(controller.trimEvent(1, 10, 900, 0));
        QVERIFY2(controller.triggerAction(QStringLiteral("editor.save")),
                 qPrintable(controller.errorMessage()));
        QVERIFY(!controller.modified());
    }

    void openProjectReportsOfflineSourceAndRelinksIt()
    {
        const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_EDITOR_FIXTURE"));
        if (fixture.isEmpty()) QSKIP("fixture not configured");
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString source = temporary.filePath(QString::fromUtf8("离线源.wav"));
        QVERIFY(QFile::copy(fixture, source));
        const QString project = temporary.filePath(QStringLiteral("offline.agproj"));

        AudioEditorController original(AG_AUDIO_BACKEND_NULL);
        QVERIFY(original.openFile(QUrl::fromLocalFile(source)));
        QVERIFY(original.saveProjectAs(QUrl::fromLocalFile(project)));
        QVERIFY(QFile::remove(source));

        AudioEditorController loaded(AG_AUDIO_BACKEND_NULL);
        QVERIFY2(loaded.openProject(QUrl::fromLocalFile(project)),
                 qPrintable(loaded.errorMessage()));
        QCOMPARE(loaded.projectIssues().size(), 1);
        const QVariantMap issue = loaded.projectIssues().constFirst().toMap();
        QCOMPARE(issue.value(QStringLiteral("kind")).toString(),
                 QStringLiteral("missing"));
        const quint64 sourceId = issue.value(QStringLiteral("sourceId")).toULongLong();
        QSignalSpy projectChanges(&loaded, &AudioEditorController::projectChanged);
        QVERIFY(loaded.relinkProjectSource(sourceId, QUrl::fromLocalFile(fixture)));
        QVERIFY(loaded.projectIssues().isEmpty());
        QVERIFY(loaded.modified());
        QCOMPARE(projectChanges.count(), 1);
    }

    void offlineProjectSourcesBlockSourceReadingOperations_data()
    {
        QTest::addColumn<bool>("removeSource");
        QTest::addColumn<QString>("issueKind");
        QTest::newRow("missing") << true << QStringLiteral("missing");
        QTest::newRow("identity-mismatch") << false
                                            << QStringLiteral("identityMismatch");
    }

    void offlineProjectSourcesBlockSourceReadingOperations()
    {
        QFETCH(bool, removeSource);
        QFETCH(QString, issueKind);
        const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_EDITOR_FIXTURE"));
        if (fixture.isEmpty()) QSKIP("fixture not configured");
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString source = temporary.filePath(QStringLiteral("original.wav"));
        const QString replacement = temporary.filePath(QStringLiteral("replacement.wav"));
        const QString project = temporary.filePath(QStringLiteral("offline.agproj"));
        const QString blockedExport = temporary.filePath(QStringLiteral("blocked-export.wav"));
        const QString blockedSaveAs = temporary.filePath(QStringLiteral("blocked-save-as.wav"));
        const QString relinkedExport = temporary.filePath(QStringLiteral("relinked-export.wav"));
        QVERIFY(QFile::copy(fixture, source));
        QVERIFY(QFile::copy(fixture, replacement));

        AudioEditorController original(AG_AUDIO_BACKEND_NULL);
        QVERIFY2(original.openFile(QUrl::fromLocalFile(source)),
                 qPrintable(original.errorMessage()));
        const int expectedSampleRate = original.sampleRate();
        const int expectedChannels = original.channels();
        QVERIFY(original.saveProjectAs(QUrl::fromLocalFile(project)));
        if (removeSource) {
            QVERIFY(QFile::remove(source));
        } else {
            QFile changed(source);
            QVERIFY(changed.open(QIODevice::Append));
            QVERIFY(changed.write("identity mismatch") > 0);
        }

        AudioEditorController loaded(AG_AUDIO_BACKEND_NULL);
        QVERIFY2(loaded.openProject(QUrl::fromLocalFile(project)),
                 qPrintable(loaded.errorMessage()));
        QCOMPARE(loaded.projectIssues().size(), 1);
        const QVariantMap issue = loaded.projectIssues().constFirst().toMap();
        QCOMPARE(issue.value(QStringLiteral("kind")).toString(), issueKind);
        const quint64 sourceId = issue.value(QStringLiteral("sourceId")).toULongLong();
        QCOMPARE(loaded.filePath(), QFileInfo(source).absoluteFilePath());
        QVERIFY(loaded.channelPeaks().isEmpty());

        QSignalSpy exportRequested(&loaded, &AudioEditorController::exportRequested);
        QVERIFY(!loaded.actionEnabled(QStringLiteral("editor.export")));
        QVERIFY(!loaded.triggerAction(QStringLiteral("editor.export")));
        QCOMPARE(exportRequested.count(), 0);
        QVERIFY(!loaded.playPause());
        QVERIFY(loaded.errorMessage().contains(QStringLiteral("重新链接")));
        QVERIFY(!loaded.detectBpm());
        QVERIFY(loaded.errorMessage().contains(QStringLiteral("重新链接")));
        QVERIFY(!loaded.exportTo(QUrl::fromLocalFile(blockedExport)));
        QVERIFY(!QFileInfo::exists(blockedExport));
        QVERIFY(!loaded.saveAs(QUrl::fromLocalFile(blockedSaveAs)));
        QVERIFY(!QFileInfo::exists(blockedSaveAs));

        QVERIFY2(loaded.relinkProjectSource(sourceId, QUrl::fromLocalFile(replacement)),
                 qPrintable(loaded.errorMessage()));
        QVERIFY(loaded.projectIssues().isEmpty());
        QCOMPARE(loaded.filePath(), QFileInfo(replacement).absoluteFilePath());
        QCOMPARE(loaded.sampleRate(), expectedSampleRate);
        QCOMPARE(loaded.channels(), expectedChannels);
        QCOMPARE(loaded.fileName(), QFileInfo(replacement).fileName());
        QVERIFY(loaded.channelPeaks().isEmpty());
        loaded.viewport()->setViewportWidth(512.0);
        QCoreApplication::processEvents();
        QVERIFY(loaded.viewportChannelPeaks().isEmpty());

        QVERIFY2(loaded.exportTo(QUrl::fromLocalFile(relinkedExport), false,
                                 {}, 0, 0, 0, true, true, 80),
                 qPrintable(loaded.errorMessage()));
        QTRY_COMPARE_WITH_TIMEOUT(loaded.state(), EditorSessionState::Ready, 10'000);
        QVERIFY(QFileInfo::exists(relinkedExport));
    }

    void failedProjectLoadLeavesActiveStateUnchanged()
    {
        AudioEditorController controller;
        QVERIFY(controller.createUntitledDocument(48'000, 2, 1'000));
        QVERIFY(controller.trimEvent(1, 100, 800, 100));
        QVERIFY(controller.seekFrame(321));
        const qint64 framesBefore = controller.totalFrames();
        const qint64 playheadBefore = controller.playheadFrame();
        const bool modifiedBefore = controller.modified();

        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString malformed = temporary.filePath(QStringLiteral("broken.agproj"));
        QFile file(malformed);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("{not-json"), qint64{9});
        file.close();

        QVERIFY(!controller.openProject(QUrl::fromLocalFile(malformed)));
        QCOMPARE(controller.totalFrames(), framesBefore);
        QCOMPARE(controller.playheadFrame(), playheadBefore);
        QCOMPARE(controller.modified(), modifiedBefore);
        QVERIFY(controller.actionEnabled(QStringLiteral("editor.undo")));
    }

    void explicitInvalidProjectSaveAsNeverFallsBackToCurrentProject()
    {
        const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_EDITOR_FIXTURE"));
        if (fixture.isEmpty()) QSKIP("fixture not configured");
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString source = temporary.filePath(QStringLiteral("source.wav"));
        QVERIFY(QFile::copy(fixture, source));
        const QString project = temporary.filePath(QStringLiteral("current.agproj"));
        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(controller.openFile(QUrl::fromLocalFile(source)));
        QVERIFY(controller.saveProjectAs(QUrl::fromLocalFile(project)));
        QFile originalFile(project);
        QVERIFY(originalFile.open(QIODevice::ReadOnly));
        const QByteArray original = originalFile.readAll();
        originalFile.close();

        QVERIFY(!controller.saveProjectAs(
            QUrl(QStringLiteral("https://example.invalid/project.agproj"))));
        QFile unchangedFile(project);
        QVERIFY(unchangedFile.open(QIODevice::ReadOnly));
        QCOMPARE(unchangedFile.readAll(), original);
        QCOMPARE(controller.projectPath(), QFileInfo(project).absoluteFilePath());
    }

    void legacyAudioSaveAsStillWritesAudioNotProjectJson()
    {
        const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_EDITOR_FIXTURE"));
        if (fixture.isEmpty()) QSKIP("fixture not configured");
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString output = temporary.filePath(QStringLiteral("saved.wav"));
        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(controller.openFile(QUrl::fromLocalFile(fixture)));
        QVERIFY(controller.saveAs(QUrl::fromLocalFile(output)));
        QTRY_COMPARE_WITH_TIMEOUT(controller.state(), EditorSessionState::Ready,
                                  10'000);
        QFile file(output);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.read(4), QByteArray("RIFF", 4));
        QVERIFY(controller.projectPath().isEmpty());
    }

    void modifiedProjectOpenUsesDiscardConfirmationBeforeReplacement()
    {
        const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_EDITOR_FIXTURE"));
        if (fixture.isEmpty()) QSKIP("fixture not configured");
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString source = temporary.filePath(QStringLiteral("source.wav"));
        QVERIFY(QFile::copy(fixture, source));
        const QString project = temporary.filePath(QStringLiteral("target.agproj"));
        AudioEditorController projectMaker(AG_AUDIO_BACKEND_NULL);
        QVERIFY(projectMaker.openFile(QUrl::fromLocalFile(source)));
        QVERIFY(projectMaker.saveProjectAs(QUrl::fromLocalFile(project)));

        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(controller.openFile(QUrl::fromLocalFile(source)));
        QVERIFY(controller.trimEvent(1, 10, controller.totalFrames() - 10, 0));
        const qint64 changedFrames = controller.totalFrames();
        QSignalSpy requested(&controller,
                             &AudioEditorController::discardConfirmationRequested);
        QVERIFY(!controller.openProject(QUrl::fromLocalFile(project)));
        QCOMPARE(requested.count(), 1);
        QCOMPARE(controller.totalFrames(), changedFrames);
        QVERIFY(controller.modified());

        QVERIFY(controller.confirmDiscardAndOpen());
        QCOMPARE(controller.projectPath(), QFileInfo(project).absoluteFilePath());
        QVERIFY(!controller.modified());
    }

    void modifiedClearUsesDiscardConfirmationBeforeReplacement()
    {
        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(controller.createUntitledDocument(48'000, 2, 1'000));
        QVERIFY(controller.trimEvent(1, 100, 900, 0));
        const qint64 changedFrames = controller.totalFrames();
        const qint64 playhead = controller.playheadFrame();
        QSignalSpy requested(&controller,
                             &AudioEditorController::discardConfirmationRequested);

        QVERIFY(!controller.clearDocument());
        QCOMPARE(requested.count(), 1);
        QVERIFY(controller.hasDocument());
        QCOMPARE(controller.totalFrames(), changedFrames);
        QCOMPARE(controller.playheadFrame(), playhead);
        QVERIFY(controller.modified());

        controller.cancelDiscardAndOpen();
        QVERIFY(controller.hasDocument());
        QVERIFY(!controller.clearDocument());
        QCOMPARE(requested.count(), 2);
        QVERIFY(controller.confirmDiscardAndOpen());
        QVERIFY(!controller.hasDocument());
        QCOMPARE(controller.totalFrames(), qint64{0});
        QVERIFY(!controller.modified());
    }

    void persistedEditorStateTracksDirtyAgainstTheSavepoint()
    {
        const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_EDITOR_FIXTURE"));
        if (fixture.isEmpty()) QSKIP("fixture not configured");
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString source = temporary.filePath(QStringLiteral("source.wav"));
        const QString project = temporary.filePath(QStringLiteral("state.agproj"));
        QVERIFY(QFile::copy(fixture, source));

        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(controller.openFile(QUrl::fromLocalFile(source)));
        controller.viewport()->setViewportWidth(512.0);
        QVERIFY(controller.saveProjectAs(QUrl::fromLocalFile(project)));
        QVERIFY(!controller.modified());

        QVERIFY(controller.setSelection(10, 100));
        QVERIFY(controller.modified());
        QVERIFY(controller.save());
        QVERIFY(!controller.modified());
        QVERIFY(controller.clearSelection());
        QVERIFY(controller.modified());
        QVERIFY(controller.setSelection(10, 100));
        QVERIFY(!controller.modified());

        QVERIFY(controller.seekFrame(123));
        QVERIFY(controller.modified());
        QVERIFY(controller.save());
        QVERIFY(!controller.modified());
        QVERIFY(controller.seekFrame(456));
        QVERIFY(controller.modified());
        QVERIFY(controller.seekFrame(123));
        QVERIFY(!controller.modified());

        const qint64 firstEnd = qMin<qint64>(controller.totalFrames(), 10'000);
        const qint64 secondEnd = qMin<qint64>(controller.totalFrames(), 20'000);
        QVERIFY(firstEnd > 100);
        QVERIFY(secondEnd > firstEnd);
        QVERIFY(controller.viewport()->setVisibleRange(100, firstEnd));
        QVERIFY(controller.modified());
        QVERIFY(controller.save());
        QVERIFY(!controller.modified());
        QVERIFY(controller.viewport()->setVisibleRange(200, secondEnd));
        QVERIFY(controller.modified());
        QVERIFY(controller.viewport()->setVisibleRange(100, firstEnd));
        QVERIFY(!controller.modified());
    }

    void playbackProgressAndStopUsePersistedPlayheadDirtyTracking()
    {
        const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_EDITOR_FIXTURE"));
        if (fixture.isEmpty()) QSKIP("fixture not configured");
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString source = temporary.filePath(QStringLiteral("source.wav"));
        const QString project = temporary.filePath(QStringLiteral("playhead.agproj"));
        QVERIFY(QFile::copy(fixture, source));

        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(controller.openFile(QUrl::fromLocalFile(source)));
        QVERIFY(controller.seekFrame(123));
        QVERIFY(controller.saveProjectAs(QUrl::fromLocalFile(project)));
        QVERIFY(!controller.modified());

        QVERIFY2(controller.playPause(), qPrintable(controller.errorMessage()));
        QTRY_VERIFY_WITH_TIMEOUT(controller.playheadFrame() != 123, 5'000);
        QVERIFY(controller.modified());

        QVERIFY(controller.stopPlayback());
        QCOMPARE(controller.playheadFrame(), qint64{0});
        QVERIFY(controller.modified());
    }

    void offlineGateTracksOnlySourcesReferencedByTheCurrentTimeline()
    {
        const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_EDITOR_FIXTURE"));
        if (fixture.isEmpty()) QSKIP("fixture not configured");
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString firstSource = temporary.filePath(QStringLiteral("first.wav"));
        const QString secondSource = temporary.filePath(QStringLiteral("second.wav"));
        const QString project = temporary.filePath(QStringLiteral("two-sources.agproj"));
        QVERIFY(QFile::copy(fixture, firstSource));
        QVERIFY(QFile::copy(fixture, secondSource));

        AudioEditorController maker(AG_AUDIO_BACKEND_NULL);
        QVERIFY(maker.openFile(QUrl::fromLocalFile(firstSource)));
        const qint64 total = maker.totalFrames();
        const qint64 split = total / 2;
        QVERIFY(split > 0);
        QVERIFY(maker.splitEvent(1, split));
        QVERIFY(maker.saveProjectAs(QUrl::fromLocalFile(project)));

        QFile projectFile(project);
        QVERIFY(projectFile.open(QIODevice::ReadOnly));
        QJsonObject root = QJsonDocument::fromJson(projectFile.readAll()).object();
        projectFile.close();
        QJsonArray sources = root.value(QStringLiteral("sources")).toArray();
        QCOMPARE(sources.size(), 1);
        QJsonObject secondRecord = sources.at(0).toObject();
        secondRecord.insert(QStringLiteral("sourceId"), QStringLiteral("2"));
        secondRecord.insert(QStringLiteral("pathKind"), QStringLiteral("relative"));
        secondRecord.insert(QStringLiteral("path"), QStringLiteral("second.wav"));
        const QFileInfo secondInfo(secondSource);
        secondRecord.insert(QStringLiteral("fileSize"),
                            QString::number(secondInfo.size()));
        secondRecord.insert(QStringLiteral("lastModifiedUtcMs"),
                            QString::number(secondInfo.lastModified().toUTC().toMSecsSinceEpoch()));
        sources.append(secondRecord);
        root.insert(QStringLiteral("sources"), sources);
        QJsonArray events = root.value(QStringLiteral("events")).toArray();
        QCOMPARE(events.size(), 2);
        QJsonObject secondEvent = events.at(1).toObject();
        secondEvent.insert(QStringLiteral("sourceId"), QStringLiteral("2"));
        events.replace(1, secondEvent);
        root.insert(QStringLiteral("events"), events);
        QVERIFY(projectFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QVERIFY(projectFile.write(QJsonDocument(root).toJson()) > 0);
        projectFile.close();
        QVERIFY(QFile::remove(secondSource));

        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY2(controller.openProject(QUrl::fromLocalFile(project)),
                 qPrintable(controller.errorMessage()));
        QCOMPARE(controller.projectIssues().size(), 1);
        QVERIFY(!controller.actionEnabled(QStringLiteral("editor.export")));

        QVERIFY(controller.setSelection(split, total));
        QVERIFY(controller.triggerAction(QStringLiteral("editor.deleteSelection")));
        QVERIFY(controller.projectIssues().isEmpty());
        QVERIFY(controller.actionEnabled(QStringLiteral("editor.export")));
        QVERIFY2(controller.save(), qPrintable(controller.errorMessage()));
        QVERIFY(!controller.modified());

        QVERIFY(controller.undo());
        QCOMPARE(controller.projectIssues().size(), 1);
        QCOMPARE(controller.projectIssues().constFirst().toMap()
                     .value(QStringLiteral("sourceId")).toULongLong(), quint64{2});
        QVERIFY(!controller.actionEnabled(QStringLiteral("editor.export")));
    }

    void redoBackToSavedHistoryPointClearsDirtyState()
    {
        const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_EDITOR_FIXTURE"));
        if (fixture.isEmpty()) QSKIP("fixture not configured");
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString source = temporary.filePath(QStringLiteral("source.wav"));
        QVERIFY(QFile::copy(fixture, source));
        const QString project = temporary.filePath(QStringLiteral("saved.agproj"));
        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(controller.openFile(QUrl::fromLocalFile(source)));
        QVERIFY(controller.trimEvent(1, 10, controller.totalFrames() - 10, 0));
        QVERIFY(controller.saveProjectAs(QUrl::fromLocalFile(project)));
        QVERIFY(!controller.modified());

        QVERIFY(controller.undo());
        QVERIFY(controller.modified());
        QVERIFY(controller.redo());
        QVERIFY(!controller.modified());
    }

    void nonDefaultExportSettingsRoundTripAndDriveDefaultExportArguments()
    {
        using agplayer::editor::ProjectExportSettings;
        const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_EDITOR_FIXTURE"));
        if (fixture.isEmpty()) QSKIP("fixture not configured");
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString source = temporary.filePath(QStringLiteral("source.wav"));
        QVERIFY(QFile::copy(fixture, source));
        const QString project = temporary.filePath(QStringLiteral("settings.agproj"));
        const QString exported = temporary.filePath(QStringLiteral("default.flac"));

        ProjectExportSettings expected;
        expected.codecName = QStringLiteral("flac");
        expected.sampleRate = 48'000;
        expected.channels = 1;
        expected.bitRate = 192'000;
        expected.keepMetadata = false;
        expected.variableBitRate = false;
        expected.quality = 61;
        expected.outputDirectory = temporary.filePath(QStringLiteral("exports"));

        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(controller.openFile(QUrl::fromLocalFile(source)));
        controller.setProjectExportSettings(expected);
        QVERIFY(controller.saveProjectAs(QUrl::fromLocalFile(project)));

        AudioEditorController loaded(AG_AUDIO_BACKEND_NULL);
        QVERIFY(loaded.openProject(QUrl::fromLocalFile(project)));
        const ProjectExportSettings restored = loaded.projectExportSettings();
        QCOMPARE(restored.codecName, expected.codecName);
        QCOMPARE(restored.sampleRate, expected.sampleRate);
        QCOMPARE(restored.channels, expected.channels);
        QCOMPARE(restored.bitRate, expected.bitRate);
        QCOMPARE(restored.keepMetadata, expected.keepMetadata);
        QCOMPARE(restored.variableBitRate, expected.variableBitRate);
        QCOMPARE(restored.quality, expected.quality);
        QCOMPARE(restored.outputDirectory, expected.outputDirectory);

        QVERIFY(loaded.exportTo(QUrl::fromLocalFile(exported)));
        QCOMPARE(loaded.projectExportSettings().codecName, expected.codecName);
        QCOMPARE(loaded.projectExportSettings().sampleRate, expected.sampleRate);
        QCOMPARE(loaded.projectExportSettings().channels, expected.channels);
        QCOMPARE(loaded.projectExportSettings().bitRate, expected.bitRate);
        QCOMPARE(loaded.projectExportSettings().keepMetadata,
                 expected.keepMetadata);
        QCOMPARE(loaded.projectExportSettings().variableBitRate,
                 expected.variableBitRate);
        QCOMPARE(loaded.projectExportSettings().quality, expected.quality);
        QTRY_COMPARE_WITH_TIMEOUT(loaded.state(), EditorSessionState::Ready, 10'000);
        QVERIFY(QFileInfo::exists(exported));
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
