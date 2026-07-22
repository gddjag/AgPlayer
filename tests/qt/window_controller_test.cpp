#include "window_controller.hpp"

#include <QTest>

#include <vector>

class WindowControllerTest final : public QObject {
    Q_OBJECT

private slots:
    void switchingWindowsDoesNotRecreatePlayback();
    void visibilityWaitsForDestinationReadiness();
    void shutdownIsOrderedAndIdempotent();
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
    actions.stopPlayback = [&calls] { calls.push_back(1); };
    actions.flushLibrary = [&calls] { calls.push_back(2); };
    actions.cancelWaveform = [&calls] { calls.push_back(3); };
    actions.quitApplication = [&calls] { calls.push_back(4); };
    WindowController windows(std::move(actions));

    windows.requestClose();
    windows.requestClose();

    QCOMPARE(calls, std::vector<int>({1, 2, 3, 4}));
}

QTEST_GUILESS_MAIN(WindowControllerTest)
#include "window_controller_test.moc"
