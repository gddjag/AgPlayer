#include "window_controller.hpp"

#include <QCoreApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QPlatformSurfaceEvent>
#include <QScreen>
#include <QSettings>
#include <QWindow>

#include <limits>
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
QRect WindowController::mainWindowGeometry() const noexcept
{
    return mainWindow_ != nullptr ? mainWindow_->geometry() : QRect();
}
bool WindowController::miniVisible() const noexcept { return miniVisible_; }
bool WindowController::immersivePresentationActive() const noexcept
{
    return immersivePresentationActive_;
}
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

QRect WindowController::availableGeometryForWindow(QWindow* window) const
{
    QScreen* screen = window != nullptr ? window->screen() : nullptr;
    if (screen == nullptr) {
        screen = QGuiApplication::primaryScreen();
    }
    return screen != nullptr ? screen->availableGeometry() : QRect();
}

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
    const QRect previousMainGeometry = mainWindowGeometry();
    mainWindow_ = nullptr;
    mainWindowHandle_ = 0;
    miniWindow_ = miniWindow;
    if (mainWindow != nullptr) {
        const QString mainGeometryKey = mainWindowGeometryKey();
        const QString mainGeometryVersionKey =
            QStringLiteral("windows/mainGeometryVersion");
        if (mainWindowShellMode_ == 0
            && settings_.value(mainGeometryVersionKey, 0).toInt() < 2) {
            QRect geometry = settings_.value(mainGeometryKey).toRect();
            if (geometry.isValid() && geometry.height() == 399) {
                geometry.setHeight(380);
                settings_.setValue(mainGeometryKey, geometry);
            }
            // Only migrate exact historical defaults. User-resized windows
            // remain untouched while the new compact first-run geometry wins.
            if (geometry.size() == QSize(1036, 321)
                || geometry.size() == QSize(1228, 380)) {
                geometry.setSize(QSize(863, 266));
                settings_.setValue(mainGeometryKey, geometry);
            }
            settings_.setValue(mainGeometryVersionKey, 2);
        }
        restoreMainWindowGeometry(mainWindow);
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
        persistGeometry(mainWindow_, mainWindowGeometryKey());
        if (mainWindow_->geometry() != previousMainGeometry) {
            emit mainWindowGeometryChanged();
        }
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
    if (mainWindow_ == nullptr && previousMainGeometry.isValid()) {
        emit mainWindowGeometryChanged();
    }
}

