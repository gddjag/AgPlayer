#include "window_controller.hpp"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QSettings>
#include <QStandardPaths>
#include <QTest>
#include <QWindow>

#include <vector>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

class WindowControllerTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void defaultListSizeMatchesReference();
    void dpiChangePreservesNativePixelSize();
    void switchingWindowsDoesNotRecreatePlayback();
    void updatesExistingWindowObjectsAndFlags();
    void visibilityWaitsForDestinationReadiness();
    void shutdownIsOrderedAndIdempotent();
    void missingShutdownCollaboratorsRemainIdempotent();
    void listWindowVisibilityCanBeToggled();
    void listAvailabilityDoesNotOverwriteUserVisibilityRequest();
    void listWindowMagneticSnappingToMainWindowEdges();
    void listWindowExplicitSnapToEachEdge();
    void listWindowDetachesOutsideSnapThreshold();
    void listWindowDoesNotSnapWithoutProjectionOverlap();
    void nativeMoveEventsSnapAndUseReleaseHysteresis();
    void toolWindowsShareMainTransientLayering();
    void auxiliaryWindowsOpenCenteredOverMain();
#ifdef Q_OS_WIN
    void auxiliaryWindowRemainsAboveDockedPlayerGroup();
#endif
    void dockedListFollowsMainWindow();
    void firstAttachedListAlignsWithMainWindow();
    void dockedGroupDoesNotClampMainMoveAtScreenEdge();
    void horizontalDockPreservesSizesAndKeepsWindowsAdjacent();
#ifdef Q_OS_WIN
    void dockedWindowsKeepNativeSizeAcrossScreens();
#endif
    void mainMinimizeRestoresOnlyRequestedList();
    void showMainRestoresAndRaisesTheExistingWindowGroup();
    void mainMaximizeHidesOnlyDockedList();
    void geometryDockAndPinStatePersist();
    void legacyMiniGeometryMigratesToReferenceDefault();
    void persistedDockEdgeSurvivesInitialPreferenceWiring();
    void restoredGeometryBalancesMinimumAndAvailableScreen();
    void offscreenGeometryRestoresInsideAvailableScreen();
    void firstRunGeometryCentersOnPrimaryScreen();
    void closeBehaviorChoosesTrayOrOrderedShutdown();
};

void WindowControllerTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("AgPlayer"));
    QCoreApplication::setApplicationName(QStringLiteral("AgPlayer-window-controller-test"));
}

void WindowControllerTest::init()
{
    QSettings settings;
    settings.clear();
    settings.setValue(QStringLiteral("windows/listRequestedVisible"), false);
    settings.sync();
}

void WindowControllerTest::defaultListSizeMatchesReference()
{
    WindowController windows;
    QCOMPARE(windows.listWindowWidth(), 1104);
    QCOMPARE(windows.listWindowHeight(), 570);
}

void WindowControllerTest::dpiChangePreservesNativePixelSize()
{
    const QRect currentGeometry(120, 80, 1104, 342);
    const QRect windowsSuggestedGeometry(1920, 120, 1656, 513);

    QCOMPARE(WindowController::geometryForDpiChange(
                 currentGeometry, windowsSuggestedGeometry),
             QRect(1920, 120, 1104, 342));
}

void WindowControllerTest::switchingWindowsDoesNotRecreatePlayback()
{
    WindowController windows;
    windows.showMini();
    QVERIFY(windows.miniVisible());
    QVERIFY(!windows.mainVisible());
    windows.showMain();
    QVERIFY(windows.mainVisible());
    QVERIFY(!windows.miniVisible());
}

void WindowControllerTest::updatesExistingWindowObjectsAndFlags()
{
    QWindow mainWindow;
    QWindow miniWindow;
    QWindow* const originalMiniWindow = &miniWindow;
    WindowController windows;
    windows.setWindows(&mainWindow, &miniWindow);
    QVERIFY(mainWindow.isVisible());
    QVERIFY(!miniWindow.isVisible());

    windows.showMini();
    QVERIFY(!mainWindow.isVisible());
    QVERIFY(miniWindow.isVisible());
    windows.setAlwaysOnTop(true);
    QCOMPARE(&miniWindow, originalMiniWindow);
    QVERIFY(miniWindow.flags().testFlag(Qt::WindowStaysOnTopHint));
    windows.setAlwaysOnTop(false);
    QVERIFY(!miniWindow.flags().testFlag(Qt::WindowStaysOnTopHint));

    windows.showMain();
    QVERIFY(mainWindow.isVisible());
    QVERIFY(!miniWindow.isVisible());
}

