#pragma once

#include <agplayer/c_api.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace agplayer {

enum class AudioBackend {
    Default,
    Null,
};

enum class EngineState {
    Stopped,
    Loading,
    Playing,
    Paused,
    Error,
};

struct EngineSnapshot {
    EngineState state = EngineState::Stopped;
    std::int64_t position_ms = 0;
    std::int64_t duration_ms = 0;
    float volume = 1.0F;
    bool muted = false;
};

class AudioEngine final {
public:
    AudioEngine(AudioBackend backend, std::size_t buffer_frames);
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    ag_result load(const std::string& utf8_path) noexcept;
    ag_result play() noexcept;
    ag_result pause() noexcept;
    ag_result stop() noexcept;
    ag_result seek(std::int64_t position_ms) noexcept;
    ag_result set_volume(float volume) noexcept;
    void set_muted(bool muted) noexcept;
    [[nodiscard]] EngineSnapshot snapshot() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace agplayer
