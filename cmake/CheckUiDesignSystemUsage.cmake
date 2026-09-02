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
set(legacy_direct_control_count 0)
foreach(qml_file IN LISTS qml_files)
    if(qml_file MATCHES "/theme/Theme\\.qml$")
        continue()
    endif()
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
        if(NOT qml_file MATCHES "/components/Themed[A-Za-z]+\\.qml$"
                AND qml_line MATCHES "^[ \t]*(Button|ToolButton|TextField|Slider|RangeSlider|CheckBox|Switch|ComboBox|Dialog|ToolTip|ScrollBar)[ \t]*\\{")
            math(EXPR legacy_direct_control_count
                "${legacy_direct_control_count} + 1")
        endif()
    endforeach()
endforeach()

# Existing specialised pages migrate incrementally, but new work may not add
# direct Qt Quick Controls and silently grow the legacy surface.
if(legacy_direct_control_count GREATER 227)
    message(FATAL_ERROR
        "Direct Qt Quick Controls increased from the reviewed baseline of 227 to ${legacy_direct_control_count}. Use a Themed component or reduce the legacy count.")
endif()

require_text("app/qml/AgPlayer/components/TitleBar.qml"
    "color: Theme.titleBarSurface")
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
    "property int leftColumnWidth: Theme.navigationWidthExpanded")
require_text("app/qml/AgPlayer/components/RollingPlayerShell.qml"
    "Layout.preferredHeight: Theme.titleBarHeight")
require_text("app/qml/AgPlayer/components/RollingPlayerShell.qml"
    "Layout.preferredWidth: Theme.navigationWidthCompact")
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
