#include <QAction>
#include <QApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QImage>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMenu>
#ifdef Q_OS_MACOS
#include <QMenuBar>
#include <QKeySequence>
#endif
#include <QQuickWindow>
#include <QQuickItem>
#include <QQuickStyle>
#include <QSettings>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStyle>
#include <QStringList>
#include <QSystemTrayIcon>
#include <QTemporaryDir>
#include <QTimer>
#include <QElapsedTimer>
#include <QPointer>
#include <QSet>
#include <QWindow>
#include <QtPlugin>

#include <agplayer/c_api.h>

#include "agplayer_version.hpp"
#include "agplayer_application.hpp"

#include <algorithm>
#include <cstdio>
#include <functional>
#include <memory>
#include <utility>

#ifdef Q_OS_WIN
#include <windows.h>
#include <propkey.h>
#include <shobjidl.h>
#endif

#include "audio_tools_controller.hpp"
#include "audio_file_discovery.hpp"
#include "audio_visual_feature_controller.hpp"
#include "terrain_reactor_item.hpp"
#include "audio_editor/audio_editor_controller.hpp"
#include "audio_editor/playback_clip_drag_adapter.hpp"
#include "audio_preview_controller.hpp"
#include "equalizer_controller.hpp"
#include "filename_processor.hpp"
#include "format_converter.hpp"
#include "global_hotkey_manager.hpp"
#include "import_controller.hpp"
#include "library_model.hpp"
#include "lyrics_service.hpp"
#include "resource_folder_controller.hpp"
#include "library_navigation_model.hpp"
#include "library_store.hpp"
#include "metadata_editor.hpp"
#include "native_drop_router.hpp"
#include "playback_controller.hpp"
#include "player_experience_controller.hpp"
#include "immersive_theme_catalog.hpp"
#include "playback_state_store.hpp"
#include "playlist_model.hpp"
#include "qml_registration.hpp"
#include "runtime_log.hpp"
#include "rename_journal_store.hpp"
#include "settings_controller.hpp"
#include "tag_model.hpp"
#include "track_waveform_thumbnail_provider.hpp"
#include "translation_manager.hpp"
#include "waveform_provider.hpp"
#include "vocal_separation_controller.hpp"
#include "lossless_analysis_controller.hpp"
#include "video_playback_controller.hpp"
#include "window_controller.hpp"

Q_IMPORT_PLUGIN(AgPlayerPlugin)

ProbeResult probeMetadata(const QString& requestedPath, bool analyzeBpm);

#ifdef Q_OS_WIN
namespace {

constexpr wchar_t kAgPlayerAppUserModelId[] = L"AgPlayer.Desktop";
constexpr int kAgPlayerIconResourceId = 101;

struct NativeWindowIcons final {
    HICON bigIcon = nullptr;
    HICON smallIcon = nullptr;
};

NativeWindowIcons loadNativeWindowIcons()
{
    const HINSTANCE module = GetModuleHandleW(nullptr);
    return {
        static_cast<HICON>(LoadImageW(module,
            MAKEINTRESOURCEW(kAgPlayerIconResourceId), IMAGE_ICON,
            GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON),
            LR_DEFAULTCOLOR)),
        static_cast<HICON>(LoadImageW(module,
            MAKEINTRESOURCEW(kAgPlayerIconResourceId), IMAGE_ICON,
            GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
            LR_DEFAULTCOLOR))
    };
}

void publishNativeWindowIcons(const HWND hwnd,
                              const NativeWindowIcons& icons)
{
    if (icons.bigIcon != nullptr) {
        SendMessageW(hwnd, WM_SETICON, ICON_BIG,
                     reinterpret_cast<LPARAM>(icons.bigIcon));
    }
    if (icons.smallIcon != nullptr) {
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL,
                     reinterpret_cast<LPARAM>(icons.smallIcon));
    }
    // Read back the values here as a runtime contract: taskbar integration
    // must never silently depend on the tray icon or a Qt image provider.
    const auto bigResult = SendMessageW(hwnd, WM_GETICON, ICON_BIG, 0);
    const auto smallResult = SendMessageW(hwnd, WM_GETICON, ICON_SMALL, 0);
    if (bigResult == 0 || smallResult == 0) {
        qWarning("AgPlayer native window icon publication failed");
    }
}

HRESULT setWindowStringProperty(IPropertyStore* properties,
                                const PROPERTYKEY& key,
                                const QString& value)
{
    const std::wstring storage = value.toStdWString();
    PROPVARIANT property{};
    property.vt = VT_LPWSTR;
    property.pwszVal = const_cast<wchar_t*>(storage.c_str());
    return properties->SetValue(key, property);
}

QString windowStringProperty(IPropertyStore* properties,
                             const PROPERTYKEY& key)
{
    PROPVARIANT property{};
    if (FAILED(properties->GetValue(key, &property))) {
        return {};
    }
    const QString value = property.vt == VT_LPWSTR
            && property.pwszVal != nullptr
        ? QString::fromWCharArray(property.pwszVal)
        : QString();
    PropVariantClear(&property);
    return value;
}

void applyWindowsShellIdentity(QWindow* window, const QIcon& icon,
                               const NativeWindowIcons& nativeIcons)
{
    // WinIdChange also arrives when a native window is destroyed. winId()
    // would create a new platform window in that state, including during
    // QQuickWindow teardown; shell metadata must only decorate a live HWND.
    if (window == nullptr || window->handle() == nullptr) {
        return;
    }
    const auto* quickWindow = qobject_cast<QQuickWindow*>(window);
    if (quickWindow && !quickWindow->contentItem()) {
        return;
    }
    window->setIcon(icon);
    const HWND hwnd = reinterpret_cast<HWND>(window->winId());
    if (hwnd == nullptr) {
        return;
    }
    publishNativeWindowIcons(hwnd, nativeIcons);

    IPropertyStore* properties = nullptr;
    if (FAILED(SHGetPropertyStoreForWindow(
            hwnd, __uuidof(IPropertyStore),
            reinterpret_cast<void**>(&properties)))) {
        return;
    }
    const QString executable = QDir::toNativeSeparators(
        QCoreApplication::applicationFilePath());
    const QString relaunchCommand = QStringLiteral("\"") + executable
        + QStringLiteral("\"");
    const QString displayNameResource = QStringLiteral("@") + executable
        + QStringLiteral(",-102");
    const QString iconResource = executable + QStringLiteral(",-101");

    const HRESULT identityResult = setWindowStringProperty(
        properties, PKEY_AppUserModel_ID,
        QString::fromWCharArray(kAgPlayerAppUserModelId));
    const HRESULT commandResult = setWindowStringProperty(
        properties, PKEY_AppUserModel_RelaunchCommand, relaunchCommand);
    const HRESULT displayResult = setWindowStringProperty(
        properties, PKEY_AppUserModel_RelaunchDisplayNameResource,
        displayNameResource);
    const HRESULT iconResult = setWindowStringProperty(
        properties, PKEY_AppUserModel_RelaunchIconResource, iconResource);
    if (SUCCEEDED(identityResult) && SUCCEEDED(commandResult)
        && SUCCEEDED(displayResult) && SUCCEEDED(iconResult)
        && SUCCEEDED(properties->Commit())) {
        const QString identity = windowStringProperty(
            properties, PKEY_AppUserModel_ID);
        const QString storedCommand = windowStringProperty(
            properties, PKEY_AppUserModel_RelaunchCommand);
        const QString storedDisplay = windowStringProperty(
            properties, PKEY_AppUserModel_RelaunchDisplayNameResource);
        const QString storedIcon = windowStringProperty(
            properties, PKEY_AppUserModel_RelaunchIconResource);
        if (identity != QString::fromWCharArray(kAgPlayerAppUserModelId)
            || storedCommand.isEmpty() || storedDisplay.isEmpty()
            || storedIcon.isEmpty()) {
            qWarning("AgPlayer native taskbar identity publication failed");
        } else {
            if (qEnvironmentVariableIntValue("AGPLAYER_QA_SHELL_PROBE") == 1) {
                std::fputs("AgPlayer native taskbar identity verified\n",
                           stderr);
                std::fflush(stderr);
            }
        }
    } else {
        qWarning("AgPlayer native taskbar property commit failed");
    }
    properties->Release();
}

#if QT_VERSION == QT_VERSION_CHECK(6, 7, 0)
class QuickWindowDpiTeardownGuard final : public QObject {
protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        // Qt 6.7.0 forcePolish() dereferences a destroyed contentItem during
        // late DPI notifications. Live windows must still receive DPI changes.
        if (event->type() == QEvent::DevicePixelRatioChange) {
            const auto* window = qobject_cast<QQuickWindow*>(watched);
            if (window && !window->contentItem())
                return true;
        }
        return QObject::eventFilter(watched, event);
    }
};
#endif

class WindowsShellIdentityFilter final : public QObject {
public:
    explicit WindowsShellIdentityFilter(QIcon icon,
                                        NativeWindowIcons nativeIcons,
                                        QObject* parent = nullptr)
        : QObject(parent), icon_(std::move(icon)), native_icons_(nativeIcons)
    {
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::Show
            || event->type() == QEvent::WinIdChange) {
            applyWindowsShellIdentity(qobject_cast<QWindow*>(watched), icon_,
                                      native_icons_);
        }
        return false;
    }

private:
    QIcon icon_;
    NativeWindowIcons native_icons_;
};

} // namespace
#endif

