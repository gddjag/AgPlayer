#include "audio_editor_controller.hpp"

#include <filesystem>

using agplayer::editor::AudioSource;
using agplayer::editor::EditCommand;
using agplayer::editor::Selection;

AudioEditorController::AudioEditorController(QObject* parent)
    : QObject(parent), actions_(this), viewport_(this)
{
    refreshActions();
}

bool AudioEditorController::createUntitledDocument(
    const quint32 sampleRate, const quint32 channels, const qint64 frames)
{
    auto candidate = agplayer::editor::AudioDocument::fromSource(
        AudioSource{std::filesystem::path{}, sampleRate, channels, frames});
    if (candidate.totalFrames() <= 0) {
        return false;
    }
    document_ = std::move(candidate);
    has_document_ = true;
    modified_ = false;
    state_ = EditorSessionState::Ready;
    viewport_.setDocumentFrames(frames);
    refreshActions();
    emit stateChanged();
    emit documentChanged();
    return true;
}

bool AudioEditorController::setSelection(const qint64 startFrame,
                                         const qint64 endFrame)
{
    if (!has_document_ || !document_.setSelection({startFrame, endFrame})) {
        return false;
    }
    refreshActions();
    emit documentChanged();
    return true;
}

bool AudioEditorController::clearSelection()
{
    if (!document_.clearSelection()) {
        return false;
    }
    refreshActions();
    emit documentChanged();
    return true;
}

bool AudioEditorController::actionEnabled(const QString& id) const noexcept
{
    const EditorAction* const item = actions_.action(id);
    return item != nullptr && item->enabled;
}

bool AudioEditorController::triggerAction(const QString& id)
{
    EditorAction* const item = action(id);
    if (!item || !item->enabled) {
        return false;
    }
    bool changed = false;
    if (id == QStringLiteral("editor.undo")) {
        changed = document_.undo();
    } else if (id == QStringLiteral("editor.redo")) {
        changed = document_.redo();
    } else if (id == QStringLiteral("editor.cut")) {
        changed = document_.apply(EditCommand::cutSelection());
    } else if (id == QStringLiteral("editor.copy")) {
        changed = document_.apply(EditCommand::copySelection());
    } else if (id == QStringLiteral("editor.deleteSelection")) {
        changed = document_.apply(EditCommand::deleteSelection());
    } else if (id == QStringLiteral("editor.cropToSelection")) {
        changed = document_.apply(EditCommand::cropToSelection());
    } else if (id == QStringLiteral("editor.silenceSelection")) {
        changed = document_.apply(EditCommand::silenceSelection());
    } else if (id == QStringLiteral("editor.fadeIn")) {
        changed = document_.apply(EditCommand::fadeIn());
    } else if (id == QStringLiteral("editor.fadeOut")) {
        changed = document_.apply(EditCommand::fadeOut());
    } else {
        return id == QStringLiteral("editor.open")
            || id == QStringLiteral("editor.newRecording")
            || id == QStringLiteral("editor.save")
            || id == QStringLiteral("editor.export")
            || id == QStringLiteral("editor.moreMenu");
    }
    if (!changed) {
        return false;
    }
    if (id != QStringLiteral("editor.copy")) {
        modified_ = true;
        viewport_.setDocumentFrames(document_.totalFrames());
    }
    refreshActions();
    emit documentChanged();
    return true;
}

void AudioEditorController::refreshActions()
{
    const bool selection = document_.snapshot().selection.has_value();
    actions_.setEnabled(QStringLiteral("editor.open"), true);
    actions_.setEnabled(QStringLiteral("editor.newRecording"), true);
    actions_.setEnabled(QStringLiteral("editor.save"), has_document_);
    actions_.setEnabled(QStringLiteral("editor.export"), has_document_);
    actions_.setEnabled(QStringLiteral("editor.moreMenu"), has_document_);
    actions_.setEnabled(QStringLiteral("editor.undo"), document_.canUndo());
    actions_.setEnabled(QStringLiteral("editor.redo"), document_.canRedo());
    actions_.setEnabled(QStringLiteral("editor.paste"), document_.hasClipboard());
    for (const QString& id : {
             QStringLiteral("editor.cut"), QStringLiteral("editor.copy"),
             QStringLiteral("editor.deleteSelection"),
             QStringLiteral("editor.cropToSelection"),
             QStringLiteral("editor.silenceSelection"),
             QStringLiteral("editor.fadeIn"), QStringLiteral("editor.fadeOut")}) {
        actions_.setEnabled(id, has_document_ && selection);
    }
}
