if(NOT DEFINED ROOT)
    message(FATAL_ERROR "ROOT is required")
endif()

file(GLOB_RECURSE production_sources
    "${ROOT}/app/*.qml"
    "${ROOT}/qt/*.cpp"
    "${ROOT}/qt/*.hpp")

set(native_dialog_source
    "${ROOT}/app/qml/AgPlayer/components/AgColorPicker.qml")
set(native_dialog_count 0)
foreach(source IN LISTS production_sources)
    file(READ "${source}" contents)
    string(REGEX MATCHALL
        "(^|[^A-Za-z0-9_])(ColorDialog|QColorDialog)([^A-Za-z0-9_]|$)"
        picker_matches "${contents}")
    list(LENGTH picker_matches picker_count)
    if(source STREQUAL native_dialog_source)
        set(native_dialog_count ${picker_count})
    elseif(picker_count GREATER 0)
        file(RELATIVE_PATH relative "${ROOT}" "${source}")
        message(FATAL_ERROR "Second color picker found in ${relative}")
    endif()
endforeach()

if(NOT native_dialog_count EQUAL 1)
    message(FATAL_ERROR
        "AgColorPicker.qml must own exactly one QtQuick.Dialogs ColorDialog; found ${native_dialog_count}")
endif()

message(STATUS "Single system color dialog contract passed")
