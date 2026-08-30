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
set(picker_hsva_count 0)

foreach(qml_file IN LISTS qml_files)
    file(RELATIVE_PATH relative_path "${ROOT}" "${qml_file}")
    string(REPLACE "\\" "/" relative_path "${relative_path}")

    file(STRINGS "${qml_file}" source_lines ENCODING UTF-8)
    set(line_number 0)
    foreach(source_line IN LISTS source_lines)
        math(EXPR line_number "${line_number} + 1")
        if(relative_path STREQUAL
           "app/qml/AgPlayer/components/AgColorPicker.qml")
            string(REGEX MATCHALL "Qt\\.hsva\\("
                picker_hsva_matches "${source_line}")
            list(LENGTH picker_hsva_matches picker_hsva_line_count)
            math(EXPR picker_hsva_count
                "${picker_hsva_count} + ${picker_hsva_line_count}")
        endif()

        if(source_line MATCHES "Qt\\.(lighter|darker|hsla)\\("
           OR (source_line MATCHES "Qt\\.hsva\\("
               AND NOT relative_path STREQUAL
                       "app/qml/AgPlayer/components/AgColorPicker.qml"))
            list(APPEND violations
                "${relative_path}:${line_number}: page-local color derivation")
        endif()

        if(source_line MATCHES
           "(color|tint|border\\.color):[ \\t]*\"(white|black)\""
           AND NOT source_line MATCHES "theme-color-allow:")
            list(APPEND violations
                "${relative_path}:${line_number}: unclassified named color")
        endif()

        if(NOT relative_path IN_LIST exempt_files)
            if(source_line MATCHES "Qt\\.(rgba|tint)\\("
               AND NOT source_line MATCHES "theme-color-allow:")
                list(APPEND violations
                    "${relative_path}:${line_number}: unclassified runtime color")
            endif()

            if(source_line MATCHES "#[0-9A-Fa-f]+"
               AND NOT source_line MATCHES "theme-color-allow:")
                list(APPEND violations
                    "${relative_path}:${line_number}: unclassified hex color")
            endif()

        endif()
    endforeach()
endforeach()

if(NOT picker_hsva_count EQUAL 2)
    list(APPEND violations
        "app/qml/AgPlayer/components/AgColorPicker.qml: expected exactly two Qt.hsva interactions, found ${picker_hsva_count}")
endif()

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

file(READ "${ROOT}/app/qml/AgPlayer/SettingsPage.qml" settings_source ENCODING UTF-8)
foreach(legacy_control IN ITEMS
        "accentThemeColorSelector"
        "highlightThemeColorSelector"
        "highlightFollowAccentControl"
        "SettingsController.accent"
        "SettingsController.highlight")
    if(settings_source MATCHES "${legacy_control}")
        list(APPEND violations
            "app/qml/AgPlayer/SettingsPage.qml: legacy Accent/Highlight control remains")
    endif()
endforeach()
if(NOT settings_source MATCHES "ThemeColorSelector"
   OR NOT settings_source MATCHES "SettingsController\.skinColorMode"
   OR NOT settings_source MATCHES "SettingsController\.skinPreset"
   OR NOT settings_source MATCHES "SettingsController\.skinCustomColor"
   OR NOT settings_source MATCHES "SettingsController\.skinCustomKind"
   OR NOT settings_source MATCHES "SettingsController\.skinCustomColorMiddle"
   OR NOT settings_source MATCHES "SettingsController\.skinCustomColorEnd")
    list(APPEND violations
        "app/qml/AgPlayer/SettingsPage.qml: skin color selector must bind all skin settings")
endif()

file(READ "${ROOT}/app/CMakeLists.txt" app_cmake_source ENCODING UTF-8)
if(NOT app_cmake_source MATCHES
   "qml/AgPlayer/components/SkinBackdrop\.qml")
    list(APPEND violations
        "app/CMakeLists.txt: SkinBackdrop.qml must be registered in the QML module")
endif()

file(READ
    "${ROOT}/app/qml/AgPlayer/components/AgColorPicker.qml"
    picker_source ENCODING UTF-8)
if(picker_source MATCHES "QtQuick\.Dialogs"
   OR picker_source MATCHES "(^|[^A-Za-z])ColorDialog([^A-Za-z]|$)")
    list(APPEND violations
        "app/qml/AgPlayer/components/AgColorPicker.qml: system ColorDialog is forbidden")