void WindowControllerTest::visibilityWaitsForDestinationReadiness()
{
    WindowController windows;
    windows.setMiniReady(false);
    windows.showMini();
    QVERIFY(windows.mainVisible());
    QVERIFY(!windows.miniVisible());

    windows.setMiniReady(true);
    QVERIFY(!windows.mainVisible());
    QVERIFY(windows.miniVisible());

    windows.setMainReady(false);
    windows.showMain();
    QVERIFY(!windows.mainVisible());
    QVERIFY(windows.miniVisible());

    windows.setMainReady(true);
    QVERIFY(windows.mainVisible());
    QVERIFY(!windows.miniVisible());
}

void WindowControllerTest::shutdownIsOrderedAndIdempotent()
{
    std::vector<int> calls;
    WindowController::ShutdownActions actions;
    actions.cancelWaveform = [&calls] { calls.push_back(1); };
    actions.stopPlayback = [&calls] { calls.push_back(2); };
    actions.flushLibrary = [&calls] { calls.push_back(3); };
    actions.releaseCore = [&calls] { calls.push_back(4); };
    actions.quitApplication = [&calls] { calls.push_back(5); };
    WindowController windows(std::move(actions));
    windows.setCloseBehavior(1);

    windows.requestClose();
    windows.requestClose();

    QCOMPARE(calls, std::vector<int>({1, 2, 3, 4, 5}));
}

void WindowControllerTest::missingShutdownCollaboratorsRemainIdempotent()
{
    int quitCalls = 0;
    WindowController::ShutdownActions actions;
    actions.quitApplication = [&quitCalls] { ++quitCalls; };
    WindowController windows(std::move(actions));
    windows.setCloseBehavior(1);
    windows.requestClose();
    windows.requestClose();
    QCOMPARE(quitCalls, 1);
}

void WindowControllerTest::listWindowVisibilityCanBeToggled()
{
    QWindow mainWindow;
    QWindow listWindow;
    WindowController windows;
    windows.setWindows(&mainWindow, nullptr);
    windows.setListWindow(&listWindow);

    QVERIFY(!windows.listWindowDetached());
    QVERIFY(!windows.listWindowVisible());
    QVERIFY(!listWindow.isVisible());

    windows.showListWindow();
    QVERIFY(!windows.listWindowDetached());
    QVERIFY(windows.listWindowVisible());
    QVERIFY(listWindow.isVisible());

    windows.hideListWindow();
    QVERIFY(!windows.listWindowDetached());
    QVERIFY(!windows.listWindowVisible());
    QVERIFY(!listWindow.isVisible());

    windows.showListWindow();
    QVERIFY(!windows.listWindowDetached());
    QVERIFY(windows.listWindowVisible());
    QVERIFY(listWindow.isVisible());

    windows.toggleListWindow();
    QVERIFY(!windows.listWindowDetached());
    QVERIFY(!windows.listWindowVisible());
    QVERIFY(!listWindow.isVisible());
}

void WindowControllerTest::listWindowMagneticSnappingToMainWindowEdges()
{
    QWindow mainWindow;
    mainWindow.setGeometry(100, 100, 400, 300);

    QWindow listWindow;
    listWindow.setGeometry(0, 0, 200, 150);

    WindowController windows;
    windows.setWindows(&mainWindow, nullptr);
    windows.setListWindow(&listWindow);
    windows.setListWindowDetached(true);

    // Move the left edge of the list window within 15 px of the main window's
    // right edge. It was previously bottom-docked, so it inherits the shared
    // player width before becoming a side dock.
    const int expectedRightX = mainWindow.geometry().right() - 1;
    const int expectedCenterY = mainWindow.geometry().y()
        + (mainWindow.geometry().height() - listWindow.height()) / 2;

    windows.moveListWindow(expectedRightX - 15, expectedCenterY + 50);
    QCOMPARE(listWindow.x(), mainWindow.geometry().right() - 1);
    QCOMPARE(listWindow.size(), QSize(400, 150));
    QCOMPARE(windows.listWindowX(), listWindow.x());
    QCOMPARE(windows.listWindowY(), listWindow.y());
    QVERIFY(!windows.listWindowDetached());
    QCOMPARE(windows.listDockEdge(), QStringLiteral("right"));
}

