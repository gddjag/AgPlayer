#pragma once

#include "audio_editor/audio_document.hpp"
#include "editor_action_model.hpp"
#include "editor_viewport.hpp"

#include <QObject>

enum class EditorSessionState {
    Empty,
    Ready,
    Playing,
    Recording,
    RecordingPaused,
    Finalizing,
    Previewing,
    Processing,
    Saving,
    Exporting,
    Error
};
Q_DECLARE_METATYPE(EditorSessionState)

class AudioEditorController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(EditorActionModel* actions READ actions CONSTANT)
    Q_PROPERTY(EditorViewport* viewport READ viewport CONSTANT)
    Q_PROPERTY(EditorSessionState state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool hasDocument READ hasDocument NOTIFY documentChanged)
    Q_PROPERTY(bool modified READ modified NOTIFY documentChanged)
    Q_PROPERTY(qint64 totalFrames READ totalFrames NOTIFY documentChanged)

public:
    explicit AudioEditorController(QObject* parent = nullptr);

    [[nodiscard]] EditorActionModel* actions() noexcept { return &actions_; }
    [[nodiscard]] EditorViewport* viewport() noexcept { return &viewport_; }
    [[nodiscard]] EditorSessionState state() const noexcept { return state_; }
    [[nodiscard]] bool hasDocument() const noexcept { return has_document_; }
    [[nodiscard]] bool modified() const noexcept { return modified_; }
    [[nodiscard]] qint64 totalFrames() const noexcept { return document_.totalFrames(); }
    [[nodiscard]] EditorAction* action(const QString& id) noexcept
    {
        return actions_.action(id);
    }

    Q_INVOKABLE bool createUntitledDocument(
        quint32 sampleRate, quint32 channels, qint64 frames);
    Q_INVOKABLE bool setSelection(qint64 startFrame, qint64 endFrame);
    Q_INVOKABLE bool clearSelection();
    Q_INVOKABLE bool actionEnabled(const QString& id) const noexcept;
    Q_INVOKABLE bool triggerAction(const QString& id);

signals:
    void stateChanged();
    void documentChanged();

private:
    void refreshActions();

    EditorActionModel actions_;
    EditorViewport viewport_;
    agplayer::editor::AudioDocument document_;
    EditorSessionState state_{EditorSessionState::Empty};
    bool has_document_{};
    bool modified_{};
};
