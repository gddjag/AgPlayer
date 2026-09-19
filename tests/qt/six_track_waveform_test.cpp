#include "audio_editor/audio_editor_controller.hpp"
#include "audio_editor/project_document.hpp"

#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

#include <array>
#include <atomic>
#include <cmath>
#include <fstream>
#include <vector>

using namespace agplayer::editor;

namespace {
void u16(std::ostream& stream, std::uint16_t value)
{
    const std::array<char, 2> bytes{static_cast<char>(value & 255), static_cast<char>(value >> 8)};
    stream.write(bytes.data(), bytes.size());
}
void u32(std::ostream& stream, std::uint32_t value)
{
    const std::array<char, 4> bytes{static_cast<char>(value & 255), static_cast<char>((value >> 8) & 255),
        static_cast<char>((value >> 16) & 255), static_cast<char>(value >> 24)};
    stream.write(bytes.data(), bytes.size());
}
bool writePcm(const QString& path, const std::vector<float>& samples, std::uint32_t rate)
{
    std::ofstream stream(std::filesystem::path(path.toStdWString()), std::ios::binary);
    const auto bytes = static_cast<std::uint32_t>(samples.size() * 4);
    stream.write("RIFF", 4); u32(stream, 36 + bytes); stream.write("WAVEfmt ", 8);
    u32(stream, 16); u16(stream, 3); u16(stream, 1); u32(stream, rate); u32(stream, rate * 4);
    u16(stream, 4); u16(stream, 32); stream.write("data", 4); u32(stream, bytes);
    stream.write(reinterpret_cast<const char*>(samples.data()), bytes);
    return stream.good();
}
QString fixtureProject(const QString& directory)
{
    const auto firstPath = directory + "/first8k.wav";
    const auto secondPath = directory + "/second16k.wav";
    if (!writePcm(firstPath, std::vector<float>(80'000, 0.125F), 8'000)) return {};
    std::vector<float> second(160'000, 0.75F);
    for (std::size_t i = 0; i < 64; ++i) second[32'000 + i] = static_cast<float>(i + 1) / 64.0F;
    if (!writePcm(secondPath, second, 16'000)) return {};
    auto firstSource = std::make_shared<const AudioSource>(AudioSource{
        std::filesystem::path(firstPath.toStdWString()), 8'000, 1, 80'000});
    auto secondSource = std::make_shared<const AudioSource>(AudioSource{
        std::filesystem::path(secondPath.toStdWString()), 16'000, 1, 160'000});
    AudioEvent first{1, firstSource, 0, 80'000, 0};
    AudioEvent secondEvent{2, secondSource, 32'000, 112'000, 4'000};
    secondEvent.trackIndex = 5;
    auto document = AudioDocument::fromEvents({first, secondEvent});
    ProjectSaveRequest request;
    request.document = &document;
    request.visibleEndFrame = document.totalFrames();
    const auto project = directory + "/waveform.agproj";
    return ProjectDocument::save(project, request).ok() ? project : QString{};
}
} // namespace

class SixTrackWaveformTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

    void visibleClipGetsRealDetailInProjectCoordinates()
    {
        QTemporaryDir directory;
        const auto project = fixtureProject(directory.path());
        QVERIFY(!project.isEmpty());
        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(controller.openProject(QUrl::fromLocalFile(project)));
        QTRY_VERIFY_WITH_TIMEOUT(!controller.loading(), 10'000);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.sourcePeakCacheActiveForTesting(), 10'000);
        QSignalSpy published(&controller, &AudioEditorController::waveformChanged);
        controller.viewport()->setViewportWidth(200);
        QVERIFY(controller.viewport()->setVisibleRange(3'996, 4'020));
        QTRY_VERIFY_WITH_TIMEOUT(!published.empty(), 10'000);
        const auto channels = controller.eventPeaks("2", 160);
        QCOMPARE(channels.size(), qsizetype{1});
        const auto peaks = channels.front().toList();
        // Only the 20 visible frames of clip 2, without the viewport's leading
        // four blank frames. Each project frame spans two real source samples.
        QCOMPARE(peaks.size(), qsizetype{40});
        for (qsizetype frame = 0; frame < 20; ++frame) {
            const float expected = static_cast<float>(frame * 2 + 2) / 64.0F;
            QVERIFY(std::abs(peaks[frame * 2].toFloat() + expected) < 0.0001F);
            QVERIFY(std::abs(peaks[frame * 2 + 1].toFloat() - expected) < 0.0001F);
        }
        const auto first = controller.eventPeaks("1", 200).front().toList();
        QCOMPARE(first.size(), qsizetype{48});
        for (const auto& value : first) QVERIFY(std::abs(std::abs(value.toFloat()) - 0.125F) < 0.0001F);
    }

    void rapidViewportChangesAreDebouncedAndPlayheadDoesNotDecode()
    {
        QTemporaryDir directory;
        const auto project = fixtureProject(directory.path());
        QVERIFY(!project.isEmpty());
        std::atomic_int starts{0};
        std::atomic_int finishes{0};
        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(controller.openProject(QUrl::fromLocalFile(project)));
        QTRY_VERIFY_WITH_TIMEOUT(!controller.loading(), 10'000);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.sourcePeakCacheActiveForTesting(), 10'000);
        controller.setViewportWaveformTaskObserverForTesting([&](bool starting) {
            if (starting) ++starts; else ++finishes;
        });
        controller.viewport()->setViewportWidth(200);
        QVERIFY(controller.viewport()->setVisibleRange(3'996, 4'020));
        QTest::qWait(20);
        QCOMPARE(starts.load(), 0);
        QVERIFY(controller.viewport()->setVisibleRange(4'000, 4'024));
        QTest::qWait(20);
        QCOMPARE(starts.load(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(finishes.load(), 1, 10'000);
        QCOMPARE(starts.load(), 1);
        const auto generation = controller.viewportWaveformGeneration();
        QVERIFY(controller.seekFrame(4'001));
        QVERIFY(controller.seekFrame(4'010));
        QTest::qWait(350);
        QCOMPARE(starts.load(), 1);
        QCOMPARE(controller.viewportWaveformGeneration(), generation);
    }

    void trackEditsReuseSourceOverviewAnalysis()
    {
        QTemporaryDir directory;
        const auto project = fixtureProject(directory.path());
        QVERIFY(!project.isEmpty());
        std::atomic_int sourceAnalyses{0};
        AudioEditorController controller(AG_AUDIO_BACKEND_NULL);
        QVERIFY(controller.openProject(QUrl::fromLocalFile(project)));
        QTRY_VERIFY_WITH_TIMEOUT(!controller.loading(), 10'000);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.sourcePeakCacheActiveForTesting(), 10'000);
        controller.setSourcePeakCacheTaskObserverForTesting([&](bool starting) {
            if (starting) ++sourceAnalyses;
        });
        controller.viewport()->setViewportWidth(200);
        QVERIFY(controller.viewport()->setVisibleRange(3'996, 4'120));
        QTest::qWait(350);
        QSignalSpy published(&controller, &AudioEditorController::waveformChanged);
        QVERIFY(controller.setTrackMute(5, true));
        QVERIFY(controller.setTrackGain(5, 0.5));
        QVERIFY(controller.moveEventToTrack("2", 4'050, 5));
        QTRY_VERIFY_WITH_TIMEOUT(!published.empty(), 10'000);
        QCOMPARE(sourceAnalyses.load(), 0);
        QVERIFY(!controller.eventPeaks("2", 100).isEmpty());
    }
};

QTEST_MAIN(SixTrackWaveformTest)
#include "six_track_waveform_test.moc"
