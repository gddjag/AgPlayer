#include "audio_editor/audio_source_probe.hpp"

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

#include <filesystem>
#include <chrono>

using namespace agplayer::editor;

namespace {

std::filesystem::path nativePath(const QString& path)
{
#ifdef Q_OS_WIN
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::u8path(path.toUtf8().constData());
#endif
}

} // namespace

class AudioSourceProbeTest final : public QObject {
    Q_OBJECT

private slots:
    void readsFixtureIdentityWithoutPeakPayload()
    {
        const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_EDITOR_FIXTURE"));
        QVERIFY2(QFileInfo::exists(fixture), "decoder fixture is required");

        const AudioSourceProbeResult result = AudioSourceProbe::probe(nativePath(fixture));

        QVERIFY2(result.ok(), result.message.c_str());
        QCOMPARE(result.source.sample_rate, std::uint32_t{44'100});
        QCOMPARE(result.source.channels, std::uint32_t{2});
        QCOMPARE(result.source.total_frames, SampleFrame{88'200});
    }

    void rejectsUnreadableSource()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString invalid = temporary.filePath(QStringLiteral("not-audio.bin"));
        QFile file(invalid);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.write("not audio") > 0);
        file.close();

        const AudioSourceProbeResult result = AudioSourceProbe::probe(nativePath(invalid));

        QVERIFY(!result.ok());
        QVERIFY(result.source.path.empty());
    }

    void formatMatchUsesBoundedContainerDurationTolerance()
    {
        AudioSourceProbeResult probe;
        probe.success = true;
        probe.source = {{}, 48'000, 2, 96'000};
        probe.frame_tolerance = 4'800;

        QVERIFY(probe.matchesFormat(AudioSource{{}, 48'000, 2, 100'800}));
        QVERIFY(!probe.matchesFormat(AudioSource{{}, 48'000, 2, 100'801}));
        QVERIFY(!probe.matchesFormat(AudioSource{{}, 44'100, 2, 96'000}));
        QVERIFY(!probe.matchesFormat(AudioSource{{}, 48'000, 1, 96'000}));
    }

    void expiredDeadlineStopsProbeBeforeOpeningSource()
    {
        const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_EDITOR_FIXTURE"));
        QVERIFY2(QFileInfo::exists(fixture), "decoder fixture is required");

        const AudioSourceProbeResult result = AudioSourceProbe::probe(
            nativePath(fixture), std::chrono::steady_clock::now());

        QVERIFY(!result.ok());
        QVERIFY(result.timed_out);
    }
};

QTEST_APPLESS_MAIN(AudioSourceProbeTest)

#include "audio_source_probe_test.moc"
