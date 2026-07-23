#include "window_controller.hpp"

#include <QTest>
#include <QWindow>

#include <vector>

class WindowControllerTest final : public QObject {
    Q_OBJECT

private slots:
    void switchingWindowsDoesNotRecreatePlayback();
    void updatesExistingWindowObjectsAndFlags();
    void visibilityWaitsForDestinationReadiness();
    void shutdownIsOrderedAndIdempotent();
    void missingShutdownCollaboratorsRemainIdempotent();
    void listWindowVisibilityCanBeToggled();
    void listWindowMagneticSnappingToMainWindowEdges();
    void listWindowExplicitSnapToEachEdge();
};

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

    QVERIFY(windows.listWindowDetached());
    QVERIFY(!windows.listWindowVisible());
    QVERIFY(!listWindow.isVisible());

    windows.showListWindow();
    QVERIFY(windows.listWindowDetached());
    QVERIFY(windows.listWindowVisible());
    QVERIFY(listWindow.isVisible());

    windows.hideListWindow();
    QVERIFY(windows.listWindowDetached());
    QVERIFY(!windows.listWindowVisible());
    QVERIFY(!listWindow.isVisible());

    windows.showListWindow();
    QVERIFY(windows.listWindowDetached());
    QVERIFY(windows.listWindowVisible());
    QVERIFY(listWindow.isVisible());

    windows.toggleListWindow();
    QVERIFY(windows.listWindowDetached());
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
    const int expectedRightX = mainWindow.frameGeometry().right() + 1;
    const int expectedCenterY = mainWindow.frameGeometry().y()
        + (mainWindow.frameGeometry().height() - listWindow.height()) / 2;

    windows.moveListWindow(expectedRightX - 15, expectedCenterY + 50);
    QCOMPARE(listWindow.x(), expectedRightX);
    QCOMPARE(listWindow.y(), expectedCenterY);
    QCOMPARE(windows.listWindowX(), expectedRightX);
    QCOMPARE(windows.listWindowY(), expectedCenterY);
}

void WindowControllerTest::listWindowExplicitSnapToEachEdge()
{
    QWindow mainWindow;
    mainWindow.setGeometry(200, 200, 400, 300);

    QWindow listWindow;
    listWindow.setGeometry(0, 0, 200, 150);

    WindowController windows;
    windows.setWindows(&mainWindow, nullptr);
    windows.setListWindow(&listWindow);

    const QRect mainGeo = mainWindow.frameGeometry();
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
}

QTEST_MAIN(WindowControllerTest)
#include "window_controller_test.moc"
