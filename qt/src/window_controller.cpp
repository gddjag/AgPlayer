#include "window_controller.hpp"

#include <QCoreApplication>
#include <QWindow>

#include <utility>

WindowController::WindowController(QObject* parent)
    : WindowController(ShutdownActions{}, parent)
{
}

WindowController::WindowController(ShutdownActions actions, QObject* parent)
    : QObject(parent), shutdownActions_(std::move(actions))
{
    if (!shutdownActions_.quitApplication) {
        shutdownActions_.quitApplication = [] { QCoreApplication::quit(); };
    }
}

bool WindowController::mainVisible() const noexcept { return mainVisible_; }
bool WindowController::miniVisible() const noexcept { return miniVisible_; }
bool WindowController::audioToolsVisible() const noexcept { return audioToolsVisible_; }
bool WindowController::alwaysOnTop() const noexcept { return alwaysOnTop_; }

void WindowController::setWindows(QWindow* mainWindow, QWindow* miniWindow)
{
    mainWindow_ = mainWindow;
    miniWindow_ = miniWindow;
    if (mainWindow_ != nullptr) {
        mainWindow_->setVisible(mainVisible_);
    }
    if (miniWindow_ != nullptr) {
        miniWindow_->setFlag(Qt::WindowStaysOnTopHint, alwaysOnTop_);
        miniWindow_->setVisible(miniVisible_);
    }
}

void WindowController::setAudioToolsWindow(QWindow* audioToolsWindow)
{
    audioToolsWindow_ = audioToolsWindow;
    if (audioToolsWindow_ != nullptr) {
        audioToolsWindow_->setVisible(audioToolsVisible_);
    }
}

void WindowController::setMainReady(bool ready) noexcept
{
    mainReady_ = ready;
    if (mainReady_ && pendingView_ == PendingView::Main) {
        showMain();
    }
}

void WindowController::setMiniReady(bool ready) noexcept
{
    miniReady_ = ready;
    if (miniReady_ && pendingView_ == PendingView::Mini) {
        showMini();
    }
}

void WindowController::setShutdownActions(ShutdownActions actions)
{
    if (!actions.quitApplication) {
        actions.quitApplication = [] { QCoreApplication::quit(); };
    }
    shutdownActions_ = std::move(actions);
}

void WindowController::showMini()
{
    pendingView_ = PendingView::Mini;
    if (!miniReady_) {
        return;
    }
    applyMiniVisible(true);
    applyMainVisible(false);
    pendingView_ = PendingView::None;
}

void WindowController::showMain()
{
    pendingView_ = PendingView::Main;
    if (!mainReady_) {
        return;
    }
    applyMainVisible(true);
    applyMiniVisible(false);
    pendingView_ = PendingView::None;
}

void WindowController::showAudioTools()
{
    applyAudioToolsVisible(true);
}

void WindowController::hideAudioTools()
{
    applyAudioToolsVisible(false);
}

void WindowController::requestClose()
{
    if (shutdownRequested_) {
        return;
    }
    shutdownRequested_ = true;
    if (shutdownActions_.cancelWaveform) {
        shutdownActions_.cancelWaveform();
    }
    if (shutdownActions_.stopPlayback) {
        shutdownActions_.stopPlayback();
    }
    if (shutdownActions_.flushLibrary) {
        shutdownActions_.flushLibrary();
    }
    if (shutdownActions_.releaseCore) {
        shutdownActions_.releaseCore();
    }
    if (shutdownActions_.quitApplication) {
        shutdownActions_.quitApplication();
    }
}

void WindowController::setAlwaysOnTop(bool alwaysOnTop)
{
    if (alwaysOnTop_ == alwaysOnTop) {
        return;
    }
    alwaysOnTop_ = alwaysOnTop;
    if (miniWindow_ != nullptr) {
        const bool wasVisible = miniWindow_->isVisible();
        miniWindow_->setFlag(Qt::WindowStaysOnTopHint, alwaysOnTop_);
        if (wasVisible) {
            miniWindow_->show();
        }
    }
    emit alwaysOnTopChanged();
}

void WindowController::applyMainVisible(bool visible)
{
    if (mainVisible_ == visible) {
        return;
    }
    if (mainWindow_ != nullptr) {
        mainWindow_->setVisible(visible);
    }
    mainVisible_ = visible;
    emit mainVisibleChanged();
}

void WindowController::applyMiniVisible(bool visible)
{
    if (miniVisible_ == visible) {
        return;
    }
    if (miniWindow_ != nullptr) {
        miniWindow_->setVisible(visible);
    }
    miniVisible_ = visible;
    emit miniVisibleChanged();
}

void WindowController::applyAudioToolsVisible(bool visible)
{
    if (audioToolsVisible_ == visible) {
        return;
    }
    if (audioToolsWindow_ != nullptr) {
        audioToolsWindow_->setVisible(visible);
        if (visible) {
            audioToolsWindow_->requestActivate();
            audioToolsWindow_->raise();
        }
    }
    audioToolsVisible_ = visible;
    emit audioToolsVisibleChanged();
}
