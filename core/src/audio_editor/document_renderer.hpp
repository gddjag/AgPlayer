#pragma once

#include "audio_document.hpp"

#include <atomic>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace agplayer::editor {

struct RenderResult final {
    bool success{};
    SampleFrame frames{};
    std::uint32_t sample_rate{};
    std::uint32_t channels{};
    std::string message;
};

class DocumentRenderer final {
public:
    [[nodiscard]] RenderResult renderFloatWav(
        const TimelineSnapshot& snapshot,
        const std::optional<Selection>& range,
        const std::filesystem::path& output_path,
        const std::atomic_bool* cancelled = nullptr,
        std::function<void(float)> progress = {}) const;
};

} // namespace agplayer::editor
