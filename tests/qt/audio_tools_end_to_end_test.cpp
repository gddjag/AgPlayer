#include "audio_tools_controller.hpp"
#include "audio_editor/audio_editor_controller.hpp"
#include "audio_preview_controller.hpp"
#include "playback_controller.hpp"
#include "audio_file_discovery.hpp"
#include "filename_processor.hpp"
#include "format_converter.hpp"
#include "library_model.hpp"
#include "metadata_editor.hpp"

#include "../../core/src/metadata_writer.hpp"
#include "../../core/src/transcode_probe.hpp"
#include "../../core/src/transcode_verifier.hpp"

#include "../core/bpm_fixture.hpp"

#include <agplayer/c_api.h>

#include <QDir>
#include <QElapsedTimer>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QTest>
#include <QThreadPool>
#include <QTimer>
#include <QVariantMap>

#include <algorithm>
#include <cmath>

namespace {

void verifyAudioFile(const QString& path, int expectedSampleRate = 0,
                     const QString& context = {})
{
    QVERIFY2(QFileInfo::exists(path), qPrintable(path));
    ag_metadata* metadata = nullptr;
    QCOMPARE(ag_metadata_open(path.toUtf8().constData(), &metadata), AG_OK);
    QVERIFY(metadata != nullptr);
    QVERIFY(ag_metadata_duration_ms(metadata) > 0);
    if (expectedSampleRate > 0) {
        const int actualSampleRate = ag_metadata_sample_rate(metadata);
        QVERIFY2(actualSampleRate == expectedSampleRate,
                 qPrintable(QStringLiteral("%1: sample rate %2, expected %3")
                                .arg(context, QString::number(actualSampleRate),
                                     QString::number(expectedSampleRate))));
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
    void formatConverterExposesOnlyReleaseFormats();
    void audioToolsControllerKeepsLegacyIdsAndSupportsSeparation();
    void allPlaybackLocationsPauseEachOther();
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
    void formatConverterDisablesCoverRetentionWhenSourceHasNoCover();
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
    void formatConverterHonorsVorbisQualityFlacCompressionAndLosslessDepth();
    void formatConverterRetryPreservesFrozenProfile();
    void formatConverterRetryPreservesVideoExtraction();
    void formatConverterRetryPreservesMetadataSnapshot();
    void formatConverterImportsRealAiffInput();
    void formatConverterSkipPolicyLeavesExistingOutputUntouched();
    void formatConverterExposesEveryPdfRequiredOutputFormat();
    void formatConverterPreflightValidatesRecommendedParameterChoices();
    void formatConverterAppliesRealCbrAndVbrModes();
    void formatConverterExportsEveryAdvertisedBitrateMode();
    void formatConverterCancellationPreservesExistingOutput();
    void formatConverterExportsAndReopensEveryExposedFormat();
    void formatConverterTranscodesRealAacToRequiredContainers();
    void formatConverterValidatesLosslessResolvedProfileReadback();
    void formatConverterSeparatesFinishedDoneAndFailedCounts();
    void formatConverterDeduplicatesCanonicalImportPaths();
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
    void formatConverterScopesMetadataPlanToOneBatch();
    void formatConverterRejectsUnsupportedMetadataBeforeEncoding();
    void metadataEditorWritesTags();
    void metadataEditorReadbackCannotOverwriteRelocatedLibraryTrack();
    void performanceConverterCheckedRemoval();
    void performanceConversionAndMetadataBatch();
    void metadataEditorAppendsDeduplicatesAndAggregatesScopeValues();
    void metadataEditorDetectsReplacementCoverFromContent();
    void metadataEditorPreflightIsAsyncAndRequiresDecision();
    void metadataEditorPreflightSeparatesUnsupportedAndInternalFailures();
    void metadataEditorRejectsTargetChangedAfterPreflight();
    void metadataEditorDoesNotApplyWhenEveryTargetIsUnsupported();
    void metadataEditorAppliesUiPayloadToMixedContainerBatch();
    void filenameProcessorRenamesWithoutTouchingAudio();
    void filenameProcessorAppliesExactlyThePreviewedConflictPlan();
    void filenameProcessorKeepsLibraryPathsInSyncAcrossRenameAndUndo();
    void filenameProcessorPlansConflictsAndUndoes();
    void filenameProcessorUsesTwoStageTransactions();
    void filenameProcessorSanitizesWindowsReservedAndLongNames();
};

void AudioToolsEndToEndTest::formatConverterExposesOnlyReleaseFormats()
{
    const QVariantList capabilities = FormatConverter().supportedOutputFormats();
    QStringList keys;
    for (const QVariant& value : capabilities) {
        keys.append(value.toMap().value(QStringLiteral("key")).toString());
    }
    QCOMPARE(keys, QStringList({QStringLiteral("mp3"), QStringLiteral("flac"),
                                QStringLiteral("wav"), QStringLiteral("aac"),
                                QStringLiteral("opus"), QStringLiteral("ogg"),
                                QStringLiteral("alac"), QStringLiteral("aiff")}));
}

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
    QCOMPARE(opus.value(QStringLiteral("bitRate")).toInt(), 320000);
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
    QTest::newRow("aac-metadata")
        << QStringLiteral("aac") << true << false;
    QTest::newRow("aac-cover")
        << QStringLiteral("aac") << false << true;
    QTest::newRow("wav-cover")
        << QStringLiteral("wav") << true << true;
}

void AudioToolsEndToEndTest::
    formatConverterPreflightRejectsUnsupportedCapability()
{
    QFETCH(QString, format);
    QFETCH(bool, keepMetadata);
    QFETCH(bool, keepCover);
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
        {QStringLiteral("bitRate"),
         format == QStringLiteral("aac") ? 128000 : 0},
        {QStringLiteral("keepMetadata"), keepMetadata},
        {QStringLiteral("keepCover"), keepCover}});

    QVERIFY(plan.value(QStringLiteral("ready")).toBool());
    QCOMPARE(errors.count(), 0);
    QVERIFY(!converter.pendingPlan().isEmpty());
    converter.rejectPendingPlan();
}

