#include "audio_editor/recording_session.hpp"
#include "decoder.hpp"

#include <agplayer/c_api.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
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

    const std::string no_device_diagnostic = recordingBackendErrorMessage(
        "WASAPI capture initialization failed", -204);
    require(no_device_diagnostic.find("category=no-device")
                != std::string::npos
            && no_device_diagnostic.find("miniaudio=-204")
                != std::string::npos
            && no_device_diagnostic.find("device") != std::string::npos,
            "miniaudio failure diagnostic lost category, code, or description");

    RecordingSession invalid_config_session;
    RecordingConfig invalid_config;
    invalid_config.sample_rate = 1;
    require(!invalid_config_session.startManual(invalid_config),
            "invalid recording config was accepted");
    require(!invalid_config_session.lastError().empty(),
            "invalid recording config did not expose an error");

    const fs::path rejected_output = fs::temp_directory_path()
        / "agplayer-recording-invalid-device.wav";
    fs::remove(rejected_output, ignored);
    RecordingConfig rejected_config;
    rejected_config.output_path = rejected_output;
    rejected_config.device_id = "capture:device-does-not-exist";
    RecordingSession rejected;
    require(!rejected.start(rejected_config),
            "invalid capture device was accepted");
    require(rejected.lastError().find("WASAPI") != std::string::npos
            && rejected.lastError().find("device") != std::string::npos,
            "invalid capture device did not expose a specific WASAPI error");
    require(!fs::exists(rejected_output),
            "failed recording start created an output file");
    const float rejected_sample = 0.5F;
    require(rejected.pushCapturedFrames(&rejected_sample, 1) == 0
            && rejected.framesCaptured() == 0,
            "failed recording start left callback acceptance enabled");

    RecordingConfig config;
    config.output_path = output;
    config.sample_rate = 48'000;
    config.channels = 2;
    RecordingSession session;
    require(session.startManual(config), "manual recording start failed");
    std::vector<float> block(480U * 2U);
    for (std::size_t frame = 0; frame < 480U; ++frame) {
        block[frame * 2U] = frame % 2U == 0U ? -0.75F : 0.25F;
        block[frame * 2U + 1U] = frame % 2U == 0U ? -0.125F : 0.5F;
    }
    require(session.pushCapturedFrames(block.data(), 480) == 480,
            "captured frames not accepted");
    const RecordingLiveSnapshot active = session.takeLiveSnapshot(32);
    require(active.frames_captured == 480,
            "live snapshot frame count mismatch");
    require(active.interval_peak > 0.74F && active.interval_peak < 0.76F,
            "live interval level did not publish the active signal");
    require(active.envelopes.size() == 1
            && active.envelopes.front().channels == 2,
            "live snapshot did not preserve capture channels");
    require(active.envelopes.front().channel_minima[0] < -0.74F
            && active.envelopes.front().channel_maxima[0] > 0.24F
            && active.envelopes.front().channel_minima[1] < -0.12F
            && active.envelopes.front().channel_maxima[1] > 0.49F,
            "live snapshot lost per-channel waveform extrema");
    require(session.pause(), "recording pause failed");
    require(session.pushCapturedFrames(block.data(), 480) == 0,
            "paused recording accepted frames");
    require(session.resume(), "recording resume failed");
    std::fill(block.begin(), block.end(), 0.0F);
    require(session.pushCapturedFrames(block.data(), 480) == 480,
            "resumed recording rejected frames");
    const RecordingLiveSnapshot quiet = session.takeLiveSnapshot(32);
    require(quiet.frames_captured == 960 && quiet.envelopes.size() == 1,
            "quiet live snapshot did not advance recording state");
    require(quiet.interval_peak == 0.0F,
            "live input level retained the historical session maximum");
    require(quiet.envelopes.front().channel_minima[0] == 0.0F
            && quiet.envelopes.front().channel_maxima[0] == 0.0F
            && quiet.envelopes.front().channel_minima[1] == 0.0F
            && quiet.envelopes.front().channel_maxima[1] == 0.0F,
            "quiet live snapshot did not publish a falling waveform");
    require(session.peak() > 0.74F && session.peak() < 0.76F,
            "session peak hold was not retained separately");
    const RecordingResult result = session.stop();
    require(result.success, "recording stop failed");
    require(result.frames == 960, "recorded frame count mismatch");
    require(result.peak > 0.74F && result.peak < 0.76F,
            "input peak mismatch");

    agplayer::Decoder decoder;
    require(decoder.open(output.u8string(), 48'000, 2) == AG_OK,
            "recording output is not decodable");
    agplayer::DecodedAudioBlock decoded;
    std::int64_t frames = 0;
    do {
        require(decoder.read(decoded) == AG_OK, "recording decode failed");
        frames += static_cast<std::int64_t>(decoded.frames);
    } while (!decoded.end_of_stream);
    require(frames == 960, "decoded recording frame count mismatch");
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
    require(cancelled_session.pushCapturedFrames(block.data(), 480) == 0,
            "cancelled recording still accepted callback frames");
    require(!fs::exists(cancelled), "cancelled recording was committed");
    require(!fs::exists(fs::path(cancelled.u8string()
                + ".agplayer-recording.tmp"))
            && !fs::exists(fs::path(cancelled.u8string()
                + ".agplayer-recording.journal")),
            "cancelled recording left recovery artifacts");

    const fs::path pcm_window_output = fs::temp_directory_path()
        / "agplayer-recording-pcm-window-test.wav";
    fs::remove(pcm_window_output, ignored);
    RecordingConfig pcm_window_config = config;
    pcm_window_config.output_path = pcm_window_output;
    pcm_window_config.channels = 1;
    RecordingSession pcm_window_session;
    require(pcm_window_session.startManual(pcm_window_config),
            "PCM window recording start failed");
    constexpr std::size_t pushed_frames = 70'000;
    constexpr std::size_t expected_capacity = 65'536;
    std::vector<float> pcm_window_input(pushed_frames);
    for (std::size_t frame = 0; frame < pcm_window_input.size(); ++frame) {
        pcm_window_input[frame] = static_cast<float>(
            static_cast<int>(frame % 101U) - 50) / 50.0F;
    }
    require(pcm_window_session.pushCapturedFrames(
                pcm_window_input.data(), pcm_window_input.size())
            == pcm_window_input.size(),
            "long recording did not accept the deterministic PCM window");
    const RecordingPcmSnapshot pcm_window =
        pcm_window_session.takePcmSnapshot(0, pushed_frames, pushed_frames);
    require(pcm_window.channels == 1
            && pcm_window.frames == expected_capacity
            && pcm_window.start_frame == pushed_frames - expected_capacity
            && pcm_window.interleaved_samples.size() == expected_capacity,
            "live PCM snapshot was not bounded to its fixed ring capacity");
    require(std::abs(pcm_window.interleaved_samples.front()
                     - pcm_window_input[pushed_frames - expected_capacity])
                < 0.000001F
            && std::abs(pcm_window.interleaved_samples.back()
                        - pcm_window_input.back()) < 0.000001F,
            "bounded live PCM snapshot lost the retained sample shape");
    require(pcm_window_session.cancel(),
            "PCM window recording cancel failed");
    require(!fs::exists(pcm_window_output),
            "PCM window test committed a cancelled recording");
    fs::remove(output, ignored);
    fs::remove(recovered, ignored);
    return 0;
}
