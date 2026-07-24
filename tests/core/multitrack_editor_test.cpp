#undef NDEBUG

#include "multitrack_editor.hpp"

#include <cassert>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    assert(argc == 2);
    const std::filesystem::path input_path = argv[1];

    const std::filesystem::path output = input_path.parent_path() / "multitrack-out.wav";
    std::filesystem::remove(output);

    agplayer::MultiTrackEditConfig config;
    agplayer::MultiTrackEditConfig::Track track;
    track.input_path = input_path.string();
    track.gain = 0.5;
    config.tracks.push_back(track);
    config.output_path = output.string();

    std::string error;
    const ag_result result = agplayer::multitrack_edit(config, nullptr, nullptr, error);
    if (result != AG_OK) {
        std::cerr << "multitrack_edit failed: " << static_cast<int>(result) << " " << error << "\n";
    }
    assert(result == AG_OK);
    assert(std::filesystem::exists(output));
    std::filesystem::remove(output);

    // Optional: two-track mix with the same source.
    const std::filesystem::path mix_output =
        input_path.parent_path() / "multitrack-mix.wav";
    std::filesystem::remove(mix_output);

    agplayer::MultiTrackEditConfig mix_config;
    agplayer::MultiTrackEditConfig::Track track_a;
    track_a.input_path = input_path.string();
    track_a.gain = 0.5;
    mix_config.tracks.push_back(track_a);
    agplayer::MultiTrackEditConfig::Track track_b;
    track_b.input_path = input_path.string();
    track_b.gain = 0.5;
    mix_config.tracks.push_back(track_b);
    mix_config.output_path = mix_output.string();

    error.clear();
    const ag_result mix_result =
        agplayer::multitrack_edit(mix_config, nullptr, nullptr, error);
    if (mix_result != AG_OK) {
        std::cerr << "multitrack_edit two-track mix failed: "
                  << static_cast<int>(mix_result) << " " << error << "\n";
    }
    assert(mix_result == AG_OK);
    assert(std::filesystem::exists(mix_output));
    std::filesystem::remove(mix_output);

    return 0;
}