void WindowControllerTest::listWindowExplicitSnapToEachEdge()
{
    QWindow mainWindow;
    mainWindow.setGeometry(250, 200, 300, 250);

    QWindow listWindow;
    listWindow.setGeometry(0, 0, 200, 150);

    WindowController windows;
    windows.setWindows(&mainWindow, nullptr);
    windows.setListWindow(&listWindow);

    windows.snapListWindow("left");
    QCOMPARE(listWindow.geometry().right(), mainWindow.geometry().left() + 3);
    QCOMPARE(listWindow.size(), QSize(300, 150));

    windows.snapListWindow("right");
    QCOMPARE(listWindow.x(), mainWindow.geometry().right() - 1);
    QCOMPARE(listWindow.size(), QSize(300, 150));

    windows.snapListWindow("top");
    QCOMPARE(listWindow.geometry().bottom(), mainWindow.geometry().top() + 1);
    QCOMPARE(listWindow.size(), QSize(300, 150));

    windows.snapListWindow("bottom");
    QCOMPARE(listWindow.y(), mainWindow.geometry().bottom() - 1);
    QCOMPARE(listWindow.size(), QSize(300, 150));
    QCOMPARE(windows.listDockEdge(), QStringLiteral("bottom"));
}

void WindowControllerTest::listAvailabilityDoesNotOverwriteUserVisibilityRequest()
{
    QSettings settings;
    settings.remove(QStringLiteral("windows/listRequestedVisible"));
    settings.sync();

    QWindow mainWindow;
    QWindow listWindow;
    WindowController windows;
    windows.setListWindowPanelAllowed(false);
    windows.setWindows(&mainWindow, nullptr);
    windows.setListWindow(&listWindow);
    QVERIFY(!listWindow.isVisible());

    windows.setListWindowPanelAllowed(true);
    QVERIFY(listWindow.isVisible());
    windows.hideListWindow();
    windows.setListWindowPanelAllowed(false);
    windows.setListWindowPanelAllowed(true);
    QVERIFY(!listWindow.isVisible());
}

void WindowControllerTest::listWindowDetachesOutsideSnapThreshold()
{
    QWindow mainWindow;
    mainWindow.setGeometry(100, 100, 400, 300);
    QWindow listWindow;
    listWindow.setGeometry(0, 0, 200, 150);

    WindowController windows;
    windows.setWindows(&mainWindow, nullptr);
    windows.setListWindow(&listWindow);

    const int rightX = mainWindow.geometry().right();
    const int centerY = mainWindow.geometry().y()
        + (mainWindow.geometry().height() - listWindow.height()) / 2;
    windows.moveListWindow(rightX + 16, centerY);
    QCOMPARE(listWindow.position(), QPoint(rightX + 16, centerY));
    QVERIFY(windows.listWindowDetached());
    QCOMPARE(windows.listDockEdge(), QStringLiteral("none"));

    windows.setMagneticSnapEnabled(false);
    windows.moveListWindow(rightX, centerY);
    QCOMPARE(listWindow.position(), QPoint(rightX, centerY));
    QVERIFY(windows.listWindowDetached());
}

