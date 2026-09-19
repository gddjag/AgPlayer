#include "audio_editor/editor_recording_service.hpp"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>

#include <atomic>
#include <limits>
#include <mutex>
#include <vector>

using namespace agplayer::editor;

namespace {
// Only the microphone is substituted. The production ring, writer, state
// machine, file validation and signal delivery are exercised unchanged.
struct InputFixture final {
    std::mutex mutex;
    IEditorCaptureBackend::Capture capture{};
    void* user{};
    bool running{};
    std::atomic_bool disconnected{false};
    bool refuseOpen{};
    void send(const std::vector<float>& samples, int channels = 1)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (running && capture) capture(user, samples.data(), samples.size()
                                       / static_cast<std::size_t>(channels));
    }
};

class FixtureBackend final : public IEditorCaptureBackend {
public:
    explicit FixtureBackend(std::shared_ptr<InputFixture> fixture) : fixture_(std::move(fixture)) {}
    std::vector<EditorInputDevice> devices(QString&) override
    { return {{QStringLiteral("fixture"), QStringLiteral("External microphone")}}; }
    bool open(const QString&, int, int, Capture capture, void* user,
              QString& actual, QString& error) override
    {
        if (fixture_->refuseOpen) { error = QStringLiteral("device unavailable"); return false; }
        std::lock_guard<std::mutex> lock(fixture_->mutex);
        fixture_->capture = capture;
        fixture_->user = user;
        actual = QStringLiteral("External microphone");
        return true;
    }
    bool start(QString&) override
    { std::lock_guard<std::mutex> lock(fixture_->mutex); fixture_->running = true; return true; }
    void stop() noexcept override
    { std::lock_guard<std::mutex> lock(fixture_->mutex); fixture_->running = false; }
    QString failure() const override
    { return fixture_->disconnected.load() ? QStringLiteral("device disconnected") : QString{}; }
private:
    std::shared_ptr<InputFixture> fixture_;
};

EditorCaptureFactory factory(const std::shared_ptr<InputFixture>& fixture)
{ return [fixture] { return std::make_unique<FixtureBackend>(fixture); }; }

QByteArray readFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}
} // namespace

