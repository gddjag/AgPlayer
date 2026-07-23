set(CMAKE_AUTOMOC ON)

set(ICON_FILES
    ${CMAKE_SOURCE_DIR}/assets/icons/checkbox-blank-circle-fill.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/close-fill.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/equalizer-fill.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/folder-open-fill.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/fullscreen-exit-fill.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/fullscreen-fill.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/heart-fill.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/heart-line.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/music-2-fill.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/pause-fill.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/play-fill.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/playlist-2-fill.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/pushpin-fill.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/repeat-fill.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/repeat-one-fill.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/restore-line.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/shuffle-fill.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/skip-back-fill.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/skip-forward-fill.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/star-fill.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/star-line.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/subtract-line.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/volume-mute-fill.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/volume-up-fill.svg
)

set(BRAND_FILES
    ${CMAKE_SOURCE_DIR}/assets/brand/logo-lockup.png
    ${CMAKE_SOURCE_DIR}/assets/brand/logo-mark.png
)

foreach(resource_file ${BRAND_FILES} ${ICON_FILES})
    file(RELATIVE_PATH alias ${CMAKE_SOURCE_DIR} ${resource_file})
    set_source_files_properties(${resource_file} PROPERTIES QT_RESOURCE_ALIAS ${alias})
endforeach()

# Explicitly mark Theme.qml as a singleton type so the generated qmldir
# includes the 'singleton' keyword. Must be set BEFORE qt_add_qml_module
# processes the file. Without this, Theme.icon() calls fail with "Property
# 'icon' of object [object Object] is not a function" and Theme color
# properties resolve to undefined (QColor assignment errors).
set_source_files_properties(qml/AgPlayer/theme/Theme.qml PROPERTIES
    QT_QML_SINGLETON_TYPE TRUE
)

qt_add_qml_module(agplayer_app_qml
    STATIC
    URI AgPlayer
    VERSION 1.0
    QML_FILES
        qml/AgPlayer/Main.qml
        qml/AgPlayer/MiniPlayerWindow.qml
        qml/AgPlayer/AudioToolsWindow.qml
        qml/AgPlayer/theme/Theme.qml
        qml/AgPlayer/components/TitleBar.qml
        qml/AgPlayer/components/PlayerPane.qml
        qml/AgPlayer/components/PlayerControls.qml
        qml/AgPlayer/components/TrackList.qml
        qml/AgPlayer/components/EmptyLibrary.qml
        qml/AgPlayer/components/MiniPlayerControls.qml
        qml/AgPlayer/components/SideNavigation.qml
        qml/AgPlayer/components/SearchFilter.qml
        qml/AgPlayer/components/tools/ComingSoonPage.qml
        qml/AgPlayer/components/tools/FormatConvertPage.qml
        qml/AgPlayer/components/tools/ToolSidebar.qml
        qml/AgPlayer/components/tools/InfoEditPage.qml
        qml/AgPlayer/components/tools/LightEditPage.qml
        qml/AgPlayer/components/tools/MultiTrackWaveform.qml
        qml/AgPlayer/components/tools/PitchShiftPage.qml
        qml/AgPlayer/components/tools/SpeedAdjustPage.qml
    RESOURCES
        ${BRAND_FILES}
        ${ICON_FILES}
)

set_target_properties(agplayer_app_qml PROPERTIES AUTOMOC ON)

target_link_libraries(agplayer_app_qml
    PUBLIC
        Qt6::Quick
        Qt6::QuickControls2
        agplayer_qt
)

qt_add_executable(AgPlayer main.cpp)
target_link_libraries(AgPlayer PRIVATE agplayer_app_qml agplayer_app_qmlplugin)
set_target_properties(AgPlayer PROPERTIES WIN32_EXECUTABLE TRUE)
agplayer_enable_warnings(AgPlayer)
