cmake_minimum_required(VERSION 3.28)

include(cmake/AgPlayerVersion.cmake)

if(POLICY CMP0174)
    cmake_policy(SET CMP0174 NEW)
endif()

project(AgPlayer VERSION "${AGPLAYER_VERSION}" LANGUAGES C CXX)

file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated")
configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/agplayer_version.hpp.in"
    "${CMAKE_BINARY_DIR}/generated/agplayer_version.hpp"
    @ONLY
)

option(AG_ENABLE_AUDIO_EDITOR "Enable audio editor" ON)
option(AG_ENABLE_NATIVE_RECORDING "Enable native recording" ON)
option(AG_ENABLE_TIME_PITCH "Enable time and pitch processing" ON)

if(NOT WIN32)
    set(AG_ENABLE_NATIVE_RECORDING OFF CACHE BOOL
        "Native recording is currently implemented on Windows" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(Warnings)
include(CTest)

find_package(Qt6 6.7 REQUIRED COMPONENTS
    Core
    Gui
    Quick
    Qml
    QmlWorkerScript
    QuickControls2
    Concurrent
    Test
    QuickTest
    LinguistTools
    Widgets
)
find_package(FFMPEG REQUIRED)
find_path(MINIAUDIO_INCLUDE_DIRS miniaudio.h REQUIRED)
find_package(SoundTouch CONFIG REQUIRED)

add_subdirectory(core)
add_subdirectory(qt)
add_subdirectory(app)
add_subdirectory(tools)
add_subdirectory(tests)

qt_add_translations(
    TARGETS AgPlayer translation_manager_test
    SOURCE_TARGETS agplayer_app_qml agplayer_qt AgPlayer
    TS_FILES
        ${CMAKE_SOURCE_DIR}/translations/agplayer_zh.ts
        ${CMAKE_SOURCE_DIR}/translations/agplayer_en.ts
    LUPDATE_OPTIONS
        -no-obsolete
    RESOURCE_PREFIX "/i18n"
)
