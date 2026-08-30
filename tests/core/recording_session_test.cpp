#include "audio_editor/recording_session.hpp"
#include "decoder.hpp"

#include <agplayer/c_api.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>

namespace {

[[noreturn]] void fail(const char* message)
{
    std::cerr << message << '\n';
    std::exit(1);
}

void require(const bool condition, const char* message)
{
    if (!condition) fail(message);
}

} // namespace

int main()
{
    namespace fs = std::filesystem;
    using namespace agplayer::editor;
    const fs::path output = fs::temp_directory_path()
        / "agplayer-recording-session-test.wav";
    std::error_code ignored;
    fs::remove(output, ignored);

    const fs::path rejected_output = fs::temp_directory_path()
        / "agplayer-recording-invalid-device.wav";
    fs::remove(rejected_output, ignored);
    RecordingConfig rejected_config;
    rejected_config.output_path = rejected_output;
    rejected_config.device_id = "capture:device-does-not-exist";
    RecordingSession rejected;
    require(!rejected.start(rejected_config),
            "invalid capture device was accepted");
    require(!rejected.lastError().empty(),
            "failed recording start did not publish a diagnostic");
    require(!fs::exists(rejected_output),
            "failed recording start created an output file");

    RecordingConfig config;
    config.output_path = output;
    config.sample_rate = 48'000;
    config.channels = 2;
    RecordingSession session;
    require(session.startManual(config), "manual recording start failed");
    std::vector<float> block(480U * 2U, 0.25F);
    require(session.pushCapturedFrames(block.data(), 480) == 480,
            "captured frames not accepted");
    std::vector<float> asymmetric(64U * 2U, 0.0F);
    for (std::size_t frame = 0; frame < 64U; ++frame) {
        asymmetric[frame * 2U] = 0.125F;
        asymmetric[frame * 2U + 1U] = -0.5F;
    }
    require(session.pushCapturedFrames(asymmetric.data(), 64) == 64,
            "asymmetric captured frames not accepted");
    const auto meterDeadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(2);
    while (session.framesMetered() < 544
           && std::chrono::steady_clock::now() < meterDeadline) {
        std::this_thread::yield();
    }
    require(session.framesMetered() >= 544,
            "writer-side meter aggregation did not consume captured frames");
    require(session.recordingChannels() == 2
                && session.recordingSampleRate() == 48'000,
            "actual recording format was not published");
    const auto channel_peaks = session.recentChannelPeaks(32);
    require(channel_peaks.size() == 2 && !channel_peaks[0].empty()
                && channel_peaks[0].size() <= 32
                && channel_peaks[1].size() == channel_peaks[0].size(),
            "bounded channel peak history was not published");
    require(channel_peaks[0].back() > 0.12F
                && channel_peaks[0].back() < 0.13F
                && channel_peaks[1].back() > 0.49F,
            "channel peak history did not preserve real channel levels");
    const auto peak_levels = session.livePeakLevels();
    const auto rms_levels = session.liveRmsLevels();
    require(peak_levels.size() == 2 && rms_levels.size() == 2
                && peak_levels[1] > peak_levels[0]
                && rms_levels[1] > rms_levels[0],
            "real per-channel peak/RMS meter data was not published");
    require(session.pause(), "recording pause failed");
    require(session.pushCapturedFrames(block.data(), 480) == 0,
            "paused recording accepted frames");
    require(session.resume(), "recording resume failed");
    require(session.pushCapturedFrames(block.data(), 480) == 480,
            "resumed recording rejected frames");
    const auto live_peaks = session.recentPeaks(32);
    require(!live_peaks.empty() && live_peaks.size() <= 32,
            "live recording peak snapshot was not published");
    require(live_peaks.back() > 0.0F,
            "live recording peak snapshot lost the captured signal");
    const RecordingResult result = session.stop();
    require(result.success, "recording stop failed");
    require(result.frames == 1'024, "recorded frame count mismatch");
    require(result.peak > 0.49F && result.peak <= 0.5F,
            "input peak mismatch");

    const fs::path concurrent_output = fs::temp_directory_path()
        / "agplayer-recording-concurrent-meter-test.wav";
    fs::remove(concurrent_output, ignored);
    RecordingConfig concurrent_config = config;
    concurrent_config.output_path = concurrent_output;
    concurrent_config.sample_rate = 8'000;
    RecordingSession concurrent_session;
    require(concurrent_session.startManual(concurrent_config),
            "concurrent recording start failed");
    std::atomic<bool> producing{true};
    std::atomic<bool> coherent{true};
    std::thread producer([&] {
        for (std::size_t index = 0; index < 50'000U; ++index) {
            const float left = 0.1F + static_cast<float>(index % 8U) * 0.1F;
            const std::array<float, 2> sample{left, left * 0.5F};
            (void)concurrent_session.pushCapturedFrames(sample.data(), 1U);
        }
        producing.store(false, std::memory_order_release);
    });
    while (producing.load(std::memory_order_acquire)) {
        const auto peaks = concurrent_session.recentChannelPeaks(64U);
        if (peaks.size() != 2U || peaks[0].size() != peaks[1].size()) {
            coherent.store(false, std::memory_order_relaxed);
            break;
        }
        for (std::size_t index = 0; index < peaks[0].size(); ++index) {
            if (peaks[0][index] <= 0.0F
                || std::abs(peaks[1][index] * 2.0F - peaks[0][index])
                       > 0.0001F) {
                coherent.store(false, std::memory_order_relaxed);
                break;
            }
        }
    }
    producer.join();
    require(coherent.load(std::memory_order_relaxed),
            "concurrent peak snapshot observed a partially published slot");
    require(concurrent_session.stop().success,
            "concurrent recording stop failed");
    fs::remove(concurrent_output, ignored);

    agplayer::Decoder decoder;
    require(decoder.open(output.u8string(), 48'000, 2) == AG_OK,
            "recording output is not decodable");
    agplayer::DecodedAudioBlock decoded;
    std::int64_t frames = 0;
    do {
        require(decoder.read(decoded) == AG_OK, "recording decode failed");
        frames += static_cast<std::int64_t>(decoded.frames);
    } while (!decoded.end_of_stream);
    require(frames == 1'024, "decoded recording frame count mismatch");
    const fs::path recovered = fs::temp_directory_path()
        / "agplayer-recording-recovered.wav";
    const fs::path staged = fs::path(recovered.u8string()
        + ".agplayer-recording.tmp");
    const fs::path journal = fs::path(recovered.u8string()
        + ".agplayer-recording.journal");
    fs::remove(recovered, ignored);
    fs::remove(staged, ignored);
    fs::remove(journal, ignored);
    fs::copy_file(output, staged, fs::copy_options::overwrite_existing);
    {
        std::ofstream stream(journal, std::ios::trunc);
        stream << staged.u8string() << '\n' << recovered.u8string() << '\n'
               << 48'000 << '\n' << 2 << '\n';
    }
    const auto recovered_results = RecordingSession::recoverIncomplete(
        fs::temp_directory_path());
    const auto match = std::find_if(
        recovered_results.begin(), recovered_results.end(),
        [&recovered](const RecordingResult& item) {
            return item.path == recovered;
        });
    require(match != recovered_results.end() && match->success,
            "recording journal was not recovered");
    require(fs::exists(recovered), "recovered recording is missing");
    require(!fs::exists(staged) && !fs::exists(journal),
            "recording recovery did not clean staged files");

    const fs::path cancelled = fs::temp_directory_path()
        / "agplayer-recording-cancelled.wav";
    fs::remove(cancelled, ignored);
    RecordingConfig cancel_config = config;
    cancel_config.output_path = cancelled;
    RecordingSession cancelled_session;
    require(cancelled_session.startManual(cancel_config),
            "cancel recording start failed");
    require(cancelled_session.pushCapturedFrames(block.data(), 480) == 480,
            "cancel recording did not accept frames");
    require(cancelled_session.cancel(), "recording cancel failed");
    require(!fs::exists(cancelled), "cancelled recording was committed");
    require(!fs::exists(fs::path(cancelled.u8string()
                + ".agplayer-recording.tmp"))
            && !fs::exists(fs::path(cancelled.u8string()
                + ".agplayer-recording.journal")),
            "cancelled recording left recovery artifacts");
    fs::remove(output, ignored);
    fs::remove(recovered, ignored);
    return 0;
}
