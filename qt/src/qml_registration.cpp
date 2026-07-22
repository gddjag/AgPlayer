#include "qml_registration.hpp"

#include "library_model.hpp"
#include "playback_controller.hpp"
#include "window_controller.hpp"

#include <QQmlEngine>
#include <QtQml/qqml.h>

#include <mutex>

void registerAgPlayerQmlTypes()
{
    registerAgPlayerQmlTypes(nullptr, nullptr);
}

void registerAgPlayerQmlTypes(PlaybackController* playbackController,
                              WindowController* windowController)
{
    static std::once_flag registered;
    std::call_once(registered, [playbackController, windowController] {
        qmlRegisterType<LibraryModel>("AgPlayer", 1, 0, "LibraryModel");
        if (playbackController != nullptr) {
            qmlRegisterSingletonInstance(
                "AgPlayer", 1, 0, "PlaybackController", playbackController);
        } else {
            qmlRegisterSingletonType<PlaybackController>(
                "AgPlayer", 1, 0, "PlaybackController",
                [](QQmlEngine* engine, QJSEngine*) -> QObject* {
                    return new PlaybackController(nullptr, nullptr, engine);
                });
        }
        if (windowController != nullptr) {
            qmlRegisterSingletonInstance(
                "AgPlayer", 1, 0, "WindowController", windowController);
        } else {
            qmlRegisterSingletonType<WindowController>(
                "AgPlayer", 1, 0, "WindowController",
                [](QQmlEngine* engine, QJSEngine*) -> QObject* {
                    return new WindowController(engine);
                });
        }
    });
}
