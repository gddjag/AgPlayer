#pragma once

#include "audio_engine.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace agplayer {

class CoreContext final {
public:
    CoreContext(AudioBackend backend, std::size_t buffer_frames);

    [[nodiscard]] const std::string& last_error() const noexcept;
    void set_error(std::string value) noexcept;
    ag_result load(const std::string& utf8_path) noexcept;
    ag_result play() noexcept;
    ag_result pause() noexcept;
    ag_result stop() noexcept;
    ag_result seek(std::int64_t position_ms) noexcept;
    ag_result set_volume(float volume) noexcept;
    void set_muted(bool muted) noexcept;
    [[nodiscard]] EngineSnapshot snapshot() const noexcept;

private:
    ag_result record(ag_result result, const char* operation) noexcept;

    AudioEngine audio_engine_;
    std::string last_error_;
};

} // namespace agplayer
