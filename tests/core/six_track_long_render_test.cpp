#include "audio_editor/document_renderer.hpp"
#include "audio_editor/editor_playback_stream.hpp"
#include "decoder.hpp"
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QtTest>
#include <array>
#include <fstream>

using namespace agplayer::editor;
namespace {
void u16(std::ostream& s, std::uint16_t v) { const char b[]{char(v), char(v >> 8)}; s.write(b, 2); }
void u32(std::ostream& s, std::uint32_t v) { const char b[]{char(v), char(v >> 8), char(v >> 16), char(v >> 24)}; s.write(b, 4); }
float sourceSample(qint64 frame) { return frame % 8000 < 40 ? 0.07F : 0.01F; }
// Existing Decoder uses FFmpeg's equal-power mono -> stereo mapping.
float mixedSample(qint64 frame) { return 6 * sourceSample(frame) * std::sqrt(0.5F); }
}
class SixTrackLongRenderTest final : public QObject {
    Q_OBJECT
private slots:
    void completeFiveAndSixtyMinuteMixes() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto input = std::filesystem::path(directory.filePath("hour.wav").toStdWString());
        constexpr qint64 hourFrames = 8000LL * 3600;
        std::ofstream file(input, std::ios::binary);
        file.write("RIFF", 4); u32(file, 36 + hourFrames * 4);
        file.write("WAVEfmt ", 8); u32(file, 16); u16(file, 3); u16(file, 1);
        u32(file, 8000); u32(file, 32000); u16(file, 4); u16(file, 32);
        file.write("data", 4); u32(file, hourFrames * 4);
        std::array<float, 8000> second{};
        for (int i = 0; i < 8000; ++i) second[i] = sourceSample(i);
        for (int i = 0; i < 3600; ++i) file.write(reinterpret_cast<const char*>(second.data()), sizeof(second));
        file.close();
        QVERIFY(file.good());
        auto source = std::make_shared<AudioSource>(AudioSource{input, 8000, 1, hourFrames});
        for (int minutes : {5, 60}) {
            QElapsedTimer elapsed; elapsed.start();
            TimelineSnapshot snapshot;
            snapshot.sampleRate = 8000; snapshot.channels = 2;
            snapshot.totalFrames = 8000LL * minutes * 60;
            for (int track = 0; track < 6; ++track) {
                AudioEvent clip;
                clip.id = track + 1; clip.source = source;
                clip.sourceEnd = snapshot.totalFrames;
                clip.trackIndex = track; clip.timelineSampleRate = 8000;
                snapshot.events.push_back(clip);
            }
            std::string error;
            auto preview = EditorPlaybackStream::create(snapshot, {}, error);
            QVERIFY2(preview != nullptr, error.c_str());
            const qint64 lengthMs = minutes * 60000LL;
            for (qint64 seekMs : {qint64(0), lengthMs / 2 + 123, lengthMs - 100}) {
                QCOMPARE(preview->seek(seekMs), AG_OK);
                agplayer::DecodedAudioBlock block;
                QCOMPARE(preview->read(block), AG_OK);
                QVERIFY(block.frames > 0);
                QVERIFY2(std::abs(block.samples[0] - mixedSample(seekMs * 8)) < 0.00001F,
                    qPrintable(QString("seek=%1ms actual=%2 expected=%3").arg(seekMs).arg(block.samples[0], 0, 'g', 9).arg(mixedSample(seekMs * 8), 0, 'g', 9)));
            }
            const auto output = std::filesystem::path(directory.filePath(QString("mix-%1.wav").arg(minutes)).toStdWString());
            const auto rendered = DocumentRenderer{}.renderFloatWav(snapshot, {}, output);
            QVERIFY2(rendered.success, rendered.message.c_str());
            QCOMPARE(rendered.frames, snapshot.totalFrames);
            agplayer::Decoder decoder;
            QCOMPARE(decoder.open(output.u8string()), AG_OK);
            QCOMPARE(decoder.metadata().sample_rate, 8000);
            QCOMPARE(decoder.metadata().channels, 2);
            qint64 decoded = 0;
            double sum = 0;
            for (;;) {
                agplayer::DecodedAudioBlock block;
                QCOMPARE(decoder.read(block), AG_OK);
                for (std::size_t i = 0; i < block.frames; ++i) {
                    sum += block.samples[2 * i];
                    if (i == 0 || i + 1 == block.frames) {
                        QVERIFY(std::abs(block.samples[2 * i] - mixedSample(decoded + i)) < 0.00001F);
                        QCOMPARE(block.samples[2 * i], block.samples[2 * i + 1]);
                    }
                }
                decoded += static_cast<qint64>(block.frames);
                if (block.end_of_stream) break;
            }
            QCOMPARE(decoded, snapshot.totalFrames);
            const double expected = minutes * 60.0 * (40 * 6 * 0.07 + 7960 * 6 * 0.01) * std::sqrt(0.5);
            QVERIFY(std::abs(sum - expected) < expected * 0.00001);
            qInfo("%d-minute full six-track output verified in %lld ms", minutes, elapsed.elapsed());
        }
    }
};
QTEST_GUILESS_MAIN(SixTrackLongRenderTest)
#include "six_track_long_render_test.moc"