class EditorRecordingServiceTest final : public QObject {
    Q_OBJECT
private slots:
    void autoSelectsDeviceAndBoostsRealPcmAndPeaks()
    {
        QTemporaryDir directory;
        auto fixture = std::make_shared<InputFixture>();
        EditorRecordingService recorder(factory(fixture));
        QTRY_COMPARE(recorder.selectedInputDeviceId(), QStringLiteral("fixture"));
        recorder.setSelectedInputDeviceId(QString{});
        recorder.refreshInputDevices();
        QTest::qWait(100);
        QCOMPARE(recorder.selectedInputDeviceId(), QString{});
        QCOMPARE(recorder.inputGain(), 1.0);
        recorder.setInputGain(4);
        QSignalSpy completed(&recorder, &EditorRecordingService::finished);
        const auto output = directory.filePath(QStringLiteral("gain.wav"));
        QVERIFY(recorder.start(output, 48000, 1));
        QTRY_VERIFY(!recorder.actualInputDeviceName().isEmpty());
        fixture->send({0.0625F, -0.0625F, 0.0F});
        QTRY_COMPARE(recorder.recordedFrames(), 3);
        const auto peaks = recorder.waveformPeaks(0, 3, 1).first().toList();
        QCOMPARE(peaks[0].toFloat(), -0.25F);
        QCOMPARE(peaks[1].toFloat(), 0.25F);
        // Observe timer-driven metering without racing its next zero sample.
        QSignalSpy levels(&recorder, &EditorRecordingService::inputLevelChanged);
        fixture->send({0.0625F});
        QTRY_VERIFY(!levels.empty());
        recorder.pause();
        QCOMPARE(recorder.inputLevel(), 0.0);
        recorder.stop();
        QTRY_COMPARE(completed.count(), 1);
        QCOMPARE(readFile(output).mid(44, 9).toHex(), QByteArray("0000200000e0000000"));
    }
    void pauseExcludesInputAndStopProducesExactPcm24()
    {
        QTemporaryDir directory;
        auto fixture = std::make_shared<InputFixture>();
        EditorRecordingService recorder(factory(fixture));
        recorder.setInputGain(1); // Unity capture preserves the original PCM.
        QSignalSpy completed(&recorder, &EditorRecordingService::finished);
        QSignalSpy failed(&recorder, &EditorRecordingService::failed);
        const QString output = directory.filePath(QStringLiteral("capture.wav"));
        QVERIFY(recorder.start(output, 48'000, 1));
        QTRY_COMPARE(recorder.actualInputDeviceName(), QStringLiteral("External microphone"));
        fixture->send({0.0F, 0.5F, -1.0F});
        QTRY_COMPARE(recorder.recordedFrames(), 3);
        recorder.pause();
        QCOMPARE(recorder.state(), EditorRecordingService::Paused);
        fixture->send({0.75F, 0.75F});
        recorder.resume();
        fixture->send({1.0F, std::numeric_limits<float>::quiet_NaN()});
        recorder.stop();
        QTRY_COMPARE(completed.count(), 1);
        QCOMPARE(failed.count(), 0);
        QCOMPARE(recorder.state(), EditorRecordingService::Idle);
        QCOMPARE(recorder.recordedFrames(), 5);
        const QByteArray wav = readFile(output);
        QCOMPARE(wav.size(), 60); // RIFF pads an odd-sized data chunk.
        QCOMPARE(wav.left(4), QByteArray("RIFF"));
        QCOMPARE(wav.mid(8, 8), QByteArray("WAVEfmt "));
        QCOMPARE(qFromLittleEndian<quint16>(wav.constData() + 20), quint16(1));
        QCOMPARE(qFromLittleEndian<quint16>(wav.constData() + 34), quint16(24));
        QCOMPARE(qFromLittleEndian<quint32>(wav.constData() + 24), quint32(48'000));
        QCOMPARE(qFromLittleEndian<quint32>(wav.constData() + 40), quint32(15));
        QCOMPARE(wav.mid(44, 15).toHex(), QByteArray("000000000040000080ffff7f000000"));
    }

    void refusesExistingOutputAndKeepsItsContents()
    {
        QTemporaryDir directory;
        const QString output = directory.filePath(QStringLiteral("original.wav"));
        QFile file(output);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("original"), qint64(8));
        file.close();
        EditorRecordingService recorder(factory(std::make_shared<InputFixture>()));
        QVERIFY(!recorder.start(output, 48'000, 2));
        QCOMPARE(readFile(output), QByteArray("original"));
    }

    void disconnectPreservesValidatedPartialWithoutSuccess()
    {
        QTemporaryDir directory;
        const QString output = directory.filePath(QStringLiteral("partial.wav"));
        auto fixture = std::make_shared<InputFixture>();
        EditorRecordingService recorder(factory(fixture));
        recorder.setInputGain(1);
        QSignalSpy completed(&recorder, &EditorRecordingService::finished);
        QSignalSpy failed(&recorder, &EditorRecordingService::failed);
        QVERIFY(recorder.start(output, 44'100, 2));
        QTRY_VERIFY(!recorder.actualInputDeviceName().isEmpty());
        fixture->send({0.25F, -0.25F, 0.5F, -0.5F}, 2);
        QTRY_COMPARE(recorder.recordedFrames(), 2);
        fixture->disconnected = true;
        QTRY_COMPARE(failed.count(), 1);
        QCOMPARE(completed.count(), 0);
        QCOMPARE(recorder.state(), EditorRecordingService::Error);
        QCOMPARE(recorder.partialRecordingPath(), output);
        QCOMPARE(readFile(output).size(), 56);
        QCOMPARE(readFile(output).mid(44).toHex(), QByteArray("0000200000e00000400000c0"));
    }

    void cancelRemovesOnlyOwnedCapture()
    {
        QTemporaryDir directory;
        const QString output = directory.filePath(QStringLiteral("cancel.wav"));
        auto fixture = std::make_shared<InputFixture>();
        EditorRecordingService recorder(factory(fixture));
        QSignalSpy completed(&recorder, &EditorRecordingService::finished);
        QVERIFY(recorder.start(output, 48'000, 1));
        QTRY_VERIFY(!recorder.actualInputDeviceName().isEmpty());
        fixture->send({0.25F});
        recorder.cancel();
        QTRY_COMPARE(recorder.state(), EditorRecordingService::Idle);
        QVERIFY(!QFile::exists(output));
        QCOMPARE(completed.count(), 0);
    }

    void emptyStopFailsAndDeviceOpenFailureDoesNotClaimSuccess()
    {
        QTemporaryDir directory;
        auto fixture = std::make_shared<InputFixture>();
        EditorRecordingService recorder(factory(fixture));
        QSignalSpy completed(&recorder, &EditorRecordingService::finished);
        QSignalSpy failed(&recorder, &EditorRecordingService::failed);
        const QString empty = directory.filePath(QStringLiteral("empty.wav"));
        QVERIFY(recorder.start(empty, 48'000, 1));
        QTRY_VERIFY(!recorder.actualInputDeviceName().isEmpty());
        recorder.stop();
        QTRY_COMPARE(failed.count(), 1);
        QVERIFY(!QFile::exists(empty));
        fixture->refuseOpen = true;
        const QString denied = directory.filePath(QStringLiteral("denied.wav"));
        QVERIFY(recorder.start(denied, 48'000, 1));
        QTRY_COMPARE(failed.count(), 2);
        QCOMPARE(completed.count(), 0);
        QVERIFY(!QFile::exists(denied));
    }

    void ringOverflowReportsErrorAndPreservesEarlierAudio()
    {
        QTemporaryDir directory;
        auto fixture = std::make_shared<InputFixture>();
        EditorRecordingService recorder(factory(fixture));
        QSignalSpy failed(&recorder, &EditorRecordingService::failed);
        QSignalSpy completed(&recorder, &EditorRecordingService::finished);
        const QString output = directory.filePath(QStringLiteral("overflow.wav"));
        QVERIFY(recorder.start(output, 8'000, 1));
        QTRY_VERIFY(!recorder.actualInputDeviceName().isEmpty());
        fixture->send({0.25F});
        QTRY_COMPARE(recorder.recordedFrames(), 1);
        fixture->send(std::vector<float>(8'000 * 4, 0.5F));
        QTRY_COMPARE(failed.count(), 1);
        QCOMPARE(completed.count(), 0);
        QCOMPARE(recorder.recordedFrames(), 1);
        QCOMPARE(readFile(output).size(), 48);
    }

    void stopCannotHideOverflowAlreadyReportedByCapture()
    {
        QTemporaryDir directory;
        auto fixture = std::make_shared<InputFixture>();
        EditorRecordingService recorder(factory(fixture));
        QSignalSpy failed(&recorder, &EditorRecordingService::failed);
        QSignalSpy completed(&recorder, &EditorRecordingService::finished);
        QVERIFY(recorder.start(directory.filePath(QStringLiteral("stop-overflow.wav")), 8'000, 1));
        QTRY_VERIFY(!recorder.actualInputDeviceName().isEmpty());
        fixture->send({0.25F});
        QTRY_COMPARE(recorder.recordedFrames(), 1);
        fixture->send(std::vector<float>(8'000 * 4, 0.5F));
        recorder.stop();
        QTRY_COMPARE(failed.count(), 1);
        QCOMPARE(completed.count(), 0);
        QCOMPARE(recorder.state(), EditorRecordingService::Error);
    }
};

QTEST_GUILESS_MAIN(EditorRecordingServiceTest)
#include "editor_recording_service_test.moc"
