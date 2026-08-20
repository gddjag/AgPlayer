#include "window_controller.hpp"

#include <QCoreApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QScreen>
#include <QSettings>
#include <QWindow>

#include <utility>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#endif

namespace {

constexpr int kSnapDistance = 15;
constexpr int kSnapReleaseDistance = 24;
// Frameless DWM windows keep a translucent edge pixel.  A two-logical-pixel
// shared edge covers it at every supported DPI without exposing the desktop.
constexpr int kDockOverlap = 2;

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

bool supportsWindowStacking()
{
    const QString platform = QGuiApplication::platformName();
    return platform.compare(QStringLiteral("offscreen"), Qt::CaseInsensitive) != 0
        && platform.compare(QStringLiteral("minimal"), Qt::CaseInsensitive) != 0;
}

void raiseWindow(QWindow* window)
{
    if (window != nullptr && supportsWindowStacking()) {
        window->raise();
    }
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
    if (QCoreApplication::instance() != nullptr) {
        QCoreApplication::instance()->installNativeEventFilter(this);
    }
    loadPersistedWindowState();
}

WindowController::~WindowController()
{
    if (QCoreApplication::instance() != nullptr) {
        QCoreApplication::instance()->removeNativeEventFilter(this);
    }
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
QString WindowController::snapPreviewEdge() const
{
    return isDockEdge(pendingListSnapEdge_) ? pendingListSnapEdge_
                                            : QStringLiteral("none");
}
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
        mainWindow_->disconnect(this);
    }
    if (miniWindow_ != nullptr) {
        miniWindow_->removeEventFilter(this);
    }

    // Do not publish mainWindow_ until its native handle exists. This
    // controller is also a global native event filter; calling winId() from
    // WM_NCCREATE would otherwise recursively create the same HWND.
    mainWindow_ = nullptr;
    mainWindowHandle_ = 0;
    miniWindow_ = miniWindow;
    if (mainWindow != nullptr) {
        const QString mainGeometryKey = QStringLiteral("windows/mainGeometry");
        const QString mainGeometryVersionKey =
            QStringLiteral("windows/mainGeometryVersion");
        if (settings_.value(mainGeometryVersionKey, 0).toInt() < 2) {
            QRect geometry = settings_.value(mainGeometryKey).toRect();
            if (geometry.isValid() && geometry.height() == 399) {
                geometry.setHeight(380);
                settings_.setValue(mainGeometryKey, geometry);
            }
            // Only migrate exact historical defaults. User-resized windows
            // remain untouched while the new compact first-run geometry wins.
            if (geometry.size() == QSize(1036, 321)
                || geometry.size() == QSize(1228, 380)) {
                geometry.setSize(QSize(960, 298));
                settings_.setValue(mainGeometryKey, geometry);
            }
            settings_.setValue(mainGeometryVersionKey, 2);
        }
        restoreGeometry(mainWindow, mainGeometryKey);
        const bool usesWindowsPlatform =
            QGuiApplication::platformName().compare(
                QStringLiteral("windows"), Qt::CaseInsensitive) == 0;
        const quintptr nativeHandle = usesWindowsPlatform
            ? static_cast<quintptr>(mainWindow->winId()) : 0;
        mainWindow_ = mainWindow;
        mainWindowHandle_ = nativeHandle;
        rememberNativePixelSize(mainWindow_);
        mainWindow_->installEventFilter(this);
        connect(mainWindow_, &QWindow::windowStateChanged, this,
                [this](const Qt::WindowState state) {
                    const bool visible = listWindowPanelAllowed_
                        && listWindowRequestedVisible_ && mainVisible_
                        && state != Qt::WindowMinimized
                        && (listWindowDetached_
                            || state != Qt::WindowMaximized);
                    applyListWindowVisible(visible);
                });
        applyPlatformWindowStyle(mainWindow_);
        mainWindow_->setVisible(mainVisible_);
        persistGeometry(mainWindow_, QStringLiteral("windows/mainGeometry"));
        if (audioToolsWindow_ != nullptr) {
            audioToolsWindow_->setTransientParent(mainWindow_);
        }
        if (settingsWindow_ != nullptr) {
            settingsWindow_->setTransientParent(mainWindow_);
        }
    }
    if (miniWindow_ != nullptr) {
        const QString miniGeometryKey =
            QStringLiteral("windows/miniGeometry");
        const QRect savedMiniGeometry =
            settings_.value(miniGeometryKey).toRect();
        const QString miniGeometryVersionKey =
            QStringLiteral("windows/miniGeometryVersion");
        if (settings_.value(miniGeometryVersionKey, 0).toInt() < 4
            && savedMiniGeometry.isValid()) {
            settings_.remove(miniGeometryKey);
        }
        settings_.setValue(miniGeometryVersionKey, 4);
        restoreGeometry(miniWindow_, miniGeometryKey);
        miniWindow_->installEventFilter(this);
        applyPlatformWindowStyle(miniWindow_);
        miniWindow_->setFlag(Qt::WindowStaysOnTopHint, alwaysOnTop_);
        miniWindow_->setVisible(miniVisible_);
        persistGeometry(miniWindow_, miniGeometryKey);
    }
}