void WindowControllerTest::nativeMoveEventsSnapAndUseReleaseHysteresis()
{
    QWindow mainWindow;
    mainWindow.setGeometry(100, 100, 400, 300);
    QWindow listWindow;
    listWindow.setGeometry(700, 100, 220, 180);

    WindowController windows;
    windows.setWindows(&mainWindow, nullptr);
    windows.setListWindow(&listWindow);
    windows.showListWindow();
    windows.setListWindowDetached(true);

    const QPoint requestedDock(mainWindow.geometry().right(),
                               mainWindow.geometry().top());
    listWindow.setPosition(requestedDock + QPoint(12, 0));
    QCoreApplication::processEvents();
    QVERIFY(windows.listWindowDetached());
    QCOMPARE(windows.snapPreviewEdge(), QStringLiteral("right"));
    windows.finishListWindowInteraction();
    QVERIFY(!windows.listWindowDetached());
    QCOMPARE(windows.snapPreviewEdge(), QStringLiteral("none"));
    QCOMPARE(listWindow.x(), mainWindow.geometry().right() - 1);
    const QPoint docked = listWindow.position();

    listWindow.setPosition(docked + QPoint(20, 0));
    QCoreApplication::processEvents();
    QVERIFY(!windows.listWindowDetached());
    windows.finishListWindowInteraction();
    QCOMPARE(listWindow.position(), docked);

    listWindow.setPosition(docked + QPoint(25, 0));
    QCoreApplication::processEvents();
    QVERIFY(!windows.listWindowDetached());
    windows.finishListWindowInteraction();
    QVERIFY(windows.listWindowDetached());
    QCOMPARE(listWindow.position(), docked + QPoint(25, 0));
    QVERIFY(windows.listWindowVisible());
    QVERIFY(listWindow.isVisible());
}

void WindowControllerTest::toolWindowsShareMainTransientLayering()
{
    QWindow mainWindow;
    QWindow listWindow;
    QWindow toolsWindow;
    QWindow settingsWindow;
    WindowController windows;
    windows.setWindows(&mainWindow, nullptr);
    windows.setListWindow(&listWindow);
    windows.setAudioToolsWindow(&toolsWindow);
    windows.registerSettingsWindow(&settingsWindow);

    // Windows ownership keeps the docked list visible/minimized/restored with
    // the player. Batch Z-ordering still places settings and tools above both.
    QCOMPARE(listWindow.transientParent(), &mainWindow);
    QCOMPARE(toolsWindow.transientParent(), &mainWindow);
    QCOMPARE(settingsWindow.transientParent(), &mainWindow);
}

void WindowControllerTest::auxiliaryWindowsOpenCenteredOverMain()
{
    QWindow mainWindow;
    mainWindow.setGeometry(220, 140, 640, 360);
    QWindow toolsWindow;
    toolsWindow.setGeometry(10, 10, 360, 240);
    QWindow settingsWindow;
    settingsWindow.setGeometry(20, 20, 420, 300);

    WindowController windows;
    windows.setWindows(&mainWindow, nullptr);
    windows.setAudioToolsWindow(&toolsWindow);
    windows.registerSettingsWindow(&settingsWindow);

    windows.showAudioTools();
    QCOMPARE(toolsWindow.geometry().center(), mainWindow.geometry().center());
    QVERIFY(toolsWindow.isVisible());

    windows.presentAuxiliaryWindow(&settingsWindow);
    QCOMPARE(settingsWindow.geometry().center(), mainWindow.geometry().center());
    QVERIFY(settingsWindow.isVisible());
}

#ifdef Q_OS_WIN
void WindowControllerTest::auxiliaryWindowRemainsAboveDockedPlayerGroup()
{
    if (QGuiApplication::platformName().compare(QStringLiteral("windows"),
                                                Qt::CaseInsensitive) != 0) {
        QSKIP("requires the Windows native z-order stack");
    }

    QWindow mainWindow;
    mainWindow.setGeometry(120, 120, 640, 360);
    QWindow listWindow;
    listWindow.setGeometry(120, 478, 640, 300);
    QWindow toolsWindow;
    toolsWindow.setGeometry(260, 190, 480, 300);

    WindowController windows;
    windows.setWindows(&mainWindow, nullptr);
    windows.setListWindow(&listWindow);
    windows.setAudioToolsWindow(&toolsWindow);
    windows.showListWindow();
    windows.snapListWindow(QStringLiteral("bottom"));
    windows.showAudioTools();

    QVERIFY(QTest::qWaitForWindowExposed(&mainWindow));
    QVERIFY(QTest::qWaitForWindowExposed(&listWindow));
    QVERIFY(QTest::qWaitForWindowExposed(&toolsWindow));

    const HWND mainHandle = reinterpret_cast<HWND>(mainWindow.winId());
    const HWND listHandle = reinterpret_cast<HWND>(listWindow.winId());
    const HWND toolsHandle = reinterpret_cast<HWND>(toolsWindow.winId());
    QVERIFY(IsWindow(mainHandle));
    QVERIFY(IsWindow(listHandle));
    QVERIFY(IsWindow(toolsHandle));

    const auto isAbove = [](const HWND candidate, const HWND below) {
        for (HWND current = GetWindow(below, GW_HWNDPREV);
             current != nullptr;
             current = GetWindow(current, GW_HWNDPREV)) {
            if (current == candidate) return true;
        }
        return false;
    };

    QVERIFY2(isAbove(toolsHandle, mainHandle),
             "audio tools must remain above the player window");
    QVERIFY2(isAbove(toolsHandle, listHandle),
             "audio tools must remain above the docked list window");
}
#endif

