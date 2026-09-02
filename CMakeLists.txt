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
option(AG_ENABLE_TIME_PITCH "Enable time and pitch processing" ON)
unset(AG_ENABLE_NATIVE_RECORDING CACHE)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(Warnings)
include(CTest)
include(FetchContent)

find_package(Qt6 6.7 REQUIRED COMPONENTS
    Core
    Gui
    Quick
    Qml
    QmlWorkerScript
    QuickControls2
    Concurrent
    Network
    Test
    QuickTest
    LinguistTools
    Widgets
)
# The vcpkg FindFFMPEG module caches the aggregate result.  In an existing
# build tree that cache otherwise stays stale when a newly enabled FFmpeg
# feature (such as swscale) is added to vcpkg.json.
unset(FFMPEG_FOUND CACHE)
unset(FFMPEG_LIBRARIES CACHE)
find_package(FFMPEG REQUIRED)
find_path(MINIAUDIO_INCLUDE_DIRS miniaudio.h REQUIRED)
find_package(SoundTouch CONFIG REQUIRED)

# Keep the DSP source reproducible and offline-cacheable.  Both dependencies
# are header-only targets, so this adds no runtime DLL.
FetchContent_Declare(signalsmith_linear
    URL https://codeload.github.com/Signalsmith-Audio/linear/zip/refs/tags/0.3.1
    URL_HASH SHA256=DC44DE0DFC3AA25BC215CDAB5467E1FEC19E3FB4938AA32BF45FC5EA7EB59C7B
)
FetchContent_MakeAvailable(signalsmith_linear)

FetchContent_Declare(signalsmith_stretch
    URL https://codeload.github.com/Signalsmith-Audio/signalsmith-stretch/zip/57b93f4e9206a089a45387eaa39bdc9f310d3308
    URL_HASH SHA256=441F029651286E746399793F1FD90E3589A8B6F28B63512F8FE441B9131D722C
)
FetchContent_MakeAvailable(signalsmith_stretch)

add_subdirectory(core)
add_subdirectory(qt)
add_subdirectory(worker)
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
