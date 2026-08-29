#include "audio_editor/audio_document.hpp"
#include "audio_editor/time_pitch_session.hpp"
#include "decoder.hpp"

#include <agplayer/c_api.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
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

} // namespace

int main(const int argc, char** argv)
{
    require(argc == 2, "fixture argument required");
    namespace fs = std::filesystem;
    using namespace agplayer::editor;

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
