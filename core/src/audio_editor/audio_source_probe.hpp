#pragma once

#include "audio_event.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>

namespace agplayer::editor {

struct AudioSourceProbeResult final {
    bool success{};
    bool timed_out{};
    AudioSource source;
    SampleFrame frame_tolerance{};
    std::string message;

    [[nodiscard]] bool ok() const noexcept { return success; }
    [[nodiscard]] bool matchesFormat(const AudioSource& expected) const noexcept;
};

class AudioSourceProbe final {
public:
    [[nodiscard]] static AudioSourceProbeResult probe(
        const std::filesystem::path& path) noexcept;
    [[nodiscard]] static AudioSourceProbeResult probe(
        const std::filesystem::path& path,
        std::chrono::steady_clock::time_point deadline,
        const std::atomic_bool* cancelled = nullptr) noexcept;
};

} // namespace agplayer::editor
