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
#include <QImage>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QTest>
#include <QThreadPool>
#include <QVariantMap>

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
    void audioToolsControllerSupportsFourBuiltInTools();
    void toolsExpandDroppedFoldersRecursively();
    void audioFileDiscoveryExpandsFoldersOffTheGuiThread();
    void formatConverterLoadsDroppedFilesAsynchronously();
    void formatConverterReportsReferenceTaskColumns();
    void formatConverterExposesQueueFacadeModelsAndPreflight();
    void formatConverterPreflightReturnsResolvedProfileBeforeStarting();
    void formatConverterBuildPreflightUsesSmartProfilesAndRejectsInvalidCustomValues();
    void formatConverterAskPolicyRequiresConflictConfirmationBeforeStarting();
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
    void formatConverterWritesMetadataPlanToNewOutput();
    void formatConverterScopesMetadataPlanToOneBatch();
    void formatConverterRejectsUnsupportedMetadataBeforeEncoding();
    void metadataEditorWritesTags();
    void metadataEditorAppendsDeduplicatesAndAggregatesScopeValues();
    void metadataEditorDetectsReplacementCoverFromContent();
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
    converter.setConflictPolicy(QStringLiteral("ask"));
    converter.loadFiles({QUrl::fromLocalFile(input)});
    waitForConverterLoad(converter);

    const QVariantMap preview = converter.previewSelected(
        {0}, QStringLiteral("flac"), 0, 44100, 2, outputDir, false);
    QVERIFY(preview.value(QStringLiteral("ready")).toBool());
    QVERIFY(preview.value(QStringLiteral("requiresConfirmation")).toBool());
    QCOMPARE(preview.value(QStringLiteral("conflictCount")).toInt(), 1);
    QCOMPARE(preview.value(QStringLiteral("conflicts")).toList().size(), 1);
    QCOMPARE(converter.files().first().toMap()
                 .value(QStringLiteral("status")).toString(),
             QStringLiteral("PendingConfirmation"));
    QVERIFY(!converter.busy());
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
    setField(QStringLiteral("album"), QStringLiteral("Edited album"));
    setField(QStringLiteral("albumArtist"), QStringLiteral("Album artist"));
    setField(QStringLiteral("genre"), QStringLiteral("Edited genre"));
    // MP3 stores Year and Date in the same physical tag. Equivalent edits are
    // valid and must be reflected through both logical fields.
    setField(QStringLiteral("year"), QStringLiteral("2026-08-20"));
    setField(QStringLiteral("date"), QStringLiteral("2026-08-20"));
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
    QCOMPARE(editor.entryAt(0).value(QStringLiteral("date")).toString(),
             QStringLiteral("2026-08-20"));
    QCOMPARE(editor.entryAt(0).value(QStringLiteral("year")).toString(),
             QStringLiteral("2026-08-20"));
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
             QStringLiteral("2026-08-20"));
    QCOMPARE(library.data(library.index(0, 0), LibraryModel::DateRole).toString(),
             QStringLiteral("2026-08-20"));
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
      QCOMPARE(completedResult.value(QStringLiteral("fields")).toList().size(), 9);
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
    QCOMPARE(QString::fromUtf8(ag_metadata_year(metadata)),
             QStringLiteral("2026-08-20"));
    QCOMPARE(QString::fromUtf8(ag_metadata_date(metadata)),
             QStringLiteral("2026-08-20"));
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