void WindowControllerTest::listWindowDoesNotSnapWithoutProjectionOverlap()
{
    QWindow mainWindow;
    mainWindow.setGeometry(100, 100, 400, 300);
    QWindow listWindow;
    listWindow.setGeometry(0, 0, 200, 150);

    WindowController windows;
    windows.setWindows(&mainWindow, nullptr);
    windows.setListWindow(&listWindow);
    windows.setListWindowDetached(true);

    const int rightX = mainWindow.geometry().right();
    windows.moveListWindow(rightX, 1200);
    QCOMPARE(listWindow.position(), QPoint(rightX, 1200));
    QVERIFY(windows.listWindowDetached());
    QCOMPARE(windows.listDockEdge(), QStringLiteral("none"));
}

void WindowControllerTest::dockedListFollowsMainWindow()
{
    QWindow mainWindow;
    mainWindow.setGeometry(200, 150, 400, 300);
    QWindow listWindow;
    listWindow.setGeometry(0, 0, 240, 160);

    WindowController windows;
    windows.setWindows(&mainWindow, nullptr);
    windows.setListWindow(&listWindow);
    windows.snapListWindow(QStringLiteral("bottom"));
    mainWindow.setPosition(120, 260);
    QCoreApplication::processEvents();

    const QRect mainGeo = mainWindow.geometry();
    QCOMPARE(listWindow.x(), mainGeo.x());
    QCOMPARE(listWindow.y(), mainGeo.bottom() - 1);
    QCOMPARE(listWindow.width(), mainGeo.width());
}

void WindowControllerTest::firstAttachedListAlignsWithMainWindow()
{
    QWindow mainWindow;
    mainWindow.setGeometry(200, 150, 960, 298);
    QWindow listWindow;
    listWindow.setGeometry(50, 500, 1655, 570);

    WindowController windows;
    windows.setWindows(&mainWindow, nullptr);
    windows.setListWindow(&listWindow);

    QCOMPARE(windows.listDockEdge(), QStringLiteral("bottom"));
    QCOMPARE(listWindow.x(), mainWindow.x());
    QCOMPARE(listWindow.y(), mainWindow.geometry().bottom() - 1);
    QCOMPARE(listWindow.width(), mainWindow.width());
    QCOMPARE(listWindow.height(), 570);
}

void WindowControllerTest::dockedGroupDoesNotClampMainMoveAtScreenEdge()
{
    QWindow mainWindow;
    mainWindow.setMinimumSize(QSize(240, 120));
    mainWindow.setGeometry(200, 150, 420, 220);
    QWindow listWindow;
    listWindow.setMinimumSize(QSize(240, 120));
    listWindow.setGeometry(0, 0, 420, 180);

    WindowController windows;
    windows.setWindows(&mainWindow, nullptr);
    windows.setListWindow(&listWindow);
    windows.snapListWindow(QStringLiteral("right"));

    const QRect available = mainWindow.screen()->availableGeometry();
    const QPoint requested(available.right() - mainWindow.width() + 1,
                           available.top() + 80);
    mainWindow.setPosition(requested);
    QCoreApplication::processEvents();

    QCOMPARE(mainWindow.position(), requested);
    QCOMPARE(listWindow.x(), mainWindow.geometry().right() - 1);
    QCOMPARE(listWindow.y(), mainWindow.y());
}

