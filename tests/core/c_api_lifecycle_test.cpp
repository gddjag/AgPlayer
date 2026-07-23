// Test files deliberately keep assert() active even in Release builds: many
// test cases embed function calls with side effects inside assert(), and
// silencing them under NDEBUG would skip those calls and crash on cleanup.
#undef NDEBUG

#include <agplayer/c_api.h>

#include <array>
#include <cassert>
#include <cstring>

int main()
{
    assert(ag_player_create(nullptr) == AG_INVALID_ARGUMENT);

    ag_player* player = nullptr;
    assert(ag_player_create(&player) == AG_OK);
    assert(player != nullptr);

    std::array<char, 64> message{};
    std::size_t required = 0;
    assert(ag_player_last_error(nullptr, message.data(), message.size(), &required)
           == AG_INVALID_ARGUMENT);
    assert(ag_player_last_error(player, message.data(), message.size(), nullptr)
           == AG_INVALID_ARGUMENT);
    assert(ag_player_last_error(player, nullptr, 0, &required) == AG_OK);
    assert(required == 1);
    assert(ag_player_last_error(player, nullptr, 1, &required) == AG_INVALID_ARGUMENT);
    assert(ag_player_last_error(player, message.data(), message.size(), &required) == AG_OK);
    assert(required == 1);
    assert(std::strcmp(message.data(), "") == 0);

    ag_player_destroy(player);
    ag_player_destroy(nullptr);
}
