#pragma once

#include "audio_document.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace agplayer::editor {

struct RecordingDevice final {
    std::string id;
    std::string name;
    bool is_default{};
};

struct RecordingConfig final {
    std::filesystem::path output_path;
    std::string device_id;
    std::uint32_t sample_rate{48'000};
    std::uint32_t channels{2};
    bool monitor{};
};

enum class RecordingState { Idle, Recording, Paused, Finalizing, Error };

struct RecordingResult final {
    bool success{};
    std::string message;
    std::filesystem::path path;
    SampleFrame frames{};
    float peak{};
};

class RecordingSession final {
public:
    RecordingSession();
    ~RecordingSession();
    RecordingSession(const RecordingSession&) = delete;
    RecordingSession& operator=(const RecordingSession&) = delete;

    [[nodiscard]] static std::vector<RecordingDevice> inputDevices();
    [[nodiscard]] static std::vector<RecordingResult> recoverIncomplete(
        const std::filesystem::path& directory);
    [[nodiscard]] bool start(const RecordingConfig& config);
    [[nodiscard]] bool startManual(const RecordingConfig& config);
    [[nodiscard]] bool pause() noexcept;
    [[nodiscard]] bool resume() noexcept;
    [[nodiscard]] RecordingResult stop();
    [[nodiscard]] bool cancel();
    [[nodiscard]] std::size_t pushCapturedFrames(
        const float* interleaved, std::size_t frames) noexcept;

    [[nodiscard]] RecordingState state() const noexcept;
    [[nodiscard]] float peak() const noexcept;
    [[nodiscard]] SampleFrame framesCaptured() const noexcept;
    [[nodiscard]] std::uint64_t droppedFrames() const noexcept;
    [[nodiscard]] std::vector<float> recentPeaks(std::size_t maximum) const;
    [[nodiscard]] std::string lastError() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace agplayer::editor
