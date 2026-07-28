#include "window_controller.hpp"

#include <QCoreApplication>
#include <QScreen>
#include <QSettings>
#include <QStandardPaths>
#include <QTest>
#include <QWindow>

#include <vector>

class WindowControllerTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
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
    void dockedListFollowsMainWindow();
    void horizontalDockResizesAndKeepsWindowsAdjacent();
    void mainMinimizeRestoresOnlyRequestedList();
    void mainMaximizeHidesOnlyDockedList();
    void geometryDockAndPinStatePersist();
    void persistedDockEdgeSurvivesInitialPreferenceWiring();
    void restoredGeometryBalancesMinimumAndAvailableScreen();
    void offscreenGeometryRestoresInsideAvailableScreen();
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
    // right edge; it should snap so the list window's left edge aligns with the
    // main window's right edge and it is vertically centered.
    const int expectedRightX = mainWindow.geometry().right() + 1;
    const int expectedCenterY = mainWindow.geometry().y()
        + (mainWindow.geometry().height() - listWindow.height()) / 2;

    windows.moveListWindow(expectedRightX - 15, expectedCenterY + 50);
    QCOMPARE(listWindow.x(), expectedRightX);
    QCOMPARE(listWindow.y(), expectedCenterY);
    QCOMPARE(windows.listWindowX(), expectedRightX);
    QCOMPARE(windows.listWindowY(), expectedCenterY);
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

    const QRect mainGeo = mainWindow.geometry();
    const int centerX = mainGeo.x() + (mainGeo.width() - listWindow.width()) / 2;
    const int centerY = mainGeo.y() + (mainGeo.height() - listWindow.height()) / 2;

    windows.snapListWindow("left");
    QCOMPARE(listWindow.x(), mainGeo.left() - listWindow.width());
    QCOMPARE(listWindow.y(), centerY);

    windows.snapListWindow("right");
    QCOMPARE(listWindow.x(), mainGeo.right() + 1);
    QCOMPARE(listWindow.y(), centerY);

    windows.snapListWindow("top");
    QCOMPARE(listWindow.x(), centerX);
    QCOMPARE(listWindow.y(), mainGeo.top() - listWindow.height());

    windows.snapListWindow("bottom");
    QCOMPARE(listWindow.x(), centerX);
    QCOMPARE(listWindow.y(), mainGeo.bottom() + 1);
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

    const int rightX = mainWindow.geometry().right() + 1;
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

    const int rightX = mainWindow.geometry().right() + 1;
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
    QCOMPARE(listWindow.x(),
             mainGeo.x() + (mainGeo.width() - listWindow.width()) / 2);
    QCOMPARE(listWindow.y(), mainGeo.bottom() + 1);
}

void WindowControllerTest::horizontalDockResizesAndKeepsWindowsAdjacent()
{
    QWindow mainWindow;
    mainWindow.setFlags(Qt::FramelessWindowHint);
    mainWindow.setMinimumSize(QSize(300, 200));
    mainWindow.setGeometry(10, 80, 450, 240);
    QWindow listWindow;
    listWindow.setFlags(Qt::FramelessWindowHint);
    listWindow.setMinimumSize(QSize(300, 180));
    listWindow.setGeometry(0, 0, 450, 220);

    WindowController windows;
    windows.setWindows(&mainWindow, nullptr);
    windows.setListWindow(&listWindow);
    windows.snapListWindow(QStringLiteral("right"));

    QCOMPARE(listWindow.x(), mainWindow.geometry().right() + 1);
    const QRect available = mainWindow.screen()->availableGeometry();
    QVERIFY(available.contains(mainWindow.geometry().united(listWindow.geometry())));
    QVERIFY(mainWindow.width() >= mainWindow.minimumWidth());
    QVERIFY(listWindow.width() >= listWindow.minimumWidth());
}

void WindowControllerTest::mainMinimizeRestoresOnlyRequestedList()
{
    QWindow mainWindow;
    QWindow listWindow;
    WindowController windows;
    windows.setWindows(&mainWindow, nullptr);
    windows.setListWindow(&listWindow);
    windows.showListWindow();

    mainWindow.setWindowState(Qt::WindowMinimized);
    QCoreApplication::processEvents();
    QVERIFY(!listWindow.isVisible());
    mainWindow.setWindowState(Qt::WindowNoState);
    QCoreApplication::processEvents();
    QVERIFY(listWindow.isVisible());

    windows.hideListWindow();
    mainWindow.setWindowState(Qt::WindowMinimized);
    QCoreApplication::processEvents();
    mainWindow.setWindowState(Qt::WindowNoState);
    QCoreApplication::processEvents();
    QVERIFY(!listWindow.isVisible());
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
        miniWindow.setGeometry(180, 220, 560, 96);
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
    QWindow restoredList;
    WindowController restored;
    restored.setWindows(&restoredMain, &restoredMini);
    restored.setListWindow(&restoredList);

    QCOMPARE(restoredMain.geometry(), QRect(450, 160, 300, 220));
    QCOMPARE(restoredMini.geometry(), QRect(180, 220, 560, 96));
    QCOMPARE(restoredList.size(), QSize(400, 300));
    QCOMPARE(restored.listDockEdge(), QStringLiteral("left"));
    QVERIFY(restored.alwaysOnTop());
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

    QCOMPARE(listWindow.size(),
             QSize(listWindow.screen()->availableGeometry().width(), 360));
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
