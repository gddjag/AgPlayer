#include "audio_editor/document_renderer.hpp"
#include "audio_editor/document_render_pipeline.hpp"
#include "audio_editor/audio_file_analyzer.hpp"
#include "audio_editor/automation_time_mapper.hpp"
#include "audio_editor/editor_playback_stream.hpp"
#include "audio_editor/time_pitch_session.hpp"
#include "decoder.hpp"

#include <QTemporaryDir>
#include <QtTest>

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <vector>

using namespace agplayer::editor;

namespace {

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

bool writeFloatWav(const std::filesystem::path& path,
                   const std::vector<float>& samples,
                   const std::uint32_t sampleRate = 8'000)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    const auto bytes = static_cast<std::uint32_t>(samples.size() * sizeof(float));
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

std::vector<float> readFloatWav(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    std::array<char, 44> header{};
    stream.read(header.data(), static_cast<std::streamsize>(header.size()));
    std::vector<char> bytes{std::istreambuf_iterator<char>(stream),
                            std::istreambuf_iterator<char>()};
    std::vector<float> samples(bytes.size() / sizeof(float));
    std::memcpy(samples.data(), bytes.data(), samples.size() * sizeof(float));
    return samples;
}

std::vector<float> readRealtime(
    const TimelineSnapshot& snapshot,
    const EditorPlaybackParameters& parameters)
{
    std::string error;
    auto stream = EditorPlaybackStream::create(snapshot, parameters, error);
    if (!stream) return {};
    std::vector<float> samples;
    agplayer::DecodedAudioBlock block;
    do {
        if (stream->read(block) != AG_OK) return {};
        samples.insert(samples.end(), block.samples.begin(), block.samples.end());
    } while (!block.end_of_stream);
    return samples;
}

std::vector<float> decodeWav(const std::filesystem::path& path)
{
    agplayer::Decoder decoder;
    if (decoder.open(path.u8string()) != AG_OK) return {};
    std::vector<float> samples;
    agplayer::DecodedAudioBlock block;
    do {
        if (decoder.read(block) != AG_OK) return {};
        samples.insert(samples.end(), block.samples.begin(), block.samples.end());
    } while (!block.end_of_stream);
    return samples;
}

std::shared_ptr<const AudioSource> sourceFor(
    const std::filesystem::path& path, const SampleFrame frames)
{
    return std::make_shared<const AudioSource>(AudioSource{
        path, 8'000, 1, frames});
}

void compareNear(const float actual, const float expected)
{
    QVERIFY2(std::abs(actual - expected) < 0.0001F,
             qPrintable(QStringLiteral("expected %1, got %2")
                 .arg(expected, 0, 'f', 5).arg(actual, 0, 'f', 5)));
}

} // namespace

class DocumentRendererTest final : public QObject {
    Q_OBJECT

private slots:
    void rendersTrimmedEventsGapsGainAndMute()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto input = std::filesystem::path(
            directory.filePath(QStringLiteral("input.wav")).toStdWString());
        const auto secondInput = std::filesystem::path(
            directory.filePath(QStringLiteral("second.wav")).toStdWString());
        const auto output = std::filesystem::path(
            directory.filePath(QStringLiteral("output.wav")).toStdWString());
        std::vector<float> inputSamples(32);
        for (std::size_t index = 0; index < inputSamples.size(); ++index) {
            inputSamples[index] = static_cast<float>(index) / 64.0F;
        }
        QVERIFY(writeFloatWav(input, inputSamples));
        QVERIFY(writeFloatWav(secondInput, std::vector<float>(8, 0.75F)));
        const auto source = sourceFor(input, 32);
        const auto secondSource = sourceFor(secondInput, 8);
        AudioEvent first{1, source, 8, 12, 0};
        first.gain = 2.0F;
        const AudioEvent second{2, secondSource, 0, 2, 7};
        AudioEvent third{3, source, 16, 18, 9};
        third.mute = true;
        const TimelineSnapshot snapshot{{first, second, third}, 11, 9};
        std::vector<float> progress;

        const RenderResult result = DocumentRenderer{}.renderFloatWav(
            snapshot, std::nullopt, output, nullptr,
            [&progress](const float value) { progress.push_back(value); });

        QVERIFY2(result.success, result.message.c_str());
        QCOMPARE(result.frames, SampleFrame{11});
        const auto samples = readFloatWav(output);
        QCOMPARE(samples.size(), std::size_t{11});
        compareNear(samples[0], 0.25F);
        compareNear(samples[1], 0.28125F);
        compareNear(samples[2], 0.3125F);
        compareNear(samples[3], 0.34375F);
        for (std::size_t index = 4; index < 7; ++index) {
            compareNear(samples[index], 0.0F);
        }
        compareNear(samples[7], 0.75F);
        compareNear(samples[8], 0.75F);
        compareNear(samples[9], 0.0F);
        compareNear(samples[10], 0.0F);
        QVERIFY(!progress.empty());
        QCOMPARE(progress.back(), 1.0F);
        for (std::size_t index = 1; index < progress.size(); ++index) {
            QVERIFY(progress[index] >= progress[index - 1]);
        }
    }

    void appliesFadeAndEnvelopeAtOriginalEventOffsets()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto input = std::filesystem::path(
            directory.filePath(QStringLiteral("input.wav")).toStdWString());
        const auto output = std::filesystem::path(
            directory.filePath(QStringLiteral("output.wav")).toStdWString());
        QVERIFY(writeFloatWav(input, std::vector<float>(8, 1.0F)));
        AudioEvent event{1, sourceFor(input, 8), 0, 8, 0};
        event.gain = 0.5F;
        event.fadeIn = 2;
        event.fadeOut = 2;
        event.envelope = {{0, 0.5F}, {4, 1.0F}};

        const RenderResult result = DocumentRenderer{}.renderFloatWav(
            TimelineSnapshot{{event}, 8, 1}, Selection{1, 8}, output);

        QVERIFY2(result.success, result.message.c_str());
        const auto samples = readFloatWav(output);
        QCOMPARE(samples.size(), std::size_t{7});
        const std::array<float, 7> expected{
            0.3125F, 0.375F, 0.4375F, 0.5F, 0.5F, 0.5F, 0.0F};
        for (std::size_t index = 0; index < expected.size(); ++index) {
            compareNear(samples[index], expected[index]);
        }
    }

    void cancellationRemovesPartialOutput()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto input = std::filesystem::path(
            directory.filePath(QStringLiteral("input.wav")).toStdWString());
        const auto output = std::filesystem::path(
            directory.filePath(QStringLiteral("output.wav")).toStdWString());
        QVERIFY(writeFloatWav(input, std::vector<float>(16, 0.25F)));
        const AudioEvent event{1, sourceFor(input, 16), 0, 16, 4};
        std::atomic_bool cancelled{true};

        const RenderResult result = DocumentRenderer{}.renderFloatWav(
            TimelineSnapshot{{event}, 20, 1}, std::nullopt, output, &cancelled);

        QVERIFY(!result.success);
        QCOMPARE(result.message, std::string{"cancelled"});
        QVERIFY(!std::filesystem::exists(output));
    }

