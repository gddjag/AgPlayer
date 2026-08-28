#include "audio_editor/audio_editor_controller.hpp"
#include "audio_editor/audio_file_analyzer.hpp"
#include "playback_controller.hpp"
#include "../core/bpm_fixture.hpp"
#include "decoder.hpp"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSemaphore>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

#include <atomic>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
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

void writeU16(std::ostream& stream, const std::uint16_t value)
{
    const std::array<char, 2> bytes{static_cast<char>(value & 0xffU),
        static_cast<char>((value >> 8U) & 0xffU)};
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void writeU32(std::ostream& stream, const std::uint32_t value)
{
    const std::array<char, 4> bytes{static_cast<char>(value & 0xffU),
        static_cast<char>((value >> 8U) & 0xffU),
        static_cast<char>((value >> 16U) & 0xffU),
        static_cast<char>((value >> 24U) & 0xffU)};
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

bool writeMonoFloatWav(const QString& path, const std::vector<float>& samples,
                       const std::uint32_t sampleRate = 8'000)
{
    std::ofstream stream(std::filesystem::path(path.toStdWString()),
                         std::ios::binary | std::ios::trunc);
    const auto bytes = static_cast<std::uint32_t>(
        samples.size() * sizeof(float));
    stream.write("RIFF", 4); writeU32(stream, 36U + bytes);
    stream.write("WAVEfmt ", 8); writeU32(stream, 16U); writeU16(stream, 3U);
    writeU16(stream, 1U); writeU32(stream, sampleRate);
    writeU32(stream, sampleRate * sizeof(float));
    writeU16(stream, sizeof(float)); writeU16(stream, 32U);
    stream.write("data", 4); writeU32(stream, bytes);
    stream.write(reinterpret_cast<const char*>(samples.data()),
                 static_cast<std::streamsize>(bytes));
    return stream.good();
}

bool writeStereoFloatWav(const QString& path,
                         const std::vector<float>& interleavedSamples,
                         const std::uint32_t sampleRate = 8'000)
{
    if (interleavedSamples.empty() || interleavedSamples.size() % 2U != 0U) {
        return false;
    }
    std::ofstream stream(std::filesystem::path(path.toStdWString()),
                         std::ios::binary | std::ios::trunc);
    const auto bytes = static_cast<std::uint32_t>(
        interleavedSamples.size() * sizeof(float));
    stream.write("RIFF", 4); writeU32(stream, 36U + bytes);
    stream.write("WAVEfmt ", 8); writeU32(stream, 16U); writeU16(stream, 3U);
    writeU16(stream, 2U); writeU32(stream, sampleRate);
    writeU32(stream, sampleRate * 2U * sizeof(float));
    writeU16(stream, 2U * sizeof(float)); writeU16(stream, 32U);
    stream.write("data", 4); writeU32(stream, bytes);
    stream.write(reinterpret_cast<const char*>(interleavedSamples.data()),
                 static_cast<std::streamsize>(bytes));
    return stream.good();
}

double peakValue(const QVariantList& values, const qsizetype point,
                 const bool maximum)
{
    const qsizetype index = point * 2 + (maximum ? 1 : 0);
    return index >= 0 && index < values.size() ? values[index].toDouble() : 0.0;
}

QString saveConstantProject(const QString& directory, const QString& name,
                            const std::vector<float>& amplitudes,
                            const qint64 frames = 240'000)
{
    using namespace agplayer::editor;
    std::vector<AudioEvent> events;
    events.reserve(amplitudes.size());
    for (std::size_t index = 0; index < amplitudes.size(); ++index) {
        const QString sourcePath = QDir(directory).filePath(
            QStringLiteral("%1-source-%2.wav").arg(name).arg(index));
        if (!writeMonoFloatWav(sourcePath,
                std::vector<float>(static_cast<std::size_t>(frames),
                                   amplitudes[index]))) {
            return {};
        }
        const AudioFileAnalysis analysis = AudioFileAnalyzer::analyze(
            std::filesystem::path(sourcePath.toStdWString()), 64);
        if (!analysis.success) return {};
        events.push_back(AudioEvent{
            static_cast<EventId>(index + 1U),
            std::make_shared<const AudioSource>(analysis.source),
            0, frames, static_cast<SampleFrame>(index) * frames});
    }
    AudioDocument document = AudioDocument::fromEvents(std::move(events));
    const QString projectPath = QDir(directory).filePath(name + ".agproj");
    ProjectSaveRequest request;
    request.document = &document;
    request.visibleEndFrame = document.totalFrames();
    return ProjectDocument::save(projectPath, request).ok()
        ? projectPath : QString{};
}

bool waitForDocumentLoad(AudioEditorController& controller,
                         const int timeoutMs = 10'000)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (controller.loading() && elapsed.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QTest::qWait(1);
    }
    return !controller.loading();
}

bool acceptedDocumentLoadCompleted(AudioEditorController& controller,
                                   const bool accepted)
{
    return accepted && waitForDocumentLoad(controller)
        && controller.state() != EditorSessionState::Error;
}

bool openFileAndWait(AudioEditorController& controller, const QUrl& source)
{
    return acceptedDocumentLoadCompleted(controller,
                                         controller.openFile(source));
}

bool openProjectAndWait(AudioEditorController& controller, const QUrl& source)
{
    return acceptedDocumentLoadCompleted(controller,
                                         controller.openProject(source));
}

bool relinkProjectSourceAndWait(AudioEditorController& controller,
                                const quint64 sourceId,
                                const QUrl& replacement)
{
    return acceptedDocumentLoadCompleted(controller,
        controller.relinkProjectSource(sourceId, replacement));
}

bool relinkProjectSourceAndWait(AudioEditorController& controller,
                                const QString& sourceId,
                                const QUrl& replacement)
{
    return acceptedDocumentLoadCompleted(controller,
        controller.relinkProjectSource(sourceId, replacement));
}

bool confirmDiscardAndWait(AudioEditorController& controller)
{
    return acceptedDocumentLoadCompleted(
        controller, controller.confirmDiscardAndOpen());
}

} // namespace

class AudioEditorControllerTest final : public QObject {
    Q_OBJECT

private slots:
    void defaultExportSettingsAreImmediatelyUsable()
    {
        AudioEditorController controller;
        const auto settings = controller.projectExportSettings();

        QCOMPARE(settings.codecName, QStringLiteral("MP3"));
        QCOMPARE(settings.sampleRate, 44'100);
        QCOMPARE(settings.bitDepth, 24);
        QCOMPARE(settings.channels, 2);
        QCOMPARE(settings.bitRate, qint64{320'000});
        QCOMPARE(QDir::cleanPath(settings.outputDirectory),
                 QDir::cleanPath(QStandardPaths::writableLocation(
                     QStandardPaths::DesktopLocation)));
        QVERIFY(agplayer::editor::isValidProjectExportSettings(settings));
    }

    void handoffServicesAreLazyAndReleasedWithThePage()
    {
        AudioEditorController controller;
        QVERIFY(!controller.handoffServicesCreatedForTesting());

        const QByteArray fixture = qgetenv("AGPLAYER_EDITOR_FIXTURE");
        QVERIFY2(!fixture.isEmpty(), "AGPLAYER_EDITOR_FIXTURE is required");
        QVERIFY(openFileAndWait(controller,
            QUrl::fromLocalFile(QString::fromUtf8(fixture))));
        QVERIFY(controller.setSelection(0, std::min<qint64>(
            controller.totalFrames(), controller.sampleRate())));
        QVERIFY(!controller.handoffServicesCreatedForTesting());

        QVERIFY(controller.beginSelectionHandoff(10.0, 10.0));
        QVERIFY(controller.handoffServicesCreatedForTesting());
        controller.deactivate();
        QVERIFY(!controller.handoffServicesCreatedForTesting());
    }

    void productionControllerRequiresInjectedSharedPlayer()
    {
        ag_player_config config{};
        config.backend = AG_AUDIO_BACKEND_NULL;
        ag_player* sharedPlayer = nullptr;
        QCOMPARE(ag_player_create_with_config(&config, &sharedPlayer), AG_OK);
        PlaybackController playback(sharedPlayer);
        {
            AudioEditorController controller;
            QVERIFY(!controller.playbackSupported());
            controller.setPlaybackController(&playback);
            QVERIFY(controller.playbackSupported());
            QCOMPARE(controller.playerHandleForTesting(), sharedPlayer);
        }
        ag_player_destroy(sharedPlayer);
    }

    void emptyDocumentDisablesEditActions()
    {
        AudioEditorController controller;
        QVERIFY(controller.actionEnabled(QStringLiteral("editor.open")));
        QVERIFY(!controller.actionEnabled(QStringLiteral("editor.cut")));
        QVERIFY(!controller.actionEnabled(QStringLiteral("editor.paste")));
        QVERIFY(!controller.actionEnabled(QStringLiteral("editor.undo")));
    }

    void bpmDetectionAnalyzesTheCurrentTimeline()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString source = temporary.filePath(QStringLiteral("click-120.wav"));
        QVERIFY(agplayer::test::writeClickTrackWav(source, 120, 8));

        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(controller.bpmDetectionSupported());
        QVERIFY(openFileAndWait(controller, QUrl::fromLocalFile(source)));
        QVERIFY(controller.detectBpm());
        QCOMPARE(controller.state(), EditorSessionState::Processing);
        QVERIFY(controller.bpmBusy());
        QTRY_COMPARE_WITH_TIMEOUT(controller.state(), EditorSessionState::Ready,
                                  15'000);
        QVERIFY(!controller.bpmBusy());
        QVERIFY2(std::abs(controller.originalBpm() - 120.0) < 1.0,
                 qPrintable(QString::number(controller.originalBpm())));
        QVERIFY2(std::abs(controller.bpmResult() - 120.0) < 1.0,
                 qPrintable(QString::number(controller.bpmResult())));
        QVERIFY(controller.bpmError().isEmpty());
    }

    void bpmDetectionCancellationPublishesCancelledState()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString source = temporary.filePath(QStringLiteral("click-120-long.wav"));
        QVERIFY(agplayer::test::writeClickTrackWav(source, 120, 30));

        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(openFileAndWait(controller, QUrl::fromLocalFile(source)));
        QVERIFY(controller.detectBpm());
        controller.cancelOperation();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.bpmBusy(), 10'000);
        QCOMPARE(controller.state(), EditorSessionState::Ready);
        QCOMPARE(controller.bpmResult(), 0.0);
        QVERIFY(!controller.bpmError().isEmpty());
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
        QVERIFY(openFileAndWait(controller, QUrl::fromLocalFile(fixture)));
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
        QVERIFY(!controller.loopEnabled());
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
        QVERIFY(openFileAndWait(saved, QUrl::fromLocalFile(source)));
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
        QVERIFY2(openProjectAndWait(controller, QUrl::fromLocalFile(project)),
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

    void selectionLoopClearAndSharedBoundaryGesturesAreTransactional()
    {
        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(controller.createUntitledDocument(48'000, 2, 1'000));
        QVERIFY(controller.setSelection(100, 300));
        QVERIFY(controller.loopEnabled());
        QVERIFY(controller.clearSelection());
        QVERIFY(!controller.loopEnabled());

        QVERIFY(controller.addEnvelopePoint(QStringLiteral("1"), 700, 0.2));
        QVERIFY(controller.splitEvent(QStringLiteral("1"), 400));
        const auto historyAfterSplit = controller.historyStateIdForTesting();
        QVERIFY(controller.beginSharedBoundaryGesture(
            QStringLiteral("1"), QStringLiteral("2")));
        QVERIFY(controller.trimSharedBoundary(QStringLiteral("1"),
                                              QStringLiteral("2"), 250));
        const QVariantList preview = controller.timelineEventViews();
        const auto previewLeft = preview[0].toMap();
        const qint64 previewLength = previewLeft.value(
            QStringLiteral("sourceEnd")).toLongLong()
            - previewLeft.value(QStringLiteral("sourceStart")).toLongLong();
        for (const QVariant& point : previewLeft.value(
                 QStringLiteral("envelope")).toList()) {
            QVERIFY(point.toMap().value(QStringLiteral("offset")).toLongLong()
                    < previewLength);
        }
        QVERIFY(controller.trimSharedBoundary(QStringLiteral("1"),
                                              QStringLiteral("2"), 640));
        QCOMPARE(controller.historyStateIdForTesting(), historyAfterSplit);
        QVERIFY(controller.endEventGesture());
        QCOMPARE(controller.historyStateIdForTesting(), historyAfterSplit + 1);
        const QVariantList views = controller.timelineEventViews();
        QCOMPARE(views[0].toMap().value(QStringLiteral("sourceEnd")).toLongLong(),
                 qint64{640});
        QCOMPARE(views[1].toMap().value(QStringLiteral("sourceStart")).toLongLong(),
                 qint64{640});

        QVERIFY(controller.clearTimeline());
        QCOMPARE(controller.totalFrames(), qint64{0});
        QVERIFY(controller.undo());
        QCOMPARE(controller.timelineEventViews().size(), 2);
    }

    void selectionPlaybackPollSeeksBackToSelectionStart()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString source = temporary.filePath(QStringLiteral("loop.wav"));
        QVERIFY(writeMonoFloatWav(source, std::vector<float>(16'000, 0.25F)));

        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(openFileAndWait(controller, QUrl::fromLocalFile(source)));
        QVERIFY(controller.setSelection(4'000, 8'000));
        QVERIFY(controller.seekFrame(4'000));
        bool pollArmed = false;
        bool observedSelectionEnd = false;
        bool loopedToSelectionStart = false;
        QObject::connect(&controller, &AudioEditorController::playbackChanged,
                         &controller, [&] {
            if (!pollArmed) return;
            if (controller.playheadFrame() == 8'000) {
                observedSelectionEnd = true;
            } else if (observedSelectionEnd && controller.playing()
                       && controller.playheadFrame() == 4'000) {
                loopedToSelectionStart = true;
            }
        });
        QVERIFY2(controller.playPause(), qPrintable(controller.errorMessage()));
        pollArmed = true;
        QVERIFY(controller.seekFrame(8'000));
        QVERIFY(observedSelectionEnd);
        QTRY_VERIFY_WITH_TIMEOUT(loopedToSelectionStart, 2'000);
        QVERIFY(controller.playing());
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

    void gainAndEnvelopePointGesturesPreviewCommitOnceAndCancelCleanly()
    {
        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(controller.createUntitledDocument(48'000, 2, 1'000));
        const QString id = controller.timelineEventViews().front().toMap()
            .value(QStringLiteral("id")).toString();
        const auto initialHistory = controller.historyStateIdForTesting();

        QVERIFY(controller.beginEventGainGesture(id));
        QVERIFY(controller.updateEventGainGesture(0.25));
        QVERIFY(controller.updateEventGainGesture(0.75));
        QCOMPARE(controller.historyStateIdForTesting(), initialHistory);
        QCOMPARE(controller.timelineEventViews().front().toMap()
                     .value(QStringLiteral("gain")).toDouble(), 0.75);
        QVERIFY(controller.cancelEventGainGesture());
        QCOMPARE(controller.timelineEventViews().front().toMap()
                     .value(QStringLiteral("gain")).toDouble(), 1.0);
        QCOMPARE(controller.historyStateIdForTesting(), initialHistory);

        QVERIFY(controller.beginEventGainGesture(id));
        QVERIFY(controller.updateEventGainGesture(0.5));
        QVERIFY(controller.endEventGainGesture());
        QCOMPARE(controller.historyStateIdForTesting(), initialHistory + 1);
        QCOMPARE(controller.timelineEventViews().front().toMap()
                     .value(QStringLiteral("gain")).toDouble(), 0.5);

        QVERIFY(controller.addEnvelopePoint(id, 300, 0.4));
        const auto envelopeHistory = controller.historyStateIdForTesting();
        QVERIFY(controller.beginEnvelopePointGesture(id, 300));
        QVERIFY(controller.updateEnvelopePointGesture(420, 0.8));
        QVERIFY(controller.updateEnvelopePointGesture(450, 1.2));
        QCOMPARE(controller.historyStateIdForTesting(), envelopeHistory);
        auto preview = controller.timelineEventViews().front().toMap()
            .value(QStringLiteral("envelope")).toList();
        QCOMPARE(preview.front().toMap().value(QStringLiteral("offset")).toLongLong(),
                 qint64{450});
        QVERIFY(std::abs(preview.front().toMap()
            .value(QStringLiteral("gain")).toDouble() - 1.2) < 0.0001);
        QVERIFY(controller.cancelEnvelopePointGesture());
        preview = controller.timelineEventViews().front().toMap()
            .value(QStringLiteral("envelope")).toList();
        QCOMPARE(preview.front().toMap().value(QStringLiteral("offset")).toLongLong(),
                 qint64{300});
        QCOMPARE(controller.historyStateIdForTesting(), envelopeHistory);

        QVERIFY(controller.beginEnvelopePointGesture(id, 300));
        QVERIFY(controller.updateEnvelopePointGesture(500, 0.9));
        QVERIFY(controller.endEnvelopePointGesture());
        QCOMPARE(controller.historyStateIdForTesting(), envelopeHistory + 1);
        QVERIFY(controller.removeEnvelopePoint(id, 500));
        QVERIFY(controller.timelineEventViews().front().toMap()
                    .value(QStringLiteral("envelope")).toList().isEmpty());
    }

    void gainGestureWaveformPreviewsLiveAndCoalescesPendingJobs()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString source = temporary.filePath(QStringLiteral("gesture.wav"));
        QVERIFY(writeMonoFloatWav(source, std::vector<float>(1'024, 0.5F)));
        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(openFileAndWait(controller, QUrl::fromLocalFile(source)));
        controller.viewport()->setViewportWidth(32.0);
        QVERIFY(controller.viewport()->setVisibleRange(0, 1'024));
        QTRY_VERIFY_WITH_TIMEOUT(!controller.viewportChannelPeaks().isEmpty(),
                                 10'000);
        const QString id = controller.timelineEventViews().front().toMap()
            .value(QStringLiteral("id")).toString();
        const auto history = controller.historyStateIdForTesting();

        QSemaphore firstStarted;
        QSemaphore releaseFirst;
        std::atomic_int active{0};
        std::atomic_int maximum{0};
        std::atomic_int starts{0};
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

        QVERIFY(controller.beginEventGainGesture(id));
        QVERIFY(controller.updateEventGainGesture(0.25));
        QVERIFY(firstStarted.tryAcquire(1, 5'000));
        QVERIFY(!controller.viewportChannelPeaks().isEmpty());
        QVERIFY(controller.updateEventGainGesture(0.5));
        QVERIFY(controller.updateEventGainGesture(0.75));
        QTest::qWait(100);
        QCOMPARE(starts.load(), 1);
        QCOMPARE(maximum.load(), 1);
        QCOMPARE(controller.historyStateIdForTesting(), history);
        releaseFirst.release();
        QTRY_COMPARE_WITH_TIMEOUT(starts.load(), 2, 10'000);
        QTRY_COMPARE_WITH_TIMEOUT(active.load(), 0, 10'000);
        const auto previewReady = [&controller] {
            const QVariantList channels = controller.viewportChannelPeaks();
            if (channels.isEmpty()) return false;
            const QVariantList peaks = channels.front().toList();
            return !peaks.isEmpty()
                && std::abs(peakValue(peaks, 0, true) - 0.375) < 0.01;
        };
        QTRY_VERIFY_WITH_TIMEOUT(previewReady(), 10'000);

        QVERIFY(controller.cancelEventGainGesture());
        const auto restored = [&controller] {
            const QVariantList channels = controller.viewportChannelPeaks();
            if (channels.isEmpty()) return false;
            return std::abs(peakValue(channels.front().toList(), 0, true)
                            - 0.5) < 0.01;
        };
        QTRY_VERIFY_WITH_TIMEOUT(restored(), 10'000);
        QCOMPARE(controller.historyStateIdForTesting(), history);

        QVERIFY(controller.beginEventGainGesture(id));
        QVERIFY(controller.updateEventGainGesture(0.4));
        const auto committedPreview = [&controller] {
            const QVariantList channels = controller.viewportChannelPeaks();
            if (channels.isEmpty()) return false;
            return std::abs(peakValue(channels.front().toList(), 0, true)
                            - 0.2) < 0.01;
        };
        QTRY_VERIFY_WITH_TIMEOUT(committedPreview(), 10'000);
        QVERIFY(controller.endEventGainGesture());
        QVERIFY(!controller.viewportChannelPeaks().isEmpty());
        QCOMPARE(controller.historyStateIdForTesting(), history + 1);
        QTRY_VERIFY_WITH_TIMEOUT(committedPreview(), 10'000);
    }

    void successfulDocumentReplacementClearsEventGestureAfterPreparation()
    {
        const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_EDITOR_FIXTURE"));
        if (fixture.isEmpty()) QSKIP("fixture not configured");
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString project = temporary.filePath(QStringLiteral("replacement.agproj"));
        AudioEditorController projectMaker(AG_AUDIO_BACKEND_NULL);
        QVERIFY(openFileAndWait(projectMaker, QUrl::fromLocalFile(fixture)));
        QVERIFY(projectMaker.saveProjectAs(QUrl::fromLocalFile(project)));

        const auto exercise = [&](const auto& replace, const bool keepsEvent) {
            AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
            QVERIFY(openFileAndWait(controller, QUrl::fromLocalFile(fixture)));
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
            return openFileAndWait(controller, QUrl::fromLocalFile(fixture));
        }, true);
        exercise([&](AudioEditorController& controller) {
            return openProjectAndWait(controller, QUrl::fromLocalFile(project));
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
            QVERIFY(openFileAndWait(controller, QUrl::fromLocalFile(fixture)));
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
            return openFileAndWait(controller, QUrl::fromLocalFile(
                temporary.filePath(QStringLiteral("missing.wav"))));
        });
        exercise([&](AudioEditorController& controller) {
            return openProjectAndWait(controller, QUrl::fromLocalFile(
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
        QVERIFY(openFileAndWait(controller, QUrl::fromLocalFile(source)));
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
        QVERIFY(openFileAndWait(controller, QUrl::fromLocalFile(source)));
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
        QVERIFY(openFileAndWait(controller, QUrl::fromLocalFile(source)));
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
        QVERIFY2(openProjectAndWait(restored, QUrl::fromLocalFile(project)),
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
        QVERIFY(openFileAndWait(controller, QUrl::fromLocalFile(source)));
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
        QVERIFY2(openFileAndWait(controller, QUrl::fromLocalFile(source)),
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
        QVERIFY2(openProjectAndWait(loaded, QUrl::fromLocalFile(project)),
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

        QVERIFY(openFileAndWait(controller, QUrl::fromLocalFile(source)));
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
        QVERIFY(openFileAndWait(original, QUrl::fromLocalFile(source)));
        QVERIFY(original.saveProjectAs(QUrl::fromLocalFile(project)));
        QVERIFY(QFile::remove(source));

        AudioEditorController loaded(AG_AUDIO_BACKEND_NULL);
        QVERIFY2(openProjectAndWait(loaded, QUrl::fromLocalFile(project)),
                 qPrintable(loaded.errorMessage()));
        QCOMPARE(loaded.projectIssues().size(), 1);
        const QVariantMap issue = loaded.projectIssues().constFirst().toMap();
        QCOMPARE(issue.value(QStringLiteral("kind")).toString(),
                 QStringLiteral("missing"));
        QCOMPARE(issue.value(QStringLiteral("sourceId")).metaType(),
                 QMetaType::fromType<QString>());
        QSignalSpy projectChanges(&loaded, &AudioEditorController::projectChanged);
        QVERIFY(relinkProjectSourceAndWait(
            loaded, issue.value(QStringLiteral("sourceId")).toString(),
            QUrl::fromLocalFile(fixture)));
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
        QVERIFY2(openFileAndWait(original, QUrl::fromLocalFile(source)),
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
        QVERIFY2(openProjectAndWait(loaded, QUrl::fromLocalFile(project)),
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

        QVERIFY2(relinkProjectSourceAndWait(loaded, sourceId, QUrl::fromLocalFile(replacement)),
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

        QVERIFY(!openProjectAndWait(controller, QUrl::fromLocalFile(malformed)));
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
        QVERIFY(openFileAndWait(controller, QUrl::fromLocalFile(source)));
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
        QVERIFY(openFileAndWait(controller, QUrl::fromLocalFile(fixture)));
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
        QVERIFY(openFileAndWait(controller, QUrl::fromLocalFile(source)));
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

    void configuredExportDefaultsToAUsableDirectoryAndPublishesDecodedSuccessPath()
    {
        using agplayer::editor::AudioFileAnalyzer;
        using agplayer::editor::ProjectExportSettings;
        const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_EDITOR_FIXTURE"));
        if (fixture.isEmpty()) QSKIP("fixture not configured");
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());

        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(openFileAndWait(controller, QUrl::fromLocalFile(fixture)));
        QSignalSpy directoryRequested(
            &controller, &AudioEditorController::exportDirectoryRequested);
        QSignalSpy exportSucceeded(
            &controller, &AudioEditorController::exportSucceeded);
        QVERIFY(!controller.projectExportSettings().outputDirectory.isEmpty());
        QCOMPARE(directoryRequested.count(), 0);

        ProjectExportSettings settings;
        settings.codecName = QStringLiteral("wav");
        settings.outputDirectory = temporary.path();
        QVERIFY(controller.setProjectExportSettings(settings));
        QVERIFY(controller.exportToConfiguredDirectory());
        QTRY_COMPARE_WITH_TIMEOUT(controller.state(), EditorSessionState::Ready,
                                  10'000);
        QCOMPARE(exportSucceeded.count(), 1);
        QVERIFY(!controller.lastExportPath().isEmpty());
        QCOMPARE(exportSucceeded.constFirst().constFirst().toString(),
                 controller.lastExportPath());
        const auto decoded = AudioFileAnalyzer::analyze(
            std::filesystem::path(controller.lastExportPath().toStdWString()), 32);
        QVERIFY2(decoded.success, decoded.message.c_str());
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
        QVERIFY(openFileAndWait(controller, QUrl::fromLocalFile(fixture)));
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
        QVERIFY(openFileAndWait(controller, QUrl::fromLocalFile(fixture)));
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
        QVERIFY(openFileAndWait(controller, QUrl::fromLocalFile(fixture)));
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
        QVERIFY(openFileAndWait(projectMaker, QUrl::fromLocalFile(source)));
        QVERIFY(projectMaker.saveProjectAs(QUrl::fromLocalFile(project)));

        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(openFileAndWait(controller, QUrl::fromLocalFile(source)));
        QVERIFY(controller.trimEvent(1, 10, controller.totalFrames() - 10, 0));
        const qint64 changedFrames = controller.totalFrames();
        QSignalSpy requested(&controller,
                             &AudioEditorController::discardConfirmationRequested);
        QVERIFY(!openProjectAndWait(controller, QUrl::fromLocalFile(project)));
        QCOMPARE(requested.count(), 1);
        QCOMPARE(controller.totalFrames(), changedFrames);
        QVERIFY(controller.modified());

        QVERIFY(confirmDiscardAndWait(controller));
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
        QVERIFY(confirmDiscardAndWait(controller));
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
        QVERIFY(openFileAndWait(controller, QUrl::fromLocalFile(source)));
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
        QVERIFY(openFileAndWait(controller, QUrl::fromLocalFile(source)));
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

    void playbackTransportTransitionsImmediatelyWithoutRenderingPreview()
    {
        const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_EDITOR_FIXTURE"));
        if (fixture.isEmpty()) QSKIP("fixture not configured");

        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(openFileAndWait(controller, QUrl::fromLocalFile(fixture)));

        QVERIFY2(controller.playPause(), qPrintable(controller.errorMessage()));
        QCOMPARE(controller.state(), EditorSessionState::Playing);
        QVERIFY(controller.playing());

        QVERIFY2(controller.playPause(), qPrintable(controller.errorMessage()));
        QCOMPARE(controller.state(), EditorSessionState::Ready);
        QVERIFY(!controller.playing());
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
        QVERIFY(openFileAndWait(controller, QUrl::fromLocalFile(source)));
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

    void failedStopAndSeekKeepPublishedTransportState()
    {
        const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_EDITOR_FIXTURE"));
        if (fixture.isEmpty()) QSKIP("fixture not configured");
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString source = temporary.filePath(QStringLiteral("source.wav"));
        QVERIFY(QFile::copy(fixture, source));

        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(openFileAndWait(controller, QUrl::fromLocalFile(source)));
        QVERIFY(controller.seekFrame(123));
        QVERIFY(QFile::remove(source));
        QVERIFY(controller.playPause());
        QTRY_COMPARE_WITH_TIMEOUT(controller.state(), EditorSessionState::Error,
                                  10'000);

        const qint64 frameBefore = controller.playheadFrame();
        const qint64 positionBefore = controller.positionMs();
        QVERIFY(!controller.seekFrame(frameBefore + 333));
        QCOMPARE(controller.playheadFrame(), frameBefore);
        QCOMPARE(controller.positionMs(), positionBefore);
        QCOMPARE(controller.state(), EditorSessionState::Error);

        QVERIFY(!controller.stopPlayback());
        QCOMPARE(controller.playheadFrame(), frameBefore);
        QCOMPARE(controller.positionMs(), positionBefore);
        QCOMPARE(controller.state(), EditorSessionState::Error);
        QVERIFY(!controller.errorMessage().isEmpty());
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
        QVERIFY(openFileAndWait(maker, QUrl::fromLocalFile(source)));
        QVERIFY(maker.saveProjectAs(QUrl::fromLocalFile(project)));

        QFile projectFile(project);
        QVERIFY(projectFile.open(QIODevice::ReadOnly));
        QJsonObject root = QJsonDocument::fromJson(projectFile.readAll()).object();
        projectFile.close();
        const QJsonObject templateSource = root.value(QStringLiteral("sources"))
            .toArray().first().toObject();
        QJsonObject event = root.value(QStringLiteral("events")).toArray().first().toObject();
        const QDir projectDirectory = QFileInfo(project).dir();
        QJsonArray sources;
        for (int index = 0; index < 129; ++index) {
            const QString linkPath = temporary.filePath(
                QStringLiteral("source-link-%1.wav").arg(index + 1));
            std::error_code linkError;
            std::filesystem::create_hard_link(
                std::filesystem::path(source.toStdWString()),
                std::filesystem::path(linkPath.toStdWString()), linkError);
            QVERIFY2(!linkError, linkError.message().c_str());

            QJsonObject record = templateSource;
            record.insert(QStringLiteral("sourceId"), QString::number(index + 1));
            record.insert(QStringLiteral("pathKind"), QStringLiteral("relative"));
            record.insert(QStringLiteral("path"), QDir::fromNativeSeparators(
                projectDirectory.relativeFilePath(linkPath)));
            sources.append(record);
        }
        event.insert(QStringLiteral("sourceId"), QStringLiteral("129"));
        root.insert(QStringLiteral("sources"), sources);
        root.insert(QStringLiteral("events"), QJsonArray{event});
        root.insert(QStringLiteral("markers"), QJsonArray{});
        QVERIFY(projectFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QVERIFY(projectFile.write(QJsonDocument(root).toJson()) > 0);
        projectFile.close();

        AudioEditorController loaded(AG_AUDIO_BACKEND_NULL);
        QVERIFY2(openProjectAndWait(loaded, QUrl::fromLocalFile(project)),
                 qPrintable(loaded.errorMessage()));
        QCOMPARE(loaded.projectIssues().size(), 1);
        const QVariantMap issue = loaded.projectIssues().constFirst().toMap();
        QCOMPARE(issue.value(QStringLiteral("kind")).toString(), QStringLiteral("unavailable"));
        QCOMPARE(issue.value(QStringLiteral("sourceId")).toULongLong(), quint64{129});

        QVERIFY2(relinkProjectSourceAndWait(loaded, quint64{129}, QUrl::fromLocalFile(source)),
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
        QVERIFY(openFileAndWait(maker, QUrl::fromLocalFile(firstSource)));
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
        QVERIFY2(openProjectAndWait(controller, QUrl::fromLocalFile(project)),
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
        QVERIFY(openFileAndWait(maker, QUrl::fromLocalFile(firstSource)));
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
        QVERIFY(openProjectAndWait(controller, QUrl::fromLocalFile(project)));
        QVERIFY(controller.setSelection(split, controller.totalFrames()));
        QVERIFY(controller.triggerAction(QStringLiteral("editor.deleteSelection")));
        for (int index = 1; index <= 257; ++index) {
            QVERIFY(controller.moveEvent(1, index));
        }
        QVERIFY(!relinkProjectSourceAndWait(controller, quint64{2}, QUrl::fromLocalFile(fixture)));
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
        QVERIFY(openFileAndWait(maker, QUrl::fromLocalFile(firstSource)));
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
        QVERIFY(openProjectAndWait(controller, QUrl::fromLocalFile(project)));
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
        QVERIFY(openFileAndWait(controller, QUrl::fromLocalFile(source)));
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
        QVERIFY(openFileAndWait(controller, QUrl::fromLocalFile(source)));
        controller.setProjectExportSettings(expected);
        QVERIFY(controller.saveProjectAs(QUrl::fromLocalFile(project)));

        AudioEditorController loaded(AG_AUDIO_BACKEND_NULL);
        QVERIFY(openProjectAndWait(loaded, QUrl::fromLocalFile(project)));
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
        QVERIFY2(openFileAndWait(controller, QUrl::fromLocalFile(fixture)),
                 qPrintable(controller.errorMessage()));
        QVERIFY(controller.hasDocument());
        QVERIFY(controller.totalFrames() > 0);
        QCOMPARE(controller.fileName(), QFileInfo(fixture).fileName());
    }

    void timelineMutationRetainsLastGoodWaveformUntilNewestGenerationPublishes()
    {
        const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_EDITOR_FIXTURE"));
        if (fixture.isEmpty()) QSKIP("fixture not configured");
        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(openFileAndWait(controller, QUrl::fromLocalFile(fixture)));
        controller.viewport()->setViewportWidth(96.0);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.viewportChannelPeaks().isEmpty(),
                                 10'000);
        const QVariantList lastGood = controller.viewportChannelPeaks();
        const QString id = controller.timelineEventViews().front().toMap()
            .value(QStringLiteral("id")).toString();

        QVERIFY(controller.setEventGain(id, 0.5));
        QVERIFY(!controller.viewportChannelPeaks().isEmpty());
        QCOMPARE(controller.viewportChannelPeaks(), lastGood);
        QTRY_VERIFY_WITH_TIMEOUT(controller.viewportChannelPeaks() != lastGood,
                                 10'000);
        QVERIFY(!controller.viewportChannelPeaks().isEmpty());
    }

    void unavailableViewportReadKeepsLastGoodAfterTimelineMutations()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString source = temporary.filePath(QStringLiteral("source.wav"));
        constexpr qint64 frames = 2'048;
        QVERIFY(writeMonoFloatWav(source,
            std::vector<float>(static_cast<std::size_t>(frames), 0.5F)));
        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(openFileAndWait(controller, QUrl::fromLocalFile(source)));
        controller.viewport()->setViewportWidth(64.0);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.viewportChannelPeaks().isEmpty(),
                                 10'000);
        const QVariantList lastGood = controller.viewportChannelPeaks();

        // Remove all already-built source views before deleting the file so
        // every following edit exercises the real unavailable decoder path.
        controller.clearViewportSourcePeaksForTesting();
        QVERIFY(QFile::remove(source));
        std::atomic_int starts{0};
        std::atomic_int finishes{0};
        controller.setViewportWaveformTaskObserverForTesting(
            [&](const bool starting) {
                if (starting) {
                    ++starts;
                } else {
                    ++finishes;
                }
            });
        const auto verifyLastGood = [&] {
            QVERIFY2(starts.load() > 0,
                     "the unavailable viewport job was not scheduled");
            QTRY_COMPARE_WITH_TIMEOUT(finishes.load(), starts.load(), 5'000);
            QCOMPARE(controller.viewportChannelPeaks(), lastGood);
        };

        QString id = controller.timelineEventViews().front().toMap()
            .value(QStringLiteral("id")).toString();
        QVERIFY(controller.splitEvent(id, frames / 2));
        verifyLastGood();
        QVERIFY(controller.trimEvent(id, 0, frames / 4, 0));
        verifyLastGood();
        QVERIFY(controller.setSelection(0, frames / 8));
        QVERIFY(controller.triggerAction(QStringLiteral("editor.cropToSelection")));
        verifyLastGood();
        id = controller.timelineEventViews().front().toMap()
            .value(QStringLiteral("id")).toString();
        QVERIFY(controller.setEventGain(id, 0.5));
        verifyLastGood();
        QVERIFY(controller.addEnvelopePoint(id, 8, 0.5));
        verifyLastGood();
    }

    void viewportWaveformTargetUsesBoundedFractionalDevicePixelRatio()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString source = temporary.filePath(QStringLiteral("source.wav"));
        QVERIFY(writeMonoFloatWav(source, std::vector<float>(4'096, 0.5F)));
        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(openFileAndWait(controller, QUrl::fromLocalFile(source)));
        controller.viewport()->setViewportWidth(10.0);

        for (const auto [dpr, expectedBuckets] : {
                 std::pair{1.0, 20}, std::pair{1.25, 25},
                 std::pair{1.5, 30}, std::pair{2.0, 40}}) {
            controller.setViewportWaveformDevicePixelRatio(dpr);
            QTRY_VERIFY_WITH_TIMEOUT(
                controller.viewportChannelPeaks().size() == 1
                    && controller.viewportChannelPeaks().front().toList().size()
                        == expectedBuckets * 2,
                10'000);
        }
        controller.viewport()->setViewportWidth(0.4);
        controller.setViewportWaveformDevicePixelRatio(1.0);
        QTRY_VERIFY_WITH_TIMEOUT(
            controller.viewportChannelPeaks().size() == 1
                && controller.viewportChannelPeaks().front().toList().size() == 2,
            10'000);
    }

    void cachedAndPreciseWaveformsApplyGainFadeAndEnvelopeConsistently()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString source = temporary.filePath(QStringLiteral("constant.wav"));
        constexpr qint64 frames = 1'024;
        QVERIFY(writeMonoFloatWav(source,
            std::vector<float>(static_cast<std::size_t>(frames), 0.5F)));
        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY2(openFileAndWait(controller, QUrl::fromLocalFile(source)),
                 qPrintable(controller.errorMessage()));
        const QString id = controller.timelineEventViews().front().toMap()
            .value(QStringLiteral("id")).toString();
        QVERIFY(controller.setEventGain(id, 0.5));
        QVERIFY(controller.setEventFadeOut(id, 4));
        QVERIFY(controller.addEnvelopePoint(id, 0, 0.5));
        QVERIFY(controller.addEnvelopePoint(id, frames - 1, 1.5));

        // Force the wide view to use the already-built peak pyramid.  A
        // renderer that silently falls back to decoding the whole automated
        // slice will blank here instead of publishing cached peaks.
        QVERIFY(QFile::remove(source));
        controller.viewport()->setViewportWidth(32.0);
        QVERIFY(controller.viewport()->setVisibleRange(0, frames));
        const auto cachedReady = [&controller] {
            const QVariantList channels = controller.viewportChannelPeaks();
            return channels.size() == 1
                && channels.front().toList().size() == 128;
        };
        QTRY_VERIFY_WITH_TIMEOUT(cachedReady(), 10'000);
        const QVariantList cached = controller.viewportChannelPeaks()
            .front().toList();
        QVERIFY(peakValue(cached, 0, false) < -0.12);
        QVERIFY(peakValue(cached, 0, true) < 0.14);
        QVERIFY(peakValue(cached, 32, true) > 0.20);
        QVERIFY(peakValue(cached, 63, false) < -0.30);
        QVERIFY(peakValue(cached, 63, true) > 0.30);

        QVERIFY(writeMonoFloatWav(source,
            std::vector<float>(static_cast<std::size_t>(frames), 0.5F)));
        controller.viewport()->setViewportWidth(512.0);
        const auto preciseReady = [&controller] {
            const QVariantList channels = controller.viewportChannelPeaks();
            return channels.size() == 1
                && channels.front().toList().size() == 2'048;
        };
        QTRY_VERIFY_WITH_TIMEOUT(preciseReady(), 10'000);
        const QVariantList precise = controller.viewportChannelPeaks()
            .front().toList();
        QVERIFY(std::abs(peakValue(precise, 0, false) + 0.125) < 0.001);
        QVERIFY(std::abs(peakValue(precise, 0, true) - 0.125) < 0.001);
        QVERIFY(std::abs(peakValue(precise, 512, true) - 0.25) < 0.01);
        QVERIFY(std::abs(peakValue(precise, 1'023, true)) < 0.0001);

        for (qsizetype bucket = 0; bucket < 64; ++bucket) {
            double expectedMinimum = 1.0;
            double expectedMaximum = -1.0;
            for (qsizetype frame = bucket * 16;
                 frame < (bucket + 1) * 16; ++frame) {
                expectedMinimum = std::min(expectedMinimum,
                    peakValue(precise, frame, false));
                expectedMaximum = std::max(expectedMaximum,
                    peakValue(precise, frame, true));
            }
            QVERIFY2(std::abs(peakValue(cached, bucket, false)
                                  - expectedMinimum) < 0.001,
                     qPrintable(QStringLiteral("cached minimum mismatch at %1")
                                    .arg(bucket)));
            QVERIFY2(std::abs(peakValue(cached, bucket, true)
                                  - expectedMaximum) < 0.001,
                     qPrintable(QStringLiteral("cached maximum mismatch at %1")
                                    .arg(bucket)));
        }
    }

    void antiphaseStereoWaveformPublishesOneCenteredAmplitudeEnvelope()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString source = temporary.filePath(QStringLiteral("stereo.wav"));
        std::vector<float> samples;
        samples.reserve(2'048);
        for (int frame = 0; frame < 1'024; ++frame) {
            samples.push_back(0.5F);
            samples.push_back(-0.5F);
        }
        QVERIFY(writeStereoFloatWav(source, samples));

        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(openFileAndWait(controller, QUrl::fromLocalFile(source)));
        controller.viewport()->setViewportWidth(512.0);
        const auto ready = [&controller] {
            const QVariantList channels = controller.viewportChannelPeaks();
            return channels.size() == 1
                && channels.front().toList().size() == 2'048;
        };
        QTRY_VERIFY_WITH_TIMEOUT(ready(), 10'000);
        const QVariantList mix = controller.viewportChannelPeaks().front().toList();
        for (qsizetype index = 0; index < mix.size(); index += 2) {
            QVERIFY2(std::abs(mix[index].toDouble() + 0.5) < 0.001,
                     "opposite-polarity channels must not cancel the envelope");
            QVERIFY2(std::abs(mix[index + 1].toDouble() - 0.5) < 0.001,
                     "single waveform must remain centered around zero");
        }
    }

    void cachedWaveformIsBoundedAndKeepsNarrowInteriorEnvelopePoint()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString source = temporary.filePath(QStringLiteral("narrow.wav"));
        constexpr qint64 frames = 480'000;
        QVERIFY(writeMonoFloatWav(source,
            std::vector<float>(static_cast<std::size_t>(frames), 0.5F)));
        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(openFileAndWait(controller, QUrl::fromLocalFile(source)));
        const QString id = controller.timelineEventViews().front().toMap()
            .value(QStringLiteral("id")).toString();
        constexpr qint64 narrow = 250'001;
        QVERIFY(controller.addEnvelopePoint(id, narrow - 1, 1.0));
        QVERIFY(controller.addEnvelopePoint(id, narrow, 2.0));
        QVERIFY(controller.addEnvelopePoint(id, narrow + 1, 1.0));

        QVERIFY(QFile::remove(source));
        QElapsedTimer elapsed;
        elapsed.start();
        controller.viewport()->setViewportWidth(16.0);
        QVERIFY(controller.viewport()->setVisibleRange(0, frames));
        const auto ready = [&controller] {
            const QVariantList channels = controller.viewportChannelPeaks();
            return channels.size() == 1
                && channels.front().toList().size() == 64;
        };
        QTRY_VERIFY_WITH_TIMEOUT(ready(), 10'000);
        QVERIFY2(elapsed.elapsed() < 3'000,
                 "global automated waveform exceeded the bounded cache budget");
        const QVariantList peaks = controller.viewportChannelPeaks()
            .front().toList();
        const qsizetype point = static_cast<qsizetype>(narrow * 32 / frames);
        QVERIFY2(peakValue(peaks, point, true) > 0.99,
                 "cached peak missed a narrow interior envelope maximum");
    }

    void wideMultiSourceProjectUsesBoundedCacheForEverySource()
    {
        using namespace agplayer::editor;
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString firstPath = temporary.filePath(
            QStringLiteral("long-first.wav"));
        const QString secondPath = temporary.filePath(
            QStringLiteral("long-second.wav"));
        const QString projectPath = temporary.filePath(
            QStringLiteral("multi-source.agproj"));
        constexpr SampleFrame frames = 240'000;
        QVERIFY(writeMonoFloatWav(firstPath,
            std::vector<float>(static_cast<std::size_t>(frames), 0.25F)));
        QVERIFY(writeMonoFloatWav(secondPath,
            std::vector<float>(static_cast<std::size_t>(frames), 0.75F)));
        const AudioFileAnalysis first = AudioFileAnalyzer::analyze(
            std::filesystem::path(firstPath.toStdWString()), 64);
        const AudioFileAnalysis second = AudioFileAnalyzer::analyze(
            std::filesystem::path(secondPath.toStdWString()), 64);
        QVERIFY(first.success);
        QVERIFY(second.success);
        const auto firstSource = std::make_shared<const AudioSource>(first.source);
        const auto secondSource = std::make_shared<const AudioSource>(second.source);
        const AudioEvent firstEvent{1, firstSource, 0, frames, 0};
        const AudioEvent secondEvent{2, secondSource, 0, frames, frames};
        AudioDocument document = AudioDocument::fromEvents(
            {firstEvent, secondEvent});
        ProjectSaveRequest save;
        save.document = &document;
        save.visibleEndFrame = document.totalFrames();
        const ProjectSaveResult saved = ProjectDocument::save(projectPath, save);
        QVERIFY2(saved.ok(), qPrintable(saved.message));

        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        controller.viewport()->setViewportWidth(16.0);
        QVERIFY2(openProjectAndWait(controller, QUrl::fromLocalFile(projectPath)),
                 qPrintable(controller.errorMessage()));
        QTRY_VERIFY_WITH_TIMEOUT(
            !controller.sourcePeakCacheActiveForTesting(), 10'000);
        const auto ready = [](const AudioEditorController& candidate) {
            const QVariantList channels = candidate.viewportChannelPeaks();
            return channels.size() == 1
                && channels.front().toList().size() == 64;
        };
        QTRY_VERIFY_WITH_TIMEOUT(ready(controller), 10'000);
        const QVariantList firstOpenPeaks = controller.viewportChannelPeaks()
            .front().toList();
        QVERIFY(std::abs(peakValue(firstOpenPeaks, 8, true) - 0.25) < 0.01);
        QVERIFY(std::abs(peakValue(firstOpenPeaks, 24, true) - 0.75) < 0.01);

        std::atomic_int cacheWorkerStarts{0};
        AudioEditorController cachedController(AG_AUDIO_BACKEND_NULL);
        cachedController.viewport()->setViewportWidth(16.0);
        cachedController.setSourcePeakCacheTaskObserverForTesting(
            [&](const bool starting) {
                if (starting) ++cacheWorkerStarts;
            });
        QElapsedTimer elapsed;
        elapsed.start();
        QVERIFY2(openProjectAndWait(cachedController, QUrl::fromLocalFile(projectPath)),
                 qPrintable(cachedController.errorMessage()));
        QTRY_VERIFY_WITH_TIMEOUT(ready(cachedController), 10'000);
        QVERIFY2(elapsed.elapsed() < 3'000,
                 "multi-source weak-cache reopen exceeded cache budget");
        QCOMPARE(cacheWorkerStarts.load(), 0);

        QVERIFY(QFile::remove(firstPath));
        QVERIFY(QFile::remove(secondPath));
        const quint64 generation = cachedController.viewportWaveformGeneration();
        cachedController.viewport()->setViewportWidth(17.0);
        QTRY_VERIFY_WITH_TIMEOUT(
            cachedController.viewportWaveformGeneration() > generation, 5'000);
        QTRY_VERIFY_WITH_TIMEOUT(ready(cachedController), 10'000);
        const QVariantList peaks = cachedController.viewportChannelPeaks()
            .front().toList();
        QVERIFY(std::abs(peakValue(peaks, 8, true) - 0.25) < 0.01);
        QVERIFY(std::abs(peakValue(peaks, 24, true) - 0.75) < 0.01);
    }

    void uncachedProjectKeepsWaveformEmptyUntilSourceAnalysisCompletes()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString project = saveConstantProject(
            temporary.path(), QStringLiteral("nonblocking"), {0.25F, 0.75F});
        QVERIFY(!project.isEmpty());
        const QString previous = temporary.filePath(QStringLiteral("previous.wav"));
        QVERIFY(writeMonoFloatWav(previous, std::vector<float>(1'024, 0.5F)));

        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(openFileAndWait(controller, QUrl::fromLocalFile(previous)));
        controller.viewport()->setViewportWidth(16.0);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.viewportChannelPeaks().isEmpty(),
                                 10'000);
        const QVariantList lastGood = controller.viewportChannelPeaks();
        QSemaphore started;
        QSemaphore release;
        const auto unblock = qScopeGuard([&] { release.release(); });
        controller.setSourcePeakCacheTaskObserverForTesting(
            [&](const bool starting) {
                if (starting) {
                    started.release();
                    release.acquire();
                }
            });

        QElapsedTimer elapsed;
        elapsed.start();
        QVERIFY2(openProjectAndWait(controller, QUrl::fromLocalFile(project)),
                 qPrintable(controller.errorMessage()));
        QVERIFY2(elapsed.elapsed() < 500,
                 "openProject blocked the UI on full source analysis");
        QVERIFY(started.tryAcquire(1, 5'000));
        QVERIFY(controller.sourcePeakCacheActiveForTesting());
        QVERIFY(!lastGood.isEmpty());
        QVERIFY(controller.viewportChannelPeaks().isEmpty());
        release.release();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.sourcePeakCacheActiveForTesting(),
                                 10'000);
        const auto ready = [&controller] {
            const QVariantList channels = controller.viewportChannelPeaks();
            return channels.size() == 1
                && channels.front().toList().size() == 64;
        };
        QTRY_VERIFY_WITH_TIMEOUT(ready(), 10'000);
    }

    void obsoleteProjectSourceAnalysisCannotPublishIntoNewProject()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString first = saveConstantProject(
            temporary.path(), QStringLiteral("stale-first"), {0.25F},
            2'000'000);
        const QString second = saveConstantProject(
            temporary.path(), QStringLiteral("stale-second"), {0.75F},
            2'000'000);
        QVERIFY(!first.isEmpty());
        QVERIFY(!second.isEmpty());

        QSemaphore firstStarted;
        std::atomic_int starts{0};
        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        controller.viewport()->setViewportWidth(16.0);
        controller.setSourcePeakCacheTaskObserverForTesting(
            [&](const bool starting) {
                if (!starting) return;
                const int sequence = ++starts;
                if (sequence == 1) {
                    firstStarted.release();
                }
            });

        QVERIFY(openProjectAndWait(controller, QUrl::fromLocalFile(first)));
        QVERIFY(firstStarted.tryAcquire(1, 5'000));
        const quint64 firstGeneration =
            controller.sourcePeakCacheGenerationForTesting();
        QVERIFY(openProjectAndWait(controller, QUrl::fromLocalFile(second)));
        QVERIFY(controller.sourcePeakCacheGenerationForTesting()
                > firstGeneration);
        QTRY_COMPARE_WITH_TIMEOUT(starts.load(), 2, 5'000);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.sourcePeakCacheActiveForTesting(),
                                 10'000);
        const auto newestPublished = [&controller] {
            const QVariantList channels = controller.viewportChannelPeaks();
            if (channels.size() != 1) return false;
            const QVariantList peaks = channels.front().toList();
            return peaks.size() == 64
                && std::abs(peakValue(peaks, 16, true) - 0.75) < 0.01;
        };
        QTRY_VERIFY_WITH_TIMEOUT(newestPublished(), 10'000);
    }

    void deactivationAndDocumentCloseCancelSourceAnalysis()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString deactivationProject = saveConstantProject(
            temporary.path(), QStringLiteral("cancel-deactivate"),
            {0.4F, 0.6F});
        const QString closeProject = saveConstantProject(
            temporary.path(), QStringLiteral("cancel-close"), {0.3F, 0.7F});
        QVERIFY(!deactivationProject.isEmpty());
        QVERIFY(!closeProject.isEmpty());

        {
            QSemaphore started;
            QSemaphore release;
            std::atomic_int finishes{0};
            AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
            const auto unblock = qScopeGuard([&] { release.release(); });
            controller.setSourcePeakCacheTaskObserverForTesting(
                [&](const bool starting) {
                    if (starting) {
                        started.release();
                        release.acquire();
                    } else {
                        ++finishes;
                    }
                });
            QVERIFY(openProjectAndWait(controller,
                QUrl::fromLocalFile(deactivationProject)));
            QVERIFY(started.tryAcquire(1, 5'000));
            controller.deactivate();
            QVERIFY(!controller.sourcePeakCacheActiveForTesting());
            release.release();
            QTRY_COMPARE_WITH_TIMEOUT(finishes.load(), 1, 10'000);
        }

        {
            QSemaphore started;
            QSemaphore release;
            std::atomic_int finishes{0};
            AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
            const auto unblock = qScopeGuard([&] { release.release(); });
            controller.setSourcePeakCacheTaskObserverForTesting(
                [&](const bool starting) {
                    if (starting) {
                        started.release();
                        release.acquire();
                    } else {
                        ++finishes;
                    }
                });
            QVERIFY(openProjectAndWait(controller, QUrl::fromLocalFile(closeProject)));
            QVERIFY(started.tryAcquire(1, 5'000));
            QVERIFY(controller.clearDocument());
            QVERIFY(!controller.sourcePeakCacheActiveForTesting());
            release.release();
            QTRY_COMPARE_WITH_TIMEOUT(finishes.load(), 1, 10'000);
            QVERIFY(!controller.hasDocument());
        }
    }

    void openFileCompletionWaitsForSuccessAndRunsOnlyOnce()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString first = temporary.filePath(QStringLiteral("first.wav"));
        const QString second = temporary.filePath(QStringLiteral("second.wav"));
        QVERIFY(writeMonoFloatWav(first, std::vector<float>(8'000, 0.25F)));
        QVERIFY(writeMonoFloatWav(second, std::vector<float>(4'000, 0.75F)));

        QSemaphore started;
        QSemaphore release;
        const auto unblock = qScopeGuard([&] { release.release(); });
        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        controller.setDocumentLoadTaskObserverForTesting(
            [&](const bool starting) {
                if (starting) {
                    started.release();
                    release.acquire();
                }
            });

        int completions = 0;
        bool observedLoading = true;
        qint64 observedFrames = 0;
        int observedRate = 0;
        QVERIFY(controller.openFileWhenReady(
            QUrl::fromLocalFile(first), &controller, [&] {
                ++completions;
                observedLoading = controller.loading();
                observedFrames = controller.totalFrames();
                observedRate = controller.sampleRate();
                QVERIFY(controller.setSelection(observedFrames / 4,
                                                observedFrames / 2));
                QVERIFY(controller.seekFrame(observedFrames / 3));
            }));
        QVERIFY(started.tryAcquire(1, 5'000));
        QCOMPARE(completions, 0);
        QVERIFY(controller.loading());

        release.release();
        QTRY_COMPARE_WITH_TIMEOUT(completions, 1, 10'000);
        QVERIFY(!observedLoading);
        QCOMPARE(observedFrames, qint64{8'000});
        QCOMPARE(observedRate, 8'000);
        QCOMPARE(controller.selectionStart(), qint64{2'000});
        QCOMPARE(controller.selectionEnd(), qint64{4'000});
        QCOMPARE(controller.playheadFrame(), qint64{2'666});

        controller.setDocumentLoadTaskObserverForTesting({});
        QVERIFY(!controller.openFile(QUrl::fromLocalFile(second)));
        QVERIFY(confirmDiscardAndWait(controller));
        QCOMPARE(completions, 1);
        QCOMPARE(controller.filePath(), second);
        QCOMPARE(controller.selectionStart(), qint64{-1});
        QCOMPARE(controller.selectionEnd(), qint64{-1});
    }

    void openFileCompletionIgnoresSupersededGeneration()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString obsolete = temporary.filePath(
            QStringLiteral("obsolete.wav"));
        const QString newest = temporary.filePath(QStringLiteral("newest.wav"));
        QVERIFY(writeMonoFloatWav(obsolete,
            std::vector<float>(8'000, 0.25F)));
        QVERIFY(writeMonoFloatWav(newest,
            std::vector<float>(12'000, 0.75F)));

        QSemaphore firstStarted;
        QSemaphore releaseFirst;
        std::atomic_int starts{0};
        const auto unblock = qScopeGuard([&] { releaseFirst.release(); });
        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        controller.setDocumentLoadTaskObserverForTesting(
            [&](const bool starting) {
                if (starting && ++starts == 1) {
                    firstStarted.release();
                    releaseFirst.acquire();
                }
            });

        int obsoleteCompletions = 0;
        QVERIFY(controller.openFileWhenReady(
            QUrl::fromLocalFile(obsolete), &controller,
            [&] { ++obsoleteCompletions; }));
        QVERIFY(firstStarted.tryAcquire(1, 5'000));
        QVERIFY(controller.openFile(QUrl::fromLocalFile(newest)));
        releaseFirst.release();
        QVERIFY(waitForDocumentLoad(controller));
        QCOMPARE(controller.filePath(), newest);
        QCOMPARE(controller.totalFrames(), qint64{12'000});
        QCOMPARE(obsoleteCompletions, 0);
    }

    void openFileCompletionDoesNotRunForFailure()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString invalid = temporary.filePath(QStringLiteral("invalid.wav"));
        QFile file(invalid);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.write("not audio") > 0);
        file.close();

        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        int completions = 0;
        QVERIFY(controller.openFileWhenReady(
            QUrl::fromLocalFile(invalid), &controller,
            [&] { ++completions; }));
        QVERIFY(waitForDocumentLoad(controller));
        QCOMPARE(controller.state(), EditorSessionState::Error);
        QVERIFY(!controller.errorMessage().isEmpty());
        QCOMPARE(completions, 0);
    }

    void openFileReturnsBeforeSlowAnalysisCompletes()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString oldPath = temporary.filePath(QStringLiteral("old.wav"));
        const QString nextPath = temporary.filePath(QStringLiteral("next.wav"));
        QVERIFY(writeMonoFloatWav(oldPath, std::vector<float>(4'096, 0.25F)));
        QVERIFY(writeMonoFloatWav(nextPath, std::vector<float>(240'000, 0.75F)));

        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(openFileAndWait(controller, QUrl::fromLocalFile(oldPath)));
        QVERIFY(waitForDocumentLoad(controller));
        controller.viewport()->setViewportWidth(16.0);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.viewportChannelPeaks().isEmpty(),
                                 10'000);
        const QVariantList oldPeaks = controller.viewportChannelPeaks();
        QSemaphore started;
        QSemaphore release;
        const auto unblock = qScopeGuard([&] { release.release(); });
        controller.setDocumentLoadTaskObserverForTesting(
            [&](const bool starting) {
                if (starting) {
                    started.release();
                    release.acquire();
                }
            });

        QElapsedTimer elapsed;
        elapsed.start();
        QVERIFY(controller.openFile(QUrl::fromLocalFile(nextPath)));
        QVERIFY2(elapsed.elapsed() < 500,
                 "openFile blocked the UI on full audio analysis");
        QVERIFY(started.tryAcquire(1, 5'000));
        QVERIFY(controller.loading());
        QCOMPARE(controller.filePath(), oldPath);
        QCOMPARE(controller.viewportChannelPeaks(), oldPeaks);
        release.release();
        QVERIFY(waitForDocumentLoad(controller));
        QCOMPARE(controller.filePath(), nextPath);
    }

    void openProjectReturnsBeforeSlowProbeCompletes()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString oldPath = temporary.filePath(QStringLiteral("old.wav"));
        QVERIFY(writeMonoFloatWav(oldPath, std::vector<float>(4'096, 0.25F)));
        const QString project = saveConstantProject(
            temporary.path(), QStringLiteral("slow-project"), {0.75F});
        QVERIFY(!project.isEmpty());

        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(openFileAndWait(controller, QUrl::fromLocalFile(oldPath)));
        QVERIFY(waitForDocumentLoad(controller));
        QSemaphore started;
        QSemaphore release;
        const auto unblock = qScopeGuard([&] { release.release(); });
        controller.setDocumentLoadTaskObserverForTesting(
            [&](const bool starting) {
                if (starting) {
                    started.release();
                    release.acquire();
                }
            });

        QElapsedTimer elapsed;
        elapsed.start();
        QVERIFY(controller.openProject(QUrl::fromLocalFile(project)));
        QVERIFY2(elapsed.elapsed() < 500,
                 "openProject blocked the UI on project source probes");
        QVERIFY(started.tryAcquire(1, 5'000));
        QVERIFY(controller.loading());
        QCOMPARE(controller.filePath(), oldPath);
        release.release();
        QVERIFY(waitForDocumentLoad(controller));
        QCOMPARE(controller.projectPath(), QFileInfo(project).absoluteFilePath());
    }

    void relinkReturnsBeforeSlowAnalysisCompletes()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        constexpr qint64 frames = 240'000;
        const QString project = saveConstantProject(
            temporary.path(), QStringLiteral("slow-relink"), {0.25F}, frames);
        const QString replacement = temporary.filePath(
            QStringLiteral("replacement.wav"));
        QVERIFY(!project.isEmpty());
        QVERIFY(writeMonoFloatWav(replacement,
            std::vector<float>(static_cast<std::size_t>(frames), 0.75F)));

        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(openProjectAndWait(controller, QUrl::fromLocalFile(project)));
        QVERIFY(waitForDocumentLoad(controller));
        const QString original = controller.filePath();
        QSemaphore started;
        QSemaphore release;
        const auto unblock = qScopeGuard([&] { release.release(); });
        controller.setDocumentLoadTaskObserverForTesting(
            [&](const bool starting) {
                if (starting) {
                    started.release();
                    release.acquire();
                }
            });

        QElapsedTimer elapsed;
        elapsed.start();
        QVERIFY(controller.relinkProjectSource(
            quint64{1}, QUrl::fromLocalFile(replacement)));
        QVERIFY2(elapsed.elapsed() < 500,
                 "relinkProjectSource blocked the UI on replacement analysis");
        QVERIFY(started.tryAcquire(1, 5'000));
        QVERIFY(controller.loading());
        QCOMPARE(controller.filePath(), original);
        release.release();
        QVERIFY(waitForDocumentLoad(controller));
        QCOMPARE(controller.filePath(), replacement);
    }

    void deactivationAndClearCancelDocumentLoads()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString first = temporary.filePath(QStringLiteral("first.wav"));
        const QString second = temporary.filePath(QStringLiteral("second.wav"));
        QVERIFY(writeMonoFloatWav(first, std::vector<float>(240'000, 0.25F)));
        QVERIFY(writeMonoFloatWav(second, std::vector<float>(240'000, 0.75F)));

        {
            QSemaphore started;
            QSemaphore release;
            std::atomic_int finishes{0};
            AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
            const auto unblock = qScopeGuard([&] { release.release(); });
            controller.setDocumentLoadTaskObserverForTesting(
                [&](const bool starting) {
                    if (starting) {
                        started.release();
                        release.acquire();
                    } else {
                        ++finishes;
                    }
                });
            QVERIFY(controller.openFile(QUrl::fromLocalFile(first)));
            QVERIFY(started.tryAcquire(1, 5'000));
            controller.deactivate();
            QVERIFY(!controller.loading());
            release.release();
            QTRY_COMPARE_WITH_TIMEOUT(finishes.load(), 1, 10'000);
            QVERIFY(!controller.hasDocument());
        }

        {
            QSemaphore started;
            QSemaphore release;
            std::atomic_int finishes{0};
            AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
            const auto unblock = qScopeGuard([&] { release.release(); });
            controller.setDocumentLoadTaskObserverForTesting(
                [&](const bool starting) {
                    if (starting) {
                        started.release();
                        release.acquire();
                    } else {
                        ++finishes;
                    }
                });
            QVERIFY(controller.openFile(QUrl::fromLocalFile(second)));
            QVERIFY(started.tryAcquire(1, 5'000));
            QVERIFY(controller.clearDocument());
            QVERIFY(!controller.loading());
            release.release();
            QTRY_COMPARE_WITH_TIMEOUT(finishes.load(), 1, 10'000);
            QVERIFY(!controller.hasDocument());
        }
    }

    void newestDocumentLoadWinsAcrossFileAndProjectRequests()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString obsolete = temporary.filePath(
            QStringLiteral("obsolete.wav"));
        QVERIFY(writeMonoFloatWav(obsolete,
            std::vector<float>(240'000, 0.25F)));
        const QString newest = saveConstantProject(
            temporary.path(), QStringLiteral("newest"), {0.75F});
        QVERIFY(!newest.isEmpty());

        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QSemaphore firstStarted;
        QSemaphore releaseFirst;
        std::atomic_int starts{0};
        const auto unblock = qScopeGuard([&] { releaseFirst.release(); });
        controller.setDocumentLoadTaskObserverForTesting(
            [&](const bool starting) {
                if (starting && ++starts == 1) {
                    firstStarted.release();
                    releaseFirst.acquire();
                }
            });
        QVERIFY(controller.openFile(QUrl::fromLocalFile(obsolete)));
        QVERIFY(firstStarted.tryAcquire(1, 5'000));
        const quint64 obsoleteGeneration =
            controller.documentLoadGenerationForTesting();
        QVERIFY(controller.loading());
        QVERIFY(!controller.modified());
        QVERIFY2(controller.openProject(QUrl::fromLocalFile(newest)),
                 qPrintable(QStringLiteral("state=%1 loading=%2 error=%3")
                     .arg(static_cast<int>(controller.state()))
                     .arg(controller.loading())
                     .arg(controller.errorMessage())));
        QVERIFY(controller.documentLoadGenerationForTesting()
                > obsoleteGeneration);
        releaseFirst.release();
        QVERIFY(waitForDocumentLoad(controller));
        QCOMPARE(starts.load(), 2);
        QCOMPARE(controller.projectPath(), QFileInfo(newest).absoluteFilePath());
        QVERIFY(controller.filePath().endsWith(QStringLiteral("newest-source-0.wav")));
    }

    void obsoleteRelinkCannotOverwriteNewFileOpen()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        constexpr qint64 frames = 240'000;
        const QString project = saveConstantProject(
            temporary.path(), QStringLiteral("relink-race"), {0.25F}, frames);
        const QString replacement = temporary.filePath(
            QStringLiteral("replacement.wav"));
        const QString newest = temporary.filePath(QStringLiteral("newest.wav"));
        QVERIFY(writeMonoFloatWav(replacement,
            std::vector<float>(static_cast<std::size_t>(frames), 0.5F)));
        QVERIFY(writeMonoFloatWav(newest,
            std::vector<float>(static_cast<std::size_t>(frames), 0.75F)));
        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(openProjectAndWait(controller, QUrl::fromLocalFile(project)));
        QVERIFY(waitForDocumentLoad(controller));

        QSemaphore firstStarted;
        QSemaphore releaseFirst;
        std::atomic_int starts{0};
        const auto unblock = qScopeGuard([&] { releaseFirst.release(); });
        controller.setDocumentLoadTaskObserverForTesting(
            [&](const bool starting) {
                if (starting && ++starts == 1) {
                    firstStarted.release();
                    releaseFirst.acquire();
                }
            });
        QVERIFY(controller.relinkProjectSource(
            quint64{1}, QUrl::fromLocalFile(replacement)));
        QVERIFY(firstStarted.tryAcquire(1, 5'000));
        QVERIFY(controller.openFile(QUrl::fromLocalFile(newest)));
        releaseFirst.release();
        QVERIFY(waitForDocumentLoad(controller));
        QCOMPARE(starts.load(), 2);
        QCOMPARE(controller.filePath(), newest);
        QVERIFY(controller.projectPath().isEmpty());
    }

    void failedAsyncLoadsPreserveDocumentAndWaveform()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString oldPath = temporary.filePath(QStringLiteral("old.wav"));
        const QString invalidAudio = temporary.filePath(
            QStringLiteral("invalid.wav"));
        const QString invalidProject = temporary.filePath(
            QStringLiteral("invalid.agproj"));
        const QString wrongReplacement = temporary.filePath(
            QStringLiteral("wrong.wav"));
        QVERIFY(writeMonoFloatWav(oldPath, std::vector<float>(4'096, 0.5F)));
        QFile invalidAudioFile(invalidAudio);
        QVERIFY(invalidAudioFile.open(QIODevice::WriteOnly));
        QVERIFY(invalidAudioFile.write("not audio") > 0);
        invalidAudioFile.close();
        QFile invalidProjectFile(invalidProject);
        QVERIFY(invalidProjectFile.open(QIODevice::WriteOnly));
        QVERIFY(invalidProjectFile.write("not json") > 0);
        invalidProjectFile.close();
        QVERIFY(writeMonoFloatWav(wrongReplacement,
            std::vector<float>(2'048, 0.25F)));

        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(openFileAndWait(controller, QUrl::fromLocalFile(oldPath)));
        QVERIFY(waitForDocumentLoad(controller));
        controller.viewport()->setViewportWidth(16.0);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.viewportChannelPeaks().isEmpty(),
                                 10'000);
        const QString oldDocumentPath = controller.filePath();
        const qint64 oldFrames = controller.totalFrames();
        const QVariantList oldPeaks = controller.viewportChannelPeaks();

        QVERIFY(controller.openFile(QUrl::fromLocalFile(invalidAudio)));
        QVERIFY(waitForDocumentLoad(controller));
        QCOMPARE(controller.state(), EditorSessionState::Error);
        QVERIFY(!controller.errorMessage().isEmpty());
        QCOMPARE(controller.filePath(), oldDocumentPath);
        QCOMPARE(controller.totalFrames(), oldFrames);
        QCOMPARE(controller.viewportChannelPeaks(), oldPeaks);

        QVERIFY(controller.openProject(QUrl::fromLocalFile(invalidProject)));
        QVERIFY(waitForDocumentLoad(controller));
        QVERIFY(!controller.errorMessage().isEmpty());
        QCOMPARE(controller.filePath(), oldDocumentPath);
        QCOMPARE(controller.totalFrames(), oldFrames);
        QCOMPARE(controller.viewportChannelPeaks(), oldPeaks);

        QVERIFY(controller.relinkProjectSource(
            quint64{1}, QUrl::fromLocalFile(wrongReplacement)));
        QVERIFY(waitForDocumentLoad(controller));
        QVERIFY(!controller.errorMessage().isEmpty());
        QCOMPARE(controller.filePath(), oldDocumentPath);
        QCOMPARE(controller.totalFrames(), oldFrames);
        QCOMPARE(controller.viewportChannelPeaks(), oldPeaks);
    }

    void successfulProjectReplacementClearsOldWaveformUntilCachePublishes()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString oldPath = temporary.filePath(QStringLiteral("old.wav"));
        QVERIFY(writeMonoFloatWav(oldPath, std::vector<float>(4'096, 0.25F)));
        const QString project = saveConstantProject(
            temporary.path(), QStringLiteral("replacement"), {0.75F});
        QVERIFY(!project.isEmpty());

        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(openFileAndWait(controller, QUrl::fromLocalFile(oldPath)));
        QVERIFY(waitForDocumentLoad(controller));
        controller.viewport()->setViewportWidth(16.0);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.viewportChannelPeaks().isEmpty(),
                                 10'000);
        QSemaphore cacheStarted;
        QSemaphore releaseCache;
        const auto unblock = qScopeGuard([&] { releaseCache.release(); });
        controller.setSourcePeakCacheTaskObserverForTesting(
            [&](const bool starting) {
                if (starting) {
                    cacheStarted.release();
                    releaseCache.acquire();
                }
            });
        bool sawReplacementWithEmptyWaveform = false;
        connect(&controller, &AudioEditorController::documentChanged,
                &controller, [&] {
            if (controller.projectPath()
                    == QFileInfo(project).absoluteFilePath()
                && controller.viewportChannelPeaks().isEmpty()) {
                sawReplacementWithEmptyWaveform = true;
            }
        });

        QVERIFY(controller.openProject(QUrl::fromLocalFile(project)));
        QVERIFY(waitForDocumentLoad(controller));
        QVERIFY(cacheStarted.tryAcquire(1, 5'000));
        QVERIFY(sawReplacementWithEmptyWaveform);
        QVERIFY(controller.viewportChannelPeaks().isEmpty());
        releaseCache.release();
        QTRY_VERIFY_WITH_TIMEOUT(
            !controller.sourcePeakCacheActiveForTesting(), 10'000);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.viewportChannelPeaks().isEmpty(),
                                 10'000);
    }

    void deepZoomPublishesActualDecodedSamplesWithoutCoarseRepetition()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString source = temporary.filePath(QStringLiteral("ramp.wav"));
        std::vector<float> samples(64);
        for (std::size_t index = 0; index < samples.size(); ++index) {
            samples[index] = static_cast<float>(index) / 64.0F - 0.5F;
        }
        QVERIFY(writeMonoFloatWav(source, samples));
        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(openFileAndWait(controller, QUrl::fromLocalFile(source)));
        controller.viewport()->setViewportWidth(128.0);
        QVERIFY(controller.viewport()->setVisibleRange(10, 18));
        const auto ready = [&controller] {
            const QVariantList channels = controller.viewportChannelPeaks();
            return channels.size() == 1
                && channels.front().toList().size() == 16;
        };
        QTRY_VERIFY_WITH_TIMEOUT(ready(), 10'000);
        const QVariantList precise = controller.viewportChannelPeaks()
            .front().toList();
        for (qsizetype point = 0; point < 8; ++point) {
            const double expected = std::abs(
                samples[static_cast<std::size_t>(point + 10)]);
            QVERIFY(std::abs(peakValue(precise, point, false) + expected)
                    < 0.0001);
            QVERIFY(std::abs(peakValue(precise, point, true) - expected)
                    < 0.0001);
        }
    }

    void deepZoomStartsAtExactNonMillisecondSourceFrame()
    {
        for (const std::uint32_t sampleRate : {44'100U, 48'000U}) {
            QTemporaryDir temporary;
            QVERIFY(temporary.isValid());
            const QString source = temporary.filePath(
                QStringLiteral("awkward-%1.wav").arg(sampleRate));
            std::vector<float> samples(256);
            for (std::size_t index = 0; index < samples.size(); ++index) {
                samples[index] = static_cast<float>(index) / 512.0F;
            }
            QVERIFY(writeMonoFloatWav(source, samples, sampleRate));
            AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
            QVERIFY(openFileAndWait(controller, QUrl::fromLocalFile(source)));
            const QString id = controller.timelineEventViews().front().toMap()
                .value(QStringLiteral("id")).toString();
            constexpr qint64 sourceStart = 25;
            QVERIFY(controller.trimEvent(id, sourceStart, 200, 0));
            controller.viewport()->setViewportWidth(128.0);
            QVERIFY(controller.viewport()->setVisibleRange(0, 8));
            const auto ready = [&controller] {
                const QVariantList channels = controller.viewportChannelPeaks();
                return channels.size() == 1
                    && channels.front().toList().size() == 16;
            };
            QTRY_VERIFY_WITH_TIMEOUT(ready(), 10'000);
            const QVariantList precise = controller.viewportChannelPeaks()
                .front().toList();
            for (qsizetype point = 0; point < 8; ++point) {
                const double expected = std::abs(samples[static_cast<std::size_t>(
                    sourceStart + point)]);
                QVERIFY2(std::abs(peakValue(precise, point, false) + expected)
                             < 0.0001,
                         qPrintable(QStringLiteral(
                             "sample rate %1 point %2 was not sample-exact")
                             .arg(sampleRate).arg(point)));
                QVERIFY(std::abs(peakValue(precise, point, true) - expected)
                        < 0.0001);
            }
        }
    }

    void metadataEditKeepsFixturePeaksAndDefersViewportDecode()
    {
        const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_EDITOR_FIXTURE"));
        if (fixture.isEmpty()) QSKIP("fixture not configured");
        const QDir fixtureDirectory = QFileInfo(fixture).dir();
        const QStringList filesBefore = fixtureDirectory.entryList(
            QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY2(openFileAndWait(controller, QUrl::fromLocalFile(fixture)),
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
        QVERIFY(waveformChanges.count() >= 1);
        QVERIFY(!controller.busy());
        QCOMPARE(controller.state(), EditorSessionState::Ready);
        QCOMPARE(stateChanges.count(), 0);
    }

    void visibleTimelineWaveformKeepsGapsBlankAndLatestRequestWins()
    {
        const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_EDITOR_FIXTURE"));
        if (fixture.isEmpty()) QSKIP("fixture not configured");
        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY2(openFileAndWait(controller, QUrl::fromLocalFile(fixture)),
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
        QVERIFY(openFileAndWait(controller, QUrl::fromLocalFile(fixture)));
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
        QVERIFY(openFileAndWait(controller, QUrl::fromLocalFile(fixture)));
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
        QVERIFY(openFileAndWait(controller, QUrl::fromLocalFile(source)));
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
