target_sources(agplayer_qt PRIVATE
    ${CMAKE_SOURCE_DIR}/qt/src/lossless_analysis_controller.cpp
    ${CMAKE_SOURCE_DIR}/qt/src/lossless_analysis_controller.hpp
    ${CMAKE_SOURCE_DIR}/qt/src/lossless_report.cpp
    ${CMAKE_SOURCE_DIR}/qt/src/lossless_report.hpp
    ${CMAKE_SOURCE_DIR}/qt/src/lossless_task_model.cpp
    ${CMAKE_SOURCE_DIR}/qt/src/lossless_task_model.hpp
)

if(NOT BUILD_TESTING)
    return()
endif()

add_executable(lossless_analysis_controller_test
    ${CMAKE_SOURCE_DIR}/tests/qt/lossless_analysis_controller_test.cpp
)
set_target_properties(lossless_analysis_controller_test PROPERTIES AUTOMOC ON)
target_include_directories(lossless_analysis_controller_test PRIVATE
    ${CMAKE_SOURCE_DIR}/qt/src
    ${CMAKE_SOURCE_DIR}/core/src
)
target_link_libraries(lossless_analysis_controller_test PRIVATE
    agplayer_qt
    Qt6::Test
)
agplayer_enable_warnings(lossless_analysis_controller_test)
add_test(NAME lossless_analysis_controller_test
    COMMAND lossless_analysis_controller_test
)
set_tests_properties(lossless_analysis_controller_test PROPERTIES
    # Two real DSP passes allow up to 60s each in Debug, plus controller cases.
    TIMEOUT 180
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/$<IF:$<CONFIG:Debug>,debug/bin,bin>"
)