void AudioToolsEndToEndTest::
    formatConverterDisablesCoverRetentionWhenSourceHasNoCover()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString input = temp.filePath(QStringLiteral("without-cover.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(input, 120, 1));

    FormatConverter converter;
    converter.loadFiles({QUrl::fromLocalFile(input)});
    waitForConverterLoad(converter);
    const QVariantMap plan = converter.buildPreflight({
        {QStringLiteral("outputFormat"), QStringLiteral("mp3")},
        {QStringLiteral("outputDir"), temp.filePath(QStringLiteral("out"))},
        {QStringLiteral("bitRate"), 128000},
        {QStringLiteral("keepCover"), true}});

    QVERIFY(plan.value(QStringLiteral("ready")).toBool());
    const QVariantMap resolved = plan.value(QStringLiteral("tasks")).toList()
                                     .constFirst().toMap()
                                     .value(QStringLiteral("resolvedProfile")).toMap();
    QCOMPARE(resolved.value(QStringLiteral("keepCover")).toBool(), false);
    converter.rejectPendingPlan();
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
    connect(&converter, &FormatConverter::progressChanged, &converter, [&] {
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
    formatConverterHonorsVorbisQualityFlacCompressionAndLosslessDepth()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString input = temp.filePath(QStringLiteral("parameter-source.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(input, 123, 3));

    const auto convert = [&](const QString& format, const QString& directory,
                             int quality, const QString& bitDepth = QString()) {
        FormatConverter converter;
        converter.loadFiles({QUrl::fromLocalFile(input)});
        QElapsedTimer loadTimer;
        loadTimer.start();
        while (converter.busy() && loadTimer.elapsed() < 5000) {
            QTest::qWait(10);
        }
        if (converter.busy()) {
            return QVariantMap{{QStringLiteral("error"),
                                QStringLiteral("input load timed out")}};
        }
        QVariantMap request{
            {QStringLiteral("outputFormat"), format},
            {QStringLiteral("outputDir"), temp.filePath(directory)},
            {QStringLiteral("bitRate"), 0},
            {QStringLiteral("quality"), quality},
            {QStringLiteral("sampleRate"),
             format == QStringLiteral("ogg") ? 0 : 44100},
            {QStringLiteral("keepMetadata"), false},
            {QStringLiteral("keepCover"), false},
        };
        if (!bitDepth.isEmpty()) {
            request.insert(QStringLiteral("bitDepth"), bitDepth);
        }
        const QVariantMap plan = converter.buildPreflight(request);
        if (!plan.value(QStringLiteral("ready")).toBool()) {
            return QVariantMap{{QStringLiteral("error"),
                                plan.value(QStringLiteral("error"))}};
        }
        QSignalSpy completed(&converter, &FormatConverter::transcodeCompleted);
        converter.confirmPendingPlan();
        if (completed.isEmpty() && !completed.wait(30000)) {
            return QVariantMap{{QStringLiteral("error"),
                                QStringLiteral("conversion timed out")}};
        }
        const QVariantMap row = converter.files().front().toMap();
        if (converter.failedCount() != 0) {
            return QVariantMap{{QStringLiteral("error"),
                                row.value(QStringLiteral("errorMessage"))}};
        }
        return QVariantMap{{QStringLiteral("path"),
                            row.value(QStringLiteral("outputPath"))}};
    };

    const QVariantMap oggLowResult = convert(QStringLiteral("ogg"),
                                             QStringLiteral("ogg-low"), 0);
    const QVariantMap oggHighResult = convert(QStringLiteral("ogg"),
                                              QStringLiteral("ogg-high"), 10);
    QVERIFY2(oggLowResult.value(QStringLiteral("error")).toString().isEmpty(),
             qPrintable(oggLowResult.value(QStringLiteral("error")).toString()));
    QVERIFY2(oggHighResult.value(QStringLiteral("error")).toString().isEmpty(),
             qPrintable(oggHighResult.value(QStringLiteral("error")).toString()));
    const QString oggLow = oggLowResult.value(QStringLiteral("path")).toString();
    const QString oggHigh = oggHighResult.value(QStringLiteral("path")).toString();
    verifyAudioFile(oggLow);
    verifyAudioFile(oggHigh);
    QVERIFY2(QFileInfo(oggHigh).size() > QFileInfo(oggLow).size(),
             "Higher Vorbis quality must produce the larger encoded stream");

    const QVariantMap flacFastResult = convert(QStringLiteral("flac"),
                                               QStringLiteral("flac-fast"), 0);
    const QVariantMap flacDenseResult = convert(QStringLiteral("flac"),
                                                QStringLiteral("flac-dense"), 8);
    QVERIFY2(flacFastResult.value(QStringLiteral("error")).toString().isEmpty(),
             qPrintable(flacFastResult.value(QStringLiteral("error")).toString()));
    QVERIFY2(flacDenseResult.value(QStringLiteral("error")).toString().isEmpty(),
             qPrintable(flacDenseResult.value(QStringLiteral("error")).toString()));
    const QString flacFast = flacFastResult.value(QStringLiteral("path")).toString();
    const QString flacDense = flacDenseResult.value(QStringLiteral("path")).toString();
    verifyAudioFile(flacFast);
    verifyAudioFile(flacDense);
    QVERIFY2(QFileInfo(flacDense).size() <= QFileInfo(flacFast).size(),
             "FLAC compression level 8 must not be larger than level 0");

    for (const QString& format : {QStringLiteral("wav"),
                                  QStringLiteral("flac"),
                                  QStringLiteral("alac")}) {
        const QVariantMap result = convert(
            format, QStringLiteral("depth-") + format,
            format == QStringLiteral("flac") ? 5 : 75,
            QStringLiteral("s24"));
        QVERIFY2(result.value(QStringLiteral("error")).toString().isEmpty(),
                 qPrintable(result.value(QStringLiteral("error")).toString()));
        const QString output = result.value(QStringLiteral("path")).toString();
        ag_metadata* metadata = nullptr;
        QCOMPARE(ag_metadata_open(output.toUtf8().constData(), &metadata), AG_OK);
        QVERIFY(metadata != nullptr);
        QCOMPARE(ag_metadata_bits_per_sample(metadata), 24);
        ag_metadata_destroy(metadata);
    }
}

void AudioToolsEndToEndTest::formatConverterRetryPreservesFrozenProfile()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString input = temp.filePath(QStringLiteral("retry-source.wav"));
    const QString outputDir = temp.filePath(QStringLiteral("out"));
    QVERIFY(agplayer::test::writeClickTrackWav(input, 123, 2));
    QVERIFY(QDir().mkpath(outputDir));

    FormatConverter converter;
    converter.loadFiles({QUrl::fromLocalFile(input)});
    waitForConverterLoad(converter);
    const QVariantMap plan = converter.buildPreflight({
        {QStringLiteral("outputFormat"), QStringLiteral("flac")},
        {QStringLiteral("outputDir"), outputDir},
        {QStringLiteral("quality"), 8},
        {QStringLiteral("bitDepth"), QStringLiteral("s24")},
        {QStringLiteral("conflictPolicy"), QStringLiteral("overwrite")},
        {QStringLiteral("keepMetadata"), false},
        {QStringLiteral("keepCover"), false}});
    QVERIFY(plan.value(QStringLiteral("ready")).toBool());
    const QVariantMap task = plan.value(QStringLiteral("tasks")).toList()
                                 .constFirst().toMap();
    const QString output = task.value(QStringLiteral("outputPath")).toString();
    QVERIFY(QDir().mkpath(output));

    QSignalSpy firstCompletion(&converter,
                               &FormatConverter::transcodeCompleted);
    converter.confirmPendingPlan();
    QVERIFY(firstCompletion.wait(30'000));
    QVariantMap failed = converter.files().constFirst().toMap();
    QCOMPARE(failed.value(QStringLiteral("status")).toString(),
             QStringLiteral("Error"));
    QVERIFY(!failed.value(QStringLiteral("errorMessage")).toString().isEmpty());
    QCOMPARE(failed.value(QStringLiteral("resolvedProfile")).toMap()
                 .value(QStringLiteral("quality")).toInt(), 8);
    QCOMPARE(failed.value(QStringLiteral("resolvedProfile")).toMap()
                 .value(QStringLiteral("bitDepth")).toString(),
             QStringLiteral("s24"));

    QVERIFY(QDir(output).removeRecursively());
    QSignalSpy retryCompletion(&converter,
                               &FormatConverter::transcodeCompleted);
    converter.retryTask(failed.value(QStringLiteral("taskId")).toString());
    QVERIFY(retryCompletion.wait(30'000));
    const QVariantMap retried = converter.files().constFirst().toMap();
    QCOMPARE(retried.value(QStringLiteral("status")).toString(),
             QStringLiteral("Done"));
    ag_metadata* metadata = nullptr;
    QCOMPARE(ag_metadata_open(output.toUtf8().constData(), &metadata), AG_OK);
    QVERIFY(metadata != nullptr);
    QCOMPARE(ag_metadata_bits_per_sample(metadata), 24);
    ag_metadata_destroy(metadata);
}

void AudioToolsEndToEndTest::formatConverterRetryPreservesVideoExtraction()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString wav = temp.filePath(QStringLiteral("video-source.wav"));
    const QString video = temp.filePath(QStringLiteral("video-source.mp4"));
    const QString outputDir = temp.filePath(QStringLiteral("out"));
    QVERIFY(agplayer::test::writeClickTrackWav(wav, 123, 2));
    QVERIFY(QFile::copy(wav, video));

    FormatConverter converter;
    converter.loadFiles({QUrl::fromLocalFile(video)});
    waitForConverterLoad(converter);
    const QVariantMap plan = converter.buildPreflight({
        {QStringLiteral("outputFormat"), QStringLiteral("flac")},
        {QStringLiteral("outputDir"), outputDir},
        {QStringLiteral("extractAudio"), true},
        {QStringLiteral("conflictPolicy"), QStringLiteral("overwrite")},
        {QStringLiteral("keepMetadata"), false},
        {QStringLiteral("keepCover"), false}});
    QVERIFY2(plan.value(QStringLiteral("ready")).toBool(),
             qPrintable(plan.value(QStringLiteral("error")).toString()));
    const QVariantMap task = plan.value(QStringLiteral("tasks")).toList()
                                 .constFirst().toMap();
    const QString output = task.value(QStringLiteral("outputPath")).toString();
    QVERIFY(QDir().mkpath(output));

    QSignalSpy firstCompletion(&converter,
                               &FormatConverter::transcodeCompleted);
    converter.confirmPendingPlan();
    QVERIFY(firstCompletion.wait(30'000));
    const QVariantMap failed = converter.files().constFirst().toMap();
    QCOMPARE(failed.value(QStringLiteral("status")).toString(),
             QStringLiteral("Error"));
    QVERIFY(QDir(output).removeRecursively());

    QSignalSpy retryCompletion(&converter,
                               &FormatConverter::transcodeCompleted);
    converter.retryTask(failed.value(QStringLiteral("taskId")).toString());
    QVERIFY(retryCompletion.wait(30'000));
    const QVariantMap retried = converter.files().constFirst().toMap();
    QCOMPARE(retried.value(QStringLiteral("status")).toString(),
             QStringLiteral("Done"));
    verifyAudioFile(output);
}

void AudioToolsEndToEndTest::formatConverterRetryPreservesMetadataSnapshot()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString input = temp.filePath(QStringLiteral("metadata-retry.wav"));
    const QString outputDir = temp.filePath(QStringLiteral("out"));
    QVERIFY(agplayer::test::writeClickTrackWav(input, 123, 2));

    FormatConverter converter;
    const QVariantMap fields{{QStringLiteral("title"),
        QVariantMap{{QStringLiteral("mode"), QStringLiteral("set")},
                    {QStringLiteral("value"),
                     QStringLiteral("Frozen retry title")}}}};
    QVERIFY(converter.setMetadataEditPlan(fields, {}));
    converter.loadFiles({QUrl::fromLocalFile(input)});
    waitForConverterLoad(converter);
    const QVariantMap plan = converter.buildPreflight({
        {QStringLiteral("outputFormat"), QStringLiteral("flac")},
        {QStringLiteral("outputDir"), outputDir},
        {QStringLiteral("conflictPolicy"), QStringLiteral("overwrite")},
        {QStringLiteral("keepMetadata"), false},
        {QStringLiteral("keepCover"), false}});
    QVERIFY2(plan.value(QStringLiteral("ready")).toBool(),
             qPrintable(plan.value(QStringLiteral("error")).toString()));
    const QVariantMap task = plan.value(QStringLiteral("tasks")).toList()
                                 .constFirst().toMap();
    const QString output = task.value(QStringLiteral("outputPath")).toString();
    QVERIFY(QDir().mkpath(output));

    QSignalSpy firstCompletion(&converter,
                               &FormatConverter::transcodeCompleted);
    converter.confirmPendingPlan();
    QVERIFY(firstCompletion.wait(30'000));
    const QVariantMap failed = converter.files().constFirst().toMap();
    QCOMPARE(failed.value(QStringLiteral("status")).toString(),
             QStringLiteral("Error"));
    QVERIFY(QDir(output).removeRecursively());

    QSignalSpy retryCompletion(&converter,
                               &FormatConverter::transcodeCompleted);
    converter.retryTask(failed.value(QStringLiteral("taskId")).toString());
    QVERIFY(retryCompletion.wait(30'000));
    const QVariantMap retried = converter.files().constFirst().toMap();
    QCOMPARE(retried.value(QStringLiteral("status")).toString(),
             QStringLiteral("Done"));
    ag_metadata* metadata = nullptr;
    QCOMPARE(ag_metadata_open(output.toUtf8().constData(), &metadata), AG_OK);
    QVERIFY(metadata != nullptr);
    QCOMPARE(QString::fromUtf8(ag_metadata_title(metadata)),
             QStringLiteral("Frozen retry title"));
    ag_metadata_destroy(metadata);
}

void AudioToolsEndToEndTest::formatConverterImportsRealAiffInput()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString wav = temp.filePath(QStringLiteral("source.wav"));
    const QString aiff = temp.filePath(QStringLiteral("source.aiff"));
    QVERIFY(agplayer::test::writeClickTrackWav(wav, 120, 1));

    const QByteArray aiffPath = aiff.toUtf8();
    ag_transcode_request_v2 request{};
    request.struct_size = sizeof(request);
    request.api_version = AG_TRANSCODE_REQUEST_V2_VERSION;
    request.output_path = aiffPath.constData();
    request.muxer_name = "aiff";
    request.codec_name = "pcm_s16be";
    request.sample_format = "s16";
    request.channel_layout = "stereo";
    QCOMPARE(ag_transcode_v2(wav.toUtf8().constData(), &request, nullptr,
                             nullptr, nullptr), AG_OK);
    verifyAudioFile(aiff);

    FormatConverter converter;
    converter.loadFiles({QUrl::fromLocalFile(aiff)});
    waitForConverterLoad(converter);
    QCOMPARE(converter.fileCount(), 1);
    const QVariantMap plan = converter.buildPreflight({
        {QStringLiteral("outputFormat"), QStringLiteral("flac")},
        {QStringLiteral("outputDir"), temp.filePath(QStringLiteral("out"))},
        {QStringLiteral("quality"), 5},
        {QStringLiteral("keepMetadata"), false},
        {QStringLiteral("keepCover"), false}});
    QVERIFY2(plan.value(QStringLiteral("ready")).toBool(),
             qPrintable(plan.value(QStringLiteral("error")).toString()));
    QSignalSpy completed(&converter, &FormatConverter::transcodeCompleted);
    converter.confirmPendingPlan();
    QVERIFY(completed.wait(30'000));
    const QVariantMap row = converter.files().constFirst().toMap();
    QCOMPARE(row.value(QStringLiteral("status")).toString(),
             QStringLiteral("Done"));
    verifyAudioFile(row.value(QStringLiteral("outputPath")).toString());
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
        QStringLiteral("flac"), QStringLiteral("aiff"),
        QStringLiteral("ogg"), QStringLiteral("opus"),
        QStringLiteral("alac"), QStringLiteral("aac")};
    QCOMPARE(keys, required);
}

void AudioToolsEndToEndTest::
    formatConverterPreflightValidatesRecommendedParameterChoices()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString input = temp.filePath(QStringLiteral("recommended.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(input, 120, 1));

    FormatConverter converter;
    converter.loadFiles({QUrl::fromLocalFile(input)});
    waitForConverterLoad(converter);

    QVariantMap plan = converter.buildPreflight({
        {QStringLiteral("outputFormat"), QStringLiteral("ogg")},
        {QStringLiteral("bitRate"), 0},
        {QStringLiteral("quality"), 6},
        {QStringLiteral("sampleRate"), 0},
        {QStringLiteral("channels"), 0},
        {QStringLiteral("outputDir"), temp.path()},
    });
    QVERIFY2(plan.value(QStringLiteral("ready")).toBool(),
             qPrintable(plan.value(QStringLiteral("error")).toString()));
    QCOMPARE(plan.value(QStringLiteral("resolvedProfile")).toMap()
                 .value(QStringLiteral("quality")).toInt(), 6);

    plan = converter.buildPreflight({
        {QStringLiteral("outputFormat"), QStringLiteral("aiff")},
        {QStringLiteral("bitRate"), 0},
        {QStringLiteral("quality"), 0},
        {QStringLiteral("sampleRate"), 0},
        {QStringLiteral("bitDepth"), QStringLiteral("s24")},
        {QStringLiteral("channels"), 0},
        {QStringLiteral("keepMetadata"), false},
        {QStringLiteral("keepCover"), false},
        {QStringLiteral("outputDir"), temp.path()},
    });
    QVERIFY2(plan.value(QStringLiteral("ready")).toBool(),
             qPrintable(plan.value(QStringLiteral("error")).toString()));
    const QVariantMap profile = plan.value(
        QStringLiteral("resolvedProfile")).toMap();
    QCOMPARE(profile.value(QStringLiteral("format")).toString(),
             QStringLiteral("aiff"));
    QCOMPARE(profile.value(QStringLiteral("bitDepth")).toString(),
             QStringLiteral("s24"));
    QVERIFY(plan.value(QStringLiteral("tasks")).toList().front().toMap()
                .value(QStringLiteral("outputPath")).toString()
                .endsWith(QStringLiteral(".aiff")));

    plan = converter.buildPreflight({
        {QStringLiteral("outputFormat"), QStringLiteral("mp3")},
        {QStringLiteral("bitRate"), 96000},
        {QStringLiteral("sampleRate"), 44100},
        {QStringLiteral("outputDir"), temp.path()},
    });
    QVERIFY(!plan.value(QStringLiteral("ready")).toBool());
    QVERIFY(plan.value(QStringLiteral("error")).toString()
                .contains(QStringLiteral("bitRate")));
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
            || key == QStringLiteral("alac")
            || key == QStringLiteral("aiff");
        const int sampleRate = key == QStringLiteral("opus") ? 48000 : 44100;
        converter.start(key, lossless ? 0 : 192000, sampleRate, 2,
                        outputDir, true, false, false);
        if (completed.isEmpty()) {
            QVERIFY2(completed.wait(30000), qPrintable(key));
        }
        const QVariantMap row = converter.files().first().toMap();
        QVERIFY2(converter.failedCount() == 0,
                 qPrintable(QStringLiteral("%1: %2")
                                .arg(key, row.value(QStringLiteral("errorMessage"))
                                              .toString())));
        QCOMPARE(row.value(QStringLiteral("status")).toString(),
                 QStringLiteral("Done"));
        verifyAudioFile(row.value(QStringLiteral("outputPath")).toString(),
                        key == QStringLiteral("opus") ? 48000 : 44100, key);
    }
}

void AudioToolsEndToEndTest::formatConverterTranscodesRealAacToRequiredContainers()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString wav = temp.filePath(QStringLiteral("aac-source.wav"));
    const QString aac = temp.filePath(QStringLiteral("aac-source.aac"));
    QVERIFY(agplayer::test::writeClickTrackWav(wav, 120, 2));
    QCOMPARE(ag_transcode(wav.toUtf8().constData(), aac.toUtf8().constData(),
                          "aac", 192000, 44100, 2, nullptr, nullptr, nullptr),
             AG_OK);

    ag_metadata* sourceMetadata = nullptr;
    QCOMPARE(ag_metadata_open(aac.toUtf8().constData(), &sourceMetadata), AG_OK);
    QVERIFY(sourceMetadata != nullptr);
    const qint64 sourceDurationMs = ag_metadata_duration_ms(sourceMetadata);
    QCOMPARE(ag_metadata_sample_rate(sourceMetadata), 44100);
    QCOMPARE(ag_metadata_channels(sourceMetadata), 2);
    ag_metadata_destroy(sourceMetadata);
    agplayer::TranscodeVerificationPlan sourceVerificationPlan;
    agplayer::TranscodeVerificationResult sourceVerification;
    std::string sourceVerificationError;
    QCOMPARE(agplayer::verify_transcoded_output(
                 aac.toUtf8().toStdString(), sourceVerificationPlan,
                 sourceVerification, sourceVerificationError),
             AG_OK);
    QVERIFY(sourceVerification.decoded_duration_ms > sourceDurationMs);

    struct ExpectedOutput {
        const char* format;
        const char* extension;
        const char* muxer;
        const char* encoder;
        const char* readbackContainer;
        const char* readbackCodec;
        int sampleRate;
        bool lossless;
    };
    const std::array<ExpectedOutput, 4> outputs{{
        {"opus", "opus", "ogg", "libopus", "ogg", "opus", 48000, false},
        {"ogg", "ogg", "ogg", "libvorbis", "ogg", "vorbis", 44100, false},
        {"alac", "m4a", "ipod", "alac", "mov", "alac", 44100, true},
        {"aiff", "aiff", "aiff", "pcm_s16be", "aiff", "pcm_s16be", 44100, true},
    }};

    for (const ExpectedOutput& expected : outputs) {
        const QString format = QString::fromLatin1(expected.format);
        const QString outputDir = temp.filePath(format);
        QVERIFY(QDir().mkpath(outputDir));

        FormatConverter converter;
        converter.loadFiles({QUrl::fromLocalFile(aac)});
        waitForConverterLoad(converter);
        const QVariantMap plan = converter.buildPreflight({
            {QStringLiteral("outputFormat"), format},
            {QStringLiteral("preset"), QStringLiteral("custom")},
            {QStringLiteral("bitRate"),
             format == QStringLiteral("opus") ? 192000 : 0},
            {QStringLiteral("quality"),
             format == QStringLiteral("ogg") ? 8 : 100},
            {QStringLiteral("sampleRate"), 0},
            {QStringLiteral("channels"), 0},
            {QStringLiteral("channelLayout"), QString()},
            {QStringLiteral("bitDepth"), QString()},
            {QStringLiteral("outputDir"), outputDir},
            {QStringLiteral("keepMetadata"), false},
            {QStringLiteral("keepCover"), false},
            {QStringLiteral("preserveDirectories"), false},
            {QStringLiteral("extractAudio"), false},
        });
        QVERIFY2(plan.value(QStringLiteral("ready")).toBool(),
                 qPrintable(format + QStringLiteral(": ")
                            + plan.value(QStringLiteral("error")).toString()));
        const QVariantMap profile = plan.value(
            QStringLiteral("resolvedProfile")).toMap();
        QCOMPARE(profile.value(QStringLiteral("format")).toString(), format);
        QCOMPARE(profile.value(QStringLiteral("muxer")).toString(),
                 QString::fromLatin1(expected.muxer));
        QCOMPARE(profile.value(QStringLiteral("codec")).toString(),
                 QString::fromLatin1(expected.encoder));
        QCOMPARE(profile.value(QStringLiteral("sampleRate")).toInt(),
                 expected.sampleRate);
        QCOMPARE(profile.value(QStringLiteral("channelLayout")).toString(),
                 QStringLiteral("stereo"));

        QSignalSpy completed(&converter, &FormatConverter::transcodeCompleted);
        converter.confirmPendingPlan();
        if (completed.isEmpty()) {
            QVERIFY2(completed.wait(30'000), qPrintable(format));
        }
        const QVariantMap row = converter.files().first().toMap();
        QVERIFY2(converter.failedCount() == 0,
                 qPrintable(format + QStringLiteral(": ")
                            + row.value(QStringLiteral("errorMessage")).toString()));
        QCOMPARE(converter.doneCount(), 1);
        QCOMPARE(row.value(QStringLiteral("status")).toString(),
                 QStringLiteral("Done"));
        const QString outputPath = row.value(QStringLiteral("outputPath")).toString();
        QCOMPARE(QFileInfo(outputPath).suffix().toLower(),
                 QString::fromLatin1(expected.extension));

        agplayer::MediaProbe readback;
        std::string probeError;
        QCOMPARE(agplayer::probe_transcode_input(
                     outputPath.toUtf8().toStdString(), readback, probeError),
                 AG_OK);
        QCOMPARE(readback.audio_streams.size(), std::size_t{1});
        QVERIFY(QString::fromStdString(readback.container).contains(
            QString::fromLatin1(expected.readbackContainer),
            Qt::CaseInsensitive));
        const agplayer::AudioStreamProbe& audio = readback.audio_streams.front();
        QCOMPARE(QString::fromStdString(audio.codec),
                 QString::fromLatin1(expected.readbackCodec));
        QCOMPARE(audio.sample_rate,
                 profile.value(QStringLiteral("sampleRate")).toInt());
        QCOMPARE(QString::fromStdString(audio.channel_layout),
                 profile.value(QStringLiteral("channelLayout")).toString());

        agplayer::TranscodeVerificationPlan verificationPlan;
        verificationPlan.expected_duration_ms =
            sourceVerification.decoded_duration_ms;
        verificationPlan.lossless = expected.lossless;
        agplayer::TranscodeVerificationResult verification;
        std::string verificationError;
        QCOMPARE(agplayer::verify_transcoded_output(
                     outputPath.toUtf8().toStdString(), verificationPlan,
                     verification, verificationError),
                 AG_OK);
        QVERIFY(verification.decoded_duration_ms > 0);

        ag_metadata* metadata = nullptr;
        QCOMPARE(ag_metadata_open(outputPath.toUtf8().constData(), &metadata),
                 AG_OK);
        QVERIFY(metadata != nullptr);
        QVERIFY(ag_metadata_duration_ms(metadata) > 0);
        QCOMPARE(ag_metadata_sample_rate(metadata), expected.sampleRate);
        QCOMPARE(ag_metadata_channels(metadata), 2);
        ag_metadata_destroy(metadata);
    }
}

void AudioToolsEndToEndTest::
    formatConverterValidatesLosslessResolvedProfileReadback()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString input = temp.filePath(QStringLiteral("profile-source.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(input, 120, 1));

    struct LosslessCase {
        const char* format;
        const char* sampleFormat;
    };
    constexpr std::array<LosslessCase, 2> cases{{
        {"alac", "s32p"},
        {"aiff", "s32"},
    }};
    for (const LosslessCase& test_case : cases) {
        const QString format = QString::fromLatin1(test_case.format);
        const QString output_dir = temp.filePath(format);
        QVERIFY(QDir().mkpath(output_dir));
        FormatConverter converter;
        converter.loadFiles({QUrl::fromLocalFile(input)});
        waitForConverterLoad(converter);
        const QVariantMap plan = converter.buildPreflight({
            {QStringLiteral("outputFormat"), format},
            {QStringLiteral("preset"), QStringLiteral("custom")},
            {QStringLiteral("sampleRate"), 44'100},
            {QStringLiteral("channels"), 2},
            {QStringLiteral("channelLayout"), QStringLiteral("stereo")},
            {QStringLiteral("bitDepth"), QStringLiteral("s24")},
            {QStringLiteral("outputDir"), output_dir},
            {QStringLiteral("keepMetadata"), false},
            {QStringLiteral("keepCover"), false},
        });
        QVERIFY2(plan.value(QStringLiteral("ready")).toBool(),
                 qPrintable(plan.value(QStringLiteral("error")).toString()));
        const QVariantMap profile = plan.value(
            QStringLiteral("resolvedProfile")).toMap();
        QCOMPARE(profile.value(QStringLiteral("bitDepth")).toString(),
                 QStringLiteral("s24"));
        QCOMPARE(profile.value(QStringLiteral("sampleFormat")).toString(),
                 QString::fromLatin1(test_case.sampleFormat));

        QSignalSpy completed(&converter, &FormatConverter::transcodeCompleted);
        converter.confirmPendingPlan();
        if (completed.isEmpty()) {
            QVERIFY2(completed.wait(30'000), qPrintable(format));
        }
        const QVariantMap row = converter.files().first().toMap();
        QVERIFY2(converter.failedCount() == 0,
                 qPrintable(row.value(QStringLiteral("errorMessage")).toString()));
        const QString output = row.value(QStringLiteral("outputPath")).toString();

        agplayer::MediaProbe readback;
        std::string probe_error;
        QCOMPARE(agplayer::probe_transcode_input(
                     output.toUtf8().toStdString(), readback, probe_error),
                 AG_OK);
        QCOMPARE(readback.audio_streams.size(), std::size_t{1});
        const agplayer::AudioStreamProbe& audio = readback.audio_streams.front();
        QCOMPARE(audio.bits_per_sample, 24);
        QCOMPARE(QString::fromStdString(audio.sample_format),
                 QString::fromLatin1(test_case.sampleFormat));

        QString validation_error;
        QVERIFY2(format_converter_detail::validate_audio_output(
                     output, profile, validation_error),
                 qPrintable(validation_error));

        QVariantMap wrong_depth = profile;
        wrong_depth.insert(QStringLiteral("bitDepth"), QStringLiteral("s16"));
        validation_error.clear();
        QVERIFY(!format_converter_detail::validate_audio_output(
            output, wrong_depth, validation_error));
        QCOMPARE(validation_error,
                 QStringLiteral("resolved profile bitDepth mismatch "
                                "(expected=s16, actual=s24)"));

        QVariantMap wrong_format = profile;
        wrong_format.insert(QStringLiteral("sampleFormat"),
                            QStringLiteral("s16"));
        validation_error.clear();
        QVERIFY(!format_converter_detail::validate_audio_output(
            output, wrong_format, validation_error));
        QCOMPARE(validation_error,
                 QStringLiteral("resolved profile sampleFormat mismatch "
                                "(expected=s16, actual=%1)")
                     .arg(QString::fromLatin1(test_case.sampleFormat)));
    }
}

void AudioToolsEndToEndTest::formatConverterSeparatesFinishedDoneAndFailedCounts()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString successInput = temp.filePath(QStringLiteral("success.wav"));
    const QString failedInput = temp.filePath(QStringLiteral("failure.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(successInput, 120, 1));
    QVERIFY(QFile::copy(successInput, failedInput));

    FormatConverter converter;
    converter.loadFiles({QUrl::fromLocalFile(successInput),
                         QUrl::fromLocalFile(failedInput)});
    waitForConverterLoad(converter);
    QCOMPARE(converter.fileCount(), 2);
    QVERIFY(QFile::remove(failedInput));

    QSignalSpy completed(&converter, &FormatConverter::transcodeCompleted);
    converter.start(QStringLiteral("mp3"), 192000, 44100, 2,
                    temp.filePath(QStringLiteral("outputs")), false, false,
                    false);
    if (completed.isEmpty()) {
        QVERIFY(completed.wait(30'000));
    }
    QCOMPARE(converter.completedCount(), 2);
    QCOMPARE(converter.doneCount(), 1);
    QCOMPARE(converter.failedCount(), 1);
    QCOMPARE(completed.first().at(0).toInt(), 1);
    QCOMPARE(completed.first().at(1).toInt(), 1);

    auto* filter = converter.filteredTaskModel();
    QVERIFY(filter != nullptr);
    QVERIFY(filter->setProperty("statusFilter", QStringLiteral("Done")));
    QCOMPARE(filter->property("visibleCount").toInt(), 1);
    QVERIFY(filter->setProperty("statusFilter", QStringLiteral("Error")));
    QCOMPARE(filter->property("visibleCount").toInt(), 1);
}

void AudioToolsEndToEndTest::formatConverterDeduplicatesCanonicalImportPaths()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString input = temp.filePath(QStringLiteral("Canonical-Source.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(input, 120, 1));
    const QString dottedAlias = QDir(temp.path()).filePath(
        QStringLiteral("./Canonical-Source.wav"));

    FormatConverter converter;
    converter.loadFiles({QUrl::fromLocalFile(input),
                         QUrl::fromLocalFile(input),
                         QUrl::fromLocalFile(dottedAlias)});
    waitForConverterLoad(converter);
    QCOMPARE(converter.fileCount(), 1);

#ifdef Q_OS_WIN
    converter.loadFiles({QUrl::fromLocalFile(input.toUpper())});
    waitForConverterLoad(converter);
    QCOMPARE(converter.fileCount(), 1);
#endif
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

void AudioToolsEndToEndTest::allPlaybackLocationsPauseEachOther()
{
    const QString fixture = QCoreApplication::applicationDirPath() + "/fixtures/sine-440hz.wav";
    QVERIFY(QFileInfo::exists(fixture));
    ag_player* core = nullptr;
    const ag_player_config config{AG_AUDIO_BACKEND_NULL, 0U};
    QCOMPARE(ag_player_create_with_config(&config, &core), AG_OK);
    const auto cleanup = qScopeGuard([&] { ag_player_destroy(core); });
    LibraryModel library;
    TrackRecord track;
    track.trackId = "focus-track";
    track.path = fixture;
    track.available = true;
    QVERIFY(library.append(track));
    PlaybackController playback(core, &library);
    AudioEditorController editor(AG_AUDIO_BACKEND_NULL);
    editor.setPlaybackController(&playback);
    AudioPreviewController preview(AG_AUDIO_BACKEND_NULL, &playback);
    AudioToolsController tools;
    tools.bindPlaybackControllers(&playback, &editor, &preview);
    QVERIFY(editor.openFile(QUrl::fromLocalFile(fixture)));
    QTRY_VERIFY_WITH_TIMEOUT(editor.hasDocument() && !editor.loading(), 5000);
    QVERIFY(playback.playTrackIds({track.trackId}, track.trackId));
    playback.seek(500);
    preview.play(QUrl::fromLocalFile(fixture));
    QTRY_VERIFY(preview.playing());
    QTRY_COMPARE(playback.state(), PlaybackController::Paused);
    QVERIFY(playback.positionMs() >= 450);
    preview.seek(600);
    playback.play();
    QTRY_COMPARE(playback.state(), PlaybackController::Playing);
    QVERIFY(!preview.playing());
    QVERIFY(preview.hasSource());
    QVERIFY(preview.positionMs() >= 550);
    preview.resume();
    QTRY_VERIFY(preview.playing());
    QTRY_COMPARE(playback.state(), PlaybackController::Paused);
    QVERIFY(editor.playPause());
    QTRY_VERIFY(editor.playing());
    QVERIFY(!preview.playing());
    editor.seekMs(700);
    preview.toggle(QUrl::fromLocalFile(fixture));
    QTRY_VERIFY(preview.playing());
    QVERIFY(!editor.playing());
    QVERIFY(editor.positionMs() >= 650);
    QVERIFY(editor.playPause());
    QVERIFY(!preview.playing());
    playback.play();
    QTRY_COMPARE(playback.state(), PlaybackController::Playing);
    QVERIFY(!editor.playing());
    QVERIFY(editor.positionMs() >= 650);
    QVERIFY(editor.playPause());
    QVERIFY(preview.playMix({{"one", fixture, 0.5}, {"two", fixture, 0.5}}, 500));
    QVERIFY(!editor.playing());
    QVERIFY(preview.playing());
    playback.play();
    QVERIFY(!preview.playing());
    QVERIFY(preview.mixActive());
    preview.resume();
    QVERIFY(preview.playing());
    QTRY_COMPARE(playback.state(), PlaybackController::Paused);
    QVERIFY(editor.playPause());
    QVERIFY(!preview.playing());
    QVERIFY(playback.playTrackIds({track.trackId}, track.trackId));
    QVERIFY(!editor.playing());
    editor.deactivate();
    QTRY_COMPARE(playback.state(), PlaybackController::Playing);
    preview.play(QUrl::fromLocalFile(fixture));
    preview.setDspParameters(1.2, 100, true, false, false, true);
    QVERIFY(editor.playPause());
    QTRY_VERIFY_WITH_TIMEOUT(!preview.processing(), 10000);
    QVERIFY(!preview.playing());
}

void AudioToolsEndToEndTest::audioToolsControllerKeepsLegacyIdsAndSupportsSeparation()
{
    AudioToolsController controller;
    QCOMPARE(controller.currentTool(), 0);

    controller.setCurrentTool(1);
    QCOMPARE(controller.currentTool(), 1);

    controller.setCurrentTool(3);
    QCOMPARE(controller.currentTool(), 3);

    controller.setCurrentTool(4);
    QCOMPARE(controller.currentTool(), 4);

    controller.setCurrentTool(5);
    QCOMPARE(controller.currentTool(), 5);
    controller.setCurrentTool(6);
    QCOMPARE(controller.currentTool(), 5);
    controller.setCurrentTool(-1);
    QCOMPARE(controller.currentTool(), 5);
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
    fields.insert(QStringLiteral("customTag"), QVariantMap{
        {QStringLiteral("mode"), QStringLiteral("set")},
        {QStringLiteral("value"), QStringLiteral("转换标签")}});
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
    QCOMPARE(QString::fromUtf8(ag_metadata_custom_tag(metadata)),
             QStringLiteral("转换标签"));
    ag_metadata_destroy(metadata);
    QVERIFY(QFileInfo::exists(input));
}

void AudioToolsEndToEndTest::formatConverterScopesMetadataPlanToOneBatch()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString oldInput = temp.filePath(QStringLiteral("old-queue.wav"));
    const QString targetInput = temp.filePath(QStringLiteral("metadata-target.wav"));
    const QString firstOutputDir = temp.filePath(QStringLiteral("first-output"));
    const QString secondOutputDir = temp.filePath(QStringLiteral("second-output"));
    QVERIFY(agplayer::test::writeClickTrackWav(oldInput, 120, 1));
    QVERIFY(agplayer::test::writeClickTrackWav(targetInput, 124, 1));
    QVERIFY(QDir().mkpath(firstOutputDir));
    QVERIFY(QDir().mkpath(secondOutputDir));

    FormatConverter converter;
    converter.loadFiles({QUrl::fromLocalFile(oldInput)});
    waitForConverterLoad(converter);

    const QVariantMap fields{{QStringLiteral("title"),
                              QVariantMap{{QStringLiteral("mode"),
                                           QStringLiteral("set")},
                                          {QStringLiteral("value"),
                                           QStringLiteral("Scoped title")}}}};
    QVERIFY(converter.setMetadataEditPlanForFiles(
        fields, {}, {QUrl::fromLocalFile(targetInput)}));
    converter.loadFiles({QUrl::fromLocalFile(targetInput)});
    waitForConverterLoad(converter);

    QSignalSpy completed(&converter, &FormatConverter::transcodeCompleted);
    converter.start(QStringLiteral("flac"), 0, 44100, 2, firstOutputDir,
                    false, false, false);
    QVERIFY(completed.wait(30'000));
    QCOMPARE(completed.first().first().toInt(), 2);

    const auto readTitle = [](const QString& path) {
        ag_metadata* metadata = nullptr;
        if (ag_metadata_open(path.toUtf8().constData(), &metadata) != AG_OK
            || metadata == nullptr) {
            return QStringLiteral("<open failed>");
        }
        const QString title = QString::fromUtf8(ag_metadata_title(metadata));
        ag_metadata_destroy(metadata);
        return title;
    };
    QCOMPARE(readTitle(QDir(firstOutputDir).filePath(QStringLiteral("old-queue.flac"))),
             QString());
    QCOMPARE(readTitle(QDir(firstOutputDir).filePath(
                 QStringLiteral("metadata-target.flac"))),
             QStringLiteral("Scoped title"));

    completed.clear();
    converter.start(QStringLiteral("flac"), 0, 44100, 2, secondOutputDir,
                    false, false, false);
    QVERIFY(completed.wait(30'000));
    QCOMPARE(readTitle(QDir(secondOutputDir).filePath(
                 QStringLiteral("metadata-target.flac"))),
             QString());

    FormatConverter cancelled;
    QVERIFY(cancelled.setMetadataEditPlanForFiles(
        fields, {}, {QUrl::fromLocalFile(targetInput)}));
    cancelled.cancel();
    cancelled.loadFiles({QUrl::fromLocalFile(targetInput)});
    waitForConverterLoad(cancelled);
    QSignalSpy cancelledCompleted(&cancelled,
                                  &FormatConverter::transcodeCompleted);
    const QString cancelledOutputDir = temp.filePath(QStringLiteral("cancelled-output"));
    QVERIFY(QDir().mkpath(cancelledOutputDir));
    cancelled.start(QStringLiteral("flac"), 0, 44100, 2,
                    cancelledOutputDir, false, false, false);
    QVERIFY(cancelledCompleted.wait(30'000));
    QCOMPARE(readTitle(QDir(cancelledOutputDir).filePath(
                 QStringLiteral("metadata-target.flac"))),
             QString());
}

void AudioToolsEndToEndTest::metadataEditorReadbackCannotOverwriteRelocatedLibraryTrack()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = temp.filePath(QStringLiteral("original.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(path, 120, 1));
    LibraryModel library;
    TrackRecord record;
    record.trackId = QStringLiteral("relocated");
    record.path = path;
    record.title = QStringLiteral("Before");
    QVERIFY(library.append(record));
    MetadataEditor editor;
    editor.setLibraryModel(&library);
    QSignalSpy loaded(&editor, &MetadataEditor::entriesLoaded);
    editor.loadFiles({QUrl::fromLocalFile(path)});
    QVERIFY(loaded.wait(30000));

    QThreadPool* pool = QThreadPool::globalInstance();
    const int oldLimit = pool->maxThreadCount();
    pool->setMaxThreadCount(1);
    QSemaphore entered;
    QSemaphore release;
    const auto restorePool = qScopeGuard([&] {
        release.release();
        pool->waitForDone();
        pool->setMaxThreadCount(oldLimit);
    });
    connect(&editor, &MetadataEditor::preflightCompleted, this, [&] {
        pool->start([&] { entered.release(); release.acquire(); });
        QVERIFY(entered.tryAcquire(1, 3000));
    });
    QSignalSpy preflight(&editor, &MetadataEditor::preflightCompleted);
    QSignalSpy applied(&editor, &MetadataEditor::metadataApplied);
    editor.applyMetadata({{QStringLiteral("title"), QVariantMap{
        {QStringLiteral("mode"), QStringLiteral("set")},
        {QStringLiteral("value"), QStringLiteral("Written")}}}}, {});
    QVERIFY(preflight.wait(30000));
    QVERIFY(editor.busy());
    const QString relocated = temp.filePath(QStringLiteral("relocated.wav"));
    QVERIFY(library.updateTrackPath(record.trackId, relocated));
    release.release();
    QVERIFY(applied.wait(30000));
    QCOMPARE(editor.successCount(), 1);
    QCOMPARE(library.recordForId(record.trackId)->title, QStringLiteral("Before"));
    QCOMPARE(library.recordForId(record.trackId)->path, canonicalLibraryPath(relocated));
    ag_metadata* metadata = nullptr;
    QCOMPARE(ag_metadata_open(path.toUtf8().constData(), &metadata), AG_OK);
    const QString written = QString::fromUtf8(ag_metadata_title(metadata));
    ag_metadata_destroy(metadata);
    QCOMPARE(written, QStringLiteral("Written"));
}

void AudioToolsEndToEndTest::performanceConverterCheckedRemoval()
{
    if (!qEnvironmentVariableIsSet("AGPLAYER_RUN_PERFORMANCE"))
        QSKIP("Opt-in real-file performance measurement");
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString source = temp.filePath(QStringLiteral("source.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(source, 120, 1));
    QList<QUrl> urls;
    for (int i = 0; i < 2000; ++i) {
        const QString path = temp.filePath(QStringLiteral("track-%1.wav").arg(i));
        QVERIFY(QFile::copy(source, path));
        urls.append(QUrl::fromLocalFile(path));
    }
    FormatConverter converter;
    QElapsedTimer elapsed;
    elapsed.start();
    converter.loadFiles(urls);
    QTRY_VERIFY_WITH_TIMEOUT(!converter.busy(), 60000);
    const qint64 loadMs = elapsed.elapsed();
    QCOMPARE(converter.fileCount(), 2000);
    QStringList remaining;
    for (int i = 0; i < converter.taskModel()->rowCount(); ++i) {
        const QString taskId = converter.taskModel()->data(
            converter.taskModel()->index(i, 0), Qt::UserRole + 1).toString();
        if ((i % 2) == 1) {
            converter.setTaskChecked(taskId, false);
            remaining.append(taskId);
        }
    }
    elapsed.restart();
    converter.removeChecked();
    const qint64 removeUs = elapsed.nsecsElapsed() / 1000;
    QCOMPARE(converter.fileCount(), 1000);
    QCOMPARE(converter.taskModel()->rowCount(), 1000);
    for (int i = 0; i < remaining.size(); ++i) {
        QCOMPARE(converter.taskModel()->data(converter.taskModel()->index(i, 0),
                                             Qt::UserRole + 1).toString(), remaining.at(i));
    }
    qInfo("PERF converter files=2000 load_ms=%lld checked_remove_us=%lld remaining=1000", loadMs, removeUs);
}

void AudioToolsEndToEndTest::performanceConversionAndMetadataBatch()
{
    if (!qEnvironmentVariableIsSet("AGPLAYER_RUN_PERFORMANCE"))
        QSKIP("Opt-in real-file performance measurement");
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString source = temp.filePath(QStringLiteral("source.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(source, 120, 30));
    const QString outputDir = temp.filePath(QStringLiteral("output"));
    QVERIFY(QDir().mkpath(outputDir));
    QList<QUrl> urls;
    for (int i = 0; i < 8; ++i) {
        const QString path = temp.filePath(QStringLiteral("track-%1.wav").arg(i));
        QVERIFY(QFile::copy(source, path));
        urls.append(QUrl::fromLocalFile(path));
    }
    FormatConverter converter;
    converter.setParallelJobs(2);
    converter.loadFiles(urls);
    waitForConverterLoad(converter);
    QElapsedTimer elapsed;
    elapsed.start();
    QSignalSpy completed(&converter, &FormatConverter::transcodeCompleted);
    converter.start(QStringLiteral("flac"), 0, 16000, 1, outputDir, true, false, false);
    QVERIFY(completed.wait(60000));
    const qint64 convertMs = elapsed.elapsed();
    QCOMPARE(converter.failedCount(), 0);
    QCOMPARE(converter.doneCount(), 8);
    qint64 outputBytes = 0;
    for (const QVariant& value : converter.files()) {
        const QString path = value.toMap().value(QStringLiteral("outputPath")).toString();
        verifyAudioFile(path, 16000);
        agplayer::MediaProbe probe;
        std::string error;
        QCOMPARE(agplayer::probe_transcode_input(path.toUtf8().toStdString(), probe, error), AG_OK);
        QCOMPARE(probe.audio_streams.front().codec, std::string("flac"));
        QCOMPARE(probe.audio_streams.front().duration_ms, 30000);
        outputBytes += QFileInfo(path).size();
    }
    qInfo("PERF conversion files=8 pcm=16000Hz/mono/16bit/30s codec=flac parallel=2 elapsed_ms=%lld output_bytes=%lld",
          convertMs, outputBytes);

    const QString flac = converter.files().first().toMap().value(QStringLiteral("outputPath")).toString();
    QList<QUrl> metadataUrls;
    for (int i = 0; i < 400; ++i) {
        const QString path = temp.filePath(QStringLiteral("metadata-%1.flac").arg(i));
        QVERIFY(QFile::copy(flac, path));
        metadataUrls.append(QUrl::fromLocalFile(path));
    }
    MetadataEditor editor;
    LibraryModel library;
    const bool attachedLibrary = qEnvironmentVariableIsSet("AGPLAYER_PERF_LIBRARY");
    if (attachedLibrary) {
        QList<TrackRecord> records;
        for (int i = 0; i < metadataUrls.size(); ++i) {
            TrackRecord record;
            record.trackId = QStringLiteral("perf-%1").arg(i);
            record.path = metadataUrls.at(i).toLocalFile();
            record.title = QStringLiteral("Before");
            record.favorite = true;
            record.rating = 4;
            records.append(record);
        }
        library.appendBatch(std::move(records));
        editor.setLibraryModel(&library);
    }
    QSignalSpy loaded(&editor, &MetadataEditor::entriesLoaded);
    editor.loadFiles(metadataUrls);
    QVERIFY(loaded.wait(60000));
    QCOMPARE(editor.fileCount(), 400);
    QSignalSpy applied(&editor, &MetadataEditor::metadataApplied);
    QSignalSpy libraryChanged(&library, &QAbstractItemModel::dataChanged);
    elapsed.restart();
    qint64 lastTick = 0;
    qint64 maxGap = 0;
    QTimer heartbeat;
    heartbeat.setInterval(5);
    connect(&heartbeat, &QTimer::timeout, this, [&] {
        const qint64 now = elapsed.elapsed();
        maxGap = qMax(maxGap, now - lastTick);
        lastTick = now;
    });
    heartbeat.start();
    editor.applyMetadata({{QStringLiteral("title"), QVariantMap{
        {QStringLiteral("mode"), QStringLiteral("set")},
        {QStringLiteral("value"), QStringLiteral("Performance batch")}}}}, {});
    QVERIFY(applied.wait(60000));
    maxGap = qMax(maxGap, elapsed.elapsed() - lastTick);
    qInfo("PERF metadata files=400 field=title library=%d elapsed_ms=%lld heartbeat_max_gap_ms=%lld",
          attachedLibrary, elapsed.elapsed(), maxGap);
    QCOMPARE(editor.successCount(), 400);
    QCOMPARE(editor.failedCount(), 0);
    if (attachedLibrary) QCOMPARE(libraryChanged.count(), 1);
    heartbeat.stop();
    for (int i = 0; i < editor.fileCount(); ++i) {
        QCOMPARE(editor.entryAt(i).value(QStringLiteral("title")).toString(),
                 QStringLiteral("Performance batch"));
        const QString path = metadataUrls.at(i).toLocalFile();
        ag_metadata* metadata = nullptr;
        QCOMPARE(ag_metadata_open(path.toUtf8().constData(), &metadata), AG_OK);
        const QString title = QString::fromUtf8(ag_metadata_title(metadata));
        ag_metadata_destroy(metadata);
        QCOMPARE(title, QStringLiteral("Performance batch"));
        QVERIFY(!QFileInfo::exists(path + QStringLiteral(".agbak")));
        if (attachedLibrary) {
            const TrackRecord* record = library.recordForId(QStringLiteral("perf-%1").arg(i));
            QVERIFY(record != nullptr);
            QCOMPARE(record->title, title);
            QCOMPARE(record->path, canonicalLibraryPath(path));
            QCOMPARE(record->durationMs, 30000);
            QCOMPARE(record->sampleRate, 16000);
            QCOMPARE(record->rating, 4);
            QVERIFY(record->favorite);
        }
    }
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

    const QString originalYearDate = QStringLiteral("1999-07-04");
    QCOMPARE(ag_metadata_write(input.toUtf8().constData(), nullptr, nullptr,
                               nullptr,
                               originalYearDate.toUtf8().constData(), nullptr,
                               nullptr, nullptr, 0, nullptr),
             AG_OK);

    {
        LibraryModel preservationLibrary;
        TrackRecord preservationTrack;
        preservationTrack.trackId = QStringLiteral("metadata-preservation-track");
        preservationTrack.path = input;
        preservationTrack.available = true;
        QVERIFY(preservationLibrary.append(preservationTrack));

        MetadataEditor preservationEditor;
        preservationEditor.setLibraryModel(&preservationLibrary);
        QSignalSpy preservationLoaded(&preservationEditor,
                                      &MetadataEditor::entriesLoaded);
        preservationEditor.loadFiles({QUrl::fromLocalFile(input)});
        QVERIFY(preservationLoaded.wait(30000));

        const QVariantMap customTagOnly{
            {QStringLiteral("customTag"),
             QVariantMap{{QStringLiteral("mode"), QStringLiteral("set")},
                         {QStringLiteral("value"), QStringLiteral("电子")}}}};
        QSignalSpy preservationApplied(&preservationEditor,
                                       &MetadataEditor::metadataApplied);
        preservationEditor.applyMetadata(customTagOnly, {});
        QVERIFY(preservationApplied.wait(30000));
        QCOMPARE(preservationApplied.first().first().toInt(), 1);
        QCOMPARE(preservationLibrary.data(preservationLibrary.index(0, 0),
                                          LibraryModel::YearRole).toString(),
                 originalYearDate);
        QCOMPARE(preservationLibrary.data(preservationLibrary.index(0, 0),
                                          LibraryModel::DateRole).toString(),
                 originalYearDate);

        ag_metadata* preservedMetadata = nullptr;
        QCOMPARE(ag_metadata_open(input.toUtf8().constData(), &preservedMetadata),
                 AG_OK);
        QCOMPARE(QString::fromUtf8(ag_metadata_custom_tag(preservedMetadata)),
                 QStringLiteral("电子"));
        QCOMPARE(QString::fromUtf8(ag_metadata_year(preservedMetadata)),
                 originalYearDate);
        QCOMPARE(QString::fromUtf8(ag_metadata_date(preservedMetadata)),
                 originalYearDate);
        ag_metadata_destroy(preservedMetadata);
    }

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
    setField(QStringLiteral("album"), QStringLiteral("Edited album"));
    setField(QStringLiteral("albumArtist"), QStringLiteral("Album artist"));
    setField(QStringLiteral("genre"), QStringLiteral("Edited genre"));
    setField(QStringLiteral("customTag"), QStringLiteral("电子"));
    setField(QStringLiteral("composer"), QStringLiteral("Composer"));
    setField(QStringLiteral("bpm"), QStringLiteral("128.50"));

    const QString coverPath = temp.filePath(QStringLiteral("new-cover.bmp"));
    QImage coverImage(2, 2, QImage::Format_RGB32);
    coverImage.fill(Qt::red);
    QVERIFY(coverImage.save(coverPath, "BMP"));
    editor.setCoverImage(QUrl::fromLocalFile(coverPath));
    fields.insert(QStringLiteral("coverMode"), QStringLiteral("set"));

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
    const QVariantMap updatedEntry = editor.entryAt(0);
    QCOMPARE(updatedEntry.value(QStringLiteral("date")).toString(),
             originalYearDate);
    QCOMPARE(updatedEntry.value(QStringLiteral("customTag")).toString(),
             QStringLiteral("电子"));
    QVERIFY(!updatedEntry.contains(QStringLiteral("year")));
    QCOMPARE(library.data(library.index(0, 0), LibraryModel::TitleRole).toString(),
             QStringLiteral("Edited title"));
    QCOMPARE(library.data(library.index(0, 0), LibraryModel::ArtistRole).toString(),
             QStringLiteral("Edited artist"));
    QCOMPARE(library.data(library.index(0, 0), LibraryModel::AlbumRole).toString(),
             QStringLiteral("Edited album"));
    QCOMPARE(library.data(library.index(0, 0),
                          LibraryModel::AlbumArtistRole).toString(),
             QStringLiteral("Album artist"));
    QCOMPARE(library.data(library.index(0, 0), LibraryModel::GenreRole).toString(),
             QStringLiteral("Edited genre"));
    QCOMPARE(library.data(library.index(0, 0), LibraryModel::YearRole).toString(),
             originalYearDate);
    QCOMPARE(library.data(library.index(0, 0), LibraryModel::DateRole).toString(),
             originalYearDate);
    QCOMPARE(library.data(library.index(0, 0),
                          LibraryModel::ComposerRole).toString(),
             QStringLiteral("Composer"));
    QCOMPARE(library.data(library.index(0, 0), LibraryModel::BpmRole).toDouble(),
             128.5);
    const QUrl refreshedCover = library.data(
        library.index(0, 0), LibraryModel::CoverUrlRole).toUrl();
    QVERIFY(refreshedCover.isValid());
    QVERIFY(!refreshedCover.isEmpty());
    QVERIFY(QFileInfo::exists(refreshedCover.toLocalFile()));
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
      QCOMPARE(completedResult.value(QStringLiteral("fields")).toList().size(), 8);
      QVERIFY(completedResult.value(QStringLiteral("cover")).toMap()
                  .contains(QStringLiteral("status")));

    ag_metadata* metadata = nullptr;
    QCOMPARE(ag_metadata_open(input.toUtf8().constData(), &metadata), AG_OK);
    QCOMPARE(QString::fromUtf8(ag_metadata_title(metadata)),
             QStringLiteral("Edited title"));
    QCOMPARE(QString::fromUtf8(ag_metadata_artist(metadata)),
             QStringLiteral("Edited artist"));
    QCOMPARE(QString::fromUtf8(ag_metadata_album(metadata)),
             QStringLiteral("Edited album"));
    QCOMPARE(QString::fromUtf8(ag_metadata_album_artist(metadata)),
             QStringLiteral("Album artist"));
    QCOMPARE(QString::fromUtf8(ag_metadata_genre(metadata)),
             QStringLiteral("Edited genre"));
    QCOMPARE(QString::fromUtf8(ag_metadata_composer(metadata)),
             QStringLiteral("Composer"));
    QCOMPARE(QString::fromUtf8(ag_metadata_bpm_tag(metadata)),
             QStringLiteral("128.50"));
    QCOMPARE(QString::fromUtf8(ag_metadata_custom_tag(metadata)),
             QStringLiteral("电子"));
    QCOMPARE(QString::fromUtf8(ag_metadata_year(metadata)),
             originalYearDate);
    QCOMPARE(QString::fromUtf8(ag_metadata_date(metadata)),
             originalYearDate);
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

void AudioToolsEndToEndTest::
    formatConverterRejectsUnsupportedMetadataBeforeEncoding()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString input = temp.filePath(QStringLiteral("metadata-preflight.wav"));
    const QString outputDir = temp.filePath(QStringLiteral("converted"));
    QVERIFY(agplayer::test::writeClickTrackWav(input, 120, 1));
    QVERIFY(QDir().mkpath(outputDir));

    FormatConverter converter;
    const QVariantMap fields{{QStringLiteral("title"),
                              QVariantMap{{QStringLiteral("mode"),
                                           QStringLiteral("set")},
                                          {QStringLiteral("value"),
                                           QStringLiteral("Unsupported")}}}};
    QVERIFY(converter.setMetadataEditPlan(fields, {}));
    converter.loadFiles({QUrl::fromLocalFile(input)});
    waitForConverterLoad(converter);

    QSignalSpy errors(&converter, &FormatConverter::errorOccurred);
    QSignalSpy completed(&converter, &FormatConverter::transcodeCompleted);
    converter.start(QStringLiteral("aac"), 128000, 44100, 2, outputDir,
                    false, false, false);

    QCOMPARE(converter.busy(), false);
    QCOMPARE(errors.count(), 1);
    QCOMPARE(completed.count(), 0);
    QVERIFY(!QFileInfo::exists(
        QDir(outputDir).filePath(QStringLiteral("metadata-preflight.aac"))));
}

void AudioToolsEndToEndTest::
    metadataEditorAppendsDeduplicatesAndAggregatesScopeValues()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString first = temp.filePath(QStringLiteral("first.wav"));
    const QString second = temp.filePath(QStringLiteral("second.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(first, 120, 1));
    QVERIFY(agplayer::test::writeClickTrackWav(second, 128, 1));

    MetadataEditor editor;
    QSignalSpy loaded(&editor, &MetadataEditor::entriesLoaded);
    editor.loadFiles({QUrl::fromLocalFile(first)});
    QVERIFY(loaded.wait(30'000));
    QCOMPARE(editor.fileCount(), 1);

    loaded.clear();
    QVERIFY(QDir().mkpath(temp.filePath(QStringLiteral("alias"))));
    const QString firstAlias = QDir(temp.path()).filePath(
        QStringLiteral("alias/../first.mp3"));
    editor.loadFiles({QUrl::fromLocalFile(firstAlias),
                      QUrl::fromLocalFile(second)});
    QVERIFY(loaded.wait(30'000));
    QCOMPARE(editor.fileCount(), 2);

    MetadataEntry firstEntry;
    firstEntry.title = QStringLiteral("First title");
    firstEntry.hasCover = true;
    firstEntry.coverFingerprint = QStringLiteral("first-cover");
    MetadataEntry secondEntry;
    secondEntry.title = QStringLiteral("Second title");
    secondEntry.hasCover = true;
    secondEntry.coverFingerprint = QStringLiteral("second-cover");
    const QList<MetadataEntry> entries{firstEntry, secondEntry};
    const QVariantMap aggregate = aggregate_metadata_entries(entries, {0, 1});
    const QVariantMap title = aggregate.value(QStringLiteral("title")).toMap();
    QCOMPARE(title.value(QStringLiteral("multiple")).toBool(), true);
    QCOMPARE(title.value(QStringLiteral("value")).toString(), QString());
    QCOMPARE(aggregate.value(QStringLiteral("cover")).toMap()
                 .value(QStringLiteral("state")).toString(),
             QStringLiteral("multiple"));
    const QVariantMap current = aggregate_metadata_entries(entries, {0});
    QCOMPARE(current.value(QStringLiteral("title")).toMap()
                 .value(QStringLiteral("value")).toString(),
             QStringLiteral("First title"));

    const QString emptyFolder = temp.filePath(QStringLiteral("empty"));
    QVERIFY(QDir().mkpath(emptyFolder));
    loaded.clear();
    editor.loadFiles({QUrl::fromLocalFile(emptyFolder)});
    QVERIFY(loaded.wait(30'000));
    QCOMPARE(editor.fileCount(), 2);
}

void AudioToolsEndToEndTest::metadataEditorDetectsReplacementCoverFromContent()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString disguisedBmp = temp.filePath(QStringLiteral("cover.png"));
    QImage image(17, 11, QImage::Format_RGB32);
    image.fill(Qt::blue);
    QVERIFY(image.save(disguisedBmp, "BMP"));

    MetadataEditor editor;
    editor.setCoverImage(QUrl::fromLocalFile(disguisedBmp));
    const QVariantMap details = editor.replacementCoverDetails();
    QCOMPARE(details.value(QStringLiteral("fileName")).toString(),
             QStringLiteral("cover.png"));
    QCOMPARE(details.value(QStringLiteral("width")).toInt(), 17);
    QCOMPARE(details.value(QStringLiteral("height")).toInt(), 11);
    QCOMPARE(details.value(QStringLiteral("mimeType")).toString(),
             QStringLiteral("image/bmp"));
    QVERIFY(details.value(QStringLiteral("sizeBytes")).toLongLong() > 0);

    const QString invalid = temp.filePath(QStringLiteral("invalid.jpg"));
    QFile invalidFile(invalid);
    QVERIFY(invalidFile.open(QIODevice::WriteOnly));
    QCOMPARE(invalidFile.write("not an image"), qint64{12});
    invalidFile.close();
    QSignalSpy errors(&editor, &MetadataEditor::errorOccurred);
    editor.setCoverImage(QUrl::fromLocalFile(invalid));
    QCOMPARE(errors.count(), 1);
    QCOMPARE(editor.replacementCoverDetails(), details);
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

void AudioToolsEndToEndTest::
    metadataEditorPreflightSeparatesUnsupportedAndInternalFailures()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString valid = temp.filePath(QStringLiteral("valid.wav"));
    const QString unsupported = temp.filePath(QStringLiteral("broken.mp3"));
    QVERIFY(agplayer::test::writeClickTrackWav(valid, 120, 2));
    QFile broken(unsupported);
    QVERIFY(broken.open(QIODevice::WriteOnly));
    QCOMPARE(broken.write("not audio"), qint64(9));
    broken.close();

    MetadataEditor editor;
    QSignalSpy loaded(&editor, &MetadataEditor::entriesLoaded);
    editor.loadFiles({QUrl::fromLocalFile(valid),
                      QUrl::fromLocalFile(unsupported)});
    QVERIFY(loaded.wait(30'000));
    const QVariantMap fields{{QStringLiteral("title"),
                              QVariantMap{{QStringLiteral("mode"),
                                           QStringLiteral("set")},
                                          {QStringLiteral("value"),
                                           QStringLiteral("Only supported")}}}};
    QSignalSpy preflight(&editor, &MetadataEditor::preflightCompleted);
    QSignalSpy decision(&editor, &MetadataEditor::preflightDecisionRequired);
    editor.applyMetadata(fields, {0, 1, 99});
    QVERIFY(preflight.wait(30'000));
    QCOMPARE(editor.supportedCount(), 1);
    QCOMPARE(editor.unsupportedCount(), 1);
    QCOMPARE(editor.failedCount(), 1);
    QCOMPARE(editor.results().size(), 3);
    QCOMPARE(decision.count(), 1);
    QVERIFY(editor.requiresPreflightDecision());

    int supportedRows = 0;
    int unsupportedRows = 0;
    int failedRows = 0;
    for (const QVariant& value : editor.results()) {
        const QString status = value.toMap()
                                   .value(QStringLiteral("status")).toString();
        supportedRows += status == QLatin1String("supported");
        unsupportedRows += status == QLatin1String("unsupported");
        failedRows += status == QLatin1String("failed");
    }
    QCOMPARE(supportedRows, 1);
    QCOMPARE(unsupportedRows, 1);
    QCOMPARE(failedRows, 1);

    QSignalSpy applied(&editor, &MetadataEditor::metadataApplied);
    editor.applyPreflightDecision(QStringLiteral("supportedOnly"));
    QVERIFY(applied.wait(30'000));
    QCOMPARE(applied.constLast().at(0).toInt(), 1);
    QCOMPARE(applied.constLast().at(1).toInt(), 1);
    QCOMPARE(editor.successCount(), 1);
    QCOMPARE(editor.unsupportedCount(), 1);
    QCOMPARE(editor.failedCount(), 1);
    QCOMPARE(editor.results().size(), 3);
    QCOMPARE(editor.entryAt(0).value(QStringLiteral("title")).toString(),
             QStringLiteral("Only supported"));

    MetadataEditor internalOnly;
    QSignalSpy internalLoaded(&internalOnly, &MetadataEditor::entriesLoaded);
    internalOnly.loadFiles({QUrl::fromLocalFile(valid)});
    QVERIFY(internalLoaded.wait(30'000));
    QSignalSpy internalPreflight(&internalOnly,
                                 &MetadataEditor::preflightCompleted);
    QSignalSpy internalApplied(&internalOnly, &MetadataEditor::metadataApplied);
    const QVariantMap internalFields{{QStringLiteral("title"),
                                      QVariantMap{{QStringLiteral("mode"),
                                                   QStringLiteral("set")},
                                                  {QStringLiteral("value"),
                                                   QStringLiteral("Must not expand")}}}};
    internalOnly.applyMetadata(internalFields, {99});
    QVERIFY(internalPreflight.wait(30'000));
    QCOMPARE(internalOnly.supportedCount(), 0);
    QCOMPARE(internalOnly.unsupportedCount(), 0);
    QCOMPARE(internalOnly.failedCount(), 1);
    QVERIFY(!internalOnly.requiresPreflightDecision());
    QTest::qWait(250);
    QCOMPARE(internalApplied.count(), 0);
    QCOMPARE(internalOnly.entryAt(0).value(QStringLiteral("title")).toString(),
             QStringLiteral("Only supported"));
}

void AudioToolsEndToEndTest::metadataEditorRejectsTargetChangedAfterPreflight()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString valid = temp.filePath(QStringLiteral("stable.wav"));
    const QString invalid = temp.filePath(QStringLiteral("broken.mp3"));
    QVERIFY(agplayer::test::writeClickTrackWav(valid, 120, 2));
    QFile broken(invalid);
    QVERIFY(broken.open(QIODevice::WriteOnly));
    QCOMPARE(broken.write("not audio"), qint64(9));
    broken.close();

    MetadataEditor editor;
    QSignalSpy loaded(&editor, &MetadataEditor::entriesLoaded);
    editor.loadFiles({QUrl::fromLocalFile(valid), QUrl::fromLocalFile(invalid)});
    QVERIFY(loaded.wait(30'000));
    const QVariantMap fields{{QStringLiteral("title"),
                              QVariantMap{{QStringLiteral("mode"), QStringLiteral("set")},
                                          {QStringLiteral("value"), QStringLiteral("Must not write")}}}};
    QSignalSpy preflight(&editor, &MetadataEditor::preflightCompleted);
    QSignalSpy decision(&editor, &MetadataEditor::preflightDecisionRequired);
    editor.applyMetadata(fields, {});
    QVERIFY(preflight.wait(30'000));
    QVERIFY(decision.count() == 1);

    // The bytes and modification time no longer match the target frozen at
    // preflight.  Confirming must fail safely instead of addressing by a stale
    // list index.
    QVERIFY(agplayer::test::writeClickTrackWav(valid, 127, 1));
    QSignalSpy applied(&editor, &MetadataEditor::metadataApplied);
    editor.applyPreflightDecision(QStringLiteral("supportedOnly"));
    QVERIFY(applied.wait(30'000));
    QCOMPARE(editor.successCount(), 0);
    QCOMPARE(editor.failedCount(), 1);
    QCOMPARE(editor.entryAt(0).value(QStringLiteral("title")).toString(), QString());
    QCOMPARE(editor.results().size(), 2);
    QCOMPARE(editor.unsupportedCount(), 1);
    QCOMPARE(editor.results().constFirst().toMap()
                 .value(QStringLiteral("status")).toString(),
             QStringLiteral("unsupported"));
    const QVariantMap changedResult = editor.results().constLast().toMap();
    QCOMPARE(changedResult.value(QStringLiteral("status")).toString(),
             QStringLiteral("failed"));
    QCOMPARE(changedResult.value(QStringLiteral("errorCode")).toInt(),
             static_cast<int>(agplayer::MetadataErrorCode::SourceChanged));
    QVERIFY(!changedResult.value(QStringLiteral("message")).toString().isEmpty());
}

void AudioToolsEndToEndTest::formatConverterExportsEveryAdvertisedBitrateMode()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString input = temp.filePath(QStringLiteral("advertised-modes.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(input, 120, 2));

    const QVariantList formats = FormatConverter().supportedOutputFormats();
    for (const QVariant& value : formats) {
        const QVariantMap capability = value.toMap();
        if (!capability.value(QStringLiteral("available")).toBool()) continue;
        const QString format = capability.value(QStringLiteral("key")).toString();
        const QVariantList modes = capability.value(QStringLiteral("bitrateModes"))
                                       .toList();
        for (const QVariant& modeValue : modes) {
            const QString mode = modeValue.toMap()
                                     .value(QStringLiteral("key")).toString();
            const QString outputDir = temp.filePath(format + QLatin1Char('-') + mode);
            QVERIFY(QDir().mkpath(outputDir));
            FormatConverter converter;
            converter.setBitrateMode(mode);
            converter.loadFiles({QUrl::fromLocalFile(input)});
            waitForConverterLoad(converter);
            QSignalSpy completed(&converter, &FormatConverter::transcodeCompleted);
            converter.start(format, 192000,
                            format == QStringLiteral("opus") ? 48000 : 44100,
                            2, outputDir, false, false, false);
            if (completed.isEmpty()) {
                QVERIFY2(completed.wait(30'000),
                         qPrintable(format + QLatin1Char('/') + mode));
            }
            const QVariantMap row = converter.files().first().toMap();
            QVERIFY2(converter.failedCount() == 0,
                     qPrintable(QStringLiteral("%1/%2: %3")
                                    .arg(format, mode,
                                         row.value(QStringLiteral("errorDetail"))
                                             .toString())));
            verifyAudioFile(row.value(QStringLiteral("outputPath")).toString(),
                            format == QStringLiteral("opus") ? 48000 : 44100,
                            format + QLatin1Char('/') + mode);
        }
    }
}

void AudioToolsEndToEndTest::
    metadataEditorDoesNotApplyWhenEveryTargetIsUnsupported()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString invalid = temp.filePath(QStringLiteral("broken.mp3"));
    QFile broken(invalid);
    QVERIFY(broken.open(QIODevice::WriteOnly));
    QCOMPARE(broken.write("not audio"), qint64(9));
    broken.close();

    MetadataEditor editor;
    QSignalSpy loaded(&editor, &MetadataEditor::entriesLoaded);
    editor.loadFiles({QUrl::fromLocalFile(invalid)});
    QVERIFY(loaded.wait(30'000));
    QCOMPARE(editor.fileCount(), 1);

    const QVariantMap fields{{QStringLiteral("title"),
                              QVariantMap{{QStringLiteral("mode"),
                                           QStringLiteral("set")},
                                          {QStringLiteral("value"),
                                           QStringLiteral("Must not write")}}}};
    QSignalSpy preflight(&editor, &MetadataEditor::preflightCompleted);
    QSignalSpy applied(&editor, &MetadataEditor::metadataApplied);
    editor.applyMetadata(fields, {0});
    QVERIFY(preflight.wait(30'000));
    QCOMPARE(editor.supportedCount(), 0);
    QCOMPARE(editor.unsupportedCount(), 1);
    QVERIFY(editor.requiresPreflightDecision());
    QCOMPARE(editor.results().size(), 1);

    editor.applyPreflightDecision(QStringLiteral("skipUnsupported"));
    QTest::qWait(500);
    QCOMPARE(applied.count(), 0);
    QCOMPARE(editor.results().size(), 1);
    QCOMPARE(editor.results().first().toMap()
                 .value(QStringLiteral("status")).toString(),
             QStringLiteral("unsupported"));
    QCOMPARE(editor.entryAt(0).value(QStringLiteral("title")).toString(),
             QString());
}

void AudioToolsEndToEndTest::metadataEditorAppliesUiPayloadToMixedContainerBatch()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString source = temp.filePath(QStringLiteral("batch-source.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(source, 120, 2));
    const auto readTitle = [](const QString& path) {
        ag_metadata* metadata = nullptr;
        if (ag_metadata_open(path.toUtf8().constData(), &metadata) != AG_OK
            || metadata == nullptr) {
            return QString{};
        }
        const QString title = QString::fromUtf8(ag_metadata_title(metadata));
        ag_metadata_destroy(metadata);
        return title;
    };

    const QList<QPair<QString, QString>> targets{
        {QStringLiteral("batch.mp3"), QStringLiteral("libmp3lame")},
        {QStringLiteral("batch.flac"), QStringLiteral("flac")},
        {QStringLiteral("batch.m4a"), QStringLiteral("aac")},
        {QStringLiteral("batch.aac"), QStringLiteral("aac")},
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
        const QString path = editor.entryAt(index).value(
            QStringLiteral("path")).toString();
        QCOMPARE(readTitle(path), QStringLiteral("Mixed batch"));
        QVERIFY(!QFileInfo::exists(path + QStringLiteral(".agbak")));
        QCOMPARE(editor.results().at(index).toMap()
                     .value(QStringLiteral("status")).toString(),
                 QStringLiteral("completed"));
    }

    const QVariantMap replacement{
        {QStringLiteral("title"),
         QVariantMap{{QStringLiteral("mode"), QStringLiteral("set")},
                     {QStringLiteral("value"), QStringLiteral("Replacement batch")}}}};
    applied.clear();
    editor.applyMetadata(replacement, {});
    QVERIFY(applied.wait(30'000));
    QCOMPARE(editor.successCount(), targets.size());
    for (int index = 0; index < editor.fileCount(); ++index) {
        const QString path = editor.entryAt(index).value(
            QStringLiteral("path")).toString();
        QCOMPARE(readTitle(path), QStringLiteral("Replacement batch"));
        QVERIFY(!QFileInfo::exists(path + QStringLiteral(".agbak")));
    }

    const QVariantMap clearTitle{
        {QStringLiteral("title"),
         QVariantMap{{QStringLiteral("mode"), QStringLiteral("clear")}}}};
    applied.clear();
    editor.applyMetadata(clearTitle, {});
    QVERIFY(applied.wait(30'000));
    QCOMPARE(editor.successCount(), targets.size());
    for (int index = 0; index < editor.fileCount(); ++index) {
        const QString path = editor.entryAt(index).value(
            QStringLiteral("path")).toString();
        QCOMPARE(readTitle(path), QString());
        QVERIFY(!QFileInfo::exists(path + QStringLiteral(".agbak")));
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

    QSignalSpy undone(&processor, &FilenameProcessor::undoCompleted);
    processor.undoLast();
    QVERIFY(undone.wait(30'000));
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
    QVERIFY(undone.wait(30'000));
    QCOMPARE(undone.count(), 1);
    QCOMPARE(undone.first().at(0).toInt(), 0);
    QCOMPARE(undone.first().at(1).toInt(), 1);
    QVERIFY(processor.canUndo());

    QVERIFY(QFile::remove(input));
    undone.clear();
    processor.undoLast();
    QVERIFY(undone.wait(30'000));
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
    QVERIFY(undone.wait(30'000));
    QCOMPARE(undone.count(), 1);
    QCOMPARE(undone.first().at(0).toInt(), 2);
    QCOMPARE(undone.first().at(1).toInt(), 0);
    QVERIFY(QFileInfo::exists(first));
    QVERIFY(QFileInfo::exists(second));
    QVERIFY(!processor.canUndo());
}

QTEST_MAIN(AudioToolsEndToEndTest)
#include "audio_tools_end_to_end_test.moc"
