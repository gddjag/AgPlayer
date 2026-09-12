#pragma once

#include "../../../core/src/audio_editor/audio_document.hpp"
#include "../../../core/src/audio_editor/editor_playback_stream.hpp"

#include <agplayer/c_api.h>

#include <QString>

struct ag_player;
class PlaybackController;

// Owns only editor stream state. The actual output device and realtime thread
// remain owned by the application's existing ag_player/AudioEngine instance.
class EditorPlaybackAdapter final {
public:
    explicit EditorPlaybackAdapter(ag_player* player = nullptr,
                                   PlaybackController* owner = nullptr) noexcept;

    void attach(ag_player* player, PlaybackController* owner) noexcept;
    [[nodiscard]] bool available() const noexcept { return player_ != nullptr; }
    [[nodiscard]] bool prepare(
        agplayer::editor::TimelineSnapshot snapshot,
        const agplayer::editor::EditorPlaybackParameters& parameters,
        QString& error);
    [[nodiscard]] ag_result play() noexcept;
    [[nodiscard]] ag_result pause() noexcept;
    [[nodiscard]] ag_result stop() noexcept;
    [[nodiscard]] ag_result seek(qint64 positionMs) noexcept;
    [[nodiscard]] ag_result snapshot(ag_playback_snapshot& value) const noexcept;
    void release() noexcept;

private:
    ag_player* player_{};
    PlaybackController* owner_{};
};