    void sharedPipelineAppliesAutomationAfterTimePitch()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto input = std::filesystem::path(
            directory.filePath(QStringLiteral("input.wav")).toStdWString());
        const auto output = std::filesystem::path(
            directory.filePath(QStringLiteral("output.wav")).toStdWString());
        constexpr SampleFrame frames = 8'000;
        QVERIFY(writeFloatWav(input,
            std::vector<float>(static_cast<std::size_t>(frames), 0.5F)));
        const auto analysis = AudioFileAnalyzer::analyze(input, 64);
        QVERIFY2(analysis.success, analysis.message.c_str());
        auto event = AudioDocument::fromSource(analysis.source)
            .timelineSnapshot().events.front();
        event.envelope = {{0, 1.0F}, {3'999, 1.0F}, {4'000, 0.0F},
                          {4'001, 1.0F}};
        WriteRequest request;
        request.snapshot = TimelineSnapshot{{event}, frames, 9};
        request.output_path = output;
        request.codec_name = "pcm_f32le";
        request.sample_rate = 8'000;
        request.channels = 1;
        request.keep_metadata = false;
        TimePitchSession timePitch;
        QVERIFY(timePitch.setSpeedPercent(200.0));

        const auto result = DocumentRenderPipeline{}.write(request, timePitch);
        QVERIFY2(result.ok(), result.message.c_str());
        agplayer::Decoder decoder;
        QCOMPARE(decoder.open(output.u8string()), AG_OK);
        std::vector<float> samples;
        agplayer::DecodedAudioBlock block;
        do {
            QCOMPARE(decoder.read(block), AG_OK);
            samples.insert(samples.end(), block.samples.begin(),
                           block.samples.end());
        } while (!block.end_of_stream);
        QVERIFY(samples.size() > 3'500U && samples.size() < 4'500U);
        const std::size_t mapped = samples.size() / 2U;
        float localMinimum = 1.0F;
        for (std::size_t index = mapped - 8U; index < mapped + 9U; ++index) {
            localMinimum = std::min(localMinimum, std::abs(samples[index]));
        }
        QVERIFY2(localMinimum < 0.05F,
                 "export pipeline applied gain before time/pitch");
        QVERIFY(std::abs(samples[mapped - 32U]) > 0.25F);
        QVERIFY(std::abs(samples[mapped + 32U]) > 0.25F);
    }

    void realtimeAndOfflineUseTheSameAutomationClockThroughFlush()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto input = std::filesystem::path(
            directory.filePath(QStringLiteral("mapping-input.wav")).toStdWString());
        constexpr SampleFrame frames = 8'003;
        QVERIFY(writeFloatWav(input,
            std::vector<float>(static_cast<std::size_t>(frames), 0.5F)));
        const auto analysis = AudioFileAnalyzer::analyze(input, 64);
        QVERIFY2(analysis.success, analysis.message.c_str());
        auto event = AudioDocument::fromSource(analysis.source)
            .timelineSnapshot().events.front();
        event.fadeIn = 257;
        event.fadeInCurve = FadeCurve::Exponential;
        event.fadeOut = 513;
        event.fadeOutCurve = FadeCurve::Smooth;
        for (SampleFrame frame = 0; frame < frames; frame += 257) {
            event.envelope.push_back({frame,
                (frame / 257) % 2 == 0 ? 0.2F : 1.8F});
        }
        const TimelineSnapshot snapshot{{event}, frames, 10};
        AudioEvent neutralEvent = event;
        neutralEvent.fadeIn = 0;
        neutralEvent.fadeOut = 0;
        neutralEvent.envelope.clear();
        const TimelineSnapshot neutral{{neutralEvent}, frames, 11};
        const auto renderOffline = [&](const TimelineSnapshot& value,
                                       const QString& name) {
            TimePitchSession timePitch;
            if (!timePitch.setSpeedPercent(137.0)) return std::vector<float>{};
            WriteRequest request;
            request.snapshot = value;
            request.output_path = std::filesystem::path(
                directory.filePath(name).toStdWString());
            request.codec_name = "pcm_f32le";
            request.sample_rate = 8'000;
            request.channels = 1;
            request.keep_metadata = false;
            const auto written = DocumentRenderPipeline{}.write(request, timePitch);
            return written.ok() ? decodeWav(request.output_path)
                                : std::vector<float>{};
        };
        const std::vector<float> offline = renderOffline(
            snapshot, QStringLiteral("mapping-automated.wav"));
        const std::vector<float> offlineNeutral = renderOffline(
            neutral, QStringLiteral("mapping-neutral.wav"));
        EditorPlaybackParameters parameters;
        parameters.speed_ratio = 1.37;
        parameters.keep_pitch = true;
        const std::vector<float> realtime = readRealtime(snapshot, parameters);
        const std::vector<float> realtimeNeutral = readRealtime(
            neutral, parameters);
        QVERIFY(!offline.empty());
        QCOMPARE(offline.size(), offlineNeutral.size());
        QCOMPARE(realtime.size(), realtimeNeutral.size());
        QCOMPARE(offline.size(), realtime.size());
        const AutomationTimeMapper automationTime(0, frames, 1.37);
        for (std::size_t frame = 0; frame < realtime.size(); ++frame) {
            if (std::abs(offlineNeutral[frame]) < 0.05F
                || std::abs(realtimeNeutral[frame]) < 0.05F) {
                continue;
            }
            const float offlineGain = offline[frame] / offlineNeutral[frame];
            const float realtimeGain = realtime[frame] / realtimeNeutral[frame];
            QVERIFY2(std::abs(realtimeGain - offlineGain) < 0.001F,
                     qPrintable(QStringLiteral(
                         "automation clocks diverged at output frame %1: "
                         "offlineGain=%2 realtimeGain=%3 offline=%4 "
                         "offlineNeutral=%5 realtime=%6 realtimeNeutral=%7 "
                         "offlineFrames=%8 realtimeFrames=%9 mappedFrame=%10")
                         .arg(frame)
                         .arg(offlineGain, 0, 'g', 9)
                         .arg(realtimeGain, 0, 'g', 9)
                         .arg(offline[frame], 0, 'g', 9)
                         .arg(offlineNeutral[frame], 0, 'g', 9)
                         .arg(realtime[frame], 0, 'g', 9)
                         .arg(realtimeNeutral[frame], 0, 'g', 9)
                         .arg(offline.size())
                         .arg(realtime.size())
                         .arg(automationTime.map(
                             static_cast<SampleFrame>(frame)))));
        }
        QCOMPARE(automationTime.map(
            static_cast<SampleFrame>(offline.size() + 12U)), frames - 1);
    }

