#pragma once

#include "audio_document.hpp"

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace agplayer::editor {

struct AudioFileAnalysis final {
    bool success{};
    AudioSource source;
    std::string format;
    int bits_per_sample{};
    std::int64_t bit_rate{};
    std::vector<std::vector<float>> channel_peaks;
    std::vector<float> visual_mix_peaks;
    std::string message;
};

class AudioFileAnalyzer final {
public:
    [[nodiscard]] static AudioFileAnalysis analyze(
        const std::filesystem::path& path,
        std::size_t target_points,
        const std::atomic_bool* cancelled = nullptr) noexcept;
};

} // namespace agplayer::editor
