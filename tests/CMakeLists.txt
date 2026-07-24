add_executable(dependency_smoke_test
    core/dependency_smoke_test.cpp
)

target_include_directories(dependency_smoke_test SYSTEM PRIVATE
    "${FFMPEG_INCLUDE_DIRS}"
    "${MINIAUDIO_INCLUDE_DIRS}"
)
target_link_libraries(dependency_smoke_test PRIVATE ${FFMPEG_LIBRARIES})

agplayer_enable_warnings(dependency_smoke_test)

add_test(NAME dependency_smoke_test COMMAND dependency_smoke_test)
set_tests_properties(dependency_smoke_test PROPERTIES
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin"
)

add_executable(c_api_lifecycle_test
    core/c_api_lifecycle_test.cpp
)
target_link_libraries(c_api_lifecycle_test PRIVATE agplayer_core)

agplayer_enable_warnings(c_api_lifecycle_test)

add_test(NAME c_api_lifecycle_test COMMAND c_api_lifecycle_test)

add_executable(format_matrix_test
    core/format_matrix_test.cpp
)
set_target_properties(format_matrix_test PROPERTIES AUTOMOC ON)
target_link_libraries(format_matrix_test PRIVATE agplayer_core Qt6::Test)
target_compile_definitions(format_matrix_test PRIVATE
    AGPLAYER_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures/generated"
)
agplayer_enable_warnings(format_matrix_test)
add_test(NAME format_matrix_test COMMAND format_matrix_test)
set_tests_properties(format_matrix_test PROPERTIES
    TIMEOUT 120
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin"
)

add_executable(pcm_ring_buffer_test
    core/pcm_ring_buffer_test.cpp
)
target_include_directories(pcm_ring_buffer_test PRIVATE
    "${CMAKE_SOURCE_DIR}/core/src"
)
target_link_libraries(pcm_ring_buffer_test PRIVATE agplayer_core)

agplayer_enable_warnings(pcm_ring_buffer_test)

add_test(NAME pcm_ring_buffer_test COMMAND pcm_ring_buffer_test)

add_executable(playback_session_test
    core/playback_session_test.cpp
)
target_include_directories(playback_session_test PRIVATE
    "${CMAKE_SOURCE_DIR}/core/src"
)
target_link_libraries(playback_session_test PRIVATE agplayer_core)

agplayer_enable_warnings(playback_session_test)

add_test(NAME playback_session_test COMMAND playback_session_test)
set_tests_properties(playback_session_test PROPERTIES
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin"
)

add_executable(fixture_generator
    "${CMAKE_SOURCE_DIR}/tools/fixture_generator.cpp"
)
agplayer_enable_warnings(fixture_generator)

set(DECODER_FIXTURE_DIR "${CMAKE_CURRENT_BINARY_DIR}/fixtures")
set(SINE_WAV_FIXTURE "${DECODER_FIXTURE_DIR}/sine-440hz.wav")
set(GAPLESS_A_FIXTURE "${DECODER_FIXTURE_DIR}/gapless-a.wav")
set(GAPLESS_B_FIXTURE "${DECODER_FIXTURE_DIR}/gapless-b.wav")
add_custom_command(
    OUTPUT "${SINE_WAV_FIXTURE}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${DECODER_FIXTURE_DIR}"
    COMMAND $<TARGET_FILE:fixture_generator> "${SINE_WAV_FIXTURE}"
    DEPENDS fixture_generator
    VERBATIM
)
add_custom_target(decoder_fixture DEPENDS "${SINE_WAV_FIXTURE}")

add_custom_command(
    OUTPUT "${GAPLESS_A_FIXTURE}" "${GAPLESS_B_FIXTURE}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${DECODER_FIXTURE_DIR}"
    COMMAND $<TARGET_FILE:fixture_generator> "${GAPLESS_A_FIXTURE}" 0 44100
    COMMAND $<TARGET_FILE:fixture_generator> "${GAPLESS_B_FIXTURE}" 48000 48000 48000 1
    DEPENDS fixture_generator
    VERBATIM
)
add_custom_target(gapless_fixtures
    DEPENDS "${GAPLESS_A_FIXTURE}" "${GAPLESS_B_FIXTURE}"
)

add_executable(decoder_test
    core/decoder_test.cpp
)
add_dependencies(decoder_test decoder_fixture)
target_include_directories(decoder_test PRIVATE
    "${CMAKE_SOURCE_DIR}/core/src"
)
target_link_libraries(decoder_test PRIVATE agplayer_core)

agplayer_enable_warnings(decoder_test)

add_test(NAME decoder_test COMMAND decoder_test "${SINE_WAV_FIXTURE}")
set_tests_properties(decoder_test PROPERTIES
    TIMEOUT 10
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin"
)

add_executable(waveform_analyzer_test
    core/waveform_analyzer_test.cpp
)
add_dependencies(waveform_analyzer_test decoder_fixture)
target_include_directories(waveform_analyzer_test PRIVATE
    "${CMAKE_SOURCE_DIR}/core/src"
)
target_link_libraries(waveform_analyzer_test PRIVATE agplayer_core)

