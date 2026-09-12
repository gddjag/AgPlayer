set(CMAKE_AUTOMOC ON)

set(AGPLAYER_PLATFORM_SOURCES)
if(WIN32)
    configure_file(agplayer.manifest.in
        "${CMAKE_CURRENT_BINARY_DIR}/agplayer.manifest" @ONLY)
    configure_file(agplayer.rc.in
        "${CMAKE_CURRENT_BINARY_DIR}/agplayer.rc" @ONLY)
    list(APPEND AGPLAYER_PLATFORM_SOURCES
        "${CMAKE_CURRENT_BINARY_DIR}/agplayer.rc"
        "${CMAKE_CURRENT_BINARY_DIR}/agplayer.manifest")
endif()

set(ICON_FILES
    ${CMAKE_SOURCE_DIR}/assets/icons/add-line.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/arrow-down-s-line.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/arrow-go-back-line.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/arrow-go-forward-line.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/arrow-up-s-line.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/arrow-right-s-line.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/bar-chart-line.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/briefcase-4-line.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/brush-line.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/checkbox-blank-line.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/checkbox-blank-fill.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/checkbox-blank-circle-fill.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/checkbox-circle-line.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/check-line.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/checkbox-circle-fill.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/close-fill.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/close-line.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/clipboard-line.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/crop-line.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/cursor-line.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/delete-bin-line.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/equalizer-line.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/error-warning-line.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/file-copy-line.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/file-music-fill.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/folder-open-fill.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/folder-open-line.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/folder-add-line.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/fullscreen-exit-fill.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/fullscreen-fill.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/heart-fill.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/heart-line.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/information-line.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/keyboard-box-line.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/list-unordered.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/lyrics.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/lucide-audio-lines.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/lucide-box.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/lucide-drum.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/lucide-guitar.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/lucide-mic.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/music-2-line.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/music-2-fill.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/pause-fill.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/picture-in-picture-2-line.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/play-fill.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/play-order-line.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/price-tag-3-line.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/pushpin-line.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/repeat-list-line.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/repeat-one-line-alt.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/rewind-fill.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/restore-line.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/save-3-line.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/search-line.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/file-search-line.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/scissors-cut-line.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/settings-3-fill.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/shuffle-arrows-line.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/side-panel-toggle.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/skip-back-fill.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/skip-forward-fill.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/stop-fill.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/speed-fill.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/star-fill.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/star-line.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/subtract-line.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/sound-module-line.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/theme-skin.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/time-line.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/volume-mute-line.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/volume-up-line.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/waveform-switch.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/user-playlist.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/user-add-resource-folder.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/user-resource-folder.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/user-resource-root-red.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/user-resource-subfolder.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/user-tag.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/user-library.svg
)