void WindowControllerTest::horizontalDockPreservesSizesAndKeepsWindowsAdjacent()
{
    QWindow mainWindow;
    mainWindow.setFlags(Qt::FramelessWindowHint);
    mainWindow.setMinimumSize(QSize(300, 200));
    mainWindow.setGeometry(10, 80, 450, 240);
    QWindow listWindow;
    listWindow.setFlags(Qt::FramelessWindowHint);
    listWindow.setMinimumSize(QSize(300, 180));
    listWindow.setGeometry(0, 0, 450, 220);
    const QSize mainSize = mainWindow.size();
    const QSize listSize = listWindow.size();

    WindowController windows;
    windows.setWindows(&mainWindow, nullptr);
    windows.setListWindow(&listWindow);
    windows.snapListWindow(QStringLiteral("right"));

    QCOMPARE(listWindow.x(), mainWindow.geometry().right() - 1);
    QCOMPARE(listWindow.y(), mainWindow.y());
    QCOMPARE(mainWindow.size(), mainSize);
    QCOMPARE(listWindow.size(), listSize);
    // A docked player/list pair can straddle a monitor seam.  Keeping both
    // windows at the user-selected size is more important than squeezing the
    // group back into one screen while it is being moved.
    QVERIFY(mainWindow.geometry().intersects(listWindow.geometry())
            || mainWindow.geometry().adjusted(-1, -1, 1, 1)
                   .intersects(listWindow.geometry()));
    QVERIFY(mainWindow.width() >= mainWindow.minimumWidth());
    QVERIFY(listWindow.width() >= listWindow.minimumWidth());
}

#ifdef Q_OS_WIN
void WindowControllerTest::dockedWindowsKeepNativeSizeAcrossScreens()
{
    const QList<QScreen*> screens = QGuiApplication::screens();
    if (screens.size() < 2) {
        QSKIP("A second monitor is required for the cross-screen regression");
    }

    QWindow mainWindow;
    mainWindow.setFlags(Qt::FramelessWindowHint);
    mainWindow.setMinimumSize(QSize(612, 228));
    mainWindow.setGeometry(QRect(screens.at(0)->availableGeometry().topLeft()
                                     + QPoint(80, 80),
                                 QSize(1104, 342)));
    QWindow listWindow;
    listWindow.setFlags(Qt::FramelessWindowHint);
    listWindow.setMinimumSize(QSize(720, 320));
    listWindow.setGeometry(QRect(QPoint(), QSize(1228, 570)));

    WindowController windows;
    windows.setWindows(&mainWindow, nullptr);
    windows.setListWindow(&listWindow);
    windows.showListWindow();
    windows.snapListWindow(QStringLiteral("bottom"));
    QTRY_VERIFY(mainWindow.winId() != 0 && listWindow.winId() != 0);

    RECT initialMain{};
    RECT initialList{};
    QVERIFY(GetWindowRect(reinterpret_cast<HWND>(mainWindow.winId()), &initialMain));
    QVERIFY(GetWindowRect(reinterpret_cast<HWND>(listWindow.winId()), &initialList));
    const QSize mainNativeSize(initialMain.right - initialMain.left,
                               initialMain.bottom - initialMain.top);
    const QSize listNativeSize(initialList.right - initialList.left,
                               initialList.bottom - initialList.top);

    mainWindow.setPosition(screens.at(1)->availableGeometry().topLeft()
                           + QPoint(80, 80));
    QTRY_VERIFY(mainWindow.screen() == screens.at(1));
    QTRY_COMPARE(listWindow.x(), mainWindow.x());
    QTRY_COMPARE(listWindow.y(), mainWindow.geometry().bottom() - 1);

    const auto nativeSize = [](QWindow& window) {
        RECT rect{};
        if (!GetWindowRect(reinterpret_cast<HWND>(window.winId()), &rect)) {
            return QSize();
        }
        return QSize(rect.right - rect.left, rect.bottom - rect.top);
    };
    QTRY_COMPARE(nativeSize(mainWindow), mainNativeSize);
    QTRY_COMPARE(nativeSize(listWindow), listNativeSize);

    mainWindow.setPosition(screens.at(0)->availableGeometry().topLeft()
                           + QPoint(120, 120));
    QTRY_VERIFY(mainWindow.screen() == screens.at(0));
    QTRY_COMPARE(listWindow.x(), mainWindow.x());
    QTRY_COMPARE(listWindow.y(), mainWindow.geometry().bottom() - 1);
    QTRY_COMPARE(nativeSize(mainWindow), mainNativeSize);
    QTRY_COMPARE(nativeSize(listWindow), listNativeSize);
}
#endif

