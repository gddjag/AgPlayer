#include "audio_tools_controller.hpp"
#include "audio_editor/audio_editor_controller.hpp"
#include "audio_preview_controller.hpp"
#include "playback_controller.hpp"
#include <QPointer>

AudioToolsController::AudioToolsController(QObject* parent)
    : QObject(parent)
{
}

void AudioToolsController::bindPlaybackControllers(PlaybackController* playback,
                                                 AudioEditorController* editor,
                                                 AudioPreviewController* preview)
{
    for (const auto& connection : playbackConnections_) disconnect(connection);
    playbackConnections_.clear();
    if (!playback || !editor || !preview) return;
    const QPointer<PlaybackController> main = playback;
    const QPointer<AudioEditorController> edit = editor;
    const QPointer<AudioPreviewController> audition = preview;
    playbackConnections_.append(connect(playback, &PlaybackController::playbackRequested, this,
        [edit, audition](bool* accepted) {
            if (edit && !edit->pauseForPlaybackHandoff()) { *accepted = false; return; }
            if (audition) { audition->pause(); if (audition->playing()) *accepted = false; }
        }, Qt::DirectConnection));
    playbackConnections_.append(connect(editor, &AudioEditorController::playbackRequested, this,
        [main, audition](bool* accepted) {
            if (audition) { audition->pause(); if (audition->playing()) { *accepted = false; return; } }
            if (main && !main->pauseForPlaybackHandoff()) *accepted = false;
        }, Qt::DirectConnection));
    playbackConnections_.append(connect(preview, &AudioPreviewController::playbackRequested, this,
        [main, edit](bool* accepted) {
            if (edit && !edit->pauseForPlaybackHandoff()) { *accepted = false; return; }
            if (main && !main->pauseForPlaybackHandoff()) *accepted = false;
        }, Qt::DirectConnection));
}

int AudioToolsController::currentTool() const noexcept { return currentTool_; }
bool AudioToolsController::visible() const noexcept { return visible_; }

void AudioToolsController::setCurrentTool(int tool)
{
    if (tool < 0 || tool > 5 || tool == currentTool_) {
        return;
    }
    currentTool_ = tool;
    emit currentToolChanged();
}

void AudioToolsController::show()
{
    if (visible_) {
        return;
    }
    visible_ = true;
    emit visibleChanged();
    emit showRequested();
}

void AudioToolsController::hide()
{
    if (!visible_) {
        return;
    }
    visible_ = false;
    emit visibleChanged();
    emit hideRequested();
}

void AudioToolsController::selectTool(int tool)
{
    setCurrentTool(tool);
    if (!visible_) {
        show();
    }
}
