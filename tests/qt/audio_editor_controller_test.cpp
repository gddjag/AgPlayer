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
        QVERIFY(controller.actionEnabled(QStringLiteral("editor.undo")));
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
