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

    ag_visual_pcm_snapshot pcm{};
    assert(ag_player_set_visual_pcm_enabled(nullptr, 1) == AG_INVALID_ARGUMENT);
    assert(ag_player_read_visual_pcm(nullptr, &pcm) == AG_INVALID_ARGUMENT);
    assert(ag_player_read_visual_pcm(player, nullptr) == AG_INVALID_ARGUMENT);
    assert(ag_player_set_visual_pcm_enabled(player, 2) == AG_INVALID_ARGUMENT);
    assert(ag_player_read_visual_pcm(player, &pcm) == AG_OK);
    assert(pcm.sample_count == 0);
    assert(ag_player_set_visual_pcm_enabled(player, 1) == AG_OK);

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

    bool receivedPcm = false;
    const auto pcmDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    do {
        assert(ag_player_read_visual_pcm(player, &pcm) == AG_OK);
        if (pcm.sample_count != 0) {
            assert(pcm.sample_count <= 1024 && pcm.sample_rate > 0);
            for (size_t i = 0; i < pcm.sample_count; ++i)
                if (std::isfinite(pcm.samples[i]) && std::abs(pcm.samples[i]) > 0.0001F)
                    receivedPcm = true;
        }
        if (!receivedPcm) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    } while (!receivedPcm && std::chrono::steady_clock::now() < pcmDeadline);
    assert(receivedPcm);
    // Muting the speakers must not silence the independent visual analysis.
    const auto generationBeforeMute = pcm.generation;
    assert(ag_player_set_muted(player, 1) == AG_OK);
    assert(ag_player_output_levels(player, &levels) == AG_OK);
    assert(isZero(levels));
    assert(ag_player_read_visual_pcm(player, &pcm) == AG_OK);
    assert(pcm.generation == generationBeforeMute);
    assert(ag_player_set_visual_pcm_enabled(player, 0) == AG_OK);
    assert(ag_player_read_visual_pcm(player, &pcm) == AG_OK);
    assert(pcm.sample_count == 0);
    const auto disabledGeneration = pcm.generation;
    assert(ag_player_set_visual_pcm_enabled(player, 1) == AG_OK);
    receivedPcm = false;
    const auto mutedDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    do {
        assert(ag_player_read_visual_pcm(player, &pcm) == AG_OK);
        assert(pcm.generation != disabledGeneration);
        for (size_t i = 0; i < pcm.sample_count; ++i)
            if (std::isfinite(pcm.samples[i]) && std::abs(pcm.samples[i]) > 0.0001F)
                receivedPcm = true;
        if (!receivedPcm) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    } while (!receivedPcm && std::chrono::steady_clock::now() < mutedDeadline);
    assert(receivedPcm && "muted output must retain visual PCM");
    assert(ag_player_output_levels(player, &levels) == AG_OK);
    assert(isZero(levels));
    const auto generationBeforeUnmute = pcm.generation;
    assert(ag_player_set_muted(player, 0) == AG_OK);
    assert(ag_player_read_visual_pcm(player, &pcm) == AG_OK);
    assert(pcm.generation == generationBeforeUnmute);
    const auto generation = pcm.generation;
    assert(ag_player_pause(player) == AG_OK);
    assert(ag_player_read_visual_pcm(player, &pcm) == AG_OK);
    assert(pcm.generation == generation);
    assert(ag_player_play(player) == AG_OK);
    assert(ag_player_seek(player, 0) == AG_OK);
    assert(ag_player_read_visual_pcm(player, &pcm) == AG_OK);
    assert(pcm.generation != generation);
    assert(ag_player_set_visual_pcm_enabled(player, 0) == AG_OK);
    assert(ag_player_read_visual_pcm(player, &pcm) == AG_OK);
    assert(pcm.sample_count == 0);

    assert(ag_player_set_muted(player, 1) == AG_OK);
    assert(ag_player_output_levels(player, &levels) == AG_OK);
    assert(isZero(levels));
    ag_player_destroy(player);
    return 0;
}
