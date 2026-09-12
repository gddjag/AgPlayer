#include "editor_playback_adapter.hpp"

#include "../../../core/src/audio_editor/editor_player_bridge.hpp"
#include "../playback_controller.hpp"

EditorPlaybackAdapter::EditorPlaybackAdapter(
    ag_player* const player, PlaybackController* const owner) noexcept
    : player_(player), owner_(owner)
{
}

void EditorPlaybackAdapter::attach(
    ag_player* const player, PlaybackController* const owner) noexcept
{
    release();
    player_ = player;
    owner_ = owner;
}

bool EditorPlaybackAdapter::prepare(
    agplayer::editor::TimelineSnapshot snapshot,
    const agplayer::editor::EditorPlaybackParameters& parameters,
    QString& error)
{
    if (player_ == nullptr || (owner_ != nullptr && !owner_->acquireEditorOutput())) {
        error = QStringLiteral("Playback core is unavailable");
        return false;
    }
    std::string streamError;
    auto stream = agplayer::editor::EditorPlaybackStream::create(
        std::move(snapshot), parameters, streamError);
    if (!stream) {
        error = QString::fromStdString(streamError);
        release();
        return false;
    }
    const ag_result result = agplayer::editor::replace_editor_playback_stream(
        player_, std::move(stream));
    if (result != AG_OK) {
        error = QStringLiteral("Unable to attach editor playback stream");
        release();
        return false;
    }
    return true;
}

ag_result EditorPlaybackAdapter::play() noexcept
{
    return player_ != nullptr ? ag_player_play(player_) : AG_INVALID_ARGUMENT;
}

ag_result EditorPlaybackAdapter::pause() noexcept
{
    return player_ != nullptr ? ag_player_pause(player_) : AG_INVALID_ARGUMENT;
}

ag_result EditorPlaybackAdapter::stop() noexcept
{
    return player_ != nullptr ? ag_player_stop(player_) : AG_INVALID_ARGUMENT;
}

ag_result EditorPlaybackAdapter::seek(const qint64 positionMs) noexcept
{
    return player_ != nullptr ? ag_player_seek(player_, positionMs)
                              : AG_INVALID_ARGUMENT;
}

ag_result EditorPlaybackAdapter::snapshot(
    ag_playback_snapshot& value) const noexcept
{
    return player_ != nullptr ? ag_player_snapshot(player_, &value)
                              : AG_INVALID_ARGUMENT;
}

void EditorPlaybackAdapter::release() noexcept
{
    if (owner_ != nullptr) owner_->releaseEditorOutput();
}
