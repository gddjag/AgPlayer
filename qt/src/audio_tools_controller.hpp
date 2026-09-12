#pragma once

#include <QObject>
#include <QList>

class PlaybackController;
class AudioEditorController;
class AudioPreviewController;

class AudioToolsController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int currentTool READ currentTool WRITE setCurrentTool NOTIFY currentToolChanged)
    Q_PROPERTY(bool visible READ visible NOTIFY visibleChanged)

public:
    explicit AudioToolsController(QObject* parent = nullptr);
    void bindPlaybackControllers(PlaybackController* playback,
                                 AudioEditorController* editor,
                                 AudioPreviewController* preview);

    int currentTool() const noexcept;
    bool visible() const noexcept;
    void setCurrentTool(int tool);

    Q_INVOKABLE void show();
    Q_INVOKABLE void hide();
    Q_INVOKABLE void selectTool(int tool);
    Q_INVOKABLE void addCurrentListToLossless() { emit losslessPlaylistRequested(); }
    Q_INVOKABLE void locateLosslessFile(const QString& path) { emit losslessLocateRequested(path); }

signals:
    void currentToolChanged();
    void visibleChanged();
    void showRequested();
    void hideRequested();
    void losslessPlaylistRequested();
    void losslessLocateRequested(const QString& path);

private:
    QList<QMetaObject::Connection> playbackConnections_;
    int currentTool_ = 0;  // Default: Light Editor
    bool visible_ = false;
};
