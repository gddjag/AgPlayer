#include "window_controller.hpp"

#include <QCoreApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QScreen>
#include <QSettings>
#include <QWindow>

#include <utility>

namespace {

constexpr int kSnapDistance = 15;

bool isDockEdge(const QString& edge)
{
    return edge == QStringLiteral("left") || edge == QStringLiteral("right")
        || edge == QStringLiteral("top") || edge == QStringLiteral("bottom");
}

bool isMinimized(const QWindow* window)
{
    return window != nullptr && window->windowState() == Qt::WindowMinimized;
}

bool isMaximized(const QWindow* window)
{
    return window != nullptr && window->windowState() == Qt::WindowMaximized;
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
    windowStateSyncTimer_.setSingleShot(true);
    windowStateSyncTimer_.setInterval(250);
    connect(&windowStateSyncTimer_, &QTimer::timeout,
            this, &WindowController::flushWindowState);
    loadPersistedWindowState();
}

WindowController::~WindowController()
{
    flushWindowState();
    shutdownActions_ = {};
}

bool WindowController::mainVisible() const noexcept { return mainVisible_; }
bool WindowController::miniVisible() const noexcept { return miniVisible_; }
bool WindowController::audioToolsVisible() const noexcept { return audioToolsVisible_; }
bool WindowController::alwaysOnTop() const noexcept { return alwaysOnTop_; }
bool WindowController::magneticSnapEnabled() const noexcept { return magneticSnapEnabled_; }
int WindowController::preferredDockEdge() const noexcept { return preferredDockEdge_; }
QString WindowController::listDockEdge() const { return listDockEdge_; }
int WindowController::closeBehavior() const noexcept { return closeBehavior_; }
bool WindowController::listWindowVisible() const noexcept { return listWindowVisible_; }
bool WindowController::listWindowDetached() const noexcept { return listWindowDetached_; }
int WindowController::listWindowX() const noexcept { return listWindowX_; }
int WindowController::listWindowY() const noexcept { return listWindowY_; }
int WindowController::listWindowWidth() const noexcept { return listWindowWidth_; }
int WindowController::listWindowHeight() const noexcept { return listWindowHeight_; }

void WindowController::setWindows(QWindow* mainWindow, QWindow* miniWindow)
{
    if (mainWindow_ != nullptr) {
        mainWindow_->removeEventFilter(this);
    }
    if (miniWindow_ != nullptr) {
        miniWindow_->removeEventFilter(this);
    }

    mainWindow_ = mainWindow;
    miniWindow_ = miniWindow;
    if (mainWindow_ != nullptr) {
        restoreGeometry(mainWindow_, QStringLiteral("windows/mainGeometry"));
        mainWindow_->installEventFilter(this);
        mainWindow_->setVisible(mainVisible_);
        persistGeometry(mainWindow_, QStringLiteral("windows/mainGeometry"));
    }
    if (miniWindow_ != nullptr) {
        restoreGeometry(miniWindow_, QStringLiteral("windows/miniGeometry"));
        miniWindow_->installEventFilter(this);
        miniWindow_->setFlag(Qt::WindowStaysOnTopHint, alwaysOnTop_);
        miniWindow_->setVisible(miniVisible_);
        persistGeometry(miniWindow_, QStringLiteral("windows/miniGeometry"));
    }
}

void WindowController::setListWindow(QWindow* listWindow)
{
    if (listWindow_ != nullptr) {
        listWindow_->removeEventFilter(this);
    }
    listWindow_ = listWindow;
    if (listWindow_ == nullptr) {
        return;
    }

    restoreGeometry(listWindow_, QStringLiteral("windows/listGeometry"));
    listWindow_->installEventFilter(this);
    setListWindowWidth(listWindow_->width());
    setListWindowHeight(listWindow_->height());
    if (!listWindowDetached_) {
        repositionDockedListWindow();
    } else {
        setListWindowX(listWindow_->x());
        setListWindowY(listWindow_->y());
    }
    applyListWindowVisible(shouldShowListWindow());
    persistGeometry(listWindow_, QStringLiteral("windows/listGeometry"));
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

void WindowController::setMagneticSnapEnabled(bool enabled)
{
    if (magneticSnapEnabled_ == enabled) {
        return;
    }
    magneticSnapEnabled_ = enabled;
    emit magneticSnapEnabledChanged();
}

void WindowController::setPreferredDockEdge(int edge)
{
    const int bounded = qBound(0, edge, 3);
    const bool initialCall = !preferredDockEdgeInitialized_;
    preferredDockEdgeInitialized_ = true;
    if (preferredDockEdge_ == bounded) {
        return;
    }
    preferredDockEdge_ = bounded;
    emit preferredDockEdgeChanged();
    if (!listWindowDetached_ && (!initialCall || !hasPersistedDockEdge_)) {
        setListDockEdge(edgeForPreference(preferredDockEdge_));
        repositionDockedListWindow();
    }
}

void WindowController::setCloseBehavior(int behavior)
{
    const int bounded = behavior == 1 ? 1 : 0;
    if (closeBehavior_ == bounded) {
        return;
    }
    closeBehavior_ = bounded;
    emit closeBehaviorChanged();
}

void WindowController::setListWindowPanelAllowed(bool allowed)
{
    if (listWindowPanelAllowed_ == allowed) {
        return;
    }
    listWindowPanelAllowed_ = allowed;
    applyListWindowVisible(shouldShowListWindow());
}

void WindowController::setListWindowDetached(bool detached)
{
    if (listWindowDetached_ == detached) {
        return;
    }
    listWindowDetached_ = detached;
    emit listWindowDetachedChanged();
    if (detached) {
        setListDockEdge(QStringLiteral("none"));
    } else {
        setListDockEdge(edgeForPreference(preferredDockEdge_));
        repositionDockedListWindow();
    }
    applyListWindowVisible(shouldShowListWindow());
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
    if (closeBehavior_ == 0 && shutdownActions_.minimizeToTray) {
        applyAudioToolsVisible(false);
        applyMainVisible(false);
        applyMiniVisible(false);
        applyListWindowVisible(false);
        shutdownActions_.minimizeToTray();
        return;
    }
    shutdown();
}

void WindowController::requestExit()
{
    shutdown();
}

void WindowController::shutdown()
{
    if (shutdownRequested_) {
        return;
    }
    shutdownRequested_ = true;
    flushWindowState();
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

bool WindowController::shouldShowListWindow() const
{
    return listWindowPanelAllowed_ && listWindowRequestedVisible_ && mainVisible_
        && !isMinimized(mainWindow_)
        && (listWindowDetached_ || !isMaximized(mainWindow_));
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
    settings_.setValue(QStringLiteral("windows/miniAlwaysOnTop"), alwaysOnTop_);
    scheduleWindowStateSync();
    emit alwaysOnTopChanged();
}

void WindowController::showListWindow()
{
    listWindowRequestedVisible_ = true;
    settings_.setValue(QStringLiteral("windows/listRequestedVisible"), true);
    scheduleWindowStateSync();
    applyListWindowVisible(shouldShowListWindow());
}

void WindowController::hideListWindow()
{
    listWindowRequestedVisible_ = false;
    settings_.setValue(QStringLiteral("windows/listRequestedVisible"), false);
    scheduleWindowStateSync();
    applyListWindowVisible(false);
}

void WindowController::toggleListWindow()
{
    if (listWindowRequestedVisible_) {
        hideListWindow();
    } else {
        showListWindow();
    }
}

void WindowController::moveListWindow(int x, int y)
{
    if (listWindow_ == nullptr) {
        setListWindowX(x);
        setListWindowY(y);
        return;
    }

    const QString edge = magneticSnapEnabled_ ? snapEdgeForPosition(x, y)
                                              : QStringLiteral("none");
    if (isDockEdge(edge)) {
        if (listWindowDetached_) {
            listWindowDetached_ = false;
            emit listWindowDetachedChanged();
        }
        setListDockEdge(edge);
        repositionDockedListWindow();
        return;
    }

    if (!listWindowDetached_) {
        listWindowDetached_ = true;
        emit listWindowDetachedChanged();
    }
    setListDockEdge(QStringLiteral("none"));
    updatingWindowGeometry_ = true;
    listWindow_->setPosition(x, y);
    updatingWindowGeometry_ = false;
    setListWindowX(x);
    setListWindowY(y);
    scheduleWindowStateSync();
}

void WindowController::snapListWindow(const QString& direction)
{
    if (listWindow_ == nullptr || mainWindow_ == nullptr || !isDockEdge(direction)) {
        return;
    }
    if (listWindowDetached_) {
        listWindowDetached_ = false;
        emit listWindowDetachedChanged();
    }
    setListDockEdge(direction);
    repositionDockedListWindow();
}

void WindowController::activateSearch()
{
    showListWindow();
    if (listWindow_ != nullptr) {
        listWindow_->requestActivate();
        listWindow_->raise();
    }
    emit searchRequested();
}

void WindowController::toggleMiniPlayer()
{
    if (miniVisible_) {
        showMain();
    } else {
        showMini();
    }
}

void WindowController::applyMainVisible(bool visible)
{
    if (mainVisible_ == visible) {
        if (visible) {
            applyListWindowVisible(shouldShowListWindow());
        }
        return;
    }
    if (mainWindow_ != nullptr) {
        mainWindow_->setVisible(visible);
    }
    mainVisible_ = visible;
    emit mainVisibleChanged();
    applyListWindowVisible(shouldShowListWindow());
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
            if (!listWindowDetached_) {
                repositionDockedListWindow();
            }
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
    setListWindowDetached(detached);
}

void WindowController::updateListWindowPosition()
{
    repositionDockedListWindow();
}

void WindowController::repositionDockedListWindow()
{
    if (listWindow_ == nullptr || mainWindow_ == nullptr || listWindowDetached_) {
        return;
    }
    if (isMaximized(mainWindow_)) {
        applyListWindowVisible(false);
        return;
    }
    QScreen* screen = mainWindow_->screen();
    if (screen == nullptr) {
        screen = QGuiApplication::screenAt(mainWindow_->geometry().center());
    }
    if (screen == nullptr) {
        return;
    }

    const QRect available = screen->availableGeometry();
    QString effectiveEdge = listDockEdge_;
    const bool horizontal = effectiveEdge == QStringLiteral("left")
        || effectiveEdge == QStringLiteral("right");
    const int minimumCombined = horizontal
        ? mainWindow_->minimumWidth() + listWindow_->minimumWidth()
        : mainWindow_->minimumHeight() + listWindow_->minimumHeight();
    const int availableSpan = horizontal ? available.width() : available.height();
    if (minimumCombined > availableSpan) {
        effectiveEdge = horizontal ? QStringLiteral("bottom") : QStringLiteral("right");
        setListDockEdge(effectiveEdge);
    }

    updatingWindowGeometry_ = true;
    if (effectiveEdge == QStringLiteral("left")
        || effectiveEdge == QStringLiteral("right")) {
        int excess = mainWindow_->width() + listWindow_->width() - available.width();
        if (excess > 0) {
            const int listShrink = qMin(
                excess, listWindow_->width() - listWindow_->minimumWidth());
            if (listShrink > 0) {
                listWindow_->resize(listWindow_->width() - listShrink,
                                    listWindow_->height());
                excess -= listShrink;
            }
            const int mainShrink = qMin(
                excess, mainWindow_->width() - mainWindow_->minimumWidth());
            if (mainShrink > 0) {
                mainWindow_->resize(mainWindow_->width() - mainShrink,
                                    mainWindow_->height());
            }
        }
    } else {
        int excess = mainWindow_->height() + listWindow_->height() - available.height();
        if (excess > 0) {
            const int listShrink = qMin(
                excess, listWindow_->height() - listWindow_->minimumHeight());
            if (listShrink > 0) {
                listWindow_->resize(listWindow_->width(),
                                    listWindow_->height() - listShrink);
                excess -= listShrink;
            }
            const int mainShrink = qMin(
                excess, mainWindow_->height() - mainWindow_->minimumHeight());
            if (mainShrink > 0) {
                mainWindow_->resize(mainWindow_->width(),
                                    mainWindow_->height() - mainShrink);
            }
        }
    }

    QRect mainGeometry = mainWindow_->geometry();
    QPoint position = computeSnapForEdge(effectiveEdge);
    QRect group = mainGeometry.united(
        QRect(position, QSize(listWindow_->width(), listWindow_->height())));
    int shiftX = 0;
    int shiftY = 0;
    if (group.left() < available.left()) {
        shiftX = available.left() - group.left();
    } else if (group.right() > available.right()) {
        shiftX = available.right() - group.right();
    }
    if (group.top() < available.top()) {
        shiftY = available.top() - group.top();
    } else if (group.bottom() > available.bottom()) {
        shiftY = available.bottom() - group.bottom();
    }
    if (shiftX != 0 || shiftY != 0) {
        mainWindow_->setPosition(mainWindow_->position() + QPoint(shiftX, shiftY));
        position = computeSnapForEdge(effectiveEdge);
    }
    listWindow_->setPosition(position);
    updatingWindowGeometry_ = false;
    setListWindowX(position.x());
    setListWindowY(position.y());
    scheduleWindowStateSync();
}

void WindowController::setListDockEdge(const QString& edge)
{
    const QString normalized = isDockEdge(edge) ? edge : QStringLiteral("none");
    if (listDockEdge_ == normalized) {
        return;
    }
    listDockEdge_ = normalized;
    settings_.setValue(QStringLiteral("windows/listDockEdge"), listDockEdge_);
    scheduleWindowStateSync();
    emit listDockEdgeChanged();
}

QString WindowController::snapEdgeForPosition(int x, int y) const
{
    if (mainWindow_ == nullptr || listWindow_ == nullptr) {
        return QStringLiteral("none");
    }

    const QRect mainGeo = mainWindow_->geometry();
    const int listWidth = listWindow_->width();
    const int listHeight = listWindow_->height();
    const int leftX = mainGeo.left() - listWidth;
    const int rightX = mainGeo.right() + 1;
    const int topY = mainGeo.top() - listHeight;
    const int bottomY = mainGeo.bottom() + 1;
    const bool verticalProjectionOverlaps =
        y < mainGeo.bottom() + 1 && y + listHeight > mainGeo.top();
    const bool horizontalProjectionOverlaps =
        x < mainGeo.right() + 1 && x + listWidth > mainGeo.left();

    struct Candidate {
        QString edge;
        int distance;
    };
    const Candidate candidates[] = {
        {QStringLiteral("left"),
         verticalProjectionOverlaps ? qAbs(x - leftX) : kSnapDistance + 1},
        {QStringLiteral("right"),
         verticalProjectionOverlaps ? qAbs(x - rightX) : kSnapDistance + 1},
        {QStringLiteral("top"),
         horizontalProjectionOverlaps ? qAbs(y - topY) : kSnapDistance + 1},
        {QStringLiteral("bottom"),
         horizontalProjectionOverlaps ? qAbs(y - bottomY) : kSnapDistance + 1},
    };

    QString best = QStringLiteral("none");
    int bestDistance = kSnapDistance + 1;
    for (const Candidate& candidate : candidates) {
        if (candidate.distance <= kSnapDistance && candidate.distance < bestDistance) {
            best = candidate.edge;
            bestDistance = candidate.distance;
        }
    }
    return best;
}

void WindowController::loadPersistedWindowState()
{
    alwaysOnTop_ =
        settings_.value(QStringLiteral("windows/miniAlwaysOnTop"), false).toBool();
    listWindowRequestedVisible_ =
        settings_.value(QStringLiteral("windows/listRequestedVisible"), true).toBool();
    hasPersistedDockEdge_ = settings_.contains(QStringLiteral("windows/listDockEdge"));
    const QString storedEdge =
        settings_.value(QStringLiteral("windows/listDockEdge"), QStringLiteral("bottom"))
            .toString();
    listDockEdge_ = isDockEdge(storedEdge) ? storedEdge : QStringLiteral("none");
    listWindowDetached_ = listDockEdge_ == QStringLiteral("none");
}

void WindowController::restoreGeometry(QWindow* window, const QString& key)
{
    if (window == nullptr) {
        return;
    }
    QRect geometry = settings_.value(key).toRect();
    if (!geometry.isValid()) {
        return;
    }
    geometry.setWidth(qMax(geometry.width(), window->minimumWidth()));
    geometry.setHeight(qMax(geometry.height(), window->minimumHeight()));
    const auto screens = QGuiApplication::screens();
    QScreen* bestScreen = nullptr;
    qint64 bestArea = -1;
    for (QScreen* screen : screens) {
        if (screen == nullptr) {
            continue;
        }
        const QRect intersection = screen->availableGeometry().intersected(geometry);
        const qint64 area =
            static_cast<qint64>(intersection.width()) * intersection.height();
        if (area > bestArea) {
            bestArea = area;
            bestScreen = screen;
        }
    }
    if (bestScreen == nullptr) {
        bestScreen = QGuiApplication::primaryScreen();
    }
    if (bestScreen == nullptr) {
        window->setGeometry(geometry);
        return;
    }

    const QRect available = bestScreen->availableGeometry();
    geometry.setWidth(qMin(geometry.width(), available.width()));
    geometry.setHeight(qMin(geometry.height(), available.height()));
    const int maxX = available.right() - geometry.width() + 1;
    const int maxY = available.bottom() - geometry.height() + 1;
    geometry.moveLeft(qBound(available.left(), geometry.left(), maxX));
    geometry.moveTop(qBound(available.top(), geometry.top(), maxY));
    window->setGeometry(geometry);
}

void WindowController::persistGeometry(QWindow* window, const QString& key)
{
    if (window == nullptr || !window->geometry().isValid()) {
        return;
    }
    settings_.setValue(key, window->geometry());
}

void WindowController::scheduleWindowStateSync()
{
    windowStateSyncTimer_.start();
}

void WindowController::flushWindowState()
{
    windowStateSyncTimer_.stop();
    persistGeometry(mainWindow_, QStringLiteral("windows/mainGeometry"));
    persistGeometry(miniWindow_, QStringLiteral("windows/miniGeometry"));
    persistGeometry(listWindow_, QStringLiteral("windows/listGeometry"));
    settings_.sync();
}

QString WindowController::edgeForPreference(int edge)
{
    switch (edge) {
    case 0:
        return QStringLiteral("top");
    case 2:
        return QStringLiteral("left");
    case 3:
        return QStringLiteral("right");
    default:
        return QStringLiteral("bottom");
    }
}

QPoint WindowController::computeSnappedPosition(int x, int y) const
{
    const QString edge = magneticSnapEnabled_ ? snapEdgeForPosition(x, y)
                                              : QStringLiteral("none");
    return isDockEdge(edge) ? computeSnapForEdge(edge) : QPoint(x, y);
}

QPoint WindowController::computeSnapForEdge(const QString& direction) const
{
    if (mainWindow_ == nullptr || listWindow_ == nullptr) {
        return QPoint(listWindowX_, listWindowY_);
    }

    const QRect mainGeo = mainWindow_->geometry();
    const int listWidth = listWindow_->width();
    const int listHeight = listWindow_->height();
    const int centerY = mainGeo.y() + (mainGeo.height() - listHeight) / 2;
    const int centerX = mainGeo.x() + (mainGeo.width() - listWidth) / 2;

    QPoint target(listWindowX_, listWindowY_);
    if (direction == QStringLiteral("left")) {
        target = QPoint(mainGeo.left() - listWidth, centerY);
    } else if (direction == QStringLiteral("right")) {
        target = QPoint(mainGeo.right() + 1, centerY);
    } else if (direction == QStringLiteral("top")) {
        target = QPoint(centerX, mainGeo.top() - listHeight);
    } else if (direction == QStringLiteral("bottom")) {
        target = QPoint(centerX, mainGeo.bottom() + 1);
    }

    return target;
}

bool WindowController::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == mainWindow_) {
        if (event->type() == QEvent::Move || event->type() == QEvent::Resize) {
            scheduleWindowStateSync();
            if (!updatingWindowGeometry_ && !listWindowDetached_) {
                repositionDockedListWindow();
            }
        } else if (event->type() == QEvent::WindowStateChange) {
            applyListWindowVisible(shouldShowListWindow());
        }
    } else if (watched == miniWindow_
               && (event->type() == QEvent::Move || event->type() == QEvent::Resize)) {
        scheduleWindowStateSync();
    } else if (watched == listWindow_
               && (event->type() == QEvent::Move || event->type() == QEvent::Resize)) {
        setListWindowX(listWindow_->x());
        setListWindowY(listWindow_->y());
        setListWindowWidth(listWindow_->width());
        setListWindowHeight(listWindow_->height());
        if (!updatingWindowGeometry_) {
            scheduleWindowStateSync();
        }
    }
    return QObject::eventFilter(watched, event);
}
