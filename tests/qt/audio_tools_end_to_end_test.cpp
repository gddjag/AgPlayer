#include "format_converter.hpp"
#include "metadata_editor.hpp"
#include "pitch_shifter.hpp"
#include "speed_adjuster.hpp"

#include "../core/bpm_fixture.hpp"

#include <agplayer/c_api.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QTest>
#include <QThreadPool>
#include <QVariantMap>

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

} // namespace

class AudioToolsEndToEndTest final : public QObject {
    Q_OBJECT

private slots:
    void formatConverterExportsPreservesMetadataAndAvoidsCollisions();
    void formatConverterReservesParallelOutputNames();
    void formatConverterOverwritePolicyReusesSafeTargets();
    void formatConverterCancellationFinalizesQueuedEntries();
    void speedAdjusterAnalyzesAlignsAndExports();
    void pitchShifterExportsRequestedSampleRate();
    void pitchShifterCancellationReachesNativeWorker();
    void metadataEditorWritesAndRenames();
    void metadataEditorKeepsEntriesStableWhileBusy();
    void metadataEditorRenameCannotEscapeSourceDirectory();
};

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
    QSignalSpy videoCompleted(
        &videoConverter, &FormatConverter::transcodeCompleted);
    videoConverter.start(QStringLiteral("wav"), 0, 44100, 2, outputDir,
                         false, false, false);
    QVERIFY(videoCompleted.wait(30000));
    QCOMPARE(videoCompleted.first().first().toInt(), 0);
    QCOMPARE(videoCompleted.first().last().toInt(), 1);

    videoCompleted.clear();
    videoConverter.start(QStringLiteral("wav"), 0, 44100, 2, outputDir,
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
    QSignalSpy completed(&converter, &FormatConverter::transcodeCompleted);
    converter.start(QStringLiteral("flac"), 0, 44100, 2, outputDir,
                    false, false, false);
    converter.cancel();
    QVERIFY(completed.wait(30000));
    QCOMPARE(converter.completedCount(), converter.fileCount());

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

void AudioToolsEndToEndTest::speedAdjusterAnalyzesAlignsAndExports()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString input = temp.filePath(QStringLiteral("speed.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(input, 120, 4));

    SpeedAdjuster adjuster;
    adjuster.loadFile(QUrl::fromLocalFile(input));
    QVERIFY(adjuster.hasInput());
    QVERIFY(std::abs(adjuster.detectedBpm() - 120.0) < 1.0);
    adjuster.setBeatAlign(true);

    QSignalSpy completed(&adjuster, &SpeedAdjuster::speedAdjustCompleted);
    adjuster.startBpmAdjust(120.0, true, QStringLiteral("wav"), temp.path());
    QVERIFY(completed.wait(30000));
    const QString output = completed.first().first().toString();
    verifyAudioFile(output);
    QVERIFY(output.contains(QStringLiteral("_bpm")));
}

void AudioToolsEndToEndTest::pitchShifterExportsRequestedSampleRate()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString input = temp.filePath(QStringLiteral("pitch.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(input, 120, 2));

    PitchShifter shifter;
    shifter.loadFile(QUrl::fromLocalFile(input));
    QVERIFY(shifter.hasInput());

    QSignalSpy completed(&shifter, &PitchShifter::pitchShiftCompleted);
    shifter.start(200, true, 1.0, QStringLiteral("wav"), 16000,
                  true, true, temp.path());
    QVERIFY(completed.wait(30000));
    verifyAudioFile(completed.first().first().toString(), 16000);
}

void AudioToolsEndToEndTest::pitchShifterCancellationReachesNativeWorker()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString input = temp.filePath(QStringLiteral("cancel.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(input, 120, 30));

    PitchShifter shifter;
    shifter.loadFile(QUrl::fromLocalFile(input));
    QSignalSpy completed(&shifter, &PitchShifter::pitchShiftCompleted);
    QSignalSpy errors(&shifter, &PitchShifter::errorOccurred);
    shifter.start(400, true, 1.0, QStringLiteral("wav"), 44100,
                  true, true, temp.path());
    shifter.cancel();
    QTRY_VERIFY_WITH_TIMEOUT(errors.count() > 0 || completed.count() > 0,
                             30000);
    QCOMPARE(completed.count(), 0);
    QVERIFY(errors.first().first().toString().contains(
        QStringLiteral("cancel"), Qt::CaseInsensitive));
    QVERIFY(!shifter.busy());
}

void AudioToolsEndToEndTest::metadataEditorWritesAndRenames()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString input = temp.filePath(QStringLiteral("meta.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(input, 120, 2));

    MetadataEditor editor;
    QSignalSpy entriesLoaded(&editor, &MetadataEditor::entriesLoaded);
    editor.loadFiles({QUrl::fromLocalFile(input)});
    QVERIFY(entriesLoaded.wait(30000));
    QCOMPARE(editor.fileCount(), 1);

    QVariantMap fields;
    fields.insert(QStringLiteral("title"), QStringLiteral("Edited title"));
    fields.insert(QStringLiteral("artist"), QStringLiteral("Edited artist"));
    QSignalSpy metadataApplied(&editor, &MetadataEditor::metadataApplied);
    editor.applyMetadata(fields, {});
    QVERIFY(metadataApplied.wait(30000));
    QCOMPARE(metadataApplied.first().first().toInt(), 1);

    ag_metadata* metadata = nullptr;
    QCOMPARE(ag_metadata_open(input.toUtf8().constData(), &metadata), AG_OK);
    QCOMPARE(QString::fromUtf8(ag_metadata_title(metadata)),
             QStringLiteral("Edited title"));
    QCOMPARE(QString::fromUtf8(ag_metadata_artist(metadata)),
             QStringLiteral("Edited artist"));
    ag_metadata_destroy(metadata);

    QCOMPARE(editor.renameExample(QStringLiteral("P-"), QStringLiteral("-S"),
                                  true, 7, 3),
             QStringLiteral("P-007-S.wav"));
    QSignalSpy renamed(&editor, &MetadataEditor::renameApplied);
    editor.applyRename(QStringLiteral("P-"), QStringLiteral("-S"),
                       true, 7, 3);
    QVERIFY(renamed.wait(30000));
    QCOMPARE(renamed.first().first().toInt(), 1);
    QVERIFY(QFileInfo::exists(
        temp.filePath(QStringLiteral("P-007-S.wav"))));
}

void AudioToolsEndToEndTest::metadataEditorKeepsEntriesStableWhileBusy()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString input = temp.filePath(QStringLiteral("stable.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(input, 120, 2));

    MetadataEditor editor;
    QSignalSpy entriesLoaded(&editor, &MetadataEditor::entriesLoaded);
    editor.loadFiles({QUrl::fromLocalFile(input)});
    QVERIFY(entriesLoaded.wait(30000));
    QCOMPARE(editor.fileCount(), 1);
    QVERIFY(QFile::copy(input, temp.filePath(QStringLiteral("safe-stable.wav"))));

    QSignalSpy renamed(&editor, &MetadataEditor::renameApplied);
    QSignalSpy entriesChanged(&editor, &MetadataEditor::entriesChanged);
    editor.applyRename(QStringLiteral("safe-"), QString(), false, 1, 1);
    QVERIFY(editor.busy());
    editor.clear();
    QCOMPARE(editor.fileCount(), 1);
    QVERIFY(renamed.wait(30000));
    QCOMPARE(entriesChanged.count(), 1);

    const QVariantMap entry = editor.entryAt(0);
    QCOMPARE(entry.value(QStringLiteral("fileName")).toString(),
             QStringLiteral("safe-stable_2.wav"));
    QVERIFY(QFileInfo::exists(
        temp.filePath(QStringLiteral("safe-stable_2.wav"))));
    editor.clear();
    QCOMPARE(editor.fileCount(), 0);
    QCOMPARE(entriesChanged.count(), 2);
}

void AudioToolsEndToEndTest::metadataEditorRenameCannotEscapeSourceDirectory()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString sourceDir = temp.filePath(QStringLiteral("source"));
    QVERIFY(QDir().mkpath(sourceDir));
    const QString input =
        QDir(sourceDir).filePath(QStringLiteral("traverse.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(input, 120, 2));

    MetadataEditor editor;
    QSignalSpy entriesLoaded(&editor, &MetadataEditor::entriesLoaded);
    editor.loadFiles({QUrl::fromLocalFile(input)});
    QVERIFY(entriesLoaded.wait(30000));

    QSignalSpy renamed(&editor, &MetadataEditor::renameApplied);
    editor.applyRename(QStringLiteral("../outside-"), QString(), false, 1, 1);
    QVERIFY(renamed.wait(30000));
    QCOMPARE(renamed.first().first().toInt(), 1);

    const QFileInfo renamedFile(
        editor.entryAt(0).value(QStringLiteral("path")).toString());
    QCOMPARE(renamedFile.absolutePath(), QFileInfo(input).absolutePath());
    QCOMPARE(renamedFile.fileName(), QStringLiteral(".._outside-traverse.wav"));
}

QTEST_MAIN(AudioToolsEndToEndTest)
#include "audio_tools_end_to_end_test.moc"
