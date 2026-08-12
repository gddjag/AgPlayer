#include "light_editor_controller.hpp"

#include "../core/bpm_fixture.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QVariantMap>

#include <algorithm>
#include <cmath>

class LightEditorControllerTest final : public QObject {
    Q_OBJECT

private slots:
    void exposesSixTracksAndMixerDefaults();
    void savesAndLoadsProjectWithMixerState();
    void savesProjectsWithAgprojExtension();
    void savesPortablePdfProjectManifest();
    void autosavesChangedProjectsAndRecoversTheAutosave();
    void relinksMissingProjectMediaWithoutLosingClipEdits();
    void loadsClipBeforeBackgroundWaveformAnalysisCompletes();
    void loadsFirstSupportedFileFromDroppedFolder();
    void fillsEmptyTracksFromDroppedFilesInOrder();
    void queuesDroppedFoldersWithoutBlockingTheEditor();
    void movesSnapsLocksAndRestoresClips();
    void trimsToSafeSourceBounds();
    void edgeTrimKeepsTheAudioEventAnchoredToItsAudibleStart();
    void editsSelectedClipsWithClipboardSplitMergeAndCrop();
    void movesClipsAcrossTracksAndDuplicatesInPlace();
    void createsEqualPowerCrossfadeForOverlappingClips();
    void disablesOnlyAutomaticCrossfades();
    void splitsClipsOnTheSameTimelineLane();
    void selectsAndMovesMultipleClipsAsOneEdit();
    void boxSelectsClipsAcrossTimelineLanes();
    void singleClipCommandsKeepSelectionSetCoherent();
    void configuresAndPersistsClipLoopMode();
    void followsProjectBpmForLoopedEvents();
    void rippleDeleteMovesLaterClipsAndUndoRestoresThem();
    void validatesBpmAndTrackSwitches();
    void selectedTrackSpeedControlsStayTrackLocal();
    void selectedTrackSpeedIsAppliedToExportWithoutBatchUnify();
    void clipTimePitchPropertiesPersistInProject();
    void timePitchControlsApplyToTheSelectedClipOnly();
    void analyzesAndUnifiesBpm();
    void exportsTimelineAndFiltersTracks();
    void exportsLoopedClipToRequestedTimelineDuration();
    void exportsLoopRangeAndSelectedClipScopes();
    void exportsAllSupportedFormats();
    void reportsTrackAnalysisErrors();
};

void LightEditorControllerTest::loadsClipBeforeBackgroundWaveformAnalysisCompletes()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString audioPath = temp.filePath(QStringLiteral("async-waveform.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(audioPath, 120, 8));

    LightEditor editor;
    QSignalSpy waveformReady(&editor, &LightEditor::waveformAnalysisCompleted);

    editor.loadFileToTrack(0, QUrl::fromLocalFile(audioPath));

    QCOMPARE(editor.clipCount(), 1);
    QCOMPARE(waveformReady.count(), 0);
    QVERIFY(editor.tracks().at(0).toMap()
                .value(QStringLiteral("peaks")).toList().isEmpty());

    QTRY_COMPARE_WITH_TIMEOUT(waveformReady.count(), 1, 10000);
    const QVariantMap clip = editor.tracks().at(0).toMap();
    QCOMPARE(waveformReady.constFirst().at(0).toString(),
             clip.value(QStringLiteral("clipId")).toString());
    QVERIFY(!clip.value(QStringLiteral("peaks")).toList().isEmpty());
}

void LightEditorControllerTest::queuesDroppedFoldersWithoutBlockingTheEditor()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    for (int index = 0; index < 12; ++index) {
        QVERIFY(agplayer::test::writeClickTrackWav(
            temp.filePath(QStringLiteral("queued-%1.wav").arg(index)),
            120, 1));
    }

    LightEditor editor;
    QSignalSpy queued(&editor, &LightEditor::filesQueued);
    QVERIFY(!editor.importBusy());

    editor.queueFiles({QUrl::fromLocalFile(temp.path())});

    QVERIFY(editor.importBusy());
    QCOMPARE(editor.clipCount(), 0);
    QTRY_VERIFY_WITH_TIMEOUT(!editor.importBusy(), 10000);
    QCOMPARE(editor.clipCount(), 12);
    QVERIFY(queued.count() >= 1);
}

void LightEditorControllerTest::relinksMissingProjectMediaWithoutLosingClipEdits()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString originalPath = temp.filePath(QStringLiteral("original.wav"));
    const QString relocatedPath = temp.filePath(QStringLiteral("relocated.wav"));
    const QString projectPath = temp.filePath(QStringLiteral("missing-media.agproj"));
    QVERIFY(agplayer::test::writeClickTrackWav(originalPath, 120, 2));

    LightEditor editor;
    editor.setSnapEnabled(false);
    QSignalSpy waveformReady(&editor, &LightEditor::waveformAnalysisCompleted);
    editor.loadFileToTrack(0, QUrl::fromLocalFile(originalPath));
    QTRY_COMPARE_WITH_TIMEOUT(waveformReady.count(), 1, 10000);
    QVariantMap clip = editor.tracks().at(0).toMap();
    const QString clipId = clip.value(QStringLiteral("clipId")).toString();
    QVERIFY(editor.moveClipById(clipId, 735));
    QVERIFY(editor.setClipGainById(clipId, 0.42));
    QVERIFY(editor.setClipPitchById(clipId, 3.0, 17.0));
    QVERIFY(editor.saveProject(QUrl::fromLocalFile(projectPath)));
    QVERIFY(QFile::rename(originalPath, relocatedPath));

    LightEditor restored;
    QVERIFY(restored.loadProject(QUrl::fromLocalFile(projectPath)));
    clip = restored.tracks().at(0).toMap();
    QCOMPARE(clip.value(QStringLiteral("available")).toBool(), false);
    QCOMPARE(clip.value(QStringLiteral("timelineStartMs")).toLongLong(), 735);
    QCOMPARE(clip.value(QStringLiteral("gain")).toDouble(), 0.42);

    QSignalSpy restoredWaveformReady(
        &restored, &LightEditor::waveformAnalysisCompleted);
    QVERIFY(restored.relinkClipById(
        clipId, QUrl::fromLocalFile(relocatedPath), true));
    QTRY_VERIFY_WITH_TIMEOUT(restoredWaveformReady.count() >= 1, 10000);
    clip = restored.tracks().at(0).toMap();
    QCOMPARE(clip.value(QStringLiteral("path")).toString(), relocatedPath);
    QCOMPARE(clip.value(QStringLiteral("available")).toBool(), true);
    QCOMPARE(clip.value(QStringLiteral("timelineStartMs")).toLongLong(), 735);
    QCOMPARE(clip.value(QStringLiteral("gain")).toDouble(), 0.42);
    QCOMPARE(clip.value(QStringLiteral("pitchSemitones")).toDouble(), 3.0);
    QCOMPARE(clip.value(QStringLiteral("finePitchCents")).toDouble(), 17.0);
    QVERIFY(!clip.value(QStringLiteral("peaks")).toList().isEmpty());
}

