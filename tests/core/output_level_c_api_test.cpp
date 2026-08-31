#undef NDEBUG

#include <agplayer/c_api.h>

#include <cassert>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <thread>

namespace {

bool isZero(const ag_output_levels& levels)
{
    return levels.left_peak == 0.0F && levels.right_peak == 0.0F
        && levels.left_rms == 0.0F && levels.right_rms == 0.0F;
}

bool isValid(const ag_output_levels& levels)
{
    return std::isfinite(levels.left_peak)
        && std::isfinite(levels.right_peak)
        && std::isfinite(levels.left_rms)
        && std::isfinite(levels.right_rms)
        && levels.left_peak >= 0.0F && levels.left_peak <= 1.0F
        && levels.right_peak >= 0.0F && levels.right_peak <= 1.0F
        && levels.left_rms >= 0.0F && levels.left_rms <= 1.0F
        && levels.right_rms >= 0.0F && levels.right_rms <= 1.0F;
}

} // namespace

int main(const int argc, char** argv)
{
    assert(argc == 2);
    const std::filesystem::path media(argv[1]);
    assert(std::filesystem::exists(media));

    ag_output_levels levels{1.0F, 1.0F, 1.0F, 1.0F};
    assert(ag_player_output_levels(nullptr, &levels) == AG_INVALID_ARGUMENT);

    ag_player* player = nullptr;
    const ag_player_config config{AG_AUDIO_BACKEND_NULL, 4'096U};
    assert(ag_player_create_with_config(&config, &player) == AG_OK);
    assert(player != nullptr);
    assert(ag_player_output_levels(player, nullptr) == AG_INVALID_ARGUMENT);
    assert(ag_player_output_levels(player, &levels) == AG_OK);
    assert(isZero(levels));

    assert(ag_player_load(player, media.string().c_str()) == AG_OK);
    assert(ag_player_play(player) == AG_OK);
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(3);
    do {
        assert(ag_player_output_levels(player, &levels) == AG_OK);
        assert(isValid(levels));
        if (levels.left_peak > 0.0F && levels.right_peak > 0.0F) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    } while (std::chrono::steady_clock::now() < deadline);
    assert(levels.left_peak > 0.0F);
    assert(levels.right_peak > 0.0F);
    assert(levels.left_rms > 0.0F);
    assert(levels.right_rms > 0.0F);

    assert(ag_player_set_muted(player, 1) == AG_OK);
    assert(ag_player_output_levels(player, &levels) == AG_OK);
    assert(isZero(levels));
    ag_player_destroy(player);
    return 0;
}
