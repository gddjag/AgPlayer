#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <qqml.h>

#include <agplayer/c_api.h>

#include "import_controller.hpp"
#include "library_model.hpp"
#include "playback_controller.hpp"
#include "waveform_item.hpp"
#include "window_controller.hpp"

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("AgPlayer"));
    app.setOrganizationName(QStringLiteral("AgPlayer"));

    ag_player* core = nullptr;
    if (ag_player_create(&core) != AG_OK)
        return 2;

    int result = 1;
    {
        LibraryModel library;
        PlaybackController playback(core, &library);
        ImportController importer(&library);
        WindowController windows;

        qmlRegisterSingletonInstance("AgPlayer", 1, 0, "LibraryModel", &library);
        qmlRegisterSingletonInstance("AgPlayer", 1, 0, "PlaybackController", &playback);
        qmlRegisterSingletonInstance("AgPlayer", 1, 0, "ImportController", &importer);
        qmlRegisterSingletonInstance("AgPlayer", 1, 0, "WindowController", &windows);
        qmlRegisterType<WaveformItem>("AgPlayer", 1, 0, "WaveformItem");

        QQmlApplicationEngine engine;
        engine.loadFromModule("AgPlayer", "Main");
        if (!engine.rootObjects().isEmpty())
            result = app.exec();
    }

    ag_player_destroy(core);
    return result;
}
