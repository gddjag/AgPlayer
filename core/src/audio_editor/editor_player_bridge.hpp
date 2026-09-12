#pragma once

#include "../audio_stream_source.hpp"

#include <agplayer/c_api.h>

#include <memory>

namespace agplayer::editor {

// Internal C++ bridge for attaching an editor PCM source to an existing
// ag_player. It deliberately stays outside the public stable C ABI.
[[nodiscard]] ag_result load_editor_playback_stream(
    ag_player* player, std::shared_ptr<agplayer::IAudioStreamSource> stream) noexcept;
[[nodiscard]] ag_result replace_editor_playback_stream(
    ag_player* player, std::shared_ptr<agplayer::IAudioStreamSource> stream) noexcept;

} // namespace agplayer::editor
