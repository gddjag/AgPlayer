if(NOT DEFINED ROOT)
    message(FATAL_ERROR "ROOT is required")
endif()

file(GLOB_RECURSE production_sources
    "${ROOT}/app/*.qml"
    "${ROOT}/qt/*.cpp"
    "${ROOT}/qt/*.hpp")

foreach(source IN LISTS production_sources)
    file(READ "${source}" contents)
    if(contents MATCHES "(^|[^A-Za-z0-9_])(ColorDialog|QColorDialog)([^A-Za-z0-9_]|$)")
        file(RELATIVE_PATH relative "${ROOT}" "${source}")
        message(FATAL_ERROR "System or duplicate color picker found in ${relative}")
    endif()
endforeach()

message(STATUS "Single native QML color picker contract passed")
