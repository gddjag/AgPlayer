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
    assert(ag_encoder_available(nullptr) == 0);
    assert(ag_encoder_available("flac") == 1);
    assert(ag_encoder_available("agplayer-no-such-encoder") == 0);
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

    const ag_player_config null_config{AG_AUDIO_BACKEND_NULL, 0U};
    ag_player* null_player = nullptr;
    assert(ag_player_create_with_config(&null_config, &null_player) == AG_OK);
    std::size_t device_count = 0U;
    assert(ag_player_output_device_count(null_player, &device_count) == AG_OK);
    assert(device_count >= 1U);
    assert(ag_player_set_transition_fade_ms(null_player, 200) == AG_OK);
    assert(ag_player_set_transition_fade_ms(null_player, 100)
           == AG_INVALID_ARGUMENT);
    assert(ag_player_set_match_track_sample_rate(null_player, true) == AG_OK);
    const char* scope_paths[] = {"first.wav", "second.wav"};
    assert(ag_player_set_scoped_queue(null_player, scope_paths, 2U,
                                      0U, 0U, 0)
           == AG_INVALID_ARGUMENT);
    assert(ag_player_set_scoped_queue(null_player, scope_paths, 2U,
                                      1U, 1U, 0)
           == AG_INVALID_ARGUMENT);
    std::array<float, 64> spectrum{};
    assert(ag_player_spectrum(nullptr, spectrum.data(), spectrum.size())
           == AG_INVALID_ARGUMENT);
    assert(ag_player_spectrum(null_player, nullptr, spectrum.size())
           == AG_INVALID_ARGUMENT);
    assert(ag_player_spectrum(null_player, spectrum.data(), 0U)
           == AG_INVALID_ARGUMENT);
    assert(ag_player_spectrum(null_player, spectrum.data(), spectrum.size())
           == AG_OK);
    assert(ag_player_output_device_count(null_player, nullptr)
           == AG_INVALID_ARGUMENT);
    std::size_t id_required = 0U;
    assert(ag_player_output_device_id(
               null_player, 0U, nullptr, 0U, &id_required)
           == AG_OK);
    assert(id_required > 1U);
    std::array<char, 512> device_id{};
    assert(id_required <= device_id.size());
    assert(ag_player_output_device_id(
               null_player, 0U, device_id.data(), device_id.size(),
               &id_required)
           == AG_OK);
    assert(device_id.front() != '\0');
    assert(ag_player_output_device_name(
               null_player, device_count, nullptr, 0U, &required)
           == AG_INVALID_ARGUMENT);
    assert(ag_player_output_device_name(
               null_player, 0U, nullptr, 0U, &required)
           == AG_OK);
    assert(required > 1U);
    std::array<char, 256> device_name{};
    assert(required <= device_name.size());
    assert(ag_player_output_device_name(
               null_player, 0U, device_name.data(), device_name.size(),
               &required)
           == AG_OK);
    assert(device_name.front() != '\0');
    std::array<char, 512> snapshot_id{};
    std::array<char, 256> snapshot_name{};
    std::size_t snapshot_id_required = 0U;
    std::size_t snapshot_name_required = 0U;
    assert(ag_player_output_device_info(
               null_player, 0U,
               snapshot_id.data(), snapshot_id.size(),
               &snapshot_id_required,
               snapshot_name.data(), snapshot_name.size(),
               &snapshot_name_required)
           == AG_OK);
    assert(std::strcmp(snapshot_id.data(), device_id.data()) == 0);
    assert(std::strcmp(snapshot_name.data(), device_name.data()) == 0);
    assert(ag_player_set_output_device(
               null_player, device_id.data(), 0)
           == AG_OK);
    assert(ag_player_set_output_device(null_player, "", 0) == AG_OK);
    ag_player_destroy(null_player);
    ag_player_destroy(nullptr);
}
