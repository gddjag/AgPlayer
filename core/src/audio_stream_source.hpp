#pragma once

#include "decoder.hpp"

#include <agplayer/c_api.h>

#include <cstdint>

namespace agplayer {

// Decode-thread source boundary used by AudioEngine. Implementations may read
// files, timelines, or another bounded PCM source; AudioEngine remains the
// sole owner of the output device and real-time callback.
class IAudioStreamSource {
public:
    virtual ~IAudioStreamSource() = default;

    [[nodiscard]] virtual const MediaMetadata& metadata() const noexcept = 0;
    [[nodiscard]] virtual ag_result read(DecodedAudioBlock& block) noexcept = 0;
    [[nodiscard]] virtual ag_result seek(std::int64_t position_ms) noexcept = 0;
};

} // namespace agplayer