void WindowControllerTest::mainMinimizeRestoresOnlyRequestedList()
{
    QWindow mainWindow;
    QWindow listWindow;
    WindowController windows;
    windows.setWindows(&mainWindow, nullptr);
    windows.setListWindow(&listWindow);
    windows.showListWindow();

    mainWindow.setWindowState(Qt::WindowMinimized);
    QTRY_VERIFY(!listWindow.isVisible());
    mainWindow.setWindowState(Qt::WindowNoState);
    QTRY_VERIFY(listWindow.isVisible());

    windows.hideListWindow();
    mainWindow.setWindowState(Qt::WindowMinimized);
    QCoreApplication::processEvents();
    mainWindow.setWindowState(Qt::WindowNoState);
    QTRY_VERIFY(!listWindow.isVisible());
}

void WindowControllerTest::showMainRestoresAndRaisesTheExistingWindowGroup()
{
    QWindow mainWindow;
    QWindow listWindow;
    WindowController windows;
    windows.setWindows(&mainWindow, nullptr);
    windows.setListWindow(&listWindow);
    windows.showListWindow();

    mainWindow.setWindowState(Qt::WindowMinimized);
    QCoreApplication::processEvents();
    windows.showMain();
    QCoreApplication::processEvents();

    QVERIFY(mainWindow.isVisible());
    QVERIFY(mainWindow.windowState() != Qt::WindowMinimized);
    QVERIFY(listWindow.isVisible());
}

void WindowControllerTest::mainMaximizeHidesOnlyDockedList()
{
    QWindow mainWindow;
    mainWindow.setFlags(Qt::FramelessWindowHint);
    mainWindow.setGeometry(80, 80, 400, 260);
    QWindow listWindow;
    listWindow.setFlags(Qt::FramelessWindowHint);
    listWindow.setGeometry(0, 0, 360, 220);
    WindowController windows;
    windows.setWindows(&mainWindow, nullptr);
    windows.setListWindow(&listWindow);
    windows.showListWindow();
    windows.snapListWindow(QStringLiteral("bottom"));

    mainWindow.setWindowState(Qt::WindowMaximized);
    QCoreApplication::processEvents();
    QVERIFY(!listWindow.isVisible());
    mainWindow.setWindowState(Qt::WindowNoState);
    QCoreApplication::processEvents();
    QVERIFY(listWindow.isVisible());

    windows.setListWindowDetached(true);
    mainWindow.setWindowState(Qt::WindowMaximized);
    QCoreApplication::processEvents();
    QVERIFY(listWindow.isVisible());
}

void WindowControllerTest::geometryDockAndPinStatePersist()
{
    {
        QWindow mainWindow;
        mainWindow.setGeometry(450, 160, 300, 220);
        QWindow miniWindow;
        miniWindow.setFlags(Qt::FramelessWindowHint);
        miniWindow.setGeometry(50, 220, 700, 300);
        QWindow listWindow;
        listWindow.setGeometry(0, 0, 400, 300);

        WindowController windows;
        windows.setWindows(&mainWindow, &miniWindow);
        windows.setListWindow(&listWindow);
        windows.snapListWindow(QStringLiteral("left"));
        windows.setAlwaysOnTop(true);
        QCoreApplication::processEvents();
    }
    QSettings().sync();

    QWindow restoredMain;
    QWindow restoredMini;
    restoredMini.setFlags(Qt::FramelessWindowHint);
    restoredMini.setGeometry(50, 220, 700, 300);
    QWindow restoredList;
    WindowController restored;
    restored.setWindows(&restoredMain, &restoredMini);
    restored.setListWindow(&restoredList);

    QCOMPARE(restoredMain.geometry(), QRect(450, 160, 300, 220));
    QCOMPARE(restoredMini.geometry(), QRect(50, 220, 700, 300));
    QCOMPARE(restoredList.size(), QSize(300, 300));
    QCOMPARE(restoredList.geometry().right(), restoredMain.geometry().left() + 1);
    QCOMPARE(restored.listDockEdge(), QStringLiteral("left"));
    QVERIFY(restored.alwaysOnTop());
}