agplayer_enable_warnings(waveform_analyzer_test)

add_test(NAME waveform_analyzer_test
    COMMAND waveform_analyzer_test "${SINE_WAV_FIXTURE}"
)
set_tests_properties(waveform_analyzer_test PROPERTIES TIMEOUT 10)

add_executable(waveform_cache_test
    core/waveform_cache_test.cpp
)
add_dependencies(waveform_cache_test decoder_fixture)
target_include_directories(waveform_cache_test PRIVATE
    "${CMAKE_SOURCE_DIR}/core/src"
)
target_link_libraries(waveform_cache_test PRIVATE agplayer_core)

agplayer_enable_warnings(waveform_cache_test)

add_test(NAME waveform_cache_test
    COMMAND waveform_cache_test "${SINE_WAV_FIXTURE}")
set_tests_properties(waveform_cache_test PROPERTIES
    TIMEOUT 10
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin"
)

add_executable(audio_engine_test
    core/audio_engine_test.cpp
)
add_dependencies(audio_engine_test decoder_fixture)
target_link_libraries(audio_engine_test PRIVATE agplayer_core)

agplayer_enable_warnings(audio_engine_test)

add_test(NAME audio_engine_test COMMAND audio_engine_test "${SINE_WAV_FIXTURE}")
set_tests_properties(audio_engine_test PROPERTIES TIMEOUT 10)

add_executable(transcoder_test
    core/transcoder_test.cpp
)
add_dependencies(transcoder_test decoder_fixture)
target_include_directories(transcoder_test PRIVATE
    "${CMAKE_SOURCE_DIR}/core/src"
)
target_link_libraries(transcoder_test PRIVATE agplayer_core)

agplayer_enable_warnings(transcoder_test)

add_test(NAME transcoder_test COMMAND transcoder_test "${SINE_WAV_FIXTURE}")
set_tests_properties(transcoder_test PROPERTIES
    TIMEOUT 30
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin"
)

add_executable(pitch_shifter_test
    core/pitch_shifter_test.cpp
)
add_dependencies(pitch_shifter_test decoder_fixture)
target_include_directories(pitch_shifter_test PRIVATE
    "${CMAKE_SOURCE_DIR}/core/src"
)
target_link_libraries(pitch_shifter_test PRIVATE agplayer_core)

agplayer_enable_warnings(pitch_shifter_test)

add_test(NAME pitch_shifter_test COMMAND pitch_shifter_test "${SINE_WAV_FIXTURE}")
set_tests_properties(pitch_shifter_test PROPERTIES
    TIMEOUT 30
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin"
)

add_executable(light_editor_test
    core/light_editor_test.cpp
)
add_dependencies(light_editor_test decoder_fixture)
target_include_directories(light_editor_test PRIVATE
    "${CMAKE_SOURCE_DIR}/core/src"
)
target_link_libraries(light_editor_test PRIVATE agplayer_core)

agplayer_enable_warnings(light_editor_test)

add_test(NAME light_editor_test COMMAND light_editor_test "${SINE_WAV_FIXTURE}")
set_tests_properties(light_editor_test PROPERTIES
    TIMEOUT 30
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin"
)

add_executable(metadata_writer_test
    core/metadata_writer_test.cpp
)
add_dependencies(metadata_writer_test decoder_fixture)
target_link_libraries(metadata_writer_test PRIVATE agplayer_core)

agplayer_enable_warnings(metadata_writer_test)

add_test(NAME metadata_writer_test COMMAND metadata_writer_test "${SINE_WAV_FIXTURE}")
set_tests_properties(metadata_writer_test PROPERTIES
    TIMEOUT 30
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin"
)

add_executable(queue_gapless_test
    core/queue_gapless_test.cpp
)
add_dependencies(queue_gapless_test decoder_fixture gapless_fixtures)
target_include_directories(queue_gapless_test PRIVATE
    "${CMAKE_SOURCE_DIR}/core/src"
)
target_link_libraries(queue_gapless_test PRIVATE agplayer_core)

agplayer_enable_warnings(queue_gapless_test)

add_test(NAME queue_gapless_test
    COMMAND queue_gapless_test
        "${GAPLESS_A_FIXTURE}"
        "${GAPLESS_B_FIXTURE}"
        "${SINE_WAV_FIXTURE}"
)
set_tests_properties(queue_gapless_test PROPERTIES
    TIMEOUT 30
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin"
)

add_executable(recovery_test
    core/recovery_test.cpp
)
add_dependencies(recovery_test decoder_fixture)
target_include_directories(recovery_test PRIVATE
    "${CMAKE_SOURCE_DIR}/core/src"
)
target_link_libraries(recovery_test PRIVATE agplayer_core)

agplayer_enable_warnings(recovery_test)

add_test(NAME recovery_test
    COMMAND recovery_test "${SINE_WAV_FIXTURE}"
)
set_tests_properties(recovery_test PROPERTIES TIMEOUT 30)

