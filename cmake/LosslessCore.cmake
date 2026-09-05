include_guard(GLOBAL)

function(agplayer_enable_lossless_core)
    if(NOT TARGET agplayer_core)
        message(FATAL_ERROR "agplayer_enable_lossless_core requires agplayer_core")
    endif()

    target_sources(agplayer_core PRIVATE
        "${CMAKE_SOURCE_DIR}/core/src/lossless/lossless_analyzer.cpp"
        "${CMAKE_SOURCE_DIR}/core/src/lossless/lossless_analyzer.hpp"
        "${CMAKE_SOURCE_DIR}/core/src/lossless/lossless_mdct.cpp"
        "${CMAKE_SOURCE_DIR}/core/src/lossless/lossless_mdct.hpp"
        "${CMAKE_SOURCE_DIR}/core/src/lossless/lossless_resampled_mdct.cpp"
        "${CMAKE_SOURCE_DIR}/core/src/lossless/lossless_resampled_mdct.hpp"
        "${CMAKE_SOURCE_DIR}/core/src/lossless/lossless_celt.cpp"
        "${CMAKE_SOURCE_DIR}/core/src/lossless/lossless_celt.hpp"
        "${CMAKE_SOURCE_DIR}/core/src/lossless/lossless_types.hpp"
        "${CMAKE_SOURCE_DIR}/core/src/lossless/sacd_reader.cpp"
        "${CMAKE_SOURCE_DIR}/core/src/lossless/sacd_reader.hpp"
    )
    if(MSVC)
        target_compile_options(agplayer_core PRIVATE /utf-8)
    endif()

    if(BUILD_TESTING)
        add_executable(lossless_resampled_mdct_test "${CMAKE_SOURCE_DIR}/tests/core/lossless_resampled_mdct_test.cpp")
        target_include_directories(lossless_resampled_mdct_test PRIVATE "${CMAKE_SOURCE_DIR}/core/src")
        target_link_libraries(lossless_resampled_mdct_test PRIVATE agplayer_core)
        agplayer_enable_warnings(lossless_resampled_mdct_test)
        add_test(NAME lossless_resampled_mdct_test COMMAND lossless_resampled_mdct_test)
        set_tests_properties(lossless_resampled_mdct_test PROPERTIES TIMEOUT 90
            ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/$<IF:$<CONFIG:Debug>,debug/bin,bin>")
        add_executable(lossless_celt_test "${CMAKE_SOURCE_DIR}/tests/core/lossless_celt_test.cpp")
        target_include_directories(lossless_celt_test PRIVATE "${CMAKE_SOURCE_DIR}/core/src")
        target_link_libraries(lossless_celt_test PRIVATE agplayer_core)
        agplayer_enable_warnings(lossless_celt_test)
        add_test(NAME lossless_celt_test COMMAND lossless_celt_test)
        set_tests_properties(lossless_celt_test PROPERTIES TIMEOUT 90
            ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/$<IF:$<CONFIG:Debug>,debug/bin,bin>")
        add_executable(lossless_mdct_test "${CMAKE_SOURCE_DIR}/tests/core/lossless_mdct_test.cpp")
        target_include_directories(lossless_mdct_test PRIVATE "${CMAKE_SOURCE_DIR}/core/src")
        target_link_libraries(lossless_mdct_test PRIVATE agplayer_core)
        agplayer_enable_warnings(lossless_mdct_test)
        add_test(NAME lossless_mdct_test COMMAND lossless_mdct_test)
        set_tests_properties(lossless_mdct_test PROPERTIES TIMEOUT 90
            ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/$<IF:$<CONFIG:Debug>,debug/bin,bin>")
        add_executable(lossless_analyzer_test
            "${CMAKE_SOURCE_DIR}/tests/core/lossless_analyzer_test.cpp"
        )
        target_include_directories(lossless_analyzer_test PRIVATE
            "${CMAKE_SOURCE_DIR}/core/src"
        )
        target_link_libraries(lossless_analyzer_test PRIVATE agplayer_core)
        agplayer_enable_warnings(lossless_analyzer_test)
        set(lossless_analyzer_test_arguments)
        set(lossless_dst_fixture
            "${CMAKE_SOURCE_DIR}/build/qa/lossless-corpus/fate-dst-64fs44-2ch.dff")
        if(EXISTS "${lossless_dst_fixture}")
            list(APPEND lossless_analyzer_test_arguments "${lossless_dst_fixture}")
        endif()
        set(lossless_iso_fixture
            "${CMAKE_SOURCE_DIR}/build/qa/lossless-corpus/fate-dst-synthetic.iso")
        if(EXISTS "${lossless_dst_fixture}" AND EXISTS "${lossless_iso_fixture}")
            list(APPEND lossless_analyzer_test_arguments "${lossless_iso_fixture}")
        endif()
        add_test(NAME lossless_analyzer_test
            COMMAND lossless_analyzer_test ${lossless_analyzer_test_arguments})
        set_tests_properties(lossless_analyzer_test PROPERTIES
            # This target scans dozens of native-rate and inverse-rate fixtures.
            # Keep a bounded suite timeout; per-task latency is measured separately.
            TIMEOUT 300
            ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/$<IF:$<CONFIG:Debug>,debug/bin,bin>"
        )
    endif()
endfunction()
