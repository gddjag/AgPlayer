#pragma once

#include "audio_document.hpp"

#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace agplayer::editor {

[[nodiscard]] std::string recordingBackendErrorMessage(
    std::string_view operation, int resultCode);

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

enum class RecordingState { Idle, Starting, Recording, Paused, Finalizing, Error };

struct RecordingResult final {
    bool success{};
    std::string message;
    std::filesystem::path path;
    SampleFrame frames{};
    float peak{};
};

struct RecordingEnvelopePoint final {
    SampleFrame start_frame{};
    SampleFrame end_frame{};
    std::uint32_t channels{};
    std::array<float, 2> channel_minima{};
    std::array<float, 2> channel_maxima{};
};

struct RecordingLiveSnapshot final {
    SampleFrame frames_captured{};
    float interval_peak{};
    std::vector<RecordingEnvelopePoint> envelopes;
};

struct RecordingPcmSnapshot final {
    SampleFrame start_frame{};
    SampleFrame frames{};
    std::uint32_t channels{};
    std::vector<float> interleaved_samples;
};

class RecordingCapture {
public:
    virtual ~RecordingCapture() = default;
    [[nodiscard]] virtual bool start(const RecordingConfig& config) = 0;
    [[nodiscard]] virtual bool pause() noexcept = 0;
    [[nodiscard]] virtual bool resume() noexcept = 0;
    [[nodiscard]] virtual RecordingResult stop() = 0;
    [[nodiscard]] virtual bool cancel() = 0;
    [[nodiscard]] virtual RecordingState state() const noexcept = 0;
    [[nodiscard]] virtual SampleFrame framesCaptured() const noexcept = 0;
    [[nodiscard]] virtual RecordingLiveSnapshot takeLiveSnapshot(
        std::size_t maximum) = 0;
    [[nodiscard]] virtual RecordingPcmSnapshot takePcmSnapshot(
        SampleFrame startFrame, SampleFrame endFrame,
        std::size_t maximumFrames) const = 0;
    [[nodiscard]] virtual std::string lastError() const = 0;
};

class RecordingSession final : public RecordingCapture {
public:
    RecordingSession();
    ~RecordingSession();
    RecordingSession(const RecordingSession&) = delete;
    RecordingSession& operator=(const RecordingSession&) = delete;

    [[nodiscard]] static std::vector<RecordingDevice> inputDevices();
    [[nodiscard]] static std::vector<RecordingResult> recoverIncomplete(
        const std::filesystem::path& directory);
    [[nodiscard]] bool start(const RecordingConfig& config) override;
    [[nodiscard]] bool startManual(const RecordingConfig& config);
    [[nodiscard]] bool pause() noexcept override;
    [[nodiscard]] bool resume() noexcept override;
    [[nodiscard]] RecordingResult stop() override;
    [[nodiscard]] bool cancel() override;
    [[nodiscard]] std::size_t pushCapturedFrames(
        const float* interleaved, std::size_t frames) noexcept;

    [[nodiscard]] RecordingState state() const noexcept override;
    [[nodiscard]] float peak() const noexcept;
    [[nodiscard]] SampleFrame framesCaptured() const noexcept override;
    [[nodiscard]] std::uint64_t droppedFrames() const noexcept;
    [[nodiscard]] RecordingLiveSnapshot takeLiveSnapshot(
        std::size_t maximum) override;
    [[nodiscard]] RecordingPcmSnapshot takePcmSnapshot(
        SampleFrame startFrame, SampleFrame endFrame,
        std::size_t maximumFrames) const override;
    [[nodiscard]] std::vector<float> recentPeaks(std::size_t maximum) const;
    [[nodiscard]] std::string lastError() const override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace agplayer::editor
