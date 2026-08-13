#include "audio_tools_controller.hpp"

AudioToolsController::AudioToolsController(QObject* parent)
    : QObject(parent)
{
}

int AudioToolsController::currentTool() const noexcept { return currentTool_; }
QString AudioToolsController::currentToolId() const { return toolIdForIndex(currentTool_); }
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

void AudioToolsController::selectToolById(const QString& toolId)
{
    const int tool = toolIndexForId(toolId);
    if (tool >= 0) selectTool(tool);
}

QString AudioToolsController::toolIdForIndex(const int tool) const
{
    static const QString ids[] = {
        QStringLiteral("audio-editor"),
        QStringLiteral("format-converter"),
        QStringLiteral("metadata-editor"),
        QStringLiteral("filename-processor"),
        QStringLiteral("voice-clone"),
    };
    return tool >= 0 && tool < 5 ? ids[tool] : QString{};
}

int AudioToolsController::toolIndexForId(const QString& toolId) const
{
    for (int index = 0; index < 5; ++index) {
        if (toolIdForIndex(index) == toolId) return index;
    }
    return -1;
}