endif()

file(READ "${ROOT}/app/main.cpp" main_source ENCODING UTF-8)
foreach(required_cli_token IN ITEMS
        "--qa-skin-kind"
        "--qa-skin-start"
        "--qa-skin-middle"
        "--qa-skin-end"
        "--qa-open-skin-picker"
        "selectDefaultSkin"
        "selectSkinPreset"
        "setSkinCustomConfiguration"
        "skinCustomStart"
        "skinCustomMiddle"
        "skinCustomEnd")
    if(NOT main_source MATCHES "${required_cli_token}")
        list(APPEND violations
            "app/main.cpp: missing deterministic QA contract ${required_cli_token}")
    endif()
endforeach()

file(READ
    "${ROOT}/scripts/qa-final-ui-matrix.ps1"
    qa_matrix_source ENCODING UTF-8)
foreach(required_matrix_token IN ITEMS
        "SkinKind"
        "SkinStart"
        "SkinMiddle"
        "SkinEnd"
        "OpenSkinPicker"
        "--qa-skin-kind"
        "--qa-skin-start"
        "--qa-skin-middle"
        "--qa-skin-end"
        "--qa-open-skin-picker")
    if(NOT qa_matrix_source MATCHES "${required_matrix_token}")
        list(APPEND violations
            "scripts/qa-final-ui-matrix.ps1: missing ${required_matrix_token}")
    endif()
endforeach()

set(required_selector_zh
    "Aurora|极光"
    "Sea Glass|海盐"
    "Sunset|日落"
    "Lavender Mist|薰衣草"
    "Morning Glow|晨光"
    "Solid|单色"
    "Gradient|三色渐变"
    "Start|起点颜色"
    "Middle|中点颜色"
    "End|终点颜色"
)
set(required_picker_zh
    "应用|应用"
    "取消|取消"
)
set(required_selector_en
    "Aurora|Aurora"
    "Sea Glass|Sea Glass"
    "Sunset|Sunset"
    "Lavender Mist|Lavender Mist"
    "Morning Glow|Morning Glow"
    "Solid|Solid"
    "Gradient|3-color gradient"
    "Start|Start color"
    "Middle|Middle color"
    "End|End color"
)
set(required_picker_en
    "应用|Apply"
    "取消|Cancel"
)
set(required_selector_th
    "Aurora|แสงออโรรา"
    "Sea Glass|แก้วทะเล"
    "Sunset|ยามอาทิตย์อัสดง"
    "Lavender Mist|หมอกลาเวนเดอร์"
    "Morning Glow|แสงรุ่งอรุณ"
    "Solid|สีเดียว"
    "Gradient|การไล่สี 3 สี"
    "Start|สีเริ่มต้น"
    "Middle|สีกลาง"
    "End|สีสิ้นสุด"
)
set(required_picker_th
    "应用|นำไปใช้"
    "取消|ยกเลิก"
)
set(required_selector_vi
    "Aurora|Cực quang"
    "Sea Glass|Thủy tinh biển"
    "Sunset|Hoàng hôn"
    "Lavender Mist|Sương oải hương"
    "Morning Glow|Ánh ban mai"
    "Solid|Một màu"
    "Gradient|Chuyển sắc 3 màu"
    "Start|Màu bắt đầu"
    "Middle|Màu giữa"
    "End|Màu kết thúc"
)
set(required_picker_vi
    "应用|Áp dụng"
    "取消|Hủy"
)

