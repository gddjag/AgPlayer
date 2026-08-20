#pragma once

#include "audio_event.hpp"

#include <filesystem>
#include <string>

namespace agplayer::editor {

struct AudioSourceProbeResult final {
    bool success{};
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
};

} // namespace agplayer::editor
