// Test files deliberately keep assert() active in Release builds.
#undef NDEBUG

#include <agplayer/c_api.h>

#include <cassert>
#include <cmath>
#include <filesystem>
#include <limits>

int main(const int argc, char** argv)
{
    assert(argc == 2);
    ag_scratch_status status{};
    assert(ag_player_begin_scratch(nullptr) == AG_INVALID_ARGUMENT);
    assert(ag_player_update_scratch(nullptr, 1.0F) == AG_INVALID_ARGUMENT);
    assert(ag_player_end_scratch(nullptr) == AG_INVALID_ARGUMENT);
    assert(ag_player_cancel_scratch(nullptr) == AG_INVALID_ARGUMENT);
    assert(ag_player_scratch_status(nullptr, &status) == AG_INVALID_ARGUMENT);

    const ag_player_config config{AG_AUDIO_BACKEND_NULL, 4'096U};
    ag_player* player = nullptr;
    assert(ag_player_create_with_config(&config, &player) == AG_OK);
    assert(player != nullptr);
    assert(ag_player_scratch_status(player, nullptr) == AG_INVALID_ARGUMENT);
    assert(ag_player_update_scratch(
               player, std::numeric_limits<float>::quiet_NaN())
           == AG_INVALID_ARGUMENT);
    assert(ag_player_begin_scratch(player) == AG_INVALID_ARGUMENT);
    assert(ag_player_load(player,
                          std::filesystem::u8path(argv[1]).string().c_str())
           == AG_OK);
    assert(ag_player_begin_scratch(player) == AG_INVALID_ARGUMENT);
    assert(ag_player_play(player) == AG_OK);
    assert(ag_player_begin_scratch(player) == AG_OK);
    assert(ag_player_scratch_status(player, &status) == AG_OK);
    assert(status.active != 0);
    assert(ag_player_update_scratch(player, 8.0F) == AG_OK);
    assert(ag_player_end_scratch(player) == AG_OK);
    assert(ag_player_cancel_scratch(player) == AG_OK);
    assert(ag_player_scratch_status(player, &status) == AG_OK);
    assert(status.active == 0);
    assert(status.ready == 0);
    assert(status.buffering == 0);
    ag_player_destroy(player);
    return 0;
}
