cmake_minimum_required(VERSION 3.28)

include(cmake/AgPlayerVersion.cmake)

if(POLICY CMP0174)
    cmake_policy(SET CMP0174 NEW)
endif()

project(AgPlayer VERSION "${AGPLAYER_VERSION}" LANGUAGES C CXX)

file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated")
if(APPLE)
    set(AGPLAYER_DEFAULT_UPDATE_MANIFEST_URL "https://download.agplayer.com/updates/macos/latest.json")
else()
    set(AGPLAYER_DEFAULT_UPDATE_MANIFEST_URL "https://download.agplayer.com/updates/latest.json")
endif()
set(AGPLAYER_UPDATE_MANIFEST_URL "${AGPLAYER_DEFAULT_UPDATE_MANIFEST_URL}" CACHE STRING "Direct HTTPS URL of the official release manifest; empty disables update requests")
if(AGPLAYER_UPDATE_MANIFEST_URL AND NOT AGPLAYER_UPDATE_MANIFEST_URL MATCHES "^https://[A-Za-z0-9][A-Za-z0-9./_?=&%:+~-]*$")
    message(FATAL_ERROR "AGPLAYER_UPDATE_MANIFEST_URL must be a safe direct HTTPS URL")
endif()
configure_file(cmake/agplayer_update_config.hpp.in generated/agplayer_update_config.hpp @ONLY)
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

if(APPLE)
    include(cmake/MacOS.cmake)
    agplayer_validate_macos_target()
endif()

set(AGPLAYER_QT_COMPONENTS
    Core
    Gui
    Quick
    Qml
    QmlWorkerScript
    QuickControls2
    Concurrent
    Network
    LinguistTools
    Widgets
)
if(BUILD_TESTING)
    list(APPEND AGPLAYER_QT_COMPONENTS Test QuickTest)
endif()
find_package(Qt6 6.7 REQUIRED COMPONENTS ${AGPLAYER_QT_COMPONENTS})
# The vcpkg FindFFMPEG module caches the aggregate result.  In an existing
# build tree that cache otherwise stays stale when a newly enabled FFmpeg
# feature (such as swscale) is added to vcpkg.json.
unset(FFMPEG_FOUND CACHE)
unset(FFMPEG_LIBRARIES CACHE)
find_package(FFMPEG REQUIRED)
find_path(MINIAUDIO_INCLUDE_DIRS miniaudio.h REQUIRED)
find_package(SoundTouch CONFIG REQUIRED)
get_target_property(AGPLAYER_SOUNDTOUCH_TYPE SoundTouch::SoundTouch TYPE)
if(WIN32 AND NOT AGPLAYER_SOUNDTOUCH_TYPE STREQUAL "SHARED_LIBRARY")
    message(FATAL_ERROR "Windows releases require a replaceable SoundTouch DLL; rebuild the SoundTouch overlay as shared.")
endif()

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
if(BUILD_TESTING)
    add_subdirectory(tools)
    add_subdirectory(tests)
    include(cmake/LosslessSacd.cmake)
endif()
include(cmake/LosslessCore.cmake)
agplayer_enable_lossless_core()
if(BUILD_TESTING)
    add_executable(lossless_scan_cli tests/core/lossless_scan_cli.cpp)
    target_include_directories(lossless_scan_cli PRIVATE ${CMAKE_SOURCE_DIR}/core/src)
    target_link_libraries(lossless_scan_cli PRIVATE agplayer_core Qt6::Core)
    if(MSVC)
        target_compile_options(lossless_scan_cli PRIVATE /utf-8)
    endif()
endif()
include(cmake/LosslessQt.cmake)
include(cmake/LosslessUi.cmake)
if(BUILD_TESTING)
    add_test(NAME qml_lossless_identification_test
        COMMAND qml_audio_tools_test -o ${CMAKE_BINARY_DIR}/qml-lossless-identification.txt,txt
            -input ${CMAKE_SOURCE_DIR}/tests/qml/tst_lossless_identification.qml)
    set_tests_properties(qml_lossless_identification_test PROPERTIES
        TIMEOUT 60 RESOURCE_LOCK qml_audio_tools_runtime
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_CONTROLS_STYLE=Basic"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:AgPlayer>;PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/$<IF:$<CONFIG:Debug>,debug/bin,bin>")
endif()

set(AGPLAYER_TRANSLATION_TARGETS AgPlayer)
if(BUILD_TESTING)
    list(APPEND AGPLAYER_TRANSLATION_TARGETS translation_manager_test qml_main_window_test)
endif()
qt_add_translations(
    TARGETS ${AGPLAYER_TRANSLATION_TARGETS}
    SOURCE_TARGETS agplayer_app_qml agplayer_qt AgPlayer
    TS_FILES
        ${CMAKE_SOURCE_DIR}/translations/agplayer_zh.ts
        ${CMAKE_SOURCE_DIR}/translations/agplayer_en.ts
    LUPDATE_OPTIONS
        -no-obsolete
    RESOURCE_PREFIX "/i18n"
)
