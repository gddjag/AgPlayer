#include "audio_editor/document_renderer.hpp"

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
