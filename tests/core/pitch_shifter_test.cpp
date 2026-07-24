#undef NDEBUG

#include "pitch_shifter.hpp"

#include <cassert>
#include <filesystem>
#include <iostream>
#include <string>

int main(const int argc, char** argv)
{
    assert(argc == 2);
    const std::filesystem::path input_path = argv[1];

    const std::filesystem::path output_path =
        input_path.parent_path() / "pitch-shifter-out.wav";
    std::filesystem::remove(output_path);

    agplayer::PitchShiftConfig config;
    config.pitch_cents = 100;
    config.keep_tempo = true;
    config.tempo_ratio = 1.0;
    config.output_path = output_path.string();
    config.output_codec_name = "pcm_s16le";
    std::string error;
    const ag_result result =
        agplayer::pitch_shift(input_path.string(), config, nullptr, nullptr, error);
    if (result != AG_OK) {
        std::cerr << "pitch_shift failed: " << static_cast<int>(result)
                  << " " << error << "\n";
    }
    assert(result == AG_OK);
    assert(std::filesystem::exists(output_path));

    std::filesystem::remove(output_path);
    return 0;
}
