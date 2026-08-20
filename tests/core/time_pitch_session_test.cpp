#include "audio_editor/audio_document.hpp"
#include "audio_editor/time_pitch_session.hpp"
#include "decoder.hpp"

#include <agplayer/c_api.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>

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
    parameters.setOriginalBpm(100.0);
    require(parameters.setTargetBpm(125.0), "target BPM rejected");
    require(std::abs(parameters.speedPercent() - 125.0) < 0.001,
            "target BPM did not update speed");
    require(parameters.setSpeedPercent(80.0), "speed rejected");
    require(std::abs(parameters.targetBpm() - 80.0) < 0.001,
            "speed did not update target BPM");
    require(parameters.setPitch(3, 25), "pitch rejected");
    require(parameters.pitchCents() == 325, "pitch conversion mismatch");

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
    fs::remove(output, ignored);
    return 0;
}
