#include "audio_editor/audio_editor_controller.hpp"
#include "manual_recording_capture.hpp"
#include "audio_editor/audio_file_analyzer.hpp"
#include "../core/bpm_fixture.hpp"
#include "decoder.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QtTest>

#include <atomic>
#include <cmath>
#include <limits>
#include <thread>
#include <vector>

namespace {

float maximumAbsolutePeak(const agplayer::editor::AudioFileAnalysis& analysis)
{
    float maximum = 0.0F;
    for (const auto& channel : analysis.channel_peaks) {
        for (const float sample : channel) {
            maximum = std::max(maximum, std::abs(sample));
        }
    }
    return maximum;
}

struct DecodedProbe final {
    agplayer::MediaMetadata metadata;
    std::vector<float> firstChannel;
    qint64 frames{};
};

DecodedProbe decodeProbe(const QString& path)
{
    DecodedProbe result;
    agplayer::Decoder decoder;
    if (decoder.open(std::filesystem::path(path.toStdWString()).u8string())
        != AG_OK) {
        return result;
    }
    result.metadata = decoder.metadata();
    agplayer::DecodedAudioBlock block;
    do {
        if (decoder.read(block) != AG_OK) return {};
        for (std::size_t frame = 0; frame < block.frames; ++frame) {
            result.firstChannel.push_back(block.samples[
                frame * static_cast<std::size_t>(result.metadata.channels)]);
        }
        result.frames += static_cast<qint64>(block.frames);
    } while (!block.end_of_stream);
    return result;
}

double positiveCrossingFrequency(const DecodedProbe& probe)
{
    if (probe.metadata.sample_rate <= 0 || probe.firstChannel.size() < 2) {
        return 0.0;
    }
    qint64 crossings = 0;
    for (std::size_t index = 1; index < probe.firstChannel.size(); ++index) {
        if (probe.firstChannel[index - 1] <= 0.0F
            && probe.firstChannel[index] > 0.0F) {
            ++crossings;
        }
    }
    const double seconds = static_cast<double>(probe.frames)
        / probe.metadata.sample_rate;
    return seconds > 0.0 ? crossings / seconds : 0.0;
}

} // namespace

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

    void recordingBackendAcceptsCaptureRequestAndReportsInvalidDeviceAsync()
    {
        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(controller.recordingSupported());
        QVERIFY(controller.actionEnabled(QStringLiteral("editor.newRecording")));

        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString output = temporary.filePath(QStringLiteral("capture.wav"));
        QVERIFY(controller.startRecording(
            QUrl::fromLocalFile(output),
            QStringLiteral("capture:device-does-not-exist"),
            48'000, 2, false, false));
        QCOMPARE(controller.state(), EditorSessionState::RecordingStarting);
        QTRY_COMPARE_WITH_TIMEOUT(controller.state(), EditorSessionState::Empty,
                                  10'000);
        QVERIFY(controller.errorMessage().contains(QStringLiteral("WASAPI")));
        QVERIFY(controller.errorMessage().contains(QStringLiteral("device")));
        QVERIFY(!QFileInfo::exists(output));
    }

    void recordingNeverSilentlyReplacesAnExistingDocument()
    {
        const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_EDITOR_FIXTURE"));
        if (fixture.isEmpty()) QSKIP("fixture not configured");
        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(controller.openFile(QUrl::fromLocalFile(fixture)));
        const auto historyBefore = controller.historyStateIdForTesting();
        QSignalSpy discardRequested(
            &controller, &AudioEditorController::discardConfirmationRequested);

        QVERIFY(!controller.startRecordingToTemporaryFile(
            QStringLiteral("capture:device-does-not-exist"), 48'000, 2,
            false, false));
        QCOMPARE(discardRequested.count(), 1);
        QCOMPARE(controller.state(), EditorSessionState::Ready);
        QCOMPARE(controller.historyStateIdForTesting(), historyBefore);

        QVERIFY(controller.startRecordingToTemporaryFile(
            QStringLiteral("capture:device-does-not-exist"), 48'000, 2,
            false, true));
        QCOMPARE(discardRequested.count(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(controller.state(), EditorSessionState::Ready,
                                  10'000);
        QCOMPARE(controller.historyStateIdForTesting(), historyBefore);
    }

    void bpmDetectionAnalyzesTheCurrentTimeline()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString source = temporary.filePath(QStringLiteral("click-120.wav"));
        QVERIFY(agplayer::test::writeClickTrackWav(source, 120, 8));

        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(controller.bpmDetectionSupported());
        QVERIFY(controller.openFile(QUrl::fromLocalFile(source)));
        QVERIFY(controller.detectBpm());
        QCOMPARE(controller.state(), EditorSessionState::Processing);
        QTRY_COMPARE_WITH_TIMEOUT(controller.state(), EditorSessionState::Ready,
                                  15'000);
        QVERIFY2(std::abs(controller.originalBpm() - 120.0) < 1.0,
                 qPrintable(QString::number(controller.originalBpm())));
    }

    void timePitchAndFormantControlsDriveProcessingParameters()
    {
        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(controller.timePitchSupported());
        QVERIFY(controller.formantPreservationSupported());

        controller.setOriginalBpm(128.0);
        QVERIFY(controller.setTargetBpm(160.0));
        QCOMPARE(controller.speedPercent(), 125.0);
        QVERIFY(controller.setSpeedPercent(75.0));
        QCOMPARE(controller.targetBpm(), 96.0);
        controller.setKeepPitch(false);
        QVERIFY(!controller.keepPitch());
        QVERIFY(controller.setPitch(2, 50));
        QCOMPARE(controller.pitchCents(), 250);
        controller.setFormantPreservation(true);
        QVERIFY(controller.formantPreservation());
    }

    void viewportWidthBeforeFirstOpenDoesNotCreateDiscardPrompt()
    {
        const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_EDITOR_FIXTURE"));
        if (fixture.isEmpty()) QSKIP("fixture not configured");
        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QSignalSpy discardRequested(
            &controller, &AudioEditorController::discardConfirmationRequested);

        controller.viewport()->setViewportWidth(1'167.0);

        QVERIFY(!controller.modified());
        QVERIFY(controller.openFile(QUrl::fromLocalFile(fixture)));
        QCOMPARE(discardRequested.count(), 0);
        QVERIFY(controller.hasDocument());
        QVERIFY(controller.actionEnabled(QStringLiteral("editor.export")));
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

    void qmlEventViewsKeepLargeIdsAsStringsAndGesturesCoalesce()
    {
        const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_EDITOR_FIXTURE"));
        if (fixture.isEmpty()) QSKIP("fixture not configured");
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString source = temporary.filePath(QStringLiteral("source.wav"));
        const QString project = temporary.filePath(QStringLiteral("large-id.agproj"));
        QVERIFY(QFile::copy(fixture, source));

        AudioEditorController saved(AG_AUDIO_BACKEND_NULL);
        QVERIFY(saved.openFile(QUrl::fromLocalFile(source)));
        QVERIFY(saved.saveProjectAs(QUrl::fromLocalFile(project)));
        QFile projectFile(project);
        QVERIFY(projectFile.open(QIODevice::ReadOnly));
        QJsonObject root = QJsonDocument::fromJson(projectFile.readAll()).object();
        projectFile.close();
        QJsonArray events = root.value(QStringLiteral("events")).toArray();
        QCOMPARE(events.size(), 1);
        QJsonObject event = events.first().toObject();
        const QString largeId = QStringLiteral("9007199254740993");
        event.insert(QStringLiteral("id"), largeId);
        events.replace(0, event);
        root.insert(QStringLiteral("events"), events);
        QVERIFY(projectFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QVERIFY(projectFile.write(QJsonDocument(root).toJson()) > 0);
        projectFile.close();

        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY2(controller.openProject(QUrl::fromLocalFile(project)),
                 qPrintable(controller.errorMessage()));
        QCOMPARE(controller.formantPreservationSupported(), true);
        QVariantList views = controller.timelineEventViews();
        QCOMPARE(views.size(), 1);
        QCOMPARE(views.first().toMap().value(QStringLiteral("id")).toString(), largeId);

        QVERIFY(controller.beginEventGesture(largeId, QStringLiteral("move"), false));
        QVERIFY(controller.moveEvent(largeId, 100));
        QVERIFY(controller.moveEvent(largeId, 200));
        QCOMPARE(controller.timelineEventViews().first().toMap()
                     .value(QStringLiteral("timelineStart")).toLongLong(), qint64{200});
        QVERIFY(controller.endEventGesture());
        QCOMPARE(controller.timelineEventViews().first().toMap()
                     .value(QStringLiteral("timelineStart")).toLongLong(), qint64{200});
        QVERIFY(controller.undo());
        QCOMPARE(controller.timelineEventViews().first().toMap()
                     .value(QStringLiteral("timelineStart")).toLongLong(), qint64{0});
        QVERIFY(!controller.undo());

        QVERIFY(controller.beginEventGesture(largeId, QStringLiteral("move"), true));
        QVERIFY(controller.moveEvent(largeId, controller.totalFrames() + 1'000));
        QVERIFY(controller.endEventGesture());
        QCOMPARE(controller.timelineEventViews().size(), 2);
        QVERIFY(controller.undo());
        QCOMPARE(controller.timelineEventViews().size(), 1);
    }

    void rejectedEventGesturesPublishRollbackWithoutChangingHistory()
    {
        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(controller.createUntitledDocument(48'000, 2, 1'000));
        QVERIFY(controller.splitEvent(1, 500));

        const quint64 revision = controller.timelineRevisionForTesting();
        const std::uint64_t history = controller.historyStateIdForTesting();
        const bool undoEnabled = controller.actionEnabled(QStringLiteral("editor.undo"));
        QSignalSpy documentChanges(&controller,
                                   &AudioEditorController::documentChanged);

        QVERIFY(controller.beginEventGesture(QStringLiteral("2"),
                                             QStringLiteral("move"), false));
        QVERIFY(controller.moveEvent(QStringLiteral("2"), 100));
        QCOMPARE(controller.timelineEventViews().at(1).toMap()
                     .value(QStringLiteral("timelineStart")).toLongLong(),
                 qint64{100});
        documentChanges.clear();
        QVERIFY(!controller.endEventGesture());
        QCOMPARE(documentChanges.count(), 1);
        QCOMPARE(controller.timelineEventViews().at(1).toMap()
                     .value(QStringLiteral("timelineStart")).toLongLong(),
                 qint64{500});
        QCOMPARE(controller.timelineRevisionForTesting(), revision);
        QCOMPARE(controller.historyStateIdForTesting(), history);
        QCOMPARE(controller.actionEnabled(QStringLiteral("editor.undo")), undoEnabled);

        QVERIFY(controller.beginEventGesture(QStringLiteral("1"),
                                             QStringLiteral("trim"), false));
        QVERIFY(controller.trimEvent(QStringLiteral("1"), 0, 2'000, 0));
        QCOMPARE(controller.timelineEventViews().first().toMap()
                     .value(QStringLiteral("sourceEnd")).toLongLong(),
                 qint64{2'000});
        documentChanges.clear();
        QVERIFY(!controller.endEventGesture());
        QCOMPARE(documentChanges.count(), 1);
        QCOMPARE(controller.timelineEventViews().first().toMap()
                     .value(QStringLiteral("sourceEnd")).toLongLong(),
                 qint64{500});
        QCOMPARE(controller.timelineRevisionForTesting(), revision);
        QCOMPARE(controller.historyStateIdForTesting(), history);
        QCOMPARE(controller.actionEnabled(QStringLiteral("editor.undo")), undoEnabled);
    }

    void fadeOutGestureAndEnvelopeUseObservableSingleStepEdits()
    {
        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(controller.createUntitledDocument(48'000, 2, 1'000));
        const QString id = controller.timelineEventViews().front().toMap()
            .value(QStringLiteral("id")).toString();
        const auto historyBefore = controller.historyStateIdForTesting();

        QVERIFY(controller.beginEventGesture(
            id, QStringLiteral("fadeOut"), false));
        QVERIFY(controller.setEventFadeOut(id, 100));
        QVERIFY(controller.setEventFadeOut(id, 240));
        QCOMPARE(controller.historyStateIdForTesting(), historyBefore);
        QCOMPARE(controller.timelineEventViews().front().toMap()
                     .value(QStringLiteral("fadeOut")).toLongLong(), qint64{240});
        QVERIFY(controller.endEventGesture());
        QCOMPARE(controller.historyStateIdForTesting(), historyBefore + 1);

        QVERIFY(controller.addEnvelopePoint(id, 300, 0.4));
        const QVariantList envelope = controller.timelineEventViews().front().toMap()
            .value(QStringLiteral("envelope")).toList();
        QCOMPARE(envelope.size(), 1);
        QCOMPARE(envelope.front().toMap().value(QStringLiteral("offset")).toLongLong(),
                 qint64{300});
        QVERIFY(std::abs(envelope.front().toMap()
            .value(QStringLiteral("gain")).toDouble() - 0.4) < 0.000001);
        QVERIFY(controller.undo());
        QVERIFY(controller.timelineEventViews().front().toMap()
                    .value(QStringLiteral("envelope")).toList().isEmpty());
        QVERIFY(controller.undo());
        QCOMPARE(controller.timelineEventViews().front().toMap()
                     .value(QStringLiteral("fadeOut")).toLongLong(), qint64{0});
    }

    void activeRecordingPublishesLiveTimelineAndFinalPlayableDocument()
    {
        auto capture = std::make_unique<ManualRecordingCapture>();
        ManualRecordingCapture* const driver = capture.get();
        AudioEditorController controller(
            AG_AUDIO_BACKEND_NULL, std::move(capture));
        controller.viewport()->setViewportWidth(320.0);
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString output = temporary.filePath(QStringLiteral("active.wav"));
        QVERIFY(controller.startRecording(
            QUrl::fromLocalFile(output), {}, 16'000, 2, false, false));
        QTRY_COMPARE_WITH_TIMEOUT(controller.state(),
                                  EditorSessionState::Recording, 5'000);

        std::vector<float> active(16'000U * 2U);
        for (std::size_t frame = 0; frame < 16'000U; ++frame) {
            active[frame * 2U] = frame % 2U == 0U ? -0.8F : 0.3F;
            active[frame * 2U + 1U] = frame % 2U == 0U ? -0.2F : 0.6F;
        }
        QCOMPARE(driver->feed(active, 16'000), 16'000U);
        QTRY_COMPARE_WITH_TIMEOUT(controller.recordingFrames(), 16'000, 1'000);
        QTRY_COMPARE_WITH_TIMEOUT(controller.playheadFrame(), 16'000, 1'000);
        QTRY_COMPARE_WITH_TIMEOUT(controller.positionMs(), 1'000, 1'000);
        QTRY_VERIFY_WITH_TIMEOUT(controller.inputLevel() > 0.79, 1'000);
        QTRY_VERIFY_WITH_TIMEOUT(
            controller.viewportChannelPeaks().size() == 2, 2'000);

        std::vector<float> quiet(16'000U * 2U, 0.0F);
        QCOMPARE(driver->feed(quiet, 16'000), 16'000U);
        QTRY_COMPARE_WITH_TIMEOUT(controller.playheadFrame(), 32'000, 1'000);
        QTRY_VERIFY_WITH_TIMEOUT(controller.inputLevel() < 0.01, 1'000);
        QTRY_VERIFY_WITH_TIMEOUT([&controller] {
            const QVariantList channels = controller.viewportChannelPeaks();
            if (channels.size() != 2) return false;
            const QVariantList left = channels.front().toList();
            bool sawSignal = false;
            bool sawQuiet = false;
            for (qsizetype index = 0; index + 1 < left.size(); index += 2) {
                const double minimum = left[index].toDouble();
                const double maximum = left[index + 1].toDouble();
                sawSignal = sawSignal || minimum < -0.79 || maximum > 0.29;
                sawQuiet = sawQuiet || (std::abs(minimum) < 0.001
                                        && std::abs(maximum) < 0.001);
            }
            return sawSignal && sawQuiet;
        }(), 2'000);

        QVERIFY(controller.stopRecording());
        QCOMPARE(controller.state(), EditorSessionState::Finalizing);
        QVERIFY(controller.recording());
        QVERIFY(!controller.cancelRecording());
        QCOMPARE(controller.totalFrames(), 32'000);
        QVERIFY(!controller.viewportChannelPeaks().isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(controller.state(),
                                  EditorSessionState::Ready, 5'000);
        QVERIFY(controller.hasDocument());
        QCOMPARE(controller.totalFrames(), 32'000);
        QCOMPARE(controller.sampleRate(), 16'000);
        QCOMPARE(controller.channels(), 2);
        QCOMPARE(controller.playheadFrame(), 0);
        QCOMPARE(controller.positionMs(), 0);
        QCOMPARE(controller.inputLevel(), 0.0);
        QVERIFY(!controller.viewportChannelPeaks().isEmpty());
        QVERIFY(QFileInfo::exists(output));
        QVERIFY(controller.playPause());
        QTRY_VERIFY_WITH_TIMEOUT(controller.playing(), 5'000);
        QVERIFY(controller.stopPlayback());
    }

    void startupCallbackBlockBeforeStartReturnsIsRetainedInFinalDocument()
    {
        auto capture = std::make_unique<ManualRecordingCapture>();
        ManualRecordingCapture* const driver = capture.get();
        constexpr std::size_t startupFrames = 32;
        std::vector<float> startupSamples(startupFrames * 2U);
        for (std::size_t frame = 0; frame < startupFrames; ++frame) {
            startupSamples[frame * 2U] = static_cast<float>(frame) / 40.0F;
            startupSamples[frame * 2U + 1U] =
                -static_cast<float>(frame) / 40.0F;
        }
        driver->setStartupSamples(startupSamples);
        AudioEditorController controller(
            AG_AUDIO_BACKEND_NULL, std::move(capture));
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString output = temporary.filePath(
            QStringLiteral("startup-block.wav"));

        QVERIFY(controller.startRecording(
            QUrl::fromLocalFile(output), {}, 16'000, 2, false, false));
        QTRY_COMPARE_WITH_TIMEOUT(controller.state(),
                                  EditorSessionState::Recording, 5'000);
        QCOMPARE(controller.recordingFrames(),
                 static_cast<qint64>(startupFrames));
        const auto pcm = driver->takePcmSnapshot(
            0, static_cast<agplayer::editor::SampleFrame>(startupFrames),
            startupFrames);
        QCOMPARE(pcm.frames,
                 static_cast<agplayer::editor::SampleFrame>(startupFrames));
        QCOMPARE(pcm.interleaved_samples.size(), startupSamples.size());
        for (std::size_t index = 0; index < startupSamples.size(); ++index) {
            QVERIFY(std::abs(pcm.interleaved_samples[index]
                             - startupSamples[index]) < 0.000001F);
        }

        QVERIFY(controller.stopRecording());
        QTRY_COMPARE_WITH_TIMEOUT(controller.state(),
                                  EditorSessionState::Ready, 5'000);
        QVERIFY(controller.hasDocument());
        QCOMPARE(controller.totalFrames(),
                 static_cast<qint64>(startupFrames));
        QCOMPARE(controller.timelineEventViews().size(), 1);
        QCOMPARE(controller.timelineEventViews().front().toMap()
                     .value(QStringLiteral("timelineEnd")).toLongLong(),
                 static_cast<qint64>(startupFrames));
        QVERIFY(QFileInfo::exists(output));
    }

    void delayedRecordingStopCancelsBeforeCaptureBecomesReady()
    {
        auto gate = std::make_shared<ManualRecordingStartGate>();
        auto capture = std::make_unique<ManualRecordingCapture>(gate);
        AudioEditorController controller(
            AG_AUDIO_BACKEND_NULL, std::move(capture));
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString output = temporary.filePath(QStringLiteral("delayed.wav"));

        QVERIFY(controller.startRecording(
            QUrl::fromLocalFile(output), {}, 16'000, 2, false, false));
        QVERIFY(gate->waitForStartAttempt(std::chrono::seconds(2)));
        const EditorSessionState startingState = controller.state();
        const bool startingIsRecording = controller.recording();
        const qint64 startingFrames = controller.recordingFrames();
        const bool startingAllowsOpen = controller.actionEnabled(
            QStringLiteral("editor.open"));
        const bool stopAccepted = controller.stopRecording();
        gate->release();

        QCOMPARE(startingState, EditorSessionState::RecordingStarting);
        QVERIFY(startingIsRecording);
        QCOMPARE(startingFrames, 0);
        QVERIFY(!startingAllowsOpen);
        QVERIFY(stopAccepted);
        QTRY_COMPARE_WITH_TIMEOUT(controller.state(),
                                  EditorSessionState::Empty, 5'000);
        QVERIFY(!controller.recording());
        QVERIFY(!QFileInfo::exists(output));
    }

    void delayedRecordingDeactivateNeverStartsCaptureAfterTheWindowHides()
    {
        auto gate = std::make_shared<ManualRecordingStartGate>();
        auto capture = std::make_unique<ManualRecordingCapture>(gate);
        AudioEditorController controller(
            AG_AUDIO_BACKEND_NULL, std::move(capture));
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString output = temporary.filePath(
            QStringLiteral("delayed-deactivate.wav"));

        QVERIFY(controller.startRecording(
            QUrl::fromLocalFile(output), {}, 16'000, 2, false, false));
        QVERIFY(gate->waitForStartAttempt(std::chrono::seconds(2)));
        QCOMPARE(controller.state(), EditorSessionState::RecordingStarting);
        controller.deactivate();
        gate->release();

        QTRY_COMPARE_WITH_TIMEOUT(controller.state(),
                                  EditorSessionState::Empty, 5'000);
        QTest::qWait(100);
        QVERIFY(!controller.recording());
        QVERIFY(!QFileInfo::exists(output));
    }

    void destructionWaitsForDelayedRecordingStartThenCancelsIt()
    {
        auto gate = std::make_shared<ManualRecordingStartGate>();
        auto capture = std::make_unique<ManualRecordingCapture>(gate);
        auto controller = std::make_unique<AudioEditorController>(
            AG_AUDIO_BACKEND_NULL, std::move(capture));
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString output = temporary.filePath(
            QStringLiteral("delayed-destruction.wav"));

        QVERIFY(controller->startRecording(
            QUrl::fromLocalFile(output), {}, 16'000, 2, false, false));
        QVERIFY(gate->waitForStartAttempt(std::chrono::seconds(2)));
        std::thread releaseStart([gate] {
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
            gate->release();
        });
        controller.reset();
        releaseStart.join();

        QVERIFY(!QFileInfo::exists(output));
        QVERIFY(!QFileInfo::exists(output
            + QStringLiteral(".agplayer-recording.tmp")));
        QVERIFY(!QFileInfo::exists(output
            + QStringLiteral(".agplayer-recording.journal")));
    }

    void liveRecordingSampleModePreservesAdjacentPcmShape()
    {
        auto capture = std::make_unique<ManualRecordingCapture>();
        ManualRecordingCapture* const driver = capture.get();
        AudioEditorController controller(
            AG_AUDIO_BACKEND_NULL, std::move(capture));
        controller.viewport()->setViewportWidth(8.0);
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString output = temporary.filePath(
            QStringLiteral("live-samples.wav"));

        QVERIFY(controller.startRecording(
            QUrl::fromLocalFile(output), {}, 16'000, 1, false, false));
        QTRY_COMPARE_WITH_TIMEOUT(controller.state(),
                                  EditorSessionState::Recording, 5'000);
        const std::vector<float> samples{
            -0.9F, -0.5F, -0.1F, 0.2F, 0.7F, -0.3F, 0.4F, 0.8F,
            -0.7F, 0.6F, -0.2F, 0.1F, 0.5F, -0.4F, 0.3F, 0.0F};
        QCOMPARE(driver->feed(samples, samples.size()), samples.size());
        QTRY_COMPARE_WITH_TIMEOUT(controller.recordingFrames(), 16, 1'000);
        QTRY_COMPARE_WITH_TIMEOUT(
            controller.viewport()->documentFrames(), 16, 1'000);
        QVERIFY(controller.viewport()->setVisibleRange(4, 12));
        QTRY_VERIFY_WITH_TIMEOUT(
            controller.viewportChannelPeaks().size() == 1, 1'000);

        const QVariantList channel =
            controller.viewportChannelPeaks().front().toList();
        QCOMPARE(channel.size(), 16);
        for (qsizetype point = 0; point < 8; ++point) {
            const double expected = samples[static_cast<std::size_t>(point + 4)];
            QVERIFY(std::abs(channel[point * 2].toDouble() - expected)
                    < 0.000001);
            QVERIFY(std::abs(channel[point * 2 + 1].toDouble() - expected)
                    < 0.000001);
        }
        QVERIFY(controller.cancelRecording());
    }

    void fadeInGestureCommitsOneObservableUndoStep()
    {
        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(controller.createUntitledDocument(48'000, 2, 1'000));
        const QString id = controller.timelineEventViews().front().toMap()
            .value(QStringLiteral("id")).toString();
        const auto historyBefore = controller.historyStateIdForTesting();

        QVERIFY(controller.beginEventGesture(
            id, QStringLiteral("fadeIn"), false));
        QVERIFY(controller.setEventFadeIn(id, 120));
        QVERIFY(controller.setEventFadeIn(id, 240));
        QCOMPARE(controller.historyStateIdForTesting(), historyBefore);
        QCOMPARE(controller.timelineEventViews().front().toMap()
                     .value(QStringLiteral("fadeIn")).toLongLong(), qint64{240});
        QVERIFY(controller.endEventGesture());
        QCOMPARE(controller.historyStateIdForTesting(), historyBefore + 1);
        QVERIFY(controller.undo());
        QCOMPARE(controller.timelineEventViews().front().toMap()
                     .value(QStringLiteral("fadeIn")).toLongLong(), qint64{0});
    }

    void successfulDocumentReplacementClearsEventGestureAfterPreparation()
    {
        const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_EDITOR_FIXTURE"));
        if (fixture.isEmpty()) QSKIP("fixture not configured");
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString project = temporary.filePath(QStringLiteral("replacement.agproj"));
        AudioEditorController projectMaker(AG_AUDIO_BACKEND_NULL);
        QVERIFY(projectMaker.openFile(QUrl::fromLocalFile(fixture)));
        QVERIFY(projectMaker.saveProjectAs(QUrl::fromLocalFile(project)));

        const auto exercise = [&](const auto& replace, const bool keepsEvent) {
            AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
            QVERIFY(controller.openFile(QUrl::fromLocalFile(fixture)));
            QVERIFY(controller.beginEventGesture(QStringLiteral("1"),
                                                 QStringLiteral("move"), false));
            QVERIFY(controller.moveEvent(QStringLiteral("1"), 123));
            QCOMPARE(controller.timelineEventViews().first().toMap()
                         .value(QStringLiteral("timelineStart")).toLongLong(),
                     qint64{123});

            QVERIFY(replace(controller));
            if (keepsEvent) {
                QCOMPARE(controller.timelineEventViews().size(), 1);
                QCOMPARE(controller.timelineEventViews().first().toMap()
                             .value(QStringLiteral("id")).toString(),
                         QStringLiteral("1"));
                QCOMPARE(controller.timelineEventViews().first().toMap()
                             .value(QStringLiteral("timelineStart")).toLongLong(),
                         qint64{0});
            } else {
                QVERIFY(controller.timelineEventViews().isEmpty());
            }
            QVERIFY(!controller.endEventGesture());
        };

        exercise([](AudioEditorController& controller) {
            return controller.createUntitledDocument(48'000, 2, 2'000);
        }, true);
        exercise([&](AudioEditorController& controller) {
            return controller.openFile(QUrl::fromLocalFile(fixture));
        }, true);
        exercise([&](AudioEditorController& controller) {
            return controller.openProject(QUrl::fromLocalFile(project));
        }, true);
        exercise([](AudioEditorController& controller) {
            return controller.clearDocument();
        }, false);
    }

    void failedDocumentOpenPreservesEventGesture()
    {
        const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_EDITOR_FIXTURE"));
        if (fixture.isEmpty()) QSKIP("fixture not configured");
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const auto exercise = [&](const auto& openMissing) {
            AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
            QVERIFY(controller.openFile(QUrl::fromLocalFile(fixture)));
            QVERIFY(controller.beginEventGesture(QStringLiteral("1"),
                                                 QStringLiteral("move"), false));
            QVERIFY(controller.moveEvent(QStringLiteral("1"), 123));

            QVERIFY(!openMissing(controller));
            QCOMPARE(controller.timelineEventViews().first().toMap()
                         .value(QStringLiteral("timelineStart")).toLongLong(),
                     qint64{123});
            QVERIFY(controller.endEventGesture());
            QCOMPARE(controller.timelineEventViews().first().toMap()
                         .value(QStringLiteral("timelineStart")).toLongLong(),
                     qint64{123});
        };
        exercise([&](AudioEditorController& controller) {
            return controller.openFile(QUrl::fromLocalFile(
                temporary.filePath(QStringLiteral("missing.wav"))));
        });
        exercise([&](AudioEditorController& controller) {
            return controller.openProject(QUrl::fromLocalFile(
                temporary.filePath(QStringLiteral("missing.agproj"))));
        });
    }

    void eventGesturePreviewRejectsTimelineEndOverflowWithoutMutation()
    {
        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(controller.createUntitledDocument(48'000, 2, 1'000));
        const qint64 maximum = std::numeric_limits<qint64>::max();
        const quint64 revision = controller.timelineRevisionForTesting();
        const std::uint64_t history = controller.historyStateIdForTesting();
        QSignalSpy documentChanges(&controller,
                                   &AudioEditorController::documentChanged);

        QVERIFY(controller.beginEventGesture(QStringLiteral("1"),
                                             QStringLiteral("move"), false));
        documentChanges.clear();
        QVERIFY(!controller.moveEvent(QStringLiteral("1"), maximum));
        QVERIFY(!controller.moveEvent(QStringLiteral("1"), maximum - 999));
        QCOMPARE(documentChanges.count(), 0);
        QCOMPARE(controller.timelineEventViews().first().toMap()
                     .value(QStringLiteral("timelineStart")).toLongLong(), qint64{0});
        QCOMPARE(controller.timelineRevisionForTesting(), revision);
        QCOMPARE(controller.historyStateIdForTesting(), history);
        QVERIFY(controller.moveEvent(QStringLiteral("1"), maximum - 1'000));
        QCOMPARE(documentChanges.count(), 1);
        QCOMPARE(controller.timelineEventViews().first().toMap()
                     .value(QStringLiteral("timelineEnd")).toLongLong(), maximum);
        QCOMPARE(controller.timelineRevisionForTesting(), revision);
        QCOMPARE(controller.historyStateIdForTesting(), history);
        QVERIFY(controller.cancelEventGesture());

        QVERIFY(controller.beginEventGesture(QStringLiteral("1"),
                                             QStringLiteral("trim"), false));
        documentChanges.clear();
        QVERIFY(!controller.trimEvent(QStringLiteral("1"), 0, 500, maximum));
        QVERIFY(!controller.trimEvent(QStringLiteral("1"), 0, 500,
                                      maximum - 499));
        QCOMPARE(documentChanges.count(), 0);
        QCOMPARE(controller.timelineEventViews().first().toMap()
                     .value(QStringLiteral("timelineStart")).toLongLong(), qint64{0});
        QCOMPARE(controller.timelineRevisionForTesting(), revision);
        QCOMPARE(controller.historyStateIdForTesting(), history);
        QVERIFY(controller.trimEvent(QStringLiteral("1"), 0, 500,
                                     maximum - 500));
        QCOMPARE(documentChanges.count(), 1);
        QCOMPARE(controller.timelineEventViews().first().toMap()
                     .value(QStringLiteral("timelineEnd")).toLongLong(), maximum);
        QCOMPARE(controller.timelineRevisionForTesting(), revision);
        QCOMPARE(controller.historyStateIdForTesting(), history);
        QVERIFY(controller.cancelEventGesture());
    }

    void selectionAndViewportUseOneExactFramePixelMapping()
    {
        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(controller.createUntitledDocument(96'000, 2, 691'200'000));
        controller.viewport()->setViewportWidth(1'167.0);
        QVERIFY(controller.viewport()->setVisibleRange(400'000'123, 400'960'123));
        const qint64 start = controller.viewport()->frameAtPixel(177.25);
        const qint64 end = controller.viewport()->frameAtPixel(899.75);
        QVERIFY(controller.setSelection(start, end));
        QCOMPARE(controller.selectionStart(), start);
        QCOMPARE(controller.selectionEnd(), end);
        QVERIFY(std::abs(controller.viewport()->pixelAtFrame(start) - 177.25) <= 0.01);
        QVERIFY(std::abs(controller.viewport()->pixelAtFrame(end) - 899.75) <= 0.01);
    }

    void selectionDragRendersARealTemporaryWavAndInvalidatesItOnChange()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString source = temporary.filePath(QStringLiteral("drag-source.wav"));
        QVERIFY(agplayer::test::writeClickTrackWav(source, 120, 2));

        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(controller.openFile(QUrl::fromLocalFile(source)));
        QVERIFY(controller.setSelection(4'000, 12'000));
        QSignalSpy dragChanged(
            &controller, &AudioEditorController::selectionDragChanged);

        QVERIFY(controller.prepareSelectionDrag());
        QTRY_VERIFY_WITH_TIMEOUT(controller.selectionDragReady(), 10'000);
        QVERIFY(dragChanged.count() > 0);
        const QUrl dragUrl = controller.selectionDragFile();
        QVERIFY(dragUrl.isLocalFile());
        QVERIFY(QFileInfo::exists(dragUrl.toLocalFile()));
        QFile rendered(dragUrl.toLocalFile());
        QVERIFY(rendered.open(QIODevice::ReadOnly));
        QCOMPARE(rendered.read(4), QByteArray("RIFF", 4));
        rendered.close();
        const DecodedProbe probe = decodeProbe(dragUrl.toLocalFile());
        QCOMPARE(probe.frames, qint64{8'000});
        QCOMPARE(probe.metadata.sample_rate, 16'000);

        QVERIFY(controller.setSelection(6'000, 10'000));
        QVERIFY(!controller.selectionDragReady());
        QVERIFY(controller.selectionDragFile().isEmpty());
    }

    void creatingASelectionEnablesLoopAndClearingItDisablesLoop()
    {
        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(controller.createUntitledDocument(48'000, 2, 96'000));
        QVERIFY(!controller.loopEnabled());

        QVERIFY(controller.setSelection(12'000, 36'000));
        QVERIFY(controller.loopEnabled());

        QVERIFY(controller.clearSelection());
        QVERIFY(!controller.loopEnabled());
    }

    void playbackAndRecordingAnnounceExclusivePreviewBeforeStarting()
    {
        AudioEditorController playback(AG_AUDIO_BACKEND_NULL);
        QVERIFY(playback.createUntitledDocument(48'000, 2, 4'800));
        QVERIFY(playback.setSelection(1'000, 2'000));
        QVERIFY(playback.seekFrame(3'000));
        QSignalSpy playbackStarting(
            &playback, &AudioEditorController::exclusivePreviewStarting);
        QVERIFY(playback.playPause());
        QCOMPARE(playbackStarting.count(), 1);
        QCOMPARE(playback.playheadFrame(), qint64{1'000});
        QVERIFY(playback.loopEnabled());
        playback.cancelOperation();

        AudioEditorController recording(AG_AUDIO_BACKEND_NULL);
        QSignalSpy recordingStarting(
            &recording, &AudioEditorController::exclusivePreviewStarting);
        QVERIFY(recording.startRecordingToTemporaryFile(
            QStringLiteral("capture:device-does-not-exist"), 48'000, 2,
            false, false));
        QCOMPARE(recordingStarting.count(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(!recording.recording(), 5'000);
    }

    void invalidViewportRequestsCancelOlderWorkAndAdvanceGeneration()
    {
        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(controller.createUntitledDocument(48'000, 2, 96'000));
        controller.viewport()->setViewportWidth(320.0);
        const quint64 validGeneration = controller.viewportWaveformGeneration();

        controller.viewport()->setViewportWidth(0.0);

        QVERIFY(controller.viewportWaveformGeneration() > validGeneration);
        QVERIFY(controller.viewportChannelPeaks().isEmpty());
    }

    void splitAndMergeRouteThroughController()
    {
        AudioEditorController controller;
        QVERIFY(controller.createUntitledDocument(48'000, 2, 1'000));
        QVERIFY(controller.splitEvent(1, 400));
        QVERIFY(controller.mergeEvents(1, 2));
        QCOMPARE(controller.totalFrames(), qint64{1'000});
    }

    void processingActionsRouteToRealDocumentEdits()
    {
        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(controller.createUntitledDocument(48'000, 2, 1'000));
        QVERIFY(controller.setSelection(200, 800));
        for (const QString& id : {QStringLiteral("editor.silenceSelection"),
                                  QStringLiteral("editor.fadeIn"),
                                  QStringLiteral("editor.fadeOut")}) {
            const auto before = controller.historyStateIdForTesting();
            QVERIFY(controller.actionEnabled(id));
            QVERIFY(controller.triggerAction(id));
            QVERIFY(controller.historyStateIdForTesting() != before);
            QVERIFY(controller.undo());
        }
        QVERIFY(controller.actionEnabled(QStringLiteral("editor.cropToSelection")));
        QVERIFY(controller.triggerAction(QStringLiteral("editor.cropToSelection")));
        QCOMPARE(controller.totalFrames(), qint64{600});
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
        QVERIFY(loaded.playbackSupported());
        QVERIFY(loaded.bpmDetectionSupported());
        QVERIFY(loaded.exportSupported());
        QVERIFY(!loaded.playPause());
        QVERIFY(!loaded.detectBpm());

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

        QVERIFY(loaded.actionEnabled(QStringLiteral("editor.export")));
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
        QVERIFY(controller.exportSupported());
        QVERIFY(controller.saveAs(QUrl::fromLocalFile(output)));
        QTRY_COMPARE_WITH_TIMEOUT(controller.state(), EditorSessionState::Ready,
                                  10'000);
        QFile file(output);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.read(4), QByteArray("RIFF", 4));
        QVERIFY(controller.projectPath().isEmpty());
    }

    void configuredDirectoryExportCreatesANonOverwritingAudioFile()
    {
        using agplayer::editor::ProjectExportSettings;
        const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_EDITOR_FIXTURE"));
        if (fixture.isEmpty()) QSKIP("fixture not configured");
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString source = temporary.filePath(QStringLiteral("source.wav"));
        const QString occupied = temporary.filePath(
            QStringLiteral("source_edited.wav"));
        QVERIFY(QFile::copy(fixture, source));
        QFile collision(occupied);
        QVERIFY(collision.open(QIODevice::WriteOnly));
        QVERIFY(collision.write("occupied") > 0);
        collision.close();

        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(controller.openFile(QUrl::fromLocalFile(source)));
        ProjectExportSettings settings;
        settings.codecName = QStringLiteral("wav");
        settings.outputDirectory = temporary.path();
        QVERIFY(controller.setProjectExportSettings(settings));
        QVERIFY(controller.exportToConfiguredDirectory());
        QTRY_COMPARE_WITH_TIMEOUT(controller.state(), EditorSessionState::Ready,
                                  10'000);
        QCOMPARE(QFile(occupied).size(), qint64{8});
        QFile exported(temporary.filePath(QStringLiteral("source_edited_2.wav")));
        QVERIFY(exported.open(QIODevice::ReadOnly));
        QCOMPARE(exported.read(4), QByteArray("RIFF", 4));
    }

    void trackMixPropertiesClampAndEnforceSingleTrackSolo()
    {
        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QSignalSpy mixChanged(&controller, &AudioEditorController::trackMixChanged);
        QCOMPARE(controller.trackMuted(), false);
        QCOMPARE(controller.trackSolo(), false);
        QCOMPARE(controller.trackGainDb(), 0.0);

        controller.setTrackMuted(true);
        QCOMPARE(controller.trackMuted(), true);
        QCOMPARE(controller.trackSolo(), false);
        controller.setTrackSolo(true);
        QCOMPARE(controller.trackMuted(), false);
        QCOMPARE(controller.trackSolo(), true);

        controller.setTrackGainDb(-100.0);
        QCOMPARE(controller.trackGainDb(), -60.0);
        controller.setTrackGainDb(100.0);
        QCOMPARE(controller.trackGainDb(), 12.0);
        QVERIFY(mixChanged.count() >= 4);
    }

    void trackMixAffectsExportWithoutMutatingDocumentHistory()
    {
        using agplayer::editor::AudioFileAnalyzer;
        using agplayer::editor::ProjectExportSettings;
        const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_EDITOR_FIXTURE"));
        if (fixture.isEmpty()) QSKIP("fixture not configured");
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const auto sourceAnalysis = AudioFileAnalyzer::analyze(
            std::filesystem::path(fixture.toStdWString()), 4'096);
        QVERIFY(sourceAnalysis.success);
        const float sourcePeak = maximumAbsolutePeak(sourceAnalysis);
        QVERIFY(sourcePeak > 0.1F);

        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(controller.openFile(QUrl::fromLocalFile(fixture)));
        const auto historyBefore = controller.historyStateIdForTesting();
        ProjectExportSettings settings;
        settings.codecName = QStringLiteral("wav");
        settings.outputDirectory = temporary.path();
        QVERIFY(controller.setProjectExportSettings(settings));

        controller.setTrackGainDb(-6.020599913);
        QVERIFY(controller.exportToConfiguredDirectory());
        QTRY_COMPARE_WITH_TIMEOUT(controller.state(), EditorSessionState::Ready,
                                  10'000);
        const QString stem = QFileInfo(fixture).completeBaseName();
        const QString gainedPath = temporary.filePath(
            QStringLiteral("%1_edited.wav").arg(stem));
        const auto gained = AudioFileAnalyzer::analyze(
            std::filesystem::path(gainedPath.toStdWString()), 4'096);
        QVERIFY2(gained.success, gained.message.c_str());
        const float gainedPeak = maximumAbsolutePeak(gained);
        QVERIFY(gainedPeak > sourcePeak * 0.45F);
        QVERIFY(gainedPeak < sourcePeak * 0.55F);

        controller.setTrackMuted(true);
        QVERIFY(controller.exportToConfiguredDirectory());
        QTRY_COMPARE_WITH_TIMEOUT(controller.state(), EditorSessionState::Ready,
                                  10'000);
        const QString mutedPath = temporary.filePath(
            QStringLiteral("%1_edited_2.wav").arg(stem));
        const auto muted = AudioFileAnalyzer::analyze(
            std::filesystem::path(mutedPath.toStdWString()), 4'096);
        QVERIFY2(muted.success, muted.message.c_str());
        QVERIFY(maximumAbsolutePeak(muted) < 0.00001F);
        QCOMPARE(controller.historyStateIdForTesting(), historyBefore);
    }

    void configuredWavExportHonorsBitDepth_data()
    {
        QTest::addColumn<int>("bitDepth");
        QTest::newRow("16-bit") << 16;
        QTest::newRow("24-bit") << 24;
        QTest::newRow("32-bit") << 32;
    }

    void configuredWavExportHonorsBitDepth()
    {
        using agplayer::editor::AudioFileAnalyzer;
        using agplayer::editor::ProjectExportSettings;
        QFETCH(int, bitDepth);
        const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_EDITOR_FIXTURE"));
        if (fixture.isEmpty()) QSKIP("fixture not configured");
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(controller.openFile(QUrl::fromLocalFile(fixture)));
        ProjectExportSettings settings;
        settings.codecName = QStringLiteral("wav");
        settings.bitDepth = bitDepth;
        settings.outputDirectory = temporary.path();
        QVERIFY(controller.setProjectExportSettings(settings));
        QVERIFY(controller.exportToConfiguredDirectory());
        QTRY_COMPARE_WITH_TIMEOUT(controller.state(), EditorSessionState::Ready,
                                  10'000);
        const auto exported = AudioFileAnalyzer::analyze(
            std::filesystem::path(
                temporary.filePath(QStringLiteral("%1_edited.wav").arg(
                    QFileInfo(fixture).completeBaseName())).toStdWString()),
            32);
        QVERIFY2(exported.success, exported.message.c_str());
        QCOMPARE(exported.bits_per_sample, bitDepth);
    }

    void formalExportAppliesSpeedPitchKeepPitchAndFormantProcessing()
    {
        const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_EDITOR_FIXTURE"));
        if (fixture.isEmpty()) QSKIP("fixture not configured");
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(controller.openFile(QUrl::fromLocalFile(fixture)));
        const DecodedProbe original = decodeProbe(fixture);
        QVERIFY(original.frames > 0);

        const auto exportAndWait = [&controller](const QString& path) {
            if (!controller.exportTo(QUrl::fromLocalFile(path), false,
                                     QStringLiteral("pcm_s16le"))) {
                return false;
            }
            QElapsedTimer timer;
            timer.start();
            while (controller.state() != EditorSessionState::Ready
                   && timer.elapsed() < 15'000) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
                QTest::qWait(10);
            }
            return controller.state() == EditorSessionState::Ready;
        };

        QVERIFY(controller.setSpeedPercent(150.0));
        controller.setKeepPitch(true);
        QVERIFY(controller.setPitch(0, 0));
        const QString keepPitchPath = temporary.filePath(
            QStringLiteral("speed-keep-pitch.wav"));
        QVERIFY2(exportAndWait(keepPitchPath), qPrintable(controller.errorMessage()));
        const DecodedProbe kept = decodeProbe(keepPitchPath);
        QVERIFY(std::llabs(kept.frames
            - static_cast<qint64>(std::llround(original.frames / 1.5))) < 2'048);
        QVERIFY(std::abs(positiveCrossingFrequency(kept) - 440.0) < 25.0);

        controller.setKeepPitch(false);
        const QString followSpeedPath = temporary.filePath(
            QStringLiteral("speed-follow-pitch.wav"));
        QVERIFY2(exportAndWait(followSpeedPath), qPrintable(controller.errorMessage()));
        const DecodedProbe followed = decodeProbe(followSpeedPath);
        QVERIFY(std::llabs(followed.frames
            - static_cast<qint64>(std::llround(original.frames / 1.5))) < 2'048);
        QVERIFY(std::abs(positiveCrossingFrequency(followed) - 660.0) < 35.0);

        QVERIFY(controller.setSpeedPercent(100.0));
        controller.setKeepPitch(true);
        QVERIFY(controller.setPitch(7, 0));
        controller.setFormantPreservation(false);
        const QString unprotectedPath = temporary.filePath(
            QStringLiteral("pitch-unprotected.wav"));
        QVERIFY2(exportAndWait(unprotectedPath), qPrintable(controller.errorMessage()));
        const DecodedProbe pitched = decodeProbe(unprotectedPath);
        QVERIFY(std::abs(positiveCrossingFrequency(pitched) - 659.3) < 35.0);

        controller.setFormantPreservation(true);
        const QString protectedPath = temporary.filePath(
            QStringLiteral("pitch-protected.wav"));
        QVERIFY2(exportAndWait(protectedPath), qPrintable(controller.errorMessage()));
        QFile unprotected(unprotectedPath);
        QFile protectedFile(protectedPath);
        QVERIFY(unprotected.open(QIODevice::ReadOnly));
        QVERIFY(protectedFile.open(QIODevice::ReadOnly));
        QVERIFY(unprotected.readAll() != protectedFile.readAll());
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

        QVERIFY(controller.playbackSupported());
        QVERIFY2(controller.playPause(), qPrintable(controller.errorMessage()));
        QTRY_VERIFY_WITH_TIMEOUT(controller.playheadFrame() != 123, 5'000);
        QVERIFY(controller.modified());

        QVERIFY(controller.stopPlayback());
        QCOMPARE(controller.playheadFrame(), qint64{0});
        QVERIFY(controller.modified());
    }

    void selectionPlayheadJumpTracksDirtyBeforeAsyncPreviewFailure()
    {
        const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_EDITOR_FIXTURE"));
        if (fixture.isEmpty()) QSKIP("fixture not configured");
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString source = temporary.filePath(QStringLiteral("source.wav"));
        const QString project = temporary.filePath(QStringLiteral("selection.agproj"));
        QVERIFY(QFile::copy(fixture, source));

        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(controller.openFile(QUrl::fromLocalFile(source)));
        QVERIFY(controller.setSelection(100, 200));
        QVERIFY(controller.saveProjectAs(QUrl::fromLocalFile(project)));
        QVERIFY(!controller.modified());
        QVERIFY(QFile::remove(source));

        QSignalSpy documentChanges(&controller, &AudioEditorController::documentChanged);
        QVERIFY(controller.playPause());
        QCOMPARE(controller.playheadFrame(), qint64{100});
        QVERIFY(controller.modified());
        QCOMPARE(documentChanges.count(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(controller.state(), EditorSessionState::Error,
                                  10'000);
    }

    void unavailableProjectSourceRelinkClearsIssue()
    {
        const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_EDITOR_FIXTURE"));
        if (fixture.isEmpty()) QSKIP("fixture not configured");
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString source = temporary.filePath(QStringLiteral("source.wav"));
        const QString project = temporary.filePath(QStringLiteral("budget.agproj"));
        QVERIFY(QFile::copy(fixture, source));

        AudioEditorController maker(AG_AUDIO_BACKEND_NULL);
        QVERIFY(maker.openFile(QUrl::fromLocalFile(source)));
        QVERIFY(maker.saveProjectAs(QUrl::fromLocalFile(project)));

        QFile projectFile(project);
        QVERIFY(projectFile.open(QIODevice::ReadOnly));
        QJsonObject root = QJsonDocument::fromJson(projectFile.readAll()).object();
        projectFile.close();
        const QJsonObject templateSource = root.value(QStringLiteral("sources"))
            .toArray().first().toObject();
        QJsonObject event = root.value(QStringLiteral("events")).toArray().first().toObject();
        QJsonArray sources;
        for (int index = 0; index < 4'096; ++index) {
            QJsonObject record = templateSource;
            record.insert(QStringLiteral("sourceId"), QString::number(index + 1));
            sources.append(record);
        }
        event.insert(QStringLiteral("sourceId"), QStringLiteral("4096"));
        root.insert(QStringLiteral("sources"), sources);
        root.insert(QStringLiteral("events"), QJsonArray{event});
        root.insert(QStringLiteral("markers"), QJsonArray{});
        QVERIFY(projectFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QVERIFY(projectFile.write(QJsonDocument(root).toJson()) > 0);
        projectFile.close();

        AudioEditorController loaded(AG_AUDIO_BACKEND_NULL);
        QVERIFY2(loaded.openProject(QUrl::fromLocalFile(project)),
                 qPrintable(loaded.errorMessage()));
        QCOMPARE(loaded.projectIssues().size(), 1);
        const QVariantMap issue = loaded.projectIssues().constFirst().toMap();
        QCOMPARE(issue.value(QStringLiteral("kind")).toString(), QStringLiteral("unavailable"));
        QCOMPARE(issue.value(QStringLiteral("sourceId")).toULongLong(), quint64{4'096});

        QVERIFY2(loaded.relinkProjectSource(quint64{4'096}, QUrl::fromLocalFile(source)),
                 qPrintable(loaded.errorMessage()));
        QVERIFY(loaded.projectIssues().isEmpty());
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

    void obsoleteProjectSourcesAreDiscardedAfterTheirUndoHistoryExpires()
    {
        const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_EDITOR_FIXTURE"));
        if (fixture.isEmpty()) QSKIP("fixture not configured");
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString firstSource = temporary.filePath(QStringLiteral("first.wav"));
        const QString secondSource = temporary.filePath(QStringLiteral("second.wav"));
        const QString project = temporary.filePath(QStringLiteral("history.agproj"));
        QVERIFY(QFile::copy(fixture, firstSource));
        QVERIFY(QFile::copy(fixture, secondSource));

        AudioEditorController maker(AG_AUDIO_BACKEND_NULL);
        QVERIFY(maker.openFile(QUrl::fromLocalFile(firstSource)));
        const qint64 split = maker.totalFrames() / 2;
        QVERIFY(split > 0);
        QVERIFY(maker.splitEvent(1, split));
        QVERIFY(maker.saveProjectAs(QUrl::fromLocalFile(project)));

        QFile projectFile(project);
        QVERIFY(projectFile.open(QIODevice::ReadOnly));
        QJsonObject root = QJsonDocument::fromJson(projectFile.readAll()).object();
        projectFile.close();
        QJsonArray sources = root.value(QStringLiteral("sources")).toArray();
        QJsonObject secondRecord = sources.at(0).toObject();
        secondRecord.insert(QStringLiteral("sourceId"), QStringLiteral("2"));
        secondRecord.insert(QStringLiteral("pathKind"), QStringLiteral("relative"));
        secondRecord.insert(QStringLiteral("path"), QStringLiteral("second.wav"));
        const QFileInfo secondInfo(secondSource);
        secondRecord.insert(QStringLiteral("fileSize"), QString::number(secondInfo.size()));
        secondRecord.insert(QStringLiteral("lastModifiedUtcMs"),
                            QString::number(secondInfo.lastModified().toUTC().toMSecsSinceEpoch()));
        sources.append(secondRecord);
        root.insert(QStringLiteral("sources"), sources);
        QJsonArray events = root.value(QStringLiteral("events")).toArray();
        QJsonObject secondEvent = events.at(1).toObject();
        secondEvent.insert(QStringLiteral("sourceId"), QStringLiteral("2"));
        events.replace(1, secondEvent);
        root.insert(QStringLiteral("events"), events);
        QVERIFY(projectFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QVERIFY(projectFile.write(QJsonDocument(root).toJson()) > 0);
        projectFile.close();
        QVERIFY(QFile::remove(secondSource));

        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(controller.openProject(QUrl::fromLocalFile(project)));
        QVERIFY(controller.setSelection(split, controller.totalFrames()));
        QVERIFY(controller.triggerAction(QStringLiteral("editor.deleteSelection")));
        for (int index = 1; index <= 257; ++index) {
            QVERIFY(controller.moveEvent(1, index));
        }
        QVERIFY(!controller.relinkProjectSource(quint64{2}, QUrl::fromLocalFile(fixture)));
    }

    void clipboardKeepsOfflineSourceIdentityAfterCutHistoryExpires()
    {
        const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_EDITOR_FIXTURE"));
        if (fixture.isEmpty()) QSKIP("fixture not configured");
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString firstSource = temporary.filePath(QStringLiteral("first.wav"));
        const QString secondSource = temporary.filePath(QStringLiteral("second.wav"));
        const QString project = temporary.filePath(QStringLiteral("clipboard-history.agproj"));
        QVERIFY(QFile::copy(fixture, firstSource));
        QVERIFY(QFile::copy(fixture, secondSource));

        AudioEditorController maker(AG_AUDIO_BACKEND_NULL);
        QVERIFY(maker.openFile(QUrl::fromLocalFile(firstSource)));
        const qint64 split = maker.totalFrames() / 2;
        QVERIFY(split > 0);
        QVERIFY(maker.splitEvent(1, split));
        QVERIFY(maker.saveProjectAs(QUrl::fromLocalFile(project)));

        QFile projectFile(project);
        QVERIFY(projectFile.open(QIODevice::ReadOnly));
        QJsonObject root = QJsonDocument::fromJson(projectFile.readAll()).object();
        projectFile.close();
        QJsonArray sources = root.value(QStringLiteral("sources")).toArray();
        QJsonObject secondRecord = sources.at(0).toObject();
        secondRecord.insert(QStringLiteral("sourceId"), QStringLiteral("2"));
        secondRecord.insert(QStringLiteral("pathKind"), QStringLiteral("relative"));
        secondRecord.insert(QStringLiteral("path"), QStringLiteral("second.wav"));
        const QFileInfo secondInfo(secondSource);
        secondRecord.insert(QStringLiteral("fileSize"), QString::number(secondInfo.size()));
        secondRecord.insert(QStringLiteral("lastModifiedUtcMs"),
                            QString::number(secondInfo.lastModified().toUTC().toMSecsSinceEpoch()));
        sources.append(secondRecord);
        root.insert(QStringLiteral("sources"), sources);
        QJsonArray events = root.value(QStringLiteral("events")).toArray();
        QJsonObject secondEvent = events.at(1).toObject();
        secondEvent.insert(QStringLiteral("sourceId"), QStringLiteral("2"));
        events.replace(1, secondEvent);
        root.insert(QStringLiteral("events"), events);
        QVERIFY(projectFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QVERIFY(projectFile.write(QJsonDocument(root).toJson()) > 0);
        projectFile.close();

        QFile changed(secondSource);
        QVERIFY(changed.open(QIODevice::Append));
        QVERIFY(changed.write("identity mismatch") > 0);
        changed.close();

        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(controller.openProject(QUrl::fromLocalFile(project)));
        QCOMPARE(controller.projectIssues().size(), 1);
        QCOMPARE(controller.projectIssues().constFirst().toMap()
                     .value(QStringLiteral("kind")).toString(),
                 QStringLiteral("identityMismatch"));
        QVERIFY(controller.setSelection(split, controller.totalFrames()));
        QVERIFY(controller.triggerAction(QStringLiteral("editor.cut")));
        QVERIFY(controller.projectIssues().isEmpty());
        for (int index = 1; index <= 257; ++index) {
            QVERIFY(controller.moveEvent(1, index));
        }
        QVERIFY(controller.seekFrame(controller.totalFrames()));
        QVERIFY(controller.triggerAction(QStringLiteral("editor.paste")));
        QCOMPARE(controller.projectIssues().size(), 1);
        QCOMPARE(controller.projectIssues().constFirst().toMap()
                     .value(QStringLiteral("sourceId")).toULongLong(), quint64{2});
        QCOMPARE(controller.projectIssues().constFirst().toMap()
                     .value(QStringLiteral("kind")).toString(),
                 QStringLiteral("identityMismatch"));
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
        expected.bitDepth = 16;
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
        QCOMPARE(restored.bitDepth, expected.bitDepth);
        QCOMPARE(restored.channels, expected.channels);
        QCOMPARE(restored.bitRate, expected.bitRate);
        QCOMPARE(restored.keepMetadata, expected.keepMetadata);
        QCOMPARE(restored.variableBitRate, expected.variableBitRate);
        QCOMPARE(restored.quality, expected.quality);
        QCOMPARE(restored.outputDirectory, expected.outputDirectory);

        QVERIFY(loaded.exportSupported());
        QVERIFY(loaded.exportTo(QUrl::fromLocalFile(exported)));
        QCOMPARE(loaded.projectExportSettings().codecName, expected.codecName);
        QCOMPARE(loaded.projectExportSettings().sampleRate, expected.sampleRate);
        QCOMPARE(loaded.projectExportSettings().bitDepth, expected.bitDepth);
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

    void visibleTimelineWaveformKeepsGapsBlankAndLatestRequestWins()
    {
        const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_EDITOR_FIXTURE"));
        if (fixture.isEmpty()) QSKIP("fixture not configured");
        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY2(controller.openFile(QUrl::fromLocalFile(fixture)),
                 qPrintable(controller.errorMessage()));
        const qint64 quarter = controller.totalFrames() / 4;
        QVERIFY(quarter > 0);
        QVERIFY(controller.setSelection(quarter, quarter * 2));
        QVERIFY(controller.triggerAction(QStringLiteral("editor.deleteSelection")));

        controller.viewport()->setViewportWidth(120.0);
        QVERIFY(controller.viewport()->setVisibleRange(0, quarter));
        QVERIFY(controller.viewport()->setVisibleRange(quarter, quarter * 2));

        const auto allBucketsBlank = [&controller] {
            const QVariantList channels = controller.viewportChannelPeaks();
            if (channels.isEmpty()) return false;
            for (const QVariant& channelValue : channels) {
                const QVariantList values = channelValue.toList();
                if (values.isEmpty() || values.size() % 2 != 0
                    || values.size() / 2 > 240) {
                    return false;
                }
                for (const QVariant& value : values) {
                    if (value.isValid() && !value.isNull()) return false;
                }
            }
            return true;
        };
        QTRY_VERIFY_WITH_TIMEOUT(allBucketsBlank(), 10'000);

        QVERIFY(controller.viewport()->setVisibleRange(quarter * 2,
                                                        controller.totalFrames()));
        const auto hasVisiblePeak = [&controller] {
            for (const QVariant& channelValue : controller.viewportChannelPeaks()) {
                const QVariantList values = channelValue.toList();
                if (values.size() / 2 > 240) return false;
                for (const QVariant& value : values) {
                    if (value.isValid() && !value.isNull()
                        && std::abs(value.toDouble()) > 0.001) {
                        return true;
                    }
                }
            }
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(hasVisiblePeak(), 10'000);
    }

    void viewportDecodeIsSingleFlightAndPublishesOnlyLatestPendingRequest()
    {
        const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_EDITOR_FIXTURE"));
        if (fixture.isEmpty()) QSKIP("fixture not configured");
        QSemaphore firstStarted;
        QSemaphore releaseFirst;
        std::atomic_int active{0};
        std::atomic_int maximum{0};
        std::atomic_int starts{0};
        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(controller.openFile(QUrl::fromLocalFile(fixture)));
        const qint64 quarter = controller.totalFrames() / 4;
        QVERIFY(controller.setSelection(quarter, quarter * 2));
        QVERIFY(controller.triggerAction(QStringLiteral("editor.deleteSelection")));
        controller.setViewportWaveformTaskObserverForTesting(
            [&](const bool starting) {
                if (!starting) {
                    --active;
                    return;
                }
                const int now = ++active;
                int observed = maximum.load();
                while (now > observed
                       && !maximum.compare_exchange_weak(observed, now)) {}
                if (++starts == 1) {
                    firstStarted.release();
                    releaseFirst.acquire();
                }
            });

        controller.viewport()->setViewportWidth(120.0);
        QVERIFY(firstStarted.tryAcquire(1, 5'000));
        QVERIFY(controller.viewport()->setVisibleRange(quarter, quarter * 2));
        QVERIFY(controller.viewport()->setVisibleRange(quarter * 2,
                                                        controller.totalFrames()));
        QTest::qWait(100);
        QCOMPARE(starts.load(), 1);
        QCOMPARE(maximum.load(), 1);
        releaseFirst.release();

        QTRY_COMPARE_WITH_TIMEOUT(starts.load(), 2, 10'000);
        QTRY_COMPARE_WITH_TIMEOUT(active.load(), 0, 10'000);
        QCOMPARE(maximum.load(), 1);
        const auto hasVisiblePeak = [&controller] {
            for (const QVariant& channelValue : controller.viewportChannelPeaks()) {
                for (const QVariant& value : channelValue.toList()) {
                    if (value.isValid() && !value.isNull()
                        && std::abs(value.toDouble()) > 0.001) return true;
                }
            }
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(hasVisiblePeak(), 10'000);
    }

    void invalidViewportRequestCancelsActiveAndDropsPendingDecode()
    {
        const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_EDITOR_FIXTURE"));
        if (fixture.isEmpty()) QSKIP("fixture not configured");
        QSemaphore firstStarted;
        QSemaphore releaseFirst;
        std::atomic_int active{0};
        std::atomic_int starts{0};
        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(controller.openFile(QUrl::fromLocalFile(fixture)));
        controller.setViewportWaveformTaskObserverForTesting(
            [&](const bool starting) {
                if (!starting) {
                    --active;
                    return;
                }
                ++active;
                if (++starts == 1) {
                    firstStarted.release();
                    releaseFirst.acquire();
                }
            });

        controller.viewport()->setViewportWidth(120.0);
        QVERIFY(firstStarted.tryAcquire(1, 5'000));
        QVERIFY(controller.viewport()->setVisibleRange(
            controller.totalFrames() / 4, controller.totalFrames() / 2));
        const quint64 pendingGeneration = controller.viewportWaveformGeneration();
        controller.viewport()->setViewportWidth(0.0);
        QVERIFY(controller.viewportWaveformGeneration() > pendingGeneration);
        QVERIFY(controller.viewportChannelPeaks().isEmpty());
        releaseFirst.release();

        QTRY_COMPARE_WITH_TIMEOUT(active.load(), 0, 10'000);
        QCOMPARE(starts.load(), 1);
        QVERIFY(controller.viewportChannelPeaks().isEmpty());
    }

    void noiseReductionCommitsOneUndoablePersistentReplacement()
    {
        const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_EDITOR_FIXTURE"));
        if (fixture.isEmpty()) QSKIP("fixture not configured");
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString source = temporary.filePath(QStringLiteral("source.wav"));
        const QString project = temporary.filePath(QStringLiteral("session.agproj"));
        QVERIFY(QFile::copy(fixture, source));
        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(controller.openFile(QUrl::fromLocalFile(source)));
        QVERIFY(controller.saveProjectAs(QUrl::fromLocalFile(project)));
        const QString originalPath = controller.filePath();
        const auto historyBefore = controller.historyStateIdForTesting();

        QVERIFY(controller.reduceNoise());
        QCOMPARE(controller.state(), EditorSessionState::Processing);
        QVERIFY(controller.noiseReductionActive());
        QTRY_COMPARE_WITH_TIMEOUT(controller.state(), EditorSessionState::Ready,
                                  15'000);
        QVERIFY(!controller.noiseReductionActive());
        QVERIFY(controller.historyStateIdForTesting() != historyBefore);
        QVERIFY(controller.modified());
        QVERIFY(controller.filePath().contains(QStringLiteral("noise-reduced")));
        QVERIFY(QFileInfo::exists(controller.filePath()));
        const QString expectedMediaDirectory = QDir(temporary.path()).filePath(
            QStringLiteral("session.media"));
        QCOMPARE(QFileInfo(controller.filePath()).absolutePath(),
                 QFileInfo(expectedMediaDirectory).absoluteFilePath());
        QVERIFY(controller.undo());
        QCOMPARE(controller.historyStateIdForTesting(), historyBefore);
        QCOMPARE(controller.filePath(), originalPath);
    }

    void obsoleteReplacementOperationsAreAbsent()
    {
        const QMetaObject& meta = AudioEditorController::staticMetaObject;
        QCOMPARE(meta.indexOfMethod("applyTimePitch()"), -1);
    }
};

QTEST_GUILESS_MAIN(AudioEditorControllerTest)

#include "audio_editor_controller_test.moc"