void WindowController::setListWindow(QWindow* listWindow)
{
    if (listWindow_ != nullptr) {
        listWindow_->removeEventFilter(this);
    }
    if (listWindow == nullptr) {
        listWindow_ = nullptr;
        listWindowHandle_ = 0;
        return;
    }

    // Restore the logical geometry before materializing the native handle so
    // DPI/frame adjustments do not overwrite the persisted client geometry.
    restoreGeometry(listWindow, QStringLiteral("windows/listGeometry"));
    applyPlatformWindowStyle(listWindow);
    // Materialize the platform handle before publishing listWindow_. The
    // controller is already a native event filter at this point; calling
    // winId() from inside WM_NCCREATE would recursively create the same window.
    const bool usesWindowsPlatform =
        QGuiApplication::platformName().compare(
            QStringLiteral("windows"), Qt::CaseInsensitive) == 0;
    const quintptr nativeHandle = usesWindowsPlatform
        ? static_cast<quintptr>(listWindow->winId()) : 0;
    listWindow_ = listWindow;
    listWindowHandle_ = nativeHandle;
    rememberNativePixelSize(listWindow_);

    listWindow_->installEventFilter(this);
    setListDockEdge(listWindowDetached_ ? QStringLiteral("none") : listDockEdge_);
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
    if (audioToolsWindow_ != nullptr) {
        audioToolsWindow_->removeEventFilter(this);
    }
    audioToolsWindow_ = audioToolsWindow;
    if (audioToolsWindow_ != nullptr) {
        audioToolsWindow_->setTransientParent(mainWindow_);
        audioToolsWindow_->installEventFilter(this);
        applyPlatformWindowStyle(audioToolsWindow_);
        audioToolsWindow_->setVisible(audioToolsVisible_);
        if (audioToolsVisible_) {
            audioToolsWindow_->requestActivate();
            raiseWindow(audioToolsWindow_);
        }
    }
}

void WindowController::registerSettingsWindow(QWindow* window)
{
    if (settingsWindow_ != nullptr) {
        settingsWindow_->removeEventFilter(this);
    }
    settingsWindow_ = window;
    if (settingsWindow_ != nullptr) {
        settingsWindow_->setTransientParent(mainWindow_);
        restoreGeometry(settingsWindow_, QStringLiteral("windows/settingsGeometry"));
        settingsWindow_->installEventFilter(this);
        applyPlatformWindowStyle(settingsWindow_);
        persistGeometry(settingsWindow_, QStringLiteral("windows/settingsGeometry"));
    }
}

