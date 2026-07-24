#undef NDEBUG

#include "pitch_shifter.hpp"

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

} // namespace

int main(const int argc, char** argv)
{
    assert(argc == 2);
    const std::filesystem::path input_path = argv[1];

    // Happy path: valid sine wave pitch-shifts successfully.
    const std::filesystem::path happy_output =
        input_path.parent_path() / "pitch-shifter-out.wav";
    std::filesystem::remove(happy_output);

    agplayer::PitchShiftConfig happy_config;
    happy_config.pitch_cents = 100;
    happy_config.keep_tempo = true;
    happy_config.tempo_ratio = 1.0;
    happy_config.output_path = happy_output.string();
    happy_config.output_codec_name = "pcm_s16le";
    std::string error;
    ag_result result =
        agplayer::pitch_shift(input_path.string(), happy_config,
                              nullptr, nullptr, error);
    if (result != AG_OK) {
        std::cerr << "pitch_shift happy path failed: " << static_cast<int>(result)
                  << " " << error << "\n";
    }
    assert(result == AG_OK);
    assert(std::filesystem::exists(happy_output));

    // Failure path: corrupted ADPCM-tagged WAV triggers avcodec_send_packet
    // failure. The tool must return a non-AG_OK status and must not leave a
    // complete/successful output file.
    const std::filesystem::path corrupt_path = make_corrupt_adpcm_wav(input_path);
    const std::filesystem::path failure_output =
        corrupt_path.parent_path() / "pitch-shifter-out.wav";
    std::filesystem::remove(failure_output);

    agplayer::PitchShiftConfig failure_config;
    failure_config.pitch_cents = 100;
    failure_config.keep_tempo = true;
    failure_config.tempo_ratio = 1.0;
    failure_config.output_path = failure_output.string();
    failure_config.output_codec_name = "pcm_s16le";
    error.clear();
    result = agplayer::pitch_shift(corrupt_path.string(), failure_config,
                                   nullptr, nullptr, error);
    if (result == AG_OK) {
        std::cerr << "expected pitch_shift failure for corrupted input\n";
    }
    assert(result != AG_OK);
    assert(!std::filesystem::exists(failure_output));

    std::filesystem::remove(happy_output);
    std::filesystem::remove(corrupt_path);
    return 0;
}
