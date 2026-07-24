#undef NDEBUG

#include "transcoder.hpp"

#include "decoder.hpp"

#include <cmath>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

std::filesystem::path make_corrupt_adpcm_wav(const std::filesystem::path& source)
{
    const std::filesystem::path dest =
        source.parent_path() / "corrupt-adpcm.wav";
    std::filesystem::remove(dest);
    std::filesystem::copy_file(
        source, dest, std::filesystem::copy_options::overwrite_existing);

    std::fstream file(dest, std::ios::in | std::ios::out | std::ios::binary);
    assert(file);
    file.seekp(20, std::ios::beg);
    const char tag[] = {static_cast<char>(0x02), static_cast<char>(0x00)};
    file.write(tag, sizeof(tag));
    assert(file);
    return dest;
}

float scan_peak(const std::filesystem::path& path)
{
    agplayer::Decoder decoder;
    if (decoder.open(path.string()) != AG_OK) {
        return -1.0f;
    }
    agplayer::DecodedAudioBlock block;
    float peak = 0.0f;
    do {
        if (decoder.read(block) != AG_OK) {
            return -1.0f;
        }
        for (float sample : block.samples) {
            const float abs_sample = std::abs(sample);
            if (abs_sample > peak) {
                peak = abs_sample;
            }
        }
    } while (!block.end_of_stream);
    return peak;
}

} // namespace

int main(const int argc, char** argv)
{
    assert(argc == 2);
    const std::filesystem::path input_path = argv[1];

    // Happy path: valid sine wave transcodes successfully.
    const std::filesystem::path happy_output =
        input_path.parent_path() / "transcoder-out.wav";
    std::filesystem::remove(happy_output);

    agplayer::TranscodeConfig happy_config;
    happy_config.output_path = happy_output.string();
    happy_config.codec_name = "pcm_s16le";
    std::string error;
    ag_result result =
        agplayer::transcode(input_path.string(), happy_config,
                            nullptr, nullptr, error);
    if (result != AG_OK) {
        std::cerr << "transcode happy path failed: " << static_cast<int>(result)
                  << " " << error << "\n";
    }
    assert(result == AG_OK);
    assert(std::filesystem::exists(happy_output));

    // Failure path: corrupted ADPCM-tagged WAV triggers avcodec_send_packet
    // failure. The tool must return a non-AG_OK status and must not leave a
    // complete/successful output file.
    const std::filesystem::path corrupt_path = make_corrupt_adpcm_wav(input_path);
    const std::filesystem::path failure_output =
        corrupt_path.parent_path() / "transcoder-out.wav";
    std::filesystem::remove(failure_output);

    agplayer::TranscodeConfig failure_config;
    failure_config.output_path = failure_output.string();
    failure_config.codec_name = "pcm_s16le";
    error.clear();
    result = agplayer::transcode(corrupt_path.string(), failure_config,
                                 nullptr, nullptr, error);
    if (result == AG_OK) {
        std::cerr << "expected transcode failure for corrupted input\n";
    }
    assert(result != AG_OK);
    assert(!std::filesystem::exists(failure_output));

    // Invalid argument: input and output paths must differ.
    const std::filesystem::path collision_path =
        input_path.parent_path() / "transcoder-collision.wav";
    std::filesystem::remove(collision_path);
    std::filesystem::copy_file(input_path, collision_path);

    agplayer::TranscodeConfig collision_config;
    collision_config.output_path = collision_path.string();
    collision_config.codec_name = "pcm_s16le";
    error.clear();
    result = agplayer::transcode(collision_path.string(), collision_config,
                                 nullptr, nullptr, error);
    if (result != AG_INVALID_ARGUMENT) {
        std::cerr << "expected AG_INVALID_ARGUMENT for input/output collision, got "
                  << static_cast<int>(result) << " " << error << "\n";
    }
    assert(result == AG_INVALID_ARGUMENT);
    std::filesystem::remove(collision_path);

    // Volume normalize: output peak should approach -1 dBFS for a quiet fixture.
    // For sine fixture (already loud), gain should be capped at 1.0.
    const std::filesystem::path normalize_output =
        input_path.parent_path() / "transcoder-normalized.wav";
    std::filesystem::remove(normalize_output);
    agplayer::TranscodeConfig normalize_config;
    normalize_config.output_path = normalize_output.string();
    normalize_config.codec_name = "pcm_s16le";
    normalize_config.volume_normalize = true;
    error.clear();
    result = agplayer::transcode(input_path.string(), normalize_config,
                                 nullptr, nullptr, error);
    if (result != AG_OK) {
        std::cerr << "transcode volume normalize failed: "
                  << static_cast<int>(result) << " " << error << "\n";
    }
    assert(result == AG_OK);
    assert(std::filesystem::exists(normalize_output));
    const float input_peak = scan_peak(input_path);
    const float normalized_peak = scan_peak(normalize_output);
    assert(input_peak > 0.0f);
    assert(normalized_peak > 0.0f);
    assert(normalized_peak <= input_peak * 1.01f);
    std::filesystem::remove(normalize_output);

    std::filesystem::remove(happy_output);
    std::filesystem::remove(corrupt_path);
    return 0;
}
