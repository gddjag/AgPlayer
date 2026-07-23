// Test files deliberately keep assert() active even in Release builds: many
// test cases embed function calls with side effects inside assert(), and
// silencing them under NDEBUG would skip those calls and crash on cleanup.
#undef NDEBUG

#include <agplayer/c_api.h>

#include "waveform_cache.hpp"

#include <cassert>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <crtdbg.h>
#endif

namespace {

void truncateAfterBytes(const std::filesystem::path& path, std::size_t bytes)
{
    std::filesystem::resize_file(path, bytes);
}

std::size_t fileSize(const std::filesystem::path& path)
{
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    return ec ? 0U : static_cast<std::size_t>(size);
}

} // namespace

int main(const int argc, char** argv)
{
#ifdef _WIN32
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
    assert(argc == 2);

    const std::filesystem::path fixture_path = argv[1];
    const std::filesystem::path work_dir =
        fixture_path.parent_path() / "recovery-case";
    {
        std::error_code ec;
        std::filesystem::remove_all(work_dir, ec);
    }
    std::filesystem::create_directories(work_dir);

    // 1. Device initialization failure: invalid backend in config rejected
    {
        ag_player_config config{};
        config.backend = static_cast<ag_audio_backend>(99);
        config.buffer_frames = 4'096U;
        ag_player* player = nullptr;
        assert(ag_player_create_with_config(&config, &player) == AG_INVALID_ARGUMENT);
        assert(player == nullptr);
    }

    // 2. Device loss simulation and recovery
    {
        ag_player_config config{};
        config.backend = AG_AUDIO_BACKEND_NULL;
        config.buffer_frames = 4'096U;
        ag_player* player = nullptr;
        const ag_result create_result = ag_player_create_with_config(&config, &player);
        assert(create_result == AG_OK);
        assert(player != nullptr);

        const std::string fixture = fixture_path.string();
        const ag_result load_result = ag_player_load(player, fixture.c_str());
        assert(load_result == AG_OK);
        const ag_result play_result = ag_player_play(player);
        assert(play_result == AG_OK);
        std::this_thread::sleep_for(std::chrono::milliseconds(80));

        ag_playback_snapshot snapshot{};
        const ag_result snap_result = ag_player_snapshot(player, &snapshot);
        assert(snap_result == AG_OK);
        assert(snapshot.state == AG_PLAYING);

        // Simulate device loss
        const ag_result loss_result = ag_player_simulate_device_loss(player);
        assert(loss_result == AG_OK);
        const int lost = ag_player_device_lost(player);
        assert(lost == 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        const ag_result snap2_result = ag_player_snapshot(player, &snapshot);
        assert(snap2_result == AG_OK);
        assert(snapshot.state == AG_PAUSED || snapshot.state == AG_STOPPED
               || snapshot.state == AG_ERROR);

        // Retry device
        const ag_result retry_result = ag_player_retry_device(player);
        assert(retry_result == AG_OK);
        const int lost2 = ag_player_device_lost(player);
        assert(lost2 == 0);

        ag_player_destroy(player);
    }

    // 3. Source file missing after import (path stays but file deleted)
    {
        const std::filesystem::path missing_source =
            work_dir / "deleted-source.wav";
        std::filesystem::copy_file(fixture_path, missing_source);

        ag_player_config config{};
        config.backend = AG_AUDIO_BACKEND_NULL;
        config.buffer_frames = 4'096U;
        ag_player* player = nullptr;
        const ag_result c3 = ag_player_create_with_config(&config, &player);
        assert(c3 == AG_OK);

        const std::string path_str = missing_source.string();
        const ag_result l3 = ag_player_load(player, path_str.c_str());
        assert(l3 == AG_OK);

        // Delete the source file while loaded. On Windows the decoder may
        // hold an open handle, making deletion fail with a sharing violation.
        // Use the non-throwing overload so the test continues regardless.
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        std::error_code remove_ec;
        const bool removed = std::filesystem::remove(missing_source, remove_ec);
        (void)removed;

        // Operations should not crash; either succeed or report error gracefully
        ag_playback_snapshot snapshot{};
        ag_player_snapshot(player, &snapshot);
        ag_player_play(player);
        ag_player_pause(player);
        ag_player_stop(player);

        ag_player_destroy(player);
        std::error_code cleanup_ec;
        std::filesystem::remove(missing_source, cleanup_ec);
    }

    // 4. Corrupt cache: re-analyze when cache is bad
    {
        // Isolated source copy so the cache key is stable and does not
        // interfere with other tests sharing the fixture.
        const std::filesystem::path cache_source =
            work_dir / "cache-source.wav";
        std::filesystem::copy_file(fixture_path, cache_source);

        ag_cancel_token* token = ag_cancel_token_create();
        assert(token != nullptr);

        // First analysis produces a valid waveform from the source. The
        // analyze API does not itself touch the on-disk cache; WaveformCache
        // is a separate persistence layer, so we save explicitly here to
        // model the integration scenario the spec requires.
        const std::string source_str = cache_source.string();
        ag_waveform* first_waveform = nullptr;
        const ag_result first_result =
            ag_waveform_analyze(source_str.c_str(), 64U, token, nullptr,
                                nullptr, &first_waveform);
        assert(first_result == AG_OK);
        assert(first_waveform != nullptr);
        const size_t first_count = ag_waveform_count(first_waveform);
        assert(first_count == 64U);

        std::vector<float> peaks(first_count);
        for (size_t index = 0U; index < first_count; ++index) {
            peaks[index] = ag_waveform_peak(first_waveform, index);
        }
        ag_waveform_destroy(first_waveform);

        const std::filesystem::path cache_file =
            work_dir / "corrupt-cache.agwf";
        assert(agplayer::WaveformCache::save(cache_file, cache_source, peaks));

        // A valid cache loads back cleanly.
        std::vector<float> loaded;
        assert(agplayer::WaveformCache::load(cache_file, cache_source, loaded));
        assert(loaded == peaks);

        // Corrupt the cache: keep the "AGWF" magic but zero the rest so the
        // version field no longer matches. The cache layer must reject it.
        {
            std::ofstream overwrite(cache_file,
                                    std::ios::binary | std::ios::trunc);
            assert(overwrite);
            overwrite.write("AGWF", 4);
            const std::vector<char> zeros(28, '\0');
            overwrite.write(zeros.data(),
                            static_cast<std::streamsize>(zeros.size()));
            assert(overwrite);
        }
        std::vector<float> corrupt_loaded;
        assert(!agplayer::WaveformCache::load(cache_file, cache_source,
                                              corrupt_loaded));
        assert(corrupt_loaded.empty());

        // Re-analyze from source: a corrupt cache must not break analysis.
        // The analyze API re-derives peaks from the source every time.
        ag_waveform* second_waveform = nullptr;
        const ag_result second_result =
            ag_waveform_analyze(source_str.c_str(), 64U, token, nullptr,
                                nullptr, &second_waveform);
        assert(second_result == AG_OK);
        assert(second_waveform != nullptr);
        assert(ag_waveform_count(second_waveform) == first_count);
        for (size_t index = 0U; index < first_count; ++index) {
            assert(ag_waveform_peak(second_waveform, index) == peaks[index]);
        }
        ag_waveform_destroy(second_waveform);

        ag_cancel_token_destroy(token);
    }

    // 5. Decode failure mid-track: truncated file after start
    {
        const std::filesystem::path truncated =
            work_dir / "truncated.wav";
        std::filesystem::copy_file(fixture_path, truncated);
        const std::size_t original_size = fileSize(truncated);
        assert(original_size > 200U);
        truncateAfterBytes(truncated, original_size / 2U);

        ag_player_config config{};
        config.backend = AG_AUDIO_BACKEND_NULL;
        config.buffer_frames = 4'096U;
        ag_player* player = nullptr;
        const ag_result c5 = ag_player_create_with_config(&config, &player);
        assert(c5 == AG_OK);

        const std::string truncated_str = truncated.string();
        // Load may succeed or fail; either way no crash
        const ag_result load_result = ag_player_load(player, truncated_str.c_str());
        if (load_result == AG_OK) {
            ag_player_play(player);
            std::this_thread::sleep_for(std::chrono::milliseconds(80));
            ag_player_stop(player);
        }

        ag_player_destroy(player);
        std::error_code trunc_ec;
        std::filesystem::remove(truncated, trunc_ec);
    }

    // 6. Repeated destroy/cancel calls are idempotent; cancellation during
    //    analyze returns AG_CANCELLED
    {
        ag_player* player = nullptr;
        const ag_result c6 = ag_player_create(&player);
        assert(c6 == AG_OK);
        ag_player_destroy(player);
        // Double destroy is safe (no-op on nullptr)
        ag_player_destroy(nullptr);

        ag_cancel_token* token = ag_cancel_token_create();
        ag_cancel_token_cancel(token);
        ag_cancel_token_cancel(token); // double cancel safe
        ag_cancel_token_destroy(token);
        ag_cancel_token_destroy(nullptr);

        ag_waveform_destroy(nullptr);
        ag_metadata_destroy(nullptr);

        // A cancelled token makes ag_waveform_analyze return AG_CANCELLED
        // without producing a waveform (moved here from the former section 4).
        ag_cancel_token* analyze_token = ag_cancel_token_create();
        ag_cancel_token_cancel(analyze_token);
        ag_waveform* cancelled_waveform = nullptr;
        const ag_result cancelled_result =
            ag_waveform_analyze(fixture_path.string().c_str(), 64U,
                                analyze_token, nullptr, nullptr,
                                &cancelled_waveform);
        assert(cancelled_result == AG_CANCELLED);
        assert(cancelled_waveform == nullptr);
        ag_cancel_token_destroy(analyze_token);
    }

    // 7. Device loss notification does not terminate process
    {
        ag_player_config config{};
        config.backend = AG_AUDIO_BACKEND_NULL;
        config.buffer_frames = 4'096U;
        ag_player* player = nullptr;
        const ag_result c7 = ag_player_create_with_config(&config, &player);
        assert(c7 == AG_OK);
        const ag_result l7 = ag_player_load(player, fixture_path.string().c_str());
        assert(l7 == AG_OK);
        const ag_result p7 = ag_player_play(player);
        assert(p7 == AG_OK);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        const ag_result sl7 = ag_player_simulate_device_loss(player);
        assert(sl7 == AG_OK);
        // Process still alive: snapshot works
        ag_playback_snapshot snapshot{};
        assert(ag_player_snapshot(player, &snapshot) == AG_OK);
        // Device lost is observable
        const int dl7 = ag_player_device_lost(player);
        assert(dl7 == 1);

        // Retry recovers device
        const ag_result r7 = ag_player_retry_device(player);
        assert(r7 == AG_OK);
        assert(ag_player_device_lost(player) == 0);

        ag_player_destroy(player);
    }

    std::error_code workdir_ec;
    std::filesystem::remove_all(work_dir, workdir_ec);
    return 0;
}
