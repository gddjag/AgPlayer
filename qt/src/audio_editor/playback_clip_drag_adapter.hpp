#pragma once

#include "selection_drag_controller.hpp"

#include <QObject>

#include <memory>

class LibraryModel;

class PlaybackClipDragAdapter final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool preparing READ preparing NOTIFY preparingChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)

public:
    explicit PlaybackClipDragAdapter(LibraryModel* library,
                                     QObject* parent = nullptr);
    PlaybackClipDragAdapter(LibraryModel* library, QString outputDirectory,
                            SelectionDragController::DragFunction drag,
                            QObject* parent = nullptr);
    ~PlaybackClipDragAdapter() override;

    [[nodiscard]] bool preparing() const noexcept;
    [[nodiscard]] QString errorMessage() const { return error_message_; }

    Q_INVOKABLE bool begin(double sceneX, double sceneY,
                           const QString& trackId,
                           qint64 selectionStartMs,
                           qint64 selectionEndMs);
    Q_INVOKABLE void update(double sceneX, double sceneY);
    Q_INVOKABLE void cancel();

signals:
    void preparingChanged();
    void handoffReady(const QUrl& url);
    void errorMessageChanged();
    void errorOccurred(const QString& message);

private:
    void setError(QString message);

    LibraryModel* library_{};
    HandoffAssetManager assets_;
    std::unique_ptr<SelectionDragController> drag_controller_;
    QString error_message_;
};
