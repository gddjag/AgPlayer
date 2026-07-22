#pragma once

#include <QObject>
#include <QPointer>

#include <functional>

class QWindow;

class WindowController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool mainVisible READ mainVisible NOTIFY mainVisibleChanged)
    Q_PROPERTY(bool miniVisible READ miniVisible NOTIFY miniVisibleChanged)
    Q_PROPERTY(bool alwaysOnTop READ alwaysOnTop WRITE setAlwaysOnTop NOTIFY alwaysOnTopChanged)

public:
    struct ShutdownActions {
        std::function<void()> cancelWaveform;
        std::function<void()> stopPlayback;
        std::function<void()> flushLibrary;
        std::function<void()> releaseCore;
        std::function<void()> quitApplication;
    };

    explicit WindowController(QObject* parent = nullptr);
    explicit WindowController(ShutdownActions actions, QObject* parent = nullptr);

    bool mainVisible() const noexcept;
    bool miniVisible() const noexcept;
    bool alwaysOnTop() const noexcept;

    void setWindows(QWindow* mainWindow, QWindow* miniWindow);
    void setMainReady(bool ready) noexcept;
    void setMiniReady(bool ready) noexcept;
    void setShutdownActions(ShutdownActions actions);

    Q_INVOKABLE void showMini();
    Q_INVOKABLE void showMain();
    Q_INVOKABLE void requestClose();
    Q_INVOKABLE void setAlwaysOnTop(bool alwaysOnTop);

signals:
    void mainVisibleChanged();
    void miniVisibleChanged();
    void alwaysOnTopChanged();

private:
    enum class PendingView { None, Main, Mini };

    void applyMainVisible(bool visible);
    void applyMiniVisible(bool visible);

    QPointer<QWindow> mainWindow_;
    QPointer<QWindow> miniWindow_;
    ShutdownActions shutdownActions_;
    bool mainVisible_ = true;
    bool miniVisible_ = false;
    bool alwaysOnTop_ = false;
    bool mainReady_ = true;
    bool miniReady_ = true;
    bool shutdownRequested_ = false;
    PendingView pendingView_ = PendingView::None;
};
