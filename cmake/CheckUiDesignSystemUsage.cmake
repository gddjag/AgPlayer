cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED ROOT)
    message(FATAL_ERROR "ROOT is required")
endif()

function(require_text relative_path expected)
    file(READ "${ROOT}/${relative_path}" source)
    string(FIND "${source}" "${expected}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR
            "${relative_path} must use the UI design-system contract: ${expected}")
    endif()
endfunction()

file(GLOB_RECURSE qml_files "${ROOT}/app/qml/AgPlayer/*.qml")

# Freeze legacy direct Qt Quick Controls per file. A new file has a baseline of
# zero, and reducing one old page cannot create quota for another page.
set(legacy_control_baselines
    "app/qml/AgPlayer/AudioToolsWindow.qml=1"
    "app/qml/AgPlayer/components/audioeditor/EditorCommandBar.qml=1"
    "app/qml/AgPlayer/components/audioeditor/EditorSlider.qml=1"
    "app/qml/AgPlayer/components/audioeditor/EditorWaveformCanvas.qml=3"
    "app/qml/AgPlayer/components/ColorField.qml=5"
    "app/qml/AgPlayer/components/EmptyLibrary.qml=0"
    "app/qml/AgPlayer/components/ExperienceActions.qml=2"
    "app/qml/AgPlayer/components/ImmersiveControlPanel.qml=8"
    "app/qml/AgPlayer/components/ImmersiveSurface.qml=3"
    "app/qml/AgPlayer/components/ImportStatusPanel.qml=2"
    "app/qml/AgPlayer/components/IntegratedPlayerControls.qml=7"
    "app/qml/AgPlayer/components/LibrarySidePanel.qml=3"
    "app/qml/AgPlayer/components/LyricsPanel.qml=5"
    "app/qml/AgPlayer/components/MiniPlayerControls.qml=11"
    "app/qml/AgPlayer/components/PlayerControls.qml=7"
    "app/qml/AgPlayer/components/PlayerPane.qml=1"
    "app/qml/AgPlayer/components/PlayerVolumeControl.qml=2"
    "app/qml/AgPlayer/components/RollingPlayerShell.qml=9"
    "app/qml/AgPlayer/components/SearchFilter.qml=4"
    "app/qml/AgPlayer/components/SideNavigation.qml=5"
    "app/qml/AgPlayer/components/TagManagementPanel.qml=7"
    "app/qml/AgPlayer/components/TitleBar.qml=4"
    "app/qml/AgPlayer/components/tools/AudioEditorPage.qml=2"
    "app/qml/AgPlayer/components/tools/FilenameProcessPage.qml=1"
    "app/qml/AgPlayer/components/tools/FormatConvertPage.qml=0"
    "app/qml/AgPlayer/components/tools/FormatErrorDialog.qml=2"
    "app/qml/AgPlayer/components/tools/FormatPreflightDialog.qml=1"
    "app/qml/AgPlayer/components/tools/FormatSettingsPanel.qml=4"
    "app/qml/AgPlayer/components/tools/FormatTaskTable.qml=6"
    "app/qml/AgPlayer/components/tools/MetadataEditPage.qml=8"
    "app/qml/AgPlayer/components/tools/VocalSeparationPage.qml=6"
    "app/qml/AgPlayer/components/TrackList.qml=5"
    "app/qml/AgPlayer/components/TransportControls.qml=7"
    "app/qml/AgPlayer/components/VideoTransportBar.qml=2"
    "app/qml/AgPlayer/EqualizerWindow.qml=9"
    "app/qml/AgPlayer/ListWindow.qml=7"
    "app/qml/AgPlayer/MiniPlayerWindow.qml=4"
    "app/qml/AgPlayer/SettingsPage.qml=3"
)

function(get_legacy_control_baseline relative_path output_variable)
    set(baseline 0)
    foreach(entry IN LISTS legacy_control_baselines)
        string(REGEX REPLACE "=([0-9]+)$" "" entry_path "${entry}")
        if(entry_path STREQUAL relative_path)
            string(REGEX REPLACE "^.*=" "" baseline "${entry}")
            break()
        endif()
    endforeach()
    set(${output_variable} "${baseline}" PARENT_SCOPE)
endfunction()

