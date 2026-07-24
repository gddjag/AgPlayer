#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QImage>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QStringList>
#include <QTimer>
#include <QWindow>
#include <QtPlugin>

#include <agplayer/c_api.h>

#include <functional>
#include <memory>

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
#include "qml_registration.hpp"
#include "runtime_log.hpp"
#include "settings_controller.hpp"
#include "speed_adjuster.hpp"
#include "window_controller.hpp"

Q_IMPORT_PLUGIN(AgPlayerPlugin)

ProbeResult probeMetadata(const QString& requestedPath, bool analyzeBpm);

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("AgPlayer"));
    app.setOrganizationName(QStringLiteral("AgPlayer"));

    RuntimeLog::install();

    // Development-only QA arguments. Parsed before ag_player_create so the
    // production player instance is reused (controllers are never bypassed).
    //   --qa-play <path>            load + play a file through the normal path
    //   --qa-screenshot-main <png>  grab the main window after playback starts
    //   --qa-screenshot-mini <png>  grab the mini player window likewise
    QString qaPlayPath;
    QString qaScreenshotMain;
    QString qaScreenshotMini;
    QString initialFilePath;
    {
        const QStringList cliArgs = QGuiApplication::arguments();
        for (int i = 1; i < cliArgs.size(); ++i) {
            const QString& arg = cliArgs.at(i);
            if (arg == QStringLiteral("--qa-play") && i + 1 < cliArgs.size()) {
                qaPlayPath = cliArgs.at(++i);
            } else if (arg == QStringLiteral("--qa-screenshot-main")
                       && i + 1 < cliArgs.size()) {
                qaScreenshotMain = cliArgs.at(++i);
            } else if (arg == QStringLiteral("--qa-screenshot-mini")
                       && i + 1 < cliArgs.size()) {
                qaScreenshotMini = cliArgs.at(++i);
            } else if (!arg.startsWith('-') && initialFilePath.isEmpty()) {
                initialFilePath = arg;
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
        const QString libraryPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
            + QStringLiteral("/library.json");
        LibraryStore store(libraryPath);
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
        const bool autoReadBpm = settings.autoReadBpm();
        ImportController importer(&library, [autoReadBpm](const QString& path) {
            return probeMetadata(path, autoReadBpm);
        });
        WindowController windows;
        AudioToolsController audioTools;
        MetadataEditor metadataEditor;
        FormatConverter formatConverter;
        PitchShifter pitchShifter;
        SpeedAdjuster speedAdjuster;
        LightEditor lightEditor;

        register_agplayer_qml_types(&library, &playback, &importer, &windows,
                                    &audioTools, &metadataEditor,
                                    &formatConverter, &pitchShifter,
                                    &speedAdjuster, &lightEditor, &settings);

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

        windows.setShutdownActions({
            [&importer]() { importer.cancel(); },
            [&core]() {
                if (core != nullptr) {
                    ag_player_stop(core);
                }
            },
            [&library]() { library.flush(); },
            [&playback, &core]() {
                playback.setPlayer(nullptr);
                if (core != nullptr) {
                    ag_player_destroy(core);
                    core = nullptr;
                }
            },
            []() { QCoreApplication::quit(); }
        });

        QQmlApplicationEngine engine;
        engine.addImportPath("qrc:/");
        engine.loadFromModule("AgPlayer", "Main");

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

            // The playlist window is a permanent second window in the new UI;
            // show it immediately below the main player window.
            windows.showListWindow();

            playFileIfPending();

            // Global hotkeys (Windows RegisterHotKey). Parsed from the settings
            // defaults and re-registered whenever the user changes a shortcut.
            GlobalHotkeyManager hotkeys;
            app.installNativeEventFilter(&hotkeys);

            auto registerGlobalHotkeys = [&]() {
                hotkeys.unregisterAll();

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

            // --qa-screenshot-main / --qa-screenshot-mini: poll for AG_PLAYING,
            // wait 500ms for the waveform to render, grab the window, save the
            // PNG, then quit through the normal shutdown path.
            const bool wantScreenshotMain = !qaScreenshotMain.isEmpty();
            const bool wantScreenshotMini = !qaScreenshotMini.isEmpty();
            if (wantScreenshotMini && miniWindow != nullptr) {
                auto* miniWin = qobject_cast<QWindow*>(miniWindow);
                if (miniWin) {
                    miniWin->show();
                }
            }
            if (wantScreenshotMain || wantScreenshotMini) {
                QWindow* const targetWindow = wantScreenshotMain
                    ? qobject_cast<QWindow*>(mainWindow)
                    : qobject_cast<QWindow*>(miniWindow);
                const QString screenshotPath = wantScreenshotMain
                    ? qaScreenshotMain : qaScreenshotMini;

                auto attempts = std::make_shared<int>(0);
                auto pollFunc = std::make_shared<std::function<void()>>();
                *pollFunc = [core, targetWindow, screenshotPath,
                             attempts, pollFunc]() {
                    ag_playback_snapshot snapshot{};
                    ag_player_snapshot(core, &snapshot);
                    if (snapshot.state == AG_PLAYING) {
                        // Ensure the window is visible and rendered before grabbing
                        targetWindow->setVisible(true);
                        targetWindow->requestActivate();
                        QTimer::singleShot(1500, [targetWindow, screenshotPath]() {
                            auto* const quickWin =
                                qobject_cast<QQuickWindow*>(targetWindow);
                            if (quickWin != nullptr) {
                                quickWin->update();
                                // Process events to let the scene graph render
                                QCoreApplication::processEvents(
                                    QEventLoop::AllEvents, 500);
                                quickWin->grabWindow().save(screenshotPath);
                            }
                            QCoreApplication::quit();
                        });
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
