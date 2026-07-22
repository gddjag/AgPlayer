#include "qml_registration.hpp"

#include "library_model.hpp"

#include <QtQml/qqml.h>

#include <mutex>

void registerAgPlayerQmlTypes()
{
    static std::once_flag registered;
    std::call_once(registered, [] {
        qmlRegisterType<LibraryModel>("AgPlayer", 1, 0, "LibraryModel");
    });
}
