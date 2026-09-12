#include "audio_editor/audio_file_analyzer.hpp"
#include "audio_editor/editor_playback_stream.hpp"
#include "audio_editor/editor_player_bridge.hpp"
#include "audio_editor/automation_time_mapper.hpp"
#include "formant_preserver.hpp"
#include "time_pitch_engine.hpp"

#include <agplayer/c_api.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>

namespace {

class FailOnPutEngine final : public agplayer::ITimePitchEngine {
public:
    bool configure(int, int) override { return true; }
    bool setTempoRatio(double) override { return true; }
    bool setPitchCents(double) override { return true; }
    bool setRateRatio(double) override { return true; }
    bool setFormantPreservation(bool) override { return true; }
    void put(const float*, std::size_t) override { failed_ = true; }
    std::size_t receive(float*, std::size_t) override { return 0U; }
    void flush() override {}
    void reset() override { failed_ = false; }
    bool failed() const noexcept override { return failed_; }
    agplayer::TimePitchEngineKind kind() const noexcept override
    {
        return agplayer::TimePitchEngineKind::None;
    }

private:
    bool failed_{};
};

std::unique_ptr<agplayer::ITimePitchEngine> createFailOnPutEngine()
{
    return std::make_unique<FailOnPutEngine>();
}

[[noreturn]] void fail(const char* message)
{
    std::cerr << message << '\n';
    std::exit(1);
}

void require(const bool condition, const char* message)
{
    if (!condition) fail(message);
}

std::vector<float> readAll(agplayer::IAudioStreamSource& stream)
{
    std::vector<float> samples;
    agplayer::DecodedAudioBlock block;
    for (;;) {
        require(stream.read(block) == AG_OK, "editor stream read failed");
        samples.insert(samples.end(), block.samples.begin(), block.samples.end());
        if (block.end_of_stream) return samples;
    }
}

double crossingFrequency(const std::vector<float>& samples,
                         const int sampleRate, const int channels)
{
    std::size_t crossings = 0;
    const std::size_t frames = samples.size() / static_cast<std::size_t>(channels);
    for (std::size_t frame = 1; frame < frames; ++frame) {
        const float previous = samples[(frame - 1) * channels];
        const float current = samples[frame * channels];
        if (previous <= 0.0F && current > 0.0F) ++crossings;
    }
    return frames > 0
        ? static_cast<double>(crossings) * sampleRate / frames : 0.0;
}

std::vector<float> syntheticShiftedVowel(const int sampleRate,
                                         const std::size_t frames,
                                         const double pitchRatio)
{
    constexpr double fundamental = 100.0;
    constexpr std::array<double, 2> formants{700.0, 2'000.0};
    constexpr std::array<double, 2> widths{130.0, 260.0};
    std::vector<float> samples(frames, 0.0F);
    const double pi = std::acos(-1.0);
    for (double frequency = fundamental; frequency < 6'500.0;
         frequency += fundamental) {
        double amplitude = 0.015;
        for (std::size_t index = 0; index < formants.size(); ++index) {
            const double shiftedFormant = formants[index] * pitchRatio;
            const double distance = (frequency - shiftedFormant) / widths[index];
            amplitude += std::exp(-0.5 * distance * distance);
        }
        amplitude /= std::sqrt(frequency / fundamental);
        for (std::size_t frame = 0; frame < frames; ++frame) {
            samples[frame] += static_cast<float>(amplitude * std::sin(
                2.0 * pi * frequency * static_cast<double>(frame) / sampleRate));
        }
    }
    const float peak = *std::max_element(samples.begin(), samples.end(),
        [](const float left, const float right) {
            return std::abs(left) < std::abs(right);
        });
    const float scale = 0.8F / std::max(0.001F, std::abs(peak));
    for (float& sample : samples) sample *= scale;
    return samples;
}

double spectralCentroid(const std::vector<float>& samples, const int sampleRate,
                        const double lowHz, const double highHz)
{
    const std::size_t begin = samples.size() / 2U;
    const std::size_t window = std::min<std::size_t>(8'192U,
                                                     samples.size() - begin);
    const double pi = std::acos(-1.0);
    double weighted = 0.0;
    double energy = 0.0;
    for (double frequency = lowHz; frequency <= highHz; frequency += 25.0) {
        double real = 0.0;
        double imaginary = 0.0;
        for (std::size_t offset = 0; offset < window; ++offset) {
            const double phase = 2.0 * pi * frequency * offset / sampleRate;
            const double value = samples[begin + offset];
            real += value * std::cos(phase);
            imaginary -= value * std::sin(phase);
        }
        const double binEnergy = real * real + imaginary * imaginary;
        weighted += frequency * binEnergy;
        energy += binEnergy;
    }
    return energy > 0.0 ? weighted / energy : 0.0;
}

void formantPreserverRestoresControlledSpectralCentroids()
{
    constexpr int sampleRate = 48'000;
    constexpr double pitchRatio = 1.5;
    auto shifted = syntheticShiftedVowel(sampleRate, 24'000U, pitchRatio);
    auto protectedSamples = shifted;
    agplayer::FormantPreserver preserver(sampleRate, 1, pitchRatio);
    preserver.process(protectedSamples.data(), protectedSamples.size());

    for (const auto& band : std::array<std::array<double, 3>, 2>{
             std::array<double, 3>{450.0, 1'250.0, 700.0},
             std::array<double, 3>{1'400.0, 3'600.0, 2'000.0}}) {
        const double shiftedCentroid = spectralCentroid(
            shifted, sampleRate, band[0], band[1]);
        const double protectedCentroid = spectralCentroid(
            protectedSamples, sampleRate, band[0], band[1]);
        require(std::abs(protectedCentroid - band[2])
                    < std::abs(shiftedCentroid - band[2]) * 0.9,
                "formant-aware processing did not restore spectral centroid");
    }
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

std::filesystem::path writeRampFixture()
{
    constexpr std::uint32_t sampleRate = 44'100U;
    constexpr std::uint32_t frames = 200U;
    const auto path = std::filesystem::temp_directory_path()
        / "agplayer-editor-sample-exact.wav";
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write("RIFF", 4);
    writeU32(stream, 36U + frames * 2U);
    stream.write("WAVEfmt ", 8);
    writeU32(stream, 16U);
    writeU16(stream, 1U);
    writeU16(stream, 1U);
    writeU32(stream, sampleRate);
    writeU32(stream, sampleRate * 2U);
    writeU16(stream, 2U);
    writeU16(stream, 16U);
    stream.write("data", 4);
    writeU32(stream, frames * 2U);
    for (std::int32_t frame = 0; frame < static_cast<std::int32_t>(frames);
         ++frame) {
        const auto sample = static_cast<std::int16_t>(frame * 100 - 10'000);
        writeU16(stream, static_cast<std::uint16_t>(sample));
    }
    require(stream.good(), "could not write sample-exact fixture");
    return path;
}

std::filesystem::path writeAutomationOrderFixture()
{
    constexpr std::uint32_t sampleRate = 8'000U;
    constexpr std::uint32_t frames = 8'000U;
    const auto path = std::filesystem::temp_directory_path()
        / "agplayer-editor-automation-order.wav";
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write("RIFF", 4); writeU32(stream, 36U + frames * 4U);
    stream.write("WAVEfmt ", 8); writeU32(stream, 16U); writeU16(stream, 3U);
    writeU16(stream, 1U); writeU32(stream, sampleRate);
    writeU32(stream, sampleRate * 4U); writeU16(stream, 4U);
    writeU16(stream, 32U); stream.write("data", 4);
    writeU32(stream, frames * 4U);
    const float sample = 0.5F;
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        stream.write(reinterpret_cast<const char*>(&sample), sizeof(sample));
    }
    require(stream.good(), "could not write automation-order fixture");
    return path;
}

void automationRunsAfterTimePitchInRealtimeStream()
{
    using namespace agplayer::editor;
    const auto path = writeAutomationOrderFixture();
    const auto analysis = AudioFileAnalyzer::analyze(path, 64U);
    require(analysis.success, "automation-order fixture analysis failed");
    auto event = AudioDocument::fromSource(analysis.source)
        .timelineSnapshot().events.front();
    event.envelope = {{0, 1.0F}, {3'999, 1.0F}, {4'000, 0.0F},
                      {4'001, 1.0F}};
    const TimelineSnapshot snapshot{{event}, 8'000, 7};
    EditorPlaybackParameters parameters;
    parameters.speed_ratio = 2.0;
    std::string error;
    auto stream = EditorPlaybackStream::create(snapshot, parameters, error);
    require(stream != nullptr, "automation-order stream creation failed");
    const auto samples = readAll(*stream);
    require(samples.size() > 3'500U && samples.size() < 4'500U,
            "automation-order stream duration mismatch");
    const std::size_t mapped = samples.size() / 2U;
    float localMinimum = 1.0F;
    for (std::size_t index = mapped > 8U ? mapped - 8U : 0U;
         index < std::min(samples.size(), mapped + 9U); ++index) {
        localMinimum = std::min(localMinimum, std::abs(samples[index]));
    }
    require(localMinimum < 0.05F,
            "time/pitch swallowed a post-process one-frame gain notch");
    require(std::abs(samples[mapped - 32U]) > 0.25F
                && std::abs(samples[mapped + 32U]) > 0.25F,
            "post-time/pitch automation damaged neighboring audio");
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

std::chrono::nanoseconds renderMutedEventTimeline(
    const agplayer::editor::AudioSource& source, const std::size_t eventCount)
{
    using namespace agplayer::editor;
    const auto sharedSource = std::make_shared<const AudioSource>(source);
    std::vector<AudioEvent> events;
    events.reserve(eventCount);
    for (std::size_t index = 0; index < eventCount; ++index) {
        AudioEvent event{static_cast<EventId>(index + 1U), sharedSource,
                         0, 1, static_cast<SampleFrame>(index)};
        event.mute = true;
        events.push_back(std::move(event));
    }
    TimelineSnapshot snapshot{std::move(events),
                              static_cast<SampleFrame>(eventCount), 1};
    EditorPlaybackParameters parameters;
    std::string error;
    auto stream = EditorPlaybackStream::create(
        std::move(snapshot), parameters, error);
    require(stream != nullptr, "many-event stream creation failed");

    agplayer::DecodedAudioBlock block;
    std::size_t renderedFrames = 0;
    const auto started = std::chrono::steady_clock::now();
    do {
        require(stream->read(block) == AG_OK,
                "many-event stream read failed");
        renderedFrames += block.frames;
    } while (!block.end_of_stream);
    const auto elapsed = std::chrono::steady_clock::now() - started;
    require(renderedFrames == eventCount,
            "many-event stream frame count mismatch");
    return std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed);
}

void sequentialAutomationCursorSearchRemainsNearLinear()
{
    using namespace agplayer::editor;
    const auto path = writeAutomationOrderFixture();
    const auto analysis = AudioFileAnalyzer::analyze(path, 64U);
    require(analysis.success, "many-event fixture analysis failed");

    (void)renderMutedEventTimeline(analysis.source, 2'000U);
    const auto fastest = [&](const std::size_t eventCount) {
        auto best = std::chrono::nanoseconds::max();
        for (int attempt = 0; attempt < 3; ++attempt) {
            best = std::min(best,
                            renderMutedEventTimeline(analysis.source,
                                                     eventCount));
        }
        return best;
    };
    const auto small = fastest(100'000U);
    const auto large = fastest(400'000U);
    if (large >= small * 10) {
        std::cerr << "automation event lookup rescanned the timeline per "
                     "output block (100k="
                  << std::chrono::duration_cast<std::chrono::microseconds>(small).count()
                  << "us, 400k="
                  << std::chrono::duration_cast<std::chrono::microseconds>(large).count()
                  << "us)\n";
        std::exit(1);
    }

    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

void automationCursorRepositionsAfterBackwardSeek()
{
    using namespace agplayer::editor;
    const auto path = writeAutomationOrderFixture();
    const auto analysis = AudioFileAnalyzer::analyze(path, 64U);
    require(analysis.success, "automation seek fixture analysis failed");
    const auto source = std::make_shared<const AudioSource>(analysis.source);
    AudioEvent quiet{1U, source, 0, 4'000, 0};
    quiet.gain = 0.25F;
    AudioEvent loud{2U, source, 4'000, 8'000, 4'000};
    TimelineSnapshot snapshot{{quiet, loud}, 8'000, 1};
    EditorPlaybackParameters parameters;
    std::string error;
    auto stream = EditorPlaybackStream::create(snapshot, parameters, error);
    require(stream != nullptr, "automation seek stream creation failed");

    agplayer::DecodedAudioBlock block;
    require(stream->read(block) == AG_OK && block.frames == 4'096,
            "automation seek initial read failed");
    require(stream->seek(0) == AG_OK && stream->read(block) == AG_OK
                && block.frames > 0,
            "automation backward seek failed");
    require(std::abs(block.samples.front() - 0.125F) < 0.001F,
            "automation cursor did not return to the first event after seek");

    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

void awkward44100OffsetsRemainSampleExact()
{
    using namespace agplayer::editor;
    const auto path = writeRampFixture();
    const auto analysis = AudioFileAnalyzer::analyze(path, 32U);
    require(analysis.success, "sample-exact fixture analysis failed");
    const float frame45 = static_cast<float>(45 * 100 - 10'000) / 32'768.0F;

    auto trimmedDocument = AudioDocument::fromSource(analysis.source);
    const auto eventId = trimmedDocument.timelineSnapshot().events.front().id;
    require(trimmedDocument.trimEvent(eventId, 45, 100, 0),
            "sample-exact event trim failed");
    EditorPlaybackParameters neutral;
    std::string error;
    auto eventStream = EditorPlaybackStream::create(
        trimmedDocument.timelineSnapshot(), neutral, error);
    agplayer::DecodedAudioBlock block;
    require(eventStream && eventStream->read(block) == AG_OK && block.frames > 0,
            "sample-exact event read failed");
    require(std::abs(block.samples.front() - frame45) < 0.0001F,
            "awkward event source offset skipped one PCM frame");

    auto seekStream = EditorPlaybackStream::create(
        AudioDocument::fromSource(analysis.source).timelineSnapshot(), neutral,
        error);
    require(seekStream && seekStream->seek(1) == AG_OK
                && seekStream->read(block) == AG_OK && block.frames > 0,
            "sample-exact seek failed");
    require(std::abs(block.samples.front() - frame45) < 0.0001F,
            "1ms seek at 44.1kHz landed one PCM frame early");

    auto automatedEvent = AudioDocument::fromSource(analysis.source)
        .timelineSnapshot().events.front();
    automatedEvent.envelope = {{44, 0.0F}, {45, 1.0F}};
    auto automatedSeek = EditorPlaybackStream::create(
        TimelineSnapshot{{automatedEvent}, 200, 1}, neutral, error);
    require(automatedSeek && automatedSeek->seek(1) == AG_OK
                && automatedSeek->read(block) == AG_OK && block.frames > 0,
            "sample-exact automated seek failed");
    require(std::abs(block.samples.front() - frame45) < 0.0001F,
            "seek applied automation from the preceding 44.1kHz frame");

    for (const double ratio : {0.5, 1.25, 2.0}) {
        AutomationTimeMapper mapper(0, 200, ratio);
        constexpr SampleFrame outputFrame = 45;
        const SampleFrame rawFrame = static_cast<SampleFrame>(std::ceil(
            44.1L * static_cast<long double>(ratio)));
        mapper.resetAnchor(outputFrame, rawFrame);
        require(mapper.map(outputFrame) == rawFrame,
                "seek automation anchor did not match raw PCM frame");
    }
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

void multiEventProcessingAndDecodeFailuresAreCovered()
{
    using namespace agplayer::editor;
    const auto path = writeRampFixture();
    const auto analysis = AudioFileAnalyzer::analyze(path, 32U);
    require(analysis.success, "multi-event fixture analysis failed");
    const auto source = std::make_shared<const AudioSource>(analysis.source);
    AudioEvent shaped{1U, source, 40, 50, 0};
    shaped.gain = 0.5F;
    shaped.fadeIn = 3;
    shaped.fadeOut = 2;
    shaped.envelope = {{5, 0.5F}};
    AudioEvent muted{2U, source, 50, 60, 15};
    muted.mute = true;
    AudioEvent gained{3U, source, 60, 65, 30};
    gained.gain = 0.25F;
    auto snapshot = AudioDocument::fromEvents(
        {shaped, muted, gained}).timelineSnapshot();
    EditorPlaybackParameters neutral;
    std::string error;
    auto stream = EditorPlaybackStream::create(snapshot, neutral, error);
    require(stream != nullptr, "multi-event stream creation failed");
    const auto samples = readAll(*stream);
    require(samples.size() == 35U, "multi-event duration mismatch");
    require(std::abs(samples[0]) < 0.00001F
                && std::abs(samples[9]) < 0.00001F,
            "event fades were not applied at exact boundaries");
    require(std::abs(samples[2]) > std::abs(samples[5]),
            "gain envelope was not applied across the event");
    require(std::all_of(samples.begin() + 10, samples.begin() + 30,
                        [](const float value) {
                            return std::abs(value) < 0.00001F;
                        }),
            "timeline gaps or muted event emitted non-silent PCM");
    const float expectedGain = static_cast<float>(60 * 100 - 10'000)
        / 32'768.0F * 0.25F;
    require(std::abs(samples[30] - expectedGain) < 0.0001F,
            "event gain was not applied to the final event");

    auto missing = std::make_shared<AudioSource>(analysis.source);
    missing->path = path.parent_path() / "agplayer-editor-missing-source.wav";
    AudioEvent unavailable{4U, missing, 0, 10, 0};
    auto failedStream = EditorPlaybackStream::create(
        AudioDocument::fromEvents({unavailable}).timelineSnapshot(), neutral,
        error);
    agplayer::DecodedAudioBlock block;
    require(failedStream && failedStream->read(block) == AG_DECODE_ERROR,
            "missing event source did not surface a decode failure");
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

} // namespace

int main(int argc, char** argv)
{
    using namespace agplayer::editor;
    require(argc == 2, "fixture path argument missing");
    formantPreserverRestoresControlledSpectralCentroids();
    automationRunsAfterTimePitchInRealtimeStream();
    sequentialAutomationCursorSearchRemainsNearLinear();
    automationCursorRepositionsAfterBackwardSeek();
    awkward44100OffsetsRemainSampleExact();
    multiEventProcessingAndDecodeFailuresAreCovered();
    const auto analysis = AudioFileAnalyzer::analyze(
        std::filesystem::u8path(argv[1]), 64);
    require(analysis.success, "fixture analysis failed");

    const auto snapshot = AudioDocument::fromSource(
        analysis.source).timelineSnapshot();
    EditorPlaybackParameters neutral;
    std::string error;
    auto original = EditorPlaybackStream::create(snapshot, neutral, error);
    require(original != nullptr, "neutral editor stream creation failed");
    const auto originalSamples = readAll(*original);
    require(!originalSamples.empty(), "neutral editor stream returned no audio");

    auto timestamped = EditorPlaybackStream::create(snapshot, neutral, error);
    require(timestamped != nullptr,
            "timestamp editor stream creation failed");
    agplayer::DecodedAudioBlock firstTimestamped;
    agplayer::DecodedAudioBlock secondTimestamped;
    require(timestamped->read(firstTimestamped) == AG_OK
                && firstTimestamped.frames > 0
                && timestamped->read(secondTimestamped) == AG_OK
                && secondTimestamped.frames > 0,
            "timestamp editor stream reads failed");
    require(firstTimestamped.timestamp_frame == 0
                && secondTimestamped.timestamp_frame
                    == static_cast<std::int64_t>(firstTimestamped.frames),
            "editor stream frame timestamps were not continuous");

    EditorPlaybackParameters failingParameters;
    failingParameters.speed_ratio = 0.75;
    auto failingStream = EditorPlaybackStream::create(
        snapshot, failingParameters, error, &createFailOnPutEngine);
    require(failingStream != nullptr,
            "injected-failure editor stream creation failed");
    agplayer::DecodedAudioBlock failingBlock;
    require(failingStream->read(failingBlock) == AG_INTERNAL_ERROR,
            "editor time/pitch failure was silently treated as EOS");

    EditorPlaybackParameters pitched;
    pitched.pitch_cents = 700;
    auto shifted = EditorPlaybackStream::create(snapshot, pitched, error);
    require(shifted != nullptr, "pitched editor stream creation failed");
    const auto shiftedSamples = readAll(*shifted);
    const double shiftedFrequency = crossingFrequency(
        shiftedSamples, shifted->metadata().sample_rate,
        shifted->metadata().channels);
    require(std::abs(shiftedFrequency - 659.3) < 40.0,
            "editor playback pitch path did not change audible frequency");

    pitched.formant_preservation = true;
    auto protectedStream = EditorPlaybackStream::create(snapshot, pitched, error);
    require(protectedStream != nullptr,
            "formant-protected editor stream creation failed");
    const auto protectedSamples = readAll(*protectedStream);
    require(protectedSamples.size() == shiftedSamples.size(),
            "formant path unexpectedly changed playback duration");
    require(!std::equal(protectedSamples.begin(), protectedSamples.end(),
                        shiftedSamples.begin()),
            "formant control did not alter editor playback samples");

    EditorPlaybackParameters faster;
    faster.speed_ratio = 1.5;
    faster.keep_pitch = true;
    auto spedUp = EditorPlaybackStream::create(snapshot, faster, error);
    require(spedUp != nullptr, "tempo editor stream creation failed");
    const auto spedUpSamples = readAll(*spedUp);
    require(spedUpSamples.size() < originalSamples.size() * 3U / 4U,
            "tempo path did not shorten editor playback");
    require(std::abs(crossingFrequency(
                spedUpSamples, spedUp->metadata().sample_rate,
                spedUp->metadata().channels) - 440.0) < 30.0,
            "tempo path failed to preserve pitch");

    faster.keep_pitch = false;
    auto naturalSpeed = EditorPlaybackStream::create(snapshot, faster, error);
    require(naturalSpeed != nullptr,
            "rate-based editor stream creation failed");
    const auto naturalSamples = readAll(*naturalSpeed);
    require(naturalSamples.size() < originalSamples.size() * 3U / 4U,
            "non-preserving speed path did not shorten editor playback");
    require(std::abs(crossingFrequency(
                naturalSamples, naturalSpeed->metadata().sample_rate,
                naturalSpeed->metadata().channels) - 660.0) < 40.0,
            "non-preserving speed path did not raise audible pitch");

    require(spedUp->seek(250) == AG_OK, "editor stream seek failed");
    agplayer::DecodedAudioBlock afterSeek;
    require(spedUp->read(afterSeek) == AG_OK && afterSeek.frames > 0,
            "editor stream did not resume after seek");
    require(afterSeek.timestamp_frame
                == static_cast<std::int64_t>(
                    spedUp->metadata().sample_rate / 4),
            "editor stream seek did not publish its output-frame timestamp");

    ag_player_config config{};
    config.backend = AG_AUDIO_BACKEND_NULL;
    config.buffer_frames = 4'096;
    ag_player* player = nullptr;
    require(ag_player_create_with_config(&config, &player) == AG_OK,
            "shared output player creation failed");
    auto outputStream = EditorPlaybackStream::create(snapshot, neutral, error);
    require(outputStream != nullptr
                && load_editor_playback_stream(player, outputStream) == AG_OK,
            "AudioEngine rejected editor timeline stream");
    require(ag_player_play(player) == AG_OK,
            "AudioEngine failed to play editor timeline stream");
    ag_playback_snapshot playback{};
    require(ag_player_snapshot(player, &playback) == AG_OK
                && playback.state == AG_PLAYING,
            "editor timeline did not own the existing AudioEngine output");
    require(ag_player_seek(player, 250) == AG_OK,
            "AudioEngine failed to seek editor timeline stream");
    require(ag_player_stop(player) == AG_OK,
            "AudioEngine failed to stop editor timeline stream");
    ag_player_destroy(player);
    return 0;
}
