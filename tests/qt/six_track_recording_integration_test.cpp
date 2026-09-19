#include "audio_editor/audio_editor_controller.hpp"
#include "audio_editor/project_document.hpp"
#include "decoder.hpp"

#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>
#include <atomic>
#include <memory>
#include <vector>

using namespace agplayer::editor;

namespace {
struct CaptureFixture {
    IEditorCaptureBackend::Capture callback{};
    void* user{};
    int channels{};
    std::atomic_bool started{};
    bool reject{};
    void push(int frames, float sample) {
        if (!started.load(std::memory_order_acquire)) return;
        std::vector<float> pcm(static_cast<std::size_t>(frames * channels), sample);
        callback(user, pcm.data(), static_cast<std::size_t>(frames));
    }
};
class FixtureInput final : public IEditorCaptureBackend {
public:
    explicit FixtureInput(std::shared_ptr<CaptureFixture> fixture) : fixture_(std::move(fixture)) {}
    std::vector<EditorInputDevice> devices(QString&) override { return {{"fixture", "Injected PCM input"}}; }
    bool open(const QString&, int, int channels, Capture capture, void* user,
              QString& actual, QString& error) override {
        if (fixture_->reject) { error = "Input access denied by test device"; return false; }
        fixture_->channels = channels;
        fixture_->callback = capture;
        fixture_->user = user;
        actual = "Injected PCM input";
        return true;
    }
    bool start(QString&) override { fixture_->started.store(true, std::memory_order_release); return true; }
    void stop() noexcept override { fixture_->started.store(false, std::memory_order_release); }
    QString failure() const override { return {}; }
private:
    std::shared_ptr<CaptureFixture> fixture_;
};
}

class SixTrackRecordingIntegrationTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }
    void overwriteMiddlePreservesSidesOtherTrackAndOneUndo() {
        auto input = std::make_shared<CaptureFixture>();
        AudioEditorController editor(AG_AUDIO_BACKEND_NULL);
        QVERIFY(editor.recorder()->setCaptureFactoryForTesting([input] { return std::make_unique<FixtureInput>(input); }));
        QVERIFY(editor.createUntitledDocument(48000, 2, 4800));
        editor.selectEvent("1");
        QVERIFY(editor.triggerAction("editor.copy"));
        editor.setSelectedTrack(1);
        QVERIFY(editor.triggerAction("editor.paste"));
        editor.setSelectedTrack(0);
        QVERIFY(editor.seekFrame(1200));
        QVERIFY(editor.startRecording());
        QVERIFY(editor.viewport()->visibleFrameCount() <= editor.sampleRate() * 10LL);
        QTRY_VERIFY(input->started.load());
        input->push(960, 0.25F);
        QTRY_COMPARE(editor.recorder()->recordedFrames(), 960);
        QTRY_COMPARE(editor.playheadFrame(), 2160);
        const auto peaks = editor.recorder()->waveformPeaks(0, 960, 10);
        QCOMPARE(peaks.size(), 1);
        QVERIFY(peaks.front().toList()[1].toFloat() > 0.24F);
        editor.pauseResumeRecording();
        input->push(480, 0.9F);
        QCOMPARE(editor.recorder()->recordedFrames(), 960);
        editor.stopRecording();
        QTRY_VERIFY_WITH_TIMEOUT(!editor.busy(), 10000);
        QCOMPARE(editor.timelineEventViews().size(), 4);
        QCOMPARE(editor.totalFrames(), 4800);
        bool left = false, right = false, other = false, take = false;
        for (const auto& item : editor.timelineEventViews()) {
            const auto clip = item.toMap();
            const auto start = clip.value("timelineStart").toLongLong();
            const auto end = clip.value("timelineEnd").toLongLong();
            if (clip.value("trackIndex").toInt() == 1) other = start == 0 && end == 4800;
            else {
                left |= start == 0 && end == 1200;
                take |= start == 1200 && end == 2160;
                right |= start == 2160 && end == 4800;
            }
        }
        QVERIFY(left && right && other && take);
        QVERIFY(editor.undo());
        QCOMPARE(editor.timelineEventViews().size(), 2);
        QVERIFY(editor.redo());
        QCOMPARE(editor.timelineEventViews().size(), 4);
        QVERIFY(editor.seekFrame(1200));
        QVERIFY(editor.startRecording());
        QTRY_VERIFY(input->started.load());
        input->push(480, 0.5F);
        QTRY_COMPARE(editor.recorder()->recordedFrames(), 480);
        editor.stopRecording();
        QTRY_VERIFY_WITH_TIMEOUT(!editor.busy(), 10000);
        QCOMPARE(editor.timelineEventViews().size(), 5);
        QVERIFY(editor.undo());
        QCOMPARE(editor.timelineEventViews().size(), 4);
    }
    void capturePauseStopUndoAndPortableProject() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        auto input = std::make_shared<CaptureFixture>();
        AudioEditorController editor(AG_AUDIO_BACKEND_NULL);
        QVERIFY(editor.recorder()->setCaptureFactoryForTesting([input] { return std::make_unique<FixtureInput>(input); }));
        editor.setSelectedTrack(2);
        QVERIFY(editor.startRecording());
        QCOMPARE(editor.sampleRate(), 48000);
        QCOMPARE(editor.channels(), 2);
        QTRY_VERIFY_WITH_TIMEOUT(input->started.load(std::memory_order_acquire), 5000);
        input->push(480, 0.125F);
        QTRY_COMPARE_WITH_TIMEOUT(editor.recorder()->recordedFrames(), 480, 5000);
        editor.pauseResumeRecording();
        QCOMPARE(editor.recorder()->state(), EditorRecordingService::Paused);
        input->push(480, 0.9F); // Pausing must not write frames or silence.
        editor.pauseResumeRecording();
        input->push(480, 0.25F);
        QTRY_COMPARE_WITH_TIMEOUT(editor.recorder()->recordedFrames(), 960, 5000);
        editor.stopRecording();
        QTRY_COMPARE_WITH_TIMEOUT(editor.timelineEventViews().size(), 1, 10000);
        QTRY_VERIFY(!editor.loading());
        QCOMPARE(editor.totalFrames(), 960);
        QCOMPARE(editor.timelineEventViews().front().toMap().value("trackIndex").toInt(), 2);
        QVERIFY(editor.undo());
        QCOMPARE(editor.timelineEventViews().size(), 0);
        QVERIFY(editor.redo());
        QCOMPARE(editor.timelineEventViews().size(), 1);
        const QString source = editor.filePath();
        const QString project = directory.filePath("recording.agproj");
        QVERIFY2(editor.saveProject(QUrl::fromLocalFile(project)), qPrintable(editor.errorMessage()));
        auto loaded = ProjectDocument::load(project);
        QVERIFY2(loaded.ok(), qPrintable(loaded.message));
        QVERIFY(loaded.issues.empty());
        QCOMPARE(loaded.document->totalFrames(), 960);
        const auto snapshot = loaded.document->timelineSnapshot();
        QCOMPARE(snapshot.events.front().trackIndex, 2);
        const QString delivered = QString::fromStdWString(snapshot.events.front().source->path.wstring());
        QVERIFY(delivered != source);
        QVERIFY(delivered.startsWith(directory.path()));
        agplayer::Decoder decoder;
        QCOMPARE(decoder.open(snapshot.events.front().source->path.u8string()), AG_OK);
        agplayer::DecodedAudioBlock block;
        QCOMPARE(decoder.read(block), AG_OK);
        QCOMPARE(block.frames, std::size_t(960));
        // Unity (0 dB) is the default; save/reopen must preserve actual input.
        QVERIFY(std::abs(block.samples[0] - 0.125F) < 0.00001F);
        QVERIFY(std::abs(block.samples[960] - 0.25F) < 0.00001F);
        decoder.close();
        QVERIFY(editor.undo());
        QVERIFY(QFileInfo::exists(source)); // Undo/redo history still owns the take.
        QVERIFY(editor.redo());
        QVERIFY(editor.clearDocument());
        QVERIFY(!QFileInfo::exists(source)); // Only the session copy becomes unreferenced.
        QVERIFY(QFileInfo::exists(delivered));
        QVERIFY(ProjectDocument::load(project).issues.empty());
    }
    void closingPageStopsAndCommitsWithoutFakeWaveform() {
        auto input = std::make_shared<CaptureFixture>();
        AudioEditorController editor(AG_AUDIO_BACKEND_NULL);
        QVERIFY(editor.recorder()->setCaptureFactoryForTesting([input] { return std::make_unique<FixtureInput>(input); }));
        QVERIFY(editor.startRecording());
        QTRY_VERIFY(input->started.load(std::memory_order_acquire));
        QCOMPARE(editor.timelineEventViews().size(), 0);
        input->push(480, 0.125F);
        QTRY_COMPARE(editor.recorder()->recordedFrames(), 480);
        editor.deactivate();
        QTRY_COMPARE_WITH_TIMEOUT(editor.timelineEventViews().size(), 1, 10000);
        QVERIFY(!editor.playing());
        QVERIFY(!editor.editorPlaybackOwnsPlayer());
        const QString source = editor.filePath();
        QVERIFY(QFile::remove(source));
    }
    void deniedDeviceDoesNotAddClip() {
        auto input = std::make_shared<CaptureFixture>();
        input->reject = true;
        AudioEditorController editor(AG_AUDIO_BACKEND_NULL);
        QVERIFY(editor.recorder()->setCaptureFactoryForTesting([input] { return std::make_unique<FixtureInput>(input); }));
        QVERIFY(editor.startRecording());
        QTRY_COMPARE_WITH_TIMEOUT(editor.recorder()->state(), EditorRecordingService::Error, 5000);
        QVERIFY(!editor.busy());
        QCOMPARE(editor.timelineEventViews().size(), 0);
        QVERIFY(editor.errorMessage().contains("denied"));
        QVERIFY(editor.actionEnabled("editor.open"));
        const QString partial = editor.recorder()->partialRecordingPath();
        if (!partial.isEmpty()) QVERIFY(QFile::remove(partial));
    }
    void unknownBpmParametersReset() {
        AudioEditorController editor(AG_AUDIO_BACKEND_NULL);
        QCOMPARE(editor.originalBpm(), 0.0);
        QVERIFY(editor.setSpeedPercent(150));
        QVERIFY(editor.setPitch(3, 0));
        QVERIFY(editor.resetTimePitch());
        QCOMPARE(editor.speedPercent(), 100.0);
        QCOMPARE(editor.pitchCents(), 0);
    }
    void unknownBpmSpeedSurvivesProjectReopen() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        AudioEditorController editor(AG_AUDIO_BACKEND_NULL);
        const QString fixture = qEnvironmentVariable("AGPLAYER_EDITOR_FIXTURE");
        QVERIFY(QFileInfo::exists(fixture));
        QVERIFY(editor.addFiles({QUrl::fromLocalFile(fixture)}));
        QTRY_VERIFY_WITH_TIMEOUT(!editor.loading(), 10000);
        QCOMPARE(editor.originalBpm(), 0.0);
        QVERIFY(editor.setSpeedPercent(150));
        QVERIFY(editor.setPitch(3, 0));
        const auto project = QUrl::fromLocalFile(directory.filePath("unknown-bpm.agproj"));
        QVERIFY(editor.saveProject(project));
        AudioEditorController reopened(AG_AUDIO_BACKEND_NULL);
        QVERIFY(reopened.openProject(project));
        QTRY_VERIFY_WITH_TIMEOUT(!reopened.loading(), 10000);
        QCOMPARE(reopened.speedPercent(), 150.0);
        QCOMPARE(reopened.pitchCents(), 300);
    }
    void clipGestureSelectionAndTrimPreview() {
        AudioEditorController editor(AG_AUDIO_BACKEND_NULL);
        QVERIFY(editor.createUntitledDocument(48000, 2, 48000));
        editor.selectEvent("1");
        QVERIFY(editor.beginEventGesture("1", "move"));
        QVERIFY(editor.moveEventToTrack("1", 0, 2));
        QVERIFY(editor.endEventGesture());
        QCOMPARE(editor.selectedTrack(), 2);
        QVERIFY(editor.beginEventGesture("1", "trim"));
        QSignalSpy changed(&editor, &AudioEditorController::documentChanged);
        QVERIFY(editor.trimEvent(QStringLiteral("1"), 1200, 48000, 1200));
        QVERIFY(changed.count() > 0);
        QCOMPARE(editor.timelineEventViews().front().toMap().value("timelineStart").toLongLong(), 1200);
        QVERIFY(editor.cancelEventGesture());
        QCOMPARE(editor.timelineEventViews().front().toMap().value("timelineStart").toLongLong(), 0);
    }
    void recordingBlocksExistingMixPlayback() {
        auto input = std::make_shared<CaptureFixture>();
        AudioEditorController editor(AG_AUDIO_BACKEND_NULL);
        QVERIFY(editor.recorder()->setCaptureFactoryForTesting([input] { return std::make_unique<FixtureInput>(input); }));
        const QString fixture = qEnvironmentVariable("AGPLAYER_EDITOR_FIXTURE");
        QVERIFY(QFileInfo::exists(fixture));
        QVERIFY(editor.addFiles({QUrl::fromLocalFile(fixture)}));
        QTRY_VERIFY_WITH_TIMEOUT(!editor.loading(), 10000);
        editor.setSelectedTrack(1);
        QVERIFY(editor.startRecording());
        QTRY_VERIFY(input->started.load(std::memory_order_acquire));
        QVERIFY(!editor.playPause());
        QVERIFY(!editor.playing());
        input->push(480, 0.125F);
        QTRY_COMPARE(editor.recorder()->recordedFrames(), 480);
        editor.stopRecording();
        QTRY_COMPARE_WITH_TIMEOUT(editor.timelineEventViews().size(), 2, 10000);
        QTemporaryDir saved;
        QVERIFY(saved.isValid());
        QVERIFY(editor.saveProject(QUrl::fromLocalFile(saved.filePath("capture.agproj"))));
        QVERIFY(editor.clearDocument());
    }
};
QTEST_GUILESS_MAIN(SixTrackRecordingIntegrationTest)
#include "six_track_recording_integration_test.moc"