    void mutedInputCannotLeakAcrossTimePitchIntoAudibleEvent()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto input = std::filesystem::path(
            directory.filePath(QStringLiteral("mute-input.wav")).toStdWString());
        const auto output = std::filesystem::path(
            directory.filePath(QStringLiteral("mute-output.wav")).toStdWString());
        constexpr SampleFrame half = 4'000;
        std::vector<float> samples(static_cast<std::size_t>(half * 2), 0.0F);
        for (SampleFrame frame = half - 1'000; frame < half; ++frame) {
            samples[static_cast<std::size_t>(frame)] = 0.9F;
        }
        QVERIFY(writeFloatWav(input, samples));
        const auto source = sourceFor(input, half * 2);
        AudioEvent muted{1, source, 0, half, 0};
        muted.mute = true;
        const AudioEvent audible{2, source, half, half * 2, half};
        const TimelineSnapshot snapshot{{muted, audible}, half * 2, 11};

        TimePitchSession timePitch;
        QVERIFY(timePitch.setSpeedPercent(137.0));
        WriteRequest request;
        request.snapshot = snapshot;
        request.output_path = output;
        request.codec_name = "pcm_f32le";
        request.sample_rate = 8'000;
        request.channels = 1;
        request.keep_metadata = false;
        const auto written = DocumentRenderPipeline{}.write(request, timePitch);
        QVERIFY2(written.ok(), written.message.c_str());
        const std::vector<float> offline = decodeWav(output);

