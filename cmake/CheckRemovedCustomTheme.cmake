if(NOT DEFINED ROOT)
    message(FATAL_ERROR "ROOT is required")
endif()

set(removed_files
    "qt/src/theme_manager.cpp"
    "qt/src/theme_manager.hpp"
    "app/qml/AgPlayer/components/ThemeColorSelector.qml"
    "app/qml/AgPlayer/components/AgColorPicker.qml"
    "app/qml/AgPlayer/components/ColorScale.js"
    "app/qml/AgPlayer/components/SkinBackdrop.qml"
)
foreach(relative_path IN LISTS removed_files)
    if(EXISTS "${ROOT}/${relative_path}")
        message(FATAL_ERROR "Removed custom-theme file still exists: ${relative_path}")
    endif()
endforeach()

file(GLOB_RECURSE runtime_sources
    "${ROOT}/app/*.cpp"
    "${ROOT}/app/*.hpp"
    "${ROOT}/app/qml/*.qml"
    "${ROOT}/qt/src/*.cpp"
    "${ROOT}/qt/src/*.hpp"
)
list(APPEND runtime_sources
    "${ROOT}/app/CMakeLists.txt"
    "${ROOT}/qt/CMakeLists.txt"
    "${ROOT}/scripts/qa-final-ui-matrix.ps1"
)
set(forbidden_runtime_terms
    "ThemeManager"
    "ThemeSettingsSynchronizer"
    "skinColorMode"
    "skinPreset"
    "skinCustomKind"
    "skinCustomColor"
    "ThemeColorSelector"
    "AgColorPicker"
    "SkinBackdrop"
    "--qa-skin"
    "--qa-open-skin-picker"
)
foreach(source_path IN LISTS runtime_sources)
    file(READ "${source_path}" source_text)
    foreach(forbidden_term IN LISTS forbidden_runtime_terms)
        string(FIND "${source_text}" "${forbidden_term}" found_offset)
        if(NOT found_offset EQUAL -1)
            file(RELATIVE_PATH relative_path "${ROOT}" "${source_path}")
            message(FATAL_ERROR
                "${relative_path}: removed custom-theme term remains: ${forbidden_term}")
        endif()
    endforeach()
endforeach()

file(READ "${ROOT}/app/qml/AgPlayer/components/ColorField.qml" color_field)
if(NOT color_field MATCHES "Popup[ \t\r\n]*\\{"
   OR NOT color_field MATCHES "objectName:[ \t]*\"colorFieldPicker\""
   OR NOT color_field MATCHES "Math.min\\(296,"
   OR NOT color_field MATCHES "Math.min\\(312,")
    message(FATAL_ERROR
        "ColorField.qml must keep the compact shared color picker contract")
endif()

file(READ "${ROOT}/app/qml/AgPlayer/SettingsPage.qml" settings_page)
string(FIND "${settings_page}" "qsTr(\"跟随系统\")" system_offset)
string(FIND "${settings_page}" "qsTr(\"浅色\")" light_offset)
string(FIND "${settings_page}" "qsTr(\"深色\")" dark_offset)
if(system_offset EQUAL -1 OR light_offset EQUAL -1 OR dark_offset EQUAL -1
   OR NOT system_offset LESS light_offset OR NOT light_offset LESS dark_offset)
    message(FATAL_ERROR
        "SettingsPage.qml must show System, Light, Dark in that order")
endif()

file(READ "${ROOT}/app/qml/AgPlayer/theme/Theme.qml" theme_source)
foreach(required_contract IN ITEMS
        "readonly property int mode: Runtime.SettingsController.themeMode"
        "readonly property color accent: isLight ? \"#1F1ED9\" : \"#7657E8\""
        "readonly property color titleBarSurface: isLight ? \"#E5E8ED\" : \"#202329\""
        "readonly property color navigationSurface: isLight ? \"#ECEEF2\" : \"#24272D\""
        "readonly property color contentSurface: isLight ? \"#FAFAFB\" : \"#181A1D\"")
    string(FIND "${theme_source}" "${required_contract}" contract_offset)
    if(contract_offset EQUAL -1)
        message(FATAL_ERROR "Theme.qml missing fixed contract: ${required_contract}")
    endif()
endforeach()

message(STATUS "Custom theme removal contract passed")
