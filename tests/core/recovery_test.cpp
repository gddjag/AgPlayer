// Test files deliberately keep assert() active even in Release builds: many
// test cases embed function calls with side effects inside assert(), and
// silencing them under NDEBUG would skip those calls and crash on cleanup.
#undef NDEBUG

#include <agplayer/c_api.h>

#include "audio_engine.hpp"
#include "audio_stream_source.hpp"
#include "waveform_cache.hpp"

#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#ifdef _WIN32
#include <crtdbg.h>
#endif

namespace agplayer {

class AudioEngineTestAccess final {
public:
    static std::int64_t pendingBoundary(const AudioEngine& engine)
    {
        return engine.pending_boundary_for_testing();
    }
};

} // namespace agplayer

namespace {

class RecoveryRampStream final : public agplayer::IAudioStreamSource {
public:
    explicit RecoveryRampStream(const int sampleRate)
        : sample_rate_(sampleRate)
    {
        metadata_.sample_rate = sampleRate;
        metadata_.channels = 1;
        metadata_.duration_ms = 2'000;
    }

    const agplayer::MediaMetadata& metadata() const noexcept override
    {
        return metadata_;
    }

    ag_result read(agplayer::DecodedAudioBlock& block) noexcept override
    {
        const std::int64_t totalFrames = static_cast<std::int64_t>(
            sample_rate_) * 2;
        const std::size_t frames = static_cast<std::size_t>(
            std::max<std::int64_t>(0, std::min<std::int64_t>(
                64, totalFrames - position_)));
        block = {};
        block.frames = frames;
        block.timestamp_frame = position_;
        block.timestamp_ms = position_ * 1'000 / sample_rate_;
        block.samples.resize(frames);
        for (std::size_t frame = 0; frame < frames; ++frame) {
            block.samples[frame] = static_cast<float>(
                position_ + static_cast<std::int64_t>(frame))
                / static_cast<float>(sample_rate_);
        }
        position_ += static_cast<std::int64_t>(frames);
        block.end_of_stream = position_ >= totalFrames;
        return AG_OK;
    }

    ag_result seek(const std::int64_t positionMs) noexcept override
    {
        if (positionMs < 0 || positionMs > metadata_.duration_ms) {
            return AG_INVALID_ARGUMENT;
        }
        position_ = positionMs * sample_rate_ / 1'000;
        last_seek_ms_.store(positionMs, std::memory_order_release);
        seek_count_.fetch_add(1, std::memory_order_acq_rel);
        return AG_OK;
    }

    int seekCount() const noexcept
    {
        return seek_count_.load(std::memory_order_acquire);
    }