foreach(qt_test IN ITEMS
    library_model_test
    library_store_test
    import_controller_test
    playback_controller_test
    window_controller_test
    global_hotkey_manager_test
    waveform_item_test
    shutdown_test
    runtime_log_test
)
    add_executable(${qt_test} "qt/${qt_test}.cpp")
    set_target_properties(${qt_test} PROPERTIES AUTOMOC ON)
    target_include_directories(${qt_test} PRIVATE
        "${CMAKE_SOURCE_DIR}/qt/src"
    )
    target_link_libraries(${qt_test} PRIVATE agplayer_qt Qt6::Test)
    agplayer_enable_warnings(${qt_test})
    add_test(NAME ${qt_test} COMMAND ${qt_test})
    set_tests_properties(${qt_test} PROPERTIES
        TIMEOUT 10
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin"
    )
endforeach()

set_tests_properties(shutdown_test PROPERTIES TIMEOUT 30)
set_tests_properties(runtime_log_test PROPERTIES TIMEOUT 30)

add_executable(qml_waveform_test
    qt/qml_waveform_test_main.cpp
)
set_target_properties(qml_waveform_test PROPERTIES AUTOMOC ON)
target_include_directories(qml_waveform_test PRIVATE
    "${CMAKE_SOURCE_DIR}/qt/src"
)
target_link_libraries(qml_waveform_test PRIVATE
    agplayer_qt
    Qt6::QuickTest
)
target_compile_definitions(qml_waveform_test PRIVATE
    QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml"
)
agplayer_enable_warnings(qml_waveform_test)
add_test(NAME qml_waveform_test COMMAND qml_waveform_test -input ${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_waveform.qml)
set_tests_properties(qml_waveform_test PROPERTIES
    TIMEOUT 10
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin"
)

add_executable(qml_main_window_test
    qt/qml_main_window_test_main.cpp
)
set_target_properties(qml_main_window_test PROPERTIES AUTOMOC ON)
target_include_directories(qml_main_window_test PRIVATE
    "${CMAKE_SOURCE_DIR}/qt/src"
    "${CMAKE_SOURCE_DIR}/core/include"
)
target_link_libraries(qml_main_window_test PRIVATE
    agplayer_app_qml
    agplayer_app_qmlplugin
    Qt6::QuickTest
)
target_compile_definitions(qml_main_window_test PRIVATE
    QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml"
)
agplayer_enable_warnings(qml_main_window_test)
add_test(NAME qml_main_window_test COMMAND qml_main_window_test -input ${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_main_window.qml)
set_tests_properties(qml_main_window_test PROPERTIES
    TIMEOUT 10
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin"
)

add_executable(qml_mini_player_test
    qt/qml_mini_player_test_main.cpp
)
set_target_properties(qml_mini_player_test PROPERTIES AUTOMOC ON)
target_include_directories(qml_mini_player_test PRIVATE
    "${CMAKE_SOURCE_DIR}/qt/src"
    "${CMAKE_SOURCE_DIR}/core/include"
)
target_link_libraries(qml_mini_player_test PRIVATE
    agplayer_app_qml
    agplayer_app_qmlplugin
    Qt6::QuickTest
)
target_compile_definitions(qml_mini_player_test PRIVATE
    QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml"
)
agplayer_enable_warnings(qml_mini_player_test)
add_test(NAME qml_mini_player_test COMMAND qml_mini_player_test -input ${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_mini_player.qml)
set_tests_properties(qml_mini_player_test PROPERTIES
    TIMEOUT 10
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin"
)

add_executable(qml_error_states_test
    qt/qml_error_states_test_main.cpp
)
set_target_properties(qml_error_states_test PROPERTIES AUTOMOC ON)
target_include_directories(qml_error_states_test PRIVATE
    "${CMAKE_SOURCE_DIR}/qt/src"
    "${CMAKE_SOURCE_DIR}/core/include"
)
target_link_libraries(qml_error_states_test PRIVATE
    agplayer_app_qml
    agplayer_app_qmlplugin
    Qt6::QuickTest
)
target_compile_definitions(qml_error_states_test PRIVATE
    QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml"
)
agplayer_enable_warnings(qml_error_states_test)
add_test(NAME qml_error_states_test COMMAND qml_error_states_test -input ${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_error_states.qml)
set_tests_properties(qml_error_states_test PROPERTIES
    TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin"
)

add_dependencies(import_controller_test decoder_fixture)
add_dependencies(playback_controller_test decoder_fixture)
set_tests_properties(import_controller_test PROPERTIES
    ENVIRONMENT "AGPLAYER_TEST_AUDIO=${SINE_WAV_FIXTURE}"
)
set_tests_properties(playback_controller_test PROPERTIES
    ENVIRONMENT "AGPLAYER_TEST_WAV=${SINE_WAV_FIXTURE}"
)
set_tests_properties(window_controller_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