void LightEditorControllerTest::configuresAndPersistsClipLoopMode()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString audioPath = temp.filePath(QStringLiteral("loop.wav"));
    const QString projectPath = temp.filePath(QStringLiteral("loop.agproj"));
    QVERIFY(agplayer::test::writeClickTrackWav(audioPath, 120, 2));

    LightEditor editor;
    editor.loadFileToTrack(0, QUrl::fromLocalFile(audioPath));
    QVariantMap clip = editor.tracks().at(0).toMap();
    const QString clipId = clip.value(QStringLiteral("clipId")).toString();
    QCOMPARE(clip.value(QStringLiteral("loopMode")).toString(),
             QStringLiteral("OneShot"));
    QVERIFY(editor.setClipLoopById(clipId, QStringLiteral("Loop"), 5500));
    QVERIFY(editor.setClipFadeCurvesById(
        clipId, QStringLiteral("Smooth"), QStringLiteral("Linear")));
    clip = editor.tracks().at(0).toMap();
    QCOMPARE(clip.value(QStringLiteral("loopMode")).toString(),
             QStringLiteral("Loop"));
    QCOMPARE(clip.value(QStringLiteral("timelineDurationMs")).toLongLong(),
             5500);
    QVERIFY(editor.saveProject(QUrl::fromLocalFile(projectPath)));

    LightEditor restored;
    QVERIFY(restored.loadProject(QUrl::fromLocalFile(projectPath)));
    const QVariantMap restoredClip = restored.tracks().at(0).toMap();
    QCOMPARE(restoredClip.value(QStringLiteral("loopMode")).toString(),
             QStringLiteral("Loop"));
    QCOMPARE(restoredClip.value(QStringLiteral("timelineDurationMs")).toLongLong(),
             5500);
    QCOMPARE(restoredClip.value(QStringLiteral("fadeInCurve")).toString(),
             QStringLiteral("Smooth"));
    QCOMPARE(restoredClip.value(QStringLiteral("fadeOutCurve")).toString(),
             QStringLiteral("Linear"));
}