function(required_translation_is_valid catalog_variable context_name
         source_text translation_text result_variable)
    set(catalog_text "${${catalog_variable}}")
    set(is_valid FALSE)

    set(context_marker "<name>${context_name}</name>")
    string(FIND "${catalog_text}" "${context_marker}" context_name_offset)
    if(NOT context_name_offset EQUAL -1)
        string(SUBSTRING "${catalog_text}" 0 ${context_name_offset}
            context_prefix)
        string(FIND "${context_prefix}" "<context>" context_start REVERSE)
        string(SUBSTRING "${catalog_text}" ${context_name_offset} -1
            context_suffix)
        string(FIND "${context_suffix}" "</context>" context_end_offset)

        if(NOT context_start EQUAL -1 AND NOT context_end_offset EQUAL -1)
            math(EXPR context_length
                "${context_name_offset} + ${context_end_offset} + 10 - ${context_start}")
            string(SUBSTRING "${catalog_text}" ${context_start}
                ${context_length} context_text)

            set(source_marker "<source>${source_text}</source>")
            string(FIND "${context_text}" "${source_marker}" source_offset)
            if(NOT source_offset EQUAL -1)
                string(SUBSTRING "${context_text}" 0 ${source_offset}
                    message_prefix)
                string(FIND "${message_prefix}" "<message"
                    message_start REVERSE)
                string(SUBSTRING "${context_text}" ${source_offset} -1
                    message_suffix)
                string(FIND "${message_suffix}" "</message>"
                    message_end_offset)

                if(NOT message_start EQUAL -1
                   AND NOT message_end_offset EQUAL -1)
                    math(EXPR message_length
                        "${source_offset} + ${message_end_offset} + 10 - ${message_start}")
                    string(SUBSTRING "${context_text}" ${message_start}
                        ${message_length} message_text)

                    if(NOT message_text MATCHES
                       "type[ \\t]*=[ \\t]*[\"'](obsolete|vanished|unfinished)[\"']")
                        string(FIND "${message_text}" "<translation"
                            translation_start)
                        if(NOT translation_start EQUAL -1)
                            string(SUBSTRING "${message_text}"
                                ${translation_start} -1 translation_suffix)
                            string(FIND "${translation_suffix}" ">"
                                translation_open_end)
                            if(NOT translation_open_end EQUAL -1)
                                math(EXPR translation_content_start
                                    "${translation_open_end} + 1")
                                string(SUBSTRING "${translation_suffix}"
                                    ${translation_content_start} -1
                                    translation_content_suffix)
                                string(FIND "${translation_content_suffix}"
                                    "</translation>" translation_close)
                                if(NOT translation_close EQUAL -1)
                                    string(SUBSTRING
                                        "${translation_content_suffix}" 0
                                        ${translation_close}
                                        actual_translation)
                                    string(STRIP "${actual_translation}"
                                        actual_translation)
                                    if(actual_translation STREQUAL
                                       translation_text)
                                        set(is_valid TRUE)
                                    endif()
                                endif()
                            endif()
                        endif()
                    endif()
                endif()
            endif()
        endif()
    endif()

    set(${result_variable} ${is_valid} PARENT_SCOPE)
endfunction()

foreach(language IN ITEMS zh en th vi)
    set(catalog_path "${ROOT}/translations/agplayer_${language}.ts")
    file(READ "${catalog_path}" catalog_source ENCODING UTF-8)
    foreach(required_context IN ITEMS ThemeColorSelector AgColorPicker)
        if(required_context STREQUAL "ThemeColorSelector")
            set(required_variable "required_selector_${language}")
        else()
            set(required_variable "required_picker_${language}")
        endif()

        foreach(required_pair IN LISTS ${required_variable})
            string(REPLACE "|" ";" pair_values "${required_pair}")
            list(GET pair_values 0 source_text)
            list(GET pair_values 1 translation_text)
            required_translation_is_valid(catalog_source
                "${required_context}" "${source_text}" "${translation_text}"
                translation_is_valid)
            if(NOT translation_is_valid)
                list(APPEND violations
                    "translations/agplayer_${language}.ts: missing valid ${required_context}::${source_text} => ${translation_text}")
            endif()
        endforeach()
    endforeach()
endforeach()

file(READ "${ROOT}/app/qml/AgPlayer/theme/Theme.qml" theme_source ENCODING UTF-8)
if(NOT theme_source MATCHES
   "readonly property color currentTrackSurface:[ 	]*ThemeManager\.currentTrackSurface")
    list(APPEND violations
        "app/qml/AgPlayer/theme/Theme.qml: current track must use ThemeManager.currentTrackSurface")
endif()
if(NOT theme_source MATCHES
   "readonly property color selectedTrackSelection:[ 	]*highlightSoft")
    list(APPEND violations
        "app/qml/AgPlayer/theme/Theme.qml: selected row must remain on the ordinary highlight token")
endif()

if(violations)
    list(JOIN violations "\n  " violation_text)
    message(FATAL_ERROR
        "QML theme color classification failed:\n  ${violation_text}\n"
        "Move ordinary UI colors to Theme tokens. Fixed media, format-badge, "
        "or user-data colors must carry a same-line theme-color-allow reason.")
endif()

message(STATUS "QML theme color classification passed")