int main(int argc, char* argv[])
{
#ifdef Q_OS_WIN
    // The application ships ahead-of-time compiled QML in its executable.  A
    // stale per-user disk cache can otherwise keep an older control tree after
    // an upgrade (for example, the former ten-band equalizer).  Prefer the
    // versioned resources from this build; this does not disable the compiled
    // QML cache embedded by qt_add_qml_module.
    qputenv("QML_DISABLE_DISK_CACHE", QByteArrayLiteral("1"));
#endif
#ifdef Q_OS_WIN
    // Must be set before Qt creates any native window so taskbar grouping and
    // the installed shortcut resolve to the same stable application identity.
    SetCurrentProcessExplicitAppUserModelID(L"AgPlayer.Desktop");
#endif
    // The application supplies its own control visuals. A non-native style
    // keeps those visuals supported and consistent on Windows/macOS while
    // Theme.qml still follows the host system palette when requested.
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    AgPlayerApplication app(argc, argv);
#if defined(Q_OS_WIN) && QT_VERSION == QT_VERSION_CHECK(6, 7, 0)
    // Declared before the QML engine so the guard covers its entire teardown.
    QuickWindowDpiTeardownGuard dpiTeardownGuard;
    app.installEventFilter(&dpiTeardownGuard);
#endif
#ifdef Q_OS_WIN
    QFont interfaceFont = app.font();
    interfaceFont.setFamilies({QStringLiteral("Microsoft YaHei UI"),
                               QStringLiteral("Segoe UI Variable"),
                               QStringLiteral("Segoe UI")});
    app.setFont(interfaceFont);
#endif
    app.setApplicationName(QStringLiteral("AgPlayer"));
    app.setApplicationVersion(
        QString::fromLatin1(agplayer::version::kVersion));
    app.setOrganizationName(QStringLiteral("AgPlayer"));
    const QIcon applicationIcon(QStringLiteral(
        ":/qt/qml/AgPlayer/assets/brand/agplayer.ico"));
    app.setWindowIcon(applicationIcon);
#ifdef Q_OS_WIN
    WindowsShellIdentityFilter shellIdentityFilter(
        applicationIcon, loadNativeWindowIcons(), &app);
    app.installEventFilter(&shellIdentityFilter);
    if (qEnvironmentVariableIntValue("AGPLAYER_QA_SHELL_PROBE") == 1
        && QCoreApplication::arguments().contains(QStringLiteral("--qa-test-mode"))) {
        // Exercise the production filter with a real, already-destroyed HWND.
        // Deliver the late notification explicitly so this regression does not
        // depend on monitor layout or platform teardown notification timing.
        QWindow probeWindow;
        probeWindow.create();
        const HWND originalHwnd = probeWindow.handle()
            ? reinterpret_cast<HWND>(probeWindow.winId()) : nullptr;
        const bool nativeCreated = originalHwnd && IsWindow(originalHwnd);
        app.removeEventFilter(&shellIdentityFilter);
        probeWindow.destroy();
        const bool nativeDestroyed = !probeWindow.handle()
            && originalHwnd && !IsWindow(originalHwnd);
        app.installEventFilter(&shellIdentityFilter);
        QEvent lateWinIdChange(QEvent::WinIdChange);
        QCoreApplication::sendEvent(&probeWindow, &lateWinIdChange);
        const bool stayedDestroyed = !probeWindow.handle();
        // Clean up even on RED without allowing another shell notification to
        // interfere with the assertion or the test's own object destruction.
        app.removeEventFilter(&shellIdentityFilter);
        probeWindow.destroy();
        app.installEventFilter(&shellIdentityFilter);
        const bool passed = nativeCreated && nativeDestroyed && stayedDestroyed;
        std::fprintf(stderr,
            "AgPlayer shell teardown probe: %s created=%d destroyed=%d stayedDestroyed=%d\n",
            passed ? "passed" : "failed", int(nativeCreated),
            int(nativeDestroyed), int(stayedDestroyed));
        std::fflush(stderr);
        if (!passed)
            return 7;
    }
#endif

    // Development-only QA arguments. Parsed before ag_player_create so the
    // production player instance is reused (controllers are never bypassed).
    //   --qa-test-mode              isolate QStandardPaths from user data
    //   --qa-log <path>             write the runtime log to an explicit path
    //   --qa-play <path>            load + play a file through the normal path
    //   --qa-video-fullscreen       enter video fullscreen through Main.qml
    //   --qa-exit-after-ms <ms>     fail-safe timed exit for acceptance runs
    //   --qa-screenshot-main <png>  grab the main window after playback starts
    //   --qa-screenshot-mini <png>  grab the mini player window likewise
    //   --qa-waveform-mode <0..3>   override waveform mode for visual QA
    //   --qa-tool <0..5>             choose the audio-tool screenshot page
    //   --qa-settings-section <0..6> capture one settings section
    //   --qa-equalizer-size <w> <h> resize the EQ visual target
    //   --qa-tag <name>              seed a tag in --qa-test-mode only
    //   --qa-selected-tag <name>     select a seeded tag in --qa-test-mode only
    //   --qa-immersive-theme <id>  select an original theme in QA mode
    //   --qa-immersive-stability    log renderer counters (requires QA test mode)
    //   --qa-lyric-placement <0..2>  enable and position QA lyrics
    //   --qa-capture-delay-ms <ms>  QA capture delay, 2500..60000
    bool qaTestMode = false;
    QString qaLogPath;
    QString qaPlayPath;
    bool qaVideoFullscreen = false;
    int qaExitAfterMs = 0;
    QString qaScreenshotMain;
    QString qaPlayerShell;
    int qaWaveformMode = -1;
    int qaMainWidth = 0;
    int qaMainHeight = 0;
    QString qaImmersiveTheme;
    bool qaImmersiveThemeRequested = false;
    int qaLyricPlacement = -1;
    int qaCaptureDelayMs = 2500;
    bool qaIntegratedShellLifecycleProbe = false;
    bool qaImmersiveStability = false;
    QString qaScreenshotMini;
    QString qaScreenshotTools;
    int qaTool = 0;
    QSize qaToolsSize;
    QString qaScreenshotList;
    QString qaListCategory;
    QStringList qaSeedTags;
    QString qaSelectedTag;
    QString qaTheme;
    QString qaLanguage;
    bool qaOpenSettings = false;
    int qaSettingsSection = -1;
    bool qaOpenEqualizer = false;
    QSize qaEqualizerSize;
    QString qaLibraryPath;
    QString qaImportFolder;
    qint64 qaEditorSelectionStartMs = -1;
    qint64 qaEditorSelectionEndMs = -1;
    qint64 qaPlaybackSelectionStartMs = -1;
    qint64 qaPlaybackSelectionEndMs = -1;
    qint64 qaEditorPlayheadMs = -1;
    bool qaEditorReferenceState = false;
    QString initialFilePath;
    QString instanceKeySuffix;
    {
        const QStringList cliArgs = QGuiApplication::arguments();
        for (int i = 1; i < cliArgs.size(); ++i) {
            const QString& arg = cliArgs.at(i);
            if (arg == QStringLiteral("--qa-test-mode")) {
                qaTestMode = true;
            } else if (arg == QStringLiteral("--qa-log") && i + 1 < cliArgs.size()) {
                qaLogPath = cliArgs.at(++i);
            } else if (arg == QStringLiteral("--qa-play") && i + 1 < cliArgs.size()) {
                qaPlayPath = cliArgs.at(++i);
            } else if (arg == QStringLiteral("--qa-video-fullscreen")) {
                qaVideoFullscreen = true;
            } else if (arg == QStringLiteral("--qa-exit-after-ms")
                       && i + 1 < cliArgs.size()) {
                bool ok = false;
                const int value = cliArgs.at(++i).toInt(&ok);
                if (ok && value > 0) qaExitAfterMs = value;
            } else if (arg == QStringLiteral("--qa-screenshot-main")
                       && i + 1 < cliArgs.size()) {
                qaScreenshotMain = cliArgs.at(++i);
            } else if (arg == QStringLiteral("--qa-player-shell")
                       && i + 1 < cliArgs.size()) {
                qaPlayerShell = cliArgs.at(++i).toLower();
            } else if (arg == QStringLiteral("--qa-waveform-mode")
                       && i + 1 < cliArgs.size()) {
                bool ok = false;
                const int mode = cliArgs.at(++i).toInt(&ok);
                if (ok && mode >= 0 && mode <= 3) qaWaveformMode = mode;
            } else if (arg == QStringLiteral("--qa-width")
                       && i + 1 < cliArgs.size()) {
                bool ok = false;
                const int value = cliArgs.at(++i).toInt(&ok);
                if (ok && value > 0) qaMainWidth = value;
            } else if (arg == QStringLiteral("--qa-height")
                       && i + 1 < cliArgs.size()) {
                bool ok = false;
                const int value = cliArgs.at(++i).toInt(&ok);
                if (ok && value > 0) qaMainHeight = value;
            } else if (arg == QStringLiteral("--qa-immersive-theme")) {
                qaImmersiveThemeRequested = true;
                qaImmersiveTheme.clear();
                if (i + 1 < cliArgs.size() && !cliArgs.at(i + 1).startsWith('-'))
                    qaImmersiveTheme = cliArgs.at(++i);
            } else if (arg == QStringLiteral("--qa-lyric-placement")
                       && i + 1 < cliArgs.size()) {
                bool ok = false;
                const int value = cliArgs.at(++i).toInt(&ok);
                if (ok && value >= 0 && value <= 2) qaLyricPlacement = value;
            } else if (arg == QStringLiteral("--qa-capture-delay-ms")
                       && i + 1 < cliArgs.size()) {
                bool ok = false;
                const int value = cliArgs.at(++i).toInt(&ok);
                if (ok) qaCaptureDelayMs = std::clamp(value, 2500, 60000);
            } else if (arg == QStringLiteral("--qa-integrated-shell-lifecycle-probe")) {
                qaIntegratedShellLifecycleProbe = true;
            } else if (arg == QStringLiteral("--qa-immersive-stability")) {
                qaImmersiveStability = true;
            } else if (arg == QStringLiteral("--qa-screenshot-mini")
                       && i + 1 < cliArgs.size()) {
                qaScreenshotMini = cliArgs.at(++i);
            } else if (arg == QStringLiteral("--qa-screenshot-tools")
                       && i + 1 < cliArgs.size()) {
                qaScreenshotTools = cliArgs.at(++i);
            } else if (arg == QStringLiteral("--qa-tool")
                       && i + 1 < cliArgs.size()) {
                bool ok = false;
                const int requestedTool = cliArgs.at(++i).toInt(&ok);
                if (ok && requestedTool >= 0 && requestedTool <= 5) {
                    qaTool = requestedTool;
                }
            } else if (arg == QStringLiteral("--qa-tools-size")
                       && i + 2 < cliArgs.size()) {
                bool widthOk = false;
                bool heightOk = false;
                const int width = cliArgs.at(++i).toInt(&widthOk);
                const int height = cliArgs.at(++i).toInt(&heightOk);
                if (widthOk && heightOk && width >= 880 && height >= 560) {
                    qaToolsSize = QSize(width, height);
                }
            } else if (arg == QStringLiteral("--qa-screenshot-list")
                       && i + 1 < cliArgs.size()) {
                qaScreenshotList = cliArgs.at(++i);
            } else if (arg == QStringLiteral("--qa-list-category")
                       && i + 1 < cliArgs.size()) {
                qaListCategory = cliArgs.at(++i).toLower();
            } else if (arg == QStringLiteral("--qa-tag") && i + 1 < cliArgs.size()) {
                qaSeedTags.append(cliArgs.at(++i));
            } else if (arg == QStringLiteral("--qa-selected-tag")
                       && i + 1 < cliArgs.size()) {
                qaSelectedTag = cliArgs.at(++i);
            } else if (arg == QStringLiteral("--qa-theme")
                       && i + 1 < cliArgs.size()) {
                qaTheme = cliArgs.at(++i).toLower();
            } else if (arg == QStringLiteral("--qa-language")
                       && i + 1 < cliArgs.size()) {
                qaLanguage = cliArgs.at(++i).toLower();
            } else if (arg == QStringLiteral("--qa-open-settings")) {
                qaOpenSettings = true;
            } else if (arg == QStringLiteral("--qa-settings-section")
                       && i + 1 < cliArgs.size()) {
                bool ok = false;
                const int section = cliArgs.at(++i).toInt(&ok);
                if (ok && section >= 0 && section <= 6) {
                    qaSettingsSection = section;
                }
            } else if (arg == QStringLiteral("--qa-open-equalizer")) {
                qaOpenEqualizer = true;
            } else if (arg == QStringLiteral("--qa-equalizer-size")
                       && i + 2 < cliArgs.size()) {
                bool widthOk = false;
                bool heightOk = false;
                const int width = cliArgs.at(++i).toInt(&widthOk);
                const int height = cliArgs.at(++i).toInt(&heightOk);
                if (widthOk && heightOk && width >= 1080 && height >= 480) {
                    qaEqualizerSize = QSize(width, height);
                }
            } else if (arg == QStringLiteral("--qa-library")
                       && i + 1 < cliArgs.size()) {
                qaLibraryPath = cliArgs.at(++i);
            } else if (arg == QStringLiteral("--qa-import-folder")
                       && i + 1 < cliArgs.size()) {
                qaImportFolder = cliArgs.at(++i);
            } else if (arg == QStringLiteral("--qa-editor-selection-ms")
                       && i + 2 < cliArgs.size()) {
                bool startOk = false;
                bool endOk = false;
                const qint64 start = cliArgs.at(++i).toLongLong(&startOk);
                const qint64 end = cliArgs.at(++i).toLongLong(&endOk);
                if (startOk && endOk && start >= 0 && end > start) {
                    qaEditorSelectionStartMs = start;
                    qaEditorSelectionEndMs = end;
                }
            } else if (arg == QStringLiteral("--qa-playback-selection-ms")
                       && i + 2 < cliArgs.size()) {
                bool startOk = false;
                bool endOk = false;
                const qint64 start = cliArgs.at(++i).toLongLong(&startOk);
                const qint64 end = cliArgs.at(++i).toLongLong(&endOk);
                if (startOk && endOk && start >= 0 && end > start) {
                    qaPlaybackSelectionStartMs = start;
                    qaPlaybackSelectionEndMs = end;
                }
            } else if (arg == QStringLiteral("--qa-editor-playhead-ms")
                       && i + 1 < cliArgs.size()) {
                bool ok = false;
                const qint64 value = cliArgs.at(++i).toLongLong(&ok);
                if (ok && value >= 0) qaEditorPlayheadMs = value;
            } else if (arg == QStringLiteral("--qa-editor-reference-state")) {
                qaEditorReferenceState = true;
            } else if (arg == QStringLiteral("--qa-instance-key")
                       && i + 1 < cliArgs.size()) {
                instanceKeySuffix = cliArgs.at(++i);
            } else if (!arg.startsWith('-') && initialFilePath.isEmpty()) {
                initialFilePath = arg;
            }
        }
    }

    if (qaImmersiveThemeRequested
        && (!qaTestMode || qaImmersiveTheme.isEmpty()
            || !agplayer::immersive::findBuiltInTheme(qaImmersiveTheme.toStdString()))) {
        RuntimeLog::install(qaLogPath);
        qCritical().noquote() << "Invalid --qa-immersive-theme:" << qaImmersiveTheme
                             << "(requires --qa-test-mode and a built-in theme id)";
        return 2;
    }

    if (qaTestMode) {
        app.setApplicationName(QStringLiteral("AgPlayer-QA"));
        QStandardPaths::setTestModeEnabled(true);
        QSettings qaSettings;
        qaSettings.clear();
        qaSettings.sync();
    }

    // Install logging before the single-instance handshake as it is the
    // earliest cross-process boundary and otherwise invisible on GUI builds.
    RuntimeLog::install(qaLogPath);
    // Recover only once per process after QStandardPaths and the QA identity
    // are finalized. Individual processor instances may be created by tests
    // and auxiliary windows and must not rescan the persistent journal store.
    agplayer::qt::RenameJournalStore::recoverIncomplete();

    std::unique_ptr<QLocalServer> singleInstanceServer;
    QString singleInstanceMailboxDirectory;
    if (!qaTestMode) {
        // A per-user pipe name must not depend on the resolved AppData path:
        // test/portable launches can legitimately use different data roots
        // while they still belong to the same desktop user and must forward to
        // the one running player.
#ifdef Q_OS_WIN
        QByteArray instanceScope = qgetenv("USERNAME");
#else
        QByteArray instanceScope = QStandardPaths::writableLocation(
            QStandardPaths::HomeLocation).toUtf8();
#endif
        instanceScope += instanceKeySuffix.isEmpty()
            ? qgetenv("AGPLAYER_INSTANCE_KEY_SUFFIX")
            : instanceKeySuffix.toUtf8();
        const QByteArray userKey = QCryptographicHash::hash(
            instanceScope,
            QCryptographicHash::Sha256).toHex().left(16);
        const QString serverName = QStringLiteral("AgPlayer.SingleInstance.%1")
            .arg(QString::fromLatin1(userKey));
        const QString instanceKey = QString::fromLatin1(userKey);
        const auto forwardToPrimary = [&](bool includeMediaPath = true) {
            QLocalSocket socket;
            socket.connectToServer(serverName, QIODevice::WriteOnly);
            if (!socket.waitForConnected(500)) {
                return false;
            }
            const QByteArray mediaUrl = !includeMediaPath || initialFilePath.isEmpty()
                ? QByteArray("\n")
                : QUrl::fromLocalFile(QFileInfo(initialFilePath).absoluteFilePath())
                      .toEncoded() + '\n';
            socket.write(mediaUrl);
            socket.flush();
            socket.waitForBytesWritten(500);
            socket.disconnectFromServer();
            return true;
        };
#ifdef Q_OS_WIN
        // The mutex is the authoritative ownership primitive on Windows.
        // QLocalServer carries the command payload, while a tiny durable
        // mailbox is the fallback for a process that is still creating its
        // QML windows.  This prevents a second player process even if the
        // local-pipe handshake races the primary startup.
        const std::wstring mutexName =
            QStringLiteral("Local\\AgPlayer.SingleInstance.%1")
                .arg(instanceKey).toStdWString();
        HANDLE instanceMutex = CreateMutexW(nullptr, TRUE, mutexName.c_str());
        const bool anotherInstanceOwnsMutex =
            instanceMutex != nullptr && GetLastError() == ERROR_ALREADY_EXISTS;
        const QString tempRoot = QStandardPaths::writableLocation(
            QStandardPaths::TempLocation);
        singleInstanceMailboxDirectory = QDir(tempRoot).filePath(
            QStringLiteral("AgPlayer-inbox-%1").arg(instanceKey));
        QDir().mkpath(singleInstanceMailboxDirectory);
        if (anotherInstanceOwnsMutex) {
            // The mailbox is authoritative for media-open requests.  A local
            // socket received while the primary is building its QML windows
            // can otherwise be accepted before its handler exists.  Persist
            // the path first, then use the socket only to activate the window.
            bool mailboxWritten = false;
            if (!initialFilePath.isEmpty()) {
                const QString commandPath = QDir(singleInstanceMailboxDirectory)
                    .filePath(QStringLiteral("%1-%2.command")
                        .arg(QCoreApplication::applicationPid())
                        .arg(QDateTime::currentMSecsSinceEpoch()));
                QSaveFile commandFile(commandPath);
                if (commandFile.open(QIODevice::WriteOnly)) {
                    const QByteArray payload = QUrl::fromLocalFile(
                        QFileInfo(initialFilePath).absoluteFilePath()).toEncoded();
                    commandFile.write(payload);
                    if (commandFile.commit()) {
                        mailboxWritten = true;
                    }
                }
            }
            if (forwardToPrimary(!mailboxWritten) || mailboxWritten) {
                CloseHandle(instanceMutex);
                RuntimeLog::uninstall();
                return 0;
            }
            CloseHandle(instanceMutex);
            RuntimeLog::uninstall();
            return 4;
        }
#endif
        singleInstanceServer = std::make_unique<QLocalServer>();
        if (!singleInstanceServer->listen(serverName)) {
            // Never remove the server name before proving that there is no
            // live owner.  Removing it after a transient connect failure can
            // let a second process create another server while the primary is
            // still starting, defeating the single-instance contract.
            for (int attempt = 0; attempt < 3; ++attempt) {
                if (forwardToPrimary()) {
                    RuntimeLog::uninstall();
                    return 0;
                }
            }

            // A failed listen plus no reachable owner is the stale-pipe case
            // left by a crashed earlier process.  Only then is it safe to
            // reclaim the name for this process.
            QLocalServer::removeServer(serverName);
            if (!singleInstanceServer->listen(serverName)) {
                RuntimeLog::uninstall();
                return 4;
            }
        }
    }

    ag_player* core = nullptr;
    if (ag_player_create(&core) != AG_OK) {
        RuntimeLog::uninstall();
        return 2;
    }

    int result = 1;
    {
        LibraryModel library;
        const QString libraryPath = qaLibraryPath.isEmpty()
            ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                + QStringLiteral("/library.json")
            : qaLibraryPath;
        LibraryStore store(libraryPath);
        PlaylistModel playlists(QFileInfo(libraryPath).dir().filePath(
            QStringLiteral("playlists.json")));
        playlists.load();
        QObject::connect(&library, &LibraryModel::trackRemoved, &playlists,
                         &PlaylistModel::removeTrackFromAll);
        QObject::connect(&library, &LibraryModel::rowsInserted, &store,
            [&store, &library](const QModelIndex&, int, int) {
                store.requestSave(library.tracks());
            });
        QObject::connect(&library, &LibraryModel::dataChanged, &store,
            [&store, &library](const QModelIndex&, const QModelIndex&, const QList<int>&) {
                store.requestSave(library.tracks());
            });
        QObject::connect(&library, &LibraryModel::modelReset, &store,
            [&store, &library] {
                store.requestSave(library.tracks());
            });
        QObject::connect(&library, &LibraryModel::flushRequested, &store,
            [&store, &library] {
                store.requestSave(library.tracks());
                store.flush();
            });
        QList<TrackRecord> loaded = store.load();
        if (!loaded.isEmpty()) {
            library.replaceAll(std::move(loaded));
        }
        const QDir libraryDataDirectory = QFileInfo(libraryPath).dir();
        QString tagStoragePath =
            libraryDataDirectory.filePath(QStringLiteral("tags.json"));
        std::unique_ptr<QTemporaryDir> qaTagStorageDirectory;
        if (qaTestMode && !qaLibraryPath.isEmpty() && !qaSeedTags.isEmpty()) {
            qaTagStorageDirectory = std::make_unique<QTemporaryDir>(
                QDir(QDir::tempPath()).filePath(
                    QStringLiteral("AgPlayer-qa-tags-XXXXXX")));
            tagStoragePath = qaTagStorageDirectory->isValid()
                ? qaTagStorageDirectory->filePath(QStringLiteral("tags.json"))
                : QString();
        }
        TagModel tagModel(
            &library,
            tagStoragePath);
        if (qaTestMode) {
            for (const QString& tagName : qaSeedTags) {
                tagModel.createTag(tagName);
            }
            if (!qaSeedTags.isEmpty()) {
                tagModel.flush();
                qInfo().noquote() << "QA seeded tags: requested="
                                  << qaSeedTags.size()
                                  << "created=" << tagModel.count();
            }
            if (!qaSelectedTag.isEmpty()) {
                tagModel.setSelectedKey(qaSelectedTag);
            }
        }

        PlaybackController playback(core, &library);
        VideoPlaybackController videoPlayback(&library, &playback);
        if (qaPlaybackSelectionStartMs >= 0
            && qaPlaybackSelectionEndMs > qaPlaybackSelectionStartMs) {
            QObject::connect(
                &playback, &PlaybackController::durationMsChanged, &app,
                [&playback, qaPlaybackSelectionStartMs,
                 qaPlaybackSelectionEndMs]() {
                    if (playback.durationMs() > 0
                        && playback.selectionEndMs() <= playback.selectionStartMs()) {
                        playback.commitSelection(qaPlaybackSelectionStartMs,
                                                 qaPlaybackSelectionEndMs);
                    }
                });
        }
        PlaybackClipDragAdapter playbackClipDrag(&library);
        EqualizerController equalizer(core);
        QObject::connect(&playback, &PlaybackController::currentTrackIdChanged,
                         &equalizer, &EqualizerController::refreshStatus);
        QObject::connect(&playback, &PlaybackController::stateChanged,
                         &equalizer, &EqualizerController::refreshStatus);
        SettingsController settings;
        const auto applyKeepPitch = [&settings, &playback]() {
            playback.setKeepPitch(
                settings.keepPitchWhileSpeedChange());
        };
        applyKeepPitch();
        QObject::connect(
            &settings,
            &SettingsController::keepPitchWhileSpeedChangeChanged,
            &app, applyKeepPitch);
        PlayerExperienceController playerExperience(&settings);
        AudioVisualFeatureController audioVisualFeatures(&playback);
        LyricsService lyricsService(&library, &playback, &settings);
        if (qaTestMode) {
            if (qaImmersiveThemeRequested) {
                playerExperience.applyTheme(qaImmersiveTheme);
                if (!qaScreenshotMain.isEmpty()) {
                    playerExperience.setAutoRotate(0);
                    playerExperience.setAutoRotateSpeed(0);
                    playerExperience.setSongAdaptiveColorEnabled(false);
                }
                qInfo().noquote() << "QA immersive theme:" << playerExperience.themeId();
            }
            if (qaLyricPlacement >= 0) {
                playerExperience.setLyricPosition(qaLyricPlacement);
                playerExperience.setLyricsVisible(true);
            }
        }
        if (qaPlayerShell == QStringLiteral("integrated")) {
            settings.setPlayerShellMode(1);
        } else if (qaPlayerShell == QStringLiteral("rolling")) {
            settings.setPlayerShellMode(2);
        } else if (qaPlayerShell == QStringLiteral("classic")) {
            settings.setPlayerShellMode(0);
        }
        const auto applyOutputDevice = [&settings, &playback]() {
            const QString requested = settings.outputDevice();
            if (!requested.isEmpty()
                && !playback.outputDeviceIds().contains(requested)) {
                const qsizetype legacyIndex =
                    playback.outputDevices().indexOf(requested);
                if (legacyIndex >= 0
                    && legacyIndex < playback.outputDeviceIds().size()) {
                    settings.setOutputDevice(
                        playback.outputDeviceIds().at(legacyIndex));
                    return;
                }
            }
            if (playback.setOutputDevice(settings.outputDevice(),
                                         settings.exclusiveMode())) {
                return;
            }
            // A temporarily unavailable endpoint or exclusive session must
            // not erase the user's saved preference. Use a shared default
            // device for this run; the preference will be retried next time.
            playback.setOutputDevice(QString(), false);
        };
        applyOutputDevice();
        QObject::connect(&settings, &SettingsController::outputDeviceChanged,
                         &app, applyOutputDevice);
        QObject::connect(&settings, &SettingsController::exclusiveModeChanged,
                         &app, applyOutputDevice);
        playback.setTransitionFadeMs(settings.transitionFadeMs());
        playback.setMatchTrackSampleRate(settings.matchTrackSampleRate());
        playback.setReplayGainSettings(settings.replayGainMode(),
                                       settings.replayGainClipProtection());
        QObject::connect(
            &settings, &SettingsController::transitionFadeMsChanged,
            &app, [&settings, &playback]() {
                playback.setTransitionFadeMs(settings.transitionFadeMs());
            });
        QObject::connect(
            &settings, &SettingsController::matchTrackSampleRateChanged,
            &app, [&settings, &playback]() {
                playback.setMatchTrackSampleRate(
                    settings.matchTrackSampleRate());
            });
        const auto applyReplayGain = [&settings, &playback]() {
            playback.setReplayGainSettings(
                settings.replayGainMode(),
                settings.replayGainClipProtection());
        };
        QObject::connect(&settings, &SettingsController::replayGainModeChanged,
                         &app, applyReplayGain);
        QObject::connect(
            &settings, &SettingsController::replayGainClipProtectionChanged,
            &app, applyReplayGain);
        playback.setMode(static_cast<PlaybackController::Mode>(
            settings.defaultPlaybackMode()));
        QObject::connect(&settings, &SettingsController::defaultPlaybackModeChanged,
                         &playback, [&settings, &playback]() {
            playback.setMode(static_cast<PlaybackController::Mode>(
                settings.defaultPlaybackMode()));
        });
        PlaybackStateStore playbackStateStore(
            QFileInfo(libraryPath).dir().filePath(QStringLiteral("session.json")));
        PlaybackStateStore::State restoredState = playbackStateStore.load();
        if (!restoredState.valid) {
            QSettings legacySession;
            legacySession.beginGroup(QStringLiteral("session"));
            restoredState.currentTrackId =
                legacySession.value(QStringLiteral("trackId")).toString();
            restoredState.positionMs =
                legacySession.value(QStringLiteral("positionMs"), 0).toLongLong();
            legacySession.endGroup();
        }
        playbackStateStore.markRunStarted();
        const bool hasExplicitStartupMedia =
            !initialFilePath.isEmpty() || !qaPlayPath.isEmpty()
            || !qaImportFolder.isEmpty();
        const bool shouldRestorePlayback =
            settings.restoreLastPlaybackOnStartup() && !hasExplicitStartupMedia;
        if (shouldRestorePlayback && restoredState.valid
            && restoredState.mode >= PlaybackController::Sequential
            && restoredState.mode <= PlaybackController::RepeatAll) {
            playback.setMode(
                static_cast<PlaybackController::Mode>(restoredState.mode));
        }
        const auto savePlaybackState =
            [&playbackStateStore, &playback](const bool cleanExit) {
            PlaybackStateStore::State state;
            state.queueTrackIds = playback.queueTrackIds();
            state.currentTrackId = playback.currentTrackId();
            state.positionMs = playback.positionMs();
            state.mode = static_cast<int>(playback.mode());
            state.cleanExit = cleanExit;
            state.valid = true;
            return playbackStateStore.save(state);
        };
        QTimer sessionSaveTimer;
        sessionSaveTimer.setInterval(1'000);
        QObject::connect(&sessionSaveTimer, &QTimer::timeout, &app,
                         [&savePlaybackState]() { savePlaybackState(false); });
        sessionSaveTimer.start();
        if (qaTheme == QStringLiteral("dark")) {
            settings.setThemeMode(0);
        } else if (qaTheme == QStringLiteral("light")) {
            settings.setThemeMode(1);
        } else if (qaTheme == QStringLiteral("system")) {
            settings.setThemeMode(2);
        }
        if (!qaLanguage.isEmpty()) {
            settings.setLanguage(qaLanguage);
        }
        // QA defaults must match a fresh installation; an explicit waveform
        // argument below is the only presentation override.
        if (qaWaveformMode >= 0) {
            settings.setWaveformMode(qaWaveformMode);
        }
        TranslationManager translations;
        if (!translations.setLanguage(settings.language())) {
            settings.setLanguage(QStringLiteral("zh"));
        }
        QObject::connect(&settings, &SettingsController::languageChanged, &app,
                         [&settings, &translations]() {
            if (!translations.setLanguage(settings.language())
                && settings.language() != QStringLiteral("zh")) {
                settings.setLanguage(QStringLiteral("zh"));
            }
        });
        WaveformProvider waveformProvider(&settings);
        TrackWaveformThumbnailProvider trackWaveformThumbnailProvider(
            settings.cacheDirectory());
        QObject::connect(
            &settings, &SettingsController::cacheDirectoryChanged,
            &trackWaveformThumbnailProvider,
            [&settings, &trackWaveformThumbnailProvider]() {
                trackWaveformThumbnailProvider.setCacheDirectory(
                    settings.cacheDirectory());
            });
        QObject::connect(
            &waveformProvider, &WaveformProvider::waveformCacheReady,
            &trackWaveformThumbnailProvider,
            &TrackWaveformThumbnailProvider::invalidateSourceCache);
        QObject::connect(
            &trackWaveformThumbnailProvider,
            &TrackWaveformThumbnailProvider::analysisRequested,
            &waveformProvider,
            [&waveformProvider](const QString& sourcePath) {
                waveformProvider.prefetchTracks({sourcePath});
            });
        QObject::connect(&waveformProvider, &WaveformProvider::waveformReady,
                         &playback,
                         [&playback, &library, &settings](
                             const QString& path, const QVariantMap& layers) {
                             const QString trackId =
                                 layers.value(QStringLiteral("_trackId")).toString();
                             playback.applyWaveformDuration(
                                 trackId,
                                 layers.value(QStringLiteral("_durationMs")).toLongLong());
                             double bpm = readEmbeddedBpmTag(path);
                             if (bpm <= 0.0 && settings.autoReadBpm()) {
                                 bpm = layers.value(QStringLiteral("_bpm")).toDouble();
                             }
                             if (bpm > 0.0) {
                                 library.setBpm(trackId, bpm);
                             }
                         });
        auto autoReadBpmFlag = std::make_shared<std::atomic_bool>(
            settings.autoReadBpm() && qaImportFolder.isEmpty());
        ImportController importer(&library, [autoReadBpmFlag](const QString& path) {
            return probeMetadata(path, autoReadBpmFlag->load(std::memory_order_relaxed));
        });
        ResourceFolderController resourceFolders;
        resourceFolders.setStoragePath(libraryDataDirectory.filePath(
            QStringLiteral("resource-roots.json")));
        resourceFolders.setLibraryModel(&library);
        resourceFolders.setImportController(&importer);
        LibraryNavigationModel libraryNavigation(
            &library, &playlists, &tagModel, &resourceFolders);
        QObject::connect(&settings, &SettingsController::autoReadBpmChanged, &app,
                         [autoReadBpmFlag, &settings]() {
            autoReadBpmFlag->store(settings.autoReadBpm(), std::memory_order_relaxed);
        });
        WindowController windows;
#ifdef Q_OS_MACOS
        QObject::connect(&app, &QGuiApplication::applicationStateChanged,
                         &windows, [&windows](Qt::ApplicationState state) {
            if (state == Qt::ApplicationActive) windows.restoreApplicationWindows();
        });
#endif
        NativeDropRouter nativeDrops;
        windows.setMagneticSnapEnabled(settings.windowMagneticSnap());
        windows.setPreferredDockEdge(settings.listWindowPosition());
        windows.setCloseBehavior(settings.closeBehavior());
        windows.setListWindowPanelAllowed(
            settings.playerShellMode() == 0
            && library.count() > 0 && settings.showListWindowPanel());

        QObject::connect(&settings, &SettingsController::windowMagneticSnapChanged,
                         &windows, [&settings, &windows]() {
            windows.setMagneticSnapEnabled(settings.windowMagneticSnap());
        });
        QObject::connect(&settings, &SettingsController::listWindowPositionChanged,
                         &windows, [&settings, &windows]() {
            windows.setPreferredDockEdge(settings.listWindowPosition());
        });
        QObject::connect(&settings, &SettingsController::closeBehaviorChanged,
                         &windows, [&settings, &windows]() {
            windows.setCloseBehavior(settings.closeBehavior());
        });

        AudioToolsController audioTools;
        AudioEditorController audioEditor;
        audioEditor.setPlaybackController(&playback);
        if (!qaScreenshotTools.isEmpty()) {
            audioTools.selectTool(qaTool);
        }
        MetadataEditor metadataEditor;
        metadataEditor.setLibraryModel(&library);
        FilenameProcessor filenameProcessor;
        filenameProcessor.setLibraryModel(&library);
        FormatConverter formatConverter;
        LosslessAnalysisController losslessAnalysis;
        QObject::connect(&translations, &TranslationManager::languageChanged,
                         &losslessAnalysis, &LosslessAnalysisController::refreshTranslations);
        AudioPreviewController audioPreview(
            AG_AUDIO_BACKEND_DEFAULT, &playback);
        audioTools.bindPlaybackControllers(&playback, &audioEditor, &audioPreview);
        WaveformProvider separationWaveformProvider(&settings);
        VocalSeparationControllerOptions separationOptions;
        separationOptions.dataRoot = QFileInfo(libraryPath).dir().filePath(
            QStringLiteral("separation"));
        separationOptions.outputDirectory = settings.defaultOutputDirectory();
        VocalSeparationController vocalSeparation(
            &audioPreview, &separationWaveformProvider, &library, &importer,
            &playlists, separationOptions);
        if (!qaScreenshotTools.isEmpty() && !qaPlayPath.isEmpty()
            && QFileInfo::exists(qaPlayPath)) {
            const QList<QUrl> qaToolUrls{QUrl::fromLocalFile(qaPlayPath)};
            switch (qaTool) {
            case 0:
                audioEditor.openFileWhenReady(
                    qaToolUrls.constFirst(), &app, [&audioEditor] {
                        if (audioEditor.totalFrames() > 2'000) {
                            audioEditor.setSelection(
                                audioEditor.totalFrames() / 4,
                                audioEditor.totalFrames() / 2);
                            audioEditor.seekMs(audioEditor.durationMs() / 3);
                        }
                    });
                break;
            case 1:
                formatConverter.loadFiles(qaToolUrls);
                break;
            case 2:
                metadataEditor.loadFiles(qaToolUrls);
                break;
            case 3:
                filenameProcessor.loadFiles(qaToolUrls);
                break;
            case 4:
                vocalSeparation.selectInput(qaToolUrls.constFirst());
                break;
            case 5:
                losslessAnalysis.loadFiles({qaToolUrls.constFirst()});
                losslessAnalysis.start();
                break;
            default:
                break;
            }
        }
        const auto applyOverwritePolicy = [&]() {
            const bool overwrite = settings.overwritePolicy() == 1;
            formatConverter.setOverwriteExisting(overwrite);
        };
        applyOverwritePolicy();
        QObject::connect(&settings, &SettingsController::overwritePolicyChanged,
                         &app, applyOverwritePolicy);

        register_agplayer_qml_types(&library, &playback, &importer, &windows,
                                    &audioTools, &metadataEditor,
                                    &formatConverter, &filenameProcessor,
                                    &settings, &waveformProvider, &playlists,
                                    &equalizer, &audioEditor,
                                    AgPlayerQmlRuntimeModels{
                                        &tagModel,
                                        &libraryNavigation,
                                        &resourceFolders,
                                        &trackWaveformThumbnailProvider,
                                        &playbackClipDrag,
                                        &videoPlayback,
                                        &losslessAnalysis},
                                    &playerExperience, &audioVisualFeatures,
                                    &lyricsService, &audioPreview,
                                    &vocalSeparation);

        QString pendingPlayFilePath;
        int pendingPlayFinishes = 0;

        auto playFileIfPending = [&]() {
            if (pendingPlayFilePath.isEmpty()) {
                return;
            }
            pendingPlayFinishes = importer.busy() ? 2 : 1;
            const QUrl url = QUrl::fromLocalFile(pendingPlayFilePath);
            importer.importUrls({url});
        };

        QTimer singleInstanceMailboxTimer;
        if (!singleInstanceMailboxDirectory.isEmpty()) {
            const auto processForwardedMailbox =
                [&singleInstanceMailboxDirectory, &pendingPlayFilePath,
                 &playFileIfPending, &windows]() {
                QDir mailbox(singleInstanceMailboxDirectory);
                const QFileInfoList commands = mailbox.entryInfoList(
                    {QStringLiteral("*.command")}, QDir::Files,
                    QDir::Time | QDir::Reversed);
                for (const QFileInfo& command : commands) {
                    QFile commandFile(command.absoluteFilePath());
                    if (!commandFile.open(QIODevice::ReadOnly)) {
                        continue;
                    }
                    const QByteArray payload = commandFile.readAll().trimmed();
                    commandFile.close();
                    QFile::remove(command.absoluteFilePath());
                    const QString path = QUrl::fromEncoded(payload).toLocalFile();
                    if (!path.isEmpty() && QFileInfo::exists(path)) {
                        pendingPlayFilePath = QFileInfo(path).absoluteFilePath();
                        playFileIfPending();
                        windows.showMain();
                    }
                }
            };
            QObject::connect(&singleInstanceMailboxTimer, &QTimer::timeout,
                             &app, processForwardedMailbox);
            singleInstanceMailboxTimer.setInterval(100);
            singleInstanceMailboxTimer.start();
            processForwardedMailbox();
        }

        if (singleInstanceServer != nullptr) {
            const auto consumeSingleInstanceConnections = [&]() {
                while (singleInstanceServer->hasPendingConnections()) {
                        QLocalSocket* socket =
                            singleInstanceServer->nextPendingConnection();
                        if (socket == nullptr) {
                            continue;
                        }
                        // A launcher can connect while the application is still
                        // constructing its QML windows.  Handle both an already
                        // queued payload and normal readyRead/disconnect paths;
                        // otherwise the primary keeps the socket pending and
                        // silently loses the file-open request.
                        const auto received = std::make_shared<bool>(false);
                        const auto receive = [&, socket, received]() {
                            if (*received) {
                                return;
                            }
                            const QByteArray message = socket->readAll().trimmed();
                            if (message.isEmpty()
                                && socket->state() != QLocalSocket::UnconnectedState) {
                                return;
                            }
                            *received = true;
                            if (!message.isEmpty()) {
                                const QString path =
                                    QUrl::fromEncoded(message).toLocalFile();
                                if (!path.isEmpty() && QFileInfo::exists(path)) {
                                    pendingPlayFilePath =
                                        QFileInfo(path).absoluteFilePath();
                                    playFileIfPending();
                                }
                                windows.showMain();
                            } else {
                                windows.toggleMainWindowGroup();
                            }
                        };
                        QObject::connect(socket, &QLocalSocket::readyRead,
                                         &app, receive);
                        QObject::connect(socket, &QLocalSocket::disconnected,
                                         &app, receive);
                        QObject::connect(socket, &QLocalSocket::disconnected,
                                         socket, &QObject::deleteLater);
                        receive();
                    }
            };
            QObject::connect(singleInstanceServer.get(), &QLocalServer::newConnection,
                             &app, consumeSingleInstanceConnections);
            // QLocalServer emits newConnection before the QML stack is ready
            // when a second launch races startup.  Drain any such connection
            // immediately after wiring the handler.
            consumeSingleInstanceConnections();
        }

        QObject::connect(&importer, &ImportController::finished, &app,
                         [&library, &playback, &pendingPlayFilePath,
                          &pendingPlayFinishes]() {
            if (pendingPlayFilePath.isEmpty()) {
                return;
            }
            const int row = library.indexForLocalFile(pendingPlayFilePath);
            if (row >= 0) {
                playback.playRow(row);
                pendingPlayFilePath.clear();
                pendingPlayFinishes = 0;
                return;
            }
            if (--pendingPlayFinishes <= 0) {
                pendingPlayFilePath.clear();
                pendingPlayFinishes = 0;
            }
        });

        QObject::connect(&app, &AgPlayerApplication::fileOpenRequested, &playback,
                         [&](const QUrl& url) {
            const QFileInfo file(url.toLocalFile());
            if (file.isFile()) {
                pendingPlayFilePath = file.absoluteFilePath();
                playFileIfPending();
                windows.showMain();
            }
        });

        QSystemTrayIcon tray(QIcon(
            QStringLiteral(":/qt/qml/AgPlayer/assets/brand/logo-mark.png")), &app);
        QMenu trayMenu;
        QAction trayShowAction;
        QAction trayExitAction;
        const auto updateTrayText = [&]() {
            trayShowAction.setText(QCoreApplication::translate(
                "SystemTray", "Show AgPlayer"));
            trayExitAction.setText(QCoreApplication::translate(
                "SystemTray", "Exit"));
            tray.setToolTip(QStringLiteral("AgPlayer"));
        };
        updateTrayText();
        trayMenu.addAction(&trayShowAction);
        trayMenu.addSeparator();
        trayMenu.addAction(&trayExitAction);
        tray.setContextMenu(&trayMenu);
        if (QSystemTrayIcon::isSystemTrayAvailable() && !qaTestMode) {
            tray.show();
        }

        QObject::connect(&translations, &TranslationManager::languageChanged,
                         &app, updateTrayText);
        QObject::connect(&trayShowAction, &QAction::triggered,
                         &windows, [&windows]() {
            windows.showMain();
        });
        QObject::connect(&trayExitAction, &QAction::triggered,
                         &windows, &WindowController::requestExit);
        QObject::connect(&tray, &QSystemTrayIcon::activated, &windows,
                         [&windows](QSystemTrayIcon::ActivationReason reason) {
            if (reason == QSystemTrayIcon::Trigger
                || reason == QSystemTrayIcon::DoubleClick) {
                windows.showMain();
            }
        });

        WindowController::ShutdownActions shutdownActions;
        shutdownActions.cancelWaveform = [&importer]() { importer.cancel(); };
        shutdownActions.stopPlayback = [&core]() {
                if (core != nullptr) {
                    ag_player_stop(core);
                }
            };
        shutdownActions.flushLibrary =
            [&library, &playlists, &tagModel, &settings, &savePlaybackState]() {
            library.flush();
            playlists.flush();
            if (!tagModel.flush()) {
                qWarning().noquote()
                    << QCoreApplication::translate(
                           "Main", "Failed to save tag data during shutdown");
            }
            savePlaybackState(true);
            if (settings.cleanTempOnExit()) {
                settings.clearTempFiles();
            }
        };
        shutdownActions.releaseCore = [&audioEditor, &playback, &core]() {
            audioEditor.setPlaybackController(nullptr);
            playback.setPlayer(nullptr);
            if (core != nullptr) {
                ag_player_destroy(core);
                core = nullptr;
            }
        };
        if (QSystemTrayIcon::isSystemTrayAvailable() && !qaTestMode) {
            shutdownActions.minimizeToTray = [&tray]() {
                tray.show();
                tray.showMessage(QStringLiteral("AgPlayer"),
                                 QCoreApplication::translate(
                                     "SystemTray", "AgPlayer is still running"));
            };
        }
        shutdownActions.quitApplication = []() { QCoreApplication::quit(); };
        windows.setShutdownActions(std::move(shutdownActions));

        QQmlApplicationEngine engine;
        QObject::connect(&translations, &TranslationManager::languageChanged,
                         &engine, &QQmlApplicationEngine::retranslate);
        engine.addImportPath("qrc:/");
        engine.loadFromModule("AgPlayer", "Main");

        if (engine.rootObjects().isEmpty()) {
            RuntimeLog::log(AG_INTERNAL_ERROR, QStringLiteral("Main"),
                QStringLiteral("Failed to load Main QML module"));
            result = 3;
            return result;
        }

#ifdef Q_OS_MACOS
        // A parentless QMenuBar supplies the macOS global menu without adding
        // a row to the existing QML layout. Roles place Settings/Quit in the app menu.
        QMenuBar macMenuBar;
        QMenu* appMenu = macMenuBar.addMenu(QStringLiteral("AgPlayer"));
        QAction* macSettings = new QAction(appMenu);
        macSettings->setMenuRole(QAction::PreferencesRole);
        macSettings->setShortcut(QKeySequence::Preferences);
        appMenu->addAction(macSettings);
        QObject::connect(macSettings, &QAction::triggered, &engine, [&windows, &engine] {
            windows.showMain();
            QMetaObject::invokeMethod(engine.rootObjects().first(), "openSettingsPage");
        });
        QAction* macQuit = new QAction(appMenu);
        macQuit->setMenuRole(QAction::QuitRole);
        macQuit->setShortcut(QKeySequence::Quit);
        appMenu->addAction(macQuit);
        QObject::connect(macQuit, &QAction::triggered, &windows, &WindowController::requestExit);
        QMenu* macWindowMenu = macMenuBar.addMenu(QString());
        QAction* macMinimize = macWindowMenu->addAction(QString());
        macMinimize->setShortcut(QKeySequence(QStringLiteral("Ctrl+M")));
        QObject::connect(macMinimize, &QAction::triggered, &app, [] {
            if (QWindow* focused = QGuiApplication::focusWindow()) focused->showMinimized();
        });
        QAction* macClose = macWindowMenu->addAction(QString());
        macClose->setShortcut(QKeySequence::Close);
        QObject::connect(macClose, &QAction::triggered, &app, [] {
            if (QWindow* focused = QGuiApplication::focusWindow()) focused->close();
        });
        const auto updateMacMenuText = [macSettings, macQuit, macWindowMenu, macMinimize, macClose] {
            macSettings->setText(QCoreApplication::translate("Main", "设置…"));
            macQuit->setText(QCoreApplication::translate("Main", "退出 AgPlayer"));
            macWindowMenu->setTitle(QCoreApplication::translate("Main", "窗口"));
            macMinimize->setText(QCoreApplication::translate("Main", "最小化"));
            macClose->setText(QCoreApplication::translate("Main", "关闭窗口"));
        };
        updateMacMenuText();
        QObject::connect(&translations, &TranslationManager::languageChanged, &macMenuBar, updateMacMenuText);
#endif

        if (!qaPlayPath.isEmpty()) {
            initialFilePath = qaPlayPath;
        }

        pendingPlayFilePath = initialFilePath;
        if (!pendingPlayFilePath.isEmpty() && !QFileInfo::exists(pendingPlayFilePath)) {
            pendingPlayFilePath.clear();
        }

        if (!engine.rootObjects().isEmpty()) {
            QObject* mainWindow = engine.rootObjects().first();
            const bool qaExpectVideo = !qaPlayPath.isEmpty()
                && agplayer::qt::isSupportedVideoExtension(
                    QFileInfo(qaPlayPath).suffix());

            const auto enterQaVideoFullscreen =
                [mainWindow, &videoPlayback, qaVideoFullscreen]() {
                if (!qaVideoFullscreen || !videoPlayback.visible()) {
                    return;
                }
                if (!QMetaObject::invokeMethod(mainWindow,
                                               "enterVideoFullscreen")) {
                    qWarning("QA video fullscreen request failed");
                }
            };
            QObject::connect(&videoPlayback,
                             &VideoPlaybackController::visibleChanged,
                             &app, enterQaVideoFullscreen);
            QTimer::singleShot(0, &app, enterQaVideoFullscreen);

            if (qaTestMode && qaImmersiveStability) {
                playback.setMode(PlaybackController::RepeatOne);
                playerExperience.setQualityPreset(PlayerExperienceController::High);
                const QPointer<QObject> qaRoot(mainWindow);
                auto elapsed = std::make_shared<QElapsedTimer>();
                elapsed->start();
                auto* responsiveness = new QTimer(&app);
                responsiveness->setTimerType(Qt::PreciseTimer);
                auto previousProbe = std::make_shared<qint64>(elapsed->elapsed());
                QObject::connect(responsiveness, &QTimer::timeout, &app,
                    [elapsed, previousProbe] {
                        const qint64 now = elapsed->elapsed();
                        qInfo() << "QA immersive GUI delayMs="
                                << std::max<qint64>(0, now - *previousProbe - 1000);
                        *previousProbe = now;
                    });
                responsiveness->start(1000);
                const auto sampleRenderer = [qaRoot, elapsed, &playback](const QString& phase) {
                    QSet<TerrainReactorItem*> items;
                    const auto collectItems = [&items](QObject* root) {
                        if (!root) return;
                        for (auto* item : root->findChildren<TerrainReactorItem*>())
                            items.insert(item);
                    };
                    collectItems(qaRoot.data());
                    for (QWindow* window : QGuiApplication::allWindows()) collectItems(window);
                    TerrainReactorItem* terrain = nullptr;
                    for (auto* item : items) {
                        if (!terrain || item->active()) terrain = item;
                    }
                    const auto counter = [terrain](const char* name) -> qulonglong {
                        return terrain ? terrain->property(name).toULongLong() : 0;
                    };
                    // Only numeric health counters: no track names, paths or audio data.
                    qInfo().noquote() << QStringLiteral(
                        "QA immersive stability: phase=%1 elapsedMs=%2 present=%3 active=%4 exposed=%5 frames=%6 itemLive=%7 resources=%8 items=%9")
                        .arg(phase).arg(elapsed->elapsed()).arg(terrain ? 1 : 0)
                        .arg(counter("active")).arg(counter("hostExposed"))
                        .arg(counter("frameCount")).arg(counter("liveRendererCount"))
                        .arg(counter("resourceGeneration")).arg(items.size());
                    qInfo() << "QA immersive audio: state=" << playback.state()
                            << "positionMs=" << playback.positionMs()
                            << "durationMs=" << playback.durationMs();
                };
                auto* sampler = new QTimer(&app);
                sampler->setTimerType(Qt::PreciseTimer);
                QObject::connect(sampler, &QTimer::timeout, &app, [sampler, sampleRenderer] {
                    sampleRenderer(QStringLiteral("sample"));
                    sampler->setInterval(60000);
                });
                QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [sampler, sampleRenderer] {
                    sampler->stop();
                    // aboutToQuit precedes scene-graph teardown: this is not
                    // evidence that GPU resources have already been released.
                    sampleRenderer(QStringLiteral("pre-exit"));
                });
                sampler->start(3000);
            }

            if (qaExitAfterMs > 0) {
                QTimer::singleShot(
                    qaExitAfterMs, &app,
                    [mainWindow, &videoPlayback, qaExpectVideo]() {
                        qInfo().noquote()
                            << "QA timed video diagnostics: visible="
                            << videoPlayback.visible()
                            << "workerRunning=" << videoPlayback.workerRunning()
                            << "queuedFrameCount="
                            << videoPlayback.queuedFrameCount()
                            << "queuedFrameBytes="
                            << videoPlayback.queuedFrameBytes()
                            << "frameSerial=" << videoPlayback.frameSerial()
                            << "fullscreen="
                            << mainWindow->property("videoFullscreen").toBool();
                        QCoreApplication::exit(
                            qaExpectVideo && videoPlayback.frameSerial() == 0
                                ? 8 : 0);
                    });
            }

            // Load the mini player window from the same module so it shares the
            // registered singletons. WindowController toggles visibility between
            // the two QWindow instances via setWindows().
            QQmlComponent miniComponent(&engine);
            miniComponent.loadFromModule("AgPlayer", "MiniPlayerWindow");
            QObject* miniWindow = nullptr;
            if (!miniComponent.isError()) {
                miniWindow = miniComponent.create();
            }
            if (miniWindow == nullptr) {
                qWarning().noquote()
                    << "Mini player QML failed:"
                    << miniComponent.errorString();
            } else {
                miniWindow->setProperty(
                    "waveformSession",
                    mainWindow->property("waveformSession"));
            }

            // The audio-tools window is loaded on first use.  Keeping only the
            // component here avoids constructing comparatively heavy
            // tool pages during ordinary player startup.
            QQmlComponent audioToolsComponent(&engine);
            QObject* audioToolsWindow = nullptr;

            // Only Classic uses the stand-alone playlist window. Integrated
            // and rolling keep the same filter model inside Main.qml.
            QObject* filterModel = mainWindow->findChild<QObject*>(
                QStringLiteral("filterModel"));
            QQmlComponent listComponent(&engine);
            listComponent.loadFromModule("AgPlayer", "ListWindow");
            QObject* listWindow = nullptr;
            windows.setMainWindowShellMode(settings.playerShellMode());
            windows.setWindows(qobject_cast<QWindow*>(mainWindow),
                               qobject_cast<QWindow*>(miniWindow));

            QWindow* const nativeMainWindow = qobject_cast<QWindow*>(mainWindow);
            if (nativeMainWindow != nullptr
                && qaMainWidth > 0 && qaMainHeight > 0) {
                nativeMainWindow->resize(qaMainWidth, qaMainHeight);
            }
            nativeDrops.registerWindow(nativeMainWindow,
                                       NativeDropRouter::Target::Main);
            QPointer<QWindow> immersiveDropWindow;
            const auto immersiveDropConnection = QObject::connect(&windows, &WindowController::immersivePresentationActiveChanged,
                             &nativeDrops, [&windows, &nativeDrops, mainWindow, &immersiveDropWindow]() {
                if (!windows.immersivePresentationActive()) return;
                auto* window = mainWindow->findChild<QWindow*>(QStringLiteral("immersiveVisualWindow"));
                if (window == nullptr) return;
                if (immersiveDropWindow != window) {
                    immersiveDropWindow = window;
                    QObject::connect(window, &QWindow::visibleChanged, &nativeDrops,
                                     [&nativeDrops, window](bool visible) {
                        if (visible) nativeDrops.registerWindow(window, NativeDropRouter::Target::Main);
                    });
                }
                nativeDrops.registerWindow(window, NativeDropRouter::Target::Main);
            });
            const QPointer<QObject> mainDropTarget = mainWindow;
            nativeDrops.registerHitTarget(
                nativeMainWindow, NativeDropRouter::Target::ResourceFolder,
                [mainDropTarget](const QPointF& position) {
                    if (mainDropTarget == nullptr) return false;
                    bool hit = false;
                    return QMetaObject::invokeMethod(
                               mainDropTarget, "resourceDropContainsPoint",
                               Q_RETURN_ARG(bool, hit),
                               Q_ARG(QVariant, position.x()),
                               Q_ARG(QVariant, position.y()))
                        && hit;
                });
            const auto ensureListWindow = [&]() -> QObject* {
                if (listWindow != nullptr) return listWindow;
                if (listComponent.isError()) {
                    qWarning().noquote() << "List window QML failed:"
                                         << listComponent.errorString();
                    return nullptr;
                }
                QVariantMap properties;
                if (filterModel != nullptr) {
                    properties.insert(QStringLiteral("filterModel"),
                                      QVariant::fromValue(filterModel));
                }
                properties.insert(QStringLiteral("tagSearchText"),
                                  mainWindow->property("tagSearchText"));
                listWindow = listComponent.createWithInitialProperties(properties);
                if (listWindow == nullptr) {
                    qWarning().noquote() << "List window QML failed:"
                                         << listComponent.errorString();
                    return nullptr;
                }
                if (qaListCategory == QStringLiteral("tags")) {
                    if (QObject* navigation = listWindow->findChild<QObject*>(
                            QStringLiteral("referenceSideNavigation"))) {
                        QMetaObject::invokeMethod(
                            navigation, "activateNode",
                            Q_ARG(QVariant, QStringLiteral("tags")),
                            Q_ARG(QVariant, QStringLiteral("tags:manage")),
                            Q_ARG(QVariant, QString{}));
                    }
                }
                auto* const nativeListWindow = qobject_cast<QWindow*>(listWindow);
                windows.setListWindow(nativeListWindow);
                nativeDrops.registerWindow(nativeListWindow,
                                           NativeDropRouter::Target::List);
                const QPointer<QObject> listDropTarget = listWindow;
                nativeDrops.registerHitTarget(
                    nativeListWindow, NativeDropRouter::Target::ResourceFolder,
                    [listDropTarget](const QPointF& position) {
                        if (listDropTarget == nullptr) return false;
                        QVariant hit;
                        return QMetaObject::invokeMethod(
                                   listDropTarget, "resourceDropContainsPoint",
                                   Q_RETURN_ARG(QVariant, hit),
                                   Q_ARG(QVariant, position.x()),
                                   Q_ARG(QVariant, position.y()))
                            && hit.toBool();
                    });
                return listWindow;
            };
            const auto destroyListWindow = [&]() {
                if (listWindow == nullptr) return;
                mainWindow->setProperty("tagSearchText",
                                        listWindow->property("tagSearchText"));
                auto* const nativeListWindow = qobject_cast<QWindow*>(listWindow);
                nativeDrops.unregisterWindow(nativeListWindow);
                windows.setListWindow(nullptr);
                delete listWindow;
                listWindow = nullptr;
            };
            QObject::connect(&audioTools, &AudioToolsController::losslessPlaylistRequested,
                             mainWindow, [&]() {
                auto* currentModel = qobject_cast<QAbstractItemModel*>(filterModel);
                if (currentModel == nullptr) {
                    losslessAnalysis.notifyError(QObject::tr("当前播放列表不可用"));
                    return;
                }
                QVariantList files;
                const QString category = filterModel->property("category").toString();
                if (!playlists.nameForId(category).isEmpty()) {
                    const auto ids = playlists.trackIdsForPlaylist(category);
                    for (const auto& id : ids) {
                        const auto path = library.trackForId(id).value(QStringLiteral("path")).toString();
                        if (!path.isEmpty()) files.append(QUrl::fromLocalFile(path));
                    }
                } else {
                    for (int row = 0; row < currentModel->rowCount(); ++row) {
                        const auto path = currentModel->data(currentModel->index(row, 0), LibraryModel::PathRole).toString();
                        if (!path.isEmpty()) files.append(QUrl::fromLocalFile(path));
                    }
                }
                if (files.isEmpty()) losslessAnalysis.notifyError(QObject::tr("当前播放列表没有可分析的文件"));
                else losslessAnalysis.loadFiles(files);
            });
            QObject::connect(&audioTools, &AudioToolsController::losslessLocateRequested,
                             mainWindow, [&](const QString& path) {
                QString id;
                const QString wanted = QFileInfo(path).absoluteFilePath();
                for (int row = 0; row < library.rowCount(); ++row) {
                    const auto index = library.index(row, 0);
                    if (QFileInfo(library.data(index, LibraryModel::PathRole).toString()).absoluteFilePath()
                            .compare(wanted, Qt::CaseInsensitive) == 0) {
                        id = library.data(index, LibraryModel::TrackIdRole).toString();
                        break;
                    }
                }
                if (id.isEmpty()) {
                    losslessAnalysis.notifyError(QObject::tr("此文件尚未加入播放器曲库，无法定位"));
                    return;
                }
                if (filterModel != nullptr) {
                    filterModel->setProperty("category", QStringLiteral("all"));
                    filterModel->setProperty("searchText", QString{});
                    filterModel->setProperty("exactRating", 0);
                    filterModel->setProperty("minBpm", 60.0);
                    filterModel->setProperty("maxBpm", 160.0);
                    filterModel->setProperty("tagKey", QString{});
                    filterModel->setProperty("resourceFolder", QString{});
                }
                QObject* surface = mainWindow;
                if (settings.playerShellMode() == 0) {
                    surface = ensureListWindow();
                    windows.showListWindow();
                }
                const QPointer<QObject> target = surface;
                QTimer::singleShot(0, mainWindow, [target, id, &losslessAnalysis]() {
                    if (target == nullptr) return;
                    QObject* list = target->findChild<QObject*>(QStringLiteral("sharedTrackList"));
                    if (list == nullptr)
                        list = target->findChild<QObject*>(QStringLiteral("integratedTrackList"));
                    if (list == nullptr)
                        list = target->findChild<QObject*>(QStringLiteral("rollingTrackList"));
                    QVariant located;
                    if (list == nullptr || !QMetaObject::invokeMethod(list, "locateTrack",
                            Q_RETURN_ARG(QVariant, located), Q_ARG(QVariant, id)) || !located.toBool())
                        losslessAnalysis.notifyError(QObject::tr("播放器列表尚未就绪，无法定位"));
                    if (auto* win = qobject_cast<QWindow*>(target.data())) {
                        win->show();win->raise();win->requestActivate();
                    }
                });
            });
            if (settings.playerShellMode() == 0) {
                (void)ensureListWindow();
            }
            const auto ensureAudioToolsWindow = [&]() -> QObject* {
                if (audioToolsWindow != nullptr) return audioToolsWindow;
                audioToolsComponent.loadFromModule("AgPlayer", "AudioToolsWindow");
                if (!audioToolsComponent.isError()) {
                    audioToolsWindow = audioToolsComponent.create();
                }
                if (audioToolsWindow == nullptr) {
                    qWarning().noquote()
                        << "Audio tools QML failed:"
                        << audioToolsComponent.errorString();
                    return nullptr;
                }
                auto* const toolsWindow = qobject_cast<QWindow*>(audioToolsWindow);
                windows.setAudioToolsWindow(toolsWindow);
                nativeDrops.registerWindow(
                    toolsWindow, NativeDropRouter::Target::AudioTools);
                return audioToolsWindow;
            };
            const QMetaObject::Connection audioToolsVisibleConnection = QObject::connect(
                &windows, &WindowController::audioToolsVisibleChanged, &app,
                [&windows, &ensureAudioToolsWindow]() {
                    if (windows.audioToolsVisible()) {
                        (void)ensureAudioToolsWindow();
                    }
                });
            QObject::connect(
                &nativeDrops, &NativeDropRouter::pathsDropped, &app,
                [&](NativeDropRouter::Target target, const QStringList& paths) {
                    if (paths.isEmpty()) {
                        return;
                    }
                    QList<QUrl> urls;
                    urls.reserve(paths.size());
                    for (const QString& path : paths) {
                        urls.append(QUrl::fromLocalFile(path));
                    }
                    switch (target) {
                    case NativeDropRouter::Target::Main:
                        {
                        bool accepted = false;
                        const bool invoked = mainWindow != nullptr
                            && QMetaObject::invokeMethod(
                                mainWindow, "handleShellDropUrls",
                                Qt::DirectConnection,
                                Q_RETURN_ARG(bool, accepted),
                                Q_ARG(QVariant, QVariant::fromValue(urls)));
                        if (!invoked) {
                            qWarning() << "Unable to submit classified main-window drop";
                        }
                        Q_UNUSED(accepted)
                        }
                        break;
                    case NativeDropRouter::Target::List:
                        {
                        const bool invoked = listWindow != nullptr
                            && QMetaObject::invokeMethod(
                                listWindow, "handleListDropUrls",
                                Q_ARG(QVariant, QVariant::fromValue(urls)));
                        if (!invoked) importer.importPaths(paths);
                        }
                        break;
                    case NativeDropRouter::Target::ResourceFolder:
                        {
                        bool accepted = false;
                        const bool mainInvoked = mainWindow != nullptr
                            && QMetaObject::invokeMethod(
                                mainWindow, "handleResourceDropUrls",
                                Qt::DirectConnection,
                                Q_RETURN_ARG(bool, accepted),
                                Q_ARG(QVariant, QVariant::fromValue(urls)));
                        if ((!mainInvoked || !accepted) && listWindow != nullptr) {
                            QMetaObject::invokeMethod(
                                listWindow, "handleResourceDropUrls",
                                Q_ARG(QVariant, QVariant::fromValue(urls)));
                        }
                        }
                        break;
                    case NativeDropRouter::Target::AudioTools:
                        switch (audioTools.currentTool()) {
                        case 0:
                            audioEditor.openFile(urls.constFirst());
                            break;
                        case 1:
                            formatConverter.loadFiles(urls);
                            break;
                        case 2:
                            metadataEditor.loadFiles(urls);
                            break;
                        case 3:
                            filenameProcessor.loadFiles(urls);
                            break;
                        case 4:
                            vocalSeparation.dropInput(urls);
                            break;
                        case 5: {
                            QVariantList files;
                            for (const auto& url : urls) files.append(url);
                            losslessAnalysis.loadFiles(files);
                            break;
                        }
                        default:
                            break;
                        }
                        break;
                    }
                });

            const int initialLibraryCount = library.count();
            QObject::connect(
                &library, &LibraryModel::countChanged, &app,
                [&library, &settings, &windows,
                 observedLibraryCount = initialLibraryCount]() mutable {
                    const int currentLibraryCount = library.count();
                    const bool tracksWereAdded =
                        currentLibraryCount > observedLibraryCount;
                    observedLibraryCount = currentLibraryCount;
                    const bool classicListAllowed =
                        settings.playerShellMode() == 0
                        && currentLibraryCount > 0
                        && settings.showListWindowPanel();
                    windows.setListWindowPanelAllowed(classicListAllowed);
                    if (tracksWereAdded && classicListAllowed) {
                        windows.showListWindow();
                    }
                });
            QObject::connect(&settings, &SettingsController::showListWindowPanelChanged,
                             &app, [&library, &settings, &windows]() {
                windows.setListWindowPanelAllowed(
                    settings.playerShellMode() == 0
                    && library.count() > 0 && settings.showListWindowPanel());
            });
            QObject::connect(
                &settings, &SettingsController::playerShellModeChanged, &app,
                [&library, &settings, &windows, &ensureListWindow,
                 &destroyListWindow]() {
                    windows.setMainWindowShellMode(settings.playerShellMode());
                    if (settings.playerShellMode() == 0) {
                        (void)ensureListWindow();
                    } else {
                        destroyListWindow();
                    }
                    windows.setListWindowPanelAllowed(
                        settings.playerShellMode() == 0
                        && library.count() > 0
                        && settings.showListWindowPanel());
                });
            windows.setListWindowPanelAllowed(
                settings.playerShellMode() == 0
                && library.count() > 0 && settings.showListWindowPanel());
            if (qaIntegratedShellLifecycleProbe) {
                QTimer::singleShot(
                    0, &app,
                    [&app, &settings, &playback, &listWindow, filterModel,
                     mainWindow]() {
                        const auto countShells = [mainWindow](const char* name) {
                            return mainWindow->findChildren<QObject*>(
                                QString::fromLatin1(name),
                                Qt::FindChildrenRecursively).size();
                        };
                        const auto settleLoader = [] {
                            QCoreApplication::processEvents(
                                QEventLoop::AllEvents, 500);
                            QCoreApplication::sendPostedEvents(
                                nullptr, QEvent::DeferredDelete);
                            QCoreApplication::processEvents(
                                QEventLoop::AllEvents, 500);
                        };
                        settleLoader();
                        const QString originalTrackId = playback.currentTrackId();
                        const qint64 originalPositionMs = playback.positionMs();
                        const PlaybackController::State originalState = playback.state();
                        const float originalVolume = playback.volume();
                        const PlaybackController::Mode originalMode = playback.mode();
                        const QStringList originalQueue = playback.queueTrackIds();
                        const auto usesSharedFilterModel = [filterModel](QObject* window) {
                            return window != nullptr && filterModel != nullptr
                                && window->property("filterModel").value<QObject*>()
                                    == filterModel;
                        };
                        const QPointer<QObject> initialListWindow = listWindow;
                        const bool classicInitial = settings.playerShellMode() == 0
                            && listWindow != nullptr
                            && usesSharedFilterModel(listWindow)
                            && countShells("classicPlayerShell") == 1
                            && countShells("integratedPlayerShell") == 0;

                        settings.setPlayerShellMode(2);
                        settleLoader();
                        const bool rollingHasNoList = listWindow == nullptr
                            && initialListWindow == nullptr
                            && countShells("rollingPlayerShell") == 1;

                        settings.setPlayerShellMode(1);
                        settleLoader();
                        const bool integratedLoaded = listWindow == nullptr
                            && countShells("classicPlayerShell") == 0
                            && countShells("integratedPlayerShell") == 1;

                        settings.setPlayerShellMode(2);
                        settleLoader();
                        const bool rollingStillHasNoList = listWindow == nullptr
                            && countShells("rollingPlayerShell") == 1;

                        settings.setPlayerShellMode(0);
                        settleLoader();
                        const bool classicRebuiltList = listWindow != nullptr
                            && usesSharedFilterModel(listWindow)
                            && countShells("classicPlayerShell") == 1
                            && countShells("integratedPlayerShell") == 0
                            && countShells("rollingPlayerShell") == 0;
                        const qint64 positionDrift = qAbs(
                            playback.positionMs() - originalPositionMs);
                        const bool playbackPreserved =
                            playback.currentTrackId() == originalTrackId
                            && (originalState == PlaybackController::Playing
                                    ? positionDrift < 1'000
                                    : positionDrift == 0)
                            && playback.state() == originalState
                            && qFuzzyCompare(playback.volume(), originalVolume)
                            && playback.mode() == originalMode
                            && playback.queueTrackIds() == originalQueue;
                        const bool passed = classicInitial && rollingHasNoList
                            && integratedLoaded && rollingStillHasNoList
                            && classicRebuiltList && playbackPreserved;
                        qInfo().noquote()
                            << "Integrated shell lifecycle probe:"
                            << (passed ? "passed" : "failed")
                            << "classicInitial=" << classicInitial
                            << "rollingHasNoList=" << rollingHasNoList
                            << "integratedLoaded=" << integratedLoaded
                            << "rollingStillHasNoList=" << rollingStillHasNoList
                            << "classicRebuiltList=" << classicRebuiltList
                            << "playbackPreserved=" << playbackPreserved
                            << "positionDriftMs=" << positionDrift;
                        app.exit(passed ? 0 : 7);
                    });
            }
            const bool qaShellProbe =
                qEnvironmentVariableIntValue("AGPLAYER_QA_SHELL_PROBE") == 1;
            if (qaShellProbe) {
                windows.showAudioTools();
                QCoreApplication::processEvents(QEventLoop::AllEvents, 500);
            }
            QObject* settingsWindow = nullptr;
            if (qaOpenSettings || qaShellProbe) {
                QMetaObject::invokeMethod(mainWindow, "openSettingsPage");
                QCoreApplication::processEvents(QEventLoop::AllEvents, 500);
                settingsWindow = mainWindow->findChild<QObject*>(
                    QStringLiteral("settingsWindow"));
                if (qaSettingsSection >= 0 && settingsWindow != nullptr) {
                    if (QObject* settingsPage = settingsWindow->findChild<QObject*>(
                            QStringLiteral("settingsPage"))) {
                        settingsPage->setProperty("selectedSection",
                                                  qaSettingsSection);
                    }
                }
            }
            QObject* equalizerWindow = nullptr;
            if (qaOpenEqualizer) {
                QMetaObject::invokeMethod(mainWindow, "openEqualizer");
                QCoreApplication::processEvents(QEventLoop::AllEvents, 500);
                equalizerWindow = mainWindow->findChild<QObject*>(
                    QStringLiteral("equalizerWindow"));
                if (equalizerWindow == nullptr) {
                    if (QObject* loader = mainWindow->findChild<QObject*>(
                            QStringLiteral("equalizerWindowLoader"))) {
                        equalizerWindow = qvariant_cast<QObject*>(
                            loader->property("item"));
                    }
                }
                if (qaEqualizerSize.isValid()) {
                    if (auto* equalizerWin = qobject_cast<QWindow*>(
                            equalizerWindow)) {
                        equalizerWin->resize(qaEqualizerSize);
                    }
                }
            }

            playFileIfPending();
            if (!qaImportFolder.isEmpty()) {
                importer.importFolder(QUrl::fromLocalFile(qaImportFolder));
            } else if (pendingPlayFilePath.isEmpty() && shouldRestorePlayback) {
                bool restored = false;
                if (restoredState.valid && !restoredState.queueTrackIds.isEmpty()) {
                    restored = playback.restoreQueue(restoredState.queueTrackIds,
                                                     restoredState.currentTrackId);
                }
                if (!restored) {
                    const int row = library.indexForTrackId(
                        restoredState.currentTrackId);
                    if (row >= 0) {
                        playback.loadRow(row);
                        restored = true;
                    }
                }
                if (restored) {
                    playback.seek(std::max<qint64>(0, restoredState.positionMs));
                }
            }

            // Native global hotkeys. Parsed from the settings
            // defaults and re-registered whenever the user changes a shortcut.
            GlobalHotkeyManager hotkeys;
            app.installNativeEventFilter(&hotkeys);

            auto registerGlobalHotkeys = [&]() {
                hotkeys.unregisterAll();
                QStringList registrationErrors;
                if (qaTestMode) {
                    return;
                }

                const auto registerOne = [&](const QString& combo,
                                             GlobalHotkeyManager::Action action) {
                    if (!hotkeys.registerShortcut(combo, action)) {
                        registrationErrors.append(combo + QStringLiteral(": ")
                            + (hotkeys.lastError().isEmpty()
                                ? QCoreApplication::translate("Main", "Shortcut is unavailable")
                                : hotkeys.lastError()));
                    }
                };

                const auto registerCombo = [&](const QString& combo,
                                               GlobalHotkeyManager::Action action) {
                    const QStringList parts = combo.split('/', Qt::SkipEmptyParts);
                    for (const QString& part : parts) {
                        registerOne(part.trimmed(), action);
                    }
                };

                const auto registerPair = [&](const QString& combo,
                                              GlobalHotkeyManager::Action first,
                                              GlobalHotkeyManager::Action second) {
                    const QStringList parts = combo.split('/', Qt::SkipEmptyParts);
                    if (parts.size() >= 1) {
                        registerOne(parts[0].trimmed(), first);
                    }
                    if (parts.size() >= 2) {
                        registerOne(parts[1].trimmed(), second);
                    }
                };

                registerCombo(settings.hkPlayPause(), GlobalHotkeyManager::Action::PlayPause);
                registerPair(settings.hkPrevNext(), GlobalHotkeyManager::Action::Previous,
                             GlobalHotkeyManager::Action::Next);
                registerPair(settings.hkVolumeUpDown(), GlobalHotkeyManager::Action::VolumeUp,
                             GlobalHotkeyManager::Action::VolumeDown);
                registerCombo(settings.hkToggleMiniPlayer(),
                              GlobalHotkeyManager::Action::ToggleMiniPlayer);
                registrationErrors.removeDuplicates();
                settings.setGlobalHotkeyError(registrationErrors.join(QLatin1Char('\n')));
            };

            registerGlobalHotkeys();

            QObject::connect(&settings, &SettingsController::hkPlayPauseChanged,
                             &app, registerGlobalHotkeys);
            QObject::connect(&settings, &SettingsController::hkPrevNextChanged,
                             &app, registerGlobalHotkeys);
            QObject::connect(&settings, &SettingsController::hkVolumeUpDownChanged,
                             &app, registerGlobalHotkeys);
            QObject::connect(&settings, &SettingsController::hkToggleMiniPlayerChanged,
                             &app, registerGlobalHotkeys);

            QObject::connect(&hotkeys, &GlobalHotkeyManager::triggered, &app,
                             [&](GlobalHotkeyManager::Action action) {
                                 switch (action) {
                                 case GlobalHotkeyManager::Action::PlayPause:
                                     playback.togglePlayback();
                                     break;
                                 case GlobalHotkeyManager::Action::Previous:
                                     playback.previous();
                                     break;
                                 case GlobalHotkeyManager::Action::Next:
                                     playback.next();
                                     break;
                                 case GlobalHotkeyManager::Action::VolumeUp:
                                     playback.volumeUp();
                                     break;
                                 case GlobalHotkeyManager::Action::VolumeDown:
                                     playback.volumeDown();
                                     break;
                                 case GlobalHotkeyManager::Action::ToggleMiniPlayer:
                                     windows.toggleMiniPlayer();
                                     break;
                                 }
                             });

            // --qa-screenshot-main / --qa-screenshot-mini: capture the empty
            // main surface directly, or wait for playback when a track exists.
            const bool wantScreenshotMain = !qaScreenshotMain.isEmpty();
            const bool wantScreenshotMini = !qaScreenshotMini.isEmpty();
            const bool wantScreenshotTools = !qaScreenshotTools.isEmpty();
            const bool wantScreenshotList = !qaScreenshotList.isEmpty();
            if (wantScreenshotTools) {
                (void)ensureAudioToolsWindow();
            }
            if (wantScreenshotMini && miniWindow != nullptr) {
                auto* miniWin = qobject_cast<QWindow*>(miniWindow);
                if (miniWin) {
                    miniWin->show();
                }
            }
            if (wantScreenshotTools && audioToolsWindow != nullptr) {
                if (auto* toolsWin = qobject_cast<QWindow*>(audioToolsWindow)) {
                    if (qaToolsSize.isValid()) {
                        toolsWin->resize(qaToolsSize);
                    }
                    toolsWin->show();
                }
                if (qaTool == 0 && !qaImportFolder.isEmpty()) {
                    audioEditor.openFileWhenReady(
                        QUrl::fromLocalFile(qaImportFolder), &app,
                        [&audioEditor, qaEditorReferenceState,
                         qaEditorSelectionStartMs, qaEditorSelectionEndMs,
                         qaEditorPlayheadMs] {
                            const qint64 rate = qMax<qint64>(
                                1, audioEditor.sampleRate());
                            if (qaEditorReferenceState) {
                                audioEditor.setOriginalBpm(128.0);
                                audioEditor.setPitch(2, 0);
                                audioEditor.setKeepPitch(true);
                                audioEditor.setFormantPreservation(true);
                            }
                            if (qaEditorSelectionStartMs >= 0
                                && qaEditorSelectionEndMs
                                    > qaEditorSelectionStartMs) {
                                audioEditor.setSelection(
                                    qaEditorSelectionStartMs * rate / 1'000,
                                    qaEditorSelectionEndMs * rate / 1'000);
                            }
                            if (qaEditorPlayheadMs >= 0) {
                                audioEditor.seekFrame(
                                    qaEditorPlayheadMs * rate / 1'000);
                            }
                        });
                } else if (qaTool == 1 && !qaImportFolder.isEmpty()) {
                    formatConverter.loadFiles(
                        {QUrl::fromLocalFile(qaImportFolder)});
                } else if (qaTool == 2 && !qaImportFolder.isEmpty()) {
                    metadataEditor.loadFiles(
                        {QUrl::fromLocalFile(qaImportFolder)});
                } else if (qaTool == 5 && !qaImportFolder.isEmpty()) {
                    losslessAnalysis.loadFiles({QUrl::fromLocalFile(qaImportFolder)});
                    losslessAnalysis.start();
                } else if (qaTool == 3 && !qaImportFolder.isEmpty()) {
                    if (QObject* filenamePage = audioToolsWindow->findChild<QObject*>(
                            QStringLiteral("filenameProcessPage"))) {
                        filenamePage->setProperty("qaReferenceMode", true);
                    }
                    filenameProcessor.loadFiles(
                        {QUrl::fromLocalFile(qaImportFolder)});
                }
            }
            if (wantScreenshotList && listWindow != nullptr) {
                if (qaListCategory != QStringLiteral("tags")
                    && filterModel != nullptr && !qaListCategory.isEmpty()) {
                    filterModel->setProperty("category", qaListCategory);
                }
                if (auto* listWin = qobject_cast<QWindow*>(listWindow)) {
                    listWin->show();
                }
            }
            if (wantScreenshotMain || wantScreenshotMini
                || wantScreenshotTools || wantScreenshotList) {
                const bool wantScreenshotImmersive = wantScreenshotMain
                    && QCoreApplication::arguments().contains(
                        QStringLiteral("--qa-immersive"));
                QWindow* const targetWindow = wantScreenshotMain
                    ? qobject_cast<QWindow*>(
                          qaOpenEqualizer && equalizerWindow != nullptr
                              ? equalizerWindow
                              : qaOpenSettings && settingsWindow != nullptr
                                  ? settingsWindow : mainWindow)
                    : wantScreenshotMini
                        ? qobject_cast<QWindow*>(miniWindow)
                        : wantScreenshotTools
                            ? qobject_cast<QWindow*>(audioToolsWindow)
                            : qobject_cast<QWindow*>(listWindow);
                const QString screenshotPath = wantScreenshotMain
                    ? qaScreenshotMain
                    : wantScreenshotMini
                        ? qaScreenshotMini
                        : wantScreenshotTools
                            ? qaScreenshotTools : qaScreenshotList;

                const auto captureWindow = [targetWindow, screenshotPath,
                                            mainWindow, &playback,
                                            &videoPlayback,
                                            wantScreenshotImmersive, wantScreenshotTools, qaTool]() {
                    QWindow* captureTarget = targetWindow;
                    if (wantScreenshotImmersive) {
                        captureTarget = mainWindow->findChild<QWindow*>(
                            QStringLiteral("immersiveVisualWindow"));
                    }
                    if (captureTarget == nullptr) {
                        qWarning("QA screenshot target window was not created");
                        QCoreApplication::quit();
                        return;
                    }
                    captureTarget->setVisible(true);
                    captureTarget->requestActivate();
                    if (QObject* waveform = mainWindow->findChild<QObject*>(
                            QStringLiteral("integratedWaveform"))) {
                        qInfo().noquote()
                            << "Integrated waveform QA state: durationMs="
                            << waveform->property("duration").toLongLong()
                            << "peakCount="
                            << waveform->property("peakCount").toLongLong()
                            << "visualMode="
                            << waveform->property("visualMode").toInt();
                    }
                    if (QObject* controls = mainWindow->findChild<QObject*>(
                            QStringLiteral("playerControls"))) {
                        qInfo().noquote()
                            << "Integrated controls QA geometry: x="
                            << controls->property("x").toReal()
                            << "width=" << controls->property("width").toReal();
                    }
                    if (QObject* center = mainWindow->findChild<QObject*>(
                            QStringLiteral("centerPlaybackControls"))) {
                        qInfo().noquote()
                            << "Integrated center controls QA geometry: x="
                            << center->property("x").toReal()
                            << "width=" << center->property("width").toReal();
                    }
                    if (wantScreenshotImmersive) {
                        QObject* const terrain = captureTarget->findChild<QObject*>(
                            QStringLiteral("terrainReactor"));
                        if (terrain != nullptr) {
                            auto* const terrainItem = qobject_cast<QQuickItem*>(terrain);
                            for (int attempt = 0;
                                 attempt < 20
                                 && terrain->property(terrain->property("useSyntheticFeatures").toBool()
                                                       ? "stableRenderedFrameCount" : "frameCount")
                                        .toULongLong() < 3;
                                 ++attempt) {
                                if (terrainItem != nullptr) terrainItem->update();
                                QEventLoop renderWait;
                                QTimer::singleShot(25, &renderWait,
                                                   &QEventLoop::quit);
                                renderWait.exec();
                            }
                            qInfo().noquote()
                                << "Immersive renderer QA state: status="
                                << terrain->property("renderStatus").toInt()
                                << "diagnostic="
                                << terrain->property("diagnostic").toString()
                                << "active=" << terrain->property("active").toBool()
                                << "hostExposed="
                                << terrain->property("hostExposed").toBool()
                                << "frames="
                                << terrain->property("frameCount").toULongLong()
                                << "stableFrames="
                                << terrain->property("stableRenderedFrameCount").toULongLong()
                                << "resources="
                                << terrain->property("resourceGeneration").toULongLong();
                            if (terrain->property(terrain->property("useSyntheticFeatures").toBool()
                                                    ? "stableRenderedFrameCount" : "frameCount")
                                    .toULongLong() < 3) {
                                qWarning("Immersive renderer did not present sufficient QA frames");
                                QCoreApplication::quit();
                                return;
                            }
                        } else {
                            qWarning("Immersive renderer QA item was not created");
                        }
                    }
                    qInfo().noquote()
                        << "Playback selection QA state: startMs="
                        << playback.selectionStartMs()
                        << "endMs=" << playback.selectionEndMs()
                        << "loop=" << playback.selectionLoopEnabled();
                    qInfo().noquote()
                        << "QA video diagnostics: visible="
                        << videoPlayback.visible()
                        << "workerRunning=" << videoPlayback.workerRunning()
                        << "queuedFrameCount="
                        << videoPlayback.queuedFrameCount()
                        << "queuedFrameBytes="
                        << videoPlayback.queuedFrameBytes()
                        << "frameSerial=" << videoPlayback.frameSerial()
                        << "fullscreen="
                        << mainWindow->property("videoFullscreen").toBool();
                    auto* const quickWin = qobject_cast<QQuickWindow*>(captureTarget);
                    if (quickWin != nullptr) {
                        quickWin->update();
                        QCoreApplication::processEvents(QEventLoop::AllEvents, 500);
                        QImage screenshot = quickWin->grabWindow();
                        qInfo().noquote()
                            << "QA capture scaling: dpr=" << quickWin->devicePixelRatio()
                            << "logical=" << quickWin->size()
                            << "pixels=" << screenshot.size();
                        // grabWindow() returns device pixels on a high-DPI
                        // monitor. Normalize QA artifacts to the window's
                        // logical size so the same 1228x399 contract is stable
                        // across monitors and DPI settings.
                        if (!(wantScreenshotTools && qaTool == 5) && !screenshot.isNull()
                            && screenshot.size() != quickWin->size()) {
                            screenshot = screenshot.scaled(
                                quickWin->size(), Qt::IgnoreAspectRatio,
                                Qt::SmoothTransformation);
                        }
                        if (screenshot.isNull()) {
                            qWarning("QA screenshot capture returned an empty image");
                        } else if (!screenshot.save(screenshotPath)) {
                            qWarning("QA screenshot could not be saved");
                        } else {
                            qInfo().noquote()
                                << "QA screenshot saved:" << screenshotPath
                                << screenshot.size();
                        }
                    } else {
                        qWarning("QA screenshot target is not a QQuickWindow");
                    }
                    // grabWindow() can leave a Qt Quick render job in flight.
                    // Hide the captured surface and let the render loop drain
                    // before QQmlApplicationEngine begins destroying windows.
                    captureTarget->setVisible(false);
                    QTimer::singleShot(250, QCoreApplication::quit);
                };

                if (wantScreenshotImmersive) {
                    QTimer::singleShot(qaTestMode ? qaCaptureDelayMs : 2500,
                                       captureWindow);
                } else if (wantScreenshotTools && qaTool == 0
                           && !qaImportFolder.isEmpty()) {
                    // The editor waveform is generated asynchronously.  A fixed capture
                    // delay can race the first viewport request at larger tool-window
                    // sizes and produce a misleading empty editor QA artifact.
                    auto* waveformReadyTimer = new QTimer(&app);
                    waveformReadyTimer->setInterval(50);
                    QObject::connect(waveformReadyTimer, &QTimer::timeout, &app,
                        [&audioEditor, waveformReadyTimer, attempts = 0,
                         captureWindow]() mutable {
                        if (audioEditor.hasDocument()
                            && !audioEditor.viewportChannelPeaks().isEmpty()) {
                            waveformReadyTimer->stop();
                            waveformReadyTimer->deleteLater();
                            QTimer::singleShot(250, captureWindow);
                            return;
                        }
                        if (++attempts > 200) { // 10s at 50ms: fail the artifact.
                            waveformReadyTimer->stop();
                            waveformReadyTimer->deleteLater();
                            qWarning("Timed out waiting for editor waveform QA readiness");
                            QCoreApplication::exit(2);
                        }
                    });
                    waveformReadyTimer->start();
                } else if (wantScreenshotTools && qaTool == 5
                           && (!qaPlayPath.isEmpty() || !qaImportFolder.isEmpty())) {
                    auto* readyTimer = new QTimer(&app);
                    readyTimer->setInterval(100);
                    auto attempts = std::make_shared<int>(0);
                    const bool captureRunning = QCoreApplication::arguments().contains(QStringLiteral("--qa-lossless-running"));
                    QObject::connect(readyTimer, &QTimer::timeout, &app,
                        [readyTimer, attempts, captureRunning, &losslessAnalysis, captureWindow]() {
                            ++*attempts;
                            const bool ready = captureRunning
                                ? losslessAnalysis.running()
                                : !losslessAnalysis.running() && losslessAnalysis.totalCount() > 0;
                            if (ready && *attempts > 5) {
                                readyTimer->stop();readyTimer->deleteLater();captureWindow();
                            } else if (*attempts > 1800) {
                                readyTimer->stop();readyTimer->deleteLater();
                                qWarning("Timed out waiting for lossless analysis QA state");
                                QCoreApplication::exit(2);
                            }
                        });
                    readyTimer->start();
                } else if (wantScreenshotTools
                           || (wantScreenshotMain && library.count() == 0
                               && qaPlayPath.isEmpty())) {
                    const bool measureLosslessIdle = wantScreenshotTools && qaTool == 5
                        && QCoreApplication::arguments().contains(QStringLiteral("--qa-lossless-idle"));
                    QTimer::singleShot(measureLosslessIdle ? 12000 : 1500, captureWindow);
                } else if (wantScreenshotList
                           && qaListCategory == QStringLiteral("tags")) {
                    QTimer::singleShot(1500, captureWindow);
                } else if (wantScreenshotList) {
                    auto attempts = std::make_shared<int>(0);
                    auto pollFunc = std::make_shared<std::function<void()>>();
                    *pollFunc = [&importer, &library,
                                 attempts, pollFunc,
                                 captureWindow]() {
                        if (!importer.busy() && library.count() > 0) {
                            QTimer::singleShot(1500, captureWindow);
                            return;
                        }
                        if (++(*attempts) > 400) { // 20s timeout at 50ms polls
                            QCoreApplication::quit();
                            return;
                        }
                        QTimer::singleShot(50, *pollFunc);
                    };
                    QTimer::singleShot(50, *pollFunc);
                } else {
                    auto attempts = std::make_shared<int>(0);
                    auto pollFunc = std::make_shared<std::function<void()>>();
                    *pollFunc = [core, &waveformProvider, &videoPlayback,
                                 qaExpectVideo, attempts, pollFunc,
                                 captureWindow]() {
                        ag_playback_snapshot snapshot{};
                        ag_player_snapshot(core, &snapshot);
                        const bool mediaReady = qaExpectVideo
                            ? videoPlayback.visible()
                                && videoPlayback.frameSerial() > 0
                            : waveformProvider.analysisProgress() >= 1.0;
                        if (snapshot.state == AG_PLAYING && mediaReady) {
                            QTimer::singleShot(1500, captureWindow);
                            return;
                        }
                        if (++(*attempts) > 200) { // 10s timeout at 50ms polls
                            qWarning("Timed out waiting for playback QA readiness");
                            QCoreApplication::exit(7);
                            return;
                        }
                        QTimer::singleShot(50, *pollFunc);
                    };
                    QTimer::singleShot(50, *pollFunc);
                }
            }
            app.enableFileOpenDelivery();
            result = app.exec();
            savePlaybackState(true);
            if (!tagModel.flush()) {
                qWarning().noquote()
                    << QCoreApplication::translate(
                           "Main", "Failed to save tag data after event loop exit");
            }

            QObject::disconnect(audioToolsVisibleConnection);
            QObject::disconnect(immersiveDropConnection);
            app.removeNativeEventFilter(&hotkeys);
            hotkeys.unregisterAll();
            windows.setShutdownActions({});

            if (audioToolsWindow)
                delete audioToolsWindow;
            destroyListWindow();
            if (miniWindow)
                delete miniWindow;
        }
    }

    if (core != nullptr) {
        ag_player_destroy(core);
    }
    RuntimeLog::uninstall();
    return result;
}