void LightEditorControllerTest::followsProjectBpmForLoopedEvents()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString audioPath = temp.filePath(QStringLiteral("follow-bpm.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(audioPath, 90, 10));

    LightEditor editor;
    editor.loadFileToTrack(0, QUrl::fromLocalFile(audioPath));
    const QString clipId = editor.tracks().at(0).toMap()
                               .value(QStringLiteral("clipId")).toString();
    editor.analyzeTrackBpm(0);
    QTRY_VERIFY_WITH_TIMEOUT(!editor.busy(), 30000);

    editor.setTargetBpm(120.0);
    QVERIFY(editor.setClipLoopById(
        clipId, QStringLiteral("FollowProjectBPM"), 5500));
    QVariantMap clip = editor.tracks().at(0).toMap();
    QCOMPARE(clip.value(QStringLiteral("loopMode")).toString(),
             QStringLiteral("FollowProjectBPM"));
    QCOMPARE(clip.value(QStringLiteral("timelineDurationMs")).toLongLong(), 5500);
    QVERIFY(std::abs(clip.value(QStringLiteral("speedRatio")).toDouble()
                     - (120.0 / 90.0)) < 0.02);

    editor.setTargetBpm(100.0);
    clip = editor.tracks().at(0).toMap();
    QVERIFY(std::abs(clip.value(QStringLiteral("speedRatio")).toDouble()
                     - (100.0 / 90.0)) < 0.02);
}

void LightEditorControllerTest::savesProjectsWithAgprojExtension()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString audioPath = temp.filePath(QStringLiteral("project.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(audioPath, 120, 2));

    LightEditor editor;
    editor.loadFileToTrack(0, QUrl::fromLocalFile(audioPath));
    const QString requested = temp.filePath(QStringLiteral("session"));
    const QString projectPath = requested + QStringLiteral(".agproj");
    QVERIFY(editor.saveProject(QUrl::fromLocalFile(requested)));
    QVERIFY(QFileInfo::exists(projectPath));
    QVERIFY(!QFileInfo::exists(requested));

    LightEditor restored;
    QVERIFY(restored.loadProject(QUrl::fromLocalFile(projectPath)));
    QCOMPARE(restored.clipCount(), 1);
}

void LightEditorControllerTest::savesPortablePdfProjectManifest()
{
    // Break caught: a saved .agproj omits the PDF project contract or stores
    // absolute media paths that cannot be reopened after the project moves.
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString projectDirectory = temp.filePath(QStringLiteral("project"));
    QVERIFY(QDir().mkpath(projectDirectory));
    const QString audioPath = QDir(projectDirectory).filePath(
        QStringLiteral("source.wav"));
    const QString projectPath = QDir(projectDirectory).filePath(
        QStringLiteral("portable.agproj"));
    QVERIFY(agplayer::test::writeClickTrackWav(audioPath, 120, 2));

    LightEditor editor;
    editor.loadFileToTrack(0, QUrl::fromLocalFile(audioPath));
    editor.setTargetBpm(132.5);
    editor.setSnapEnabled(false);
    editor.setSnapDivision(6);
    editor.setTimeSignature(QStringLiteral("7/8"));
    editor.setLoopEnabled(true);
    editor.setLoopStartMs(125);
    editor.setLoopEndMs(875);
    QVERIFY(editor.saveProject(QUrl::fromLocalFile(projectPath)));

    QFile manifestFile(projectPath);
    QVERIFY(manifestFile.open(QIODevice::ReadOnly));
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(
        manifestFile.readAll(), &parseError);
    QCOMPARE(parseError.error, QJsonParseError::NoError);
    QVERIFY(document.isObject());
    const QJsonObject root = document.object();
    QCOMPARE(root.value(QStringLiteral("formatVersion")).toInt(), 2);
    QCOMPARE(root.value(QStringLiteral("projectName")).toString(),
             QStringLiteral("portable"));
    QCOMPARE(root.value(QStringLiteral("sampleRate")).toInt(), 48000);
    QCOMPARE(root.value(QStringLiteral("channelLayout")).toString(),
             QStringLiteral("stereo"));
    QCOMPARE(root.value(QStringLiteral("projectBpm")).toDouble(), 132.5);
    QCOMPARE(root.value(QStringLiteral("gridDivision")).toInt(), 6);
    QCOMPARE(root.value(QStringLiteral("snap")).toBool(), false);
    const QJsonObject loop = root.value(QStringLiteral("loopRegion")).toObject();
    QCOMPARE(loop.value(QStringLiteral("enabled")).toBool(), true);
    QCOMPARE(loop.value(QStringLiteral("startMs")).toInteger(), 125);
    QCOMPARE(loop.value(QStringLiteral("endMs")).toInteger(), 875);
    QVERIFY(root.value(QStringLiteral("exportPreset")).isObject());
    QVERIFY(root.value(QStringLiteral("viewState")).isObject());
    const QJsonArray assets = root.value(QStringLiteral("assetReferences")).toArray();
    QCOMPARE(assets.size(), 1);
    QCOMPARE(assets.at(0).toObject().value(QStringLiteral("path")).toString(),
             QStringLiteral("source.wav"));
    QVERIFY(!QFileInfo(assets.at(0).toObject()
                           .value(QStringLiteral("path")).toString()).isAbsolute());

    LightEditor restored;
    QVERIFY(restored.loadProject(QUrl::fromLocalFile(projectPath)));
    QCOMPARE(restored.targetBpm(), 132.5);
    QCOMPARE(restored.snapDivision(), 6);
    QCOMPARE(restored.tracks().at(0).toMap().value(QStringLiteral("path")).toString(),
             audioPath);
}

void LightEditorControllerTest::autosavesChangedProjectsAndRecoversTheAutosave()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString audioPath = temp.filePath(QStringLiteral("autosave.wav"));
    const QString projectPath = temp.filePath(QStringLiteral("autosave.agproj"));
    QVERIFY(agplayer::test::writeClickTrackWav(audioPath, 120, 2));

    LightEditor editor;
    editor.loadFileToTrack(0, QUrl::fromLocalFile(audioPath));
    QVERIFY(!editor.autosaveNow());
    QVERIFY(editor.saveProject(QUrl::fromLocalFile(projectPath)));
    QCOMPARE(editor.projectPath(), projectPath);
    QVERIFY(!editor.projectDirty());

    const QString clipId = editor.tracks().at(0).toMap()
                               .value(QStringLiteral("clipId")).toString();
    QVERIFY(editor.setClipGainById(clipId, 0.42));
    QVERIFY(editor.projectDirty());
    QSignalSpy autosaveSpy(&editor, &LightEditor::autosaveWritten);
    QVERIFY(editor.autosaveNow());
    QCOMPARE(autosaveSpy.count(), 1);
    QVERIFY(QFileInfo::exists(projectPath + QStringLiteral(".autosave")));

    LightEditor restored;
    QSignalSpy recoverySpy(&restored, &LightEditor::autosaveRecovered);
    QVERIFY(restored.loadProject(QUrl::fromLocalFile(projectPath)));
    QCOMPARE(recoverySpy.count(), 1);
    QVERIFY(restored.projectDirty());
    QCOMPARE(restored.tracks().at(0).toMap()
                 .value(QStringLiteral("gain")).toDouble(),
             0.42);
}

void LightEditorControllerTest::singleClipCommandsKeepSelectionSetCoherent()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = temp.filePath(QStringLiteral("selection.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(path, 120, 4));

    LightEditor editor;
    editor.setSnapEnabled(false);
    editor.loadFileToTrack(0, QUrl::fromLocalFile(path));
    const QString originalId = editor.tracks().at(0).toMap()
                                   .value(QStringLiteral("clipId")).toString();
    editor.setSelectedClipId(originalId);
    QVERIFY(editor.duplicateSelectedClip(5000));
    QCOMPARE(editor.selectedClipIds().size(), 1);
    QCOMPARE(editor.selectedClipIds().constFirst().toString(),
             editor.selectedClipId());

    const QString duplicateId = editor.selectedClipId();
    QVERIFY(editor.splitSelectedClip(6500));
    QCOMPARE(editor.selectedClipIds().size(), 1);
    QCOMPARE(editor.selectedClipIds().constFirst().toString(),
             editor.selectedClipId());
    QVERIFY(editor.selectedClipId() != duplicateId);

    QVERIFY(editor.deleteSelectedClip());
    QVERIFY(editor.selectedClipIds().isEmpty());
    QVERIFY(editor.selectedClipId().isEmpty());
}

void LightEditorControllerTest::movesClipsAcrossTracksAndDuplicatesInPlace()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = temp.filePath(QStringLiteral("move.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(path, 120, 3));

    LightEditor editor;
    editor.setSnapEnabled(false);
    editor.loadFileToTrack(0, QUrl::fromLocalFile(path));
    const QString clipId = editor.tracks().at(0).toMap()
                               .value(QStringLiteral("clipId")).toString();

    QVERIFY(editor.setClipFadesById(clipId, 180, 260));
    QCOMPARE(editor.tracks().at(0).toMap()
                 .value(QStringLiteral("fadeInCurve")).toString(),
             QStringLiteral("EqualPower"));
    QVERIFY(editor.setClipFadeCurvesById(
        clipId, QStringLiteral("Linear"), QStringLiteral("Smooth")));
    QVERIFY(editor.setClipGainById(clipId, 0.75));
    QVariantMap adjusted = editor.tracks().at(0).toMap();
    QCOMPARE(adjusted.value(QStringLiteral("fadeInMs")).toInt(), 180);
    QCOMPARE(adjusted.value(QStringLiteral("fadeOutMs")).toInt(), 260);
    QCOMPARE(adjusted.value(QStringLiteral("fadeInCurve")).toString(),
             QStringLiteral("Linear"));
    QCOMPARE(adjusted.value(QStringLiteral("fadeOutCurve")).toString(),
             QStringLiteral("Smooth"));
    QCOMPARE(adjusted.value(QStringLiteral("gain")).toDouble(), 0.75);

    QVERIFY(editor.moveClipToTrackById(clipId, 3, 1250));
    QVERIFY(!editor.tracks().at(0).toMap()
                 .value(QStringLiteral("hasFile")).toBool());
    const QVariantMap moved = editor.tracks().at(3).toMap();
    QCOMPARE(moved.value(QStringLiteral("clipId")).toString(), clipId);
    QCOMPARE(moved.value(QStringLiteral("trackIndex")).toInt(), 3);
    QCOMPARE(moved.value(QStringLiteral("timelineStartMs")).toLongLong(), 1250);

    editor.setSelectedClipId(clipId);
    QVERIFY(editor.duplicateSelectedClip(4500));
    QCOMPARE(editor.clipCount(), 2);
    const QVariantList clips = editor.tracks().at(3).toMap()
                                   .value(QStringLiteral("clips")).toList();
    QCOMPARE(clips.size(), 2);
    QVERIFY(clips.at(0).toMap().value(QStringLiteral("clipId")).toString()
            != clips.at(1).toMap().value(QStringLiteral("clipId")).toString());
    QCOMPARE(clips.at(1).toMap()
                 .value(QStringLiteral("timelineStartMs")).toLongLong(), 4500);

    const QString duplicateId = editor.selectedClipId();
    QVERIFY(editor.setClipMutedById(duplicateId, true));
    const QVariantList mutedClips = editor.tracks().at(3).toMap()
                                        .value(QStringLiteral("clips")).toList();
    QVERIFY(!mutedClips.at(0).toMap().value(QStringLiteral("muted")).toBool());
    QVERIFY(mutedClips.at(1).toMap().value(QStringLiteral("muted")).toBool());

    editor.undo();
    QVERIFY(!editor.tracks().at(3).toMap()
                 .value(QStringLiteral("clips")).toList().at(1).toMap()
                 .value(QStringLiteral("muted")).toBool());
    editor.undo();
    QCOMPARE(editor.clipCount(), 1);
    editor.undo();
    QVERIFY(editor.tracks().at(0).toMap()
                .value(QStringLiteral("hasFile")).toBool());
}

void LightEditorControllerTest::createsEqualPowerCrossfadeForOverlappingClips()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString firstPath = temp.filePath(QStringLiteral("crossfade-a.wav"));
    const QString secondPath = temp.filePath(QStringLiteral("crossfade-b.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(firstPath, 120, 4));
    QVERIFY(QFile::copy(firstPath, secondPath));

    LightEditor editor;
    editor.setSnapEnabled(false);
    editor.loadFileToTrack(0, QUrl::fromLocalFile(firstPath));
    editor.loadFileToTrack(1, QUrl::fromLocalFile(secondPath));
    const QString secondId = editor.tracks().at(1).toMap()
                                 .value(QStringLiteral("clipId")).toString();
    QVERIFY(editor.moveClipToTrackById(secondId, 0, 3000));

    const QVariantList clips = editor.tracks().at(0).toMap()
                                   .value(QStringLiteral("clips")).toList();
    QCOMPARE(clips.size(), 2);
    QCOMPARE(clips.at(0).toMap().value(QStringLiteral("fadeOutMs")).toInt(),
             1000);
    QCOMPARE(clips.at(1).toMap().value(QStringLiteral("fadeInMs")).toInt(),
             1000);
    QCOMPARE(clips.at(0).toMap()
                 .value(QStringLiteral("fadeOutCurve")).toString(),
             QStringLiteral("EqualPower"));
    QCOMPARE(clips.at(1).toMap()
                 .value(QStringLiteral("fadeInCurve")).toString(),
             QStringLiteral("EqualPower"));
}

void LightEditorControllerTest::disablesOnlyAutomaticCrossfades()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString firstPath = temp.filePath(QStringLiteral("crossfade-clear-a.wav"));
    const QString secondPath = temp.filePath(QStringLiteral("crossfade-clear-b.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(firstPath, 120, 4));
    QVERIFY(QFile::copy(firstPath, secondPath));

    LightEditor editor;
    editor.setSnapEnabled(false);
    editor.loadFileToTrack(0, QUrl::fromLocalFile(firstPath));
    editor.loadFileToTrack(1, QUrl::fromLocalFile(secondPath));
    const QString secondId = editor.tracks().at(1).toMap()
                                 .value(QStringLiteral("clipId")).toString();
    QVERIFY(editor.moveClipToTrackById(secondId, 0, 3000));

    editor.setAutoCrossfade(false);
    const QVariantList clips = editor.tracks().at(0).toMap()
                                   .value(QStringLiteral("clips")).toList();
    QCOMPARE(clips.size(), 2);
    QCOMPARE(clips.at(0).toMap().value(QStringLiteral("fadeOutMs")).toInt(), 0);
    QCOMPARE(clips.at(1).toMap().value(QStringLiteral("fadeInMs")).toInt(), 0);
    QVERIFY(!clips.at(0).toMap().value(QStringLiteral("autoFadeOut")).toBool());
    QVERIFY(!clips.at(1).toMap().value(QStringLiteral("autoFadeIn")).toBool());
}

void LightEditorControllerTest::splitsClipsOnTheSameTimelineLane()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = temp.filePath(QStringLiteral("split.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(path, 120, 4));

    LightEditor editor;
    editor.setSnapEnabled(false);
    editor.loadFileToTrack(0, QUrl::fromLocalFile(path));
    editor.setSelectedTrack(0);
    QVERIFY(editor.splitSelectedClip(2000));

    const QVariantList clips = editor.tracks().at(0).toMap()
                                   .value(QStringLiteral("clips")).toList();
    QCOMPARE(clips.size(), 2);
    QCOMPARE(clips.at(0).toMap().value(QStringLiteral("trackIndex")).toInt(), 0);
    QCOMPARE(clips.at(1).toMap().value(QStringLiteral("trackIndex")).toInt(), 0);
    QCOMPARE(clips.at(0).toMap().value(QStringLiteral("outMs")).toLongLong(),
             2000);
    QCOMPARE(clips.at(1).toMap().value(QStringLiteral("inMs")).toLongLong(),
             2000);
    QCOMPARE(clips.at(1).toMap()
                 .value(QStringLiteral("timelineStartMs")).toLongLong(), 2000);
}

void LightEditorControllerTest::selectsAndMovesMultipleClipsAsOneEdit()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = temp.filePath(QStringLiteral("multi-select.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(path, 120, 2));

    LightEditor editor;
    editor.setSnapEnabled(false);
    for (int index = 0; index < 3; ++index) {
        editor.loadFileToTrack(index, QUrl::fromLocalFile(path));
    }
    QVERIFY(editor.moveClip(1, 3000));
    QVERIFY(editor.moveClip(2, 6000));

    const QString firstId = editor.tracks().at(0).toMap()
                                .value(QStringLiteral("clipId")).toString();
    const QString secondId = editor.tracks().at(1).toMap()
                                 .value(QStringLiteral("clipId")).toString();
    QVERIFY(editor.selectClip(firstId, false, false));
    QVERIFY(editor.selectClip(secondId, true, false));
    QCOMPARE(editor.selectedClipIds().size(), 2);

    QVERIFY(editor.moveSelectedClips(500, 1));
    QCOMPARE(editor.tracks().at(1).toMap()
                 .value(QStringLiteral("timelineStartMs")).toLongLong(), 500);
    const QVariantList targetLaneClips = editor.tracks().at(2).toMap()
                                             .value(QStringLiteral("clips"))
                                             .toList();
    const auto movedSecond = std::find_if(
        targetLaneClips.cbegin(), targetLaneClips.cend(),
        [&secondId](const QVariant& value) {
            return value.toMap().value(QStringLiteral("clipId")).toString()
                == secondId;
        });
    QVERIFY(movedSecond != targetLaneClips.cend());
    QCOMPARE(movedSecond->toMap()
                 .value(QStringLiteral("timelineStartMs")).toLongLong(), 3500);
    QCOMPARE(editor.selectedClipIds().size(), 2);

    QVERIFY(editor.deleteSelectedClip());
    QCOMPARE(editor.clipCount(), 1);
    editor.undo();
    QCOMPARE(editor.clipCount(), 3);
    QCOMPARE(editor.selectedClipIds().size(), 2);
    editor.undo();
    QCOMPARE(editor.tracks().at(0).toMap()
                 .value(QStringLiteral("timelineStartMs")).toLongLong(), 0);
    QCOMPARE(editor.tracks().at(1).toMap()
                 .value(QStringLiteral("timelineStartMs")).toLongLong(), 3000);
}

void LightEditorControllerTest::boxSelectsClipsAcrossTimelineLanes()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = temp.filePath(QStringLiteral("box-select.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(path, 120, 2));

    LightEditor editor;
    editor.setSnapEnabled(false);
    for (int index = 0; index < 3; ++index) {
        editor.loadFileToTrack(index, QUrl::fromLocalFile(path));
    }
    QVERIFY(editor.moveClip(1, 3000));
    QVERIFY(editor.moveClip(2, 6000));

    QCOMPARE(editor.selectClipsInRange(2200, 4500, 1, 1, false), 1);
    QCOMPARE(editor.selectedClipIds().size(), 1);
    QCOMPARE(editor.selectedClipId(),
             editor.tracks().at(1).toMap()
                 .value(QStringLiteral("clipId")).toString());

    QCOMPARE(editor.selectClipsInRange(5500, 6500, 2, 2, true), 1);
    QCOMPARE(editor.selectedClipIds().size(), 2);

    QCOMPARE(editor.selectClipsInRange(10000, 12000, 0, 15, false), 0);
    QCOMPARE(editor.selectedClipIds().size(), 0);
}

void LightEditorControllerTest::rippleDeleteMovesLaterClipsAndUndoRestoresThem()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = temp.filePath(QStringLiteral("ripple.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(path, 120, 2));

    LightEditor editor;
    editor.setSnapEnabled(false);
    for (int index = 0; index < 3; ++index) {
        editor.loadFileToTrack(index, QUrl::fromLocalFile(path));
    }
    QVERIFY(editor.moveClip(1, 3000));
    QVERIFY(editor.moveClip(2, 5000));
    editor.setRippleEditing(true);
    editor.setSelectedTrack(0);

    QVERIFY(editor.deleteSelectedClip());
    QVERIFY(!editor.tracks().at(0).toMap()
                 .value(QStringLiteral("hasFile")).toBool());
    QCOMPARE(editor.tracks().at(1).toMap()
                 .value(QStringLiteral("timelineStartMs")).toLongLong(), 1000);
    QCOMPARE(editor.tracks().at(2).toMap()
                 .value(QStringLiteral("timelineStartMs")).toLongLong(), 3000);

    editor.undo();
    QVERIFY(editor.tracks().at(0).toMap()
                .value(QStringLiteral("hasFile")).toBool());
    QCOMPARE(editor.tracks().at(1).toMap()
                 .value(QStringLiteral("timelineStartMs")).toLongLong(), 3000);
    QCOMPARE(editor.tracks().at(2).toMap()
                 .value(QStringLiteral("timelineStartMs")).toLongLong(), 5000);
}

void LightEditorControllerTest::fillsEmptyTracksFromDroppedFilesInOrder()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    QList<QUrl> urls;
    for (int index = 0; index < 7; ++index) {
        const QString path =
            temp.filePath(QStringLiteral("track-%1.wav").arg(index));
        QVERIFY(agplayer::test::writeClickTrackWav(path, 120 + index, 1));
        urls.push_back(QUrl::fromLocalFile(path));
    }

    LightEditor editor;
    QCOMPARE(editor.loadFiles(urls), 7);
    QCOMPARE(editor.clipCount(), 7);
    QCOMPARE(editor.tracks().at(0).toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("track-0.wav"));
    QCOMPARE(editor.tracks().at(5).toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("track-5.wav"));
    const QVariantList firstLaneClips = editor.tracks().at(0).toMap()
        .value(QStringLiteral("clips")).toList();
    QCOMPARE(firstLaneClips.size(), 2);
    QCOMPARE(firstLaneClips.at(1).toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("track-6.wav"));
    QVERIFY(firstLaneClips.at(0).toMap().value(QStringLiteral("clipId")).toString()
            != firstLaneClips.at(1).toMap().value(QStringLiteral("clipId")).toString());
}

void LightEditorControllerTest::loadsFirstSupportedFileFromDroppedFolder()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString nested = temp.filePath(QStringLiteral("中文目录"));
    QVERIFY(QDir().mkpath(nested));
    const QString path =
        QDir(nested).filePath(QStringLiteral("track.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(path, 120, 1));

    LightEditor editor;
    editor.loadFileToTrack(0, QUrl::fromLocalFile(temp.path()));
    QCOMPARE(
        QFileInfo(editor.tracks().at(0).toMap()
                      .value(QStringLiteral("path")).toString())
            .canonicalFilePath(),
        QFileInfo(path).canonicalFilePath());
}

void LightEditorControllerTest::exposesSixTracksAndMixerDefaults()
{
    LightEditor editor;
    QCOMPARE(editor.trackCount(), 6);
    QCOMPARE(editor.tracks().size(), 6);
    const QVariantMap emptyTrack = editor.tracks().first().toMap();
    QVERIFY(emptyTrack.contains(QStringLiteral("timelineStartMs")));
    QVERIFY(emptyTrack.contains(QStringLiteral("locked")));
    QVERIFY(emptyTrack.contains(QStringLiteral("peaks")));
    QCOMPARE(emptyTrack.value(QStringLiteral("volume")).toDouble(), 1.0);
    QCOMPARE(emptyTrack.value(QStringLiteral("pan")).toDouble(), 0.0);
    QVERIFY(!emptyTrack.value(QStringLiteral("color")).toString().isEmpty());
    QCOMPARE(emptyTrack.value(QStringLiteral("trackName")).toString(),
             QStringLiteral("Track 1"));
}

void LightEditorControllerTest::savesAndLoadsProjectWithMixerState()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString audioPath = temp.filePath(QStringLiteral("project.wav"));
    const QString projectPath = temp.filePath(QStringLiteral("session.agproject"));
    QVERIFY(agplayer::test::writeClickTrackWav(audioPath, 120, 2));

    LightEditor editor;
    editor.loadFileToTrack(0, QUrl::fromLocalFile(audioPath));
    editor.loadFileToTrack(1, QUrl::fromLocalFile(audioPath));
    editor.setTargetBpm(147.5);
    editor.setSnapDivision(8);
    editor.setTimeSignature(QStringLiteral("3/4"));
    editor.setProjectKey(QStringLiteral("Am"));
    editor.setLoopEnabled(true);
    editor.setLoopStartMs(500);
    editor.setLoopEndMs(1500);
    editor.setRippleEditing(true);
    QVERIFY(editor.setTrackVolume(0, 0.65));
    QVERIFY(editor.setTrackPan(0, -0.25));
    QVERIFY(editor.setTrackName(0, QStringLiteral("Lead")));
    QVERIFY(editor.setTrackColor(0, QStringLiteral("#D27722")));
    QVERIFY(editor.setTrackCollapsed(0, true));
    const QString firstClipId = editor.tracks().at(0).toMap()
                                    .value(QStringLiteral("clipId")).toString();
    const QString secondClipId = editor.tracks().at(1).toMap()
                                     .value(QStringLiteral("clipId")).toString();
    QVERIFY(editor.selectClip(firstClipId, false, false));
    QVERIFY(editor.selectClip(secondClipId, true, false));
    QVERIFY(editor.saveProject(QUrl::fromLocalFile(projectPath)));

    LightEditor restored;
    QVERIFY(restored.loadProject(QUrl::fromLocalFile(projectPath)));
    QCOMPARE(restored.clipCount(), 2);
    QCOMPARE(restored.targetBpm(), 147.5);
    QCOMPARE(restored.snapDivision(), 8);
    QCOMPARE(restored.timeSignature(), QStringLiteral("3/4"));
    QCOMPARE(restored.projectKey(), QStringLiteral("Am"));
    QVERIFY(restored.loopEnabled());
    QCOMPARE(restored.loopStartMs(), 500);
    QCOMPARE(restored.loopEndMs(), 1500);
    QVERIFY(restored.rippleEditing());
    const QVariantMap track = restored.tracks().first().toMap();
    QCOMPARE(track.value(QStringLiteral("trackName")).toString(),
             QStringLiteral("Lead"));
    QCOMPARE(track.value(QStringLiteral("volume")).toDouble(), 0.65);
    QCOMPARE(track.value(QStringLiteral("pan")).toDouble(), -0.25);
    QCOMPARE(track.value(QStringLiteral("color")).toString(),
             QStringLiteral("#D27722"));
    QVERIFY(track.value(QStringLiteral("collapsed")).toBool());
    QCOMPARE(restored.selectedClipIds().size(), 2);
    QCOMPARE(restored.selectedClipIds().at(0).toString(), firstClipId);
    QCOMPARE(restored.selectedClipIds().at(1).toString(), secondClipId);
    QCOMPARE(QFileInfo(track.value(QStringLiteral("path")).toString())
                 .canonicalFilePath(),
             QFileInfo(audioPath).canonicalFilePath());
}

void LightEditorControllerTest::movesSnapsLocksAndRestoresClips()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = temp.filePath(QStringLiteral("track.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(path, 120, 2));

    LightEditor editor;
    editor.loadFileToTrack(0, QUrl::fromLocalFile(path));
    editor.setTargetBpm(120.0);
    editor.setSnapEnabled(true);

    QSignalSpy tracksSpy(&editor, &LightEditor::tracksChanged);
    QVERIFY(editor.moveClip(0, 740));
    QCOMPARE(tracksSpy.count(), 1);
    QCOMPARE(editor.tracks().at(0).toMap().value(QStringLiteral("timelineStartMs")).toLongLong(),
             500);

    editor.setTrackLocked(0, true);
    tracksSpy.clear();
    QVERIFY(!editor.moveClip(0, 1000));
    QVERIFY(!editor.cutSelectedClip());
    QVERIFY(!editor.deleteSelectedClip());
    QCOMPARE(tracksSpy.count(), 0);
    QVERIFY(!editor.tracks().at(0).toMap()
                 .value(QStringLiteral("path")).toString().isEmpty());
    QCOMPARE(editor.tracks().at(0).toMap().value(QStringLiteral("timelineStartMs")).toLongLong(),
             500);

    editor.setTrackLocked(0, false);
    QVERIFY(editor.moveClip(0, 760));
    QCOMPARE(editor.tracks().at(0).toMap().value(QStringLiteral("timelineStartMs")).toLongLong(),
             1000);

    QVERIFY(editor.canUndo());
    editor.undo();
    QCOMPARE(editor.tracks().at(0).toMap().value(QStringLiteral("timelineStartMs")).toLongLong(),
             500);
    QVERIFY(editor.canRedo());
    editor.redo();
    QCOMPARE(editor.tracks().at(0).toMap().value(QStringLiteral("timelineStartMs")).toLongLong(),
             1000);
}

void LightEditorControllerTest::trimsToSafeSourceBounds()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = temp.filePath(QStringLiteral("trim.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(path, 128, 2));

    LightEditor editor;
    editor.loadFileToTrack(0, QUrl::fromLocalFile(path));
    const qint64 duration = editor.tracks().at(0).toMap()
                                .value(QStringLiteral("durationMs")).toLongLong();
    QVERIFY(duration >= 1900);

    QVERIFY(editor.trimClip(0, duration - 50, duration - 20));
    const QVariantMap track = editor.tracks().at(0).toMap();
    const qint64 inMs = track.value(QStringLiteral("inMs")).toLongLong();
    const qint64 outMs = track.value(QStringLiteral("outMs")).toLongLong();
    QVERIFY(outMs <= duration);
    QVERIFY(inMs >= 0);
    QVERIFY(outMs - inMs >= 200);
}

void LightEditorControllerTest::edgeTrimKeepsTheAudioEventAnchoredToItsAudibleStart()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = temp.filePath(QStringLiteral("edge-trim.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(path, 120, 4));

    LightEditor editor;
    editor.setSnapEnabled(false);
    editor.loadFileToTrack(0, QUrl::fromLocalFile(path));
    const QString clipId = editor.tracks().at(0).toMap()
                               .value(QStringLiteral("clipId")).toString();
    const qint64 sourceDuration = editor.tracks().at(0).toMap()
                                     .value(QStringLiteral("durationMs")).toLongLong();
    QVERIFY(sourceDuration >= 4000);
    QVERIFY(editor.moveClipById(clipId, 1000));

    QVERIFY(editor.trimClipEdgeById(clipId, 500, sourceDuration, true));
    QVariantMap clip = editor.tracks().at(0).toMap();
    QCOMPARE(clip.value(QStringLiteral("inMs")).toLongLong(), 500);
    QCOMPARE(clip.value(QStringLiteral("timelineStartMs")).toLongLong(), 1500);
    QCOMPARE(clip.value(QStringLiteral("timelineDurationMs")).toLongLong(),
             sourceDuration - 500);

    QVERIFY(editor.trimClipEdgeById(clipId, 500, 3200, false));
    clip = editor.tracks().at(0).toMap();
    QCOMPARE(clip.value(QStringLiteral("timelineStartMs")).toLongLong(), 1500);
    QCOMPARE(clip.value(QStringLiteral("timelineDurationMs")).toLongLong(), 2700);

    editor.setSnapEnabled(true);
    editor.setTargetBpm(120.0);
    editor.setSnapDivision(4);
    QVERIFY(editor.trimClipEdgeById(clipId, 800, 3200, true, false));
    clip = editor.tracks().at(0).toMap();
    QCOMPARE(clip.value(QStringLiteral("inMs")).toLongLong(), 1000);
    QCOMPARE(clip.value(QStringLiteral("timelineStartMs")).toLongLong(), 2000);
}

void LightEditorControllerTest::editsSelectedClipsWithClipboardSplitMergeAndCrop()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = temp.filePath(QStringLiteral("edit.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(path, 120, 3));

    LightEditor editor;
    editor.setSnapEnabled(false);
    editor.loadFileToTrack(0, QUrl::fromLocalFile(path));
    QVERIFY(editor.moveClip(0, 1000));
    QVERIFY(editor.trimClip(0, 200, 2800));

    QVERIFY(editor.copySelectedClip());
    QVERIFY(editor.hasClipboard());
    editor.setSelectedTrack(1);
    QVERIFY(editor.pasteClip());
    QVariantMap pasted = editor.tracks().at(1).toMap();
    QCOMPARE(pasted.value(QStringLiteral("name")).toString(),
             QFileInfo(path).fileName());
    QCOMPARE(pasted.value(QStringLiteral("timelineStartMs")).toLongLong(), 1000);
    QCOMPARE(pasted.value(QStringLiteral("inMs")).toLongLong(), 200);
    QCOMPARE(pasted.value(QStringLiteral("outMs")).toLongLong(), 2800);

    QVERIFY(editor.cutSelectedClip());
    QVERIFY(!editor.tracks().at(1).toMap().value(QStringLiteral("hasFile")).toBool());
    editor.undo();
    QVERIFY(editor.tracks().at(1).toMap().value(QStringLiteral("hasFile")).toBool());
    editor.redo();
    QVERIFY(!editor.tracks().at(1).toMap().value(QStringLiteral("hasFile")).toBool());

    editor.setSelectedTrack(0);
    QVERIFY(editor.splitSelectedClip(2000));
    const QVariantList splitClips = editor.tracks().at(0).toMap()
                                        .value(QStringLiteral("clips")).toList();
    QCOMPARE(splitClips.size(), 2);
    const QVariantMap first = splitClips.at(0).toMap();
    const QVariantMap second = splitClips.at(1).toMap();
    QCOMPARE(first.value(QStringLiteral("outMs")).toLongLong(), 1200);
    QCOMPARE(second.value(QStringLiteral("inMs")).toLongLong(), 1200);
    QCOMPARE(second.value(QStringLiteral("timelineStartMs")).toLongLong(), 2000);

    editor.setSelectedTrack(0);
    QVERIFY(editor.mergeSelectedClip());
    QCOMPARE(editor.tracks().at(0).toMap()
                 .value(QStringLiteral("outMs")).toLongLong(), 2800);
    QVERIFY(!editor.tracks().at(1).toMap().value(QStringLiteral("hasFile")).toBool());

    QVERIFY(editor.cropSelectedClip(2200));
    QCOMPARE(editor.tracks().at(0).toMap()
                 .value(QStringLiteral("outMs")).toLongLong(), 1400);
    QCOMPARE(editor.tracks().at(0).toMap()
                 .value(QStringLiteral("timelineDurationMs")).toLongLong(), 1200);
    QVERIFY(editor.deleteSelectedClip());
    QVERIFY(!editor.tracks().at(0).toMap().value(QStringLiteral("hasFile")).toBool());
}

void LightEditorControllerTest::validatesBpmAndTrackSwitches()
{
    LightEditor editor;
    QCOMPARE(editor.targetBpm(), 128.0);
    editor.setTargetBpm(39.0);
    QCOMPARE(editor.targetBpm(), 128.0);
    editor.setTargetBpm(301.0);
    QCOMPARE(editor.targetBpm(), 128.0);
    editor.setTargetBpm(140.0);
    QCOMPARE(editor.targetBpm(), 140.0);

    editor.setTrackMuted(2, true);
    editor.setTrackSolo(2, true);
    editor.setTrackLocked(2, true);
    const QVariantMap track = editor.tracks().at(2).toMap();
    QVERIFY(track.value(QStringLiteral("muted")).toBool());
    QVERIFY(track.value(QStringLiteral("solo")).toBool());
    QVERIFY(track.value(QStringLiteral("locked")).toBool());
}

void LightEditorControllerTest::selectedTrackSpeedControlsStayTrackLocal()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString first = temp.filePath(QStringLiteral("first.wav"));
    const QString second = temp.filePath(QStringLiteral("second.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(first, 100, 2));
    QVERIFY(agplayer::test::writeClickTrackWav(second, 120, 2));

    LightEditor editor;
    editor.loadFileToTrack(0, QUrl::fromLocalFile(first));
    editor.loadFileToTrack(1, QUrl::fromLocalFile(second));
    editor.analyzeTrackBpm(0);
    QTRY_VERIFY_WITH_TIMEOUT(!editor.busy(), 30000);
    editor.analyzeTrackBpm(1);
    QTRY_VERIFY_WITH_TIMEOUT(!editor.busy(), 30000);

    QVERIFY(editor.setTrackTargetBpm(0, 125.0));
    QVERIFY(editor.setTrackKeepPitch(0, false));
    QVERIFY(editor.setTrackBeatAligned(0, true));

    const QVariantMap adjusted = editor.tracks().at(0).toMap();
    const QVariantMap untouched = editor.tracks().at(1).toMap();
    QVERIFY(std::abs(adjusted.value(QStringLiteral("targetBpm")).toDouble()
                     - 125.0) < 0.01);
    QVERIFY(std::abs(adjusted.value(QStringLiteral("speedRatio")).toDouble()
                     - 1.25) < 0.02);
    QVERIFY(!adjusted.value(QStringLiteral("keepPitch")).toBool());
    QVERIFY(adjusted.value(QStringLiteral("aligned")).toBool());
    QVERIFY(std::abs(untouched.value(QStringLiteral("speedRatio")).toDouble()
                     - 1.0) < 0.01);
    QVERIFY(untouched.value(QStringLiteral("keepPitch")).toBool());
}

void LightEditorControllerTest::selectedTrackSpeedIsAppliedToExportWithoutBatchUnify()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString input = temp.filePath(QStringLiteral("tempo-source.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(input, 100, 4));

    LightEditor editor;
    editor.loadFileToTrack(0, QUrl::fromLocalFile(input));
    editor.analyzeTrackBpm(0);
    QTRY_VERIFY_WITH_TIMEOUT(!editor.busy(), 30000);
    QVERIFY(editor.setTrackTargetBpm(0, 125.0));
    QVERIFY(editor.setTrackKeepPitch(0, true));

    QSignalSpy completed(&editor, &LightEditor::lightEditCompleted);
    editor.exportProject(temp.path(), QStringLiteral("wav"), 44100, 2);
    QVERIFY(completed.wait(30000));
    QCOMPARE(completed.count(), 1);

    const QString outputPath = completed.first().first().toString();
    ag_metadata* metadata = nullptr;
    QCOMPARE(ag_metadata_open(outputPath.toUtf8().constData(), &metadata), AG_OK);
    QVERIFY(metadata != nullptr);
    const qint64 duration = ag_metadata_duration_ms(metadata);
    ag_metadata_destroy(metadata);

    QVERIFY2(duration >= 3100 && duration <= 3350,
             qPrintable(QStringLiteral("Expected 4 s / 1.25 export, got %1 ms")
                            .arg(duration)));
}

void LightEditorControllerTest::clipTimePitchPropertiesPersistInProject()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString input = temp.filePath(QStringLiteral("time-pitch.wav"));
    const QString project = temp.filePath(QStringLiteral("time-pitch.agproj"));
    QVERIFY(agplayer::test::writeClickTrackWav(input, 120, 4));

    LightEditor editor;
    editor.loadFileToTrack(0, QUrl::fromLocalFile(input));
    const QString clipId = editor.tracks().at(0).toMap()
        .value(QStringLiteral("clipId")).toString();

    QVERIFY(editor.setClipPitchById(clipId, 3.0, 25.0));
    QVERIFY(editor.setClipFormantModeById(clipId, 2));
    QVERIFY(editor.setClipTransientProtectionById(clipId, 0.75));
    QVERIFY(editor.setClipHighQualityById(clipId, true));

    const QVariantMap configured = editor.tracks().at(0).toMap();
    QCOMPARE(configured.value(QStringLiteral("pitchSemitones")).toDouble(), 3.0);
    QCOMPARE(configured.value(QStringLiteral("finePitchCents")).toDouble(), 25.0);
    QCOMPARE(configured.value(QStringLiteral("formantMode")).toInt(), 2);
    QCOMPARE(configured.value(QStringLiteral("transientProtection")).toDouble(), 0.75);
    QVERIFY(configured.value(QStringLiteral("highQuality")).toBool());

    QSignalSpy completed(&editor, &LightEditor::lightEditCompleted);
    editor.exportProject(temp.path(), QStringLiteral("wav"), 44100, 2);
    QVERIFY(completed.wait(30000));
    QCOMPARE(completed.count(), 1);
    const QString renderedPath = completed.first().first().toString();
    ag_metadata* renderedMetadata = nullptr;
    QCOMPARE(ag_metadata_open(renderedPath.toUtf8().constData(),
                              &renderedMetadata), AG_OK);
    QVERIFY(renderedMetadata != nullptr);
    QVERIFY(ag_metadata_duration_ms(renderedMetadata) >= 3900);
    ag_metadata_destroy(renderedMetadata);

    QVERIFY(editor.saveProject(QUrl::fromLocalFile(project)));

    LightEditor restored;
    QVERIFY(restored.loadProject(QUrl::fromLocalFile(project)));
    const QVariantMap reloaded = restored.tracks().at(0).toMap();
    QCOMPARE(reloaded.value(QStringLiteral("pitchSemitones")).toDouble(), 3.0);
    QCOMPARE(reloaded.value(QStringLiteral("finePitchCents")).toDouble(), 25.0);
    QCOMPARE(reloaded.value(QStringLiteral("formantMode")).toInt(), 2);
    QCOMPARE(reloaded.value(QStringLiteral("transientProtection")).toDouble(), 0.75);
    QVERIFY(reloaded.value(QStringLiteral("highQuality")).toBool());
}

void LightEditorControllerTest::timePitchControlsApplyToTheSelectedClipOnly()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString input = temp.filePath(QStringLiteral("clip-local.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(input, 120, 4));

    LightEditor editor;
    editor.loadFileToTrack(0, QUrl::fromLocalFile(input));
    editor.analyzeTrackBpm(0);
    QTRY_VERIFY_WITH_TIMEOUT(!editor.busy(), 30000);
    QVERIFY(editor.duplicateSelectedClip(5000));

    const QVariantList clips = editor.tracks().at(0).toMap()
        .value(QStringLiteral("clips")).toList();
    QCOMPARE(clips.size(), 2);
    const QString firstId = clips.at(0).toMap()
        .value(QStringLiteral("clipId")).toString();
    const QString secondId = clips.at(1).toMap()
        .value(QStringLiteral("clipId")).toString();

    QSignalSpy clipErrors(&editor, &LightEditor::trackError);
    editor.analyzeClipBpmById(secondId);
    QTRY_VERIFY_WITH_TIMEOUT(!editor.busy(), 30000);
    QCOMPARE(clipErrors.count(), 0);

    QVERIFY(editor.setClipTargetBpmById(secondId, 150.0));
    QVERIFY(editor.setClipKeepPitchById(secondId, false));
    QVERIFY(editor.setClipBeatAlignedById(secondId, true));

    const QVariantMap first = editor.tracks().at(0).toMap()
        .value(QStringLiteral("clips")).toList().at(0).toMap();
    const QVariantMap second = editor.tracks().at(0).toMap()
        .value(QStringLiteral("clips")).toList().at(1).toMap();
    QCOMPARE(first.value(QStringLiteral("clipId")).toString(), firstId);
    QVERIFY(first.value(QStringLiteral("keepPitch")).toBool());
    QVERIFY(!first.value(QStringLiteral("aligned")).toBool());
    QCOMPARE(second.value(QStringLiteral("clipId")).toString(), secondId);
    QVERIFY(std::abs(second.value(QStringLiteral("targetBpm")).toDouble()
                     - 150.0) < 0.01);
    QVERIFY(std::abs(second.value(QStringLiteral("speedRatio")).toDouble()
                     - 1.25) < 0.02);
    QVERIFY(!second.value(QStringLiteral("keepPitch")).toBool());
    QVERIFY(second.value(QStringLiteral("aligned")).toBool());
}

void LightEditorControllerTest::analyzesAndUnifiesBpm()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = temp.filePath(QStringLiteral("bpm-90.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(path, 90, 10));

    LightEditor editor;
    editor.loadFileToTrack(0, QUrl::fromLocalFile(path));
    editor.setSnapEnabled(false);
    QVERIFY(editor.moveClip(0, 740));

    editor.analyzeTrackBpm(0);
    QTRY_VERIFY_WITH_TIMEOUT(!editor.busy(), 30000);
    QVariantMap track = editor.tracks().at(0).toMap();
    QVERIFY(std::abs(track.value(QStringLiteral("originalBpm")).toDouble()
                     - 90.0) < 1.0);
    QVERIFY(track.value(QStringLiteral("bpmConfidence")).toDouble() > 80.0);

    editor.setSelectedTrack(0);
    QVERIFY(editor.splitSelectedClip(5000));
    QCOMPARE(editor.clipCount(), 2);

    editor.setTargetBpm(120.0);
    editor.setKeepPitch(true);
    editor.unifyBpm(true);
    QTRY_VERIFY_WITH_TIMEOUT(!editor.busy(), 30000);

    const QVariantList clips = editor.tracks().at(0).toMap()
                                   .value(QStringLiteral("clips")).toList();
    QCOMPARE(clips.size(), 2);
    for (const QVariant& value : clips) {
        track = value.toMap();
        QVERIFY(std::abs(track.value(QStringLiteral("speedRatio")).toDouble()
                         - (120.0 / 90.0)) < 0.02);
        QVERIFY(track.value(QStringLiteral("aligned")).toBool());
        const QString renderPath =
            track.value(QStringLiteral("renderPath")).toString();
        QVERIFY(!renderPath.isEmpty());
        QVERIFY(QFileInfo::exists(renderPath));
        QFile::remove(renderPath);
    }
}

void LightEditorControllerTest::exportsTimelineAndFiltersTracks()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = temp.filePath(QStringLiteral("source.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(path, 120, 2));

    LightEditor editor;
    editor.setSnapEnabled(false);
    for (int index = 0; index < 3; ++index) {
        editor.loadFileToTrack(index, QUrl::fromLocalFile(path));
    }
    QVERIFY(editor.moveClip(1, 1000));
    QVERIFY(editor.moveClip(2, 500));
    editor.setTrackMuted(1, true);
    editor.setTrackSolo(2, true);

    QSignalSpy completed(&editor, &LightEditor::lightEditCompleted);
    editor.exportProject(temp.path(), QStringLiteral("wav"), 44100, 2);
    QVERIFY(completed.wait(30000));
    QCOMPARE(completed.count(), 1);

    const QString outputPath = completed.first().first().toString();
    ag_metadata* metadata = nullptr;
    QCOMPARE(ag_metadata_open(outputPath.toUtf8().constData(), &metadata), AG_OK);
    QVERIFY(metadata != nullptr);
    const qint64 duration = ag_metadata_duration_ms(metadata);
    QCOMPARE(ag_metadata_sample_rate(metadata), 44100);
    QCOMPARE(ag_metadata_channels(metadata), 2);
    ag_metadata_destroy(metadata);
    QVERIFY(duration >= 2400);
    QVERIFY(duration < 2800);
}

void LightEditorControllerTest::exportsLoopedClipToRequestedTimelineDuration()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = temp.filePath(QStringLiteral("loop-source.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(path, 120, 2));

    LightEditor editor;
    editor.loadFileToTrack(0, QUrl::fromLocalFile(path));
    const QString clipId = editor.tracks().at(0).toMap()
        .value(QStringLiteral("clipId")).toString();
    QVERIFY(editor.setClipLoopById(clipId, QStringLiteral("Loop"), 5500));

    QSignalSpy completed(&editor, &LightEditor::lightEditCompleted);
    editor.exportProject(temp.path(), QStringLiteral("wav"), 44100, 2);
    QVERIFY(completed.wait(30000));

    const QString outputPath = completed.first().first().toString();
    ag_metadata* metadata = nullptr;
    QCOMPARE(ag_metadata_open(outputPath.toUtf8().constData(), &metadata), AG_OK);
    QVERIFY(metadata != nullptr);
    const qint64 duration = ag_metadata_duration_ms(metadata);
    ag_metadata_destroy(metadata);
    QVERIFY(duration >= 5450);
    QVERIFY(duration <= 5550);
}

void LightEditorControllerTest::exportsLoopRangeAndSelectedClipScopes()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = temp.filePath(QStringLiteral("scoped-source.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(path, 120, 2));

    LightEditor editor;
    editor.setSnapEnabled(false);
    editor.loadFileToTrack(0, QUrl::fromLocalFile(path));
    editor.loadFileToTrack(1, QUrl::fromLocalFile(path));
    QVERIFY(editor.moveClip(1, 5000));

    QSignalSpy completed(&editor, &LightEditor::lightEditCompleted);
    editor.exportProjectScope(QStringLiteral("loop"), 500, 1500,
                              temp.path(), QStringLiteral("wav"), 44100, 2);
    QVERIFY(completed.wait(30000));
    ag_metadata* metadata = nullptr;
    QCOMPARE(ag_metadata_open(
                 completed.first().first().toString().toUtf8().constData(),
                 &metadata),
             AG_OK);
    QVERIFY(metadata != nullptr);
    const qint64 loopDuration = ag_metadata_duration_ms(metadata);
    ag_metadata_destroy(metadata);
    QVERIFY(loopDuration >= 950);
    QVERIFY(loopDuration <= 1050);

    const QString secondClipId = editor.tracks().at(1).toMap()
                                     .value(QStringLiteral("clipId")).toString();
    QVERIFY(editor.selectClip(secondClipId, false, false));
    completed.clear();
    editor.exportProjectScope(QStringLiteral("selected"), 0, 0,
                              temp.path(), QStringLiteral("wav"), 44100, 2);
    QVERIFY(completed.wait(30000));
    metadata = nullptr;
    QCOMPARE(ag_metadata_open(
                 completed.first().first().toString().toUtf8().constData(),
                 &metadata),
             AG_OK);
    QVERIFY(metadata != nullptr);
    const qint64 selectedDuration = ag_metadata_duration_ms(metadata);
    ag_metadata_destroy(metadata);
    QVERIFY(selectedDuration >= 1950);
    QVERIFY(selectedDuration <= 2050);
}

void LightEditorControllerTest::exportsAllSupportedFormats()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = temp.filePath(QStringLiteral("source.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(path, 120, 2));

    for (const QString& format : {QStringLiteral("wav"), QStringLiteral("mp3"),
                                 QStringLiteral("flac"), QStringLiteral("aac"),
                                 QStringLiteral("m4a"), QStringLiteral("ogg")}) {
        LightEditor editor;
        editor.loadFileToTrack(0, QUrl::fromLocalFile(path));

        QSignalSpy completed(&editor, &LightEditor::lightEditCompleted);
        editor.exportProject(temp.path(), format, 16000, 1);
        QVERIFY2(completed.wait(30000), qPrintable(format));

        const QString outputPath = completed.first().first().toString();
        QVERIFY2(QFileInfo::exists(outputPath), qPrintable(outputPath));
        ag_metadata* metadata = nullptr;
        QCOMPARE(ag_metadata_open(outputPath.toUtf8().constData(), &metadata), AG_OK);
        QVERIFY(metadata != nullptr);
        QCOMPARE(ag_metadata_sample_rate(metadata), 16000);
        QCOMPARE(ag_metadata_channels(metadata), 1);
        QVERIFY(ag_metadata_duration_ms(metadata) >= 1900);
        ag_metadata_destroy(metadata);

        ag_player_config config{};
        config.backend = AG_AUDIO_BACKEND_NULL;
        config.buffer_frames = 4096;
        ag_player* player = nullptr;
        QCOMPARE(ag_player_create_with_config(&config, &player), AG_OK);
        QVERIFY(player != nullptr);
        QCOMPARE(ag_player_load(player, outputPath.toUtf8().constData()), AG_OK);
        QCOMPARE(ag_player_play(player), AG_OK);
        QTest::qWait(50);
        ag_playback_snapshot snapshot{};
        QCOMPARE(ag_player_snapshot(player, &snapshot), AG_OK);
        QCOMPARE(snapshot.state, AG_PLAYING);
        QVERIFY(snapshot.position_ms > 0);
        ag_player_destroy(player);
    }
}

void LightEditorControllerTest::reportsTrackAnalysisErrors()
{
    LightEditor editor;
    QSignalSpy errorSpy(&editor, &LightEditor::trackError);
    editor.analyzeTrackBpm(5);
    QCOMPARE(errorSpy.count(), 1);
    QCOMPARE(errorSpy.first().first().toInt(), 5);
}

QTEST_MAIN(LightEditorControllerTest)
#include "light_editor_controller_test.moc"