void WindowControllerTest::legacyMiniGeometryMigratesToReferenceDefault()
{
    QSettings settings;
    settings.setValue(QStringLiteral("windows/miniGeometry"),
                      QRect(180, 220, 760, 260));
    settings.sync();

    QWindow mainWindow;
    QWindow miniWindow;
    miniWindow.setGeometry(180, 220, 588, 186);
    WindowController windows;
    windows.setWindows(&mainWindow, &miniWindow);

    QCOMPARE(miniWindow.size(), QSize(588, 186));
}

void WindowControllerTest::restoredGeometryBalancesMinimumAndAvailableScreen()
{
    QSettings settings;
    settings.setValue(QStringLiteral("windows/listGeometry"), QRect(20, 30, 720, 320));
    settings.sync();

    QWindow listWindow;
    listWindow.setMinimumSize(QSize(1160, 360));
    WindowController windows;
    windows.setListWindow(&listWindow);

    // Restoring onto a smaller available screen may constrain the width, but
    // must retain the requested height rather than corrupting the geometry.
    QCOMPARE(listWindow.height(), 360);
    QVERIFY(listWindow.width() > 0);
}

void WindowControllerTest::offscreenGeometryRestoresInsideAvailableScreen()
{
    QSettings settings;
    settings.setValue(QStringLiteral("windows/mainGeometry"),
                      QRect(5000, 5000, 620, 360));
    settings.sync();

    QWindow mainWindow;
    WindowController windows;
    windows.setWindows(&mainWindow, nullptr);

    const QRect available = mainWindow.screen()->availableGeometry();
    QVERIFY(available.contains(mainWindow.geometry()));
}

void WindowControllerTest::firstRunGeometryCentersOnPrimaryScreen()
{
    QWindow mainWindow;
    mainWindow.setMinimumSize(QSize(680, 300));
    mainWindow.setGeometry(449, -1746, 1228, 380);

    WindowController windows;
    windows.setWindows(&mainWindow, nullptr);

    QScreen* const primary = QGuiApplication::primaryScreen();
    QVERIFY(primary != nullptr);
    // The offscreen QPA adds a tiny decoration offset, so check visibility and
    // center tolerance rather than demanding an exact client-frame match.
    QVERIFY(primary->availableGeometry().intersects(mainWindow.geometry()));
    QVERIFY(qAbs(mainWindow.geometry().center().x()
                 - primary->availableGeometry().center().x()) <= 2);
    QVERIFY(qAbs(mainWindow.geometry().center().y()
                 - primary->availableGeometry().center().y()) <= 2);
}

void WindowControllerTest::persistedDockEdgeSurvivesInitialPreferenceWiring()
{
    QSettings settings;
    settings.setValue(QStringLiteral("windows/listDockEdge"), QStringLiteral("left"));
    settings.sync();

    QWindow mainWindow;
    mainWindow.setGeometry(250, 200, 300, 250);
    QWindow listWindow;
    listWindow.setGeometry(0, 0, 200, 150);
    WindowController windows;
    windows.setPreferredDockEdge(3);
    windows.setWindows(&mainWindow, nullptr);
    windows.setListWindow(&listWindow);

    QCOMPARE(windows.listDockEdge(), QStringLiteral("left"));
    windows.setPreferredDockEdge(0);
    QCOMPARE(windows.listDockEdge(), QStringLiteral("top"));
}

void WindowControllerTest::closeBehaviorChoosesTrayOrOrderedShutdown()
{
    int trayCalls = 0;
    int quitCalls = 0;
    WindowController::ShutdownActions actions;
    actions.minimizeToTray = [&trayCalls] { ++trayCalls; };
    actions.quitApplication = [&quitCalls] { ++quitCalls; };
    WindowController windows(std::move(actions));
    QWindow audioToolsWindow;
    windows.setAudioToolsWindow(&audioToolsWindow);
    windows.showAudioTools();
    QVERIFY(audioToolsWindow.isVisible());

    windows.setCloseBehavior(0);
    windows.requestClose();
    QCOMPARE(trayCalls, 1);
    QCOMPARE(quitCalls, 0);
    QVERIFY(!windows.audioToolsVisible());
    QVERIFY(!audioToolsWindow.isVisible());

    windows.requestExit();
    QCOMPARE(quitCalls, 1);
    windows.requestExit();
    QCOMPARE(quitCalls, 1);
}

QTEST_MAIN(WindowControllerTest)
#include "window_controller_test.moc"
