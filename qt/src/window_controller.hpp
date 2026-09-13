#pragma once

#include <QObject>
#include <QAbstractNativeEventFilter>
#include <QList>
#include <QPointer>
#include <QRect>
#include <QSet>
#include <QSettings>
#include <QSize>
#include <QTimer>

#include <functional>

class QWindow;

class WindowController final : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
    Q_PROPERTY(bool mainVisible READ mainVisible NOTIFY mainVisibleChanged)
    Q_PROPERTY(int mainWindowShellMode READ mainWindowShellMode NOTIFY mainWindowShellModeChanged)
    Q_PROPERTY(QRect mainWindowGeometry READ mainWindowGeometry
                   NOTIFY mainWindowGeometryChanged)
    Q_PROPERTY(bool miniVisible READ miniVisible NOTIFY miniVisibleChanged)
    Q_PROPERTY(bool immersivePresentationActive READ immersivePresentationActive
                   NOTIFY immersivePresentationActiveChanged)
    Q_PROPERTY(bool audioToolsVisible READ audioToolsVisible NOTIFY audioToolsVisibleChanged)
    Q_PROPERTY(bool alwaysOnTop READ alwaysOnTop WRITE setAlwaysOnTop NOTIFY alwaysOnTopChanged)
    Q_PROPERTY(bool magneticSnapEnabled READ magneticSnapEnabled WRITE setMagneticSnapEnabled
                   NOTIFY magneticSnapEnabledChanged)
    Q_PROPERTY(int preferredDockEdge READ preferredDockEdge WRITE setPreferredDockEdge
                   NOTIFY preferredDockEdgeChanged)
    Q_PROPERTY(QString listDockEdge READ listDockEdge NOTIFY listDockEdgeChanged)
    Q_PROPERTY(QString snapPreviewEdge READ snapPreviewEdge NOTIFY snapPreviewEdgeChanged)
    Q_PROPERTY(int closeBehavior READ closeBehavior WRITE setCloseBehavior
                   NOTIFY closeBehaviorChanged)
    Q_PROPERTY(bool listWindowVisible READ listWindowVisible NOTIFY listWindowVisibleChanged)
    Q_PROPERTY(bool listWindowDetached READ listWindowDetached WRITE setListWindowDetached NOTIFY
                   listWindowDetachedChanged)
    Q_PROPERTY(int listWindowX READ listWindowX WRITE setListWindowX NOTIFY listWindowXChanged)
    Q_PROPERTY(int listWindowY READ listWindowY WRITE setListWindowY NOTIFY listWindowYChanged)
    Q_PROPERTY(int listWindowWidth READ listWindowWidth WRITE setListWindowWidth NOTIFY
                   listWindowWidthChanged)
    Q_PROPERTY(int listWindowHeight READ listWindowHeight WRITE setListWindowHeight NOTIFY
                   listWindowHeightChanged)

