#include "audio_tools_controller.hpp"
#include "audio_file_discovery.hpp"
#include "filename_processor.hpp"
#include "format_converter.hpp"
#include "library_model.hpp"
#include "metadata_editor.hpp"

#include "../core/bpm_fixture.hpp"

#include <agplayer/c_api.h>

#include <QDir>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QTest>
#include <QThreadPool>
#include <QVariantMap>

#include <algorithm>
#include <cmath>

namespace {

void verifyAudioFile(const QString& path, int expectedSampleRate = 0)
{
    QVERIFY2(QFileInfo::exists(path), qPrintable(path));
    ag_metadata* metadata = nullptr;
    QCOMPARE(ag_metadata_open(path.toUtf8().constData(), &metadata), AG_OK);
    QVERIFY(metadata != nullptr);
    QVERIFY(ag_metadata_duration_ms(metadata) > 0);
    if (expectedSampleRate > 0) {
        QCOMPARE(ag_metadata_sample_rate(metadata), expectedSampleRate);
    }
    ag_metadata_destroy(metadata);
}

void waitForConverterLoad(FormatConverter& converter)
{
    QTRY_VERIFY_WITH_TIMEOUT(!converter.busy(), 5'000);
}

} // namespace

class AudioToolsEndToEndTest final : public QObject {
    Q_OBJECT

private slots:
    void audioToolsControllerSupportsFourBuiltInTools();
    void toolsExpandDroppedFoldersRecursively();
    void audioFileDiscoveryExpandsFoldersOffTheGuiThread();
    void formatConverterLoadsDroppedFilesAsynchronously();
    void formatConverterReportsReferenceTaskColumns();
    void formatConverterExposesQueueFacadeModelsAndPreflight();
    void formatConverterPreflightRejectsEmptySelectionOnce();
    void formatConverterPreflightReturnsResolvedProfileBeforeStarting();
    void formatConverterBuildPreflightUsesSmartProfilesAndRejectsInvalidCustomValues();
    void formatConverterBuildPreflightFreezesResolvedSelectedTasks();
    void formatConverterPreflightRejectsVideoWhenExtractionIsDisabled();
    void formatConverterPreflightResolvesOpusSampleRate();
    void formatConverterPreflightRejectsInvalidRequest_data();
    void formatConverterPreflightRejectsInvalidRequest();
    void formatConverterPreflightRejectsUnsupportedCapability_data();
    void formatConverterPreflightRejectsUnsupportedCapability();
    void formatConverterPreflightResolvesLosslessBitrateMode_data();
    void formatConverterPreflightResolvesLosslessBitrateMode();
    void formatConverterPreflightRejectsBlockedPreservedOutputParent();
    void formatConverterPreflightRejectsInvalidImplicitOutputParent();
    void formatConverterAskPolicyRequiresConflictConfirmationBeforeStarting();
    void formatConverterConfirmedPlanUsesAskOutputPath();
    void formatConverterConfirmedPlanUsesAutoNumberOutputPath();
    void formatConverterConfirmedPlanDoesNotRenumberAfterPreview();
    void formatConverterCreateCommitNeverReplacesExistingFile();
    void formatConverterCreateActionRejectsPublishRace();
    void formatConverterConfirmedPlanPreservesSkipAction();
    void formatConverterConfirmedPlanUsesFrozenQuality();
    void formatConverterSkipPolicyLeavesExistingOutputUntouched();
    void formatConverterExposesEveryPdfRequiredOutputFormat();
    void formatConverterAppliesRealCbrAndVbrModes();
    void formatConverterCancellationPreservesExistingOutput();
    void formatConverterExportsAndReopensEveryExposedFormat();
    void formatConverterRejectsUnsupportedParameterCombinations();
    void transcodeCapiReportsFailureDetail();
    void formatConverterConvertsAcrossDistinctChinesePaths();
    void formatConverterExportsPreservesMetadataAndAvoidsCollisions();
    void formatConverterReservesParallelOutputNames();
    void formatConverterOverwritePolicyReusesSafeTargets();
    void formatConverterCancellationFinalizesQueuedEntries();
    void formatConverterCancelsIndividualEntry();
    void formatConverterReportsIntermediateProgress();
    void formatConverterRunsOnlySelectedEntries();
    void formatConverterPendingPlanRunsOnlyCheckedEntries();
    void formatConverterRejectsPendingPlanWhenAFrozenTaskIsRemoved();
    void formatConverterRejectsReimportedFrozenTask();
    void formatConverterRejectsModifiedFrozenSource();
    void formatConverterWritesMetadataPlanToNewOutput();
    void metadataEditorWritesTags();
    void metadataEditorPreflightIsAsyncAndRequiresDecision();
    void metadataEditorAppliesUiPayloadToMixedContainerBatch();
    void filenameProcessorRenamesWithoutTouchingAudio();
    void filenameProcessorAppliesExactlyThePreviewedConflictPlan();
    void filenameProcessorKeepsLibraryPathsInSyncAcrossRenameAndUndo();
    void filenameProcessorPlansConflictsAndUndoes();
    void filenameProcessorUsesTwoStageTransactions();
    void filenameProcessorSanitizesWindowsReservedAndLongNames();
};

void AudioToolsEndToEndTest::
    formatConverterBuildPreflightUsesSmartProfilesAndRejectsInvalidCustomValues()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString input = temp.filePath(QStringLiteral("smart-profile.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(input, 120, 1));

    FormatConverter converter;
    converter.loadFiles({QUrl::fromLocalFile(input)});
    waitForConverterLoad(converter);

    converter.setSelectedFormat(QStringLiteral("mp3"));
    const QVariantMap recommended = converter.buildPreflight({
        {QStringLiteral("outputFormat"), QStringLiteral("mp3")},
        {QStringLiteral("preset"), QStringLiteral("recommended")},
        {QStringLiteral("bitRate"), 1000},
        {QStringLiteral("sampleRate"), 0},
        {QStringLiteral("channelLayout"), QString()},
    });
    QVERIFY(recommended.value(QStringLiteral("ready")).toBool());
    QCOMPARE(recommended.value(QStringLiteral("bitRate")).toInt(), 320000);
    QCOMPARE(recommended.value(QStringLiteral("quality")).toInt(), 85);
    QCOMPARE(recommended.value(QStringLiteral("bitrateMode")).toString(),
             QStringLiteral("cbr"));
    converter.rejectPendingPlan();

    const QVariantMap invalidCustom = converter.buildPreflight({
        {QStringLiteral("outputFormat"), QStringLiteral("mp3")},
        {QStringLiteral("preset"), QStringLiteral("custom")},
        {QStringLiteral("bitRate"), 1000},
        {QStringLiteral("bitrateMode"), QStringLiteral("cbr")},
    });
    QVERIFY(!invalidCustom.value(QStringLiteral("ready")).toBool());
    QVERIFY(!invalidCustom.value(QStringLiteral("reason")).toString().isEmpty());
    converter.rejectPendingPlan();

    converter.setSelectedFormat(QStringLiteral("opus"));
    const QVariantMap opus = converter.buildPreflight({
        {QStringLiteral("outputFormat"), QStringLiteral("opus")},
        {QStringLiteral("preset"), QStringLiteral("recommended")},
        {QStringLiteral("sampleRate"), 44100},
    });
    QVERIFY(opus.value(QStringLiteral("ready")).toBool());
    QCOMPARE(opus.value(QStringLiteral("sampleRate")).toInt(), 48000);
    QCOMPARE(opus.value(QStringLiteral("bitRate")).toInt(), 192000);
    QCOMPARE(opus.value(QStringLiteral("bitrateMode")).toString(),
             QStringLiteral("vbr"));
    converter.rejectPendingPlan();
}

void AudioToolsEndToEndTest::formatConverterPreflightRejectsEmptySelectionOnce()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString input = temp.filePath(QStringLiteral("empty-selection.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(input, 120, 1));

    FormatConverter converter;
    converter.loadFiles({QUrl::fromLocalFile(input)});
    waitForConverterLoad(converter);
    QVERIFY(converter.buildPreflight({
        {QStringLiteral("outputFormat"), QStringLiteral("flac")},
        {QStringLiteral("outputDir"), temp.filePath(QStringLiteral("out"))}})
                .value(QStringLiteral("ready")).toBool());
    converter.setAllVisibleChecked(false);
    QCOMPARE(converter.checkedCount(), 0);

    QSignalSpy errors(&converter, &FormatConverter::errorOccurred);
    const QVariantMap plan = converter.buildPreflight({
        {QStringLiteral("outputFormat"), QStringLiteral("flac")},
        {QStringLiteral("outputDir"), temp.filePath(QStringLiteral("out"))}});

    QVERIFY(!plan.value(QStringLiteral("ready")).toBool());
    QCOMPARE(plan.value(QStringLiteral("taskCount")).toInt(), 0);
    QCOMPARE(plan.value(QStringLiteral("reason")).toString(),
             QStringLiteral("没有已选择的转换任务"));
    QCOMPARE(errors.count(), 1);
    QCOMPARE(errors.first().first().toString(),
             QStringLiteral("没有已选择的转换任务"));
    QVERIFY(converter.pendingPlan().isEmpty());

    converter.confirmPendingPlan();
    QCOMPARE(errors.count(), 1);
    QVERIFY(!converter.busy());
}
    QVERIFY(agplayer::test::writeClickTrackWav(input, 120, 1));

    FormatConverter converter;
    converter.loadFiles({QUrl::fromLocalFile(input)});
    waitForConverterLoad(converter);
<<<<<<< HEAD

    converter.setSelectedFormat(QStringLiteral("mp3"));
    const QVariantMap recommended = converter.buildPreflight({
        {QStringLiteral("outputFormat"), QStringLiteral("mp3")},
        {QStringLiteral("preset"), QStringLiteral("recommended")},
        {QStringLiteral("bitRate"), 1000},
        {QStringLiteral("sampleRate"), 0},
        {QStringLiteral("channelLayout"), QString()},
    });
    QVERIFY(recommended.value(QStringLiteral("ready")).toBool());
    QCOMPARE(recommended.value(QStringLiteral("bitRate")).toInt(), 320000);
    QCOMPARE(recommended.value(QStringLiteral("quality")).toInt(), 85);
    QCOMPARE(recommended.value(QStringLiteral("bitrateMode")).toString(),
             QStringLiteral("cbr"));
    converter.rejectPendingPlan();

    const QVariantMap invalidCustom = converter.buildPreflight({
        {QStringLiteral("outputFormat"), QStringLiteral("mp3")},
        {QStringLiteral("preset"), QStringLiteral("custom")},
        {QStringLiteral("bitRate"), 1000},
        {QStringLiteral("bitrateMode"), QStringLiteral("cbr")},
    });
    QVERIFY(!invalidCustom.value(QStringLiteral("ready")).toBool());
    QVERIFY(!invalidCustom.value(QStringLiteral("reason")).toString().isEmpty());
    converter.rejectPendingPlan();

    converter.setSelectedFormat(QStringLiteral("opus"));
    const QVariantMap opus = converter.buildPreflight({
        {QStringLiteral("outputFormat"), QStringLiteral("opus")},
        {QStringLiteral("preset"), QStringLiteral("recommended")},
        {QStringLiteral("sampleRate"), 44100},
    });
    QVERIFY(opus.value(QStringLiteral("ready")).toBool());
    QCOMPARE(opus.value(QStringLiteral("sampleRate")).toInt(), 48000);
    QCOMPARE(opus.value(QStringLiteral("bitRate")).toInt(), 192000);
    QCOMPARE(opus.value(QStringLiteral("bitrateMode")).toString(),
             QStringLiteral("vbr"));
    converter.rejectPendingPlan();
=======
    QVERIFY(converter.buildPreflight({
        {QStringLiteral("outputFormat"), QStringLiteral("flac")},
        {QStringLiteral("outputDir"), temp.filePath(QStringLiteral("out"))}})
                .value(QStringLiteral("ready")).toBool());
    converter.setAllVisibleChecked(false);
    QCOMPARE(converter.checkedCount(), 0);

    QSignalSpy errors(&converter, &FormatConverter::errorOccurred);
    const QVariantMap plan = converter.buildPreflight({
        {QStringLiteral("outputFormat"), QStringLiteral("flac")},
        {QStringLiteral("outputDir"), temp.filePath(QStringLiteral("out"))}});

    QVERIFY(!plan.value(QStringLiteral("ready")).toBool());
    QCOMPARE(plan.value(QStringLiteral("taskCount")).toInt(), 0);
    QCOMPARE(plan.value(QStringLiteral("reason")).toString(),
             QStringLiteral("没有已选择的转换任务"));
    QCOMPARE(errors.count(), 1);
    QCOMPARE(errors.first().first().toString(),
             QStringLiteral("没有已选择的转换任务"));
    QVERIFY(converter.pendingPlan().isEmpty());

    converter.confirmPendingPlan();
    QCOMPARE(errors.count(), 1);
    QVERIFY(!converter.busy());
>>>>>>> 93fafe5 (fix: close format preflight safety gaps)
}

void AudioToolsEndToEndTest::formatConverterReportsReferenceTaskColumns()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString input = temp.filePath(QStringLiteral("columns.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(input, 120, 1));

    FormatConverter converter;
    const QVariantList supported = converter.supportedOutputFormats();
    QVERIFY(!supported.isEmpty());
    bool hasFlac = false;
    for (const QVariant& value : supported) {
        const QVariantMap format = value.toMap();
        QVERIFY(!format.value(QStringLiteral("key")).toString().isEmpty());
        QVERIFY(!format.value(QStringLiteral("label")).toString().isEmpty());
        const QByteArray codec = format.value(QStringLiteral("codec")).toString().toUtf8();
        const bool available = ag_encoder_available(codec.constData()) != 0;
        QCOMPARE(format.value(QStringLiteral("available")).toBool(), available);
        QVERIFY(!format.value(QStringLiteral("encoderLabel")).toString().isEmpty());
        if (!available) {
            QVERIFY(!format.value(QStringLiteral("reason")).toString().isEmpty());
        }
        hasFlac = hasFlac
            || format.value(QStringLiteral("key")).toString() == QStringLiteral("flac");
    }
    QVERIFY(hasFlac);
    converter.loadFiles({QUrl::fromLocalFile(input)});
    waitForConverterLoad(converter);
    QCOMPARE(converter.fileCount(), 1);
    const QVariantMap row = converter.files().first().toMap();
    QVERIFY(row.value(QStringLiteral("sampleRate")).toInt() > 0);
    QVERIFY(row.value(QStringLiteral("bitRate")).toLongLong() > 0);
    QVERIFY(row.value(QStringLiteral("channels")).toInt() > 0);
    QCOMPARE(row.value(QStringLiteral("progress")).toDouble(), 0.0);
    QVERIFY(row.contains(QStringLiteral("outputFormat")));
    QVERIFY(row.contains(QStringLiteral("outputPath")));
}

void AudioToolsEndToEndTest::formatConverterExposesQueueFacadeModelsAndPreflight()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString input = temp.filePath(QStringLiteral("facade.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(input, 120, 1));

    FormatConverter converter;
    QVERIFY(converter.taskModel() != nullptr);
    QVERIFY(converter.filteredTaskModel() != nullptr);
    converter.addPlaylistPaths({input, input});
    waitForConverterLoad(converter);
    QCOMPARE(converter.taskModel()->rowCount(), 1);
    QCOMPARE(converter.checkedCount(), 1);

    converter.setSelectedFormat(QStringLiteral("flac"));
    QCOMPARE(converter.currentCapability()
                 .value(QStringLiteral("key")).toString(),
             QStringLiteral("flac"));
    const QVariantMap plan = converter.buildPreflight({
        {QStringLiteral("outputFormat"), QStringLiteral("flac")},
        {QStringLiteral("outputDir"), temp.filePath(QStringLiteral("out"))},
        {QStringLiteral("requiresConfirmation"), true}});
    QCOMPARE(plan.value(QStringLiteral("taskCount")).toInt(), 1);
    QVERIFY(plan.value(QStringLiteral("ready")).toBool());
    QVERIFY(!converter.pendingPlan().isEmpty());
    converter.rejectPendingPlan();
    QVERIFY(converter.pendingPlan().isEmpty());

    converter.removeChecked();
    QCOMPARE(converter.taskModel()->rowCount(), 0);
}

void AudioToolsEndToEndTest::
    formatConverterPreflightReturnsResolvedProfileBeforeStarting()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString input = temp.filePath(QStringLiteral("preflight.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(input, 120, 1));

    FormatConverter converter;
    converter.loadFiles({QUrl::fromLocalFile(input)});
    waitForConverterLoad(converter);

    const QVariantMap profile = converter.previewSelected(
        {0}, QStringLiteral("mp3"), 192000, 44100, 2, temp.path(), false);
    QVERIFY(profile.value(QStringLiteral("ready")).toBool());
    QVERIFY(!profile.value(QStringLiteral("requiresConfirmation")).toBool());
    QVERIFY(!profile.value(QStringLiteral("resolvedProfile")).toMap().isEmpty());
    QCOMPARE(converter.files().first().toMap()
                 .value(QStringLiteral("status")).toString(),
             QStringLiteral("Ready"));

    const QVariantMap opusProfile = converter.previewSelected(
        {0}, QStringLiteral("opus"), 192000, 44100, 2, temp.path(), false);
    QVERIFY(opusProfile.value(QStringLiteral("ready")).toBool());
    QVERIFY(opusProfile.value(QStringLiteral("requiresConfirmation")).toBool());
    QCOMPARE(opusProfile.value(QStringLiteral("resolvedProfile")).toMap()
                 .value(QStringLiteral("sampleRate")).toInt(),
             48000);
    QCOMPARE(converter.files().first().toMap()
                 .value(QStringLiteral("status")).toString(),
             QStringLiteral("PendingConfirmation"));
    QVERIFY(!converter.busy());
}

void AudioToolsEndToEndTest::
    formatConverterBuildPreflightFreezesResolvedSelectedTasks()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString outputDir = temp.filePath(QStringLiteral("converted"));
    QVERIFY(QDir().mkpath(outputDir));
    QList<QUrl> inputs;
    for (int index = 0; index < 3; ++index) {
        const QString path = temp.filePath(
            QStringLiteral("preflight-%1.wav").arg(index));
        QVERIFY(agplayer::test::writeClickTrackWav(path, 120 + index, 1));
        inputs.push_back(QUrl::fromLocalFile(path));
    }

    FormatConverter converter;
    converter.loadFiles(inputs);
    waitForConverterLoad(converter);
    const QString firstId = converter.taskModel()->index(0, 0)
                                .data(Qt::UserRole + 1).toString();
    const QString uncheckedId = converter.taskModel()->index(1, 0)
                                    .data(Qt::UserRole + 1).toString();
    const QString thirdId = converter.taskModel()->index(2, 0)
                                .data(Qt::UserRole + 1).toString();
    converter.setTaskChecked(uncheckedId, false);

    const QVariantMap plan = converter.buildPreflight({
        {QStringLiteral("outputFormat"), QStringLiteral("FLAC")},
        {QStringLiteral("outputDir"), outputDir},
        {QStringLiteral("conflictPolicy"), QStringLiteral("auto-number")},
        {QStringLiteral("keepMetadata"), true},
        {QStringLiteral("keepCover"), false},
        {QStringLiteral("preserveDirectories"), true},
        {QStringLiteral("extractAudio"), false}});
    QVERIFY(plan.value(QStringLiteral("ready")).toBool());
    QCOMPARE(plan.value(QStringLiteral("outputFormat")).toString(),
             QStringLiteral("flac"));
    QCOMPARE(plan.value(QStringLiteral("taskCount")).toInt(), 2);
    QVERIFY(plan.value(QStringLiteral("requiresConfirmation")).toBool());

    const QVariantList tasks = plan.value(QStringLiteral("tasks")).toList();
    QCOMPARE(tasks.size(), 2);
    QCOMPARE(tasks.at(0).toMap().value(QStringLiteral("taskId")).toString(),
             firstId);
    QCOMPARE(tasks.at(1).toMap().value(QStringLiteral("taskId")).toString(),
             thirdId);
    QCOMPARE(QDir::cleanPath(tasks.at(0).toMap()
                 .value(QStringLiteral("outputPath")).toString()),
             QDir::cleanPath(QDir(outputDir).filePath(
                 QStringLiteral("preflight-0.flac"))));
    QCOMPARE(QDir::cleanPath(tasks.at(1).toMap()
                 .value(QStringLiteral("outputPath")).toString()),
             QDir::cleanPath(QDir(outputDir).filePath(
                 QStringLiteral("preflight-2.flac"))));
    const QVariantMap resolved = tasks.at(0).toMap()
                                     .value(QStringLiteral("resolvedProfile"))
                                     .toMap();
    QCOMPARE(resolved.value(QStringLiteral("format")).toString(),
             QStringLiteral("flac"));
    QVERIFY(resolved.value(QStringLiteral("keepMetadata")).toBool());
    QVERIFY(!resolved.value(QStringLiteral("keepCover")).toBool());
    QVERIFY(!converter.busy());
}

void AudioToolsEndToEndTest::
    formatConverterPreflightRejectsVideoWhenExtractionIsDisabled()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString wav = temp.filePath(QStringLiteral("video-source.wav"));
    const QString video = temp.filePath(QStringLiteral("video-source.mp4"));
    QVERIFY(agplayer::test::writeClickTrackWav(wav, 120, 1));
    QVERIFY(QFile::copy(wav, video));

    FormatConverter converter;
    converter.loadFiles({QUrl::fromLocalFile(video)});
    waitForConverterLoad(converter);

    const QVariantMap plan = converter.buildPreflight({
        {QStringLiteral("outputFormat"), QStringLiteral("flac")},
        {QStringLiteral("outputDir"), temp.filePath(QStringLiteral("out"))},
        {QStringLiteral("extractAudio"), false}});
    QVERIFY(!plan.value(QStringLiteral("ready")).toBool());
    QVERIFY(plan.value(QStringLiteral("reason")).toString().contains(
        QStringLiteral("Video input requires audio extraction")));
    QVERIFY(converter.pendingPlan().isEmpty());
}

void AudioToolsEndToEndTest::formatConverterPreflightResolvesOpusSampleRate()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString input = temp.filePath(QStringLiteral("opus-plan.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(input, 120, 1));

    FormatConverter converter;
    converter.loadFiles({QUrl::fromLocalFile(input)});
    waitForConverterLoad(converter);
    const QVariantMap plan = converter.buildPreflight({
        {QStringLiteral("outputFormat"), QStringLiteral("opus")},
        {QStringLiteral("outputDir"), temp.filePath(QStringLiteral("out"))},
        {QStringLiteral("bitRate"), 128000},
        {QStringLiteral("sampleRate"), 44100}});

    QVERIFY(plan.value(QStringLiteral("ready")).toBool());
    QVERIFY(plan.value(QStringLiteral("requiresConfirmation")).toBool());
    const QVariantMap task = plan.value(QStringLiteral("tasks"))
                                 .toList().first().toMap();
    QCOMPARE(task.value(QStringLiteral("resolvedProfile")).toMap()
                 .value(QStringLiteral("sampleRate")).toInt(),
             48000);
    const QVariantList differences = task.value(
        QStringLiteral("differences")).toList();
    QVERIFY(std::any_of(differences.cbegin(), differences.cend(),
        [](const QVariant& value) {
            return value.toMap().value(QStringLiteral("field")).toString()
                == QStringLiteral("sampleRate");
        }));
}

void AudioToolsEndToEndTest::formatConverterPreflightRejectsInvalidRequest_data()
{
    QTest::addColumn<QVariantMap>("request");
    QTest::addColumn<QString>("reasonPart");

    const QVariantMap base{
        {QStringLiteral("outputFormat"), QStringLiteral("mp3")},
        {QStringLiteral("bitRate"), 128000}};
    auto withValue = [base](const QString& key, const QVariant& value) {
        QVariantMap request = base;
        request.insert(key, value);
        return request;
    };
    QTest::newRow("conflict-policy")
        << withValue(QStringLiteral("conflictPolicy"), QStringLiteral("merge"))
        << QStringLiteral("conflictPolicy");
    QTest::newRow("output-format")
        << withValue(QStringLiteral("outputFormat"), QStringLiteral("xyz"))
        << QStringLiteral("outputFormat");
    QTest::newRow("bitrate-mode")
        << withValue(QStringLiteral("bitrateMode"), QStringLiteral("abr"))
        << QStringLiteral("bitrateMode");
    QTest::newRow("bitrate-low")
        << withValue(QStringLiteral("bitRate"), 7999)
        << QStringLiteral("bitRate");
    QTest::newRow("sample-rate-low")
        << withValue(QStringLiteral("sampleRate"), 7999)
        << QStringLiteral("sampleRate");
    QTest::newRow("channel-layout")
        << withValue(QStringLiteral("channelLayout"), QStringLiteral("7.1"))
        << QStringLiteral("channelLayout");
    QTest::newRow("sample-format")
        << withValue(QStringLiteral("sampleFormat"), QStringLiteral("bogus"))
        << QStringLiteral("sampleFormat");
    QTest::newRow("quality-low")
        << withValue(QStringLiteral("quality"), -1)
        << QStringLiteral("quality");
    QTest::newRow("quality-high")
        << withValue(QStringLiteral("quality"), 101)
        << QStringLiteral("quality");
    QTest::newRow("channels")
        << withValue(QStringLiteral("channels"), 3)
        << QStringLiteral("channels");
    QTest::newRow("audio-stream")
        << withValue(QStringLiteral("audioStreamIndex"), -2)
        << QStringLiteral("audioStreamIndex");
    QTest::newRow("codec")
        << withValue(QStringLiteral("codecName"), QStringLiteral("not-a-codec"))
        << QStringLiteral("codecName");
    QTest::newRow("output-directory")
        << withValue(QStringLiteral("outputDir"), QStringLiteral("__file__"))
        << QStringLiteral("outputDir");
    QTest::newRow("output-directory-file-ancestor")
        << withValue(QStringLiteral("outputDir"),
                     QStringLiteral("__file_child__"))
        << QStringLiteral("outputDir");
#ifdef Q_OS_WIN
    QTest::newRow("output-directory-uncreatable")
        << withValue(QStringLiteral("outputDir"),
                     QStringLiteral("__uncreatable__"))
        << QStringLiteral("outputDir");
#endif
}

void AudioToolsEndToEndTest::formatConverterPreflightRejectsInvalidRequest()
{
    QFETCH(QVariantMap, request);
    QFETCH(QString, reasonPart);
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString input = temp.filePath(QStringLiteral("invalid-request.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(input, 120, 1));
    const QString outputDirectoryToken = request.value(
        QStringLiteral("outputDir")).toString();
    if (outputDirectoryToken == QStringLiteral("__file__")
        || outputDirectoryToken == QStringLiteral("__file_child__")) {
        const QString filePath = temp.filePath(QStringLiteral("not-a-directory"));
        QFile file(filePath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("file");
        file.close();
        request.insert(QStringLiteral("outputDir"),
            outputDirectoryToken == QStringLiteral("__file_child__")
                ? QDir(filePath).filePath(QStringLiteral("child"))
                : filePath);
#ifdef Q_OS_WIN
    } else if (outputDirectoryToken == QStringLiteral("__uncreatable__")) {
        request.insert(QStringLiteral("outputDir"),
            temp.filePath(QStringLiteral("invalid<component/child")));
#endif
    } else {
        request.insert(QStringLiteral("outputDir"),
                       temp.filePath(QStringLiteral("out")));
    }

    FormatConverter converter;
    converter.loadFiles({QUrl::fromLocalFile(input)});
    waitForConverterLoad(converter);
    QSignalSpy errors(&converter, &FormatConverter::errorOccurred);
    const QVariantMap plan = converter.buildPreflight(request);

    QVERIFY(!plan.value(QStringLiteral("ready")).toBool());
    QVERIFY(plan.value(QStringLiteral("reason")).toString().contains(
        reasonPart));
    QCOMPARE(errors.count(), 1);
    QVERIFY(converter.pendingPlan().isEmpty());
}

void AudioToolsEndToEndTest::
    formatConverterPreflightRejectsUnsupportedCapability_data()
{
    QTest::addColumn<QString>("format");
    QTest::addColumn<bool>("keepMetadata");
    QTest::addColumn<bool>("keepCover");
    QTest::addColumn<QString>("reasonPart");

    QTest::newRow("aac-metadata")
        << QStringLiteral("aac") << true << false
        << QStringLiteral("keepMetadata");
    QTest::newRow("aac-cover")
        << QStringLiteral("aac") << false << true
        << QStringLiteral("keepCover");
    QTest::newRow("wav-cover")
        << QStringLiteral("wav") << true << true
        << QStringLiteral("keepCover");
}

void AudioToolsEndToEndTest::
    formatConverterPreflightRejectsUnsupportedCapability()
{
    QFETCH(QString, format);
    QFETCH(bool, keepMetadata);
    QFETCH(bool, keepCover);
    QFETCH(QString, reasonPart);
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString input = temp.filePath(QStringLiteral("capability.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(input, 120, 1));

    FormatConverter converter;
    converter.loadFiles({QUrl::fromLocalFile(input)});
    waitForConverterLoad(converter);
    QSignalSpy errors(&converter, &FormatConverter::errorOccurred);
    const QVariantMap plan = converter.buildPreflight({
        {QStringLiteral("outputFormat"), format},
        {QStringLiteral("outputDir"), temp.filePath(QStringLiteral("out"))},
        {QStringLiteral("bitRate"), 128000},
        {QStringLiteral("keepMetadata"), keepMetadata},
        {QStringLiteral("keepCover"), keepCover}});

    QVERIFY(!plan.value(QStringLiteral("ready")).toBool());
    QVERIFY(plan.value(QStringLiteral("reason")).toString().contains(reasonPart));
    QCOMPARE(errors.count(), 1);
    QVERIFY(converter.pendingPlan().isEmpty());
}

void AudioToolsEndToEndTest::
    formatConverterPreflightResolvesLosslessBitrateMode_data()
{
    QTest::addColumn<QString>("format");
    QTest::newRow("flac") << QStringLiteral("flac");
    QTest::newRow("alac") << QStringLiteral("alac");
}

void AudioToolsEndToEndTest::
    formatConverterPreflightResolvesLosslessBitrateMode()
{
    QFETCH(QString, format);
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString input = temp.filePath(QStringLiteral("lossless-mode.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(input, 120, 1));

    FormatConverter converter;
    converter.loadFiles({QUrl::fromLocalFile(input)});
    waitForConverterLoad(converter);
    const QVariantMap plan = converter.buildPreflight({
        {QStringLiteral("outputFormat"), format},
        {QStringLiteral("outputDir"), temp.filePath(QStringLiteral("out"))},
        {QStringLiteral("bitrateMode"), QStringLiteral("vbr")},
        {QStringLiteral("keepMetadata"), true},
        {QStringLiteral("keepCover"), false}});

    QVERIFY(plan.value(QStringLiteral("ready")).toBool());
    QVERIFY(plan.value(QStringLiteral("requiresConfirmation")).toBool());
    const QVariantMap task = plan.value(QStringLiteral("tasks"))
                                 .toList().first().toMap();
    QCOMPARE(task.value(QStringLiteral("resolvedProfile")).toMap()
                 .value(QStringLiteral("bitrateMode")).toString(),
             QString());
    const QVariantList differences = task.value(
        QStringLiteral("differences")).toList();
    const auto difference = std::find_if(
        differences.cbegin(), differences.cend(), [](const QVariant& value) {
            return value.toMap().value(QStringLiteral("field")).toString()
                == QStringLiteral("bitrateMode");
        });
    QVERIFY(difference != differences.cend());
    QCOMPARE(difference->toMap().value(QStringLiteral("requested")).toString(),
             QStringLiteral("vbr"));
    QCOMPARE(difference->toMap().value(QStringLiteral("resolved")).toString(),
             QString());
    QVERIFY(difference->toMap()
                .value(QStringLiteral("requiresConfirmation")).toBool());
}

void AudioToolsEndToEndTest::
    formatConverterPreflightRejectsBlockedPreservedOutputParent()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString inputRoot = temp.filePath(QStringLiteral("input"));
    const QString inputAlbum = QDir(inputRoot).filePath(QStringLiteral("album"));
    const QString input = QDir(inputAlbum).filePath(QStringLiteral("song.wav"));
    const QString outputRoot = temp.filePath(QStringLiteral("output"));
    QVERIFY(QDir().mkpath(inputAlbum));
    QVERIFY(QDir().mkpath(outputRoot));
    QVERIFY(agplayer::test::writeClickTrackWav(input, 120, 1));
    QFile blocked(QDir(outputRoot).filePath(QStringLiteral("album")));
    QVERIFY(blocked.open(QIODevice::WriteOnly));
    QCOMPARE(blocked.write("not-a-directory"), 15);
    blocked.close();

    FormatConverter converter;
    converter.loadFiles({QUrl::fromLocalFile(inputRoot)});
    waitForConverterLoad(converter);
    QSignalSpy errors(&converter, &FormatConverter::errorOccurred);
    const QVariantMap plan = converter.buildPreflight({
        {QStringLiteral("outputFormat"), QStringLiteral("flac")},
        {QStringLiteral("outputDir"), outputRoot},
        {QStringLiteral("preserveDirectories"), true}});

    QVERIFY(!plan.value(QStringLiteral("ready")).toBool());
    QCOMPARE(plan.value(QStringLiteral("taskCount")).toInt(), 0);
    QVERIFY(plan.value(QStringLiteral("reason")).toString().contains(
        QStringLiteral("output"), Qt::CaseInsensitive));
    QCOMPARE(errors.count(), 1);
    QVERIFY(converter.pendingPlan().isEmpty());
    converter.confirmPendingPlan();
    QCOMPARE(errors.count(), 1);
    QVERIFY(!converter.busy());
}

void AudioToolsEndToEndTest::
    formatConverterPreflightRejectsInvalidImplicitOutputParent()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString inputParent = temp.filePath(QStringLiteral("implicit-parent"));
    const QString input = QDir(inputParent).filePath(QStringLiteral("song.wav"));
    QVERIFY(QDir().mkpath(inputParent));
    QVERIFY(agplayer::test::writeClickTrackWav(input, 120, 1));

    FormatConverter converter;
    converter.loadFiles({QUrl::fromLocalFile(input)});
    waitForConverterLoad(converter);
    QVERIFY(QFile::remove(input));
    QVERIFY(QDir().rmdir(inputParent));
    QFile blockedParent(inputParent);
    QVERIFY(blockedParent.open(QIODevice::WriteOnly));
    QCOMPARE(blockedParent.write("not-a-directory"), 15);
    blockedParent.close();

    QSignalSpy errors(&converter, &FormatConverter::errorOccurred);
    const QVariantMap plan = converter.buildPreflight({
        {QStringLiteral("outputFormat"), QStringLiteral("flac")},
        {QStringLiteral("outputDir"), QString()}});

    QVERIFY(!plan.value(QStringLiteral("ready")).toBool());
    QCOMPARE(plan.value(QStringLiteral("taskCount")).toInt(), 0);
    QVERIFY(plan.value(QStringLiteral("reason")).toString().contains(
        QStringLiteral("output"), Qt::CaseInsensitive));
    QCOMPARE(errors.count(), 1);
    QVERIFY(converter.pendingPlan().isEmpty());
    converter.confirmPendingPlan();
    QCOMPARE(errors.count(), 1);
    QVERIFY(!converter.busy());
}

void AudioToolsEndToEndTest::
    formatConverterAskPolicyRequiresConflictConfirmationBeforeStarting()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString input = temp.filePath(QStringLiteral("ask-conflict.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(input, 120, 1));
    const QString outputDir = temp.filePath(QStringLiteral("converted"));
    QVERIFY(QDir().mkpath(outputDir));
    QFile existing(QDir(outputDir).filePath(QStringLiteral("ask-conflict.flac")));
    QVERIFY(existing.open(QIODevice::WriteOnly));
    QCOMPARE(existing.write("existing-output"), 15);
    existing.close();

    FormatConverter converter;
    converter.loadFiles({QUrl::fromLocalFile(input)});
    waitForConverterLoad(converter);

    const QVariantMap plan = converter.buildPreflight({
        {QStringLiteral("outputFormat"), QStringLiteral("flac")},
        {QStringLiteral("outputDir"), outputDir},
        {QStringLiteral("conflictPolicy"), QStringLiteral("ask")},
        {QStringLiteral("extractAudio"), false}});
    QVERIFY(plan.value(QStringLiteral("ready")).toBool());
    QVERIFY(plan.value(QStringLiteral("requiresConfirmation")).toBool());
    QCOMPARE(plan.value(QStringLiteral("conflictCount")).toInt(), 1);
    const QVariantList tasks = plan.value(QStringLiteral("tasks")).toList();
    QCOMPARE(tasks.size(), 1);
    const QVariantList differences = tasks.first().toMap()
                                         .value(QStringLiteral("differences"))
                                         .toList();
    const auto conflictDifference = std::find_if(
        differences.cbegin(), differences.cend(), [](const QVariant& value) {
            return value.toMap().value(QStringLiteral("field")).toString()
                == QStringLiteral("conflict");
        });
    QVERIFY(conflictDifference != differences.cend());
    QVERIFY(conflictDifference->toMap()
                .value(QStringLiteral("requiresConfirmation")).toBool());
    QVERIFY(!converter.busy());
}

void AudioToolsEndToEndTest::formatConverterConfirmedPlanUsesAskOutputPath()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString input = temp.filePath(QStringLiteral("ask-run.wav"));
    const QString outputDir = temp.filePath(QStringLiteral("out"));
    QVERIFY(agplayer::test::writeClickTrackWav(input, 120, 1));
    QVERIFY(QDir().mkpath(outputDir));
    const QString target = QDir(outputDir).filePath(QStringLiteral("ask-run.flac"));
    QFile existing(target);
    QVERIFY(existing.open(QIODevice::WriteOnly));
    QCOMPARE(existing.write("existing"), 8);
    existing.close();

    FormatConverter converter;
    converter.loadFiles({QUrl::fromLocalFile(input)});
    waitForConverterLoad(converter);
    const QVariantMap plan = converter.buildPreflight({
        {QStringLiteral("outputFormat"), QStringLiteral("flac")},
        {QStringLiteral("outputDir"), outputDir},
        {QStringLiteral("conflictPolicy"), QStringLiteral("ask")}});
    const QVariantMap plannedTask = plan.value(QStringLiteral("tasks"))
                                        .toList().first().toMap();
    QCOMPARE(plannedTask.value(QStringLiteral("outputPath")).toString(), target);
    QCOMPARE(plannedTask.value(QStringLiteral("action")).toString(),
             QStringLiteral("overwrite"));
    QSignalSpy completed(&converter, &FormatConverter::transcodeCompleted);
    converter.confirmPendingPlan();
    QVERIFY(completed.wait(30000));

    QCOMPARE(completed.first().first().toInt(), 1);
    QCOMPARE(converter.files().first().toMap()
                 .value(QStringLiteral("outputPath")).toString(), target);
    verifyAudioFile(target);
    QVERIFY(!QFileInfo::exists(QDir(outputDir).filePath(
        QStringLiteral("ask-run_1.flac"))));
}

void AudioToolsEndToEndTest::
    formatConverterConfirmedPlanUsesAutoNumberOutputPath()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString input = temp.filePath(QStringLiteral("auto-run.wav"));
    const QString outputDir = temp.filePath(QStringLiteral("out"));
    QVERIFY(agplayer::test::writeClickTrackWav(input, 120, 1));
    QVERIFY(QDir().mkpath(outputDir));
    QFile base(QDir(outputDir).filePath(QStringLiteral("auto-run.flac")));
    QVERIFY(base.open(QIODevice::WriteOnly));
    base.write("base");
    base.close();

    FormatConverter converter;
    converter.loadFiles({QUrl::fromLocalFile(input)});
    waitForConverterLoad(converter);
    const QVariantMap plan = converter.buildPreflight({
        {QStringLiteral("outputFormat"), QStringLiteral("flac")},
        {QStringLiteral("outputDir"), outputDir},
        {QStringLiteral("conflictPolicy"), QStringLiteral("auto-number")}});
    const QString plannedPath = plan.value(QStringLiteral("tasks"))
                                    .toList().first().toMap()
                                    .value(QStringLiteral("outputPath")).toString();
    QCOMPARE(plan.value(QStringLiteral("tasks")).toList().first().toMap()
                 .value(QStringLiteral("action")).toString(),
             QStringLiteral("create"));
    QCOMPARE(QFileInfo(plannedPath).fileName(), QStringLiteral("auto-run_1.flac"));
    QSignalSpy completed(&converter, &FormatConverter::transcodeCompleted);
    converter.confirmPendingPlan();
    QVERIFY(completed.wait(30000));

    QCOMPARE(completed.first().first().toInt(), 1);
    QCOMPARE(converter.files().first().toMap()
                 .value(QStringLiteral("outputPath")).toString(), plannedPath);
    verifyAudioFile(plannedPath);
}

void AudioToolsEndToEndTest::
    formatConverterConfirmedPlanDoesNotRenumberAfterPreview()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString input = temp.filePath(QStringLiteral("numbered.wav"));
    const QString outputDir = temp.filePath(QStringLiteral("out"));
    QVERIFY(agplayer::test::writeClickTrackWav(input, 120, 1));
    QVERIFY(QDir().mkpath(outputDir));
    QFile base(QDir(outputDir).filePath(QStringLiteral("numbered.flac")));
    QVERIFY(base.open(QIODevice::WriteOnly));
    base.write("base");
    base.close();

    FormatConverter converter;
    converter.loadFiles({QUrl::fromLocalFile(input)});
    waitForConverterLoad(converter);
    const QVariantMap plan = converter.buildPreflight({
        {QStringLiteral("outputFormat"), QStringLiteral("flac")},
        {QStringLiteral("outputDir"), outputDir},
        {QStringLiteral("conflictPolicy"), QStringLiteral("auto-number")}});
    const QString plannedPath = plan.value(QStringLiteral("tasks"))
                                    .toList().first().toMap()
                                    .value(QStringLiteral("outputPath")).toString();
    QCOMPARE(QFileInfo(plannedPath).fileName(), QStringLiteral("numbered_1.flac"));
    QFile raced(plannedPath);
    QVERIFY(raced.open(QIODevice::WriteOnly));
    QCOMPARE(raced.write("race"), 4);
    raced.close();

    QSignalSpy errors(&converter, &FormatConverter::errorOccurred);
    QSignalSpy completed(&converter, &FormatConverter::transcodeCompleted);
    converter.confirmPendingPlan();
    QTRY_VERIFY_WITH_TIMEOUT(!converter.busy(), 30000);
    QCOMPARE(errors.count(), 1);
    QCOMPARE(completed.count(), 0);
    QVERIFY(!QFileInfo::exists(QDir(outputDir).filePath(
        QStringLiteral("numbered_2.flac"))));
    QVERIFY(raced.open(QIODevice::ReadOnly));
    QCOMPARE(raced.readAll(), QByteArrayLiteral("race"));
}

void AudioToolsEndToEndTest::formatConverterCreateActionRejectsPublishRace()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString input = temp.filePath(QStringLiteral("publish-race.wav"));
    const QString outputDir = temp.filePath(QStringLiteral("out"));
    QVERIFY(agplayer::test::writeClickTrackWav(input, 120, 60));
    QVERIFY(QDir().mkpath(outputDir));

    FormatConverter converter;
    converter.loadFiles({QUrl::fromLocalFile(input)});
    waitForConverterLoad(converter);
    const QVariantMap plan = converter.buildPreflight({
        {QStringLiteral("outputFormat"), QStringLiteral("flac")},
        {QStringLiteral("outputDir"), outputDir},
        {QStringLiteral("conflictPolicy"), QStringLiteral("auto-number")}});
    QVERIFY(plan.value(QStringLiteral("ready")).toBool());
    const QVariantMap task = plan.value(QStringLiteral("tasks"))
                                 .toList().first().toMap();
    QCOMPARE(task.value(QStringLiteral("action")).toString(),
             QStringLiteral("create"));
    const QString target = task.value(QStringLiteral("outputPath")).toString();
    QVERIFY(!QFileInfo::exists(target));

    bool foreignFileCreated = false;
    connect(&converter, &FormatConverter::progressChanged, this, [&] {
        if (foreignFileCreated || converter.progress() <= 0.0
            || converter.progress() >= 1.0) {
            return;
        }
        QFile foreign(target);
        QVERIFY(foreign.open(QIODevice::WriteOnly));
        QCOMPARE(foreign.write("foreign-owner"), 13);
        foreign.close();
        foreignFileCreated = true;
    }, Qt::QueuedConnection);

    QSignalSpy completed(&converter, &FormatConverter::transcodeCompleted);
    converter.confirmPendingPlan();
    QVERIFY(completed.wait(30000));
    QVERIFY2(foreignFileCreated,
             "Progress barrier did not create the competing output");
    QCOMPARE(completed.first().at(0).toInt(), 0);
    QCOMPARE(completed.first().at(1).toInt(), 1);
    QCOMPARE(converter.files().first().toMap()
                 .value(QStringLiteral("status")).toString(),
             QStringLiteral("Error"));
    QFile foreign(target);
    QVERIFY(foreign.open(QIODevice::ReadOnly));
    QCOMPARE(foreign.readAll(), QByteArrayLiteral("foreign-owner"));
}

void AudioToolsEndToEndTest::
    formatConverterCreateCommitNeverReplacesExistingFile()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString staged = temp.filePath(QStringLiteral("staged.flac"));
    const QString target = temp.filePath(QStringLiteral("target.flac"));
    QFile stagedFile(staged);
    QVERIFY(stagedFile.open(QIODevice::WriteOnly));
    QCOMPARE(stagedFile.write("converted-output"), 16);
    stagedFile.close();

    const bool committed = format_converter_detail::commit_staged_output(
        staged, target,
        format_converter_detail::OutputCommitMode::CreateNoReplace,
        [&] {
            QFile foreign(target);
            QVERIFY(foreign.open(QIODevice::WriteOnly));
            QCOMPARE(foreign.write("foreign-owner"), 13);
        });

    QVERIFY(!committed);
    QVERIFY(QFileInfo::exists(staged));
    QFile foreign(target);
    QVERIFY(foreign.open(QIODevice::ReadOnly));
    QCOMPARE(foreign.readAll(), QByteArrayLiteral("foreign-owner"));
}

void AudioToolsEndToEndTest::formatConverterConfirmedPlanPreservesSkipAction()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString input = temp.filePath(QStringLiteral("skip-plan.wav"));
    const QString outputDir = temp.filePath(QStringLiteral("out"));
    QVERIFY(agplayer::test::writeClickTrackWav(input, 120, 1));
    QVERIFY(QDir().mkpath(outputDir));
    const QString target = QDir(outputDir).filePath(QStringLiteral("skip-plan.flac"));
    QFile conflict(target);
    QVERIFY(conflict.open(QIODevice::WriteOnly));
    conflict.write("conflict");
    conflict.close();

    FormatConverter converter;
    converter.loadFiles({QUrl::fromLocalFile(input)});
    waitForConverterLoad(converter);
    const QVariantMap plan = converter.buildPreflight({
        {QStringLiteral("outputFormat"), QStringLiteral("flac")},
        {QStringLiteral("outputDir"), outputDir},
        {QStringLiteral("conflictPolicy"), QStringLiteral("skip")}});
    const QVariantMap plannedTask = plan.value(QStringLiteral("tasks"))
                                        .toList().first().toMap();
    QVERIFY(plannedTask.value(QStringLiteral("skipped")).toBool());
    QCOMPARE(plannedTask.value(QStringLiteral("action")).toString(),
             QStringLiteral("skip"));
    QVERIFY(QFile::remove(target));

    QSignalSpy completed(&converter, &FormatConverter::transcodeCompleted);
    converter.confirmPendingPlan();
    QVERIFY(completed.wait(30000));
    QCOMPARE(converter.files().first().toMap()
                 .value(QStringLiteral("status")).toString(),
             QStringLiteral("Skipped"));
    QVERIFY(!QFileInfo::exists(target));
}

void AudioToolsEndToEndTest::formatConverterConfirmedPlanUsesFrozenQuality()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString input = temp.filePath(QStringLiteral("quality.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(input, 120, 4));
    qint64 sizes[2]{};
    const int qualities[2]{10, 90};
    for (int index = 0; index < 2; ++index) {
        const QString outputDir = temp.filePath(
            QStringLiteral("quality-%1").arg(index));
        FormatConverter converter;
        converter.loadFiles({QUrl::fromLocalFile(input)});
        waitForConverterLoad(converter);
        const QVariantMap plan = converter.buildPreflight({
            {QStringLiteral("outputFormat"), QStringLiteral("mp3")},
            {QStringLiteral("outputDir"), outputDir},
            {QStringLiteral("bitRate"), 128000},
            {QStringLiteral("bitrateMode"), QStringLiteral("vbr")},
            {QStringLiteral("quality"), qualities[index]}});
        QCOMPARE(plan.value(QStringLiteral("tasks")).toList().first().toMap()
                     .value(QStringLiteral("resolvedProfile")).toMap()
                     .value(QStringLiteral("quality")).toInt(),
                 qualities[index]);
        QSignalSpy completed(&converter, &FormatConverter::transcodeCompleted);
        converter.confirmPendingPlan();
        QVERIFY(completed.wait(30000));
        const QString output = QDir(outputDir).filePath(
            QStringLiteral("quality.mp3"));
        verifyAudioFile(output);
        sizes[index] = QFileInfo(output).size();
    }
    QVERIFY2(sizes[0] != sizes[1],
             "Frozen VBR quality must change the encoded output");
}

void AudioToolsEndToEndTest::
    formatConverterSkipPolicyLeavesExistingOutputUntouched()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString input = temp.filePath(QStringLiteral("conflict.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(input, 120, 1));
    const QString outputDir = temp.filePath(QStringLiteral("converted"));
    QVERIFY(QDir().mkpath(outputDir));
    const QString existing = QDir(outputDir).filePath(QStringLiteral("conflict.flac"));
    QFile original(existing);
    QVERIFY(original.open(QIODevice::WriteOnly));
    QCOMPARE(original.write("existing-output"), 15);
    original.close();

    FormatConverter converter;
    converter.setConflictPolicy(QStringLiteral("skip"));
    converter.loadFiles({QUrl::fromLocalFile(input)});
    waitForConverterLoad(converter);
    QSignalSpy completed(&converter, &FormatConverter::transcodeCompleted);
    converter.start(QStringLiteral("flac"), 0, 44100, 2, outputDir,
                    false, false, false);
    QVERIFY(completed.wait(30000));

    QCOMPARE(converter.files().first().toMap()
                 .value(QStringLiteral("status")).toString(),
             QStringLiteral("Skipped"));
    QVERIFY(original.open(QIODevice::ReadOnly));
    QCOMPARE(original.readAll(), QByteArrayLiteral("existing-output"));
}

void AudioToolsEndToEndTest::formatConverterExposesEveryPdfRequiredOutputFormat()
{
    const QVariantList supported = FormatConverter().supportedOutputFormats();
    QSet<QString> keys;
    for (const QVariant& value : supported) {
        keys.insert(value.toMap().value(QStringLiteral("key")).toString());
    }
    const QSet<QString> required{
        QStringLiteral("mp3"), QStringLiteral("wav"),
        QStringLiteral("flac"), QStringLiteral("m4a"),
        QStringLiteral("ogg"), QStringLiteral("opus"),
        QStringLiteral("alac"), QStringLiteral("aac")};
    QCOMPARE(keys, required);
}

void AudioToolsEndToEndTest::formatConverterAppliesRealCbrAndVbrModes()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString input = temp.filePath(QStringLiteral("rate-mode.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(input, 128, 30));

    const QString cbrDir = temp.filePath(QStringLiteral("cbr"));
    QVERIFY(QDir().mkpath(cbrDir));
    FormatConverter cbr;
    QCOMPARE(cbr.bitrateMode(), QStringLiteral("cbr"));
    cbr.setBitrateMode(QStringLiteral("cbr"));
    cbr.loadFiles({QUrl::fromLocalFile(input)});
    waitForConverterLoad(cbr);
    QSignalSpy cbrCompleted(&cbr, &FormatConverter::transcodeCompleted);
    cbr.start(QStringLiteral("mp3"), 192000, 44100, 2,
              cbrDir, false, false, false);
    if (cbrCompleted.isEmpty()) {
        QVERIFY(cbrCompleted.wait(30000));
    }
    QCOMPARE(cbr.failedCount(), 0);
    const QString cbrPath = cbr.files().first().toMap()
        .value(QStringLiteral("outputPath")).toString();

    const QString vbrDir = temp.filePath(QStringLiteral("vbr"));
    QVERIFY(QDir().mkpath(vbrDir));
    FormatConverter vbr;
    QCOMPARE(vbr.bitrateMode(), QStringLiteral("cbr"));
    vbr.setBitrateMode(QStringLiteral("vbr"));
    QCOMPARE(vbr.bitrateMode(), QStringLiteral("vbr"));
    vbr.loadFiles({QUrl::fromLocalFile(input)});
    waitForConverterLoad(vbr);
    QSignalSpy vbrCompleted(&vbr, &FormatConverter::transcodeCompleted);
    vbr.start(QStringLiteral("mp3"), 192000, 44100, 2,
              vbrDir, false, false, false);
    if (vbrCompleted.isEmpty()) {
        QVERIFY(vbrCompleted.wait(30000));
    }
    QCOMPARE(vbr.failedCount(), 0);
    const QString vbrPath = vbr.files().first().toMap()
        .value(QStringLiteral("outputPath")).toString();
    verifyAudioFile(cbrPath, 44100);
    verifyAudioFile(vbrPath, 44100);
    const qint64 cbrSize = QFileInfo(cbrPath).size();
    const qint64 vbrSize = QFileInfo(vbrPath).size();
    QVERIFY(cbrSize > 0);
    QVERIFY(vbrSize > 0);
    QVERIFY2(std::abs(cbrSize - vbrSize) > cbrSize / 20,
             "CBR and VBR outputs should not be equivalent encodes");
}

void AudioToolsEndToEndTest::formatConverterCancellationPreservesExistingOutput()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString input = temp.filePath(QStringLiteral("atomic.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(input, 128, 180));
    const QString output = temp.filePath(QStringLiteral("atomic.mp3"));
    QCOMPARE(ag_transcode(input.toUtf8().constData(),
                          output.toUtf8().constData(), "libmp3lame",
                          64000, 44100, 2, nullptr, nullptr, nullptr), AG_OK);
    QFile original(output);
    QVERIFY(original.open(QIODevice::ReadOnly));
    const QByteArray originalHash = QCryptographicHash::hash(
        original.readAll(), QCryptographicHash::Sha256);
    original.close();

    FormatConverter converter;
    converter.setOverwriteExisting(true);
    converter.loadFiles({QUrl::fromLocalFile(input)});
    waitForConverterLoad(converter);
    QSignalSpy completed(&converter, &FormatConverter::transcodeCompleted);
    converter.start(QStringLiteral("mp3"), 320000, 44100, 2,
                    temp.path(), false, true, false);
    QTRY_VERIFY_WITH_TIMEOUT(
        converter.files().first().toMap()
                .value(QStringLiteral("status")).toString()
            == QStringLiteral("Converting"),
        5000);
    converter.cancel();
    if (completed.isEmpty()) {
        QVERIFY(completed.wait(30000));
    }
    QCOMPARE(converter.files().first().toMap()
                 .value(QStringLiteral("status")).toString(),
             QStringLiteral("Cancelled"));

    QFile preserved(output);
    QVERIFY(preserved.open(QIODevice::ReadOnly));
    const QByteArray preservedHash = QCryptographicHash::hash(
        preserved.readAll(), QCryptographicHash::Sha256);
    QCOMPARE(preservedHash, originalHash);
    QCOMPARE(QDir(temp.path()).entryList(
                 {QStringLiteral("*.agplayer-part-*")}, QDir::Files).size(),
             0);
}

void AudioToolsEndToEndTest::formatConverterExportsAndReopensEveryExposedFormat()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString input = temp.filePath(QStringLiteral("中文 音频 all-formats.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(input, 120, 1));

    const QVariantList formats = FormatConverter().supportedOutputFormats();
    for (const QVariant& value : formats) {
        const QVariantMap format = value.toMap();
        if (!format.value(QStringLiteral("available")).toBool()) {
            continue;
        }
        const QString key = format.value(QStringLiteral("key")).toString();
        const QString outputDir = temp.filePath(QStringLiteral("中文输出-") + key);
        QVERIFY(QDir().mkpath(outputDir));
        FormatConverter converter;
        converter.loadFiles({QUrl::fromLocalFile(input)});
        waitForConverterLoad(converter);
        QSignalSpy completed(&converter, &FormatConverter::transcodeCompleted);
        const bool lossless = key == QStringLiteral("wav")
            || key == QStringLiteral("flac")
            || key == QStringLiteral("alac");
        const int sampleRate = key == QStringLiteral("opus") ? 48000 : 44100;
        converter.start(key, lossless ? 0 : 192000, sampleRate, 2,
                        outputDir, true, false, false);
        if (completed.isEmpty()) {
            QVERIFY2(completed.wait(30000), qPrintable(key));
        }
        QCOMPARE(converter.failedCount(), 0);
        const QVariantMap row = converter.files().first().toMap();
        QCOMPARE(row.value(QStringLiteral("status")).toString(),
                 QStringLiteral("Done"));
        verifyAudioFile(row.value(QStringLiteral("outputPath")).toString(),
                        key == QStringLiteral("opus") ? 48000 : 44100);
    }
}

void AudioToolsEndToEndTest::formatConverterRejectsUnsupportedParameterCombinations()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString input = temp.filePath(QStringLiteral("invalid-options.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(input, 120, 1));

    FormatConverter converter;
    QSignalSpy errors(&converter, &FormatConverter::errorOccurred);
    converter.start(QStringLiteral("mp3"), 128000, 44100, 2,
                    temp.path(), true, false, false);
    QCOMPARE(errors.count(), 1);
    QVERIFY(!converter.busy());

    converter.loadFiles({QUrl::fromLocalFile(input)});
    waitForConverterLoad(converter);
    errors.clear();
    converter.start(QStringLiteral("not-a-codec"), 128000, 44100, 2,
                    temp.path(), true, false, false);
    QCOMPARE(errors.count(), 1);
    QVERIFY(!converter.busy());

    errors.clear();
    converter.start(QStringLiteral("mp3"), 0, 44100, 2,
                    temp.path(), true, false, false);
    QCOMPARE(errors.count(), 1);
    QVERIFY(!converter.busy());

    errors.clear();
    converter.start(QStringLiteral("flac"), 0, 44100, 3,
                    temp.path(), true, false, false);
    QCOMPARE(errors.count(), 1);
    QVERIFY(!converter.busy());
}

void AudioToolsEndToEndTest::transcodeCapiReportsFailureDetail()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QByteArray missing = temp.filePath(QStringLiteral("missing.wav")).toUtf8();
    const QByteArray output = temp.filePath(QStringLiteral("missing.mp3")).toUtf8();

    const ag_result result = ag_transcode_ex(missing.constData(), output.constData(),
                                              "libmp3lame", 128000, 44100, 2,
                                              nullptr, nullptr, nullptr, nullptr);
    QVERIFY(result != AG_OK);
    QVERIFY2(ag_last_error()[0] != '\0',
             "Failed transcodes must expose a diagnostic message.");
}

void AudioToolsEndToEndTest::formatConverterConvertsAcrossDistinctChinesePaths()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString inputDirectory = temp.filePath(QStringLiteral("下载"));
    const QString outputDirectory = temp.filePath(QStringLiteral("桌面"));
    QVERIFY(QDir().mkpath(inputDirectory));
    QVERIFY(QDir().mkpath(outputDirectory));
    const QString input = QDir(inputDirectory).filePath(QStringLiteral("伪装.mp3"));
    QVERIFY(agplayer::test::writeClickTrackWav(input, 120, 1));

    FormatConverter converter;
    converter.loadFiles({QUrl::fromLocalFile(input)});
    waitForConverterLoad(converter);
    QSignalSpy completed(&converter, &FormatConverter::transcodeCompleted);
    converter.start(QStringLiteral("wav"), 0, 44100, 2,
                    outputDirectory, false, false, false);
    QVERIFY(completed.wait(30'000));
    QCOMPARE(converter.failedCount(), 0);
    verifyAudioFile(QDir(outputDirectory).filePath(QStringLiteral("伪装.wav")));
}

void AudioToolsEndToEndTest::audioToolsControllerSupportsFourBuiltInTools()
{
    AudioToolsController controller;
    QCOMPARE(controller.currentTool(), 0);

    controller.setCurrentTool(1);
    QCOMPARE(controller.currentTool(), 1);

    controller.setCurrentTool(3);
    QCOMPARE(controller.currentTool(), 3);

    controller.setCurrentTool(4);
    QCOMPARE(controller.currentTool(), 3);
}

void AudioToolsEndToEndTest::toolsExpandDroppedFoldersRecursively()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString nested = temp.filePath(QStringLiteral("中文/嵌套"));
    QVERIFY(QDir().mkpath(nested));
    const QString first = temp.filePath(QStringLiteral("第一首.wav"));
    const QString second =
        QDir(nested).filePath(QStringLiteral("second.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(first, 120, 1));
    QVERIFY(agplayer::test::writeClickTrackWav(second, 128, 1));
    const QUrl folder = QUrl::fromLocalFile(temp.path());

    FormatConverter converter;
    converter.loadFiles({folder});
    waitForConverterLoad(converter);
    QCOMPARE(converter.fileCount(), 2);

    MetadataEditor metadata;
    QSignalSpy loaded(&metadata, &MetadataEditor::entriesLoaded);
    metadata.loadFiles({folder});
    QVERIFY(loaded.wait(5'000));
    QCOMPARE(metadata.fileCount(), 2);

    FilenameProcessor filenames;
    QSignalSpy filenamesLoaded(&filenames, &FilenameProcessor::entriesLoaded);
    filenames.loadFiles({folder});
    QVERIFY(filenamesLoaded.wait(5'000));
    QCOMPARE(filenames.fileCount(), 2);
}

void AudioToolsEndToEndTest::audioFileDiscoveryExpandsFoldersOffTheGuiThread()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString input = temp.filePath(QStringLiteral("background.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(input, 120, 1));

    QThreadPool* pool = QThreadPool::globalInstance();
    const int previousLimit = pool->maxThreadCount();
    const auto restoreLimit = qScopeGuard([pool, previousLimit] {
        pool->setMaxThreadCount(previousLimit);
    });
    pool->setMaxThreadCount(1);
    QSemaphore started;
    QSemaphore release;
    pool->start([&started, &release] {
        started.release();
        release.acquire();
    });
    QVERIFY(started.tryAcquire(1, 5'000));

    QFuture<QList<QUrl>> discovery =
        agplayer::qt::expandAudioUrlsAsync({QUrl::fromLocalFile(temp.path())});
    QVERIFY2(!discovery.isFinished(),
             "Folder expansion must be queued off the GUI thread");
    release.release();
    discovery.waitForFinished();
    QCOMPARE(discovery.result().size(), 1);
}

void AudioToolsEndToEndTest::formatConverterLoadsDroppedFilesAsynchronously()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    for (int index = 0; index < 32; ++index) {
        const QString path = temp.filePath(
            QStringLiteral("async-%1.wav").arg(index));
        QVERIFY(agplayer::test::writeClickTrackWav(path, 120, 1));
    }

    FormatConverter converter;
    QSignalSpy busyChanged(&converter, &FormatConverter::busyChanged);
    converter.loadFiles({QUrl::fromLocalFile(temp.path())});

    QVERIFY2(busyChanged.count() >= 1,
             "Dropped file discovery must enter an asynchronous busy state");
    QTRY_COMPARE_WITH_TIMEOUT(converter.fileCount(), 32, 5'000);
    QTRY_VERIFY_WITH_TIMEOUT(!converter.busy(), 5'000);
}

void AudioToolsEndToEndTest::
    formatConverterExportsPreservesMetadataAndAvoidsCollisions()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString input = temp.filePath(QStringLiteral("source.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(input, 120, 2));
    QCOMPARE(ag_metadata_write(
                 input.toUtf8().constData(), "Phase 6 title", "AgPlayer",
                 nullptr, nullptr, nullptr, nullptr, nullptr, 0, nullptr),
             AG_OK);
    const QString outputDir = temp.filePath(QStringLiteral("converted"));
    QVERIFY(QDir().mkpath(outputDir));

    FormatConverter converter;
    converter.loadFiles({QUrl::fromLocalFile(input)});
    waitForConverterLoad(converter);
    QCOMPARE(converter.fileCount(), 1);
    QSignalSpy completed(&converter, &FormatConverter::transcodeCompleted);
    converter.start(QStringLiteral("flac"), 0, 44100, 2, outputDir,
                    true, false, false);
    QVERIFY(completed.wait(30000));
    QCOMPARE(completed.first().first().toInt(), 1);
    QCOMPARE(completed.first().last().toInt(), 0);

    const QString firstOutput =
        QDir(outputDir).filePath(QStringLiteral("source.flac"));
    verifyAudioFile(firstOutput, 44100);
    ag_metadata* metadata = nullptr;
    QCOMPARE(ag_metadata_open(firstOutput.toUtf8().constData(), &metadata), AG_OK);
    QCOMPARE(QString::fromUtf8(ag_metadata_title(metadata)),
             QStringLiteral("Phase 6 title"));
    ag_metadata_destroy(metadata);

    completed.clear();
    converter.start(QStringLiteral("flac"), 0, 44100, 2, outputDir,
                    true, false, false);
    QVERIFY(completed.wait(30000));
    verifyAudioFile(
        QDir(outputDir).filePath(QStringLiteral("source_1.flac")), 44100);

    const QString videoInput = temp.filePath(QStringLiteral("video.mp4"));
    QVERIFY(QFile::copy(input, videoInput));
    FormatConverter videoConverter;
    videoConverter.loadFiles({QUrl::fromLocalFile(videoInput)});
    waitForConverterLoad(videoConverter);
    QSignalSpy videoCompleted(
        &videoConverter, &FormatConverter::transcodeCompleted);
    videoConverter.start(QStringLiteral("wav"), 0, 44100, 2, outputDir,
                         false, false, false);
    QVERIFY(videoCompleted.wait(30000));
    QCOMPARE(videoCompleted.first().first().toInt(), 0);
    QCOMPARE(videoCompleted.first().last().toInt(), 1);

    videoCompleted.clear();
    videoConverter.retryFailed(QStringLiteral("wav"), 0, 44100, 2, outputDir,
                               false, false, true);
    QVERIFY(videoCompleted.wait(30000));
    QCOMPARE(videoCompleted.first().first().toInt(), 1);
    verifyAudioFile(
        QDir(outputDir).filePath(QStringLiteral("video.wav")), 44100);
}

void AudioToolsEndToEndTest::formatConverterReservesParallelOutputNames()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString firstDir = temp.filePath(QStringLiteral("first"));
    const QString secondDir = temp.filePath(QStringLiteral("second"));
    const QString outputDir = temp.filePath(QStringLiteral("converted"));
    QVERIFY(QDir().mkpath(firstDir));
    QVERIFY(QDir().mkpath(secondDir));
    QVERIFY(QDir().mkpath(outputDir));

    const QString firstInput =
        QDir(firstDir).filePath(QStringLiteral("same.wav"));
    const QString secondInput =
        QDir(secondDir).filePath(QStringLiteral("same.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(firstInput, 120, 2));
    QVERIFY(agplayer::test::writeClickTrackWav(secondInput, 124, 2));

    FormatConverter converter;
    converter.loadFiles(
        {QUrl::fromLocalFile(firstInput), QUrl::fromLocalFile(secondInput)});
    waitForConverterLoad(converter);
    QCOMPARE(converter.fileCount(), 2);
    QSignalSpy completed(&converter, &FormatConverter::transcodeCompleted);
    converter.start(QStringLiteral("flac"), 0, 44100, 2, outputDir,
                    false, false, false);
    QVERIFY(completed.wait(30000));
    QCOMPARE(completed.first().first().toInt(), 2);
    QCOMPARE(completed.first().last().toInt(), 0);

    const QFileInfoList outputs = QDir(outputDir).entryInfoList(
        {QStringLiteral("same*.flac")}, QDir::Files, QDir::Name);
    QCOMPARE(outputs.size(), 2);
    verifyAudioFile(outputs.at(0).absoluteFilePath(), 44100);
    verifyAudioFile(outputs.at(1).absoluteFilePath(), 44100);
}

void AudioToolsEndToEndTest::formatConverterOverwritePolicyReusesSafeTargets()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString input = temp.filePath(QStringLiteral("source.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(input, 120, 1));
    const QString outputDir = temp.filePath(QStringLiteral("converted"));
    QVERIFY(QDir().mkpath(outputDir));
    const QString target =
        QDir(outputDir).filePath(QStringLiteral("source.flac"));
    QFile stale(target);
    QVERIFY(stale.open(QIODevice::WriteOnly));
    QCOMPARE(stale.write("stale"), 5);
    stale.close();

    FormatConverter converter;
    converter.setOverwriteExisting(true);
    converter.loadFiles({QUrl::fromLocalFile(input)});
    waitForConverterLoad(converter);
    QSignalSpy completed(&converter, &FormatConverter::transcodeCompleted);

    QThreadPool* globalPool = QThreadPool::globalInstance();
    const int previousMaxThreads = globalPool->maxThreadCount();
    const auto restoreThreadCount = qScopeGuard([globalPool,
                                                  previousMaxThreads] {
        globalPool->setMaxThreadCount(previousMaxThreads);
    });
    globalPool->setMaxThreadCount(1);
    QSemaphore workerStarted;
    QSemaphore releaseWorker;
    globalPool->start([&workerStarted, &releaseWorker] {
        workerStarted.release();
        releaseWorker.acquire();
    });
    QVERIFY(workerStarted.tryAcquire(1, 5000));

    converter.start(QStringLiteral("flac"), 0, 44100, 2, outputDir,
                    false, false, false);
    converter.setOverwriteExisting(false);
    releaseWorker.release();
    QVERIFY(completed.wait(30000));
    QCOMPARE(completed.first().first().toInt(), 1);
    verifyAudioFile(target, 44100);
    QVERIFY(!QFileInfo::exists(
        QDir(outputDir).filePath(QStringLiteral("source_1.flac"))));

    FormatConverter sameFormat;
    sameFormat.setOverwriteExisting(true);
    sameFormat.loadFiles({QUrl::fromLocalFile(input)});
    waitForConverterLoad(sameFormat);
    QSignalSpy sameCompleted(
        &sameFormat, &FormatConverter::transcodeCompleted);
    sameFormat.start(QStringLiteral("wav"), 0, 44100, 2, QString(),
                     false, false, false);
    QVERIFY(sameCompleted.wait(30000));
    verifyAudioFile(input);
    verifyAudioFile(temp.filePath(QStringLiteral("source_1.wav")), 44100);
}

void AudioToolsEndToEndTest::
    formatConverterCancellationFinalizesQueuedEntries()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString outputDir = temp.filePath(QStringLiteral("converted"));
    QVERIFY(QDir().mkpath(outputDir));

    QList<QUrl> inputs;
    for (int i = 0; i < 8; ++i) {
        const QString path =
            temp.filePath(QStringLiteral("cancel-%1.wav").arg(i));
        QVERIFY(agplayer::test::writeClickTrackWav(path, 120 + i, 10));
        inputs.push_back(QUrl::fromLocalFile(path));
    }

    FormatConverter converter;
    converter.loadFiles(inputs);
    waitForConverterLoad(converter);
    QSignalSpy completed(&converter, &FormatConverter::transcodeCompleted);
    converter.start(QStringLiteral("flac"), 0, 44100, 2, outputDir,
                    false, false, false);
    converter.cancel();
    QVERIFY(completed.wait(30000));
    QCOMPARE(converter.completedCount(), converter.fileCount());
    QCOMPARE(converter.failedCount(), 0);

    const QVariantList entries = converter.files();
    QCOMPARE(entries.size(), converter.fileCount());
    for (const QVariant& value : entries) {
        const QString status =
            value.toMap().value(QStringLiteral("status")).toString();
        QVERIFY2(status != QStringLiteral("Waiting")
                     && status != QStringLiteral("Converting"),
                 qPrintable(status));
    }
}

void AudioToolsEndToEndTest::formatConverterRunsOnlySelectedEntries()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString outputDir = temp.filePath(QStringLiteral("selected"));
    QVERIFY(QDir().mkpath(outputDir));
    QList<QUrl> inputs;
    for (int index = 0; index < 3; ++index) {
        const QString path = temp.filePath(
            QStringLiteral("selected-%1.wav").arg(index));
        QVERIFY(agplayer::test::writeClickTrackWav(path, 120 + index, 1));
        inputs.push_back(QUrl::fromLocalFile(path));
    }

    FormatConverter converter;
    converter.loadFiles(inputs);
    waitForConverterLoad(converter);
    QSignalSpy completed(&converter, &FormatConverter::transcodeCompleted);
    converter.startSelected({1}, QStringLiteral("flac"), 0, 44100, 2,
                            outputDir, false, false, false);
    QVERIFY(completed.wait(30000));

    const QVariantList rows = converter.files();
    QCOMPARE(rows.at(0).toMap().value(QStringLiteral("status")).toString(),
             QStringLiteral("Waiting"));
    QCOMPARE(rows.at(1).toMap().value(QStringLiteral("status")).toString(),
             QStringLiteral("Done"));
    QCOMPARE(rows.at(2).toMap().value(QStringLiteral("status")).toString(),
             QStringLiteral("Waiting"));
    QVERIFY(QFileInfo::exists(QDir(outputDir).filePath(
        QStringLiteral("selected-1.flac"))));
}

void AudioToolsEndToEndTest::formatConverterPendingPlanRunsOnlyCheckedEntries()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString outputDir = temp.filePath(QStringLiteral("checked-plan"));
    QVERIFY(QDir().mkpath(outputDir));
    QList<QUrl> inputs;
    for (int index = 0; index < 3; ++index) {
        const QString path = temp.filePath(
            QStringLiteral("checked-%1.wav").arg(index));
        QVERIFY(agplayer::test::writeClickTrackWav(path, 120 + index, 1));
        inputs.push_back(QUrl::fromLocalFile(path));
    }

    FormatConverter converter;
    converter.loadFiles(inputs);
    waitForConverterLoad(converter);
    const QString uncheckedId = converter.taskModel()
        ->index(1, 0).data(Qt::UserRole + 1).toString();
    QVERIFY(QMetaObject::invokeMethod(converter.taskModel(), "setChecked",
        Q_ARG(QString, uncheckedId), Q_ARG(bool, false)));
    QCOMPARE(converter.checkedCount(), 2);

    const QVariantMap plan = converter.buildPreflight({
        {QStringLiteral("outputFormat"), QStringLiteral("flac")},
        {QStringLiteral("outputDir"), outputDir},
        {QStringLiteral("keepMetadata"), false},
        {QStringLiteral("keepCover"), false},
        {QStringLiteral("sampleFormat"), QStringLiteral("s16")},
        {QStringLiteral("channelLayout"), QStringLiteral("stereo")}});
    QVERIFY(plan.value(QStringLiteral("ready")).toBool());
    QCOMPARE(plan.value(QStringLiteral("taskCount")).toInt(), 2);

    QSignalSpy completed(&converter, &FormatConverter::transcodeCompleted);
    converter.confirmPendingPlan();
    QVERIFY(completed.wait(30000));
    const QVariantList rows = converter.files();
    QCOMPARE(rows.at(0).toMap().value(QStringLiteral("status")).toString(),
             QStringLiteral("Done"));
    QCOMPARE(rows.at(1).toMap().value(QStringLiteral("status")).toString(),
             QStringLiteral("Waiting"));
    QCOMPARE(rows.at(2).toMap().value(QStringLiteral("status")).toString(),
             QStringLiteral("Done"));
}

void AudioToolsEndToEndTest::
    formatConverterRejectsPendingPlanWhenAFrozenTaskIsRemoved()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString outputDir = temp.filePath(QStringLiteral("stale-plan"));
    QVERIFY(QDir().mkpath(outputDir));
    const QString first = temp.filePath(QStringLiteral("frozen.wav"));
    const QString second = temp.filePath(QStringLiteral("new-selection.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(first, 120, 1));
    QVERIFY(agplayer::test::writeClickTrackWav(second, 124, 1));

    FormatConverter converter;
    converter.loadFiles(
        {QUrl::fromLocalFile(first), QUrl::fromLocalFile(second)});
    waitForConverterLoad(converter);
    const QString secondId = converter.taskModel()->index(1, 0)
                                 .data(Qt::UserRole + 1).toString();
    converter.setTaskChecked(secondId, false);

    const QVariantMap plan = converter.buildPreflight({
        {QStringLiteral("outputFormat"), QStringLiteral("flac")},
        {QStringLiteral("outputDir"), outputDir},
        {QStringLiteral("conflictPolicy"), QStringLiteral("auto-number")}});
    QVERIFY(plan.value(QStringLiteral("ready")).toBool());
    QCOMPARE(plan.value(QStringLiteral("taskCount")).toInt(), 1);

    converter.removeChecked();
    QCOMPARE(converter.fileCount(), 1);
    converter.setTaskChecked(secondId, true);
    QSignalSpy errors(&converter, &FormatConverter::errorOccurred);
    QSignalSpy completed(&converter, &FormatConverter::transcodeCompleted);
    converter.confirmPendingPlan();

    QTRY_VERIFY_WITH_TIMEOUT(!converter.busy(), 30000);
    QCOMPARE(errors.count(), 1);
    QVERIFY(!errors.first().first().toString().isEmpty());
    QCOMPARE(completed.count(), 0);
    QVERIFY(converter.pendingPlan().isEmpty());
    QCOMPARE(converter.files().first().toMap()
                 .value(QStringLiteral("status")).toString(),
             QStringLiteral("Waiting"));
    QVERIFY(!QFileInfo::exists(QDir(outputDir).filePath(
        QStringLiteral("new-selection.flac"))));
}

void AudioToolsEndToEndTest::formatConverterRejectsReimportedFrozenTask()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString input = temp.filePath(QStringLiteral("reimport.wav"));
    const QString outputDir = temp.filePath(QStringLiteral("out"));
    QVERIFY(agplayer::test::writeClickTrackWav(input, 120, 1));

    FormatConverter converter;
    converter.loadFiles({QUrl::fromLocalFile(input)});
    waitForConverterLoad(converter);
    QVERIFY(converter.buildPreflight({
        {QStringLiteral("outputFormat"), QStringLiteral("flac")},
        {QStringLiteral("outputDir"), outputDir}})
                .value(QStringLiteral("ready")).toBool());
    converter.removeFile(0);
    converter.loadFiles({QUrl::fromLocalFile(input)});
    waitForConverterLoad(converter);

    QSignalSpy errors(&converter, &FormatConverter::errorOccurred);
    QSignalSpy completed(&converter, &FormatConverter::transcodeCompleted);
    converter.confirmPendingPlan();
    QTRY_VERIFY_WITH_TIMEOUT(!converter.busy(), 30000);
    QCOMPARE(errors.count(), 1);
    QCOMPARE(completed.count(), 0);
    QVERIFY(!QFileInfo::exists(QDir(outputDir).filePath(
        QStringLiteral("reimport.flac"))));
}

void AudioToolsEndToEndTest::formatConverterRejectsModifiedFrozenSource()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString input = temp.filePath(QStringLiteral("modified.wav"));
    const QString outputDir = temp.filePath(QStringLiteral("out"));
    QVERIFY(agplayer::test::writeClickTrackWav(input, 120, 1));

    FormatConverter converter;
    converter.loadFiles({QUrl::fromLocalFile(input)});
    waitForConverterLoad(converter);
    QVERIFY(converter.buildPreflight({
        {QStringLiteral("outputFormat"), QStringLiteral("flac")},
        {QStringLiteral("outputDir"), outputDir}})
                .value(QStringLiteral("ready")).toBool());
    QVERIFY(QFile::remove(input));
    QVERIFY(agplayer::test::writeClickTrackWav(input, 132, 2));

    QSignalSpy errors(&converter, &FormatConverter::errorOccurred);
    QSignalSpy completed(&converter, &FormatConverter::transcodeCompleted);
    converter.confirmPendingPlan();
    QTRY_VERIFY_WITH_TIMEOUT(!converter.busy(), 30000);
    QCOMPARE(errors.count(), 1);
    QCOMPARE(completed.count(), 0);
    QVERIFY(!QFileInfo::exists(QDir(outputDir).filePath(
        QStringLiteral("modified.flac"))));
}

void AudioToolsEndToEndTest::formatConverterCancelsIndividualEntry()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString outputDir = temp.filePath(QStringLiteral("cancel-one"));
    QVERIFY(QDir().mkpath(outputDir));
    QList<QUrl> inputs;
    for (int index = 0; index < 4; ++index) {
        const QString path = temp.filePath(
            QStringLiteral("cancel-one-%1.wav").arg(index));
        QVERIFY(agplayer::test::writeClickTrackWav(path, 120 + index, 20));
        inputs.push_back(QUrl::fromLocalFile(path));
    }

    FormatConverter converter;
    converter.loadFiles(inputs);
    waitForConverterLoad(converter);
    QSignalSpy completed(&converter, &FormatConverter::transcodeCompleted);
    converter.start(QStringLiteral("flac"), 0, 44100, 2, outputDir,
                    false, false, false);
    converter.cancelEntry(0);
    QVERIFY(completed.wait(30000));
    const QVariantList rows = converter.files();
    QCOMPARE(rows.at(0).toMap().value(QStringLiteral("status")).toString(),
             QStringLiteral("Cancelled"));
    for (int index = 1; index < rows.size(); ++index) {
        QCOMPARE(rows.at(index).toMap().value(QStringLiteral("status")).toString(),
                 QStringLiteral("Done"));
    }
}

void AudioToolsEndToEndTest::formatConverterReportsIntermediateProgress()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString input = temp.filePath(QStringLiteral("progress.wav"));
    const QString outputDir = temp.filePath(QStringLiteral("progress-out"));
    QVERIFY(QDir().mkpath(outputDir));
    QVERIFY(agplayer::test::writeClickTrackWav(input, 120, 60));

    FormatConverter converter;
    converter.loadFiles({QUrl::fromLocalFile(input)});
    waitForConverterLoad(converter);
    bool sawIntermediate = false;
    connect(&converter, &FormatConverter::filesChanged, this, [&]() {
        const QVariantMap row = converter.files().first().toMap();
        const double progress = row.value(QStringLiteral("progress")).toDouble();
        sawIntermediate = sawIntermediate || (progress > 0.0 && progress < 1.0);
    });
    QSignalSpy completed(&converter, &FormatConverter::transcodeCompleted);
    converter.start(QStringLiteral("flac"), 0, 44100, 2, outputDir,
                    false, false, false);
    QVERIFY(completed.wait(30000));
    QVERIFY(sawIntermediate);
}

void AudioToolsEndToEndTest::formatConverterWritesMetadataPlanToNewOutput()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString input = temp.filePath(QStringLiteral("metadata-plan.wav"));
    const QString outputDir = temp.filePath(QStringLiteral("converted"));
    QVERIFY(agplayer::test::writeClickTrackWav(input, 120, 1));
    QVERIFY(QDir().mkpath(outputDir));

    FormatConverter converter;
    QVariantMap fields;
    fields.insert(QStringLiteral("title"), QVariantMap{
        {QStringLiteral("mode"), QStringLiteral("set")},
        {QStringLiteral("value"), QStringLiteral("Converted title")}});
    QVERIFY(converter.setMetadataEditPlan(fields, {}));
    converter.loadFiles({QUrl::fromLocalFile(input)});
    waitForConverterLoad(converter);
    QSignalSpy completed(&converter, &FormatConverter::transcodeCompleted);
    converter.start(QStringLiteral("flac"), 0, 44100, 2, outputDir,
                    false, false, false);
    QVERIFY(completed.wait(30000));
      QCOMPARE(completed.first().first().toInt(), 1);
      const QString output = QDir(outputDir).filePath(QStringLiteral("metadata-plan.flac"));
      QVERIFY(QFileInfo::exists(output));
      QVERIFY(!QFileInfo::exists(output + QStringLiteral(".agbak")));
      const QFileInfoList unpublishedStages = QDir(outputDir).entryInfoList(
          {QStringLiteral("*.agtranscode-*.tmp.*"),
           QStringLiteral("*.agmeta-stage-*.tmp.*")}, QDir::Files);
      QVERIFY(unpublishedStages.isEmpty());
    ag_metadata* metadata = nullptr;
    QCOMPARE(ag_metadata_open(output.toUtf8().constData(), &metadata), AG_OK);
    QCOMPARE(QString::fromUtf8(ag_metadata_title(metadata)),
             QStringLiteral("Converted title"));
    ag_metadata_destroy(metadata);
    QVERIFY(QFileInfo::exists(input));
}

void AudioToolsEndToEndTest::metadataEditorWritesTags()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString source = temp.filePath(QStringLiteral("meta-source.wav"));
    const QString encoded = temp.filePath(QStringLiteral("meta.mp3"));
    const QString input = temp.filePath(QStringLiteral("元数据编辑.mp3"));
    QVERIFY(agplayer::test::writeClickTrackWav(source, 120, 2));
    QCOMPARE(ag_transcode(source.toUtf8().constData(), encoded.toUtf8().constData(),
                          "libmp3lame", 192000, 44100, 2,
                          nullptr, nullptr, nullptr), AG_OK);
    QVERIFY(QFile::rename(encoded, input));

    LibraryModel library;
    TrackRecord libraryTrack;
    libraryTrack.trackId = QStringLiteral("metadata-library-track");
    libraryTrack.path = input;
    libraryTrack.title = QStringLiteral("Before");
    libraryTrack.available = true;
    QVERIFY(library.append(libraryTrack));

    MetadataEditor editor;
    editor.setLibraryModel(&library);
    QSignalSpy entriesLoaded(&editor, &MetadataEditor::entriesLoaded);
    editor.loadFiles({QUrl::fromLocalFile(input)});
    QVERIFY(entriesLoaded.wait(30000));
    QCOMPARE(editor.fileCount(), 1);

    QVariantMap fields;
    const auto setField = [&fields](const QString& key, const QString& value) {
        fields.insert(key, QVariantMap{{QStringLiteral("mode"), QStringLiteral("set")},
                                       {QStringLiteral("value"), value}});
    };
    setField(QStringLiteral("title"), QStringLiteral("Edited title"));
    setField(QStringLiteral("artist"), QStringLiteral("Edited artist"));
    setField(QStringLiteral("albumArtist"), QStringLiteral("Album artist"));
    setField(QStringLiteral("composer"), QStringLiteral("Composer"));
    setField(QStringLiteral("bpm"), QStringLiteral("128.50"));

    QSignalSpy preflightCompleted(&editor, &MetadataEditor::preflightCompleted);
    editor.preflightMetadata(fields, {});
    QVERIFY(editor.busy());
    QVERIFY(preflightCompleted.wait(30000));
    QCOMPARE(editor.results().size(), 1);
    const QVariantMap preflight = editor.results().first().toMap();
    QCOMPARE(preflight.value(QStringLiteral("stage")).toString(),
             QStringLiteral("preflight"));
    QCOMPARE(preflight.value(QStringLiteral("success")).toBool(), true);
    QCOMPARE(preflight.value(QStringLiteral("preflightReason")).toString(),
             QString());

    QSignalSpy metadataApplied(&editor, &MetadataEditor::metadataApplied);
    editor.applyMetadata(fields, {});
    QVERIFY(metadataApplied.wait(30000));
    QCOMPARE(metadataApplied.first().first().toInt(), 1);
    QCOMPARE(editor.entryAt(0).value(QStringLiteral("title")).toString(),
             QStringLiteral("Edited title"));
    QCOMPARE(editor.entryAt(0).value(QStringLiteral("artist")).toString(),
             QStringLiteral("Edited artist"));
    QCOMPARE(library.data(library.index(0, 0), LibraryModel::TitleRole).toString(),
             QStringLiteral("Edited title"));
    QCOMPARE(library.data(library.index(0, 0), LibraryModel::ArtistRole).toString(),
             QStringLiteral("Edited artist"));
      QCOMPARE(editor.results().size(), 1);
      const QVariantMap completedResult = editor.results().first().toMap();
      QCOMPARE(completedResult.value(QStringLiteral("success")).toBool(), true);
      QCOMPARE(completedResult.value(QStringLiteral("status")).toString(),
               QStringLiteral("completed"));
      QCOMPARE(completedResult.value(QStringLiteral("usedStreamCopy")).toBool(), true);
      QCOMPARE(completedResult.value(
                   QStringLiteral("audioVerifiedUnchanged")).toBool(), true);
      QVERIFY(completedResult.value(QStringLiteral("packetsCopied")).toULongLong() > 0);
      QCOMPARE(completedResult.value(QStringLiteral("decoderOpenCount")).toULongLong(),
               qulonglong{0});
      QCOMPARE(completedResult.value(QStringLiteral("encoderOpenCount")).toULongLong(),
               qulonglong{0});
      QCOMPARE(completedResult.value(QStringLiteral("fields")).toList().size(), 5);
      QVERIFY(completedResult.value(QStringLiteral("cover")).toMap()
                  .contains(QStringLiteral("status")));

    ag_metadata* metadata = nullptr;
    QCOMPARE(ag_metadata_open(input.toUtf8().constData(), &metadata), AG_OK);
    QCOMPARE(QString::fromUtf8(ag_metadata_title(metadata)),
             QStringLiteral("Edited title"));
    QCOMPARE(QString::fromUtf8(ag_metadata_artist(metadata)),
             QStringLiteral("Edited artist"));
    QCOMPARE(QString::fromUtf8(ag_metadata_album_artist(metadata)),
             QStringLiteral("Album artist"));
    QCOMPARE(QString::fromUtf8(ag_metadata_composer(metadata)),
             QStringLiteral("Composer"));
    QCOMPARE(QString::fromUtf8(ag_metadata_bpm_tag(metadata)),
             QStringLiteral("128.50"));
    ag_metadata_destroy(metadata);

    QVariantMap clearArtist;
    clearArtist.insert(QStringLiteral("artist"),
                       QVariantMap{{QStringLiteral("mode"),
                                    QStringLiteral("clear")}});
    metadataApplied.clear();
    editor.applyMetadata(clearArtist, {});
    QVERIFY(metadataApplied.wait(30000));
    QCOMPARE(metadataApplied.first().first().toInt(), 1);
    QCOMPARE(editor.entryAt(0).value(QStringLiteral("artist")).toString(),
             QString());
    QCOMPARE(editor.results().size(), 1);
    metadata = nullptr;
    QCOMPARE(ag_metadata_open(input.toUtf8().constData(), &metadata), AG_OK);
    QCOMPARE(QString::fromUtf8(ag_metadata_title(metadata)),
             QStringLiteral("Edited title"));
    QCOMPARE(QString::fromUtf8(ag_metadata_artist(metadata)), QString());
    ag_metadata_destroy(metadata);

}

void AudioToolsEndToEndTest::metadataEditorPreflightIsAsyncAndRequiresDecision()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString valid = temp.filePath(QStringLiteral("valid.wav"));
    const QString invalid = temp.filePath(QStringLiteral("broken.mp3"));
    QVERIFY(agplayer::test::writeClickTrackWav(valid, 120, 2));
    QFile broken(invalid);
    QVERIFY(broken.open(QIODevice::WriteOnly));
    QCOMPARE(broken.write("not audio"), qint64(9));
    broken.close();

    MetadataEditor editor;
    QSignalSpy entriesLoaded(&editor, &MetadataEditor::entriesLoaded);
    editor.loadFiles({QUrl::fromLocalFile(valid), QUrl::fromLocalFile(invalid)});
    QVERIFY(entriesLoaded.wait(30000));
    QCOMPARE(editor.fileCount(), 2);

    QVariantMap fields;
    fields.insert(QStringLiteral("title"),
                  QVariantMap{{QStringLiteral("mode"), QStringLiteral("set")},
                              {QStringLiteral("value"), QStringLiteral("Ready")}});
    QSignalSpy preflightCompleted(&editor, &MetadataEditor::preflightCompleted);
    QSignalSpy decisionRequired(&editor, &MetadataEditor::preflightDecisionRequired);
    editor.applyMetadata(fields, {});
    QVERIFY(editor.busy());
    QVERIFY(preflightCompleted.wait(30000));
    QCOMPARE(decisionRequired.count(), 1);
    QVERIFY(editor.requiresPreflightDecision());
    QCOMPARE(editor.supportedCount(), 1);
    QCOMPARE(editor.unsupportedCount(), 1);

    QSignalSpy applied(&editor, &MetadataEditor::metadataApplied);
    editor.applyPreflightDecision(QStringLiteral("supportedOnly"));
    QVERIFY(applied.wait(30000));
    QCOMPARE(editor.successCount(), 1);
    QCOMPARE(editor.unsupportedCount(), 1);
    QCOMPARE(editor.cancelledCount(), 0);
    QCOMPARE(editor.entryAt(0).value(QStringLiteral("title")).toString(),
             QStringLiteral("Ready"));
}

void AudioToolsEndToEndTest::metadataEditorAppliesUiPayloadToMixedContainerBatch()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString source = temp.filePath(QStringLiteral("batch-source.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(source, 120, 2));

    const QList<QPair<QString, QString>> targets{
        {QStringLiteral("batch.mp3"), QStringLiteral("libmp3lame")},
        {QStringLiteral("batch.flac"), QStringLiteral("flac")},
        {QStringLiteral("batch.m4a"), QStringLiteral("aac")},
        {QStringLiteral("batch.ogg"), QStringLiteral("libvorbis")},
        {QStringLiteral("batch.opus"), QStringLiteral("libopus")}};
    QList<QUrl> urls;
    for (const auto& target : targets) {
        const QString path = temp.filePath(target.first);
        QCOMPARE(ag_transcode(source.toUtf8().constData(), path.toUtf8().constData(),
                              target.second.toUtf8().constData(), 192000,
                              44100, 2, nullptr, nullptr, nullptr), AG_OK);
        urls.append(QUrl::fromLocalFile(path));
    }

    MetadataEditor editor;
    QSignalSpy entriesLoaded(&editor, &MetadataEditor::entriesLoaded);
    editor.loadFiles(urls);
    QVERIFY(entriesLoaded.wait(30'000));
    QCOMPARE(editor.fileCount(), targets.size());

    const QVariantMap fields{{QStringLiteral("title"),
                              QVariantMap{{QStringLiteral("mode"), QStringLiteral("set")},
                                          {QStringLiteral("value"), QStringLiteral("Mixed batch")}}},
                             {QStringLiteral("artist"),
                              QVariantMap{{QStringLiteral("mode"), QStringLiteral("set")},
                                          {QStringLiteral("value"), QStringLiteral("AgPlayer QA")}}}};
    QSignalSpy applied(&editor, &MetadataEditor::metadataApplied);
    editor.applyMetadata(fields, {});
    QVERIFY(applied.wait(30'000));
    QCOMPARE(applied.first().first().toInt(), targets.size());
    QCOMPARE(applied.first().at(1).toInt(), 0);
    QCOMPARE(editor.results().size(), targets.size());
    for (int index = 0; index < editor.fileCount(); ++index) {
        QCOMPARE(editor.entryAt(index).value(QStringLiteral("title")).toString(),
                 QStringLiteral("Mixed batch"));
    }
}

void AudioToolsEndToEndTest::filenameProcessorRenamesWithoutTouchingAudio()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString input = temp.filePath(QStringLiteral("My Song.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(input, 120, 2));
    QFile source(input);
    QVERIFY(source.open(QIODevice::ReadOnly));
    const QByteArray originalBytes = source.readAll();
    source.close();

    FilenameProcessor processor;
    QSignalSpy entriesLoaded(&processor, &FilenameProcessor::entriesLoaded);
    processor.loadFiles({QUrl::fromLocalFile(input)});
    QVERIFY(entriesLoaded.wait(30000));
    QCOMPARE(processor.fileCount(), 1);
    const QVariantMap loadedEntry = processor.entryAt(0);
    QCOMPARE(loadedEntry.value(QStringLiteral("directory")).toString(), temp.path());
    QCOMPARE(loadedEntry.value(QStringLiteral("extension")).toString(),
             QStringLiteral("wav"));
    QVERIFY(loadedEntry.value(QStringLiteral("fileSize")).toLongLong() > 0);
    const QString originalHash = loadedEntry.value(QStringLiteral("sha256")).toString();
    QVERIFY2(!originalHash.isEmpty(), "Rename input must be fingerprinted before staging");

    processor.removeFiles({});
    QCOMPARE(processor.fileCount(), 1);

    const QVariantMap rules{
        {QStringLiteral("prefix"), QStringLiteral("P-")},
        {QStringLiteral("suffix"), QStringLiteral("-S")},
        {QStringLiteral("replaceSpaces"), true},
        {QStringLiteral("spaceReplacement"), QStringLiteral("_")},
        {QStringLiteral("caseMode"), QStringLiteral("lower")},
        {QStringLiteral("autoNumber"), true},
        {QStringLiteral("numberStart"), 7},
        {QStringLiteral("numberDigits"), 3}};
    const QVariantList preview = processor.preview(rules);
    QCOMPARE(preview.size(), 1);
    QCOMPARE(preview.first().toMap().value(QStringLiteral("preview")).toString(),
             QStringLiteral("P-my_song-S_007.wav"));

    QSignalSpy renamed(&processor, &FilenameProcessor::renameApplied);
    processor.apply(rules);
    QVERIFY(renamed.wait(30000));
    QCOMPARE(renamed.first().at(0).toInt(), 1);
    const QString renamedPath = temp.filePath(QStringLiteral("P-my_song-S_007.wav"));
    QFile renamedFile(renamedPath);
    QVERIFY(renamedFile.open(QIODevice::ReadOnly));
    QCOMPARE(renamedFile.readAll(), originalBytes);
    QCOMPARE(processor.entryAt(0).value(QStringLiteral("sha256")).toString(),
             originalHash);
}

void AudioToolsEndToEndTest::filenameProcessorAppliesExactlyThePreviewedConflictPlan()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString input = temp.filePath(QStringLiteral("track.wav"));
    const QString occupied = temp.filePath(QStringLiteral("P-track.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(input, 120, 1));
    QVERIFY(agplayer::test::writeClickTrackWav(occupied, 124, 1));

    FilenameProcessor processor;
    QSignalSpy loaded(&processor, &FilenameProcessor::entriesLoaded);
    processor.loadFiles({QUrl::fromLocalFile(input)});
    QVERIFY(loaded.wait(30'000));

    const QVariantMap rules{{QStringLiteral("prefix"), QStringLiteral("P-")}};
    const QVariantList preview = processor.preview(
        rules, {}, QStringLiteral("autoNumber"));
    QCOMPARE(preview.size(), 1);
    const QString previewedName = preview.first().toMap()
        .value(QStringLiteral("preview")).toString();
    QCOMPARE(previewedName, QStringLiteral("P-track_2.wav"));

    QSignalSpy renamed(&processor, &FilenameProcessor::renameApplied);
    processor.apply(rules, {}, QStringLiteral("autoNumber"));
    QVERIFY(renamed.wait(30'000));
    QCOMPARE(renamed.first().at(0).toInt(), 1);
    QCOMPARE(processor.entryAt(0).value(QStringLiteral("fileName")).toString(),
             previewedName);
    QVERIFY(QFileInfo::exists(temp.filePath(previewedName)));
}

void AudioToolsEndToEndTest::filenameProcessorKeepsLibraryPathsInSyncAcrossRenameAndUndo()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString sourcePath = temp.filePath(QStringLiteral("track.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(sourcePath, 120, 1));

    TrackRecord track;
    track.trackId = QStringLiteral("renamed-track");
    track.path = sourcePath;
    track.available = true;
    LibraryModel library;
    library.replaceAll({track});

    FilenameProcessor processor;
    processor.setLibraryModel(&library);
    QSignalSpy loaded(&processor, &FilenameProcessor::entriesLoaded);
    processor.loadFiles({QUrl::fromLocalFile(sourcePath)});
    QVERIFY(loaded.wait(30'000));

    QSignalSpy renamed(&processor, &FilenameProcessor::renameApplied);
    processor.apply({{QStringLiteral("prefix"), QStringLiteral("P-")}});
    QVERIFY(renamed.wait(30'000));
    const QString renamedPath = temp.filePath(QStringLiteral("P-track.wav"));
    QCOMPARE(library.trackForId(track.trackId).value(QStringLiteral("path")).toString(),
             renamedPath);
    QVERIFY(library.trackForId(track.trackId).value(QStringLiteral("available")).toBool());

    processor.undoLast();
    QCOMPARE(library.trackForId(track.trackId).value(QStringLiteral("path")).toString(),
             sourcePath);
    QVERIFY(library.trackForId(track.trackId).value(QStringLiteral("available")).toBool());
}

void AudioToolsEndToEndTest::filenameProcessorSanitizesWindowsReservedAndLongNames()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString input = temp.filePath(QStringLiteral("C O N.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(input, 120, 1));

    FilenameProcessor processor;
    QSignalSpy loaded(&processor, &FilenameProcessor::entriesLoaded);
    processor.loadFiles({QUrl::fromLocalFile(input)});
    QVERIFY(loaded.wait(30000));

    const QVariantMap reservedRules{
        {QStringLiteral("replaceSpaces"), true},
        {QStringLiteral("spaceReplacement"), QString()}};
    const QString reserved = processor.preview(reservedRules).first().toMap()
        .value(QStringLiteral("preview")).toString();
    QCOMPARE(reserved, QStringLiteral("CON.wav"));
    QVERIFY(processor.preview(reservedRules).first().toMap()
                .value(QStringLiteral("conflict")).toBool());

    const QVariantMap longRules{
        {QStringLiteral("prefix"), QString(300, QLatin1Char('x'))}};
    const QString bounded = processor.preview(longRules).first().toMap()
        .value(QStringLiteral("preview")).toString();
    QVERIFY(bounded.size() > 255);
    QVERIFY(bounded.endsWith(QStringLiteral(".wav")));
    QVERIFY(processor.preview(longRules).first().toMap()
                .value(QStringLiteral("conflict")).toBool());
}

void AudioToolsEndToEndTest::filenameProcessorPlansConflictsAndUndoes()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString input = temp.filePath(QStringLiteral("track.wav"));
    const QString collision = temp.filePath(QStringLiteral("safe-track.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(input, 120, 2));
    QVERIFY(agplayer::test::writeClickTrackWav(collision, 124, 2));

    FilenameProcessor processor;
    QSignalSpy entriesLoaded(&processor, &FilenameProcessor::entriesLoaded);
    processor.loadFiles({QUrl::fromLocalFile(input)});
    QVERIFY(entriesLoaded.wait(30000));

    const QVariantMap rules{{QStringLiteral("prefix"),
                             QStringLiteral("safe-")}};
    QSignalSpy renamed(&processor, &FilenameProcessor::renameApplied);
    processor.apply(rules, {}, QStringLiteral("autoNumber"));
    QVERIFY(renamed.wait(30000));
    QCOMPARE(processor.entryAt(0).value(QStringLiteral("fileName")).toString(),
             QStringLiteral("safe-track_2.wav"));
    QVERIFY(processor.canUndo());

    QFile conflict(input);
    QVERIFY(conflict.open(QIODevice::WriteOnly));
    conflict.write("conflict");
    conflict.close();
    QSignalSpy undone(&processor, &FilenameProcessor::undoCompleted);
    processor.undoLast();
    QCOMPARE(undone.count(), 1);
    QCOMPARE(undone.first().at(0).toInt(), 0);
    QCOMPARE(undone.first().at(1).toInt(), 1);
    QVERIFY(processor.canUndo());

    QVERIFY(QFile::remove(input));
    undone.clear();
    processor.undoLast();
    QCOMPARE(undone.count(), 1);
    QCOMPARE(undone.first().at(0).toInt(), 1);
    QCOMPARE(undone.first().at(1).toInt(), 0);
    QCOMPARE(processor.entryAt(0).value(QStringLiteral("path")).toString(), input);
    QVERIFY(QFileInfo::exists(input));
    QVERIFY(!processor.canUndo());
}

void AudioToolsEndToEndTest::filenameProcessorUsesTwoStageTransactions()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString first = temp.filePath(QStringLiteral("a.wav"));
    const QString second = temp.filePath(QStringLiteral("x-a.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(first, 120, 1));
    QVERIFY(agplayer::test::writeClickTrackWav(second, 124, 1));

    FilenameProcessor processor;
    QSignalSpy entriesLoaded(&processor, &FilenameProcessor::entriesLoaded);
    processor.loadFiles({QUrl::fromLocalFile(first), QUrl::fromLocalFile(second)});
    QVERIFY(entriesLoaded.wait(30'000));

    const QVariantMap rules{{QStringLiteral("prefix"), QStringLiteral("x-")}};
    const QVariantList preview = processor.preview(rules);
    QCOMPARE(preview.size(), 2);
    QCOMPARE(preview.first().toMap().value(QStringLiteral("conflict")).toBool(),
             false);

    QSignalSpy renamed(&processor, &FilenameProcessor::renameApplied);
    processor.apply(rules, {}, QStringLiteral("stop"));
    QVERIFY(renamed.wait(30'000));
    QCOMPARE(renamed.first().at(0).toInt(), 2);
    QCOMPARE(renamed.first().at(1).toInt(), 0);
    QCOMPARE(renamed.first().at(2).toInt(), 0);
    QVERIFY(QFileInfo::exists(temp.filePath(QStringLiteral("x-a.wav"))));
    QVERIFY(QFileInfo::exists(temp.filePath(QStringLiteral("x-x-a.wav"))));
    QVERIFY(processor.canUndo());

    QSignalSpy undone(&processor, &FilenameProcessor::undoCompleted);
    processor.undoLast();
    QCOMPARE(undone.count(), 1);
    QCOMPARE(undone.first().at(0).toInt(), 2);
    QCOMPARE(undone.first().at(1).toInt(), 0);
    QVERIFY(QFileInfo::exists(first));
    QVERIFY(QFileInfo::exists(second));
    QVERIFY(!processor.canUndo());
}

QTEST_MAIN(AudioToolsEndToEndTest)
#include "audio_tools_end_to_end_test.moc"
