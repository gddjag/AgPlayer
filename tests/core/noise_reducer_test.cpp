#include "audio_editor/audio_document.hpp"
#include "audio_editor/audio_file_analyzer.hpp"
#include "audio_editor/noise_reducer.hpp"
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

double rms(const std::filesystem::path& path)
{
    agplayer::Decoder decoder;
    require(decoder.open(path.u8string()) == AG_OK, "decoder open failed");
    agplayer::DecodedAudioBlock block;
    long double sum = 0.0;
    std::uint64_t count = 0;
    do {
        require(decoder.read(block) == AG_OK, "decoder read failed");
        for (const float sample : block.samples) {
            sum += static_cast<long double>(sample) * sample;
            ++count;
        }
    } while (!block.end_of_stream);
    return count == 0 ? 0.0 : std::sqrt(static_cast<double>(sum / count));
}

} // namespace

int main(const int argc, char** argv)
{
    require(argc == 2, "fixture argument required");
    namespace fs = std::filesystem;
    using namespace agplayer::editor;
    const fs::path input = fs::u8path(argv[1]);
    const AudioFileAnalysis analysis = AudioFileAnalyzer::analyze(input, 64);
    require(analysis.success, "fixture analysis failed");
    const AudioDocument document = AudioDocument::fromSource(analysis.source);
    const fs::path output = fs::temp_directory_path()
        / "agplayer-noise-reducer-test.wav";
    std::error_code ignored;
    fs::remove(output, ignored);

    float last_progress = 0.0F;
    const NoiseReductionResult result = NoiseReducer::reduce(
        document.timelineSnapshot(), std::nullopt, output, nullptr,
        [&last_progress](const float value) { last_progress = value; });
    require(result.success, "noise reduction failed");
    require(fs::exists(output), "noise reduction output missing");
    require(result.frames == analysis.source.total_frames,
            "noise reduction changed frame count");
    require(last_progress == 1.0F, "noise reduction did not finish progress");
    require(rms(output) < rms(input) * 0.95,
            "noise reduction did not alter the rendered signal");
    fs::remove(output, ignored);
    return 0;
}