set(BRAND_FILES
    ${CMAKE_SOURCE_DIR}/assets/brand/agplayer.ico
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

# Adopt Qt6 default QML resource prefix to avoid policy warning.
qt_policy(SET QTP0001 NEW)

qt_add_qml_module(agplayer_app_qml
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/AgPlayer"
    STATIC
    URI AgPlayer
    VERSION 1.0
    QML_FILES
        qml/AgPlayer/Main.qml
        qml/AgPlayer/ImmersiveWindow.qml
        qml/AgPlayer/MiniPlayerWindow.qml
        qml/AgPlayer/ListWindow.qml
        qml/AgPlayer/AudioToolsWindow.qml
        qml/AgPlayer/EqualizerWindow.qml
        qml/AgPlayer/SettingsWindow.qml
        qml/AgPlayer/SettingsPage.qml
        qml/AgPlayer/theme/Theme.qml
        qml/AgPlayer/components/TitleBar.qml
        qml/AgPlayer/components/IntegratedPlayerShell.qml
    qml/AgPlayer/components/RollingPlayerShell.qml
    qml/AgPlayer/components/BeatGridOverlay.qml
        qml/AgPlayer/components/WaveSelectionOverlay.qml
        qml/AgPlayer/components/WaveformSession.qml
        qml/AgPlayer/components/SharedWaveformView.qml
        qml/AgPlayer/components/FullTrackWaveformView.qml
        qml/AgPlayer/components/PlayerPane.qml
        qml/AgPlayer/components/PlayerControls.qml
        qml/AgPlayer/components/TransportControls.qml
        qml/AgPlayer/components/PlayerVolumeControl.qml
        qml/AgPlayer/components/VideoPlaybackView.qml
        qml/AgPlayer/components/VideoTransportBar.qml
        qml/AgPlayer/components/IntegratedPlayerControls.qml
        qml/AgPlayer/components/ExperienceActions.qml
        qml/AgPlayer/components/AnimatedImmersiveIcon.qml
        qml/AgPlayer/components/PlayerPresentation.js
        qml/AgPlayer/components/FallbackCoverImage.qml
        qml/AgPlayer/components/ImmersiveSurface.qml
        qml/AgPlayer/components/ImmersiveColumnGlow.qml
        qml/AgPlayer/components/ImmersiveControlPanel.qml
        qml/AgPlayer/components/ImmersiveQueueDrawer.qml
        qml/AgPlayer/components/LyricsPanel.qml
        qml/AgPlayer/components/LibrarySidePanel.qml
        qml/AgPlayer/components/EqualizerBandSlider.qml
        qml/AgPlayer/components/EqualizerResponseCurve.qml
        qml/AgPlayer/components/TrackList.qml
        qml/AgPlayer/components/TrackSubtitle.qml
        qml/AgPlayer/components/TrackWaveformThumbnail.qml
        qml/AgPlayer/components/TagManagementPanel.qml
        qml/AgPlayer/components/EmptyLibrary.qml
        qml/AgPlayer/components/ImportStatusPanel.qml
        qml/AgPlayer/components/FileDropArea.qml
        qml/AgPlayer/components/DockedWindowFrame.qml
        qml/AgPlayer/components/WindowResizeHandles.qml
        qml/AgPlayer/components/MiniPlayerControls.qml
        qml/AgPlayer/components/SideNavigation.qml
        qml/AgPlayer/components/LibraryNavigationActions.qml
        qml/AgPlayer/components/SearchFilter.qml
        qml/AgPlayer/components/ColorField.qml
        qml/AgPlayer/components/ThemedIcon.qml
        qml/AgPlayer/components/ThemedMenuItem.qml
        qml/AgPlayer/components/ThemedButton.qml
        qml/AgPlayer/components/ThemedIconButton.qml
        qml/AgPlayer/components/ThemedTextField.qml
        qml/AgPlayer/components/ThemedSlider.qml
        qml/AgPlayer/components/ThemedListRow.qml
        qml/AgPlayer/components/ThemedPanel.qml
        qml/AgPlayer/components/ThemedTabButton.qml
        qml/AgPlayer/components/ThemedDialog.qml
        qml/AgPlayer/components/ThemedToolTip.qml
        qml/AgPlayer/components/ThemedScrollBar.qml
        qml/AgPlayer/components/ThemedSwitch.qml
        qml/AgPlayer/components/ThemedCheckBox.qml
        qml/AgPlayer/components/ThemedRadioButton.qml
        qml/AgPlayer/components/ThemedRangeSlider.qml
        qml/AgPlayer/components/ThemedComboBox.qml
        qml/AgPlayer/components/tools/FormatConvertPage.qml
        qml/AgPlayer/components/tools/FormatTaskTable.qml
        qml/AgPlayer/components/tools/FormatSettingsPanel.qml
        qml/AgPlayer/components/tools/FormatPreflightDialog.qml
        qml/AgPlayer/components/tools/FormatErrorDialog.qml
        qml/AgPlayer/components/tools/FilenameProcessPage.qml
        qml/AgPlayer/components/tools/ToolSidebar.qml
        qml/AgPlayer/components/tools/VocalSeparationPage.qml
        qml/AgPlayer/components/tools/MetadataEditPage.qml
        qml/AgPlayer/components/tools/AudioEditorPage.qml
        qml/AgPlayer/components/tools/LosslessIdentifyPage.qml
        qml/AgPlayer/components/tools/LosslessTaskPanel.qml
        qml/AgPlayer/components/tools/LosslessEvidencePanel.qml
        qml/AgPlayer/components/tools/LosslessConclusionPanel.qml
        qml/AgPlayer/components/tools/LosslessConfidenceRing.qml
        qml/AgPlayer/components/tools/LosslessProgressBar.qml
        qml/AgPlayer/components/audioeditor/EditorCommandBar.qml
        qml/AgPlayer/components/audioeditor/FileSummaryBar.qml
        qml/AgPlayer/components/audioeditor/EditorWaveformCanvas.qml
        qml/AgPlayer/components/audioeditor/EditorSlider.qml
        qml/AgPlayer/components/audioeditor/EditorStatusBar.qml
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

qt_add_executable(AgPlayer
    main.cpp
    agplayer_application.hpp
    ${AGPLAYER_PLATFORM_SOURCES}
)
if(APPLE)
    set_target_properties(AgPlayer PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.agplayer.AgPlayer"
        MACOSX_BUNDLE_BUNDLE_NAME "AgPlayer"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/macos/Info.plist.in"
        INSTALL_RPATH "@executable_path/../Frameworks")
endif()
target_include_directories(AgPlayer PRIVATE "${CMAKE_BINARY_DIR}/generated")
if(TARGET AgSeparationWorker)
    add_dependencies(AgPlayer AgSeparationWorker)
    add_custom_command(TARGET AgPlayer POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:AgSeparationWorker>"
            "$<TARGET_FILE_DIR:AgPlayer>"
        COMMENT "Deploying the on-demand separation Worker"
        VERBATIM
    )
endif()
target_link_libraries(AgPlayer PRIVATE
    Qt6::Widgets
    agplayer_app_qml
    agplayer_app_qmlplugin
)
if(WIN32)
    target_link_libraries(AgPlayer PRIVATE ole32 shell32 propsys)
endif()
set_target_properties(AgPlayer PROPERTIES WIN32_EXECUTABLE TRUE)
agplayer_enable_warnings(AgPlayer)

if(WIN32)
    get_filename_component(AGPLAYER_QT_PREFIX "${Qt6_DIR}/../../.." ABSOLUTE)
    find_program(AGPLAYER_WINDEPLOYQT_EXECUTABLE
        NAMES windeployqt
        HINTS "${AGPLAYER_QT_PREFIX}/bin"
        REQUIRED
    )
    add_custom_command(TARGET AgPlayer POST_BUILD
        COMMAND "${AGPLAYER_WINDEPLOYQT_EXECUTABLE}"
            "$<IF:$<CONFIG:Debug>,--debug,--release>"
            --no-translations --no-system-d3d-compiler --no-opengl-sw
            --skip-plugin-types qmltooling --force
            "$<TARGET_FILE:AgPlayer>"
        COMMENT "Deploying AgPlayer Qt runtime"
        VERBATIM
    )
endif()