public:
    struct ShutdownActions {
        std::function<void()> cancelWaveform;
        std::function<void()> stopPlayback;
        std::function<void()> flushLibrary;
        std::function<void()> releaseCore;
        std::function<void()> minimizeToTray;
        std::function<void()> quitApplication;
    };

    explicit WindowController(QObject* parent = nullptr);
    explicit WindowController(ShutdownActions actions, QObject* parent = nullptr);
    ~WindowController();

    bool mainVisible() const noexcept;
    int mainWindowShellMode() const noexcept { return mainWindowShellMode_; }
    QRect mainWindowGeometry() const noexcept;
    bool miniVisible() const noexcept;
    bool immersivePresentationActive() const noexcept;
    bool audioToolsVisible() const noexcept;
    bool alwaysOnTop() const noexcept;
    bool magneticSnapEnabled() const noexcept;
    int preferredDockEdge() const noexcept;
    QString listDockEdge() const;
    QString snapPreviewEdge() const;
    int closeBehavior() const noexcept;
    bool listWindowVisible() const noexcept;
    bool listWindowDetached() const noexcept;
    int listWindowX() const noexcept;
    int listWindowY() const noexcept;
    int listWindowWidth() const noexcept;
    int listWindowHeight() const noexcept;
    static QRect geometryForDpiChange(const QRect& currentGeometry,
                                      const QRect& suggestedGeometry);
    static QRect geometryForDpiChange(const QRect& currentNativeGeometry,
                                      qreal currentDpr,
                                      const QRect& suggestedNativeGeometry,
                                      qreal targetDpr,
                                      const QRect& targetAvailableGeometry);
    static QRect geometryForAvailableScreens(
        const QRect& savedGeometry, const QSize& minimumSize,
        const QList<QRect>& availableScreens, int primaryScreenIndex);

    void setWindows(QWindow* mainWindow, QWindow* miniWindow);
    void setMainWindowShellMode(int mode);
    void setListWindow(QWindow* listWindow);
    void setAudioToolsWindow(QWindow* audioToolsWindow);
    void setMainReady(bool ready) noexcept;
    void setMiniReady(bool ready) noexcept;
    void setShutdownActions(ShutdownActions actions);
    void setMagneticSnapEnabled(bool enabled);
    void setPreferredDockEdge(int edge);
    void setCloseBehavior(int behavior);
    void setListWindowPanelAllowed(bool allowed);
    void setListWindowDetached(bool detached);
    void setListWindowX(int x) noexcept;
    void setListWindowY(int y) noexcept;
    void setListWindowWidth(int width) noexcept;
    void setListWindowHeight(int height) noexcept;

    Q_INVOKABLE void showMini();
    Q_INVOKABLE void showMain();
    void restoreApplicationWindows();
    Q_INVOKABLE QRect availableGeometryForWindow(QWindow* window) const;
    Q_INVOKABLE void toggleMainWindowGroup();
    Q_INVOKABLE void enterImmersivePresentation();
    Q_INVOKABLE void leaveImmersivePresentation();
    Q_INVOKABLE void showAudioTools();
    Q_INVOKABLE void hideAudioTools();
    Q_INVOKABLE void requestClose();
    Q_INVOKABLE void requestExit();
    Q_INVOKABLE void setAlwaysOnTop(bool alwaysOnTop);
    Q_INVOKABLE void showListWindow();
    Q_INVOKABLE void hideListWindow();
    Q_INVOKABLE void toggleListWindow();
    Q_INVOKABLE void moveListWindow(int x, int y);
    Q_INVOKABLE void snapListWindow(const QString& direction);
    Q_INVOKABLE void activateSearch();
    Q_INVOKABLE void toggleMiniPlayer();
    Q_INVOKABLE void registerSettingsWindow(QWindow* window);
    Q_INVOKABLE void presentAuxiliaryWindow(QWindow* window);
    Q_INVOKABLE void finishListWindowInteraction();

signals:
    void mainVisibleChanged();
    void mainWindowShellModeChanged();
    void mainWindowGeometryChanged();
    void miniVisibleChanged();
    void immersivePresentationActiveChanged();
    void audioToolsVisibleChanged();
    void alwaysOnTopChanged();
    void magneticSnapEnabledChanged();
    void preferredDockEdgeChanged();
    void listDockEdgeChanged();
    void snapPreviewEdgeChanged();
    void closeBehaviorChanged();
    void listWindowVisibleChanged();
    void listWindowDetachedChanged();
    void listWindowXChanged();
    void listWindowYChanged();
    void listWindowWidthChanged();
    void listWindowHeightChanged();
    void searchRequested();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    bool nativeEventFilter(const QByteArray& eventType, void* message,
                           qintptr* result) override;

