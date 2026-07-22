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

add_executable(c_api_lifecycle_test
    core/c_api_lifecycle_test.cpp
)
target_link_libraries(c_api_lifecycle_test PRIVATE agplayer_core)

agplayer_enable_warnings(c_api_lifecycle_test)

add_test(NAME c_api_lifecycle_test COMMAND c_api_lifecycle_test)

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
set_tests_properties(decoder_test PROPERTIES TIMEOUT 10)

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
    COMMAND waveform_cache_test "${SINE_WAV_FIXTURE}"
)
set_tests_properties(waveform_cache_test PROPERTIES TIMEOUT 10)

add_executable(audio_engine_test
    core/audio_engine_test.cpp
)
add_dependencies(audio_engine_test decoder_fixture)
target_link_libraries(audio_engine_test PRIVATE agplayer_core)

agplayer_enable_warnings(audio_engine_test)

add_test(NAME audio_engine_test COMMAND audio_engine_test "${SINE_WAV_FIXTURE}")
set_tests_properties(audio_engine_test PROPERTIES TIMEOUT 10)

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
set_tests_properties(queue_gapless_test PROPERTIES TIMEOUT 10)

foreach(qt_test IN ITEMS
    library_model_test
    library_store_test
    import_controller_test
    qml_registration_test
    playback_controller_test
    window_controller_test
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
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    )
endforeach()

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