foreach(qml_file IN LISTS qml_files)
    if(qml_file MATCHES "/theme/Theme\\.qml$")
        continue()
    endif()
    file(RELATIVE_PATH relative_qml "${ROOT}" "${qml_file}")
    string(REPLACE "\\" "/" relative_qml "${relative_qml}")
    set(legacy_file_control_count 0)
    file(STRINGS "${qml_file}" qml_lines)
    set(line_number 0)
    foreach(qml_line IN LISTS qml_lines)
        math(EXPR line_number "${line_number} + 1")
        if(qml_line MATCHES "#[0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f]"
                AND NOT qml_line MATCHES "theme-color-allow:")
            file(RELATIVE_PATH relative_qml "${ROOT}" "${qml_file}")
            message(FATAL_ERROR
                "${relative_qml}:${line_number} contains a raw color. Use Theme or document a media-domain exception with theme-color-allow:")
        endif()
        if(qml_line MATCHES "font\\.pixelSize:[^\n]*[0-9]"
                AND NOT qml_line MATCHES "font\\.pixelSize:[ 	]*Theme\\.fontSize"
                AND NOT qml_line MATCHES "typography-size-allow:")
            message(FATAL_ERROR
                "${relative_qml}:${line_number} contains a numeric font size. Use a Theme typography token or document a visualization-only exception with typography-size-allow:")
        endif()
        if(NOT qml_file MATCHES "/components/Themed[A-Za-z]+\\.qml$"
                AND qml_line MATCHES "^[ \t]*(component[ \t]+[A-Za-z_][A-Za-z0-9_]*[ \t]*:[ \t]*)?([A-Za-z_][A-Za-z0-9_]*\\.)?(Button|ToolButton|TextField|Slider|RangeSlider|CheckBox|Switch|ComboBox|Dialog|ToolTip|ScrollBar|MenuItem|ItemDelegate)[ \t]*\\{")
            math(EXPR legacy_file_control_count
                "${legacy_file_control_count} + 1")
        endif()
    endforeach()
    get_legacy_control_baseline("${relative_qml}" legacy_file_baseline)
    if(legacy_file_control_count GREATER legacy_file_baseline)
        message(FATAL_ERROR
            "${relative_qml} has ${legacy_file_control_count} direct Qt Quick Controls; its reviewed baseline is ${legacy_file_baseline}. Use a Themed component.")
    endif()
endforeach()

require_text("app/qml/AgPlayer/components/TitleBar.qml"
    "property color surfaceColor: Theme.titleBarSurface")
require_text("app/qml/AgPlayer/components/TitleBar.qml"
    "color: surfaceColor")
require_text("app/qml/AgPlayer/AudioToolsWindow.qml"
    "ThemedIconButton {")
require_text("app/qml/AgPlayer/components/tools/ToolSidebar.qml"
    "ThemedTabButton {")
require_text("app/qml/AgPlayer/components/TitleBar.qml"
    "Layout.preferredWidth: Theme.controlHeightCompact")
require_text("app/qml/AgPlayer/components/SideNavigation.qml"
    "readonly property int navigationRowHeight: Theme.navigationRowHeight")
require_text("app/qml/AgPlayer/components/TrackList.qml"
    "readonly property int headerHeight: Theme.tableHeaderHeight")
require_text("app/qml/AgPlayer/components/TrackList.qml"
    "singleWindowLayout ? Theme.mediaListRowHeight")
require_text("app/qml/AgPlayer/ListWindow.qml"
    "readonly property int titleBarHeight: Theme.titleBarHeight")
require_text("app/qml/AgPlayer/ListWindow.qml"
    "readonly property int leftColumnWidth: Theme.navigationWidth")
require_text("app/qml/AgPlayer/components/IntegratedPlayerShell.qml"
    "property int topBarHeight: Theme.titleBarHeight")
require_text("app/qml/AgPlayer/components/IntegratedPlayerShell.qml"
    "? Theme.navigationWidthCompact")
require_text("app/qml/AgPlayer/components/IntegratedPlayerShell.qml"
    ": Theme.navigationWidth")
require_text("app/qml/AgPlayer/components/IntegratedPlayerShell.qml"
    "property int rightColumnWidth: Theme.playerInspectorWidth")
require_text("app/qml/AgPlayer/components/RollingPlayerShell.qml"
    "Layout.preferredHeight: Theme.titleBarHeight")
require_text("app/qml/AgPlayer/components/RollingPlayerShell.qml"
    "Layout.preferredWidth: Theme.navigationWidth")
require_text("app/qml/AgPlayer/SettingsPage.qml"
    "Layout.preferredHeight: Theme.settingsRowHeight")
require_text("app/qml/AgPlayer/SettingsPage.qml"
    "component SettingCombo: ThemedComboBox")
require_text("app/qml/AgPlayer/AudioToolsWindow.qml"
    "Layout.preferredHeight: Theme.titleBarHeight")
require_text("app/qml/AgPlayer/AudioToolsWindow.qml"
    "color: Theme.titleBarSurface")
require_text("app/qml/AgPlayer/AudioToolsWindow.qml"
    "color: Theme.contentSurface")
require_text("app/qml/AgPlayer/components/tools/ToolSidebar.qml"
    "color: Theme.navigationSurface")
require_text("app/qml/AgPlayer/components/tools/ToolSidebar.qml"
    "implicitHeight: Theme.settingsRowHeight")

message(STATUS "UI design-system usage contract passed")
