#pragma once

#include "audio_document.hpp"

#include <atomic>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace agplayer::editor {

struct NoiseReductionResult final {
    bool success{};
    std::string message;
    std::filesystem::path output_path;
    std::uint32_t sample_rate{};
    std::uint32_t channels{};
    SampleFrame frames{};
};

class NoiseReducer final {
public:
    [[nodiscard]] static NoiseReductionResult reduce(
        const TimelineSnapshot& snapshot,
        const std::optional<Selection>& range,
        const std::filesystem::path& output_path,
        const std::atomic_bool* cancelled = nullptr,
        std::function<void(float)> progress = {});
};

} // namespace agplayer::editor
