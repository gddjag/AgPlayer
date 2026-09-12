#pragma once

#include "audio_engine.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace agplayer {

struct StemPreviewSource {
    std::string id;
    std::string path;
    float gain = 1.0F;
};

struct StemPreviewSnapshot {
    EngineState state = EngineState::Stopped;
    std::int64_t position_ms = 0;
    std::int64_t duration_ms = 0;
};

// A short-lived preview mixer for already separated stems. Each source is
// decoded by a manual AudioEngine, while this class owns the only output
// device and therefore keeps every source on one playback clock.
class StemPreviewMixer final {
public:
    StemPreviewMixer(AudioBackend backend, std::size_t buffer_frames);
    ~StemPreviewMixer();

    StemPreviewMixer(const StemPreviewMixer&) = delete;
    StemPreviewMixer& operator=(const StemPreviewMixer&) = delete;

    ag_result load(std::vector<StemPreviewSource> sources,
                   std::int64_t position_ms) noexcept;
    ag_result play() noexcept;
    ag_result pause() noexcept;
    ag_result seek(std::int64_t position_ms) noexcept;
    ag_result stop() noexcept;
    ag_result set_gain(std::string_view id, float gain) noexcept;

    [[nodiscard]] StemPreviewSnapshot snapshot() const noexcept;

    // Used by the output callback and by deterministic Manual-backend tests.
    void render(float* output, std::size_t requested_frames) noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace agplayer
