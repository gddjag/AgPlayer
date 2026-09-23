#include "audio_editor/document_renderer.hpp"
#include "audio_editor/document_render_pipeline.hpp"
#include "audio_editor/audio_file_analyzer.hpp"
#include "audio_editor/editor_playback_stream.hpp"
#include "audio_editor/time_pitch_session.hpp"
#include "audio_editor/timeline_mixer.hpp"
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
#include <limits>
#include <memory>
#include <numeric>
#include <vector>

using namespace agplayer::editor;

namespace {

int engineCreations = 0;

std::unique_ptr<agplayer::ITimePitchEngine> countingTimePitchEngine()
{
    ++engineCreations;
    return agplayer::create_time_pitch_engine();
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
    void perTrackProcessorStaysOpenAcrossReadBlocks()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto input = std::filesystem::path(directory.filePath("long.wav").toStdWString());
        QVERIFY(writeFloatWav(input, std::vector<float>(16'000, 0.2F)));
        AudioEvent event{1, sourceFor(input, 16'000), 0, 16'000, 0};
        event.speedRatio = 0.5;
        TimelineSnapshot snapshot{{event}, 32'000, 1};
        std::string error;
        QVERIFY2(TimelineMixer::prepare(snapshot, error), error.c_str());
        engineCreations = 0;
        TimelineMixer mixer{std::move(snapshot), nullptr, &countingTimePitchEngine};
        std::vector<float> block;
        while (mixer.cursor() < 32'000) {
            QCOMPARE(mixer.read(block, 32'000), AG_OK);
            QVERIFY(!block.empty());
        }
        QCOMPARE(engineCreations, 1);
    }

    void soloAndPerTrackTimePitchReachPreviewAndExport()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto input = std::filesystem::path(directory.filePath("scoped.wav").toStdWString());
        std::vector<float> sine(8'000);
        for (std::size_t frame = 0; frame < sine.size(); ++frame)
            sine[frame] = 0.2F * std::sin(2.0 * 3.141592653589793 * 220.0 * frame / 8'000.0);
        QVERIFY(writeFloatWav(input, sine));
        auto source = sourceFor(input, 8'000);
        AudioEvent solo{1, source, 0, 8'000, 0};
        solo.speedRatio = 2.0;
        solo.pitchSemitone = 3;
        AudioEvent other{2, source, 0, 8'000, 0};
        other.trackIndex = 1;
        TimelineSnapshot snapshot{{solo, other}, 8'000, 1};
        snapshot.sampleRate = 8'000;
        snapshot.channels = 1;
        snapshot.tracks[0].solo = true;
        const auto preview = readRealtime(snapshot, {});
        QCOMPARE(preview.size(), std::size_t{8'000});
        const float tailEnergy = std::accumulate(preview.begin() + 6'000,
            preview.end(), 0.0F, [](float sum, float sample) { return sum + std::abs(sample); });
        QVERIFY(tailEnergy < 0.001F);
        const float leadEnergy = std::accumulate(preview.begin() + 1'000,
            preview.begin() + 3'000, 0.0F,
            [](float sum, float sample) { return sum + std::abs(sample); });
        QVERIFY(leadEnergy > 10.0F);
        snapshot.events[0].pitchSemitone = 0;
        const auto unpitched = readRealtime(snapshot, {});
        QCOMPARE(unpitched.size(), preview.size());
        const auto crossings = [](const std::vector<float>& samples) {
            int count = 0;
            for (std::size_t i = 1'001; i < 3'500; ++i)
                if (samples[i - 1] <= 0 && samples[i] > 0) ++count;
            return count;
        };
        QVERIFY(crossings(preview) > crossings(unpitched) * 1.1);
        snapshot.events[0].pitchSemitone = 3;
        const auto output = std::filesystem::path(directory.filePath("scoped-out.wav").toStdWString());
        const auto result = DocumentRenderer{}.renderFloatWav(snapshot, {}, output);
        QVERIFY2(result.success, result.message.c_str());
        QCOMPARE(readFloatWav(output), preview);

        snapshot.events[0].speedRatio = 0.5;
        snapshot.totalFrames = 16'000;
        const auto slowed = readRealtime(snapshot, {});
        QCOMPARE(slowed.size(), std::size_t{16'000});
        const float slowTailEnergy = std::accumulate(slowed.begin() + 13'000,
            slowed.begin() + 15'000, 0.0F,
            [](float sum, float sample) { return sum + std::abs(sample); });
        QVERIFY(slowTailEnergy > 10.0F);
    }

    void sixTracksMixRealPcmBeforeSessionEffects()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto input = std::filesystem::path(directory.filePath("six.wav").toStdWString());
        QVERIFY(writeFloatWav(input, std::vector<float>(8'000, 0.125F)));
        TimelineSnapshot snapshot;
        snapshot.sampleRate = 8'000;
        snapshot.channels = 2;
        snapshot.totalFrames = 8'000;
        for (int track = 0; track < 6; ++track) {
            AudioEvent event{static_cast<EventId>(track + 1), sourceFor(input, 8'000), 0, 8'000, 0};
            event.trackIndex = track;
            event.timelineSampleRate = 8'000;
            snapshot.events.push_back(event);
        }
        snapshot.tracks[0].muted = true;
        snapshot.tracks[1].gain = 0.5F;
        snapshot.events[2].gain = 0.5F;
        snapshot.events[3].envelope = {{0, 0.5F}};
        const auto realtime = readRealtime(snapshot, {});
        QCOMPARE(realtime.size(), std::size_t{16'000});
        // FFmpeg's mono-to-stereo matrix is -3dB per channel.
        const float expected = 0.125F * 3.5F * std::sqrt(0.5F);
        for (float sample : realtime) compareNear(sample, expected);
        const auto output = std::filesystem::path(directory.filePath("six-out.wav").toStdWString());
        const auto result = DocumentRenderer{}.renderFloatWav(snapshot, {}, output);
        QVERIFY2(result.success, result.message.c_str());
        QCOMPARE(result.channels, std::uint32_t{2});
        const auto offline = readFloatWav(output);
        QCOMPARE(offline, realtime);

        // The same six-track sum must feed the actual TimePitch engine in
        // preview and export, including tempo, pitch and formant settings.
        EditorPlaybackParameters effects;
        effects.speed_ratio = 1.37;
        effects.pitch_cents = 300;
        effects.formant_preservation = true;
        const auto previewEffects = readRealtime(snapshot, effects);
        QVERIFY(!previewEffects.empty());
        const auto effectOutput = std::filesystem::path(directory.filePath("six-effects.wav").toStdWString());
        const auto effected = DocumentRenderer{}.renderFloatWav(snapshot, {}, effectOutput,
            nullptr, {}, effects);
        QVERIFY2(effected.success, effected.message.c_str());
        QCOMPARE(readFloatWav(effectOutput), previewEffects);
    }

    void mixedSourceRatesSeekAndLongTimelineTail()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto input = std::filesystem::path(directory.filePath("rate.wav").toStdWString());
        QVERIFY(writeFloatWav(input, std::vector<float>(16'000, 0.25F), 16'000));
        auto source = std::make_shared<const AudioSource>(AudioSource{input, 16'000, 1, 16'000});
        // 100 hours exceeds both the old 60-minute cap and signed 32-bit frames.
        AudioEvent event{1, source, 4'000, 12'000, 8'000LL * 60 * 6'000};
        event.trackIndex = 5;
        event.timelineSampleRate = 8'000;
        TimelineSnapshot snapshot{{event}, event.timelineStart + 4'000, 1};
        snapshot.sampleRate = 8'000;
        snapshot.channels = 2;
        std::string error;
        auto stream = EditorPlaybackStream::create(snapshot, {}, error);
        QVERIFY2(stream != nullptr, error.c_str());
        agplayer::DecodedAudioBlock block;
        QCOMPARE(stream->read(block), AG_OK);
        QCOMPARE(block.frames, std::size_t{4'096});
        QVERIFY(std::all_of(block.samples.begin(), block.samples.end(), [](float x) { return x == 0; }));
        QCOMPARE(stream->seek(360'000'250), AG_OK);
        QCOMPARE(stream->read(block), AG_OK);
        QCOMPARE(block.frames, std::size_t{2'000});
        for (float sample : block.samples) compareNear(sample, 0.25F * std::sqrt(0.5F));
        QCOMPARE(stream->seek(0), AG_OK);
        QCOMPARE(stream->read(block), AG_OK);
        QVERIFY(std::all_of(block.samples.begin(), block.samples.end(), [](float x) { return x == 0; }));
        const auto output = std::filesystem::path(directory.filePath("tail.wav").toStdWString());
        const auto rendered = DocumentRenderer{}.renderFloatWav(snapshot,
            Selection{event.timelineStart, snapshot.totalFrames}, output);
        QVERIFY2(rendered.success, rendered.message.c_str());
        QCOMPARE(rendered.frames, SampleFrame{4'000});
        for (float sample : readFloatWav(output)) compareNear(sample, 0.25F * std::sqrt(0.5F));
    }

    void sumsWithoutNormalizationAndClampsOnlyFinalOutput()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto input = std::filesystem::path(directory.filePath("headroom.wav").toStdWString());
        QVERIFY(writeFloatWav(input, std::vector<float>(8'000, 0.4F)));
        TimelineSnapshot snapshot;
        snapshot.totalFrames = 8'000;
        for (int track = 0; track < 6; ++track) {
            AudioEvent event{static_cast<EventId>(track + 1), sourceFor(input, 8'000), 0, 8'000, 0};
            event.trackIndex = track;
            snapshot.events.push_back(event);
        }
        const auto samples = readRealtime(snapshot, {});
        QCOMPARE(samples.size(), std::size_t{8'000});
        for (float sample : samples) compareNear(sample, 1.0F);
        snapshot.legacyMasterGain = 0.25F;
        const auto reduced = readRealtime(snapshot, {});
        QCOMPARE(reduced.size(), samples.size());
        for (float sample : reduced) compareNear(sample, 0.6F);
    }

    void mixedRatesAndClipBoundariesStaySynchronous()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto firstPath = std::filesystem::path(directory.filePath("8k.wav").toStdWString());
        const auto secondPath = std::filesystem::path(directory.filePath("16k.wav").toStdWString());
        QVERIFY(writeFloatWav(firstPath, std::vector<float>(8'000, 0.125F)));
        QVERIFY(writeFloatWav(secondPath, std::vector<float>(16'000, 0.25F), 16'000));
        auto secondSource = std::make_shared<const AudioSource>(AudioSource{secondPath, 16'000, 1, 16'000});
        AudioEvent first{1, sourceFor(firstPath, 8'000), 0, 8'000, 0};
        AudioEvent second{2, secondSource, 4'000, 12'000, 2'000};
        second.trackIndex = 4;
        second.timelineSampleRate = 8'000;
        second.fadeIn = 101;
        second.fadeInCurve = FadeCurve::Linear;
        TimelineSnapshot snapshot{{first, second}, 8'000, 1};
        snapshot.sampleRate = 8'000;
        snapshot.channels = 1;
        const auto mixed = readRealtime(snapshot, {});
        QCOMPARE(mixed.size(), std::size_t{8'000});
        compareNear(mixed[1'999], 0.125F);
        compareNear(mixed[2'000], 0.125F);
        compareNear(mixed[2'050], 0.25F);
        compareNear(mixed[2'100], 0.375F);
        compareNear(mixed[5'999], 0.375F);
        compareNear(mixed[6'000], 0.125F);
        std::string error;
        auto stream = EditorPlaybackStream::create(snapshot, {}, error);
        QVERIFY(stream != nullptr);
        for (const auto milliseconds : {250, 625, 0, 750, 250}) {
            QCOMPARE(stream->seek(milliseconds), AG_OK);
            agplayer::DecodedAudioBlock block;
            QCOMPARE(stream->read(block), AG_OK);
            QVERIFY(block.frames > 0);
            const auto start = static_cast<std::size_t>(milliseconds * 8);
            for (std::size_t i = 0; i < block.frames; ++i) compareNear(block.samples[i], mixed[start + i]);
        }
    }

    void invalidSourceSamplesDoNotPoisonAnotherTrack()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto input = std::filesystem::path(directory.filePath("finite.wav").toStdWString());
        std::vector<float> sourcePcm(8'000, 0.25F);
        sourcePcm[100] = std::numeric_limits<float>::quiet_NaN();
        sourcePcm[101] = std::numeric_limits<float>::infinity();
        QVERIFY(writeFloatWav(input, sourcePcm));
        const auto source = sourceFor(input, 8'000);
        AudioEvent first{1, source, 0, 8'000, 0};
        AudioEvent second{2, source, 0, 8'000, 0};
        second.trackIndex = 1;
        const TimelineSnapshot snapshot{{first, second}, 8'000, 1};
        EditorPlaybackParameters effects;
        effects.speed_ratio = 0.75;
        const auto samples = readRealtime(snapshot, effects);
        QVERIFY(!samples.empty());
        QVERIFY(std::all_of(samples.begin(), samples.end(), [](float sample) {
            return std::isfinite(sample) && std::abs(sample) <= 1.0F;
        }));
        QVERIFY(std::any_of(samples.begin(), samples.end(), [](float sample) {
            return sample > 0.1F;
        }));
    }

    void resampledTrimAndSeekUseSourceFrameCoordinates()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto input = std::filesystem::path(directory.filePath("ramp16k.wav").toStdWString());
        std::vector<float> ramp(16'000);
        for (std::size_t i = 0; i < ramp.size(); ++i) ramp[i] = 0.1F + static_cast<float>(i) * 0.00001F;
        QVERIFY(writeFloatWav(input, ramp, 16'000));
        auto source = std::make_shared<const AudioSource>(AudioSource{input, 16'000, 1, 16'000});
        AudioEvent event{1, source, 4'000, 12'000, 0};
        event.timelineSampleRate = 8'000;
        TimelineSnapshot snapshot{{event}, 4'000, 1};
        snapshot.sampleRate = 8'000;
        snapshot.channels = 1;
        const auto samples = readRealtime(snapshot, {});
        QCOMPARE(samples.size(), std::size_t{4'000});
        compareNear(samples[0], 0.14F);
        compareNear(samples[1'000], 0.16F);
        compareNear(samples[3'999], 0.21998F);
        std::string error;
        auto stream = EditorPlaybackStream::create(snapshot, {}, error);
        QVERIFY(stream != nullptr);
        QCOMPARE(stream->seek(125), AG_OK);
        agplayer::DecodedAudioBlock block;
        QCOMPARE(stream->read(block), AG_OK);
        compareNear(block.samples.front(), 0.16F);
    }

    void cancellationDuringMixedRenderRemovesPartialOutput()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto input = std::filesystem::path(directory.filePath("cancel-in.wav").toStdWString());
        const auto output = std::filesystem::path(directory.filePath("cancel-out.wav").toStdWString());
        QVERIFY(writeFloatWav(input, std::vector<float>(16'000, 0.25F)));
        AudioEvent first{1, sourceFor(input, 16'000), 0, 16'000, 0};
        AudioEvent second = first;
        second.id = 2;
        second.trackIndex = 1;
        std::atomic_bool cancelled{false};
        const auto rendered = DocumentRenderer{}.renderFloatWav(
            TimelineSnapshot{{first, second}, 16'000, 1}, {}, output, &cancelled,
            [&cancelled](float progress) { if (progress > 0) cancelled.store(true); });
        QVERIFY(!rendered.success);
        QCOMPARE(rendered.message, std::string{"cancelled"});
        QVERIFY(rendered.frames > 0 && rendered.frames < 16'000);
        QVERIFY(!std::filesystem::exists(output));
    }

    void rejectsOverwriteOfAnyOriginalSourceBeforeTimePitch()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto firstPath = std::filesystem::path(directory.filePath("first.wav").toStdWString());
        const auto secondPath = std::filesystem::path(directory.filePath("second.wav").toStdWString());
        const auto metadataPath = std::filesystem::path(directory.filePath("metadata.wav").toStdWString());
        const std::vector<float> secondPcm(8'000, 0.25F);
        QVERIFY(writeFloatWav(firstPath, std::vector<float>(8'000, 0.125F)));
        QVERIFY(writeFloatWav(secondPath, secondPcm));
        QVERIFY(writeFloatWav(metadataPath, secondPcm));
        AudioEvent first{1, sourceFor(firstPath, 8'000), 0, 8'000, 0};
        AudioEvent second{2, sourceFor(secondPath, 8'000), 0, 8'000, 0};
        second.trackIndex = 5;
        WriteRequest request;
        request.snapshot = TimelineSnapshot{{first, second}, 8'000, 1};
        request.codec_name = "pcm_f32le";
        request.commit_mode = OutputCommitMode::Overwrite;
        request.metadata_source_path = metadataPath;
        for (const auto speed : {125.0, 100.0}) {
            TimePitchSession session;
            if (speed != 100) QVERIFY(session.setSpeedPercent(speed));
            for (const auto& target : {secondPath, metadataPath}) {
                request.output_path = target;
                const auto result = DocumentRenderPipeline{}.write(request, session);
                QVERIFY2(!result.ok(), "export overwrote an original source after replacing its snapshot with mixed PCM");
                QCOMPARE(result.error, WriteError::InvalidRequest);
                QCOMPARE(readFloatWav(target), secondPcm);
            }
        }
    }


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

    void sharedPipelineAppliesAutomationBeforeTimePitch()
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
        const auto baked = std::filesystem::path(directory.filePath("baked.wav").toStdWString());
        std::vector<float> bakedPcm(8'000, 0.5F);
        bakedPcm[4'000] = 0.0F;
        QVERIFY(writeFloatWav(baked, bakedPcm));
        EditorPlaybackParameters parameters;
        parameters.speed_ratio = 2.0;
        const auto expected = readRealtime(TimelineSnapshot{
            {AudioEvent{1, sourceFor(baked, frames), 0, frames, 0}}, frames, 1}, parameters);
        QCOMPARE(samples.size(), expected.size());
        for (std::size_t i = 0; i < samples.size(); ++i) compareNear(samples[i], expected[i]);
    }

    void realtimeAndOfflineMatchAutomatedPcmThroughFlush()
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
        EditorPlaybackParameters parameters;
        parameters.speed_ratio = 1.37;
        parameters.keep_pitch = true;
        const std::vector<float> realtime = readRealtime(snapshot, parameters);
        QVERIFY(!offline.empty());
        QCOMPARE(offline.size(), realtime.size());
        // Compare every sample, including silence and the final flush tail.
        for (std::size_t frame = 0; frame < realtime.size(); ++frame)
            compareNear(offline[frame], realtime[frame]);
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
