#include <agplayer/c_api.h>

#include <cassert>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <thread>

#ifdef _WIN32
#include <crtdbg.h>
#endif

int main(const int argc, char** argv)
{
#ifdef _WIN32
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
    assert(argc == 2);

    ag_player_config config{};
    config.backend = AG_AUDIO_BACKEND_NULL;
    config.buffer_frames = 4'096U;
    ag_playback_snapshot snapshot{};

    ag_player* player = reinterpret_cast<ag_player*>(1);
    assert(ag_player_create_with_config(nullptr, &player) == AG_INVALID_ARGUMENT);
    assert(player == nullptr);
    config.backend = static_cast<ag_audio_backend>(99);
    assert(ag_player_create_with_config(&config, &player) == AG_INVALID_ARGUMENT);
    assert(player == nullptr);
    config.backend = AG_AUDIO_BACKEND_NULL;
    assert(ag_player_create_with_config(&config, &player) == AG_OK);
    assert(player != nullptr);
    assert(ag_player_load(nullptr, argv[1]) == AG_INVALID_ARGUMENT);
    assert(ag_player_play(nullptr) == AG_INVALID_ARGUMENT);
    assert(ag_player_pause(nullptr) == AG_INVALID_ARGUMENT);
    assert(ag_player_stop(nullptr) == AG_INVALID_ARGUMENT);
    assert(ag_player_seek(nullptr, 0) == AG_INVALID_ARGUMENT);
    assert(ag_player_set_volume(nullptr, 0.5F) == AG_INVALID_ARGUMENT);
    assert(ag_player_set_muted(nullptr, 0) == AG_INVALID_ARGUMENT);
    assert(ag_player_snapshot(nullptr, &snapshot) == AG_INVALID_ARGUMENT);

    const std::filesystem::path fixture_path = argv[1];
    const std::filesystem::path missing_path =
        fixture_path.parent_path() / "missing-audio-engine.wav";
    std::filesystem::remove(missing_path);
    const std::string missing_filename = missing_path.string();
    assert(ag_player_load(player, missing_filename.c_str()) == AG_IO_ERROR);
    assert(ag_player_snapshot(player, &snapshot) == AG_OK);
    assert(snapshot.state == AG_ERROR);
    assert(ag_player_play(player) != AG_OK);
    assert(ag_player_pause(player) != AG_OK);
    assert(ag_player_stop(player) != AG_OK);
    assert(ag_player_seek(player, 0) != AG_OK);
    assert(ag_player_snapshot(player, &snapshot) == AG_OK);
    assert(snapshot.state == AG_ERROR);
    assert(ag_player_load(player, argv[1]) == AG_OK);

    snapshot = {};
    assert(ag_player_snapshot(player, &snapshot) == AG_OK);
    assert(snapshot.state == AG_STOPPED);
    assert(snapshot.duration_ms >= 1'990);
    assert(snapshot.position_ms == 0);

    assert(ag_player_play(player) == AG_OK);
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    assert(ag_player_snapshot(player, &snapshot) == AG_OK);
    assert(snapshot.state == AG_PLAYING);
    assert(snapshot.position_ms > 0);

    assert(ag_player_pause(player) == AG_OK);
    assert(ag_player_snapshot(player, &snapshot) == AG_OK);
    const long long paused_position = snapshot.position_ms;
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    assert(ag_player_snapshot(player, &snapshot) == AG_OK);
    assert(snapshot.state == AG_PAUSED);
    assert(snapshot.position_ms == paused_position);

    assert(ag_player_seek(player, 1'000) == AG_OK);
    assert(ag_player_set_volume(player, 0.25F) == AG_OK);
    assert(ag_player_set_muted(player, 1) == AG_OK);
    assert(ag_player_snapshot(player, &snapshot) == AG_OK);
    assert(snapshot.state == AG_PAUSED);
    assert(snapshot.position_ms == 1'000);
    assert(std::abs(snapshot.volume - 0.25F) < 0.001F);
    assert(snapshot.muted == 1);

    assert(ag_player_set_volume(player, -0.01F) == AG_INVALID_ARGUMENT);
    assert(ag_player_set_volume(player, 1.01F) == AG_INVALID_ARGUMENT);
    assert(ag_player_set_muted(player, 2) == AG_INVALID_ARGUMENT);
    assert(ag_player_seek(player, snapshot.duration_ms + 1) == AG_INVALID_ARGUMENT);
    assert(ag_player_stop(player) == AG_OK);
    assert(ag_player_snapshot(player, &snapshot) == AG_OK);
    assert(snapshot.state == AG_STOPPED);
    assert(snapshot.position_ms == 0);

    assert(ag_player_seek(player, 1'900) == AG_OK);
    assert(ag_player_play(player) == AG_OK);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    assert(ag_player_snapshot(player, &snapshot) == AG_OK);
    assert(snapshot.state == AG_STOPPED);

    assert(ag_player_seek(player, 500) == AG_OK);
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    assert(ag_player_snapshot(player, &snapshot) == AG_OK);
    assert(snapshot.state == AG_STOPPED);
    assert(snapshot.position_ms == 500);

    assert(ag_player_play(player) == AG_OK);
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    assert(ag_player_snapshot(player, &snapshot) == AG_OK);
    assert(snapshot.state == AG_PLAYING);
    assert(snapshot.position_ms > 500);

    ag_player_destroy(player);
}