void WindowController::setMainWindowShellMode(int mode)
{
    mode = mode >= 0 && mode <= 2 ? mode : 0;
    if (immersivePresentationActive_) {
        immersiveDeferredMainWindowShellMode_ = mode;
        immersiveDeferredMainWindowShellModeRequested_ =
            mode != immersiveRestoreMainWindowShellMode_;
        return;
    }
    if (mainWindowShellMode_ == mode) {
        return;
    }
    persistGeometry(mainWindow_, mainWindowGeometryKey());
    persistListWindowState();
    const QPoint previousPosition = mainWindow_ != nullptr ? mainWindow_->position() : QPoint();
    const QRect previousAvailable = availableGeometryForWindow(mainWindow_);
    mainWindowShellMode_ = mode;
    // QML's minimum size follows this signal, after the outgoing size is saved.
    emit mainWindowShellModeChanged();
    if (mainWindowShellMode_ == 0) {
        loadPersistedListWindowState();
    }
    if (mainWindow_ != nullptr) {
        // Switching shells is not a startup restore: keep the live monitor and
        // anchor, while recalling only the destination shell's logical size.
        {
            const QRect saved = settings_.value(mainWindowGeometryKey()).toRect();
            const QSize referenceSize = saved.isValid() ? saved.size()
                : mainWindowShellMode_ == 1 ? QSize(1386, 832)
                : mainWindowShellMode_ == 2 ? QSize(1386, 972) : QSize(863, 266);
            const QRect available = previousAvailable;
            const QSize preferredSize = referenceSize.expandedTo(mainWindow_->minimumSize());
            const QSize targetSize = available.isValid()
                ? preferredSize.boundedTo(available.size()) : preferredSize;
            QRect geometry(previousPosition, targetSize);
            if (available.isValid()) {
                if (geometry.right() > available.right()) {
                    geometry.moveRight(available.right());
                }
                if (geometry.bottom() > available.bottom()) {
                    geometry.moveBottom(available.bottom());
                }
                if (geometry.left() < available.left()) {
                    geometry.moveLeft(available.left());
                }
                if (geometry.top() < available.top()) {
                    geometry.moveTop(available.top());
                }
            }
            mainWindow_->setGeometry(geometry);
        }
        rememberNativePixelSize(mainWindow_);
        persistGeometry(mainWindow_, mainWindowGeometryKey());
        emit mainWindowGeometryChanged();
    }
    if (listWindow_ != nullptr && mainWindowShellMode_ == 0) {
        listWindowGeometryInitialized_ = false;
        restoreGeometry(listWindow_, listWindowGeometryKey());
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
    } else {
        applyListWindowVisible(false);
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

    const QString listGeometryKey = listWindowGeometryKey();
    const QString listGeometryVersionKey = listWindowGeometryVersionKey();
    if (settings_.value(listGeometryVersionKey, 0).toInt() < 1) {
        QRect geometry = settings_.value(listGeometryKey).toRect();
        if (mainWindowShellMode_ == 0 && geometry.isValid()
            && (geometry.width() == 1104 || geometry.width() == 1228
                || geometry.width() == 1284 || geometry.width() == 1447)) {
            geometry.setWidth(960);
            settings_.setValue(listGeometryKey, geometry);
        }
        settings_.setValue(listGeometryVersionKey, 1);
    }

    // Restore the logical geometry before materializing the native handle so
    // DPI/frame adjustments do not overwrite the persisted client geometry.
    restoreGeometry(listWindow, listGeometryKey);
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
    applyPlatformWindowStyle(listWindow_);

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
    persistGeometry(listWindow_, listGeometryKey);
}

void WindowController::setAudioToolsWindow(QWindow* audioToolsWindow)
{
    if (audioToolsWindow_ != nullptr) {
        audioToolsWindow_->removeEventFilter(this);
        disconnect(audioToolsWindow_, nullptr, this, nullptr);
        positionedAuxiliaryWindows_.remove(audioToolsWindow_);
    }
    audioToolsWindowHandle_ = 0;
    audioToolsTrackedDpr_ = 1.0;
    audioToolsWindow_ = audioToolsWindow;
    if (audioToolsWindow_ != nullptr) {
        const QString geometryKey = QStringLiteral("windows/audioToolsGeometry");
        if (restoreGeometry(audioToolsWindow_, geometryKey)) {
            positionedAuxiliaryWindows_.insert(audioToolsWindow_);
        }
        audioToolsWindow_->setTransientParent(mainWindow_);
        const bool usesWindowsPlatform =
            QGuiApplication::platformName().compare(
                QStringLiteral("windows"), Qt::CaseInsensitive) == 0;
        audioToolsWindowHandle_ = usesWindowsPlatform
            ? static_cast<quintptr>(audioToolsWindow_->winId()) : 0;
        QWindow* const trackedWindow = audioToolsWindow_;
        connect(trackedWindow, &QObject::destroyed, this,
                [this, trackedWindow] {
            positionedAuxiliaryWindows_.remove(trackedWindow);
            if (audioToolsWindow_.isNull()) {
                audioToolsWindowHandle_ = 0;
                audioToolsTrackedDpr_ = 1.0;
            }
        });
        audioToolsTrackedDpr_ = audioToolsWindow_->devicePixelRatio();
        audioToolsWindow_->installEventFilter(this);
        applyPlatformWindowStyle(audioToolsWindow_);
        persistGeometry(audioToolsWindow_, geometryKey);
        if (audioToolsVisible_) {
            presentAuxiliaryWindow(audioToolsWindow_);
        } else {
            audioToolsWindow_->setVisible(false);
        }
    }
}

void WindowController::registerSettingsWindow(QWindow* window)
{
    if (settingsWindow_ != nullptr) {
        settingsWindow_->removeEventFilter(this);
        disconnect(settingsWindow_, nullptr, this, nullptr);
        positionedAuxiliaryWindows_.remove(settingsWindow_);
    }
    settingsWindowHandle_ = 0;
    settingsNativePixelSize_ = {};
    settingsWindow_ = window;
    if (settingsWindow_ != nullptr) {
        settingsWindow_->setTransientParent(mainWindow_);
        const QString geometryKey = QStringLiteral("windows/settingsGeometry");
        if (restoreGeometry(settingsWindow_, geometryKey)) {
            positionedAuxiliaryWindows_.insert(settingsWindow_);
        }
        const bool usesWindowsPlatform =
            QGuiApplication::platformName().compare(
                QStringLiteral("windows"), Qt::CaseInsensitive) == 0;
        settingsWindowHandle_ = usesWindowsPlatform
            ? static_cast<quintptr>(settingsWindow_->winId()) : 0;
        QWindow* const trackedWindow = settingsWindow_;
        connect(trackedWindow, &QObject::destroyed, this,
                [this, trackedWindow] {
            positionedAuxiliaryWindows_.remove(trackedWindow);
            if (settingsWindow_.isNull()) {
                settingsWindowHandle_ = 0;
                settingsNativePixelSize_ = {};
                settingsTrackedDpr_ = 1.0;
            }
        });
        rememberNativePixelSize(settingsWindow_);
        settingsWindow_->installEventFilter(this);
        applyPlatformWindowStyle(settingsWindow_);
        persistGeometry(settingsWindow_, geometryKey);
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

    if (mainWindow_ != nullptr && !positionedAuxiliaryWindows_.contains(window)) {
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
    positionedAuxiliaryWindows_.insert(window);

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
        LONG_PTR extendedStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        const LONG_PTR originalExtendedStyle = extendedStyle;
        if (window == mainWindow_) {
            extendedStyle |= WS_EX_APPWINDOW;
            extendedStyle &= ~static_cast<LONG_PTR>(WS_EX_TOOLWINDOW);
        } else if (window == listWindow_ || window == audioToolsWindow_
                   || window == settingsWindow_) {
            extendedStyle |= WS_EX_TOOLWINDOW;
            extendedStyle &= ~static_cast<LONG_PTR>(WS_EX_APPWINDOW);
        }
        if (extendedStyle != originalExtendedStyle) {
            SetWindowLongPtrW(hwnd, GWL_EXSTYLE, extendedStyle);
            SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER
                             | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        }
        if (window == mainWindow_) {
            ensureTaskbarWindowStyles(window);
        }
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

#ifdef Q_OS_WIN
void WindowController::ensureTaskbarWindowStyles(QWindow* window) const
{
    if (window == nullptr || window != mainWindow_
        || QGuiApplication::platformName().compare(
               QStringLiteral("windows"), Qt::CaseInsensitive) != 0) {
        return;
    }

    const HWND hwnd = reinterpret_cast<HWND>(window->winId());
    if (hwnd == nullptr) {
        return;
    }
    const LONG_PTR originalStyle = GetWindowLongPtrW(hwnd, GWL_STYLE);
    const LONG_PTR taskbarStyle = originalStyle
        | static_cast<LONG_PTR>(WS_SYSMENU | WS_MINIMIZEBOX);
    if (taskbarStyle == originalStyle) {
        return;
    }

    SetWindowLongPtrW(hwnd, GWL_STYLE, taskbarStyle);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE
                     | SWP_FRAMECHANGED);
}
#endif

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
    if (immersivePresentationActive_) {
        immersiveRestoreView_ = PendingView::Mini;
        pendingView_ = PendingView::None;
        applyMainVisible(false);
        applyMiniVisible(false);
        return;
    }
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
    if (immersivePresentationActive_) {
        immersiveRestoreView_ = PendingView::Main;
        pendingView_ = PendingView::None;
        applyMainVisible(false);
        applyMiniVisible(false);
        return;
    }
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

void WindowController::enterImmersivePresentation()
{
    if (immersivePresentationActive_) {
        return;
    }
    immersiveRestoreView_ = miniVisible_ ? PendingView::Mini : PendingView::Main;
    immersiveRestoreMainWindowShellMode_ = mainWindowShellMode_;
    immersiveDeferredMainWindowShellMode_ = mainWindowShellMode_;
    immersiveDeferredMainWindowShellModeRequested_ = false;
    immersiveRestoreMainGeometry_ = mainWindow_ != nullptr
        ? mainWindow_->geometry() : QRect();
    immersiveRestoreMiniGeometry_ = miniWindow_ != nullptr
        ? miniWindow_->geometry() : QRect();
    immersiveRestoreMainGeometryKey_ = mainWindowGeometryKey();
    immersiveRestoreMainGeometryWasPersisted_ =
        settings_.contains(immersiveRestoreMainGeometryKey_);
    immersiveRestoreMiniGeometryWasPersisted_ = settings_.contains(
        QStringLiteral("windows/miniGeometry"));
    immersivePresentationActive_ = true;
    emit immersivePresentationActiveChanged();
    applyMainVisible(false);
    applyMiniVisible(false);
}

void WindowController::leaveImmersivePresentation()
{
    if (!immersivePresentationActive_) {
        return;
    }
    immersivePresentationActive_ = false;
    emit immersivePresentationActiveChanged();
    if (immersiveDeferredMainWindowShellModeRequested_) {
        if (mainWindow_ != nullptr && immersiveRestoreMainGeometry_.isValid()) {
            mainWindow_->setGeometry(immersiveRestoreMainGeometry_);
        }
        setMainWindowShellMode(immersiveDeferredMainWindowShellMode_);
        immersiveRestoreMainGeometryKey_ = mainWindowGeometryKey();
        immersiveRestoreMainGeometryWasPersisted_ =
            settings_.contains(immersiveRestoreMainGeometryKey_);
        if (mainWindow_ != nullptr) {
            immersiveRestoreMainGeometry_ = mainWindow_->geometry();
        }
    } else {
        mainWindowShellMode_ = immersiveRestoreMainWindowShellMode_;
        if (mainWindow_ != nullptr && immersiveRestoreMainGeometry_.isValid()) {
            mainWindow_->setGeometry(immersiveRestoreMainGeometry_);
        }
    }
    if (miniWindow_ != nullptr && immersiveRestoreMiniGeometry_.isValid()) {
        miniWindow_->setGeometry(immersiveRestoreMiniGeometry_);
    }
    if (immersiveRestoreView_ == PendingView::Mini) {
        showMini();
    } else {
        showMain();
    }
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
    return mainWindowShellMode_ == 0
        && listWindowPanelAllowed_ && listWindowRequestedVisible_ && mainVisible_
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
    if (mainWindowShellMode_ != 0) {
        applyListWindowVisible(false);
        return;
    }
    listWindowRequestedVisible_ = true;
    settings_.setValue(listWindowRequestedVisibleKey(), true);
    scheduleWindowStateSync();
    applyListWindowVisible(shouldShowListWindow());
}

void WindowController::hideListWindow()
{
    if (mainWindowShellMode_ != 0) {
        applyListWindowVisible(false);
        return;
    }
    listWindowRequestedVisible_ = false;
    settings_.setValue(listWindowRequestedVisibleKey(), false);
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

    // A docked list always shares the player's width. The list height remains
    // independent so its ten-row default and user-selected height are kept.
    updatingWindowGeometry_ = true;
#ifdef Q_OS_WIN
    const HWND mainHandle = reinterpret_cast<HWND>(mainWindowHandle_);
    const HWND listHandle = reinterpret_cast<HWND>(listWindowHandle_);
    RECT mainRect{};
    RECT listRect{};
    if (mainHandle != nullptr && listHandle != nullptr
        && GetWindowRect(mainHandle, &mainRect)
        && GetWindowRect(listHandle, &listRect)) {
        const int mainWidth = mainRect.right - mainRect.left;
        const int listWidth = listRect.right - listRect.left;
        const int listHeight = listRect.bottom - listRect.top;
        const int targetWidth = mainWidth;
        int targetX = listRect.left;
        int targetY = listRect.top;
        if (listDockEdge_ == QStringLiteral("left")) {
            targetX = mainRect.left - targetWidth + kDockOverlap;
            targetY = mainRect.top;
        } else if (listDockEdge_ == QStringLiteral("right")) {
            targetX = mainRect.right - kDockOverlap;
            targetY = mainRect.top;
        } else if (listDockEdge_ == QStringLiteral("top")) {
            targetX = mainRect.left;
            targetY = mainRect.top - listHeight + kDockOverlap;
        } else {
            targetX = mainRect.left;
            targetY = mainRect.bottom - kDockOverlap;
        }
        if (targetX != listRect.left || targetY != listRect.top
            || targetWidth != listWidth) {
            SetWindowPos(listHandle, nullptr, targetX, targetY,
                         targetWidth, listHeight,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        listNativePixelSize_.setWidth(targetWidth);
    } else
#endif
    {
        QSize targetSize = listWindow_->size();
        targetSize.setWidth(mainWindow_->width());
        listWindow_->resize(targetSize);
        listWindow_->setPosition(computeSnapForEdge(listDockEdge_));
    }
    updatingWindowGeometry_ = false;
    const QPoint position = listWindow_->position();
    setListWindowX(position.x());
    setListWindowY(position.y());
    setListWindowWidth(listWindow_->width());
    setListWindowHeight(listWindow_->height());
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
        applyPlatformWindowStyle(listWindow_);
    }
    if (listDockEdge_ == normalized) {
        return;
    }
    listDockEdge_ = normalized;
    settings_.setValue(listWindowDockEdgeKey(), listDockEdge_);
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
        if (msg != nullptr
            && msg->hwnd == reinterpret_cast<HWND>(mainWindowHandle_)
            && msg->message == WM_SYSCOMMAND) {
            const UINT command = static_cast<UINT>(msg->wParam) & 0xFFF0U;
            if (command == SC_MINIMIZE) {
                QTimer::singleShot(0, this, [this] {
                    applyListWindowVisible(false);
                });
            } else if (command == SC_RESTORE) {
                QTimer::singleShot(0, this, [this] {
                    applyMainVisible(true);
                    applyListWindowVisible(shouldShowListWindow());
                    raiseDockedGroup();
                });
            }
        }
        if (msg != nullptr && msg->message == WM_DPICHANGED
            && (msg->hwnd == reinterpret_cast<HWND>(mainWindowHandle_)
                || msg->hwnd == reinterpret_cast<HWND>(listWindowHandle_)
                || msg->hwnd == reinterpret_cast<HWND>(audioToolsWindowHandle_)
                || msg->hwnd == reinterpret_cast<HWND>(settingsWindowHandle_))) {
            const bool mainChanged =
                msg->hwnd == reinterpret_cast<HWND>(mainWindowHandle_);
            const bool listChanged =
                msg->hwnd == reinterpret_cast<HWND>(listWindowHandle_);
            const bool audioToolsChanged =
                msg->hwnd == reinterpret_cast<HWND>(audioToolsWindowHandle_);
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
                const qreal newDpr = qreal(LOWORD(msg->wParam)) / 96.0;
                QRect adjusted;
                if (audioToolsChanged) {
                    MONITORINFO targetMonitorInfo{};
                    targetMonitorInfo.cbSize = sizeof(targetMonitorInfo);
                    QRect targetAvailableGeometry;
                    const HMONITOR targetMonitor = MonitorFromRect(
                        suggestedRect, MONITOR_DEFAULTTONEAREST);
                    if (targetMonitor != nullptr
                        && GetMonitorInfoW(targetMonitor, &targetMonitorInfo)) {
                        const RECT& workArea = targetMonitorInfo.rcWork;
                        targetAvailableGeometry = QRect(
                            workArea.left, workArea.top,
                            workArea.right - workArea.left,
                            workArea.bottom - workArea.top);
                    }
                    adjusted = geometryForDpiChange(
                        currentGeometry, audioToolsTrackedDpr_, suggestedGeometry,
                        newDpr, targetAvailableGeometry);
                } else {
                    const QSize preservedSize = mainChanged ? mainNativePixelSize_
                        : listChanged ? listNativePixelSize_ : settingsNativePixelSize_;
                    if (mainChanged || listChanged) {
                        const qreal oldDpr = mainChanged ? mainTrackedDpr_ : listTrackedDpr_;
                        adjusted = geometryForDpiChange(
                            QRect(currentGeometry.topLeft(), preservedSize.isValid()
                                ? preservedSize : currentGeometry.size()),
                            oldDpr, suggestedGeometry, newDpr, QRect());
                    } else {
                        adjusted = geometryForDpiChange(currentGeometry, suggestedGeometry);
                        if (preservedSize.isValid()) adjusted.setSize(preservedSize);
                    }
                }
                const HWND changedWindow = msg->hwnd;
                const quintptr capturedHandle = reinterpret_cast<quintptr>(changedWindow);
                const QPointer<QWindow> changedQtWindow = mainChanged ? mainWindow_
                    : listChanged ? listWindow_
                    : audioToolsChanged ? audioToolsWindow_ : settingsWindow_;
                // Let Qt consume WM_DPICHANGED first so its screen/DPR state is
                // current, then apply the calculated native rectangle.
                QTimer::singleShot(0, this,
                                   [this, changedWindow, capturedHandle,
                                     changedQtWindow, adjusted,
                                     mainChanged, listChanged,
                                     audioToolsChanged, newDpr]() {
                    const bool stillTracked = changedQtWindow != nullptr
                        && (mainChanged
                                ? changedQtWindow == mainWindow_
                                    && capturedHandle == mainWindowHandle_
                            : listChanged
                                ? changedQtWindow == listWindow_
                                    && capturedHandle == listWindowHandle_
                            : audioToolsChanged
                                ? changedQtWindow == audioToolsWindow_
                                    && capturedHandle == audioToolsWindowHandle_
                                : changedQtWindow == settingsWindow_
                                    && capturedHandle == settingsWindowHandle_);
                    if (!stillTracked
                        || reinterpret_cast<HWND>(changedQtWindow->winId())
                            != changedWindow
                        || !IsWindow(changedWindow)) {
                        return;
                    }
                    if (mainChanged) {
                        mainTrackedDpr_ = newDpr;
                    } else if (listChanged) {
                        listTrackedDpr_ = newDpr;
                    } else if (audioToolsChanged) {
                        audioToolsTrackedDpr_ = newDpr;
                    } else {
                        settingsTrackedDpr_ = newDpr;
                    }
                    updatingWindowGeometry_ = true;
                    SetWindowPos(changedWindow, nullptr,
                                 adjusted.x(), adjusted.y(),
                                 adjusted.width(), adjusted.height(),
                                 SWP_NOZORDER | SWP_NOACTIVATE);
                    updatingWindowGeometry_ = false;
                    if (mainChanged || listChanged) {
                        rememberNativePixelSize(changedQtWindow);
                        repositionDockedListWindow();
                    }
                });
            }
        }
        if (msg != nullptr && listWindow_ != nullptr
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
    } else if (window == settingsWindow_) {
        settingsNativePixelSize_ = size;
        settingsTrackedDpr_ = window->devicePixelRatio();
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
    loadPersistedListWindowState();
}

void WindowController::loadPersistedListWindowState()
{
    listWindowRequestedVisible_ =
        settings_.value(listWindowRequestedVisibleKey(), true).toBool();
    hasPersistedDockEdge_ = settings_.contains(listWindowDockEdgeKey());
    const QString storedEdge =
        settings_.value(listWindowDockEdgeKey(), QStringLiteral("bottom"))
            .toString();
    const QString nextDockEdge = isDockEdge(storedEdge)
        ? storedEdge : QStringLiteral("none");
    const bool nextDetached = nextDockEdge == QStringLiteral("none");
    const bool dockEdgeChanged = listDockEdge_ != nextDockEdge;
    const bool detachedChanged = listWindowDetached_ != nextDetached;
    listDockEdge_ = nextDockEdge;
    listWindowDetached_ = nextDetached;
    if (dockEdgeChanged) {
        emit listDockEdgeChanged();
    }
    if (detachedChanged) {
        emit listWindowDetachedChanged();
    }
}

void WindowController::persistListWindowState()
{
    if (mainWindowShellMode_ != 0) {
        return;
    }
    settings_.setValue(listWindowRequestedVisibleKey(), listWindowRequestedVisible_);
    settings_.setValue(listWindowDockEdgeKey(), listDockEdge_);
    persistGeometry(listWindow_, listWindowGeometryKey());
    settings_.setValue(listWindowGeometryVersionKey(), 1);
}

bool WindowController::restoreGeometry(QWindow* window, const QString& key)
{
    if (window == nullptr) {
        return false;
    }
    QRect geometry = settings_.value(key).toRect();
    if (!geometry.isValid()) {
        // A QML Window may be created on the screen containing a stale cursor
        // position before the controller gets a chance to manage it.  Start a
        // first-run window on the primary work area instead so it is never
        // invisible on an unavailable/virtual secondary display.
        QScreen* const primary = QGuiApplication::primaryScreen();
        if (primary == nullptr || window->width() <= 0 || window->height() <= 0) {
            return false;
        }
        const QRect available = primary->availableGeometry();
        // Programmatic callers may already have deliberately placed a window
        // on the primary screen. Preserve that geometry; only rehome the
        // platform's invisible first-run placement.
        if (available.intersects(window->geometry())) {
            return false;
        }
        const QSize boundedSize(qMin(qMax(window->width(), window->minimumWidth()),
                                    available.width()),
                                qMin(qMax(window->height(), window->minimumHeight()),
                                    available.height()));
        QRect centered(QPoint(0, 0), boundedSize);
        centered.moveCenter(available.center());
        window->setGeometry(centered);
        return false;
    }
    const auto screens = QGuiApplication::screens();
    QList<QRect> availableScreens;
    availableScreens.reserve(screens.size());
    int primaryScreenIndex = -1;
    QScreen* const primary = QGuiApplication::primaryScreen();
    for (QScreen* screen : screens) {
        if (screen == primary) {
            primaryScreenIndex = availableScreens.size();
        }
        availableScreens.append(
            screen != nullptr ? screen->availableGeometry() : QRect());
    }
    geometry = geometryForAvailableScreens(
        geometry, window->minimumSize(), availableScreens, primaryScreenIndex);
    window->setGeometry(geometry);
    return true;
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
    if (!immersivePresentationActive_) {
        if (immersiveRestoreMainGeometryWasPersisted_
            || mainWindowGeometryKey() != immersiveRestoreMainGeometryKey_) {
            persistGeometry(mainWindow_, mainWindowGeometryKey());
        }
        if (immersiveRestoreMiniGeometryWasPersisted_) {
            persistGeometry(miniWindow_, QStringLiteral("windows/miniGeometry"));
        }
    }
    persistListWindowState();
    if (!isMinimized(audioToolsWindow_) && !isMaximized(audioToolsWindow_)) {
        persistGeometry(audioToolsWindow_, QStringLiteral("windows/audioToolsGeometry"));
    }
    if (!isMinimized(settingsWindow_) && !isMaximized(settingsWindow_)) {
        persistGeometry(settingsWindow_, QStringLiteral("windows/settingsGeometry"));
    }
    settings_.sync();
}

void WindowController::toggleMainWindowGroup()
{
    if (mainWindow_ == nullptr || isMinimized(mainWindow_)
        || !mainWindow_->isVisible()) {
        showMain();
        return;
    }

    const bool groupIsActive = mainWindow_->isActive()
        || (!listWindowDetached_ && listWindow_ != nullptr
            && listWindow_->isActive());
    if (!groupIsActive) {
        showMain();
        return;
    }

    mainWindow_->setWindowState(Qt::WindowMinimized);
    applyListWindowVisible(false);
}

QString WindowController::mainWindowGeometryKey() const
{
    return mainWindowShellMode_ == 1
        ? QStringLiteral("windows/integratedMainGeometry")
        : mainWindowShellMode_ == 2 ? QStringLiteral("windows/rollingMainGeometry")
                                    : QStringLiteral("windows/mainGeometry");
}

QString WindowController::listWindowGeometryKey() const
{
    return QStringLiteral("windows/listGeometry");
}

QString WindowController::listWindowGeometryVersionKey() const
{
    return QStringLiteral("windows/listGeometryVersion");
}

QString WindowController::listWindowRequestedVisibleKey() const
{
    return QStringLiteral("windows/listRequestedVisible");
}

QString WindowController::listWindowDockEdgeKey() const
{
    return QStringLiteral("windows/listDockEdge");
}

bool WindowController::restoreMainWindowGeometry(QWindow* window,
                                                 bool applyClassicDefault)
{
    if (restoreGeometry(window, mainWindowGeometryKey())) {
        return true;
    }
    if (window == nullptr) {
        return false;
    }
    if (mainWindowShellMode_ == 0 && !applyClassicDefault) {
        return false;
    }
    QScreen* screen = window->screen();
    if (screen == nullptr) {
        screen = QGuiApplication::primaryScreen();
    }
    if (screen == nullptr) {
        return false;
    }
    const QRect available = screen->availableGeometry();
    const QSize preferred = mainWindowShellMode_ == 1
        ? QSize(1386, 832)
        : mainWindowShellMode_ == 2 ? QSize(1386, 972) : QSize(863, 266);
    const QSize size = preferred.boundedTo(available.size());
    QRect geometry(QPoint(), size);
    geometry.moveCenter(available.center());
    window->setGeometry(geometry);
    return true;
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

QRect WindowController::geometryForDpiChange(
    const QRect& currentNativeGeometry, qreal currentDpr,
    const QRect& suggestedNativeGeometry, qreal targetDpr,
    const QRect& targetAvailableGeometry)
{
    if (!currentNativeGeometry.isValid() || !suggestedNativeGeometry.isValid()) {
        return suggestedNativeGeometry;
    }
    currentDpr = qMax(currentDpr, 0.01);
    targetDpr = qMax(targetDpr, 0.01);
    const QSize logicalSize(
        qMax(1, qRound(currentNativeGeometry.width() / currentDpr)),
        qMax(1, qRound(currentNativeGeometry.height() / currentDpr)));
    const QSize targetNativeSize(
        qMax(1, qRound(logicalSize.width() * targetDpr)),
        qMax(1, qRound(logicalSize.height() * targetDpr)));
    QRect adjusted(suggestedNativeGeometry.topLeft(), targetNativeSize);
    if (!targetAvailableGeometry.isValid()) return adjusted;

    adjusted.setSize(QSize(qMin(adjusted.width(), targetAvailableGeometry.width()),
                           qMin(adjusted.height(), targetAvailableGeometry.height())));
    const int maxX = targetAvailableGeometry.right() - adjusted.width() + 1;
    const int maxY = targetAvailableGeometry.bottom() - adjusted.height() + 1;
    adjusted.moveLeft(qBound(targetAvailableGeometry.left(), adjusted.left(), maxX));
    adjusted.moveTop(qBound(targetAvailableGeometry.top(), adjusted.top(), maxY));
    return adjusted;
}

QRect WindowController::geometryForAvailableScreens(
    const QRect& savedGeometry, const QSize& minimumSize,
    const QList<QRect>& availableScreens, int primaryScreenIndex)
{
    QRect geometry = savedGeometry;
    geometry.setWidth(qMax(geometry.width(), minimumSize.width()));
    geometry.setHeight(qMax(geometry.height(), minimumSize.height()));

    QRect virtualAvailable;
    int bestOverlapIndex = -1;
    qint64 bestOverlapArea = 0;
    int nearestIndex = -1;
    qint64 nearestDistance = (std::numeric_limits<qint64>::max)();
    for (int index = 0; index < availableScreens.size(); ++index) {
        const QRect screen = availableScreens.at(index);
        if (!screen.isValid()) continue;
        virtualAvailable = virtualAvailable.united(screen);
        const QRect intersection = screen.intersected(geometry);
        const qint64 area = static_cast<qint64>(intersection.width())
            * intersection.height();
        if (area > bestOverlapArea) {
            bestOverlapArea = area;
            bestOverlapIndex = index;
        }
        const qint64 dx = geometry.right() < screen.left()
            ? static_cast<qint64>(screen.left()) - geometry.right()
            : screen.right() < geometry.left()
                ? static_cast<qint64>(geometry.left()) - screen.right() : 0;
        const qint64 dy = geometry.bottom() < screen.top()
            ? static_cast<qint64>(screen.top()) - geometry.bottom()
            : screen.bottom() < geometry.top()
                ? static_cast<qint64>(geometry.top()) - screen.bottom() : 0;
        const qint64 distance = dx * dx + dy * dy;
        if (distance < nearestDistance
            || (distance == nearestDistance && index == primaryScreenIndex)) {
            nearestDistance = distance;
            nearestIndex = index;
        }
    }
    if (!virtualAvailable.isValid()) return geometry;

    geometry.setWidth(qMin(geometry.width(), virtualAvailable.width()));
    geometry.setHeight(qMin(geometry.height(), virtualAvailable.height()));
    const int virtualMaxX = virtualAvailable.right() - geometry.width() + 1;
    const int virtualMaxY = virtualAvailable.bottom() - geometry.height() + 1;
    geometry.moveLeft(qBound(virtualAvailable.left(), geometry.left(), virtualMaxX));
    geometry.moveTop(qBound(virtualAvailable.top(), geometry.top(), virtualMaxY));
    for (const QRect& screen : availableScreens) {
        if (screen.isValid() && screen.intersects(geometry)) return geometry;
    }

    int targetIndex = bestOverlapIndex >= 0 ? bestOverlapIndex : nearestIndex;
    if (targetIndex < 0 && primaryScreenIndex >= 0
        && primaryScreenIndex < availableScreens.size()
        && availableScreens.at(primaryScreenIndex).isValid()) {
        targetIndex = primaryScreenIndex;
    }
    if (targetIndex < 0) return geometry;
    const QRect target = availableScreens.at(targetIndex);
    geometry.setWidth(qMin(geometry.width(), target.width()));
    geometry.setHeight(qMin(geometry.height(), target.height()));
    geometry.moveLeft(qBound(target.left(), geometry.left(),
                             target.right() - geometry.width() + 1));
    geometry.moveTop(qBound(target.top(), geometry.top(),
                            target.bottom() - geometry.height() + 1));
    return geometry;
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
        if (event->type() == QEvent::PlatformSurface) {
#ifdef Q_OS_WIN
            const auto* surfaceEvent =
                static_cast<QPlatformSurfaceEvent*>(event);
            if (surfaceEvent->surfaceEventType()
                == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed) {
                mainWindowHandle_ = 0;
            } else if (surfaceEvent->surfaceEventType()
                       == QPlatformSurfaceEvent::SurfaceCreated
                       && QGuiApplication::platformName().compare(
                              QStringLiteral("windows"),
                              Qt::CaseInsensitive) == 0) {
                // SurfaceCreated guarantees that winId() reads an existing
                // HWND instead of recursively creating another surface.
                mainWindowHandle_ =
                    static_cast<quintptr>(mainWindow_->winId());
                rememberNativePixelSize(mainWindow_);
                applyPlatformWindowStyle(mainWindow_);
            }
#endif
        } else if (event->type() == QEvent::Move
                   || event->type() == QEvent::Resize) {
#ifdef Q_OS_WIN
            if (event->type() == QEvent::Resize
                && qFuzzyCompare(mainTrackedDpr_, mainWindow_->devicePixelRatio())) {
                rememberNativePixelSize(mainWindow_);
            }
#endif
            const bool isDeferredImmersiveMainRestore =
                !immersiveRestoreMainGeometryWasPersisted_
                && !immersivePresentationActive_
                && mainWindow_->geometry() == immersiveRestoreMainGeometry_;
            if (!isDeferredImmersiveMainRestore) {
                if (!immersivePresentationActive_
                    && mainWindowGeometryKey()
                        == immersiveRestoreMainGeometryKey_) {
                    immersiveRestoreMainGeometryWasPersisted_ = true;
                }
                scheduleWindowStateSync();
            }
            if (!updatingWindowGeometry_ && !listWindowDetached_) {
                repositionDockedListWindow();
            }
            emit mainWindowGeometryChanged();
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
        const bool isDeferredImmersiveMiniRestore =
            !immersiveRestoreMiniGeometryWasPersisted_
            && !immersivePresentationActive_
            && miniWindow_->geometry() == immersiveRestoreMiniGeometry_;
        if (!isDeferredImmersiveMiniRestore) {
            if (!immersivePresentationActive_) {
                immersiveRestoreMiniGeometryWasPersisted_ = true;
            }
            scheduleWindowStateSync();
        }
    } else if ((watched == audioToolsWindow_ || watched == settingsWindow_)
               && (event->type() == QEvent::Move
                   || event->type() == QEvent::Resize)) {
#ifdef Q_OS_WIN
        if (event->type() == QEvent::Resize) {
            auto* const window = qobject_cast<QWindow*>(watched);
            if (window != nullptr && watched == settingsWindow_
                && qFuzzyCompare(settingsTrackedDpr_, window->devicePixelRatio())) {
                rememberNativePixelSize(window);
            }
        }
#endif
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
        const bool listMoveMayChangeDock =
            QGuiApplication::platformName().compare(
                QStringLiteral("windows"), Qt::CaseInsensitive) != 0
            || listUserInteraction_;
        if (!updatingWindowGeometry_ && listMoveMayChangeDock && magneticSnapEnabled_
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