void WindowController::presentAuxiliaryWindow(QWindow* window)
{
    if (window == nullptr) {
        return;
    }

    if (window->transientParent() != mainWindow_) {
        window->setTransientParent(mainWindow_);
    }

    if (mainWindow_ != nullptr) {
        const QRect mainGeometry = mainWindow_->geometry();
        QScreen* screen = QGuiApplication::screenAt(mainGeometry.center());
        if (screen == nullptr) {
            screen = mainWindow_->screen();
        }
        if (screen != nullptr) {
            const QRect available = screen->availableGeometry();
            const QSize size(qMin(window->width(), available.width()),
                             qMin(window->height(), available.height()));
            if (size != window->size()) {
                window->resize(size);
            }
            QPoint position(mainGeometry.center().x() - (size.width() - 1) / 2,
                            mainGeometry.center().y() - (size.height() - 1) / 2);
            position.setX(qBound(available.left(), position.x(),
                                 available.right() - size.width() + 1));
            position.setY(qBound(available.top(), position.y(),
                                 available.bottom() - size.height() + 1));
            window->setPosition(position);
        }
    }

    lastAuxiliaryWindow_ = window;
    window->setVisible(true);
    window->requestActivate();
    raiseDockedGroup(window);
}

void WindowController::applyPlatformWindowStyle(QWindow* window) const
{
#ifdef Q_OS_WIN
    if (window == nullptr
        || QGuiApplication::platformName().compare(
               QStringLiteral("windows"), Qt::CaseInsensitive) != 0) {
        return;
    }
    constexpr DWORD kCornerPreferenceAttribute = 33;
    constexpr int kDoNotRound = 1;
    constexpr int kRound = 2;
    const int preference = isMaximized(window) ? kDoNotRound : kRound;
    const HWND hwnd = reinterpret_cast<HWND>(window->winId());
    if (hwnd != nullptr) {
        DwmSetWindowAttribute(
            hwnd,
            static_cast<DWMWINDOWATTRIBUTE>(kCornerPreferenceAttribute),
            &preference,
            sizeof(preference));
    }
#else
    Q_UNUSED(window);
#endif
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
    if (mainWindow_ != nullptr) {
        if (isMinimized(mainWindow_)) {
            mainWindow_->setWindowState(Qt::WindowNoState);
        }
        applyListWindowVisible(shouldShowListWindow());
        mainWindow_->requestActivate();
        raiseDockedGroup();
    }
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
        raiseWindow(listWindow_);
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
        if (visible) {
            presentAuxiliaryWindow(audioToolsWindow_);
        }
        return;
    }
    if (audioToolsWindow_ != nullptr) {
        if (visible) {
            presentAuxiliaryWindow(audioToolsWindow_);
        } else {
            audioToolsWindow_->setVisible(false);
        }
    }
    audioToolsVisible_ = visible;
    emit audioToolsVisibleChanged();
}

