if(NOT DEFINED ROOT)
    message(FATAL_ERROR "ROOT is required")
endif()

file(GLOB_RECURSE qml_files "${ROOT}/app/qml/AgPlayer/*.qml")
set(exempt_files
    "app/qml/AgPlayer/theme/Theme.qml"
    "app/qml/AgPlayer/components/AgColorPicker.qml"
    "app/qml/AgPlayer/components/ColorField.qml"
    "app/qml/AgPlayer/components/ThemeColorSelector.qml"
)
set(violations)

foreach(qml_file IN LISTS qml_files)
    file(RELATIVE_PATH relative_path "${ROOT}" "${qml_file}")
    string(REPLACE "\\" "/" relative_path "${relative_path}")
    if(relative_path IN_LIST exempt_files)
        continue()
    endif()

    file(STRINGS "${qml_file}" source_lines ENCODING UTF-8)
    set(line_number 0)
    foreach(source_line IN LISTS source_lines)
        math(EXPR line_number "${line_number} + 1")
        if(source_line MATCHES "Qt\\.(lighter|darker)\\(")
            list(APPEND violations
                "${relative_path}:${line_number}: runtime lighter/darker")
        elseif(source_line MATCHES "Qt\\.(rgba|hsla|tint)\\("
               AND NOT source_line MATCHES "theme-color-allow:")
            list(APPEND violations
                "${relative_path}:${line_number}: unclassified runtime color")
        endif()

        if(source_line MATCHES "#[0-9A-Fa-f]+"
           AND NOT source_line MATCHES "theme-color-allow:")
            list(APPEND violations
                "${relative_path}:${line_number}: unclassified hex color")
        endif()
    endforeach()
endforeach()

set(palette_windows
    "app/qml/AgPlayer/Main.qml"
    "app/qml/AgPlayer/ListWindow.qml"
    "app/qml/AgPlayer/MiniPlayerWindow.qml"
    "app/qml/AgPlayer/SettingsWindow.qml"
    "app/qml/AgPlayer/AudioToolsWindow.qml"
    "app/qml/AgPlayer/EqualizerWindow.qml"
)
foreach(relative_path IN LISTS palette_windows)
    file(READ "${ROOT}/${relative_path}" window_source ENCODING UTF-8)
    if(NOT window_source MATCHES "palette\\.highlight:[ \t]*Theme\\.highlight")
        list(APPEND violations
            "${relative_path}: window selection palette must use Theme.highlight")
    endif()
    if(NOT window_source MATCHES
       "palette\\.highlightedText:[ \t]*Theme\\.highlightText")
        list(APPEND violations
            "${relative_path}: window selection text must use Theme.highlightText")
    endif()
endforeach()

if(violations)
    list(JOIN violations "\n  " violation_text)
    message(FATAL_ERROR
        "QML theme color classification failed:\n  ${violation_text}\n"
        "Move ordinary UI colors to Theme tokens. Fixed media, format-badge, "
        "or user-data colors must carry a same-line theme-color-allow reason.")
endif()

message(STATUS "QML theme color classification passed")