    std::int64_t lastSeekMs() const noexcept
    {
        return last_seek_ms_.load(std::memory_order_acquire);
    }

private:
    agplayer::MediaMetadata metadata_;
    int sample_rate_{};
    std::int64_t position_{};
    std::atomic<int> seek_count_{};
    std::atomic<std::int64_t> last_seek_ms_{-1};
};

void waitForFrames(agplayer::AudioEngine& engine, const std::size_t frames)
{
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(2);
    while (engine.buffered_frames() < frames
           && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    assert(engine.buffered_frames() >= frames);
}

template <typename Predicate>
void waitForPlayerSnapshot(ag_player* const player,
                           Predicate predicate,
                           ag_playback_snapshot& snapshot)
{
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(2);
    do {
        assert(ag_player_snapshot(player, &snapshot) == AG_OK);
        if (predicate(snapshot)) return;
        std::this_thread::yield();
    } while (std::chrono::steady_clock::now() < deadline);
    assert(predicate(snapshot));
}

void writeLittleEndian16(std::ofstream& output, const std::uint16_t value)
{
    const std::array<char, 2> bytes{
        static_cast<char>(value & 0xFFU),
        static_cast<char>((value >> 8U) & 0xFFU)};
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void writeLittleEndian32(std::ofstream& output, const std::uint32_t value)
{
    const std::array<char, 4> bytes{
        static_cast<char>(value & 0xFFU),
        static_cast<char>((value >> 8U) & 0xFFU),
        static_cast<char>((value >> 16U) & 0xFFU),
        static_cast<char>((value >> 24U) & 0xFFU)};
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void writePcm16RampWav(const std::filesystem::path& path,
                       const int sampleRate,
                       const std::size_t frames)
{
    assert(sampleRate > 0);
    assert(frames <= 30'000U);
    const std::uint32_t dataBytes = static_cast<std::uint32_t>(frames * 2U);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    assert(output);
    output.write("RIFF", 4);
    writeLittleEndian32(output, 36U + dataBytes);
    output.write("WAVEfmt ", 8);
    writeLittleEndian32(output, 16U);
    writeLittleEndian16(output, 1U);
    writeLittleEndian16(output, 1U);
    writeLittleEndian32(output, static_cast<std::uint32_t>(sampleRate));
    writeLittleEndian32(output,
                        static_cast<std::uint32_t>(sampleRate * 2));
    writeLittleEndian16(output, 2U);
    writeLittleEndian16(output, 16U);
    output.write("data", 4);
    writeLittleEndian32(output, dataBytes);
    for (std::size_t frame = 0U; frame < frames; ++frame) {
        writeLittleEndian16(output, static_cast<std::uint16_t>(1'000U + frame));
    }
    assert(output);
}

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

    // 2. Device loss preserves the exact consumed source frame, including
    // recovery points which do not land on a whole millisecond.
    for (const auto [rate, consumedFrames, expectedSeekMs] : {
             std::tuple{48'000, std::size_t{25U}, std::int64_t{0}},
             std::tuple{44'100, std::size_t{45U}, std::int64_t{1}}}) {
        auto stream = std::make_shared<RecoveryRampStream>(rate);
        agplayer::AudioEngine engine(agplayer::AudioBackend::Manual, 256U);
        assert(engine.load_stream(stream) == AG_OK);
        assert(engine.play() == AG_OK);
        waitForFrames(engine, 256U);
        std::vector<float> first(consumedFrames);
        engine.render(first.data(), first.size());

        engine.simulate_device_loss();
        assert(engine.device_lost());
        assert(engine.retry_device() == AG_OK);
        assert(stream->seekCount() == 1);
        assert(stream->lastSeekMs() == expectedSeekMs);
        assert(engine.play() == AG_OK);
        waitForFrames(engine, 1U);
        float recovered = -1.0F;
        engine.render(&recovered, 1U);
        const float expected = static_cast<float>(consumedFrames)
            / static_cast<float>(rate);
        assert(std::abs(recovered - expected) < 0.000001F);

        auto outputStream = std::make_shared<RecoveryRampStream>(rate);
        agplayer::AudioEngine outputEngine(
            agplayer::AudioBackend::Manual, 256U);
        assert(outputEngine.load_stream(outputStream) == AG_OK);
        assert(outputEngine.play() == AG_OK);
        waitForFrames(outputEngine, 256U);
        std::vector<float> outputPrefix(consumedFrames);
        outputEngine.render(outputPrefix.data(), outputPrefix.size());
        outputEngine.simulate_device_loss();
        assert(outputEngine.device_lost());
        assert(outputEngine.set_output_device("invalid", false)
               == AG_INVALID_ARGUMENT);
        assert(outputEngine.device_lost());
        assert(outputEngine.set_output_device("", false) == AG_OK);
        assert(!outputEngine.device_lost());
        assert(outputStream->seekCount() == 1);
        assert(outputStream->lastSeekMs() == expectedSeekMs);
        assert(outputEngine.play() == AG_OK);
        waitForFrames(outputEngine, 1U);
        float outputRecovered = -1.0F;
        outputEngine.render(&outputRecovered, 1U);
        assert(std::abs(outputRecovered - expected) < 0.000001F);
    }

    // Real file-backed recovery uses Decoder::seekFrame(), so the first PCM
    // sample after recovery must be exact even below millisecond resolution.
    for (const auto [rate, consumedFrames, useRetry] : {
             std::tuple{48'000, std::size_t{25U}, true},
             std::tuple{44'100, std::size_t{45U}, false}}) {
        const std::filesystem::path path =
            work_dir / ("frame-exact-" + std::to_string(rate) + ".wav");
        writePcm16RampWav(path, rate, 2'048U);
        agplayer::AudioEngine engine(agplayer::AudioBackend::Manual, 256U);
        assert(engine.load(path.string()) == AG_OK);
        assert(engine.set_output_device("", false) == AG_OK);
        assert(engine.set_output_device("invalid", false)
               == AG_INVALID_ARGUMENT);
        assert(engine.set_output_device("", true) == AG_INVALID_ARGUMENT);
        assert(engine.play() == AG_OK);
        waitForFrames(engine, 256U);
        std::vector<float> prefix(consumedFrames);
        engine.render(prefix.data(), prefix.size());
        engine.simulate_device_loss();
        assert(engine.device_lost());
        assert(engine.set_output_device("invalid", false)
               == AG_INVALID_ARGUMENT);
        assert(engine.set_output_device("", true) == AG_INVALID_ARGUMENT);
        assert(engine.device_lost());
        const ag_result recoveryResult = useRetry
            ? engine.retry_device()
            : engine.set_output_device("", false);
        assert(recoveryResult == AG_OK);
        assert(!engine.device_lost());
        assert(engine.play() == AG_OK);
        waitForFrames(engine, 16U);
        std::array<float, 16> recovered{};
        engine.render(recovered.data(), recovered.size());
        for (std::size_t frame = 0U; frame < recovered.size(); ++frame) {
            const float expected = static_cast<float>(
                1'000U + consumedFrames + frame) / 32'768.0F;
            assert(std::abs(recovered[frame] - expected) < 0.000001F);
        }
    }

    // 3. C API device loss simulation and recovery
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

        ag_playback_snapshot snapshot{};
        waitForPlayerSnapshot(player, [](const ag_playback_snapshot& value) {
            return value.state == AG_PLAYING && value.position_ms > 0;
        }, snapshot);
        const std::int64_t firstRecoveryAnchor = snapshot.position_ms;

        // Simulate device loss
        const ag_result loss_result = ag_player_simulate_device_loss(player);
        assert(loss_result == AG_OK);
        const int lost = ag_player_device_lost(player);
        assert(lost == 1);
        waitForPlayerSnapshot(player, [](const ag_playback_snapshot& value) {
            return value.state == AG_PAUSED || value.state == AG_STOPPED
                || value.state == AG_ERROR;
        }, snapshot);

        // Retry device
        const ag_result retry_result = ag_player_retry_device(player);
        assert(retry_result == AG_OK);
        const int lost2 = ag_player_device_lost(player);
        assert(lost2 == 0);
        assert(ag_player_play(player) == AG_OK);
        waitForPlayerSnapshot(player,
                              [firstRecoveryAnchor](
                                  const ag_playback_snapshot& value) {
                                  return value.state == AG_PLAYING
                                      && value.position_ms
                                          > firstRecoveryAnchor;
                              }, snapshot);

        const std::int64_t secondRecoveryAnchor = snapshot.position_ms;
        assert(ag_player_simulate_device_loss(player) == AG_OK);
        assert(ag_player_device_lost(player) == 1);
        assert(ag_player_set_output_device(player, "", 0) == AG_OK);
        assert(ag_player_device_lost(player) == 0);
        assert(ag_player_play(player) == AG_OK);
        waitForPlayerSnapshot(player,
                              [secondRecoveryAnchor](
                                  const ag_playback_snapshot& value) {
                                  return value.state == AG_PLAYING
                                      && value.position_ms
                                          > secondRecoveryAnchor;
                              }, snapshot);

        ag_player_destroy(player);
    }

    // 4. A deleted published source fails recovery without corrupting the
    // session, then succeeds when the same path becomes available again.
    {
        const std::filesystem::path missing_source =
            work_dir / "deleted-source.wav";
        const std::filesystem::path prefetched_source =
            work_dir / "deleted-source-next.wav";
        writePcm16RampWav(missing_source, 48'000, 128U);
        writePcm16RampWav(prefetched_source, 48'000, 2'048U);

        agplayer::AudioEngine engine(agplayer::AudioBackend::Manual, 512U);
        assert(engine.set_queue(
                   {missing_source.string(), prefetched_source.string()}, 0U)
               == AG_OK);
        assert(engine.play() == AG_OK);
        const auto prefetchDeadline = std::chrono::steady_clock::now()
            + std::chrono::seconds(2);
        while (agplayer::AudioEngineTestAccess::pendingBoundary(engine)
                   != 128
               && std::chrono::steady_clock::now() < prefetchDeadline) {
            std::this_thread::yield();
        }
        assert(agplayer::AudioEngineTestAccess::pendingBoundary(engine)
               == 128);
        std::array<float, 25> consumed{};
        engine.render(consumed.data(), consumed.size());

        std::error_code remove_ec;
        const bool removed = std::filesystem::remove(missing_source, remove_ec);
        assert(removed);
        assert(!remove_ec);
        assert(!std::filesystem::exists(missing_source));

        engine.simulate_device_loss();
        assert(engine.device_lost());
        assert(engine.retry_device() != AG_OK);
        assert(engine.device_lost());
        assert(engine.snapshot().state == agplayer::EngineState::Error);

        writePcm16RampWav(missing_source, 48'000, 128U);
        assert(engine.retry_device() == AG_OK);
        assert(!engine.device_lost());
        assert(engine.play() == AG_OK);
        waitForFrames(engine, 16U);
        std::array<float, 16> recovered{};
        engine.render(recovered.data(), recovered.size());
        for (std::size_t frame = 0U; frame < recovered.size(); ++frame) {
            const float expected = static_cast<float>(1'025U + frame)
                / 32'768.0F;
            assert(std::abs(recovered[frame] - expected) < 0.000001F);
        }
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
            ag_playback_snapshot truncatedSnapshot{};
            waitForPlayerSnapshot(
                player, [](const ag_playback_snapshot& value) {
                    return value.position_ms > 0 || value.state == AG_ERROR
                        || value.state == AG_STOPPED;
                }, truncatedSnapshot);
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
        ag_playback_snapshot snapshot{};
        waitForPlayerSnapshot(player, [](const ag_playback_snapshot& value) {
            return value.state == AG_PLAYING && value.position_ms > 0;
        }, snapshot);

        const ag_result sl7 = ag_player_simulate_device_loss(player);
        assert(sl7 == AG_OK);
        // Process still alive: snapshot works
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
