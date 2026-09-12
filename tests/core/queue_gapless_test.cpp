// Test files deliberately keep assert() active even in Release builds: many
// test cases embed function calls with side effects inside assert(), and
// silencing them under NDEBUG would skip those calls and crash on cleanup.
#undef NDEBUG

#include "audio_engine.hpp"

#include <agplayer/c_api.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <crtdbg.h>
#endif

// Always-evaluate check macro. Unlike AG_CHECK(), AG_CHECK evaluates its
// expression even under NDEBUG (Release builds), so side-effecting calls
// (engine.play(), ag_player_set_queue(), etc.) always execute. On failure it
// prints the location and exits with a non-zero code so ctest detects failure.
#define AG_CHECK(expr)                                                        \
    do {                                                                      \
        if (!(expr)) {                                                        \
            std::fprintf(stderr, "AG_CHECK failed at %s:%d: %s\n",            \
                         __FILE__, __LINE__, #expr);                          \
            std::exit(1);                                                     \
        }                                                                     \
    } while (0)

namespace {

constexpr std::size_t sample_rate = 44'100U;
constexpr std::size_t channels = 2U;
constexpr double fixture_amplitude = 0.251188643150958;
constexpr double fixture_frequency = 440.0;

void wait_for_frames(const agplayer::AudioEngine& engine,
                     const std::size_t frames)
{
    const auto deadline = std::chrono::steady_clock::now()
                          + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
        if (engine.buffered_frames() >= frames) {
            return;
        }
        AG_CHECK(engine.snapshot().state != agplayer::EngineState::Error);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::fprintf(stderr, "[queue-gapless] buffer wait exceeded 2 s: requested=%zu available=%zu\n",
                 frames, engine.buffered_frames());
    std::fflush(stderr);
    AG_CHECK(false);
}

void stage(const char* name)
{
    std::fprintf(stderr, "[queue-gapless] %s\n", name);
    std::fflush(stderr);
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
        AG_CHECK(std::any_of(block.begin(), end, [](const float sample) {
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

float expected_fixture_sample(const std::size_t frame)
{
    const double pi = std::acos(-1.0);
    return static_cast<float>(
        std::sin(2.0 * pi * fixture_frequency
                 * static_cast<double>(frame)
                 / static_cast<double>(sample_rate))
        * fixture_amplitude);
}

std::filesystem::path write_vbr_fixture_with_incorrect_duration(
    const std::filesystem::path& input)
{
    const std::filesystem::path output =
        std::filesystem::temp_directory_path()
        / "agplayer-duration-xing-fixture.mp3";
    std::filesystem::remove(output);
    const ag_transcode_options options{0, 0, 1, 75};
    AG_CHECK(ag_transcode_ex(input.string().c_str(), output.string().c_str(),
                             "libmp3lame", 192'000, 44'100, 2, &options,
                             nullptr, nullptr, nullptr)
             == AG_OK);

    std::fstream stream(output, std::ios::in | std::ios::out | std::ios::binary);
    AG_CHECK(stream.is_open());
    const std::vector<char> bytes{
        std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    const std::array<char, 4U> xing{'X', 'i', 'n', 'g'};
    const auto marker = std::search(bytes.begin(), bytes.end(), xing.begin(), xing.end());
    AG_CHECK(marker != bytes.end());
    const std::size_t offset = static_cast<std::size_t>(
        std::distance(bytes.begin(), marker));
    AG_CHECK(offset + 12U <= bytes.size());
    stream.clear();
    stream.seekp(static_cast<std::streamoff>(offset + 8U));
    constexpr std::array<char, 4U> inflated_frame_count{
        '\0', '\0', '\2', '\0'};
    stream.write(inflated_frame_count.data(),
                 static_cast<std::streamsize>(inflated_frame_count.size()));
    stream.close();
    return output;
}

} // namespace

int main(const int argc, char** argv)
{
#ifdef _WIN32
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif

    AG_CHECK(argc == 4);
    (void)argc;
    {
        stage("duration fixture and EOF begin");
        const std::filesystem::path vbr_fixture =
            write_vbr_fixture_with_incorrect_duration(argv[3]);
        {
            agplayer::AudioEngine duration_engine(
                agplayer::AudioBackend::Manual, 65'536U);
            AG_CHECK(duration_engine.set_queue({vbr_fixture.string()}, 0U) == AG_OK);
            AG_CHECK(duration_engine.snapshot().duration_ms > 10'000);
            AG_CHECK(duration_engine.play() == AG_OK);
            std::array<float, 512U * channels> final_block{};
            const auto deadline = std::chrono::steady_clock::now()
                                  + std::chrono::seconds(2);
            while (std::chrono::steady_clock::now() < deadline
                   && duration_engine.snapshot().state != agplayer::EngineState::Stopped) {
                if (duration_engine.buffered_frames() == 0U) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                // The manual backend must model the device callback even
                // after the decoder drains its final exact-size block. A
                // zero-frame read is what publishes the terminal Stopped
                // state when EOF and an empty ring buffer coincide.
                duration_engine.render(final_block.data(), 512U);
            }
            const auto final = duration_engine.snapshot();
            if (final.state != agplayer::EngineState::Stopped) {
                std::fprintf(stderr, "[queue-gapless] EOF wait exceeded 2 s: state=%d position_ms=%lld available=%zu\n",
                             static_cast<int>(final.state),
                             static_cast<long long>(final.position_ms),
                             duration_engine.buffered_frames());
                std::fflush(stderr);
            }
            AG_CHECK(final.state == agplayer::EngineState::Stopped);
            AG_CHECK(final.position_ms < 3'000);
            AG_CHECK(std::abs(final.duration_ms - final.position_ms) <= 1);
        }
        std::filesystem::remove(vbr_fixture);
    }
    stage("duration fixture and EOF complete; gapless capture begin");

    agplayer::AudioEngine engine(agplayer::AudioBackend::Manual, 4'096U);
    AG_CHECK(engine.set_transition_fade_ms(0) == AG_OK);
    AG_CHECK(engine.set_queue({argv[1], argv[2]}, 0U) == AG_OK);
    AG_CHECK(engine.set_mode(agplayer::PlaybackMode::Sequential) == AG_OK);
    AG_CHECK(engine.previous() == AG_INVALID_ARGUMENT);
    AG_CHECK(engine.play() == AG_OK);

    const std::vector<float> captured = capture(engine, sample_rate * 2U);

    const std::size_t boundary = sample_rate * channels;
    AG_CHECK(std::abs(captured[boundary] - captured[boundary - channels]) < 0.05F);
    std::array<float, 64U> spectrum{};
    AG_CHECK(engine.spectrum(spectrum.data(), spectrum.size()) == AG_OK);
    const auto dominant = std::max_element(spectrum.begin(), spectrum.end());
    const auto dominant_index =
        static_cast<std::size_t>(std::distance(spectrum.begin(), dominant));
    AG_CHECK(dominant_index >= 4U && dominant_index <= 6U);
    AG_CHECK(*dominant > 0.25F);
    const auto spectrum_benchmark_start = std::chrono::steady_clock::now();
    for (int iteration = 0; iteration < 500; ++iteration) {
        AG_CHECK(engine.spectrum(spectrum.data(), spectrum.size()) == AG_OK);
    }
    const auto spectrum_benchmark_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - spectrum_benchmark_start)
            .count();
    AG_CHECK(spectrum_benchmark_ms < 250);
    (void)boundary;
    const agplayer::EngineSnapshot internal_snapshot = engine.snapshot();
    AG_CHECK(internal_snapshot.track_index == 1U);
    AG_CHECK(internal_snapshot.track_count == 2U);
    AG_CHECK(internal_snapshot.sample_rate == static_cast<int>(sample_rate));

    {
        stage("matched sample-rate transition begin");
        agplayer::AudioEngine matched_engine(agplayer::AudioBackend::Manual,
                                             4'096U);
        AG_CHECK(matched_engine.set_match_track_sample_rate(true) == AG_OK);
        AG_CHECK(matched_engine.set_transition_fade_ms(200) == AG_OK);
        AG_CHECK(matched_engine.set_queue({argv[1], argv[2]}, 0U) == AG_OK);
        AG_CHECK(matched_engine.play() == AG_OK);
        std::array<float, 512U * channels> transition_block{};
        for (int attempt = 0;
             attempt < 2'000
             && matched_engine.snapshot().track_index == 0U;
             ++attempt) {
            wait_for_frames(matched_engine, 1U);
            matched_engine.render(transition_block.data(), 512U);
        }
        const agplayer::EngineSnapshot matched = matched_engine.snapshot();
        AG_CHECK(matched.track_index == 1U);
        AG_CHECK(matched.sample_rate == 48'000);
    }

    {
        agplayer::AudioEngine null_device_engine(
            agplayer::AudioBackend::Null, 4'096U);
        AG_CHECK(null_device_engine.set_match_track_sample_rate(true) == AG_OK);
        AG_CHECK(null_device_engine.set_queue({argv[1], argv[2]}, 0U) == AG_OK);
        AG_CHECK(null_device_engine.play() == AG_OK);
        for (int attempt = 0;
             attempt < 400
             && null_device_engine.snapshot().track_index == 0U;
             ++attempt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        const agplayer::EngineSnapshot matched =
            null_device_engine.snapshot();
        AG_CHECK(matched.track_index == 1U);
        AG_CHECK(matched.sample_rate == 48'000);
    }

    {
        agplayer::AudioEngine seek_boundary_engine(
            agplayer::AudioBackend::Manual, 4'096U);
        AG_CHECK(seek_boundary_engine.set_match_track_sample_rate(true)
                 == AG_OK);
        AG_CHECK(seek_boundary_engine.set_queue({argv[1], argv[2]}, 0U)
                 == AG_OK);
        AG_CHECK(seek_boundary_engine.play() == AG_OK);
        wait_for_frames(seek_boundary_engine, sample_rate);
        AG_CHECK(seek_boundary_engine.seek(500) == AG_OK);
        const std::vector<float> after_seek =
            capture(seek_boundary_engine, 512U);
        AG_CHECK(seek_boundary_engine.snapshot().track_index == 0U);
        AG_CHECK(std::abs(after_seek[100U * channels]
                          - expected_fixture_sample(
                              sample_rate / 2U + 100U))
                 < 0.01F);
    }

    {
        agplayer::AudioEngine stop_boundary_engine(
            agplayer::AudioBackend::Manual, 4'096U);
        AG_CHECK(stop_boundary_engine.set_match_track_sample_rate(true)
                 == AG_OK);
        AG_CHECK(stop_boundary_engine.set_queue({argv[1], argv[2]}, 0U)
                 == AG_OK);
        AG_CHECK(stop_boundary_engine.play() == AG_OK);
        wait_for_frames(stop_boundary_engine, sample_rate);
        AG_CHECK(stop_boundary_engine.stop() == AG_OK);
        AG_CHECK(stop_boundary_engine.play() == AG_OK);
        const std::vector<float> after_stop =
            capture(stop_boundary_engine, 512U);
        AG_CHECK(stop_boundary_engine.snapshot().track_index == 0U);
        AG_CHECK(std::abs(after_stop[100U * channels]
                          - expected_fixture_sample(100U))
                 < 0.01F);
    }

    {
        stage("concurrent lifecycle begin");
        agplayer::AudioEngine lifecycle_engine(
            agplayer::AudioBackend::Null, 4'096U);
        AG_CHECK(lifecycle_engine.set_match_track_sample_rate(true) == AG_OK);
        AG_CHECK(lifecycle_engine.set_queue({argv[1], argv[2]}, 0U) == AG_OK);
        AG_CHECK(lifecycle_engine.set_mode(
                     agplayer::PlaybackMode::RepeatAll)
                 == AG_OK);
        AG_CHECK(lifecycle_engine.play() == AG_OK);
        std::atomic<bool> lifecycle_ok{true};
        std::thread switcher([&] {
            for (int attempt = 0; attempt < 12; ++attempt) {
                const ag_result result = lifecycle_engine.next();
                if (result != AG_OK && result != AG_INVALID_ARGUMENT) {
                    lifecycle_ok.store(false, std::memory_order_release);
                }
            }
        });
        std::thread pauser([&] {
            for (int attempt = 0; attempt < 12; ++attempt) {
                const ag_result pause_result = lifecycle_engine.pause();
                if (pause_result != AG_OK
                    && pause_result != AG_INVALID_ARGUMENT) {
                    lifecycle_ok.store(false, std::memory_order_release);
                }
                const ag_result play_result = lifecycle_engine.play();
                if (play_result != AG_OK
                    && play_result != AG_INVALID_ARGUMENT) {
                    lifecycle_ok.store(false, std::memory_order_release);
                }
            }
        });
        switcher.join();
        pauser.join();
        AG_CHECK(lifecycle_ok.load(std::memory_order_acquire));
        AG_CHECK(lifecycle_engine.snapshot().state
                 != agplayer::EngineState::Error);
    }

    {
        stage("serialized next/pause begin");
        agplayer::AudioEngine serialized_engine(
            agplayer::AudioBackend::Null, 4'096U);
        AG_CHECK(serialized_engine.set_queue({argv[1], argv[2]}, 0U)
                 == AG_OK);
        AG_CHECK(serialized_engine.set_mode(
                     agplayer::PlaybackMode::RepeatAll)
                 == AG_OK);
        for (int attempt = 0; attempt < 24; ++attempt) {
            AG_CHECK(serialized_engine.play() == AG_OK);
            std::atomic<bool> start{false};
            ag_result next_result = AG_INTERNAL_ERROR;
            ag_result pause_result = AG_INTERNAL_ERROR;
            std::thread switcher([&] {
                while (!start.load(std::memory_order_acquire)) {
                    std::this_thread::yield();
                }
                next_result = serialized_engine.next();
            });
            std::thread pauser([&] {
                while (!start.load(std::memory_order_acquire)) {
                    std::this_thread::yield();
                }
                pause_result = serialized_engine.pause();
            });
            start.store(true, std::memory_order_release);
            switcher.join();
            pauser.join();
            AG_CHECK(next_result == AG_OK);
            AG_CHECK(pause_result == AG_OK);
            AG_CHECK(serialized_engine.snapshot().state
                     == agplayer::EngineState::Paused);
        }
    }

    {
        stage("realtime fades begin");
        agplayer::AudioEngine fade_engine(agplayer::AudioBackend::Manual,
                                          8'192U);
        AG_CHECK(fade_engine.set_transition_fade_ms(200) == AG_OK);
        AG_CHECK(fade_engine.set_transition_fade_ms(100)
                 == AG_INVALID_ARGUMENT);
        AG_CHECK(fade_engine.set_queue({argv[1], argv[2]}, 0U) == AG_OK);
        AG_CHECK(fade_engine.play() == AG_OK);
        const std::vector<float> faded =
            capture_realtime(fade_engine, sample_rate * 2U);
        float before_boundary_peak = 0.0F;
        float before_away_peak = 0.0F;
        float near_boundary_peak = 0.0F;
        float away_from_boundary_peak = 0.0F;
        for (std::size_t frame = 0U; frame < 256U; ++frame) {
            before_boundary_peak = std::max(
                before_boundary_peak,
                std::abs(faded[boundary - (frame + 1U) * channels]));
            before_away_peak = std::max(
                before_away_peak,
                std::abs(faded[boundary
                               - (sample_rate / 10U + frame) * channels]));
            near_boundary_peak = std::max(
                near_boundary_peak,
                std::abs(faded[boundary + frame * channels]));
            away_from_boundary_peak = std::max(
                away_from_boundary_peak,
                std::abs(faded[boundary
                               + (sample_rate / 10U + frame) * channels]));
        }
        AG_CHECK(before_boundary_peak < before_away_peak * 0.25F);
        AG_CHECK(near_boundary_peak < away_from_boundary_peak * 0.25F);
    }

    {
        agplayer::AudioEngine fade_engine(agplayer::AudioBackend::Manual,
                                          4'096U);
        AG_CHECK(fade_engine.set_transition_fade_ms(500) == AG_OK);
        AG_CHECK(fade_engine.set_queue({argv[1], argv[2]}, 0U) == AG_OK);
        AG_CHECK(fade_engine.play() == AG_OK);
        const std::vector<float> faded =
            capture_realtime(fade_engine, sample_rate * 2U);
        const auto peak_at = [&faded, boundary](const std::size_t offset) {
            float peak = 0.0F;
            for (std::size_t frame = 0U; frame < 256U; ++frame) {
                peak = std::max(
                    peak,
                    std::abs(faded[boundary
                                   + (offset + frame) * channels]));
            }
            return peak;
        };
        const float near_peak = peak_at(0U);
        const float middle_peak = peak_at(sample_rate / 4U);
        const float far_peak = peak_at(sample_rate * 9U / 20U);
        AG_CHECK(near_peak < middle_peak * 0.25F);
        AG_CHECK(middle_peak < far_peak * 0.75F);
    }

    {
        agplayer::AudioEngine realtime_engine(agplayer::AudioBackend::Manual,
                                              8'192U);
        AG_CHECK(realtime_engine.set_queue({argv[1], argv[2]}, 0U) == AG_OK);
        AG_CHECK(realtime_engine.play() == AG_OK);
        const std::vector<float> realtime = capture_realtime(
            realtime_engine, sample_rate * 2U);
        AG_CHECK(std::abs(realtime[boundary] - realtime[boundary - channels])
               < 0.05F);
    }

    {
        stage("snapshot consistency begin");
        const std::vector<std::string> alternating_queue{
            argv[3], argv[1], argv[3], argv[1], argv[3], argv[1],
        };
        agplayer::AudioEngine snapshot_engine(agplayer::AudioBackend::Manual,
                                              4'096U);
        AG_CHECK(snapshot_engine.set_queue(alternating_queue, 0U) == AG_OK);
        AG_CHECK(snapshot_engine.play() == AG_OK);
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
        AG_CHECK(consistent);
    }

    agplayer::AudioEngine repeat_engine(agplayer::AudioBackend::Manual, 4'096U);
    stage("repeat, shuffle and seek begin");
    AG_CHECK(repeat_engine.set_queue({argv[1], argv[2]}, 0U) == AG_OK);
    AG_CHECK(repeat_engine.set_mode(agplayer::PlaybackMode::RepeatOne) == AG_OK);
    AG_CHECK(repeat_engine.play() == AG_OK);
    const std::vector<float> repeated = capture(repeat_engine,
                                                sample_rate + 1'024U);
    AG_CHECK(std::abs(repeated[boundary] - repeated[boundary - channels]) < 0.05F);
    const agplayer::EngineSnapshot repeat_snapshot = repeat_engine.snapshot();
    AG_CHECK(repeat_snapshot.track_index == 0U);
    AG_CHECK(repeat_snapshot.position_ms < 50);
    agplayer::AudioEngine shuffle_engine(agplayer::AudioBackend::Manual, 4'096U);
    AG_CHECK(shuffle_engine.set_queue({argv[1], argv[2]}, 0U) == AG_OK);
    AG_CHECK(shuffle_engine.set_mode(agplayer::PlaybackMode::Shuffle) == AG_OK);
    AG_CHECK(shuffle_engine.play() == AG_OK);
    const std::vector<float> shuffled = capture(shuffle_engine,
                                                sample_rate + 1'024U);
    AG_CHECK(std::abs(shuffled[boundary] - shuffled[boundary - channels]) < 0.05F);
    AG_CHECK(shuffle_engine.snapshot().track_index == 1U);

    {
        agplayer::AudioEngine shuffle_control_engine(
            agplayer::AudioBackend::Manual, 131'072U);
        AG_CHECK(shuffle_control_engine.set_queue(
                   {argv[1], argv[2], argv[3]}, 0U)
               == AG_OK);
        AG_CHECK(shuffle_control_engine.set_mode(agplayer::PlaybackMode::Shuffle)
               == AG_OK);
        AG_CHECK(shuffle_control_engine.play() == AG_OK);
        for (int transition = 0; transition < 50; ++transition) {
            AG_CHECK(shuffle_control_engine.next() == AG_OK);
        }
    }

    {
        constexpr std::size_t preload_capacity = 131'072U;
        constexpr std::size_t long_track_frames = sample_rate * 2U;
        agplayer::AudioEngine seek_engine(agplayer::AudioBackend::Manual,
                                          preload_capacity);
        AG_CHECK(seek_engine.set_queue({argv[3], argv[1]}, 0U) == AG_OK);
        wait_for_frames(seek_engine, long_track_frames + 1U);
        AG_CHECK(seek_engine.snapshot().track_index == 0U);
        AG_CHECK(seek_engine.seek(1'500) == AG_OK);
        AG_CHECK(seek_engine.snapshot().track_index == 0U);
        AG_CHECK(seek_engine.snapshot().position_ms == 1'500);
        AG_CHECK(seek_engine.play() == AG_OK);
        capture(seek_engine, 1'024U);
        AG_CHECK(seek_engine.snapshot().track_index == 0U);
    }

    {
        constexpr std::size_t preload_capacity = 131'072U;
        constexpr std::size_t long_track_frames = sample_rate * 2U;
        agplayer::AudioEngine stop_engine(agplayer::AudioBackend::Manual,
                                          preload_capacity);
        AG_CHECK(stop_engine.set_queue({argv[3], argv[1]}, 0U) == AG_OK);
        wait_for_frames(stop_engine, long_track_frames + 1U);
        AG_CHECK(stop_engine.stop() == AG_OK);
        AG_CHECK(stop_engine.play() == AG_OK);
        capture(stop_engine, sample_rate + 1'024U);
        AG_CHECK(stop_engine.snapshot().track_index == 0U);
    }

    const std::filesystem::path missing_path =
        std::filesystem::path(argv[2]).parent_path() / "missing-next.wav";
    std::filesystem::remove(missing_path);

    {
        agplayer::AudioEngine failed_stop_engine(
            agplayer::AudioBackend::Manual, 65'536U);
        AG_CHECK(failed_stop_engine.set_queue(
                   {argv[1], missing_path.string()}, 0U)
               == AG_OK);
        wait_for_frames(failed_stop_engine, sample_rate);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        AG_CHECK(failed_stop_engine.stop() == AG_OK);
        AG_CHECK(failed_stop_engine.snapshot().state
               == agplayer::EngineState::Stopped);
    }

    {
        agplayer::AudioEngine failed_seek_engine(
            agplayer::AudioBackend::Manual, 65'536U);
        AG_CHECK(failed_seek_engine.set_queue(
                   {argv[1], missing_path.string()}, 0U)
               == AG_OK);
        wait_for_frames(failed_seek_engine, sample_rate);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        AG_CHECK(failed_seek_engine.seek(500) == AG_OK);
        AG_CHECK(failed_seek_engine.snapshot().track_index == 0U);
        AG_CHECK(failed_seek_engine.snapshot().position_ms == 500);
    }

    agplayer::AudioEngine failure_engine(agplayer::AudioBackend::Manual, 65'536U);
    AG_CHECK(failure_engine.set_queue({argv[1], missing_path.string()}, 0U)
           == AG_OK);
    wait_for_frames(failure_engine, sample_rate);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    AG_CHECK(failure_engine.play() == AG_OK);
    std::array<float, 512U * channels> failure_block{};
    while (failure_engine.buffered_frames() > 0U) {
        failure_engine.render(failure_block.data(), 512U);
    }
    AG_CHECK(failure_engine.snapshot().state == agplayer::EngineState::Error);
    AG_CHECK(failure_engine.play() != AG_OK);
    AG_CHECK(failure_engine.next() != AG_OK);
    AG_CHECK(failure_engine.load(argv[1]) == AG_OK);
    AG_CHECK(failure_engine.snapshot().state == agplayer::EngineState::Stopped);

    ag_player_config config{AG_AUDIO_BACKEND_NULL, 4'096U};
    stage("C API scoped queue begin");
    ag_player* player = nullptr;
    AG_CHECK(ag_player_create_with_config(&config, &player) == AG_OK);
    const char* queue[] = {argv[1], argv[2]};
    const char* invalid_queue[] = {argv[1], nullptr};
    AG_CHECK(ag_player_set_queue(nullptr, queue, 2U, 0U) == AG_INVALID_ARGUMENT);
    AG_CHECK(ag_player_set_queue(player, nullptr, 2U, 0U) == AG_INVALID_ARGUMENT);
    AG_CHECK(ag_player_set_queue(player, queue, 0U, 0U) == AG_INVALID_ARGUMENT);
    AG_CHECK(ag_player_set_queue(player, queue, 2U, 2U) == AG_INVALID_ARGUMENT);
    AG_CHECK(ag_player_set_queue(player, invalid_queue, 2U, 0U)
           == AG_INVALID_ARGUMENT);
    AG_CHECK(ag_player_set_scoped_queue(nullptr, queue, 2U, 0U, 1U, 0)
           == AG_INVALID_ARGUMENT);
    AG_CHECK(ag_player_set_scoped_queue(player, queue, 2U, 0U, 0U, 0)
           == AG_INVALID_ARGUMENT);
    AG_CHECK(ag_player_set_scoped_queue(player, queue, 2U, 1U, 1U, 0)
           == AG_INVALID_ARGUMENT);
    AG_CHECK(ag_player_set_scoped_queue(player, invalid_queue, 2U, 0U, 1U, 1)
           == AG_INVALID_ARGUMENT);
    AG_CHECK(ag_player_set_scoped_queue(player, queue, 2U, 0U, 1U, 1)
           == AG_OK);
    const char* scoped_queue[] = {
        argv[1], argv[2], argv[1], argv[2], argv[1], argv[2],
    };
    AG_CHECK(ag_player_set_scoped_queue(player, scoped_queue, 6U, 0U, 5U, 0)
           == AG_OK);
    AG_CHECK(ag_player_set_mode(player, AG_MODE_SEQUENTIAL) == AG_OK);
    for (std::size_t expected = 1U; expected < 5U; ++expected) {
        AG_CHECK(ag_player_next(player) == AG_OK);
        ag_playback_snapshot scoped_snapshot{};
        AG_CHECK(ag_player_snapshot(player, &scoped_snapshot) == AG_OK);
        AG_CHECK(scoped_snapshot.track_index == expected);
    }
    AG_CHECK(ag_player_next(player) == AG_OK);
    ag_playback_snapshot scoped_snapshot{};
    AG_CHECK(ag_player_snapshot(player, &scoped_snapshot) == AG_OK);
    AG_CHECK(scoped_snapshot.track_index == 0U);

    AG_CHECK(ag_player_set_mode(player, AG_MODE_REPEAT_ONE) == AG_OK);
    AG_CHECK(ag_player_next(player) == AG_OK);
    AG_CHECK(ag_player_snapshot(player, &scoped_snapshot) == AG_OK);
    AG_CHECK(scoped_snapshot.track_index == 0U);

    AG_CHECK(ag_player_set_mode(player, AG_MODE_SHUFFLE) == AG_OK);
    for (int transition = 0; transition < 12; ++transition) {
        AG_CHECK(ag_player_next(player) == AG_OK);
        AG_CHECK(ag_player_snapshot(player, &scoped_snapshot) == AG_OK);
        AG_CHECK(scoped_snapshot.track_index < 5U);
    }

    AG_CHECK(ag_player_set_scoped_queue(player, scoped_queue, 6U, 0U, 4U, 1)
           == AG_OK);
    AG_CHECK(ag_player_set_mode(player, AG_MODE_REPEAT_ALL) == AG_OK);
    for (std::size_t expected = 1U; expected <= 4U; ++expected) {
        AG_CHECK(ag_player_next(player) == AG_OK);
        AG_CHECK(ag_player_snapshot(player, &scoped_snapshot) == AG_OK);
        AG_CHECK(scoped_snapshot.track_index == expected);
    }
    AG_CHECK(ag_player_set_mode(player, static_cast<ag_playback_mode>(99))
           == AG_INVALID_ARGUMENT);
    AG_CHECK(ag_player_set_queue(player, queue, 2U, 0U) == AG_OK);
    AG_CHECK(ag_player_set_mode(player, AG_MODE_SEQUENTIAL) == AG_OK);
    AG_CHECK(ag_player_previous(player) == AG_INVALID_ARGUMENT);
    AG_CHECK(ag_player_set_mode(player, AG_MODE_REPEAT_ONE) == AG_OK);
    ag_playback_snapshot snapshot{};
    AG_CHECK(ag_player_snapshot(player, &snapshot) == AG_OK);
    AG_CHECK(snapshot.track_index == 0U);
    AG_CHECK(snapshot.track_count == 2U);
    AG_CHECK(snapshot.mode == AG_MODE_REPEAT_ONE);
    AG_CHECK(ag_player_previous(player) == AG_OK);
    AG_CHECK(ag_player_set_mode(player, AG_MODE_SHUFFLE) == AG_OK);
    AG_CHECK(ag_player_next(player) == AG_OK);
    AG_CHECK(ag_player_snapshot(player, &snapshot) == AG_OK);
    AG_CHECK(snapshot.track_index != 0U);
    ag_player_destroy(player);
    stage("all assertions complete; final engine destruction begin");
}
