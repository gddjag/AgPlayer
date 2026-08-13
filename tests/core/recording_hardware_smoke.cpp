#include "audio_editor/recording_session.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <thread>

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::cerr << "usage: recording_hardware_smoke <output.wav> [seconds]\n";
        return 2;
    }

    const int seconds = argc >= 3 ? std::max(1, std::atoi(argv[2])) : 30;
    const auto devices = agplayer::editor::RecordingSession::inputDevices();
    if (devices.empty()) {
        std::cerr << "no WASAPI capture device found\n";
        return 3;
    }

    const auto selected = std::find_if(devices.begin(), devices.end(),
        [](const auto& device) { return device.is_default; });
    const auto& device = selected != devices.end() ? *selected : devices.front();
    const std::filesystem::path output = std::filesystem::u8path(argv[1]);

    agplayer::editor::RecordingSession session;
    agplayer::editor::RecordingConfig config;
    config.output_path = output;
    config.device_id = device.id;
    config.sample_rate = 48'000;
    config.channels = 2;
    config.monitor = false;

    if (!session.start(config)) {
        config.channels = 1;
        if (!session.start(config)) {
            std::cerr << "failed to start WASAPI capture: " << device.name << '\n';
            return 4;
        }
    }

    std::this_thread::sleep_for(std::chrono::seconds(seconds));
    const auto live_frames = session.framesCaptured();
    const auto live_peaks = session.recentPeaks(64);
    const auto result = session.stop();
    const auto minimum_frames = static_cast<agplayer::editor::SampleFrame>(
        config.sample_rate * std::max(1, seconds - 2));
    std::error_code error;
    const auto bytes = std::filesystem::file_size(output, error);
    if (!result.success || error || bytes <= 44 || result.frames < minimum_frames
        || live_frames == 0 || live_peaks.empty()) {
        std::cerr << "capture validation failed: message=" << result.message
                  << " frames=" << result.frames << " live=" << live_frames
                  << " bytes=" << (error ? 0 : bytes) << '\n';
        return 5;
    }

    std::cout << "device=" << device.name << " channels=" << config.channels
              << " frames=" << result.frames << " peak=" << result.peak
              << " bytes=" << bytes << '\n';
    return 0;
}
