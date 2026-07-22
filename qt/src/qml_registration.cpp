#include "qml_registration.hpp"

#include "library_model.hpp"
#include "playback_controller.hpp"
#include "waveform_item.hpp"
#include "window_controller.hpp"

#include <QtQml/qqml.h>

#include <mutex>

void registerAgPlayerQmlTypes()
{
    static std::once_flag baseTypesRegistered;
    std::call_once(baseTypesRegistered, [] {
        qmlRegisterType<LibraryModel>("AgPlayer", 1, 0, "LibraryModel");
        qmlRegisterType<WaveformItem>("AgPlayer", 1, 0, "WaveformItem");
    });
}

void registerAgPlayerQmlTypes(PlaybackController& playbackController,
                              WindowController& windowController)
{
    registerAgPlayerQmlTypes();
    static std::once_flag controllerSingletonsRegistered;
    std::call_once(controllerSingletonsRegistered, [&playbackController, &windowController] {
        qmlRegisterSingletonInstance(
            "AgPlayer", 1, 0, "PlaybackController", &playbackController);
        qmlRegisterSingletonInstance(
            "AgPlayer", 1, 0, "WindowController", &windowController);
    });
}
