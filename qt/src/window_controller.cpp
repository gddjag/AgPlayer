#include "window_controller.hpp"

#include <QCoreApplication>
#include <QWindow>

#include <utility>

namespace {

constexpr int kSnapDistance = 20;

int snapDistance(int a, int b) noexcept
{
    const int diff = a - b;
    return diff < 0 ? -diff : diff;
}

} // namespace

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
bool WindowController::listWindowVisible() const noexcept { return listWindowVisible_; }
bool WindowController::listWindowDetached() const noexcept { return listWindowDetached_; }
int WindowController::listWindowX() const noexcept { return listWindowX_; }
int WindowController::listWindowY() const noexcept { return listWindowY_; }
int WindowController::listWindowWidth() const noexcept { return listWindowWidth_; }
int WindowController::listWindowHeight() const noexcept { return listWindowHeight_; }

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

void WindowController::setListWindow(QWindow* listWindow)
{
    listWindow_ = listWindow;
    if (listWindow_ != nullptr) {
        listWindow_->setVisible(listWindowVisible_);
        if (listWindowVisible_) {
            listWindow_->requestActivate();
            listWindow_->raise();
        }
    }
}

void WindowController::setAudioToolsWindow(QWindow* audioToolsWindow)
{
    audioToolsWindow_ = audioToolsWindow;
    if (audioToolsWindow_ != nullptr) {
        audioToolsWindow_->setVisible(audioToolsVisible_);
        if (audioToolsVisible_) {
            audioToolsWindow_->requestActivate();
            audioToolsWindow_->raise();
        }
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

void WindowController::setListWindowDetached(bool detached)
{
    if (listWindowDetached_ == detached) {
        return;
    }
    listWindowDetached_ = detached;
    emit listWindowDetachedChanged();
    applyListWindowDetached(listWindowDetached_);
}

void WindowController::setListWindowX(int x) noexcept
{
    if (listWindowX_ == x) {
        return;
    }
    listWindowX_ = x;
    emit listWindowXChanged();
}

void WindowController::setListWindowY(int y) noexcept
{
    if (listWindowY_ == y) {
        return;
    }
    listWindowY_ = y;
    emit listWindowYChanged();
}

void WindowController::setListWindowWidth(int width) noexcept
{
    if (listWindowWidth_ == width) {
        return;
    }
    listWindowWidth_ = width;
    emit listWindowWidthChanged();
}

void WindowController::setListWindowHeight(int height) noexcept
{
    if (listWindowHeight_ == height) {
        return;
    }
    listWindowHeight_ = height;
    emit listWindowHeightChanged();
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

void WindowController::showListWindow()
{
    if (!listWindowDetached_) {
        setListWindowDetached(true);
    }
    applyListWindowVisible(true);
}

void WindowController::hideListWindow()
{
    applyListWindowVisible(false);
    setListWindowDetached(false);
}

void WindowController::toggleListWindow()
{
    if (!listWindowVisible_) {
        showListWindow();
    } else {
        applyListWindowVisible(false);
    }
}

void WindowController::moveListWindow(int x, int y)
{
    if (listWindow_ == nullptr) {
        setListWindowX(x);
        setListWindowY(y);
        return;
    }

    const QPoint snapped = computeSnappedPosition(x, y);
    listWindow_->setPosition(snapped);
    setListWindowX(snapped.x());
    setListWindowY(snapped.y());
}

void WindowController::snapListWindow(const QString& direction)
{
    if (listWindow_ == nullptr || mainWindow_ == nullptr) {
        return;
    }

    const QPoint snapped = computeSnapForEdge(direction);
    listWindow_->setPosition(snapped);
    setListWindowX(snapped.x());
    setListWindowY(snapped.y());
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

void WindowController::applyListWindowVisible(bool visible)
{
    if (listWindowVisible_ == visible) {
        return;
    }
    if (listWindow_ != nullptr) {
        if (visible && !listWindowGeometryInitialized_) {
            updateListWindowPosition();
            listWindowGeometryInitialized_ = true;
        }
        listWindow_->setVisible(visible);
        if (visible) {
            listWindow_->requestActivate();
            listWindow_->raise();
        }
    }
    listWindowVisible_ = visible;
    emit listWindowVisibleChanged();
}

void WindowController::applyListWindowDetached(bool detached)
{
    if (detached) {
        applyListWindowVisible(true);
    } else {
        applyListWindowVisible(false);
    }
}

void WindowController::updateListWindowPosition()
{
    if (listWindow_ == nullptr || mainWindow_ == nullptr) {
        return;
    }

    const QRect mainGeo = mainWindow_->frameGeometry();
    const int x = mainGeo.x() + (mainGeo.width() - listWindow_->width()) / 2;
    const int y = mainGeo.y() + mainGeo.height();
    listWindow_->setPosition(x, y);
    setListWindowX(x);
    setListWindowY(y);
}

QPoint WindowController::computeSnappedPosition(int x, int y) const
{
    if (mainWindow_ == nullptr || listWindow_ == nullptr) {
        return QPoint(x, y);
    }

    const QRect mainGeo = mainWindow_->frameGeometry();
    const int listWidth = listWindow_->width();
    const int listHeight = listWindow_->height();

    // Candidate snap positions for each edge.
    const int leftX = mainGeo.left() - listWidth;
    const int rightX = mainGeo.right() + 1;
    const int topY = mainGeo.top() - listHeight;
    const int bottomY = mainGeo.bottom() + 1;

    // Centered alignments.
    const int centerY = mainGeo.y() + (mainGeo.height() - listHeight) / 2;
    const int centerX = mainGeo.x() + (mainGeo.width() - listWidth) / 2;

    // Distances from the requested position to each snap edge.
    const int distLeft = snapDistance(x, leftX);
    const int distRight = snapDistance(x, rightX);
    const int distTop = snapDistance(y, topY);
    const int distBottom = snapDistance(y, bottomY);

    // Find the closest edge that is within the magnetic threshold.
    int bestDist = kSnapDistance + 1;
    QPoint bestPos(x, y);

    auto consider = [&bestDist, &bestPos](int dist, const QPoint& pos) {
        if (dist < bestDist) {
            bestDist = dist;
            bestPos = pos;
        }
    };

    consider(distLeft, QPoint(leftX, centerY));
    consider(distRight, QPoint(rightX, centerY));
    consider(distTop, QPoint(centerX, topY));
    consider(distBottom, QPoint(centerX, bottomY));

    return bestPos;
}

QPoint WindowController::computeSnapForEdge(const QString& direction) const
{
    if (mainWindow_ == nullptr || listWindow_ == nullptr) {
        return QPoint(listWindowX_, listWindowY_);
    }

    const QRect mainGeo = mainWindow_->frameGeometry();
    const int listWidth = listWindow_->width();
    const int listHeight = listWindow_->height();
    const int centerY = mainGeo.y() + (mainGeo.height() - listHeight) / 2;
    const int centerX = mainGeo.x() + (mainGeo.width() - listWidth) / 2;

    if (direction == QStringLiteral("left")) {
        return QPoint(mainGeo.left() - listWidth, centerY);
    }
    if (direction == QStringLiteral("right")) {
        return QPoint(mainGeo.right() + 1, centerY);
    }
    if (direction == QStringLiteral("top")) {
        return QPoint(centerX, mainGeo.top() - listHeight);
    }
    if (direction == QStringLiteral("bottom")) {
        return QPoint(centerX, mainGeo.bottom() + 1);
    }

    return QPoint(listWindowX_, listWindowY_);
}
