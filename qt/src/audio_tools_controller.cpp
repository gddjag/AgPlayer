#include "audio_tools_controller.hpp"

AudioToolsController::AudioToolsController(QObject* parent)
    : QObject(parent)
{
}

int AudioToolsController::currentTool() const noexcept { return currentTool_; }
bool AudioToolsController::visible() const noexcept { return visible_; }

void AudioToolsController::setCurrentTool(int tool)
{
    if (tool < 0 || tool > 4 || tool == currentTool_) {
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
