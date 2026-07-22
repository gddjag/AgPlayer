#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QStandardPaths>
#include <QWindow>
#include <qqml.h>

#include <agplayer/c_api.h>

#include "import_controller.hpp"
#include "library_model.hpp"
#include "library_store.hpp"
#include "playback_controller.hpp"
#include "runtime_log.hpp"
#include "waveform_item.hpp"
#include "window_controller.hpp"

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("AgPlayer"));
    app.setOrganizationName(QStringLiteral("AgPlayer"));

    RuntimeLog::install();

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
        ImportController importer(&library);
        WindowController windows;

        qmlRegisterSingletonInstance("AgPlayer", 1, 0, "LibraryModel", &library);
        qmlRegisterSingletonInstance("AgPlayer", 1, 0, "PlaybackController", &playback);
        qmlRegisterSingletonInstance("AgPlayer", 1, 0, "ImportController", &importer);
        qmlRegisterSingletonInstance("AgPlayer", 1, 0, "WindowController", &windows);
        qmlRegisterType<WaveformItem>("AgPlayer", 1, 0, "WaveformItem");

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
        engine.loadFromModule("AgPlayer", "Main");
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

            windows.setWindows(qobject_cast<QWindow*>(mainWindow),
                               qobject_cast<QWindow*>(miniWindow));

            result = app.exec();

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
