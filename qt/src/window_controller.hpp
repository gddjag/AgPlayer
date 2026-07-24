#pragma once

#include <QObject>
#include <QPointer>

#include <functional>

class QWindow;

class WindowController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool mainVisible READ mainVisible NOTIFY mainVisibleChanged)
    Q_PROPERTY(bool miniVisible READ miniVisible NOTIFY miniVisibleChanged)
    Q_PROPERTY(bool audioToolsVisible READ audioToolsVisible NOTIFY audioToolsVisibleChanged)
    Q_PROPERTY(bool alwaysOnTop READ alwaysOnTop WRITE setAlwaysOnTop NOTIFY alwaysOnTopChanged)
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
        std::function<void()> quitApplication;
    };

    explicit WindowController(QObject* parent = nullptr);
    explicit WindowController(ShutdownActions actions, QObject* parent = nullptr);
    ~WindowController();

    bool mainVisible() const noexcept;
    bool miniVisible() const noexcept;
    bool audioToolsVisible() const noexcept;
    bool alwaysOnTop() const noexcept;
    bool listWindowVisible() const noexcept;
    bool listWindowDetached() const noexcept;
    int listWindowX() const noexcept;
    int listWindowY() const noexcept;
    int listWindowWidth() const noexcept;
    int listWindowHeight() const noexcept;

    void setWindows(QWindow* mainWindow, QWindow* miniWindow);
    void setListWindow(QWindow* listWindow);
    void setAudioToolsWindow(QWindow* audioToolsWindow);
    void setMainReady(bool ready) noexcept;
    void setMiniReady(bool ready) noexcept;
    void setShutdownActions(ShutdownActions actions);
    void setListWindowDetached(bool detached);
    void setListWindowX(int x) noexcept;
    void setListWindowY(int y) noexcept;
    void setListWindowWidth(int width) noexcept;
    void setListWindowHeight(int height) noexcept;

    Q_INVOKABLE void showMini();
    Q_INVOKABLE void showMain();
    Q_INVOKABLE void showAudioTools();
    Q_INVOKABLE void hideAudioTools();
    Q_INVOKABLE void requestClose();
    Q_INVOKABLE void setAlwaysOnTop(bool alwaysOnTop);
    Q_INVOKABLE void showListWindow();
    Q_INVOKABLE void hideListWindow();
    Q_INVOKABLE void toggleListWindow();
    Q_INVOKABLE void moveListWindow(int x, int y);
    Q_INVOKABLE void snapListWindow(const QString& direction);
    Q_INVOKABLE void activateSearch();
    Q_INVOKABLE void toggleMiniPlayer();

signals:
    void mainVisibleChanged();
    void miniVisibleChanged();
    void audioToolsVisibleChanged();
    void alwaysOnTopChanged();
    void listWindowVisibleChanged();
    void listWindowDetachedChanged();
    void listWindowXChanged();
    void listWindowYChanged();
    void listWindowWidthChanged();
    void listWindowHeightChanged();
    void searchRequested();

private:
    enum class PendingView { None, Main, Mini };

    void applyMainVisible(bool visible);
    void applyMiniVisible(bool visible);
    void applyAudioToolsVisible(bool visible);
    void applyListWindowVisible(bool visible);
    void applyListWindowDetached(bool detached);
    void updateListWindowPosition();
    QPoint computeSnappedPosition(int x, int y) const;
    QPoint computeSnapForEdge(const QString& direction) const;

    QPointer<QWindow> mainWindow_;
    QPointer<QWindow> miniWindow_;
    QPointer<QWindow> listWindow_;
    QPointer<QWindow> audioToolsWindow_;
    ShutdownActions shutdownActions_;
    bool mainVisible_ = true;
    bool miniVisible_ = false;
    bool audioToolsVisible_ = false;
    bool alwaysOnTop_ = false;
    bool mainReady_ = true;
    bool miniReady_ = true;
    bool shutdownRequested_ = false;
    bool listWindowVisible_ = false;
    bool listWindowDetached_ = true;
    int listWindowX_ = 0;
    int listWindowY_ = 0;
    int listWindowWidth_ = 1000;
    int listWindowHeight_ = 420;
    bool listWindowGeometryInitialized_ = false;
    PendingView pendingView_ = PendingView::None;
};
