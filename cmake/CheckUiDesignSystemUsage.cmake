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

require_text("app/qml/AgPlayer/components/TitleBar.qml"
    "color: Theme.titleBarSurface")
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
