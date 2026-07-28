#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QImage>
#include <QMenu>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QStringList>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QWindow>
#include <QtPlugin>

#include <agplayer/c_api.h>

#include <functional>
#include <memory>
#include <utility>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include "audio_tools_controller.hpp"
#include "format_converter.hpp"
#include "global_hotkey_manager.hpp"
#include "import_controller.hpp"
#include "library_model.hpp"
#include "library_store.hpp"
#include "light_editor_controller.hpp"
#include "metadata_editor.hpp"
#include "pitch_shifter.hpp"
#include "playback_controller.hpp"
#include "playlist_model.hpp"
#include "qml_registration.hpp"
#include "runtime_log.hpp"
#include "settings_controller.hpp"
#include "speed_adjuster.hpp"
#include "translation_manager.hpp"
#include "waveform_provider.hpp"
#include "window_controller.hpp"

Q_IMPORT_PLUGIN(AgPlayerPlugin)

ProbeResult probeMetadata(const QString& requestedPath, bool analyzeBpm);

int main(int argc, char* argv[])
{
    // The application supplies its own control visuals. A non-native style
    // keeps those visuals supported and consistent on Windows/macOS while
    // Theme.qml still follows the host system palette when requested.
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("AgPlayer"));
    app.setOrganizationName(QStringLiteral("AgPlayer"));

    // Development-only QA arguments. Parsed before ag_player_create so the
    // production player instance is reused (controllers are never bypassed).
    //   --qa-test-mode              isolate QStandardPaths from user data
    //   --qa-log <path>             write the runtime log to an explicit path
    //   --qa-play <path>            load + play a file through the normal path
    //   --qa-screenshot-main <png>  grab the main window after playback starts
    //   --qa-screenshot-mini <png>  grab the mini player window likewise
    //   --qa-tool <0..4>             choose the audio-tool screenshot page
    bool qaTestMode = false;
    QString qaLogPath;
    QString qaPlayPath;
    QString qaScreenshotMain;
    QString qaScreenshotMini;
    QString qaScreenshotTools;
    int qaTool = 1;
    QString qaScreenshotList;
    QString qaTheme;
    QString qaLanguage;
    bool qaOpenSettings = false;
    QString qaLibraryPath;
    QString qaImportFolder;
    QString initialFilePath;
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
            } else if (arg == QStringLiteral("--qa-screenshot-main")
                       && i + 1 < cliArgs.size()) {
                qaScreenshotMain = cliArgs.at(++i);
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
                if (ok && requestedTool >= 0 && requestedTool <= 4) {
                    qaTool = requestedTool;
                }
            } else if (arg == QStringLiteral("--qa-screenshot-list")
                       && i + 1 < cliArgs.size()) {
                qaScreenshotList = cliArgs.at(++i);
            } else if (arg == QStringLiteral("--qa-theme")
                       && i + 1 < cliArgs.size()) {
                qaTheme = cliArgs.at(++i).toLower();
            } else if (arg == QStringLiteral("--qa-language")
                       && i + 1 < cliArgs.size()) {
                qaLanguage = cliArgs.at(++i).toLower();
            } else if (arg == QStringLiteral("--qa-open-settings")) {
                qaOpenSettings = true;
            } else if (arg == QStringLiteral("--qa-library")
                       && i + 1 < cliArgs.size()) {
                qaLibraryPath = cliArgs.at(++i);
            } else if (arg == QStringLiteral("--qa-import-folder")
                       && i + 1 < cliArgs.size()) {
                qaImportFolder = cliArgs.at(++i);
            } else if (!arg.startsWith('-') && initialFilePath.isEmpty()) {
                initialFilePath = arg;
            }
        }
    }

    if (qaTestMode) {
        QStandardPaths::setTestModeEnabled(true);
    }
    RuntimeLog::install(qaLogPath);

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
        const QList<TrackRecord> loaded = store.load();
        if (!loaded.isEmpty()) {
            library.replaceAll(loaded);
        }

        PlaybackController playback(core, &library);
        SettingsController settings;
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
        auto autoReadBpmFlag = std::make_shared<std::atomic_bool>(
            settings.autoReadBpm() && qaImportFolder.isEmpty());
        ImportController importer(&library, [autoReadBpmFlag](const QString& path) {
            return probeMetadata(path, autoReadBpmFlag->load(std::memory_order_relaxed));
        });
        QObject::connect(&settings, &SettingsController::autoReadBpmChanged, &app,
                         [autoReadBpmFlag, &settings]() {
            autoReadBpmFlag->store(settings.autoReadBpm(), std::memory_order_relaxed);
        });
        WindowController windows;
        windows.setMagneticSnapEnabled(settings.windowMagneticSnap());
        windows.setPreferredDockEdge(settings.listWindowPosition());
        windows.setCloseBehavior(settings.closeBehavior());
        windows.setListWindowPanelAllowed(
            library.count() > 0 && settings.showListWindowPanel());

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
        if (!qaScreenshotTools.isEmpty()) {
            audioTools.selectTool(qaTool);
        }
        MetadataEditor metadataEditor;
        FormatConverter formatConverter;
        PitchShifter pitchShifter;
        SpeedAdjuster speedAdjuster;
        LightEditor lightEditor;

        register_agplayer_qml_types(&library, &playback, &importer, &windows,
                                    &audioTools, &metadataEditor,
                                    &formatConverter, &pitchShifter,
                                    &speedAdjuster, &lightEditor, &settings,
                                    &waveformProvider, &playlists);

        QString pendingPlayFilePath;

        auto playFileIfPending = [&]() {
            if (pendingPlayFilePath.isEmpty()) {
                return;
            }
            const QUrl url = QUrl::fromLocalFile(pendingPlayFilePath);
            importer.importUrls({url});
        };

        QObject::connect(&importer, &ImportController::finished, &app,
                         [&library, &playback, &pendingPlayFilePath]() {
            if (pendingPlayFilePath.isEmpty()) {
                return;
            }
            const int row = library.indexForLocalFile(pendingPlayFilePath);
            if (row >= 0) {
                playback.playRow(row);
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

        QObject::connect(&translations, &TranslationManager::languageChanged,
                         &app, updateTrayText);
        QObject::connect(&trayShowAction, &QAction::triggered,
                         &windows, [&windows, &tray]() {
            windows.showMain();
            tray.hide();
        });
        QObject::connect(&trayExitAction, &QAction::triggered,
                         &windows, &WindowController::requestExit);
        QObject::connect(&tray, &QSystemTrayIcon::activated, &windows,
                         [&windows, &tray](QSystemTrayIcon::ActivationReason reason) {
            if (reason == QSystemTrayIcon::Trigger
                || reason == QSystemTrayIcon::DoubleClick) {
                windows.showMain();
                tray.hide();
            }
        });
        QObject::connect(&windows, &WindowController::mainVisibleChanged,
                         &app, [&windows, &tray]() {
            if (windows.mainVisible()) {
                tray.hide();
            }
        });
        QObject::connect(&windows, &WindowController::miniVisibleChanged,
                         &app, [&windows, &tray]() {
            if (windows.miniVisible()) {
                tray.hide();
            }
        });

        WindowController::ShutdownActions shutdownActions;
        shutdownActions.cancelWaveform = [&importer]() { importer.cancel(); };
        shutdownActions.stopPlayback = [&core]() {
                if (core != nullptr) {
                    ag_player_stop(core);
                }
            };
        shutdownActions.flushLibrary = [&library, &playlists]() {
            library.flush();
            playlists.flush();
        };
        shutdownActions.releaseCore = [&playback, &core]() {
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

        if (!qaPlayPath.isEmpty()) {
            initialFilePath = qaPlayPath;
        }

        pendingPlayFilePath = initialFilePath;
        if (!pendingPlayFilePath.isEmpty() && !QFileInfo::exists(pendingPlayFilePath)) {
            pendingPlayFilePath.clear();
        }

        if (!engine.rootObjects().isEmpty()) {
            QObject* mainWindow = engine.rootObjects().first();

            // Load the mini player window from the same module so it shares the
            // registered singletons. WindowController toggles visibility between
            // the two QWindow instances via setWindows().
            QQmlComponent miniComponent(&engine);
            miniComponent.loadFromModule("AgPlayer", "MiniPlayerWindow");
            QObject* miniWindow = nullptr;
            if (!miniComponent.isError()) {
                miniWindow = miniComponent.create();
            }

            // Audio tools window: separate frameless window toggled from the
            // TitleBar. Loaded from the same module so it shares singletons.
            QQmlComponent audioToolsComponent(&engine);
            audioToolsComponent.loadFromModule("AgPlayer", "AudioToolsWindow");
            QObject* audioToolsWindow = nullptr;
            if (!audioToolsComponent.isError()) {
                audioToolsWindow = audioToolsComponent.create();
            }

            // Stand-alone playlist window. Reuses the LibraryFilterModel
            // instance owned by Main.qml so filtering state stays in sync.
            QObject* filterModel = mainWindow->findChild<QObject*>(
                QStringLiteral("filterModel"));
            if (filterModel != nullptr && !qaScreenshotList.isEmpty()) {
                filterModel->setProperty("minBpm", 0.0);
                filterModel->setProperty("maxBpm", 300.0);
            }
            QQmlComponent listComponent(&engine);
            listComponent.loadFromModule("AgPlayer", "ListWindow");
            QObject* listWindow = nullptr;
            if (!listComponent.isError()) {
                if (filterModel != nullptr) {
                    listWindow = listComponent.createWithInitialProperties(
                        QVariantMap{{QStringLiteral("filterModel"),
                                     QVariant::fromValue(filterModel)}});
                } else {
                    listWindow = listComponent.create();
                }
            }

            windows.setWindows(qobject_cast<QWindow*>(mainWindow),
                               qobject_cast<QWindow*>(miniWindow));
            windows.setListWindow(qobject_cast<QWindow*>(listWindow));
            windows.setAudioToolsWindow(qobject_cast<QWindow*>(audioToolsWindow));

            QObject::connect(&library, &LibraryModel::countChanged, &app,
                             [&library, &settings, &windows]() {
                windows.setListWindowPanelAllowed(
                    library.count() > 0 && settings.showListWindowPanel());
            });
            QObject::connect(&settings, &SettingsController::showListWindowPanelChanged,
                             &app, [&library, &settings, &windows]() {
                windows.setListWindowPanelAllowed(
                    library.count() > 0 && settings.showListWindowPanel());
            });
            if (qaOpenSettings) {
                QMetaObject::invokeMethod(mainWindow, "openSettingsPage");
            }

            playFileIfPending();
            if (!qaImportFolder.isEmpty()) {
                importer.importFolder(QUrl::fromLocalFile(qaImportFolder));
            }

            // Global hotkeys (Windows RegisterHotKey). Parsed from the settings
            // defaults and re-registered whenever the user changes a shortcut.
            GlobalHotkeyManager hotkeys;
            app.installNativeEventFilter(&hotkeys);

            auto registerGlobalHotkeys = [&]() {
                hotkeys.unregisterAll();
                if (qaTestMode) {
                    return;
                }

                const auto registerCombo = [&](const QString& combo,
                                               GlobalHotkeyManager::Action action) {
                    const QStringList parts = combo.split('/', Qt::SkipEmptyParts);
                    for (const QString& part : parts) {
                        hotkeys.registerShortcut(part.trimmed(), action);
                    }
                };

                const auto registerPair = [&](const QString& combo,
                                              GlobalHotkeyManager::Action first,
                                              GlobalHotkeyManager::Action second) {
                    const QStringList parts = combo.split('/', Qt::SkipEmptyParts);
                    if (parts.size() >= 1) {
                        hotkeys.registerShortcut(parts[0].trimmed(), first);
                    }
                    if (parts.size() >= 2) {
                        hotkeys.registerShortcut(parts[1].trimmed(), second);
                    }
                };

                registerCombo(settings.hkPlayPause(), GlobalHotkeyManager::Action::PlayPause);
                registerPair(settings.hkPrevNext(), GlobalHotkeyManager::Action::Previous,
                             GlobalHotkeyManager::Action::Next);
                registerPair(settings.hkVolumeUpDown(), GlobalHotkeyManager::Action::VolumeUp,
                             GlobalHotkeyManager::Action::VolumeDown);
                registerCombo(settings.hkToggleMiniPlayer(),
                              GlobalHotkeyManager::Action::ToggleMiniPlayer);
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
            if (wantScreenshotMini && miniWindow != nullptr) {
                auto* miniWin = qobject_cast<QWindow*>(miniWindow);
                if (miniWin) {
                    miniWin->show();
                }
            }
            if (wantScreenshotTools && audioToolsWindow != nullptr) {
                if (auto* toolsWin = qobject_cast<QWindow*>(audioToolsWindow)) {
                    toolsWin->show();
                }
            }
            if (wantScreenshotList && listWindow != nullptr) {
                if (auto* listWin = qobject_cast<QWindow*>(listWindow)) {
                    listWin->show();
                }
            }
            if (wantScreenshotMain || wantScreenshotMini
                || wantScreenshotTools || wantScreenshotList) {
                QWindow* const targetWindow = wantScreenshotMain
                    ? qobject_cast<QWindow*>(mainWindow)
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

                const auto captureWindow = [targetWindow, screenshotPath]() {
                    if (targetWindow == nullptr) {
                        qWarning("QA screenshot target window was not created");
                        QCoreApplication::quit();
                        return;
                    }
                    targetWindow->setVisible(true);
                    targetWindow->requestActivate();
                    auto* const quickWin = qobject_cast<QQuickWindow*>(targetWindow);
                    if (quickWin != nullptr) {
                        quickWin->update();
                        QCoreApplication::processEvents(QEventLoop::AllEvents, 500);
                        const QImage screenshot = quickWin->grabWindow();
                        if (screenshot.isNull()) {
                            qWarning("QA screenshot capture returned an empty image");
                        } else if (!screenshot.save(screenshotPath)) {
                            qWarning("QA screenshot could not be saved");
                        }
                    } else {
                        qWarning("QA screenshot target is not a QQuickWindow");
                    }
                    QCoreApplication::quit();
                };

                if (wantScreenshotTools || (wantScreenshotMain && library.count() == 0)) {
                    QTimer::singleShot(1500, captureWindow);
                } else if (wantScreenshotList) {
                    auto attempts = std::make_shared<int>(0);
                    auto pollFunc = std::make_shared<std::function<void()>>();
                    *pollFunc = [&importer, &library, attempts, pollFunc, captureWindow]() {
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
                    *pollFunc = [core, attempts, pollFunc, captureWindow]() {
                        ag_playback_snapshot snapshot{};
                        ag_player_snapshot(core, &snapshot);
                        if (snapshot.state == AG_PLAYING) {
                            QTimer::singleShot(1500, captureWindow);
                            return;
                        }
                        if (++(*attempts) > 200) { // 10s timeout at 50ms polls
                            QCoreApplication::quit();
                            return;
                        }
                        QTimer::singleShot(50, *pollFunc);
                    };
                    QTimer::singleShot(50, *pollFunc);
                }
            }
            // Trim the working set a few seconds after startup. During launch
            // the OS faults in many pages for DLL init, relocation, and one-time
            // setup that are not touched again during steady-state playback.
            // SetProcessWorkingSetSize(-1, -1) asks the OS to page those out,
            // reducing physical RAM usage. Actively-used pages (audio callback,
            // decode loop, QML scene graph) are re-faulted immediately and stay
            // resident, so playback is unaffected.
            QTimer::singleShot(3000, []() {
#ifdef Q_OS_WIN
                if (!SetProcessWorkingSetSize(GetCurrentProcess(),
                                              static_cast<SIZE_T>(-1),
                                              static_cast<SIZE_T>(-1))) {
                    qWarning("SetProcessWorkingSetSize failed: %lu",
                             GetLastError());
                }
#endif
            });

            result = app.exec();

            app.removeNativeEventFilter(&hotkeys);
            hotkeys.unregisterAll();
            windows.setShutdownActions({});

            if (audioToolsWindow)
                delete audioToolsWindow;
            if (listWindow)
                delete listWindow;
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
