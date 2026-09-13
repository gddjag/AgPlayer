#include "window_controller.hpp"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>
#include <QWindow>

#include <cstddef>
#include <new>
#include <vector>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

class WindowControllerTest final : public QObject {
    Q_OBJECT

private slots:
    void applicationReactivationRestoresMinimizedPlayer()
    {
        QWindow mainWindow;
        QWindow miniWindow;
        WindowController windows;
        windows.setWindows(&mainWindow, &miniWindow);
        windows.setMainReady(true);
        windows.setMiniReady(true);
        windows.showMain();
        mainWindow.showMinimized();
        windows.restoreApplicationWindows();
        QVERIFY(mainWindow.isVisible());
        QCOMPARE(mainWindow.windowState(), Qt::WindowNoState);
        windows.showMini();
        miniWindow.showMinimized();
        windows.restoreApplicationWindows();
        QVERIFY(miniWindow.isVisible());
        QCOMPARE(miniWindow.windowState(), Qt::WindowNoState);
        QVERIFY(!mainWindow.isVisible());
    }
    void initTestCase();
    void init();
    void glassBackdropContractIsAbsent();
    void defaultListSizeMatchesReference();
    void availableGeometryForWindowUsesScreenWorkArea();
    void legacyListWidthsMigrateWithoutOverwritingIndependentSize();
    void dpiChangePreservesLogicalSizeAcrossScales();
    void switchingWindowsDoesNotRecreatePlayback();
    void immersivePresentationTemporarilyHidesAndRestoresPlayerWindows();
    void immersivePresentationHonorsDeferredShellRequestWithoutPersistingTemporaryState();
    void immersivePresentationDeferredRollingShellKeepsIndependentListHidden();
    void immersivePresentationRestoresMiniGeometry();
    void immersivePresentationRestoresFirstRunIntegratedGeometryWithoutCreatingASetting();
    void immersivePresentationRestoresFirstRunMiniGeometryWithoutCreatingASetting();
    void immersivePresentationFirstRunGeometryPersistsAfterLaterUserMove();
    void immersivePresentationDefersClassicShellRequestWithoutOverwritingIntegratedGeometry();
    void updatesExistingWindowObjectsAndFlags();
    void visibilityWaitsForDestinationReadiness();
    void shutdownIsOrderedAndIdempotent();
    void missingShutdownCollaboratorsRemainIdempotent();
    void listWindowVisibilityCanBeToggled();
    void firstRunClassicListAppearsWhenLibraryBecomesAvailable();
    void rollingShellCannotShowIndependentList();
    void listAvailabilityDoesNotOverwriteUserVisibilityRequest();
    void listWindowMagneticSnappingToMainWindowEdges();
    void listWindowExplicitSnapToEachEdge();
    void listWindowDetachesOutsideSnapThreshold();
    void listWindowDoesNotSnapWithoutProjectionOverlap();
    void nativeMoveEventsSnapAndUseReleaseHysteresis();
    void toolWindowsShareMainTransientLayering();
    void auxiliaryWindowsOpenCenteredOverMain();
    void audioToolsRestorePersistedGeometryWithoutRecentering();
    void audioToolsCreatedAfterShowRequestUsesFirstRunCentering();
    void audioToolsGeometryPersistsAcrossControllerLifetime();
    void restoredGeometryAvoidsVirtualDesktopHoles();
    void restoredGeometryRehomesAnOfflineScreen();
    void destroyedAuxiliaryWindowDoesNotPoisonReplacementPositioning();
    void destroyedSettingsWindowDoesNotPoisonReplacementPositioning();
#ifdef Q_OS_WIN
    void auxiliaryWindowRemainsAboveDockedPlayerGroup();
    void auxiliaryWindowsKeepNativeSizeAcrossScreens();
    void maximizedAuxiliaryWindowsPreserveNormalGeometryForRestart();
#endif
    void dockedListFollowsMainWindow();
    void firstAttachedListAlignsWithMainWindow();
    void dockedGroupDoesNotClampMainMoveAtScreenEdge();
    void dockedListOwnsAlignedWidthAndKeepsWindowsAdjacent();
#ifdef Q_OS_WIN
    void dockedWindowsKeepLogicalSizeAcrossScreens();
    void nativeTaskbarGroupUsesMainAsOnlyAppWindow();
    void recreatedMainTaskbarSurfaceRefreshesStylesAndCommands();
    void taskbarCommandsToggleDockedGroupWithoutResizing();
    void taskbarToggleEntryPointMinimizesAndRestoresWindowGroup();
    void taskbarToggleEntryPointActivatesBackgroundGroup();
    void taskbarActivationDoesNotCancelMinimize();
#endif
    void mainMinimizeRestoresOnlyRequestedList();
    void showMainRestoresAndRaisesTheExistingWindowGroup();
    void mainMaximizeHidesOnlyDockedList();
    void mainFullscreenPreservesDockedListGeometry();
    void geometryDockAndPinStatePersist();
    void legacyMiniGeometryMigratesToReferenceDefault();
    void persistedClassicGeometrySurvivesReferenceDefaultChange();
    void switchingBackToClassicRestoresUserSize();
    void shellSwitchKeepsSecondaryScreenAndUserSizes();
    void shellMinimumChangesDoNotOverwriteOutgoingUserSize();
    void classicListVisibilitySurvivesAVisitToRollingShell();
    void switchingBackWithoutClassicGeometryUsesCompactDefault();
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

void WindowControllerTest::glassBackdropContractIsAbsent()
{
    QCOMPARE(WindowController::staticMetaObject.indexOfProperty("glassBackdropEnabled"), -1);
    QCOMPARE(WindowController::staticMetaObject.indexOfMethod(
                 QMetaObject::normalizedSignature("setGlassBackdropEnabled(bool)")),
             -1);
}

void WindowControllerTest::defaultListSizeMatchesReference()
{
    WindowController windows;
    QCOMPARE(windows.listWindowWidth(), 960);
    // Fresh settings enable 48-DIP waveform rows.  The complete list window
    // is 40 title + 36 header + 10 rows + 36 filter.
    QCOMPARE(windows.listWindowHeight(), 592);
}

void WindowControllerTest::availableGeometryForWindowUsesScreenWorkArea()
{
    QWindow window;
    QVERIFY(window.screen() != nullptr);
    WindowController controller;
    QCOMPARE(controller.availableGeometryForWindow(&window),
             window.screen()->availableGeometry());

    QScreen* const primary = QGuiApplication::primaryScreen();
    QVERIFY(primary != nullptr);
    QCOMPARE(controller.availableGeometryForWindow(nullptr),
             primary->availableGeometry());
}

void WindowControllerTest::legacyListWidthsMigrateWithoutOverwritingIndependentSize()
{
    // Catches broad migrations that overwrite a genuinely independent window,
    // as well as missing migrations for exact historical defaults/tag widths.
    const QList<int> legacyWidths{1104, 1228, 1284, 1447};
    for (const int legacyWidth : legacyWidths) {
        QSettings settings;
        settings.clear();
        settings.setValue(QStringLiteral("windows/listRequestedVisible"), false);
        settings.setValue(QStringLiteral("windows/listDockEdge"),
                          QStringLiteral("none"));
        settings.setValue(QStringLiteral("windows/listGeometry"),
                          QRect(20, 30, legacyWidth, 568));
        settings.sync();
        QWindow listWindow;
        listWindow.setGeometry(20, 30, 960, 568);
        WindowController windows;
        windows.setListWindow(&listWindow);
        const int boundedAlignedWidth = qMin(
            960, listWindow.screen()->availableGeometry().width());
        QCOMPARE(listWindow.width(), boundedAlignedWidth);
        QCOMPARE(listWindow.height(), 568);
        QCOMPARE(QSettings().value(QStringLiteral("windows/listGeometryVersion"))
                     .toInt(),
                 1);
    }

    QSettings settings;
    settings.clear();
    settings.setValue(QStringLiteral("windows/listRequestedVisible"), false);
    settings.setValue(QStringLiteral("windows/listDockEdge"),
                      QStringLiteral("none"));
    settings.setValue(QStringLiteral("windows/listGeometry"),
                      QRect(20, 30, 733, 611));
    settings.sync();
    QWindow independentList;
    WindowController windows;
    windows.setListWindow(&independentList);
    QCOMPARE(independentList.size(), QSize(733, 611));
}

void WindowControllerTest::dpiChangePreservesLogicalSizeAcrossScales()
{
    struct DpiTransition {
        const char* name;
        qreal sourceDpr;
        qreal targetDpr;
        QRect currentNativeGeometry;
        QSize currentLogicalSize;
        QRect suggestedNativeGeometry;
        QRect targetAvailableGeometry;
    };
    const QList<DpiTransition> transitions{
        {"100-to-125", 1.0, 1.25, QRect(120, 80, 1104, 342), QSize(1104, 342),
         QRect(1920, 120, 1380, 428),
         QRect(1920, 0, 1920, 1040)},
        {"125-to-150", 1.25, 1.5, QRect(1920, 120, 1380, 428), QSize(1104, 342),
         QRect(3840, 80, 1656, 513),
         QRect(3840, 0, 2560, 1400)},
        {"150-to-100", 1.5, 1.0, QRect(3840, 80, 1656, 513), QSize(1104, 342),
         QRect(0, 100, 1104, 342),
         QRect(0, 0, 1920, 1080)},
    };

    for (const DpiTransition& transition : transitions) {
        const QRect result = WindowController::geometryForDpiChange(
            transition.currentNativeGeometry, transition.sourceDpr,
            transition.suggestedNativeGeometry, transition.targetDpr,
            transition.targetAvailableGeometry);
        QCOMPARE(QSize(qRound(result.width() / transition.targetDpr),
                       qRound(result.height() / transition.targetDpr)),
                 transition.currentLogicalSize);
        QCOMPARE(result.topLeft(), transition.suggestedNativeGeometry.topLeft());
        QVERIFY2(transition.targetAvailableGeometry.contains(result), transition.name);
    }

    const QRect bounded = WindowController::geometryForDpiChange(
        QRect(0, 0, 1800, 900), 1.0, QRect(1500, 900, 2250, 1125), 1.25,
        QRect(1920, 0, 1600, 900));
    QCOMPARE(bounded.size(), QSize(1600, 900));
    QVERIFY(QRect(1920, 0, 1600, 900).contains(bounded));
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

void WindowControllerTest::immersivePresentationTemporarilyHidesAndRestoresPlayerWindows()
{
    QWindow mainWindow;
    QWindow miniWindow;
    QWindow listWindow;
    WindowController windows;
    windows.setWindows(&mainWindow, &miniWindow);
    windows.setListWindow(&listWindow);
    windows.showListWindow();

    QVERIFY(windows.mainVisible());
    QVERIFY(windows.listWindowVisible());
    QVERIFY(!windows.immersivePresentationActive());

    windows.enterImmersivePresentation();
    QVERIFY(windows.immersivePresentationActive());
    QVERIFY(!windows.mainVisible());
    QVERIFY(!windows.miniVisible());
    QVERIFY(!windows.listWindowVisible());
    QVERIFY(!mainWindow.isVisible());
    QVERIFY(!miniWindow.isVisible());
    QVERIFY(!listWindow.isVisible());

    windows.leaveImmersivePresentation();
    QVERIFY(!windows.immersivePresentationActive());
    QVERIFY(windows.mainVisible());
    QVERIFY(!windows.miniVisible());
    QVERIFY(windows.listWindowVisible());
    QVERIFY(mainWindow.isVisible());
    QVERIFY(listWindow.isVisible());

    windows.enterImmersivePresentation();
    windows.showMini();
    QVERIFY(windows.immersivePresentationActive());
    QVERIFY(!windows.mainVisible());
    QVERIFY(!windows.miniVisible());
    QVERIFY(!mainWindow.isVisible());
    QVERIFY(!miniWindow.isVisible());
    windows.leaveImmersivePresentation();
    QVERIFY(!windows.mainVisible());
    QVERIFY(windows.miniVisible());
    QVERIFY(!windows.listWindowVisible());

    windows.enterImmersivePresentation();
    windows.showMain();
    QVERIFY(windows.immersivePresentationActive());
    QVERIFY(!windows.mainVisible());
    QVERIFY(!windows.miniVisible());
    QVERIFY(!mainWindow.isVisible());
    QVERIFY(!miniWindow.isVisible());
    windows.leaveImmersivePresentation();
    QVERIFY(windows.mainVisible());
    QVERIFY(!windows.miniVisible());
    QVERIFY(windows.listWindowVisible());
}

void WindowControllerTest::immersivePresentationHonorsDeferredShellRequestWithoutPersistingTemporaryState()
{
    const QRect classicGeometry(24, 36, 520, 280);
    const QRect integratedGeometry(92, 74, 640, 420);
    const QRect temporaryGeometry(180, 160, 440, 250);
    QSettings settings;
    settings.setValue(QStringLiteral("windows/mainGeometry"), classicGeometry);
    settings.setValue(QStringLiteral("windows/integratedMainGeometry"),
                      integratedGeometry);
    settings.sync();

    QWindow mainWindow;
    WindowController windows;
    windows.setWindows(&mainWindow, nullptr);
    windows.setMainWindowShellMode(1);
    QCOMPARE(mainWindow.geometry(), QRect(classicGeometry.topLeft(), integratedGeometry.size()));

    windows.enterImmersivePresentation();
    mainWindow.setGeometry(temporaryGeometry);
    windows.setMainWindowShellMode(0);

    windows.leaveImmersivePresentation();
    QCOMPARE(mainWindow.size(), classicGeometry.size().boundedTo(
                 mainWindow.screen()->availableGeometry().size()));
    QCOMPARE(settings.value(QStringLiteral("windows/mainGeometry")).toRect(),
             mainWindow.geometry());
    QCOMPARE(settings.value(QStringLiteral("windows/integratedMainGeometry")).toRect(),
             QRect(classicGeometry.topLeft(), integratedGeometry.size()));
}

void WindowControllerTest::immersivePresentationDeferredRollingShellKeepsIndependentListHidden()
{
    const QRect classicMainGeometry(100, 120, 640, 320);
    const QRect rollingMainGeometry(60, 90, 720, 360);
    const QRect classicListGeometry(100, 438, 640, 240);
    const QRect rollingListGeometry(40, 50, 460, 260);
    QSettings settings;
    settings.setValue(QStringLiteral("windows/mainGeometry"), classicMainGeometry);
    settings.setValue(QStringLiteral("windows/rollingMainGeometry"),
                      rollingMainGeometry);
    settings.setValue(QStringLiteral("windows/listGeometry"), classicListGeometry);
    settings.setValue(QStringLiteral("windows/listGeometryVersion"), 1);
    settings.setValue(QStringLiteral("windows/listRequestedVisible"), false);
    settings.setValue(QStringLiteral("windows/listDockEdge"),
                      QStringLiteral("bottom"));
    settings.setValue(QStringLiteral("windows/rollingListGeometry"),
                      rollingListGeometry);
    settings.setValue(QStringLiteral("windows/rollingListGeometryVersion"), 1);
    settings.setValue(QStringLiteral("windows/rollingListRequestedVisible"), true);
    settings.setValue(QStringLiteral("windows/rollingListDockEdge"),
                      QStringLiteral("none"));
    settings.sync();

    QWindow mainWindow;
    QWindow listWindow;
    WindowController windows;
    windows.setWindows(&mainWindow, nullptr);
    windows.setListWindow(&listWindow);

    QCOMPARE(mainWindow.geometry(), classicMainGeometry);
    QCOMPARE(listWindow.geometry(), classicListGeometry);
    QCOMPARE(windows.listDockEdge(), QStringLiteral("bottom"));
    QVERIFY(!windows.listWindowDetached());
    QVERIFY(!windows.listWindowVisible());

    windows.enterImmersivePresentation();
    windows.setMainWindowShellMode(2);
    windows.leaveImmersivePresentation();

    const QRect available = mainWindow.screen()->availableGeometry();
    const QSize targetSize = rollingMainGeometry.size().boundedTo(available.size());
    const QPoint targetPosition(
        qBound(available.left(), classicMainGeometry.x(), available.right() - targetSize.width() + 1),
        qBound(available.top(), classicMainGeometry.y(), available.bottom() - targetSize.height() + 1));
    QCOMPARE(mainWindow.geometry(), QRect(targetPosition, targetSize));
    QCOMPARE(listWindow.geometry(), classicListGeometry);
    QVERIFY(!windows.listWindowVisible());
    QVERIFY(!listWindow.isVisible());

    settings.sync();
    QCOMPARE(settings.value(QStringLiteral("windows/listGeometry")).toRect(),
             classicListGeometry);
    QVERIFY(!settings.value(
                 QStringLiteral("windows/listRequestedVisible")).toBool());
    QCOMPARE(settings.value(QStringLiteral("windows/listDockEdge")).toString(),
             QStringLiteral("bottom"));
    QCOMPARE(settings.value(
                 QStringLiteral("windows/rollingListGeometry")).toRect(),
             rollingListGeometry);
    QVERIFY(settings.value(
                QStringLiteral("windows/rollingListRequestedVisible")).toBool());
    QCOMPARE(settings.value(
                 QStringLiteral("windows/rollingListDockEdge")).toString(),
             QStringLiteral("none"));
}

void WindowControllerTest::immersivePresentationRestoresMiniGeometry()
{
    const QRect miniGeometry(118, 96, 360, 208);
    const QRect temporaryGeometry(230, 170, 280, 180);
    QSettings settings;
    settings.setValue(QStringLiteral("windows/miniGeometry"), miniGeometry);
    settings.setValue(QStringLiteral("windows/miniGeometryVersion"), 4);
    settings.sync();
    QWindow mainWindow;
    QWindow miniWindow;
    WindowController windows;
    windows.setWindows(&mainWindow, &miniWindow);
    windows.showMini();
    QCOMPARE(miniWindow.geometry(), miniGeometry);

    windows.enterImmersivePresentation();
    miniWindow.setGeometry(temporaryGeometry);
    windows.leaveImmersivePresentation();

    QCOMPARE(miniWindow.geometry(), miniGeometry);
    QCOMPARE(settings.value(QStringLiteral("windows/miniGeometry")).toRect(),
             miniGeometry);
}

void WindowControllerTest::immersivePresentationRestoresFirstRunIntegratedGeometryWithoutCreatingASetting()
{
    const QRect firstRunGeometry(126, 88, 580, 360);
    const QRect temporaryGeometry(210, 140, 320, 220);
    QWindow mainWindow;
    {
        WindowController windows;
        windows.setWindows(&mainWindow, nullptr);
        windows.setMainWindowShellMode(1);
        mainWindow.setGeometry(firstRunGeometry);

        QSettings settings;
        settings.remove(QStringLiteral("windows/integratedMainGeometry"));
        settings.sync();
        QVERIFY(!settings.contains(QStringLiteral("windows/integratedMainGeometry")));

        windows.enterImmersivePresentation();
        mainWindow.setGeometry(temporaryGeometry);
        windows.leaveImmersivePresentation();

        QCOMPARE(mainWindow.geometry(), firstRunGeometry);
        QTest::qWait(300);
        settings.sync();
        QVERIFY(!settings.contains(QStringLiteral("windows/integratedMainGeometry")));
    }
    QSettings persistedSettings;
    persistedSettings.sync();
    QVERIFY(!persistedSettings.contains(
        QStringLiteral("windows/integratedMainGeometry")));
}

void WindowControllerTest::immersivePresentationRestoresFirstRunMiniGeometryWithoutCreatingASetting()
{
    const QRect firstRunGeometry(118, 96, 360, 208);
    const QRect temporaryGeometry(230, 170, 280, 180);
    QWindow mainWindow;
    QWindow miniWindow;
    {
        WindowController windows;
        windows.setWindows(&mainWindow, &miniWindow);
        miniWindow.setGeometry(firstRunGeometry);

        QSettings settings;
        settings.remove(QStringLiteral("windows/miniGeometry"));
        settings.sync();
        QVERIFY(!settings.contains(QStringLiteral("windows/miniGeometry")));

        windows.enterImmersivePresentation();
        miniWindow.setGeometry(temporaryGeometry);
        windows.leaveImmersivePresentation();

        QCOMPARE(miniWindow.geometry(), firstRunGeometry);
        QTest::qWait(300);
        settings.sync();
        QVERIFY(!settings.contains(QStringLiteral("windows/miniGeometry")));
    }
    QSettings persistedSettings;
    persistedSettings.sync();
    QVERIFY(!persistedSettings.contains(QStringLiteral("windows/miniGeometry")));
}

void WindowControllerTest::immersivePresentationFirstRunGeometryPersistsAfterLaterUserMove()
{
    const QRect firstRunGeometry(126, 88, 580, 360);
    const QRect userGeometry(164, 112, 600, 380);
    QWindow mainWindow;
    WindowController windows;
    windows.setWindows(&mainWindow, nullptr);
    windows.setMainWindowShellMode(1);
    mainWindow.setGeometry(firstRunGeometry);

    QSettings settings;
    settings.remove(QStringLiteral("windows/integratedMainGeometry"));
    settings.sync();

    windows.enterImmersivePresentation();
    windows.leaveImmersivePresentation();
    QTest::qWait(300);
    settings.sync();
    QVERIFY(!settings.contains(QStringLiteral("windows/integratedMainGeometry")));

    mainWindow.setGeometry(userGeometry);
    QTest::qWait(300);
    settings.sync();
    QCOMPARE(settings.value(QStringLiteral("windows/integratedMainGeometry")).toRect(),
             userGeometry);
}

void WindowControllerTest::immersivePresentationDefersClassicShellRequestWithoutOverwritingIntegratedGeometry()
{
    const QRect classicGeometry(24, 36, 520, 280);
    const QRect integratedGeometry(92, 74, 640, 420);
    const QRect userClassicGeometry(48, 62, 560, 300);
    QSettings settings;
    settings.setValue(QStringLiteral("windows/mainGeometry"), classicGeometry);
    settings.setValue(QStringLiteral("windows/integratedMainGeometry"),
                      integratedGeometry);
    settings.sync();

    QWindow mainWindow;
    {
        WindowController windows;
        windows.setWindows(&mainWindow, nullptr);
        windows.setMainWindowShellMode(1);
        const QRect activeIntegratedGeometry(classicGeometry.topLeft(), integratedGeometry.size());
        QCOMPARE(mainWindow.geometry(), activeIntegratedGeometry);

        windows.enterImmersivePresentation();
        windows.setMainWindowShellMode(0);
        windows.leaveImmersivePresentation();

        QCOMPARE(mainWindow.size(), classicGeometry.size().boundedTo(
                     mainWindow.screen()->availableGeometry().size()));
        QTest::qWait(300);
        settings.sync();
        QCOMPARE(settings.value(QStringLiteral("windows/mainGeometry")).toRect(),
                 mainWindow.geometry());
        QCOMPARE(settings.value(
                     QStringLiteral("windows/integratedMainGeometry")).toRect(),
                 activeIntegratedGeometry);

        mainWindow.setGeometry(userClassicGeometry);
        QTest::qWait(300);
        settings.sync();
        QCOMPARE(settings.value(QStringLiteral("windows/mainGeometry")).toRect(),
                 userClassicGeometry);
        QCOMPARE(settings.value(
                     QStringLiteral("windows/integratedMainGeometry")).toRect(),
                 activeIntegratedGeometry);
    }
    QSettings persistedSettings;
    persistedSettings.sync();
    QCOMPARE(persistedSettings.value(QStringLiteral("windows/mainGeometry")).toRect(),
             userClassicGeometry);
    QCOMPARE(persistedSettings.value(
                 QStringLiteral("windows/integratedMainGeometry")).toRect(),
             QRect(classicGeometry.topLeft(), integratedGeometry.size()));
}

void WindowControllerTest::switchingBackToClassicRestoresUserSize()
{
    QWindow mainWindow;
    mainWindow.setGeometry(40, 50, 960, 298);

    WindowController windows;
    windows.setWindows(&mainWindow, nullptr);
    mainWindow.setGeometry(20, 30, 700, 320);

    windows.setMainWindowShellMode(1);
    QCOMPARE(mainWindow.size(), QSize(1386, 832).boundedTo(
                 mainWindow.screen()->availableGeometry().size()));
    mainWindow.setGeometry(20, 40, 760, 700);

    windows.setMainWindowShellMode(0);
    QCOMPARE(mainWindow.size(), QSize(700, 320).boundedTo(
                 mainWindow.screen()->availableGeometry().size()));

    windows.setMainWindowShellMode(1);
    QCOMPARE(mainWindow.geometry(), QRect(20, 40, 760, 700));

    windows.setMainWindowShellMode(2);
    QCOMPARE(mainWindow.size(), QSize(1386, 972).boundedTo(
                 mainWindow.screen()->availableGeometry().size()));
    mainWindow.setGeometry(30, 50, 760, 460);

    windows.setMainWindowShellMode(0);
    QCOMPARE(mainWindow.size(), QSize(700, 320).boundedTo(
                 mainWindow.screen()->availableGeometry().size()));
    const QRect referenceClassicGeometry = mainWindow.geometry();
    windows.setMainWindowShellMode(2);
    QCOMPARE(mainWindow.geometry(), QRect(30, 50, 760, 460));
    QCOMPARE(QSettings().value(QStringLiteral("windows/mainGeometry")).toRect(),
             referenceClassicGeometry);
    QCOMPARE(QSettings().value(
                 QStringLiteral("windows/integratedMainGeometry")).toRect(),
             QRect(20, 40, 760, 700));
    QCOMPARE(QSettings().value(
                 QStringLiteral("windows/rollingMainGeometry")).toRect(),
             QRect(30, 50, 760, 460));
}

void WindowControllerTest::shellSwitchKeepsSecondaryScreenAndUserSizes()
{
    QScreen* secondary = nullptr;
    for (QScreen* screen : QGuiApplication::screens()) {
        if (screen != QGuiApplication::primaryScreen()) {
            secondary = screen;
            if (screen->geometry().x() < 0 || screen->geometry().y() < 0) break;
        }
    }
    if (secondary == nullptr) {
        QTest::qSkip("requires a secondary screen", __FILE__, __LINE__);
        return;
    }
    const QRect area = secondary->availableGeometry();
    const QPoint anchor = area.topLeft() + QPoint(20, 20);
    const QSize classicSize = QSize(700, 320).boundedTo(area.size() - QSize(40, 40));
    const QSize integratedSize = QSize(800, 600).boundedTo(area.size() - QSize(40, 40));
    QSettings settings;
    settings.setValue(QStringLiteral("windows/mainGeometry"), QRect(anchor, classicSize));
    settings.setValue(QStringLiteral("windows/integratedMainGeometry"),
                      QRect(QGuiApplication::primaryScreen()->availableGeometry().topLeft(),
                            integratedSize));
    QWindow mainWindow;
    mainWindow.setFlags(Qt::FramelessWindowHint);
    WindowController windows;
    windows.setWindows(&mainWindow, nullptr);
    QTRY_COMPARE(mainWindow.screen(), secondary);
    windows.setMainWindowShellMode(1);
    QTRY_COMPARE(mainWindow.geometry(), QRect(anchor, integratedSize));
    QTRY_COMPARE(mainWindow.screen(), secondary);
    windows.setMainWindowShellMode(0);
    QTRY_COMPARE(mainWindow.geometry(), QRect(anchor, classicSize));
    // An unvisited shell uses its own default on this same monitor.
    windows.setMainWindowShellMode(2);
    QTRY_COMPARE(mainWindow.screen(), secondary);
    QVERIFY(area.contains(mainWindow.geometry()));
}

void WindowControllerTest::shellMinimumChangesDoNotOverwriteOutgoingUserSize()
{
    QWindow mainWindow;
    mainWindow.setGeometry(20, 30, 700, 320);
    WindowController windows;
    windows.setWindows(&mainWindow, nullptr);
    const QSize classicSize = mainWindow.size();
    // Mirrors Main.qml's synchronous minimum-size binding during a switch.
    connect(&windows, &WindowController::mainWindowShellModeChanged, &mainWindow, [&] {
        const QSize minimum = windows.mainWindowShellMode() == 0
            ? QSize(612, 232) : QSize(1180, 720);
        mainWindow.setMinimumSize(minimum);
        mainWindow.resize(mainWindow.size().expandedTo(minimum));
    });
    windows.setMainWindowShellMode(1);
    QCOMPARE(QSettings().value(QStringLiteral("windows/mainGeometry")).toRect().size(), classicSize);
    windows.setMainWindowShellMode(0);
    QCOMPARE(mainWindow.size(), classicSize);
}

void WindowControllerTest::classicListVisibilitySurvivesAVisitToRollingShell()
{
    QWindow mainWindow;
    mainWindow.setGeometry(100, 120, 640, 320);
    QWindow listWindow;
    listWindow.setGeometry(20, 30, 420, 240);

    WindowController windows;
    windows.setWindows(&mainWindow, nullptr);
    windows.setListWindow(&listWindow);
    windows.showListWindow();
    QVERIFY(windows.listWindowVisible());
    QVERIFY(listWindow.isVisible());

    windows.setMainWindowShellMode(2);
    QVERIFY(!windows.listWindowVisible());
    QVERIFY(!listWindow.isVisible());

    // Even a stale or accidental rolling-shell request must not materialize
    // the classic-only top-level list.
    windows.showListWindow();
    QVERIFY(!windows.listWindowVisible());
    QVERIFY(!listWindow.isVisible());

    windows.setMainWindowShellMode(0);
    QVERIFY(windows.listWindowVisible());
    QVERIFY(listWindow.isVisible());
}

void WindowControllerTest::switchingBackWithoutClassicGeometryUsesCompactDefault()
{
    WindowController windows;
    windows.setMainWindowShellMode(1);

    QWindow mainWindow;
    mainWindow.setGeometry(20, 30, 1672, 941);
    windows.setWindows(&mainWindow, nullptr);
    QSettings().remove(QStringLiteral("windows/mainGeometry"));

    windows.setMainWindowShellMode(0);

    QCOMPARE(mainWindow.size(),
             QSize(863, 266).boundedTo(
                 mainWindow.screen()->availableGeometry().size()));
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

void WindowControllerTest::firstRunClassicListAppearsWhenLibraryBecomesAvailable()
{
    // init() normally installs an explicit hidden preference.  A true first
    // run has no visibility key and should request the classic companion list.
    QSettings settings;
    settings.clear();
    settings.sync();

    QWindow mainWindow;
    QWindow listWindow;
    WindowController windows;
    windows.setListWindowPanelAllowed(false);
    windows.setWindows(&mainWindow, nullptr);
    windows.setListWindow(&listWindow);
    QVERIFY(!windows.listWindowVisible());
    QVERIFY(!listWindow.isVisible());

    // This is the controller boundary exercised when the empty library gains
    // its first successfully imported track.
    windows.setListWindowPanelAllowed(true);
    QVERIFY(windows.listWindowVisible());
    QVERIFY(listWindow.isVisible());
}

void WindowControllerTest::rollingShellCannotShowIndependentList()
{
    QWindow mainWindow;
    QWindow listWindow;
    WindowController windows;
    windows.setWindows(&mainWindow, nullptr);
    windows.setListWindow(&listWindow);

    windows.setMainWindowShellMode(2);
    windows.showListWindow();

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
#ifdef Q_OS_WIN
    // The native Win32 path includes the DWM frame coordinate adjustment.
    QCOMPARE(listWindow.geometry().right(), mainWindow.geometry().left() + 3);
#else
    // QRect::right() is inclusive: a two-pixel shared edge ends at left + 1.
    QCOMPARE(listWindow.geometry().right(), mainWindow.geometry().left() + 1);
#endif
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

void WindowControllerTest::audioToolsRestorePersistedGeometryWithoutRecentering()
{
    const QRect savedGeometry(40, 80, 700, 560);
    QSettings settings;
    settings.setValue(QStringLiteral("windows/audioToolsGeometry"), savedGeometry);
    settings.sync();

    QWindow mainWindow;
    mainWindow.setGeometry(20, 30, 600, 300);
    QWindow toolsWindow;
    toolsWindow.setGeometry(0, 0, 1672, 941);

    WindowController windows;
    windows.setWindows(&mainWindow, nullptr);
    windows.setAudioToolsWindow(&toolsWindow);
    windows.showAudioTools();
    QCoreApplication::processEvents();

    QCOMPARE(toolsWindow.geometry(), savedGeometry);
}

void WindowControllerTest::audioToolsCreatedAfterShowRequestUsesFirstRunCentering()
{
    QWindow mainWindow;
    mainWindow.setGeometry(80, 100, 640, 360);
    QWindow toolsWindow;
    toolsWindow.setGeometry(10, 10, 360, 240);

    WindowController windows;
    windows.setWindows(&mainWindow, nullptr);
    windows.showAudioTools();
    windows.setAudioToolsWindow(&toolsWindow);
    QCoreApplication::processEvents();

    QVERIFY(toolsWindow.isVisible());
    QCOMPARE(toolsWindow.geometry().center(), mainWindow.geometry().center());
}

void WindowControllerTest::audioToolsGeometryPersistsAcrossControllerLifetime()
{
    const QRect changedGeometry(60, 100, 680, 600);
    {
        QWindow mainWindow;
        QWindow toolsWindow;
        WindowController windows;
        windows.setWindows(&mainWindow, nullptr);
        windows.setAudioToolsWindow(&toolsWindow);
        toolsWindow.setGeometry(changedGeometry);
        QCoreApplication::processEvents();
    }
    QSettings().sync();

    QWindow restoredMain;
    QWindow restoredTools;
    WindowController restored;
    restored.setWindows(&restoredMain, nullptr);
    restored.setAudioToolsWindow(&restoredTools);

    QCOMPARE(restoredTools.geometry(), changedGeometry);
}

void WindowControllerTest::restoredGeometryAvoidsVirtualDesktopHoles()
{
    const QList<QRect> availableScreens{
        QRect(0, 0, 1920, 1080),
        QRect(1920, 0, 1920, 540),
    };
    const QRect restored = WindowController::geometryForAvailableScreens(
        QRect(2200, 700, 720, 320), QSize(320, 200), availableScreens, 0);

    QVERIFY(availableScreens.at(1).contains(restored));
    QVERIFY(std::any_of(availableScreens.cbegin(), availableScreens.cend(),
                        [&restored](const QRect& screen) {
                            return screen.intersects(restored);
                        }));
}

void WindowControllerTest::restoredGeometryRehomesAnOfflineScreen()
{
    const QList<QRect> availableScreens{QRect(0, 0, 1920, 1080)};
    const QRect restored = WindowController::geometryForAvailableScreens(
        QRect(3840, 100, 900, 700), QSize(320, 200), availableScreens, 0);

    QVERIFY(availableScreens.first().contains(restored));
    QCOMPARE(restored.size(), QSize(900, 700));
}

void WindowControllerTest::destroyedAuxiliaryWindowDoesNotPoisonReplacementPositioning()
{
    QWindow mainWindow;
    mainWindow.setGeometry(100, 120, 640, 360);
    WindowController windows;
    windows.setWindows(&mainWindow, nullptr);

    alignas(QWindow) std::byte storage[sizeof(QWindow)];
    auto* first = new (storage) QWindow;
    first->setGeometry(10, 10, 300, 220);
    windows.setAudioToolsWindow(first);
    windows.showAudioTools();
    QCoreApplication::processEvents();
    first->~QWindow();

    auto* replacement = new (storage) QWindow;
    replacement->setGeometry(10, 10, 300, 220);
    windows.presentAuxiliaryWindow(replacement);
    QCoreApplication::processEvents();

    QCOMPARE(replacement->geometry().center(), mainWindow.geometry().center());
    replacement->~QWindow();
}

void WindowControllerTest::destroyedSettingsWindowDoesNotPoisonReplacementPositioning()
{
    QWindow mainWindow;
    mainWindow.setGeometry(100, 120, 640, 360);
    WindowController windows;
    windows.setWindows(&mainWindow, nullptr);

    alignas(QWindow) std::byte storage[sizeof(QWindow)];
    auto* first = new (storage) QWindow;
    first->setGeometry(10, 10, 300, 220);
    windows.registerSettingsWindow(first);
    windows.presentAuxiliaryWindow(first);
    QCoreApplication::processEvents();
    first->~QWindow();

    auto* replacement = new (storage) QWindow;
    replacement->setGeometry(10, 10, 300, 220);
    windows.presentAuxiliaryWindow(replacement);
    QCoreApplication::processEvents();

    QCOMPARE(replacement->geometry().center(), mainWindow.geometry().center());
    replacement->~QWindow();
}

#ifdef Q_OS_WIN
void WindowControllerTest::auxiliaryWindowsKeepNativeSizeAcrossScreens()
{
    if (QGuiApplication::platformName().compare(QStringLiteral("windows"),
                                                Qt::CaseInsensitive) != 0) {
        QSKIP("requires the Windows native window manager");
    }
    const QList<QScreen*> screens = QGuiApplication::screens();
    if (screens.size() < 2) {
        QSKIP("hardware gap: requires at least two active Windows displays for mixed-DPI coverage");
    }

    QScreen* sourceScreen = nullptr;
    QScreen* targetScreen = nullptr;
    for (QScreen* source : screens) {
        for (QScreen* target : screens) {
            if (source != target
                && !qFuzzyCompare(source->devicePixelRatio(), target->devicePixelRatio())) {
                sourceScreen = source;
                targetScreen = target;
                break;
            }
        }
        if (sourceScreen != nullptr) break;
    }
    if (sourceScreen == nullptr) {
        QSKIP("hardware gap: requires two active Windows displays with different device-pixel ratios");
    }

    QWindow mainWindow;
    mainWindow.setGeometry(QRect(sourceScreen->availableGeometry().topLeft()
                                     + QPoint(60, 60),
                                 QSize(640, 320)));
    QWindow toolsWindow;
    QWindow settingsWindow;
    const QRect initialGeometry(
        sourceScreen->availableGeometry().topLeft() + QPoint(100, 100),
        QSize(880, 560));
    toolsWindow.setGeometry(initialGeometry);
    settingsWindow.setGeometry(initialGeometry);
    WindowController windows;
    windows.setWindows(&mainWindow, nullptr);
    windows.setAudioToolsWindow(&toolsWindow);
    windows.registerSettingsWindow(&settingsWindow);
    windows.showAudioTools();
    windows.presentAuxiliaryWindow(&settingsWindow);
    QVERIFY(QTest::qWaitForWindowExposed(&toolsWindow));
    QVERIFY(QTest::qWaitForWindowExposed(&settingsWindow));

    for (QWindow* window : {&toolsWindow, &settingsWindow}) {
        QTRY_VERIFY(window->screen() == sourceScreen);
        RECT before{};
        QVERIFY(GetWindowRect(reinterpret_cast<HWND>(window->winId()), &before));
        const QSize nativeSize(before.right - before.left,
                               before.bottom - before.top);
        const qreal sourceDpr = window->devicePixelRatio();
        const QSize logicalSize(qRound(nativeSize.width() / sourceDpr),
                                qRound(nativeSize.height() / sourceDpr));
        // Drive the actual Windows DPI-change message path; QWindow::setPosition()
        // can update its screen association without producing that transition.
        const QPoint targetPosition = targetScreen->availableGeometry().topLeft()
            + QPoint(100, 100);
        QVERIFY(SetWindowPos(reinterpret_cast<HWND>(window->winId()), nullptr,
                              targetPosition.x(), targetPosition.y(), 0, 0,
                              SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE));
        QTRY_VERIFY(window->screen() == targetScreen);
        const auto currentNativeSize = [window]() {
            RECT rect{};
            if (!GetWindowRect(reinterpret_cast<HWND>(window->winId()), &rect)) {
                return QSize();
            }
            return QSize(rect.right - rect.left, rect.bottom - rect.top);
        };
        if (window == &toolsWindow) {
            const QSize expectedNativeSize(
                qRound(logicalSize.width() * window->devicePixelRatio()),
                qRound(logicalSize.height() * window->devicePixelRatio()));
            QTRY_COMPARE(currentNativeSize(), expectedNativeSize);
        } else {
            QTRY_COMPARE(currentNativeSize(), nativeSize);
        }
    }
}

void WindowControllerTest::maximizedAuxiliaryWindowsPreserveNormalGeometryForRestart()
{
    if (QGuiApplication::platformName().compare(QStringLiteral("windows"),
                                                Qt::CaseInsensitive) != 0) {
        QSKIP("requires the Windows native window manager");
    }
    QScreen* const screen = QGuiApplication::primaryScreen();
    QVERIFY(screen != nullptr);
    const QPoint origin = screen->availableGeometry().topLeft() + QPoint(90, 70);
    const QRect toolsNormal(origin, QSize(880, 560));
    const QRect settingsNormal(origin + QPoint(40, 30), QSize(860, 640));
    QSettings settings;
    settings.setValue(QStringLiteral("windows/audioToolsGeometry"), toolsNormal);
    settings.setValue(QStringLiteral("windows/settingsGeometry"), settingsNormal);
    settings.sync();

    {
        QWindow mainWindow;
        QWindow toolsWindow;
        QWindow settingsWindow;
        WindowController windows;
        windows.setWindows(&mainWindow, nullptr);
        windows.setAudioToolsWindow(&toolsWindow);
        windows.registerSettingsWindow(&settingsWindow);
        windows.showAudioTools();
        windows.presentAuxiliaryWindow(&settingsWindow);
        QVERIFY(QTest::qWaitForWindowExposed(&toolsWindow));
        QVERIFY(QTest::qWaitForWindowExposed(&settingsWindow));

        ShowWindow(reinterpret_cast<HWND>(toolsWindow.winId()), SW_MAXIMIZE);
        ShowWindow(reinterpret_cast<HWND>(settingsWindow.winId()), SW_MAXIMIZE);
        QTRY_COMPARE(toolsWindow.windowState(), Qt::WindowMaximized);
        QTRY_COMPARE(settingsWindow.windowState(), Qt::WindowMaximized);
    }
    settings.sync();

    QWindow restoredMain;
    QWindow restoredTools;
    QWindow restoredSettings;
    WindowController restored;
    restored.setWindows(&restoredMain, nullptr);
    restored.setAudioToolsWindow(&restoredTools);
    restored.registerSettingsWindow(&restoredSettings);

    QCOMPARE(restoredTools.geometry(), toolsNormal);
    QCOMPARE(restoredSettings.geometry(), settingsNormal);
}
#endif

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

void WindowControllerTest::dockedListOwnsAlignedWidthAndKeepsWindowsAdjacent()
{
    QWindow mainWindow;
    mainWindow.setFlags(Qt::FramelessWindowHint);
    mainWindow.setMinimumSize(QSize(300, 200));
    mainWindow.setGeometry(10, 80, 450, 240);
    QWindow listWindow;
    listWindow.setFlags(Qt::FramelessWindowHint);
    listWindow.setMinimumSize(QSize(300, 180));
    listWindow.setGeometry(0, 0, 370, 220);
    const QSize mainSize = mainWindow.size();

    WindowController windows;
    windows.setWindows(&mainWindow, nullptr);
    windows.setListWindow(&listWindow);
    windows.snapListWindow(QStringLiteral("right"));

    QCOMPARE(listWindow.x(), mainWindow.geometry().right() - 1);
    QCOMPARE(listWindow.y(), mainWindow.y());
    QCOMPARE(mainWindow.size(), mainSize);
    QCOMPARE(listWindow.size(), QSize(mainWindow.width(), 220));

    listWindow.resize(610, 220);
    QCoreApplication::processEvents();
    windows.finishListWindowInteraction();
    QCOMPARE(listWindow.width(), mainWindow.width());
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
void WindowControllerTest::dockedWindowsKeepLogicalSizeAcrossScreens()
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
    const QSize mainLogicalSize = mainWindow.size();
    const int listLogicalHeight = listWindow.height();
    const auto nativeWindowsAreAdjacent = [&] {
        RECT mainRect{};
        RECT listRect{};
        return GetWindowRect(reinterpret_cast<HWND>(mainWindow.winId()), &mainRect)
            && GetWindowRect(reinterpret_cast<HWND>(listWindow.winId()), &listRect)
            && mainRect.left == listRect.left && mainRect.bottom - 2 == listRect.top;
    };

    mainWindow.setPosition(screens.at(1)->availableGeometry().topLeft()
                           + QPoint(80, 80));
    QTRY_VERIFY(mainWindow.screen() == screens.at(1));
    QTRY_COMPARE(listWindow.x(), mainWindow.x());
    QTRY_VERIFY(nativeWindowsAreAdjacent());

    QTRY_COMPARE(mainWindow.size(), mainLogicalSize);
    QTRY_COMPARE(listWindow.height(), listLogicalHeight);
    QTRY_COMPARE(listWindow.width(), mainWindow.width());

    mainWindow.setPosition(screens.at(0)->availableGeometry().topLeft()
                           + QPoint(120, 120));
    QTRY_VERIFY(mainWindow.screen() == screens.at(0));
    QTRY_COMPARE(listWindow.x(), mainWindow.x());
    QTRY_VERIFY(nativeWindowsAreAdjacent());
    QTRY_COMPARE(mainWindow.size(), mainLogicalSize);
    QTRY_COMPARE(listWindow.height(), listLogicalHeight);
    QTRY_COMPARE(listWindow.width(), mainWindow.width());
}

void WindowControllerTest::nativeTaskbarGroupUsesMainAsOnlyAppWindow()
{
    if (QGuiApplication::platformName().compare(QStringLiteral("windows"),
                                                Qt::CaseInsensitive) != 0) {
        QSKIP("requires the Windows native window manager");
    }

    QWindow mainWindow;
    mainWindow.setFlags(Qt::Window | Qt::FramelessWindowHint);
    QWindow listWindow;
    listWindow.setFlags(Qt::Window | Qt::FramelessWindowHint);
    QWindow toolsWindow;
    toolsWindow.setFlags(Qt::Window | Qt::FramelessWindowHint);
    QWindow settingsWindow;
    settingsWindow.setFlags(Qt::Window | Qt::FramelessWindowHint);

    WindowController windows;
    windows.setWindows(&mainWindow, nullptr);
    windows.setListWindow(&listWindow);
    windows.setAudioToolsWindow(&toolsWindow);
    windows.registerSettingsWindow(&settingsWindow);
    windows.setListWindowDetached(true);

    const auto exStyle = [](QWindow& window) {
        return static_cast<DWORD>(GetWindowLongPtrW(
            reinterpret_cast<HWND>(window.winId()), GWL_EXSTYLE));
    };
    const DWORD mainStyle = exStyle(mainWindow);
    QVERIFY(mainStyle & WS_EX_APPWINDOW);
    QVERIFY(!(mainStyle & WS_EX_TOOLWINDOW));
    const LONG_PTR mainWindowStyle = GetWindowLongPtrW(
        reinterpret_cast<HWND>(mainWindow.winId()), GWL_STYLE);
    QVERIFY2(mainWindowStyle & WS_SYSMENU,
             "the frameless main HWND must retain normal Shell system commands");
    QVERIFY2(mainWindowStyle & WS_MINIMIZEBOX,
             "the frameless main HWND must support Explorer taskbar minimize");
    constexpr LONG_PTR addedTaskbarStyles = WS_SYSMENU | WS_MINIMIZEBOX;
    for (QWindow* auxiliary : {&listWindow, &toolsWindow, &settingsWindow}) {
        const DWORD style = exStyle(*auxiliary);
        QVERIFY2(style & WS_EX_TOOLWINDOW,
                 "auxiliary windows must not create taskbar entries");
        QVERIFY2(!(style & WS_EX_APPWINDOW),
                 "only the main player may be the taskbar group entry");
        const LONG_PTR auxiliaryStyle = GetWindowLongPtrW(
            reinterpret_cast<HWND>(auxiliary->winId()), GWL_STYLE);
        QVERIFY2(!(auxiliaryStyle & addedTaskbarStyles),
                 "auxiliary windows must not receive main taskbar styles");
    }
}

void WindowControllerTest::recreatedMainTaskbarSurfaceRefreshesStylesAndCommands()
{
    if (QGuiApplication::platformName().compare(QStringLiteral("windows"),
                                                Qt::CaseInsensitive) != 0) {
        QSKIP("requires the Windows native window manager");
    }

    QWindow mainWindow;
    mainWindow.setFlags(Qt::Window | Qt::FramelessWindowHint);
    mainWindow.setGeometry(180, 120, 720, 280);
    QWindow listWindow;
    listWindow.setFlags(Qt::Window | Qt::FramelessWindowHint);
    listWindow.setGeometry(180, 398, 720, 420);

    WindowController windows;
    windows.setWindows(&mainWindow, nullptr);
    windows.setListWindow(&listWindow);
    windows.showListWindow();
    windows.snapListWindow(QStringLiteral("bottom"));
    QVERIFY(QTest::qWaitForWindowExposed(&mainWindow));
    QVERIFY(QTest::qWaitForWindowExposed(&listWindow));

    const QRect mainGeometry = mainWindow.geometry();
    const QRect listGeometry = listWindow.geometry();
    const HWND oldMainHandle = reinterpret_cast<HWND>(mainWindow.winId());
    QVERIFY(IsWindow(oldMainHandle));

    mainWindow.destroy();
    QTRY_VERIFY(!IsWindow(oldMainHandle));

    // Keep a native window alive while the main surface is recreated so
    // Windows cannot satisfy the test by recycling the old HWND value.
    QWindow handlePlaceholder;
    handlePlaceholder.setFlags(Qt::Tool | Qt::FramelessWindowHint);
    handlePlaceholder.create();
    QVERIFY(handlePlaceholder.handle() != nullptr);

    mainWindow.create();
    const HWND recreatedMainHandle =
        reinterpret_cast<HWND>(mainWindow.winId());
    QVERIFY(IsWindow(recreatedMainHandle));
    QVERIFY(recreatedMainHandle != oldMainHandle);

    constexpr LONG_PTR requiredStyle = WS_SYSMENU | WS_MINIMIZEBOX;
    const LONG_PTR recreatedStyle =
        GetWindowLongPtrW(recreatedMainHandle, GWL_STYLE);
    QCOMPARE(recreatedStyle & requiredStyle, requiredStyle);
    const LONG_PTR recreatedExtendedStyle =
        GetWindowLongPtrW(recreatedMainHandle, GWL_EXSTYLE);
    QVERIFY(recreatedExtendedStyle & WS_EX_APPWINDOW);
    QVERIFY(!(recreatedExtendedStyle & WS_EX_TOOLWINDOW));
    QCOMPARE(mainWindow.geometry(), mainGeometry);
    QCOMPARE(listWindow.geometry(), listGeometry);

    mainWindow.show();
    QVERIFY(QTest::qWaitForWindowExposed(&mainWindow));
    QTRY_VERIFY(listWindow.isVisible());
    SendMessageW(recreatedMainHandle, WM_SYSCOMMAND, SC_MINIMIZE, 0);
    QTRY_VERIFY(mainWindow.windowState() == Qt::WindowMinimized);
    QTRY_VERIFY(!listWindow.isVisible());

    SendMessageW(recreatedMainHandle, WM_SYSCOMMAND, SC_RESTORE, 0);
    QTRY_VERIFY(mainWindow.windowState() != Qt::WindowMinimized);
    QTRY_VERIFY(listWindow.isVisible());
    QCOMPARE(mainWindow.geometry(), mainGeometry);
    QCOMPARE(listWindow.geometry(), listGeometry);
}

void WindowControllerTest::taskbarCommandsToggleDockedGroupWithoutResizing()
{
    if (QGuiApplication::platformName().compare(QStringLiteral("windows"),
                                                Qt::CaseInsensitive) != 0) {
        QSKIP("requires the Windows native window manager");
    }

    QWindow mainWindow;
    mainWindow.setFlags(Qt::Window | Qt::FramelessWindowHint);
    mainWindow.setGeometry(180, 120, 720, 280);
    QWindow listWindow;
    listWindow.setFlags(Qt::Window | Qt::FramelessWindowHint);
    listWindow.setGeometry(180, 398, 720, 420);

    WindowController windows;
    windows.setWindows(&mainWindow, nullptr);
    windows.setListWindow(&listWindow);
    windows.showListWindow();
    windows.snapListWindow(QStringLiteral("bottom"));
    QVERIFY(QTest::qWaitForWindowExposed(&mainWindow));
    QVERIFY(QTest::qWaitForWindowExposed(&listWindow));

    const auto nativeRect = [](QWindow& window) {
        RECT rect{};
        if (!GetWindowRect(reinterpret_cast<HWND>(window.winId()), &rect)) {
            return QRect();
        }
        return QRect(rect.left, rect.top, rect.right - rect.left,
                     rect.bottom - rect.top);
    };
    const QRect mainBefore = nativeRect(mainWindow);
    const QRect listBefore = nativeRect(listWindow);
    const HWND mainHandle = reinterpret_cast<HWND>(mainWindow.winId());

    SendMessageW(mainHandle, WM_SYSCOMMAND, SC_MINIMIZE, 0);
    QTRY_VERIFY(mainWindow.windowState() == Qt::WindowMinimized);
    QTRY_VERIFY(!listWindow.isVisible());

    SendMessageW(mainHandle, WM_SYSCOMMAND, SC_RESTORE, 0);
    QTRY_VERIFY(mainWindow.windowState() != Qt::WindowMinimized);
    QTRY_VERIFY(listWindow.isVisible());
    QTRY_COMPARE(nativeRect(mainWindow), mainBefore);
    QTRY_COMPARE(nativeRect(listWindow), listBefore);

    const HWND listHandle = reinterpret_cast<HWND>(listWindow.winId());
    const auto isAbove = [](HWND candidate, HWND reference) {
        for (HWND current = GetTopWindow(nullptr); current != nullptr;
             current = GetWindow(current, GW_HWNDNEXT)) {
            if (current == candidate) return true;
            if (current == reference) return false;
        }
        return false;
    };
    QTRY_VERIFY(isAbove(listHandle, mainHandle));
}

void WindowControllerTest::taskbarToggleEntryPointMinimizesAndRestoresWindowGroup()
{
    WindowController windows;
    QWindow mainWindow;
    mainWindow.setFlags(Qt::Window | Qt::FramelessWindowHint);
    mainWindow.setGeometry(180, 120, 720, 280);
    QWindow listWindow;
    listWindow.setFlags(Qt::Window | Qt::FramelessWindowHint);
    listWindow.setGeometry(180, 398, 720, 420);
    windows.setWindows(&mainWindow, nullptr);
    windows.setListWindow(&listWindow);
    windows.showListWindow();
    windows.snapListWindow(QStringLiteral("bottom"));
    QVERIFY(QTest::qWaitForWindowExposed(&mainWindow));
    QVERIFY(QTest::qWaitForWindowExposed(&listWindow));
    mainWindow.requestActivate();
    QTRY_VERIFY(mainWindow.isActive());

    QVERIFY(QMetaObject::invokeMethod(&windows, "toggleMainWindowGroup",
                                      Qt::DirectConnection));
    QTRY_VERIFY(mainWindow.windowState() == Qt::WindowMinimized);
    QTRY_VERIFY(!listWindow.isVisible());

    QVERIFY(QMetaObject::invokeMethod(&windows, "toggleMainWindowGroup",
                                      Qt::DirectConnection));
    QTRY_VERIFY(mainWindow.windowState() != Qt::WindowMinimized);
    QTRY_VERIFY(listWindow.isVisible());
    QTRY_VERIFY(mainWindow.isActive());
}

void WindowControllerTest::taskbarToggleEntryPointActivatesBackgroundGroup()
{
    if (QGuiApplication::platformName().compare(QStringLiteral("windows"),
                                                Qt::CaseInsensitive) != 0) {
        QSKIP("requires the Windows native window manager");
    }

    WindowController windows;
    QWindow mainWindow;
    mainWindow.setFlags(Qt::Window | Qt::FramelessWindowHint);
    mainWindow.setGeometry(180, 120, 720, 280);
    QWindow otherWindow;
    otherWindow.setGeometry(960, 120, 320, 240);
    windows.setWindows(&mainWindow, nullptr);
    QVERIFY(QTest::qWaitForWindowExposed(&mainWindow));

    otherWindow.show();
    QVERIFY(QTest::qWaitForWindowExposed(&otherWindow));
    otherWindow.requestActivate();
    QTRY_VERIFY(otherWindow.isActive());
    QVERIFY(mainWindow.isVisible());
    QVERIFY(mainWindow.windowState() != Qt::WindowMinimized);

    QVERIFY(QMetaObject::invokeMethod(&windows, "toggleMainWindowGroup",
                                      Qt::DirectConnection));
    QTRY_VERIFY(mainWindow.isActive());
    QVERIFY(mainWindow.isVisible());
    QVERIFY(mainWindow.windowState() != Qt::WindowMinimized);
}

void WindowControllerTest::taskbarActivationDoesNotCancelMinimize()
{
    if (QGuiApplication::platformName().compare(QStringLiteral("windows"),
                                                Qt::CaseInsensitive) != 0) {
        QSKIP("requires the Windows native window manager");
    }

    QWindow mainWindow;
    mainWindow.setFlags(Qt::Window | Qt::FramelessWindowHint);
    mainWindow.setGeometry(180, 120, 720, 280);
    QWindow listWindow;
    listWindow.setFlags(Qt::Window | Qt::FramelessWindowHint);
    listWindow.setGeometry(180, 398, 720, 420);

    WindowController windows;
    windows.setWindows(&mainWindow, nullptr);
    windows.setListWindow(&listWindow);
    windows.showListWindow();
    windows.snapListWindow(QStringLiteral("bottom"));
    QVERIFY(QTest::qWaitForWindowExposed(&mainWindow));
    QVERIFY(QTest::qWaitForWindowExposed(&listWindow));

    const HWND mainHandle = reinterpret_cast<HWND>(mainWindow.winId());
    SendMessageW(mainHandle, WM_SYSCOMMAND, SC_MINIMIZE, 0);
    QTRY_VERIFY(mainWindow.windowState() == Qt::WindowMinimized);
    QTRY_VERIFY(!listWindow.isVisible());

    // Windows may deliver activation while processing a taskbar minimize.
    // That activation must never turn into a deferred show/restore request.
    QVERIFY(PostMessageW(mainHandle, WM_ACTIVATE, WA_ACTIVE, 0));
    QTest::qWait(100);
    QCOMPARE(mainWindow.windowState(), Qt::WindowMinimized);
    QVERIFY(!listWindow.isVisible());
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

void WindowControllerTest::mainFullscreenPreservesDockedListGeometry()
{
    QWindow mainWindow;
    mainWindow.setGeometry(80, 80, 700, 260);
    QWindow listWindow;
    listWindow.setGeometry(0, 0, 700, 320);
    WindowController windows;
    windows.setWindows(&mainWindow, nullptr);
    windows.setListWindow(&listWindow);
    windows.showListWindow();
    windows.snapListWindow(QStringLiteral("bottom"));
    const QRect normal = mainWindow.geometry();
    const int listHeight = listWindow.height();
    mainWindow.setWindowState(Qt::WindowFullScreen);
    QCoreApplication::processEvents();
    QVERIFY(!listWindow.isVisible());
    mainWindow.setWindowState(Qt::WindowNoState);
    mainWindow.setGeometry(normal);
    QCoreApplication::processEvents();
    QVERIFY(listWindow.isVisible());
    QCOMPARE(listWindow.width(), mainWindow.width());
    QCOMPARE(listWindow.x(), mainWindow.x());
    QCOMPARE(listWindow.height(), listHeight);
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

void WindowControllerTest::persistedClassicGeometrySurvivesReferenceDefaultChange()
{
    const QRect userGeometry(42, 84, 733, 347);
    QSettings settings;
    settings.setValue(QStringLiteral("windows/mainGeometry"), userGeometry);
    settings.sync();

    QWindow mainWindow;
    WindowController windows;
    windows.setWindows(&mainWindow, nullptr);

    QCOMPARE(mainWindow.geometry(), userGeometry);
    QCOMPARE(QSettings().value(QStringLiteral("windows/mainGeometry")).toRect(),
             userGeometry);
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
