#include "audio_editor/audio_document.hpp"
#include "audio_editor/time_pitch_session.hpp"
#include "decoder.hpp"
#include "time_pitch_engine.hpp"

#include <agplayer/c_api.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <vector>

namespace {

[[noreturn]] void fail(const char* message)
{
    std::cerr << message << '\n';
    std::exit(1);
}

void require(const bool condition, const char* message)
{
    if (!condition) fail(message);
}

agplayer::editor::SampleFrame decoded_frames(
    const std::filesystem::path& path, int sample_rate, int channels)
{
    agplayer::Decoder decoder;
    require(decoder.open(path.u8string(), sample_rate, channels) == AG_OK,
            "decoder open failed");
    agplayer::DecodedAudioBlock block;
    agplayer::editor::SampleFrame frames = 0;
    do {
        require(decoder.read(block) == AG_OK, "decoder read failed");
        frames += static_cast<agplayer::editor::SampleFrame>(block.frames);
    } while (!block.end_of_stream);
    return frames;
}

std::vector<float> sine(const int sampleRate, const std::size_t frames,
                        const double frequency)
{
    std::vector<float> result(frames);
    const double step = 2.0 * std::acos(-1.0) * frequency / sampleRate;
    for (std::size_t frame = 0; frame < frames; ++frame) {
        result[frame] = static_cast<float>(0.6 * std::sin(step * frame));
    }
    return result;
}

std::vector<float> receiveAvailable(agplayer::ITimePitchEngine& engine,
                                    const std::size_t capacity = 4'096U)
{
    std::vector<float> buffer(capacity);
    const std::size_t frames = engine.receive(buffer.data(), capacity);
    buffer.resize(frames);
    return buffer;
}

std::vector<float> processStream(const std::vector<float>& input,
                                 const double tempo, const double pitchCents,
                                 const double rate, const std::size_t chunk,
                                 const bool formantPreservation = false)
{
    auto engine = agplayer::create_time_pitch_engine();
    require(engine != nullptr, "time/pitch engine factory returned null");
    require(engine->configure(48'000, 1), "time/pitch engine configure failed");
    require(engine->setTempoRatio(tempo), "time/pitch engine tempo rejected");
    require(engine->setPitchCents(pitchCents), "time/pitch engine pitch rejected");
    require(engine->setRateRatio(rate), "time/pitch engine rate rejected");
    require(engine->setFormantPreservation(formantPreservation),
            "time/pitch engine formant setting rejected");

    std::vector<float> output;
    for (std::size_t offset = 0; offset < input.size(); offset += chunk) {
        const std::size_t frames = std::min(chunk, input.size() - offset);
        engine->put(input.data() + offset, frames);
        const auto available = receiveAvailable(*engine);
        output.insert(output.end(), available.begin(), available.end());
    }
    engine->flush();
    for (;;) {
        const auto available = receiveAvailable(*engine);
        if (available.empty()) break;
        output.insert(output.end(), available.begin(), available.end());
    }
    return output;
}

double crossingFrequency(const std::vector<float>& samples)
{
    std::size_t crossings = 0U;
    for (std::size_t index = 1; index < samples.size(); ++index) {
        if (samples[index - 1U] <= 0.0F && samples[index] > 0.0F) {
            ++crossings;
        }
    }
    return samples.empty() ? 0.0
        : static_cast<double>(crossings) * 48'000.0 / samples.size();
}

double alignedCorrelation(const std::vector<float>& left,
                          const std::vector<float>& right)
{
    const std::size_t start = 2'048U;
    const std::size_t count = std::min(left.size(), right.size()) - start * 2U;
    long double leftEnergy = 0.0L;
    long double rightEnergy = 0.0L;
    long double product = 0.0L;
    for (std::size_t index = 0U; index < count; ++index) {
        const long double leftValue = left[start + index];
        const long double rightValue = right[start + index];
        leftEnergy += leftValue * leftValue;
        rightEnergy += rightValue * rightValue;
        product += leftValue * rightValue;
    }
    return static_cast<double>(product / std::sqrt(leftEnergy * rightEnergy));
}

void timePitchEngineRealtimeContract()
{
    const auto input = sine(48'000, 48'000U, 440.0);
    auto bypass = agplayer::create_time_pitch_engine();
    require(bypass != nullptr && bypass->configure(48'000, 1),
            "neutral engine configuration failed");
    require(bypass->setTempoRatio(1.0) && bypass->setPitchCents(0.0)
                && bypass->setRateRatio(1.0),
            "neutral engine parameter setup failed");
    bypass->put(input.data(), 257U);
    const auto immediate = receiveAvailable(*bypass, 257U);
    require(immediate.size() == 257U,
            "neutral realtime path withheld the first PCM block");
    require(immediate == std::vector<float>(input.begin(), input.begin() + 257),
            "neutral realtime path did not preserve samples exactly");

    const auto slow = processStream(input, 0.75, 0.0, 1.0, 257U);
    const auto fast = processStream(input, 1.5, 0.0, 1.0, 257U);
    require(std::llabs(static_cast<long long>(slow.size()) - 64'000LL) < 2'048LL,
            "0.75 tempo output duration is outside tolerance");
    require(std::llabs(static_cast<long long>(fast.size()) - 32'000LL) < 2'048LL,
            "1.5 tempo output duration is outside tolerance");

    for (const std::size_t frames : {5'000U, 8'192U}) {
        const std::vector<float> shortInput(input.begin(), input.begin() + frames);
        const auto quarterSpeed = processStream(shortInput, 0.5, 0.0, 0.5,
                                                frames);
        require(!quarterSpeed.empty(),
                "quarter-speed flush returned no output for a short stream");
        const long long expected = static_cast<long long>(frames * 4U);
        require(std::llabs(static_cast<long long>(quarterSpeed.size()) - expected)
                    < 2'048LL,
                "quarter-speed flush duration is outside tolerance");
    }

    const auto raised = processStream(input, 1.0, 1'200.0, 1.0, 257U);
    const auto lowered = processStream(input, 1.0, -1'200.0, 1.0, 257U);
    require(std::abs(crossingFrequency(raised) - 880.0) < 80.0,
            "+12 semitone output frequency is outside tolerance");
    require(std::abs(crossingFrequency(lowered) - 220.0) < 45.0,
            "-12 semitone output frequency is outside tolerance");

    const auto realtime = processStream(input, 1.25, 300.0, 1.0, 257U);
    const auto offline = processStream(input, 1.25, 300.0, 1.0, input.size());
    require(!realtime.empty() && !offline.empty(),
            "processed output was unexpectedly empty");
    const auto finiteBounded = [](const float sample) {
        return std::isfinite(sample) && std::abs(sample) <= 1.5F;
    };
    require(std::all_of(realtime.begin(), realtime.end(), finiteBounded)
                && std::all_of(offline.begin(), offline.end(), finiteBounded),
            "processed output contains non-finite or unbounded samples");
    require(std::llabs(static_cast<long long>(realtime.size())
                       - static_cast<long long>(offline.size())) < 256LL,
            "realtime and offline processing diverged across chunk boundaries");
    require(alignedCorrelation(realtime, offline) > 0.85,
            "realtime and offline processing diverged in waveform shape");

    const auto plainFormants = processStream(input, 1.0, 700.0, 1.0, 257U);
    const auto protectedFormants = processStream(input, 1.0, 700.0, 1.0,
                                                 257U, true);
    require(std::all_of(protectedFormants.begin(), protectedFormants.end(),
                        finiteBounded),
            "formant-protected engine output is non-finite or unbounded");
    require(plainFormants.size() == protectedFormants.size()
                && !std::equal(plainFormants.begin(), plainFormants.end(),
                               protectedFormants.begin()),
            "formant toggle did not change the unified engine output");

    auto resetEngine = agplayer::create_time_pitch_engine();
    require(resetEngine != nullptr && resetEngine->configure(48'000, 1)
                && resetEngine->setTempoRatio(1.25)
                && resetEngine->setPitchCents(0.0)
                && resetEngine->setRateRatio(1.0),
            "reset engine setup failed");
    resetEngine->put(input.data(), 4'096U);
    resetEngine->flush();
    while (!receiveAvailable(*resetEngine).empty()) {
    }
    resetEngine->reset();
    const std::vector<float> silence(512U, 0.0F);
    resetEngine->put(silence.data(), silence.size());
    resetEngine->flush();
    const auto afterReset = receiveAvailable(*resetEngine, 4'096U);
    require(std::all_of(afterReset.begin(), afterReset.end(), [](const float value) {
        return std::abs(value) < 0.0001F;
    }), "reset leaked previous input into a new stream");
}

void signalsmithFifoBackpressureIsObservable()
{
    auto engine = agplayer::create_signalsmith_time_pitch_engine();
    require(engine != nullptr && engine->configure(48'000, 1),
            "Signalsmith engine configuration failed");
    const std::vector<float> overflow(131'073U, 0.25F);
    engine->put(overflow.data(), overflow.size());
    require(engine->failed(),
            "Signalsmith FIFO overflow was not exposed to its caller");
}

} // namespace

int main(const int argc, char** argv)
{
    require(argc == 2, "fixture argument required");
    namespace fs = std::filesystem;
    using namespace agplayer::editor;

    timePitchEngineRealtimeContract();
    signalsmithFifoBackpressureIsObservable();

    TimePitchSession parameters;
    require(parameters.setTargetBpm(130.0),
            "first target BPM did not establish a baseline");
    require(std::abs(parameters.originalBpm() - 130.0) < 0.001,
            "first target BPM baseline mismatch");
    require(std::abs(parameters.speedPercent() - 100.0) < 0.001,
            "first target BPM did not retain normal speed");
    TimePitchSession invalidBaseline;
    invalidBaseline.setOriginalBpm(100.0);
    require(invalidBaseline.setSpeedPercent(125.0),
            "invalid-baseline setup speed rejected");
    invalidBaseline.setOriginalBpm(0.0);
    require(invalidBaseline.originalBpm() == 0.0
                && invalidBaseline.targetBpm() == 0.0,
            "invalid original BPM did not clear BPM state");
    require(std::abs(invalidBaseline.speedPercent() - 100.0) < 0.001,
            "invalid original BPM did not restore normal speed");
    parameters.setOriginalBpm(100.0);
    require(parameters.setTargetBpm(125.0), "target BPM rejected");
    require(std::abs(parameters.speedPercent() - 125.0) < 0.001,
            "target BPM did not update speed");
    require(parameters.setSpeedPercent(80.0), "speed rejected");
    require(std::abs(parameters.targetBpm() - 80.0) < 0.001,
            "speed did not update target BPM");
    require(parameters.setPitch(3, 25), "pitch rejected");
    require(parameters.pitchCents() == 325, "pitch conversion mismatch");
    require(!parameters.setPitch(3, 25),
            "unchanged pitch was reported as a processing change");
    require(!parameters.setSpeedPercent(80.0),
            "unchanged speed was reported as a processing change");
    require(!parameters.setTargetBpm(80.0),
            "unchanged target BPM was reported as a processing change");
    parameters.setFormantPreservation(true);
    require(parameters.formantPreservation(),
            "formant preservation state was not retained");

    const fs::path input = fs::u8path(argv[1]);
    agplayer::Decoder probe;
    require(probe.open(input.u8string()) == AG_OK, "fixture probe failed");
    const agplayer::MediaMetadata metadata = probe.metadata();
    probe.close();
    const SampleFrame input_frames = decoded_frames(
        input, metadata.sample_rate, metadata.channels);
    const AudioDocument document = AudioDocument::fromSource(AudioSource{
        input, static_cast<std::uint32_t>(metadata.sample_rate),
        static_cast<std::uint32_t>(metadata.channels), input_frames});
    const auto before = document.timelineSnapshot();

    TimePitchSession processor;
    require(processor.setSpeedPercent(125.0), "processing speed rejected");
    processor.setKeepPitch(true);
    const fs::path output = input.parent_path() / "time-pitch-output.wav";
    std::error_code ignored;
    fs::remove(output, ignored);
    const TimePitchResult result = processor.process(
        document.timelineSnapshot(), output, std::nullopt);
    if (!result.success) std::cerr << result.message << '\n';
    require(result.success, "time/pitch processing failed");
    require(document.timelineSnapshot().revision == before.revision,
            "preview mutated document");
    const SampleFrame output_frames = decoded_frames(
        output, metadata.sample_rate, metadata.channels);
    const SampleFrame expected = static_cast<SampleFrame>(
        std::llround(static_cast<double>(input_frames) / 1.25));
    require(std::llabs(output_frames - expected) <= 2'048,
            "processed duration ratio mismatch");

    TimePitchSession unprotected;
    require(unprotected.setPitch(7, 0), "unprotected pitch rejected");
    const fs::path unprotected_output = input.parent_path()
        / "time-pitch-unprotected.wav";
    fs::remove(unprotected_output, ignored);
    const TimePitchResult unprotected_result = unprotected.process(
        document.timelineSnapshot(), unprotected_output, std::nullopt);
    require(unprotected_result.success, "unprotected processing failed");

    TimePitchSession protected_session;
    require(protected_session.setPitch(7, 0), "protected pitch rejected");
    protected_session.setFormantPreservation(true);
    const fs::path protected_output = input.parent_path()
        / "time-pitch-protected.wav";
    fs::remove(protected_output, ignored);
    const TimePitchResult protected_result = protected_session.process(
        document.timelineSnapshot(), protected_output, std::nullopt);
    require(protected_result.success, "protected processing failed");

    const auto read_bytes = [](const fs::path& path) {
        std::ifstream stream(path, std::ios::binary);
        return std::vector<char>(std::istreambuf_iterator<char>(stream), {});
    };
    const auto unprotected_bytes = read_bytes(unprotected_output);
    const auto protected_bytes = read_bytes(protected_output);
    require(!unprotected_bytes.empty() && !protected_bytes.empty(),
            "formant comparison output missing");
    require(unprotected_bytes != protected_bytes,
            "formant preservation did not change processed audio");
    fs::remove(output, ignored);
    fs::remove(unprotected_output, ignored);
    fs::remove(protected_output, ignored);
    return 0;
}