void WindowController::applyListWindowVisible(bool visible)
{
    if (listWindowVisible_ == visible
        && (listWindow_ == nullptr || listWindow_->isVisible() == visible)) {
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
            raiseWindow(listWindow_);
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

    // A docked player/list pair is one visual column. Keep the shared width
    // aligned while preserving the independently resizable list height.
    updatingWindowGeometry_ = true;
    if (listWindow_->width() != mainWindow_->width()) {
        listWindow_->resize(mainWindow_->width(), listWindow_->height());
    }
    // Keep docking in Qt's screen-independent coordinate space. Mixing HWND
    // outer-frame pixels with QWindow client geometry introduces a border/DPI
    // offset and makes the pair drift at monitor seams. WM_DPICHANGED below
    // independently preserves each native window's pixel size.
    listWindow_->setPosition(computeSnapForEdge(listDockEdge_));
    updatingWindowGeometry_ = false;
    const QPoint position = listWindow_->position();
    setListWindowX(position.x());
    setListWindowY(position.y());
    scheduleWindowStateSync();
}

void WindowController::setListDockEdge(const QString& edge)
{
    const QString normalized = isDockEdge(edge) ? edge : QStringLiteral("none");
    if (listWindow_ != nullptr) {
        QWindow* desiredOwner = normalized == QStringLiteral("none")
            ? nullptr : mainWindow_;
        if (listWindow_->transientParent() != desiredOwner) {
            listWindow_->setTransientParent(desiredOwner);
        }
    }
    if (listDockEdge_ == normalized) {
        return;
    }
    listDockEdge_ = normalized;
    settings_.setValue(QStringLiteral("windows/listDockEdge"), listDockEdge_);
    scheduleWindowStateSync();
    emit listDockEdgeChanged();
}

void WindowController::finishListWindowInteraction()
{
    if (listWindow_ == nullptr || mainWindow_ == nullptr) return;
    if (listWindowDetached_) {
        if (isDockEdge(pendingListSnapEdge_)) {
            listWindowDetached_ = false;
            emit listWindowDetachedChanged();
            setListDockEdge(pendingListSnapEdge_);
            repositionDockedListWindow();
        }
    } else if (pendingListDetach_) {
        setListWindowDetached(true);
    } else if (isDockEdge(listDockEdge_)) {
        repositionDockedListWindow();
    }
    const bool hadPreview = isDockEdge(pendingListSnapEdge_);
    pendingListSnapEdge_.clear();
    if (hadPreview) emit snapPreviewEdgeChanged();
    pendingListDetach_ = false;
    applyListWindowVisible(shouldShowListWindow());
}

void WindowController::raiseDockedGroup(QWindow* topWindow)
{
    QWindow* overlay = topWindow;
    if (overlay == nullptr && lastAuxiliaryWindow_ != nullptr
        && lastAuxiliaryWindow_->isVisible()) {
        overlay = lastAuxiliaryWindow_;
    }
#ifdef Q_OS_WIN
    constexpr UINT flags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE;
    const bool hasList = !listWindowDetached_ && listWindow_ != nullptr
        && listWindow_->isVisible();
    // DeferWindowPos resolves the relative inserts against the old z-order;
    // a transient QML list can therefore end above a settings/tools window.
    // Raise the group bottom-up in separate native operations instead. Each
    // HWND_TOP promotion is deterministic and preserves the final stack:
    // player < docked list < active auxiliary window.
    const auto raiseNative = [flags](QWindow* const window) {
        if (window == nullptr || !window->isVisible()) return;
        SetWindowPos(reinterpret_cast<HWND>(window->winId()),
                     HWND_TOP, 0, 0, 0, 0, flags);
    };
    raiseNative(mainWindow_);
    if (hasList) raiseNative(listWindow_);
    raiseNative(overlay);
#else
    if (mainWindow_ != nullptr) raiseWindow(mainWindow_);
    if (!listWindowDetached_ && listWindow_ != nullptr && listWindow_->isVisible()) {
        raiseWindow(listWindow_);
    }
    if (overlay != nullptr) raiseWindow(overlay);
#endif
}

bool WindowController::nativeEventFilter(const QByteArray& eventType, void* message,
                                         qintptr* result)
{
    Q_UNUSED(result);
#ifdef Q_OS_WIN
    if (eventType == QByteArrayLiteral("windows_generic_MSG")
        || eventType == QByteArrayLiteral("windows_dispatcher_MSG")) {
        const auto* msg = static_cast<MSG*>(message);
        if (msg != nullptr && msg->message == WM_DPICHANGED
            && (msg->hwnd == reinterpret_cast<HWND>(mainWindowHandle_)
                || msg->hwnd == reinterpret_cast<HWND>(listWindowHandle_))) {
            const bool mainChanged =
                msg->hwnd == reinterpret_cast<HWND>(mainWindowHandle_);
            const auto* suggestedRect = reinterpret_cast<RECT*>(msg->lParam);
            RECT currentRect{};
            if (suggestedRect != nullptr && GetWindowRect(msg->hwnd, &currentRect)) {
                const QRect currentGeometry(
                    currentRect.left, currentRect.top,
                    currentRect.right - currentRect.left,
                    currentRect.bottom - currentRect.top);
                const QRect suggestedGeometry(
                    suggestedRect->left, suggestedRect->top,
                    suggestedRect->right - suggestedRect->left,
                    suggestedRect->bottom - suggestedRect->top);
                QRect adjusted = geometryForDpiChange(
                    currentGeometry, suggestedGeometry);
                const QSize preservedSize = mainChanged
                    ? mainNativePixelSize_ : listNativePixelSize_;
                if (preservedSize.isValid()) adjusted.setSize(preservedSize);
                const HWND changedWindow = msg->hwnd;
                const qreal newDpr = qreal(LOWORD(msg->wParam)) / 96.0;
                // Let Qt consume WM_DPICHANGED first so its screen/DPR state is
                // current, then restore the user's native-pixel rectangle. Qt
                // otherwise preserves logical size and enlarges the window on
                // a higher-DPI monitor.
                QTimer::singleShot(0, this,
                                   [this, changedWindow, adjusted,
                                    mainChanged, newDpr]() {
                    if (!IsWindow(changedWindow)) return;
                    if (mainChanged) {
                        mainTrackedDpr_ = newDpr;
                    } else {
                        listTrackedDpr_ = newDpr;
                    }
                    updatingWindowGeometry_ = true;
                    SetWindowPos(changedWindow, nullptr,
                                 adjusted.x(), adjusted.y(),
                                 adjusted.width(), adjusted.height(),
                                 SWP_NOZORDER | SWP_NOACTIVATE);
                    updatingWindowGeometry_ = false;
                    if (mainChanged && !listWindowDetached_) {
                        repositionDockedListWindow();
                    }
                });
            }
        }
        if (msg != nullptr && mainWindow_ != nullptr
            && msg->hwnd == reinterpret_cast<HWND>(mainWindowHandle_)
            && msg->message == WM_ACTIVATE
            && LOWORD(msg->wParam) != WA_INACTIVE) {
            QTimer::singleShot(0, this, [this]() {
                if (mainWindow_ == nullptr || !mainWindow_->isVisible()) {
                    return;
                }
                showMain();
            });
        } else if (msg != nullptr && listWindow_ != nullptr
            && msg->hwnd == reinterpret_cast<HWND>(listWindowHandle_)
            && msg->message == WM_ENTERSIZEMOVE) {
            // Ignore geometry events generated by docking/DPI changes.  Only
            // a real list drag may request detaching the list from the player.
            listUserInteraction_ = true;
        } else if (msg != nullptr && listWindow_ != nullptr
            && msg->hwnd == reinterpret_cast<HWND>(listWindowHandle_)
            && msg->message == WM_EXITSIZEMOVE) {
            finishListWindowInteraction();
            listUserInteraction_ = false;
        }
    }
#else
    Q_UNUSED(eventType);
    Q_UNUSED(message);
#endif
    return false;
}

void WindowController::rememberNativePixelSize(QWindow* window)
{
#ifdef Q_OS_WIN
    if (window == nullptr) return;
    const HWND handle = reinterpret_cast<HWND>(window->winId());
    RECT rect{};
    if (handle == nullptr || !GetWindowRect(handle, &rect)) return;
    const QSize size(rect.right - rect.left, rect.bottom - rect.top);
    if (window == mainWindow_) {
        mainNativePixelSize_ = size;
        mainTrackedDpr_ = window->devicePixelRatio();
    } else if (window == listWindow_) {
        listNativePixelSize_ = size;
        listTrackedDpr_ = window->devicePixelRatio();
    }
#else
    Q_UNUSED(window);
#endif
}

QString WindowController::snapEdgeForPosition(int x, int y) const
{
    if (mainWindow_ == nullptr || listWindow_ == nullptr) {
        return QStringLiteral("none");
    }

    const QRect mainGeo = mainWindow_->geometry();
    const int listWidth = listWindow_->width();
    const int listHeight = listWindow_->height();
    const int leftX = mainGeo.left() - listWidth + kDockOverlap;
    const int rightX = mainGeo.right() - kDockOverlap + 1;
    const int topY = mainGeo.top() - listHeight + kDockOverlap;
    const int bottomY = mainGeo.bottom() - kDockOverlap + 1;
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
        // A QML Window may be created on the screen containing a stale cursor
        // position before the controller gets a chance to manage it.  Start a
        // first-run window on the primary work area instead so it is never
        // invisible on an unavailable/virtual secondary display.
        QScreen* const primary = QGuiApplication::primaryScreen();
        if (primary == nullptr || window->width() <= 0 || window->height() <= 0) {
            return;
        }
        const QRect available = primary->availableGeometry();
        // Programmatic callers may already have deliberately placed a window
        // on the primary screen. Preserve that geometry; only rehome the
        // platform's invisible first-run placement.
        if (available.intersects(window->geometry())) {
            return;
        }
        const QSize boundedSize(qMin(qMax(window->width(), window->minimumWidth()),
                                    available.width()),
                                qMin(qMax(window->height(), window->minimumHeight()),
                                    available.height()));
        QRect centered(QPoint(0, 0), boundedSize);
        centered.moveCenter(available.center());
        window->setGeometry(centered);
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

    // A restored docked group may deliberately span two monitors.  Constraining
    // it to the single screen with the largest overlap used to shrink the list
    // (and break its shared edge with the player) whenever it crossed a monitor
    // boundary.  Bound only against the full virtual work area instead.
    QRect virtualAvailable;
    for (QScreen* screen : screens) {
        if (screen != nullptr) {
            virtualAvailable = virtualAvailable.united(screen->availableGeometry());
        }
    }
    if (!virtualAvailable.isValid()) {
        virtualAvailable = bestScreen->availableGeometry();
    }
    geometry.setWidth(qMin(geometry.width(), virtualAvailable.width()));
    geometry.setHeight(qMin(geometry.height(), virtualAvailable.height()));
    const int maxX = virtualAvailable.right() - geometry.width() + 1;
    const int maxY = virtualAvailable.bottom() - geometry.height() + 1;
    geometry.moveLeft(qBound(virtualAvailable.left(), geometry.left(), maxX));
    geometry.moveTop(qBound(virtualAvailable.top(), geometry.top(), maxY));
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
    persistGeometry(settingsWindow_, QStringLiteral("windows/settingsGeometry"));
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

QRect WindowController::geometryForDpiChange(
    const QRect& currentGeometry, const QRect& suggestedGeometry)
{
    if (!currentGeometry.isValid() || !suggestedGeometry.isValid()) {
        return suggestedGeometry;
    }
    QRect adjusted = suggestedGeometry;
    adjusted.setSize(currentGeometry.size());
    return adjusted;
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
    QPoint target(listWindowX_, listWindowY_);
    if (direction == QStringLiteral("left")) {
        target = QPoint(mainGeo.left() - listWidth + kDockOverlap, mainGeo.y());
    } else if (direction == QStringLiteral("right")) {
        target = QPoint(mainGeo.right() - kDockOverlap + 1, mainGeo.y());
    } else if (direction == QStringLiteral("top")) {
        target = QPoint(mainGeo.x(), mainGeo.top() - listHeight + kDockOverlap);
    } else if (direction == QStringLiteral("bottom")) {
        target = QPoint(mainGeo.x(), mainGeo.bottom() - kDockOverlap + 1);
    }

    return target;
}

bool WindowController::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == mainWindow_) {
        if (event->type() == QEvent::Move || event->type() == QEvent::Resize) {
#ifdef Q_OS_WIN
            if (event->type() == QEvent::Resize
                && qFuzzyCompare(mainTrackedDpr_, mainWindow_->devicePixelRatio())) {
                rememberNativePixelSize(mainWindow_);
            }
#endif
            scheduleWindowStateSync();
            if (!updatingWindowGeometry_ && !listWindowDetached_) {
                repositionDockedListWindow();
            }
        } else if (event->type() == QEvent::WindowStateChange) {
            applyPlatformWindowStyle(mainWindow_);
            const Qt::WindowState state = mainWindow_->windowState();
            const bool visible = listWindowPanelAllowed_
                && listWindowRequestedVisible_ && mainVisible_
                && state != Qt::WindowMinimized
                && (listWindowDetached_ || state != Qt::WindowMaximized);
            applyListWindowVisible(visible);
        } else if (event->type() == QEvent::WindowActivate
                   && !updatingWindowZOrder_ && !listWindowDetached_) {
            updatingWindowZOrder_ = true;
            raiseDockedGroup();
            updatingWindowZOrder_ = false;
        }
    } else if ((watched == audioToolsWindow_ || watched == settingsWindow_)
               && event->type() == QEvent::WindowActivate
               && !updatingWindowZOrder_) {
        updatingWindowZOrder_ = true;
        lastAuxiliaryWindow_ = qobject_cast<QWindow*>(watched);
        raiseDockedGroup(lastAuxiliaryWindow_);
        updatingWindowZOrder_ = false;
    } else if (watched == listWindow_ && event->type() == QEvent::WindowActivate
               && !updatingWindowZOrder_ && !listWindowDetached_) {
        updatingWindowZOrder_ = true;
        raiseDockedGroup();
        updatingWindowZOrder_ = false;
    } else if (watched == miniWindow_
               && (event->type() == QEvent::Move || event->type() == QEvent::Resize)) {
        scheduleWindowStateSync();
    } else if ((watched == audioToolsWindow_ || watched == settingsWindow_)
               && (event->type() == QEvent::Move
                   || event->type() == QEvent::Resize)) {
        scheduleWindowStateSync();
    } else if ((watched == miniWindow_ || watched == listWindow_
                || watched == audioToolsWindow_ || watched == settingsWindow_)
               && event->type() == QEvent::WindowStateChange) {
        applyPlatformWindowStyle(qobject_cast<QWindow*>(watched));
    } else if (watched == listWindow_
               && (event->type() == QEvent::Move || event->type() == QEvent::Resize)) {
#ifdef Q_OS_WIN
        if (event->type() == QEvent::Resize
            && qFuzzyCompare(listTrackedDpr_, listWindow_->devicePixelRatio())) {
            rememberNativePixelSize(listWindow_);
        }
#endif
        if (!updatingWindowGeometry_ && magneticSnapEnabled_
            && mainWindow_ != nullptr) {
            if (listWindowDetached_) {
                const QString candidate = snapEdgeForPosition(
                    listWindow_->x(), listWindow_->y());
                if (pendingListSnapEdge_ != candidate) {
                    pendingListSnapEdge_ = candidate;
                    emit snapPreviewEdgeChanged();
                }
            } else if (isDockEdge(listDockEdge_)) {
                const QPoint expected = computeSnapForEdge(listDockEdge_);
                const QPoint actual = listWindow_->position();
                const int movementDelta = qMax(qAbs(actual.x() - expected.x()),
                                               qAbs(actual.y() - expected.y()));
                pendingListDetach_ = movementDelta > kSnapReleaseDistance;
            }
        }
        setListWindowX(listWindow_->x());
        setListWindowY(listWindow_->y());
        setListWindowWidth(listWindow_->width());
        setListWindowHeight(listWindow_->height());
        if (!updatingWindowGeometry_) {
            scheduleWindowStateSync();
        }
    } else if (watched == listWindow_
               && (event->type() == QEvent::MouseButtonRelease
                   || event->type() == QEvent::NonClientAreaMouseButtonRelease)) {
        finishListWindowInteraction();
    }
    return QObject::eventFilter(watched, event);
}
