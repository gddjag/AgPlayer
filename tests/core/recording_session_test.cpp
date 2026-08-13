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

    const fs::path rejected_output = fs::temp_directory_path()
        / "agplayer-recording-invalid-device.wav";
    fs::remove(rejected_output, ignored);
    RecordingConfig rejected_config;
    rejected_config.output_path = rejected_output;
    rejected_config.device_id = "capture:device-does-not-exist";
    RecordingSession rejected;
    require(!rejected.start(rejected_config),
            "invalid capture device was accepted");
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
    require(session.pause(), "recording pause failed");
    require(session.pushCapturedFrames(block.data(), 480) == 0,
            "paused recording accepted frames");
    require(session.resume(), "recording resume failed");
    require(session.pushCapturedFrames(block.data(), 480) == 480,
            "resumed recording rejected frames");
    const RecordingResult result = session.stop();
    require(result.success, "recording stop failed");
    require(result.frames == 960, "recorded frame count mismatch");
    require(result.peak > 0.24F && result.peak < 0.26F,
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
    fs::remove(output, ignored);
    fs::remove(recovered, ignored);
    return 0;
}