        EditorPlaybackParameters parameters;
        parameters.speed_ratio = 1.37;
        parameters.keep_pitch = true;
        const std::vector<float> realtime = readRealtime(snapshot, parameters);
        QVERIFY(!offline.empty());
        QVERIFY(!realtime.empty());
        const auto silent = [](const std::vector<float>& rendered) {
            return std::all_of(rendered.begin(), rendered.end(),
                [](const float sample) { return std::abs(sample) < 0.00001F; });
        };
        QVERIFY2(silent(offline),
                 "offline Time/Pitch leaked muted input into audible output");
        QVERIFY2(silent(realtime),
                 "realtime Time/Pitch leaked muted input into audible output");
    }

    void rejectsInvalidFirstEventWithoutCreatingOutput()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto output = std::filesystem::path(
            directory.filePath(QStringLiteral("output.wav")).toStdWString());
        AudioEvent invalid;

        const RenderResult result = DocumentRenderer{}.renderFloatWav(
            TimelineSnapshot{{invalid}, 1, 1}, std::nullopt, output);

        QVERIFY(!result.success);
        QCOMPARE(result.message, std::string{"invalid or unsupported timeline event"});
        QVERIFY(!std::filesystem::exists(output));
    }
};

QTEST_APPLESS_MAIN(DocumentRendererTest)

#include "document_renderer_test.moc"
