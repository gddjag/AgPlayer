#include "audio_engine.hpp"

#include <agplayer/c_api.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <crtdbg.h>
#endif

namespace {

constexpr std::size_t sample_rate = 44'100U;
constexpr std::size_t channels = 2U;

void wait_for_frames(const agplayer::AudioEngine& engine,
                     const std::size_t frames)
{
    for (int attempt = 0; attempt < 2'000; ++attempt) {
        if (engine.buffered_frames() >= frames) {
            return;
        }
        assert(engine.snapshot().state != agplayer::EngineState::Error);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(false);
}

std::vector<float> capture(agplayer::AudioEngine& engine,
                           std::size_t frame_count)
{
    std::vector<float> captured;
    captured.reserve(frame_count * channels);
    std::array<float, 512U * channels> block{};
    while (frame_count > 0U) {
        const std::size_t requested = std::min<std::size_t>(512U, frame_count);
        wait_for_frames(engine, requested);
        engine.render(block.data(), requested);
        captured.insert(captured.end(),
                        block.begin(),
                        block.begin() + static_cast<std::ptrdiff_t>(requested
                                                                    * channels));
        frame_count -= requested;
    }
    return captured;
}

std::vector<float> capture_realtime(agplayer::AudioEngine& engine,
                                    std::size_t frame_count)
{
    wait_for_frames(engine, 4'096U);
    std::vector<float> captured;
    captured.reserve(frame_count * channels);
    std::array<float, 512U * channels> block{};
    auto deadline = std::chrono::steady_clock::now();
    while (frame_count > 0U) {
        const std::size_t requested = std::min<std::size_t>(512U, frame_count);
        engine.render(block.data(), requested);
        const auto end = block.begin()
                         + static_cast<std::ptrdiff_t>(requested * channels);
        assert(std::any_of(block.begin(), end, [](const float sample) {
            return std::abs(sample) > 0.001F;
        }));
        captured.insert(captured.end(), block.begin(), end);
        frame_count -= requested;
        deadline += std::chrono::microseconds(
            static_cast<std::int64_t>(requested) * 1'000'000
            / static_cast<std::int64_t>(sample_rate));
        std::this_thread::sleep_until(deadline);
    }
    return captured;
}

} // namespace

int main(const int argc, char** argv)
{
#ifdef _WIN32
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif

    assert(argc == 4);
    (void)argc;
    agplayer::AudioEngine engine(agplayer::AudioBackend::Manual, 4'096U);
    assert(engine.set_queue({argv[1], argv[2]}, 0U) == AG_OK);
    assert(engine.set_mode(agplayer::PlaybackMode::Sequential) == AG_OK);
    assert(engine.previous() == AG_INVALID_ARGUMENT);
    assert(engine.play() == AG_OK);

    const std::vector<float> captured = capture(engine, sample_rate * 2U);

    const std::size_t boundary = sample_rate * channels;
    assert(std::abs(captured[boundary] - captured[boundary - channels]) < 0.05F);
    (void)boundary;
    const agplayer::EngineSnapshot internal_snapshot = engine.snapshot();
    assert(internal_snapshot.track_index == 1U);
    assert(internal_snapshot.track_count == 2U);

    {
        agplayer::AudioEngine realtime_engine(agplayer::AudioBackend::Manual,
                                              8'192U);
        assert(realtime_engine.set_queue({argv[1], argv[2]}, 0U) == AG_OK);
        assert(realtime_engine.play() == AG_OK);
        const std::vector<float> realtime = capture_realtime(
            realtime_engine, sample_rate * 2U);
        assert(std::abs(realtime[boundary] - realtime[boundary - channels])
               < 0.05F);
    }

    {
        const std::vector<std::string> alternating_queue{
            argv[3], argv[1], argv[3], argv[1], argv[3], argv[1],
        };
        agplayer::AudioEngine snapshot_engine(agplayer::AudioBackend::Manual,
                                              4'096U);
        assert(snapshot_engine.set_queue(alternating_queue, 0U) == AG_OK);
        assert(snapshot_engine.play() == AG_OK);
        std::atomic<bool> capture_done{false};
        std::thread render_thread([&] {
            capture(snapshot_engine, sample_rate * 9U);
            capture_done.store(true, std::memory_order_release);
        });
        bool consistent = true;
        while (!capture_done.load(std::memory_order_acquire)) {
            const agplayer::EngineSnapshot value = snapshot_engine.snapshot();
            const std::int64_t expected_duration =
                value.track_index % 2U == 0U ? 2'000 : 1'000;
            if (value.duration_ms != expected_duration) {
                consistent = false;
                break;
            }
            std::this_thread::yield();
        }
        render_thread.join();
        assert(consistent);
    }

    agplayer::AudioEngine repeat_engine(agplayer::AudioBackend::Manual, 4'096U);
    assert(repeat_engine.set_queue({argv[1], argv[2]}, 0U) == AG_OK);
    assert(repeat_engine.set_mode(agplayer::PlaybackMode::RepeatOne) == AG_OK);
    assert(repeat_engine.play() == AG_OK);
    const std::vector<float> repeated = capture(repeat_engine,
                                                sample_rate + 1'024U);
    assert(std::abs(repeated[boundary] - repeated[boundary - channels]) < 0.05F);
    const agplayer::EngineSnapshot repeat_snapshot = repeat_engine.snapshot();
    assert(repeat_snapshot.track_index == 0U);
    assert(repeat_snapshot.position_ms < 50);
    agplayer::AudioEngine shuffle_engine(agplayer::AudioBackend::Manual, 4'096U);
    assert(shuffle_engine.set_queue({argv[1], argv[2]}, 0U) == AG_OK);
    assert(shuffle_engine.set_mode(agplayer::PlaybackMode::Shuffle) == AG_OK);
    assert(shuffle_engine.play() == AG_OK);
    const std::vector<float> shuffled = capture(shuffle_engine,
                                                sample_rate + 1'024U);
    assert(std::abs(shuffled[boundary] - shuffled[boundary - channels]) < 0.05F);
    assert(shuffle_engine.snapshot().track_index == 1U);

    {
        agplayer::AudioEngine shuffle_control_engine(
            agplayer::AudioBackend::Manual, 131'072U);
        assert(shuffle_control_engine.set_queue(
                   {argv[1], argv[2], argv[3]}, 0U)
               == AG_OK);
        assert(shuffle_control_engine.set_mode(agplayer::PlaybackMode::Shuffle)
               == AG_OK);
        assert(shuffle_control_engine.play() == AG_OK);
        for (int transition = 0; transition < 50; ++transition) {
            assert(shuffle_control_engine.next() == AG_OK);
        }
    }

    {
        constexpr std::size_t preload_capacity = 131'072U;
        constexpr std::size_t long_track_frames = sample_rate * 2U;
        agplayer::AudioEngine seek_engine(agplayer::AudioBackend::Manual,
                                          preload_capacity);
        assert(seek_engine.set_queue({argv[3], argv[1]}, 0U) == AG_OK);
        wait_for_frames(seek_engine, long_track_frames + 1U);
        assert(seek_engine.snapshot().track_index == 0U);
        assert(seek_engine.seek(1'500) == AG_OK);
        assert(seek_engine.snapshot().track_index == 0U);
        assert(seek_engine.snapshot().position_ms == 1'500);
        assert(seek_engine.play() == AG_OK);
        capture(seek_engine, 1'024U);
        assert(seek_engine.snapshot().track_index == 0U);
    }

    {
        constexpr std::size_t preload_capacity = 131'072U;
        constexpr std::size_t long_track_frames = sample_rate * 2U;
        agplayer::AudioEngine stop_engine(agplayer::AudioBackend::Manual,
                                          preload_capacity);
        assert(stop_engine.set_queue({argv[3], argv[1]}, 0U) == AG_OK);
        wait_for_frames(stop_engine, long_track_frames + 1U);
        assert(stop_engine.stop() == AG_OK);
        assert(stop_engine.play() == AG_OK);
        capture(stop_engine, sample_rate + 1'024U);
        assert(stop_engine.snapshot().track_index == 0U);
    }

    const std::filesystem::path missing_path =
        std::filesystem::path(argv[2]).parent_path() / "missing-next.wav";
    std::filesystem::remove(missing_path);

    {
        agplayer::AudioEngine failed_stop_engine(
            agplayer::AudioBackend::Manual, 65'536U);
        assert(failed_stop_engine.set_queue(
                   {argv[1], missing_path.string()}, 0U)
               == AG_OK);
        wait_for_frames(failed_stop_engine, sample_rate);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        assert(failed_stop_engine.stop() == AG_OK);
        assert(failed_stop_engine.snapshot().state
               == agplayer::EngineState::Stopped);
    }

    {
        agplayer::AudioEngine failed_seek_engine(
            agplayer::AudioBackend::Manual, 65'536U);
        assert(failed_seek_engine.set_queue(
                   {argv[1], missing_path.string()}, 0U)
               == AG_OK);
        wait_for_frames(failed_seek_engine, sample_rate);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        assert(failed_seek_engine.seek(500) == AG_OK);
        assert(failed_seek_engine.snapshot().track_index == 0U);
        assert(failed_seek_engine.snapshot().position_ms == 500);
    }

    agplayer::AudioEngine failure_engine(agplayer::AudioBackend::Manual, 65'536U);
    assert(failure_engine.set_queue({argv[1], missing_path.string()}, 0U)
           == AG_OK);
    wait_for_frames(failure_engine, sample_rate);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    assert(failure_engine.play() == AG_OK);
    std::array<float, 512U * channels> failure_block{};
    while (failure_engine.buffered_frames() > 0U) {
        failure_engine.render(failure_block.data(), 512U);
    }
    assert(failure_engine.snapshot().state == agplayer::EngineState::Error);
    assert(failure_engine.play() != AG_OK);
    assert(failure_engine.next() != AG_OK);
    assert(failure_engine.load(argv[1]) == AG_OK);
    assert(failure_engine.snapshot().state == agplayer::EngineState::Stopped);

    ag_player_config config{AG_AUDIO_BACKEND_NULL, 4'096U};
    ag_player* player = nullptr;
    assert(ag_player_create_with_config(&config, &player) == AG_OK);
    const char* queue[] = {argv[1], argv[2]};
    const char* invalid_queue[] = {argv[1], nullptr};
    assert(ag_player_set_queue(nullptr, queue, 2U, 0U) == AG_INVALID_ARGUMENT);
    assert(ag_player_set_queue(player, nullptr, 2U, 0U) == AG_INVALID_ARGUMENT);
    assert(ag_player_set_queue(player, queue, 0U, 0U) == AG_INVALID_ARGUMENT);
    assert(ag_player_set_queue(player, queue, 2U, 2U) == AG_INVALID_ARGUMENT);
    assert(ag_player_set_queue(player, invalid_queue, 2U, 0U)
           == AG_INVALID_ARGUMENT);
    assert(ag_player_set_mode(player, static_cast<ag_playback_mode>(99))
           == AG_INVALID_ARGUMENT);
    assert(ag_player_set_queue(player, queue, 2U, 0U) == AG_OK);
    assert(ag_player_set_mode(player, AG_MODE_SEQUENTIAL) == AG_OK);
    assert(ag_player_previous(player) == AG_INVALID_ARGUMENT);
    assert(ag_player_set_mode(player, AG_MODE_REPEAT_ONE) == AG_OK);
    ag_playback_snapshot snapshot{};
    assert(ag_player_snapshot(player, &snapshot) == AG_OK);
    assert(snapshot.track_index == 0U);
    assert(snapshot.track_count == 2U);
    assert(snapshot.mode == AG_MODE_REPEAT_ONE);
    assert(ag_player_previous(player) == AG_OK);
    assert(ag_player_set_mode(player, AG_MODE_SHUFFLE) == AG_OK);
    assert(ag_player_next(player) == AG_OK);
    assert(ag_player_snapshot(player, &snapshot) == AG_OK);
    assert(snapshot.track_index != 0U);
    ag_player_destroy(player);
}
