#undef NDEBUG

#include "multitrack_editor.hpp"

#include <cassert>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

long long duration_ms(const std::filesystem::path& path)
{
    ag_metadata* metadata = nullptr;
    assert(ag_metadata_open(path.string().c_str(), &metadata) == AG_OK);
    assert(metadata != nullptr);
    const long long duration = ag_metadata_duration_ms(metadata);
    ag_metadata_destroy(metadata);
    return duration;
}

} // namespace

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
    const long long single_track_duration_ms = duration_ms(output);
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
    track_b.timeline_start_ms = 1000;
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
    assert(duration_ms(mix_output) >= single_track_duration_ms + 900);
    std::filesystem::remove(mix_output);

    const std::filesystem::path api_output =
        input_path.parent_path() / "multitrack-api-offset.wav";
    std::filesystem::remove(api_output);
    const std::string input_utf8 = input_path.string();
    const char* input_paths[] = {input_utf8.c_str(), input_utf8.c_str()};
    const long long timeline_starts[] = {0, 1000};
    const double gains[] = {0.5, 0.5};
    const ag_result api_result = ag_multitrack_edit_ex(
        2, input_paths, timeline_starts, nullptr, nullptr, nullptr, nullptr,
        gains, api_output.string().c_str(), nullptr, nullptr, nullptr);
    assert(api_result == AG_OK);
    assert(duration_ms(api_output) >= single_track_duration_ms + 900);
    std::filesystem::remove(api_output);

    agplayer::MultiTrackEditConfig invalid_config;
    agplayer::MultiTrackEditConfig::Track invalid_track;
    invalid_track.input_path = input_path.string();
    invalid_track.timeline_start_ms = -1;
    invalid_config.tracks.push_back(invalid_track);
    const std::filesystem::path invalid_output =
        input_path.parent_path() / "multitrack-invalid.wav";
    std::filesystem::remove(invalid_output);
    invalid_config.output_path = invalid_output.string();
    error.clear();
    assert(agplayer::multitrack_edit(
               invalid_config, nullptr, nullptr, error)
           == AG_INVALID_ARGUMENT);
    assert(!std::filesystem::exists(invalid_output));

    return 0;
}