private:
    enum class PendingView { None, Main, Mini };

    void applyMainVisible(bool visible);
    void applyMiniVisible(bool visible);
    void applyAudioToolsVisible(bool visible);
    void applyListWindowVisible(bool visible);
    void applyListWindowDetached(bool detached);
    void shutdown();
    bool shouldShowListWindow() const;
    void repositionDockedListWindow();
    void setListDockEdge(const QString& edge);
    QString snapEdgeForPosition(int x, int y) const;
    void loadPersistedWindowState();
    void loadPersistedListWindowState();
    void persistListWindowState();
    bool restoreGeometry(QWindow* window, const QString& key);
    void persistGeometry(QWindow* window, const QString& key);
    void scheduleWindowStateSync();
    void flushWindowState();
    QString mainWindowGeometryKey() const;
    QString listWindowGeometryKey() const;
    QString listWindowGeometryVersionKey() const;
    QString listWindowRequestedVisibleKey() const;
    QString listWindowDockEdgeKey() const;
    bool restoreMainWindowGeometry(QWindow* window,
                                   bool applyClassicDefault = false);
    static QString edgeForPreference(int edge);
    void updateListWindowPosition();
    QPoint computeSnappedPosition(int x, int y) const;
    QPoint computeSnapForEdge(const QString& direction) const;
    void applyPlatformWindowStyle(QWindow* window) const;
#ifdef Q_OS_WIN
    void ensureTaskbarWindowStyles(QWindow* window) const;
#endif
    void raiseDockedGroup(QWindow* topWindow = nullptr);
    void rememberNativePixelSize(QWindow* window);

    QPointer<QWindow> mainWindow_;
    quintptr mainWindowHandle_ = 0;
    QSize mainNativePixelSize_;
    qreal mainTrackedDpr_ = 1.0;
    QPointer<QWindow> miniWindow_;
    QPointer<QWindow> listWindow_;
    quintptr listWindowHandle_ = 0;
    QSize listNativePixelSize_;
    qreal listTrackedDpr_ = 1.0;
    QPointer<QWindow> audioToolsWindow_;
    quintptr audioToolsWindowHandle_ = 0;
    qreal audioToolsTrackedDpr_ = 1.0;
    QPointer<QWindow> settingsWindow_;
    quintptr settingsWindowHandle_ = 0;
    QSize settingsNativePixelSize_;
    qreal settingsTrackedDpr_ = 1.0;
    QPointer<QWindow> lastAuxiliaryWindow_;
    QSet<QWindow*> positionedAuxiliaryWindows_;
    ShutdownActions shutdownActions_;
    QSettings settings_;
    QTimer windowStateSyncTimer_;
    bool mainVisible_ = true;
    int mainWindowShellMode_ = 0;
    bool miniVisible_ = false;
    bool immersivePresentationActive_ = false;
    PendingView immersiveRestoreView_ = PendingView::Main;
    int immersiveRestoreMainWindowShellMode_ = 0;
    int immersiveDeferredMainWindowShellMode_ = 0;
    bool immersiveDeferredMainWindowShellModeRequested_ = false;
    QRect immersiveRestoreMainGeometry_;
    QRect immersiveRestoreMiniGeometry_;
    QString immersiveRestoreMainGeometryKey_;
    bool immersiveRestoreMainGeometryWasPersisted_ = true;
    bool immersiveRestoreMiniGeometryWasPersisted_ = true;
    bool audioToolsVisible_ = false;
    bool alwaysOnTop_ = false;
    bool magneticSnapEnabled_ = true;
    int preferredDockEdge_ = 1;
    QString listDockEdge_ = QStringLiteral("bottom");
    int closeBehavior_ = 0;
    bool mainReady_ = true;
    bool miniReady_ = true;
    bool shutdownRequested_ = false;
    bool listWindowVisible_ = false;
    bool listWindowRequestedVisible_ = false;
    bool listWindowPanelAllowed_ = true;
    bool listWindowDetached_ = false;
    int listWindowX_ = 0;
    int listWindowY_ = 0;
    int listWindowWidth_ = 960;
    int listWindowHeight_ = 592;
    bool listWindowGeometryInitialized_ = false;
    bool updatingWindowGeometry_ = false;
    bool updatingWindowZOrder_ = false;
    bool listUserInteraction_ = false;
    QString pendingListSnapEdge_;
    bool pendingListDetach_ = false;
    bool preferredDockEdgeInitialized_ = false;
    bool hasPersistedDockEdge_ = false;
    PendingView pendingView_ = PendingView::None;
};
