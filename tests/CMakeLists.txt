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
        "PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
)

add_executable(bpm_analyzer_test
    core/bpm_analyzer_test.cpp
)
set_target_properties(bpm_analyzer_test PROPERTIES AUTOMOC ON)
target_include_directories(bpm_analyzer_test PRIVATE
    "${CMAKE_SOURCE_DIR}/core/src"
    "${CMAKE_SOURCE_DIR}/tests/core"
)
target_link_libraries(bpm_analyzer_test PRIVATE agplayer_core Qt6::Test)
agplayer_enable_warnings(bpm_analyzer_test)

add_test(NAME bpm_analyzer_test COMMAND bpm_analyzer_test)
set_tests_properties(bpm_analyzer_test PROPERTIES
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
)

add_executable(c_api_lifecycle_test
    core/c_api_lifecycle_test.cpp
)
target_link_libraries(c_api_lifecycle_test PRIVATE agplayer_core)

agplayer_enable_warnings(c_api_lifecycle_test)

add_test(NAME c_api_lifecycle_test COMMAND c_api_lifecycle_test)

set(DECODER_FIXTURE_DIR "${CMAKE_CURRENT_BINARY_DIR}/fixtures")
set(SINE_WAV_FIXTURE "${DECODER_FIXTURE_DIR}/sine-440hz.wav")
set(FORMAT_FIXTURE_DIR "${DECODER_FIXTURE_DIR}/formats")

add_executable(format_matrix_test
    core/format_matrix_test.cpp
)
set_target_properties(format_matrix_test PROPERTIES AUTOMOC ON)
target_link_libraries(format_matrix_test PRIVATE agplayer_core Qt6::Test)
target_compile_definitions(format_matrix_test PRIVATE
    AGPLAYER_FIXTURES_DIR="${FORMAT_FIXTURE_DIR}"
    AGPLAYER_SOURCE_WAV="${SINE_WAV_FIXTURE}"
)
agplayer_enable_warnings(format_matrix_test)
add_test(NAME format_matrix_test COMMAND format_matrix_test)
set_tests_properties(format_matrix_test PROPERTIES
    TIMEOUT 120
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
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

add_executable(audio_document_test
    core/audio_document_test.cpp
)
set_target_properties(audio_document_test PROPERTIES AUTOMOC ON)
target_include_directories(audio_document_test PRIVATE
    "${CMAKE_SOURCE_DIR}/core/src"
)
target_link_libraries(audio_document_test PRIVATE agplayer_core Qt6::Test)
agplayer_enable_warnings(audio_document_test)
add_test(NAME audio_document_test COMMAND audio_document_test)
set_tests_properties(audio_document_test PROPERTIES
    TIMEOUT 10
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)

add_executable(audio_event_test
    core/audio_event_test.cpp
)
set_target_properties(audio_event_test PROPERTIES AUTOMOC ON)
target_include_directories(audio_event_test PRIVATE
    "${CMAKE_SOURCE_DIR}/core/src"
)
target_link_libraries(audio_event_test PRIVATE Qt6::Test)
agplayer_enable_warnings(audio_event_test)
add_test(NAME audio_event_test COMMAND audio_event_test)
set_tests_properties(audio_event_test PROPERTIES
    TIMEOUT 10
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)

add_executable(document_renderer_test
    core/document_renderer_test.cpp
)
set_target_properties(document_renderer_test PROPERTIES AUTOMOC ON)
target_include_directories(document_renderer_test PRIVATE
    "${CMAKE_SOURCE_DIR}/core/src"
)
target_link_libraries(document_renderer_test PRIVATE agplayer_core Qt6::Test)
agplayer_enable_warnings(document_renderer_test)
add_test(NAME document_renderer_test COMMAND document_renderer_test)
set_tests_properties(document_renderer_test PROPERTIES
    TIMEOUT 30
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin"
)

add_executable(audio_source_probe_test
    core/audio_source_probe_test.cpp
)
set_target_properties(audio_source_probe_test PROPERTIES AUTOMOC ON)
target_include_directories(audio_source_probe_test PRIVATE
    "${CMAKE_SOURCE_DIR}/core/src"
)
target_link_libraries(audio_source_probe_test PRIVATE agplayer_core Qt6::Test)
agplayer_enable_warnings(audio_source_probe_test)
add_dependencies(audio_source_probe_test decoder_fixture)
add_test(NAME audio_source_probe_test COMMAND audio_source_probe_test)
set_tests_properties(audio_source_probe_test PROPERTIES
    TIMEOUT 30
    ENVIRONMENT "AGPLAYER_EDITOR_FIXTURE=${SINE_WAV_FIXTURE}"
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin"
)

add_executable(event_edit_test
    core/event_edit_test.cpp
)
set_target_properties(event_edit_test PROPERTIES AUTOMOC ON)
target_include_directories(event_edit_test PRIVATE
    "${CMAKE_SOURCE_DIR}/core/src"
)
target_link_libraries(event_edit_test PRIVATE agplayer_core Qt6::Test)
agplayer_enable_warnings(event_edit_test)
add_test(NAME event_edit_test COMMAND event_edit_test)
set_tests_properties(event_edit_test PROPERTIES
    TIMEOUT 10
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)

add_executable(timeline_undo_stack_test
    core/timeline_undo_stack_test.cpp
)
set_target_properties(timeline_undo_stack_test PROPERTIES AUTOMOC ON)
target_include_directories(timeline_undo_stack_test PRIVATE
    "${CMAKE_SOURCE_DIR}/core/src"
)
target_link_libraries(timeline_undo_stack_test PRIVATE agplayer_core Qt6::Test)
agplayer_enable_warnings(timeline_undo_stack_test)
add_test(NAME timeline_undo_stack_test COMMAND timeline_undo_stack_test)
set_tests_properties(timeline_undo_stack_test PROPERTIES
    TIMEOUT 10
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)

add_executable(event_timeline_test
    core/event_timeline_test.cpp
)
set_target_properties(event_timeline_test PROPERTIES AUTOMOC ON)
target_include_directories(event_timeline_test PRIVATE
    "${CMAKE_SOURCE_DIR}/core/src"
)
target_link_libraries(event_timeline_test PRIVATE agplayer_core Qt6::Test)
agplayer_enable_warnings(event_timeline_test)
add_test(NAME event_timeline_test COMMAND event_timeline_test)
set_tests_properties(event_timeline_test PROPERTIES
    TIMEOUT 10
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)

add_executable(timeline_edit_command_test
    core/timeline_edit_command_test.cpp
)
set_target_properties(timeline_edit_command_test PROPERTIES AUTOMOC ON)
target_include_directories(timeline_edit_command_test PRIVATE
    "${CMAKE_SOURCE_DIR}/core/src"
)
target_link_libraries(timeline_edit_command_test PRIVATE agplayer_core Qt6::Test)
agplayer_enable_warnings(timeline_edit_command_test)
add_test(NAME timeline_edit_command_test COMMAND timeline_edit_command_test)
set_tests_properties(timeline_edit_command_test PROPERTIES
    TIMEOUT 10
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)

add_executable(document_writer_test
    core/document_writer_test.cpp
)
add_dependencies(document_writer_test decoder_fixture)
target_include_directories(document_writer_test PRIVATE
    "${CMAKE_SOURCE_DIR}/core/src"
)
target_link_libraries(document_writer_test PRIVATE agplayer_core)
agplayer_enable_warnings(document_writer_test)
add_test(NAME document_writer_test
    COMMAND document_writer_test "${SINE_WAV_FIXTURE}"
)
set_tests_properties(document_writer_test PROPERTIES
    TIMEOUT 30
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
)

add_executable(time_pitch_session_test
    core/time_pitch_session_test.cpp
)
add_dependencies(time_pitch_session_test decoder_fixture)
target_include_directories(time_pitch_session_test PRIVATE
    "${CMAKE_SOURCE_DIR}/core/src"
)
target_link_libraries(time_pitch_session_test PRIVATE agplayer_core)
agplayer_enable_warnings(time_pitch_session_test)
add_test(NAME time_pitch_session_test
    COMMAND time_pitch_session_test "${SINE_WAV_FIXTURE}"
)
set_tests_properties(time_pitch_session_test PROPERTIES
    TIMEOUT 30
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
)

add_executable(timeline_snapshot_compatibility_test
    core/timeline_snapshot_compatibility_test.cpp
)
add_dependencies(timeline_snapshot_compatibility_test decoder_fixture)
target_include_directories(timeline_snapshot_compatibility_test PRIVATE
    "${CMAKE_SOURCE_DIR}/core/src"
)
target_link_libraries(timeline_snapshot_compatibility_test PRIVATE agplayer_core)
agplayer_enable_warnings(timeline_snapshot_compatibility_test)
add_test(NAME timeline_snapshot_compatibility_test
    COMMAND timeline_snapshot_compatibility_test "${SINE_WAV_FIXTURE}"
)
set_tests_properties(timeline_snapshot_compatibility_test PROPERTIES
    TIMEOUT 60
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
)

add_executable(noise_reducer_test
    core/noise_reducer_test.cpp
)
add_dependencies(noise_reducer_test decoder_fixture)
target_include_directories(noise_reducer_test PRIVATE
    "${CMAKE_SOURCE_DIR}/core/src"
)
target_link_libraries(noise_reducer_test PRIVATE agplayer_core)
agplayer_enable_warnings(noise_reducer_test)
add_test(NAME noise_reducer_test
    COMMAND noise_reducer_test "${SINE_WAV_FIXTURE}"
)
set_tests_properties(noise_reducer_test PROPERTIES
    TIMEOUT 30
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin"
)

add_executable(recording_session_test
    core/recording_session_test.cpp
)
target_include_directories(recording_session_test PRIVATE
    "${CMAKE_SOURCE_DIR}/core/src"
)
target_link_libraries(recording_session_test PRIVATE agplayer_core)
agplayer_enable_warnings(recording_session_test)
add_test(NAME recording_session_test COMMAND recording_session_test)
set_tests_properties(recording_session_test PROPERTIES
    TIMEOUT 30
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
)

# Opt-in real-device acceptance executable. It is deliberately not registered
# with CTest because CI machines are not required to expose a capture device.
add_executable(recording_hardware_smoke
    core/recording_hardware_smoke.cpp
)
target_include_directories(recording_hardware_smoke PRIVATE
    "${CMAKE_SOURCE_DIR}/core/src"
)
target_link_libraries(recording_hardware_smoke PRIVATE agplayer_core)
agplayer_enable_warnings(recording_hardware_smoke)

foreach(audio_editor_core_test IN ITEMS
    time_pixel_mapper_test
    peak_pyramid_test
)
    add_executable(${audio_editor_core_test}
        "core/${audio_editor_core_test}.cpp"
    )
    if(audio_editor_core_test STREQUAL "peak_pyramid_test")
        target_sources(${audio_editor_core_test} PRIVATE
            "${CMAKE_SOURCE_DIR}/core/src/audio_editor/peak_pyramid.cpp"
        )
    endif()
    set_target_properties(${audio_editor_core_test} PROPERTIES AUTOMOC ON)
    target_include_directories(${audio_editor_core_test} PRIVATE
        "${CMAKE_SOURCE_DIR}/core/src"
    )
    target_link_libraries(${audio_editor_core_test} PRIVATE Qt6::Test)
    if(audio_editor_core_test STREQUAL "time_pixel_mapper_test")
        target_link_libraries(${audio_editor_core_test} PRIVATE agplayer_core)
    endif()
    agplayer_enable_warnings(${audio_editor_core_test})
    add_test(NAME ${audio_editor_core_test} COMMAND ${audio_editor_core_test})
    set_tests_properties(${audio_editor_core_test} PROPERTIES
        TIMEOUT 10
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    )
endforeach()

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
        "PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
)

add_executable(fixture_generator
    "${CMAKE_SOURCE_DIR}/tools/fixture_generator.cpp"
)
agplayer_enable_warnings(fixture_generator)

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
add_dependencies(format_matrix_test decoder_fixture)

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
        "PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
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
        "PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
)

add_executable(audio_engine_test
    core/audio_engine_test.cpp
)
add_dependencies(audio_engine_test decoder_fixture)
target_link_libraries(audio_engine_test PRIVATE agplayer_core)

agplayer_enable_warnings(audio_engine_test)

add_test(NAME audio_engine_test COMMAND audio_engine_test "${SINE_WAV_FIXTURE}")
set_tests_properties(audio_engine_test PROPERTIES TIMEOUT 10)

add_executable(graphic_equalizer_test
    core/graphic_equalizer_test.cpp
)
target_include_directories(graphic_equalizer_test PRIVATE
    "${CMAKE_SOURCE_DIR}/core/src"
)
target_link_libraries(graphic_equalizer_test PRIVATE agplayer_core)
agplayer_enable_warnings(graphic_equalizer_test)
add_test(NAME graphic_equalizer_test COMMAND graphic_equalizer_test)
set_tests_properties(graphic_equalizer_test PROPERTIES TIMEOUT 10)

add_executable(transcoder_test
    core/transcoder_test.cpp
)
add_dependencies(transcoder_test decoder_fixture)
target_include_directories(transcoder_test PRIVATE
    "${CMAKE_SOURCE_DIR}/core/src"
)
target_include_directories(transcoder_test SYSTEM PRIVATE
    "${FFMPEG_INCLUDE_DIRS}"
)
target_link_libraries(transcoder_test PRIVATE
    agplayer_core
    ${FFMPEG_LIBRARIES}
)

agplayer_enable_warnings(transcoder_test)

add_test(NAME transcoder_test COMMAND transcoder_test "${SINE_WAV_FIXTURE}")
set_tests_properties(transcoder_test PROPERTIES
    TIMEOUT 30
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
)

add_executable(transcode_verifier_test
    core/transcode_verifier_test.cpp
)
add_dependencies(transcode_verifier_test decoder_fixture)
target_include_directories(transcode_verifier_test PRIVATE
    "${CMAKE_SOURCE_DIR}/core/src"
)
target_link_libraries(transcode_verifier_test PRIVATE agplayer_core)
agplayer_enable_warnings(transcode_verifier_test)
add_test(NAME transcode_verifier_test
    COMMAND transcode_verifier_test "${SINE_WAV_FIXTURE}"
)
set_tests_properties(transcode_verifier_test PROPERTIES
    TIMEOUT 30
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
)

add_executable(transcode_v2_api_test
    core/transcode_v2_api_test.cpp
)
add_dependencies(transcode_v2_api_test decoder_fixture)
target_link_libraries(transcode_v2_api_test PRIVATE agplayer_core)
agplayer_enable_warnings(transcode_v2_api_test)
add_test(NAME transcode_v2_api_test
    COMMAND transcode_v2_api_test "${SINE_WAV_FIXTURE}"
)
set_tests_properties(transcode_v2_api_test PROPERTIES
    TIMEOUT 30
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
)

add_executable(transcode_capability_test
    core/transcode_capability_test.cpp
)
target_include_directories(transcode_capability_test PRIVATE
    "${CMAKE_SOURCE_DIR}/core/src"
)
target_include_directories(transcode_capability_test SYSTEM PRIVATE
    "${FFMPEG_INCLUDE_DIRS}"
)
target_link_libraries(transcode_capability_test PRIVATE
    agplayer_core
    ${FFMPEG_LIBRARIES}
)
agplayer_enable_warnings(transcode_capability_test)
add_test(NAME transcode_capability_test COMMAND transcode_capability_test)
set_tests_properties(transcode_capability_test PROPERTIES
    TIMEOUT 30
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
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
        "PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
)

add_executable(metadata_writer_test
    core/metadata_writer_test.cpp
)
add_dependencies(metadata_writer_test decoder_fixture)
target_include_directories(metadata_writer_test PRIVATE
    "${CMAKE_SOURCE_DIR}/core/src"
)
target_link_libraries(metadata_writer_test PRIVATE agplayer_core)

agplayer_enable_warnings(metadata_writer_test)

add_test(NAME metadata_writer_test COMMAND metadata_writer_test "${SINE_WAV_FIXTURE}")
set_tests_properties(metadata_writer_test PROPERTIES
    TIMEOUT 30
    # This suite performs atomic replacements and preflight probes next to the
    # shared decoder fixture. Keep it isolated from QML metadata readers.
    RUN_SERIAL TRUE
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
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
        "PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
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
    filename_processing_test
    rename_transaction_test
    library_model_test
    tag_model_test
    tag_filter_model_test
    library_store_test
    playlist_model_test
    library_manager_controller_test
    library_navigation_model_test
    resource_path_test
    library_file_operations_test
    playback_state_store_test
    import_controller_test
    library_filter_model_test
    playback_controller_test
    window_controller_test
    global_hotkey_manager_test
    waveform_coordinate_mapper_test
    waveform_item_test
    waveform_provider_test
    track_waveform_thumbnail_provider_test
    track_waveform_thumbnail_item_test
    shutdown_test
    runtime_log_test
    settings_controller_test
    theme_manager_test
    replay_gain_scanner_test
    translation_manager_test
    audio_preview_controller_test
    editor_timeline_math_test
    audio_tools_end_to_end_test
    format_conversion_plan_test
    format_conversion_task_model_test
    file_association_controller_test
    native_drop_router_test
    equalizer_controller_test
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
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
    )
endforeach()

target_include_directories(format_conversion_plan_test PRIVATE
    "${CMAKE_SOURCE_DIR}/core/src"
)

add_executable(editor_viewport_test
    qt/editor_viewport_test.cpp
    "${CMAKE_SOURCE_DIR}/qt/src/audio_editor/editor_viewport.cpp"
)
set_target_properties(editor_viewport_test PROPERTIES AUTOMOC ON)
target_include_directories(editor_viewport_test PRIVATE
    "${CMAKE_SOURCE_DIR}/qt/src"
    "${CMAKE_SOURCE_DIR}/core/src"
)
target_link_libraries(editor_viewport_test PRIVATE Qt6::Core Qt6::Test)
agplayer_enable_warnings(editor_viewport_test)
add_test(NAME editor_viewport_test COMMAND editor_viewport_test)
set_tests_properties(editor_viewport_test PROPERTIES
    TIMEOUT 10
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)

add_executable(audio_editor_waveform_item_test
    qt/audio_editor_waveform_item_test.cpp
    "${CMAKE_SOURCE_DIR}/qt/src/audio_editor/audio_editor_waveform_item.cpp"
)

add_executable(audio_editor_controller_test
    qt/audio_editor_controller_test.cpp
)

add_executable(project_document_test
    qt/project_document_test.cpp
)
set_target_properties(project_document_test PROPERTIES AUTOMOC ON)
target_include_directories(project_document_test PRIVATE
    "${CMAKE_SOURCE_DIR}/qt/src"
    "${CMAKE_SOURCE_DIR}/core/src"
)
target_link_libraries(project_document_test PRIVATE agplayer_qt Qt6::Test)
agplayer_enable_warnings(project_document_test)
add_dependencies(project_document_test decoder_fixture gapless_fixtures)
add_test(NAME project_document_test COMMAND project_document_test)
set_tests_properties(project_document_test PROPERTIES
    TIMEOUT 30
    ENVIRONMENT
        "AGPLAYER_EDITOR_FIXTURE=${SINE_WAV_FIXTURE};AGPLAYER_EDITOR_MISMATCH_FIXTURE=${GAPLESS_B_FIXTURE}"
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin"
)

add_executable(qml_audio_editor_test
    qt/qml_audio_editor_test_main.cpp
)
set_target_properties(qml_audio_editor_test PROPERTIES AUTOMOC ON)
target_include_directories(qml_audio_editor_test PRIVATE
    "${CMAKE_SOURCE_DIR}/qt/src"
    "${CMAKE_SOURCE_DIR}/core/src"
)
target_link_libraries(qml_audio_editor_test PRIVATE
    agplayer_qt
    agplayer_app_qmlplugin
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::QuickTest
)
agplayer_enable_warnings(qml_audio_editor_test)
add_test(NAME qml_audio_editor_test
    COMMAND qml_audio_editor_test
        -input "${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_audio_editor.qml"
)
set_tests_properties(qml_audio_editor_test PROPERTIES
    TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QML2_IMPORT_PATH=${CMAKE_BINARY_DIR}/app"
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)

set_target_properties(audio_editor_controller_test PROPERTIES AUTOMOC ON)
target_include_directories(audio_editor_controller_test PRIVATE
    "${CMAKE_SOURCE_DIR}/qt/src"
    "${CMAKE_SOURCE_DIR}/core/src"
)
target_link_libraries(audio_editor_controller_test PRIVATE agplayer_qt Qt6::Test)
agplayer_enable_warnings(audio_editor_controller_test)
add_dependencies(audio_editor_controller_test decoder_fixture)
add_test(NAME audio_editor_controller_test COMMAND audio_editor_controller_test)
set_tests_properties(audio_editor_controller_test PROPERTIES
    TIMEOUT 30
    ENVIRONMENT "AGPLAYER_EDITOR_FIXTURE=${SINE_WAV_FIXTURE}"
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)
set_target_properties(audio_editor_waveform_item_test PROPERTIES AUTOMOC ON)
target_include_directories(audio_editor_waveform_item_test PRIVATE
    "${CMAKE_SOURCE_DIR}/qt/src"
)
target_link_libraries(audio_editor_waveform_item_test PRIVATE
    Qt6::Quick Qt6::Test
)
agplayer_enable_warnings(audio_editor_waveform_item_test)
add_test(NAME audio_editor_waveform_item_test COMMAND audio_editor_waveform_item_test)
set_tests_properties(audio_editor_waveform_item_test PROPERTIES
    TIMEOUT 10
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)

# WaveformItem owns a QQuickWindow.  Explicitly select offscreen for this
# visual-unit test without changing the native-drop test's qwindows contract.
set_tests_properties(waveform_item_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
set_tests_properties(native_drop_router_test PROPERTIES
    # Native drag-and-drop is a qwindows integration contract.  Pin the
    # platform explicitly so an offscreen parent test environment cannot
    # silently disable the native path.
    ENVIRONMENT "QT_QPA_PLATFORM=windows"
)

add_test(
    NAME translation_catalog_test
    COMMAND "${CMAKE_COMMAND}"
        "-DCATALOGS=${CMAKE_SOURCE_DIR}/translations/agplayer_zh.ts;${CMAKE_SOURCE_DIR}/translations/agplayer_en.ts;${CMAKE_SOURCE_DIR}/translations/agplayer_th.ts;${CMAKE_SOURCE_DIR}/translations/agplayer_vi.ts"
        -P "${CMAKE_SOURCE_DIR}/cmake/CheckTranslationCatalogs.cmake"
)
set_tests_properties(translation_catalog_test PROPERTIES TIMEOUT 10)

add_test(
    NAME source_encoding_test
    COMMAND "${CMAKE_COMMAND}"
        "-DROOT=${CMAKE_SOURCE_DIR}"
        -P "${CMAKE_SOURCE_DIR}/cmake/CheckSourceEncoding.cmake"
)
set_tests_properties(source_encoding_test PROPERTIES TIMEOUT 10)

set_tests_properties(import_controller_test PROPERTIES
    # The 10K indexing contract itself permits up to 30 seconds.  The CTest
    # watchdog must not terminate that performance test at the old 10-second
    # generic default.
    TIMEOUT 35
)
set_tests_properties(audio_tools_end_to_end_test PROPERTIES
    TIMEOUT 120
    # Metadata writes replace files atomically.  Run this stateful integration
    # suite in a dedicated CTest process to avoid concurrent Qt/FFmpeg global
    # state from other UI tests.
    RUN_SERIAL TRUE
)
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
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
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
add_dependencies(qml_main_window_test decoder_fixture)
add_test(NAME qml_main_window_test COMMAND qml_main_window_test -input ${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_main_window.qml)
set_tests_properties(qml_main_window_test PROPERTIES
    TIMEOUT 35
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_CONTROLS_STYLE=Basic;AGPLAYER_TEST_AUDIO=${SINE_WAV_FIXTURE}"
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
)
add_test(NAME qml_color_picker_test
    COMMAND qml_main_window_test -input ${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_color_picker.qml)
set_tests_properties(qml_color_picker_test PROPERTIES
    TIMEOUT 20
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_CONTROLS_STYLE=Basic;AGPLAYER_TEST_AUDIO=${SINE_WAV_FIXTURE}"
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
)
add_test(NAME qml_about_version_test
    COMMAND qml_main_window_test
        -input ${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_main_window.qml
        MainWindowControls::test_settings_y_about_uses_one_name_and_version_line)
set_tests_properties(qml_about_version_test PROPERTIES
    TIMEOUT 10
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_CONTROLS_STYLE=Basic;AGPLAYER_TEST_AUDIO=${SINE_WAV_FIXTURE}"
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
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
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
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
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_CONTROLS_STYLE=Basic"
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
)

add_executable(qml_audio_tools_test
    qt/qml_audio_tools_test_main.cpp
)
set_target_properties(qml_audio_tools_test PROPERTIES AUTOMOC ON)
target_include_directories(qml_audio_tools_test PRIVATE
    "${CMAKE_SOURCE_DIR}/qt/src"
    "${CMAKE_SOURCE_DIR}/core/include"
)
target_link_libraries(qml_audio_tools_test PRIVATE
    agplayer_app_qml
    agplayer_app_qmlplugin
    Qt6::QuickTest
)
target_compile_definitions(qml_audio_tools_test PRIVATE
    QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml"
)
agplayer_enable_warnings(qml_audio_tools_test)
add_dependencies(qml_audio_tools_test decoder_fixture)

add_test(NAME qml_format_converter_test
    COMMAND qml_audio_tools_test -o ${CMAKE_BINARY_DIR}/qml-format-converter.xml,junitxml
        -input ${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_format_converter.qml)
set_tests_properties(qml_format_converter_test PROPERTIES
    TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_CONTROLS_STYLE=Basic;AGPLAYER_TEST_AUDIO=${SINE_WAV_FIXTURE}"
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:AgPlayer>;PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
)

add_test(NAME qml_format_converter_matrix_test
    COMMAND qml_audio_tools_test -o ${CMAKE_BINARY_DIR}/qml-format-converter-matrix.xml,junitxml
        -input ${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_format_converter_matrix.qml)
set_tests_properties(qml_format_converter_matrix_test PROPERTIES
    TIMEOUT 90
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_CONTROLS_STYLE=Basic;AGPLAYER_TEST_AUDIO=${SINE_WAV_FIXTURE}"
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:AgPlayer>;PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
)

# The reference-state fixture is a test-only QML source.  It is deliberately
# kept out of app/CMakeLists.txt and production startup.
add_test(NAME qml_format_converter_visual_fixture_test
    COMMAND qml_audio_tools_test -input ${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_format_converter_visual_fixture.qml)
set_tests_properties(qml_format_converter_visual_fixture_test PROPERTIES
    TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_CONTROLS_STYLE=Basic"
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin"
)

add_test(NAME qml_filename_process_test
    COMMAND qml_audio_tools_test -input ${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_filename_process.qml)
set_tests_properties(qml_filename_process_test PROPERTIES
    TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_CONTROLS_STYLE=Basic;AGPLAYER_TEST_AUDIO=${SINE_WAV_FIXTURE}"
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:AgPlayer>;PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
)

add_test(NAME qml_metadata_editor_test
    COMMAND qml_audio_tools_test -input ${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_metadata_editor.qml)
set_tests_properties(qml_metadata_editor_test PROPERTIES
    TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_CONTROLS_STYLE=Basic;AGPLAYER_TEST_AUDIO=${SINE_WAV_FIXTURE}"
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:AgPlayer>;PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
)

add_test(NAME metadata_editor_layout_contract_test
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass
        -File ${CMAKE_CURRENT_SOURCE_DIR}/scripts/metadata_editor_layout_contract_test.ps1
        -SourceRoot ${CMAKE_SOURCE_DIR})

add_test(NAME format_converter_reference_contract_test
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass
        -File ${CMAKE_CURRENT_SOURCE_DIR}/scripts/format_converter_reference_contract_test.ps1
        -SourceRoot ${CMAKE_SOURCE_DIR})
set_tests_properties(format_converter_reference_contract_test PROPERTIES
    TIMEOUT 10)


target_include_directories(import_controller_test PRIVATE
    "${CMAKE_SOURCE_DIR}/tests/core"
)
target_include_directories(waveform_provider_test PRIVATE
    "${CMAKE_SOURCE_DIR}/tests/core"
)
target_include_directories(track_waveform_thumbnail_provider_test PRIVATE
    "${CMAKE_SOURCE_DIR}/core/src"
)

add_dependencies(import_controller_test decoder_fixture)
add_dependencies(playback_controller_test decoder_fixture)
add_dependencies(waveform_provider_test decoder_fixture)
set_tests_properties(import_controller_test PROPERTIES
    ENVIRONMENT "AGPLAYER_TEST_AUDIO=${SINE_WAV_FIXTURE}"
)
set_tests_properties(playback_controller_test PROPERTIES
    ENVIRONMENT "AGPLAYER_TEST_WAV=${SINE_WAV_FIXTURE}"
)
set_tests_properties(waveform_provider_test PROPERTIES
    TIMEOUT 30
    ENVIRONMENT "AGPLAYER_TEST_WAV=${SINE_WAV_FIXTURE}"
)
set_tests_properties(window_controller_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
set_tests_properties(playback_controller_test qml_main_window_test qml_waveform_test
    PROPERTIES
    # Qt/FFmpeg global process state and the shared generated fixtures make
    # these integration tests deterministic only when they do not overlap.
    RUN_SERIAL TRUE
)
set_tests_properties(settings_controller_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
set_tests_properties(track_waveform_thumbnail_item_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
add_dependencies(replay_gain_scanner_test decoder_fixture)
set_tests_properties(replay_gain_scanner_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;AGPLAYER_TEST_AUDIO=${SINE_WAV_FIXTURE}"
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
)

add_executable(library_stress_test
    stress/library_stress_test.cpp
)
target_include_directories(library_stress_test PRIVATE
    "${CMAKE_SOURCE_DIR}/core/src"
)
target_link_libraries(library_stress_test PRIVATE agplayer_core)
agplayer_enable_warnings(library_stress_test)

add_test(NAME library_stress_test COMMAND library_stress_test)
set_tests_properties(library_stress_test PROPERTIES
    TIMEOUT 120
    ENVIRONMENT "AGPLAYER_STRESS_COUNT=64"
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
)

add_executable(track_waveform_thumbnail_stress_test
    stress/track_waveform_thumbnail_stress_test.cpp
)
set_target_properties(track_waveform_thumbnail_stress_test PROPERTIES AUTOMOC ON)
target_include_directories(track_waveform_thumbnail_stress_test PRIVATE
    "${CMAKE_SOURCE_DIR}/qt/src"
    "${CMAKE_SOURCE_DIR}/core/src"
)
target_link_libraries(track_waveform_thumbnail_stress_test PRIVATE
    agplayer_qt Qt6::Test
)
agplayer_enable_warnings(track_waveform_thumbnail_stress_test)
add_test(NAME track_waveform_thumbnail_stress_test
    COMMAND track_waveform_thumbnail_stress_test)
set_tests_properties(track_waveform_thumbnail_stress_test PROPERTIES
    TIMEOUT 60
    LABELS stress
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
)

add_test(NAME track_waveform_thumbnail_contract_test
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass
        -File ${CMAKE_CURRENT_SOURCE_DIR}/scripts/track_waveform_thumbnail_contract_test.ps1
        -SourceRoot ${CMAKE_SOURCE_DIR}
)
set_tests_properties(track_waveform_thumbnail_contract_test PROPERTIES TIMEOUT 10)

add_test(NAME tag_management_layout_contract_test
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass
        -File ${CMAKE_CURRENT_SOURCE_DIR}/scripts/tag_management_layout_contract_test.ps1
        -SourceRoot ${CMAKE_SOURCE_DIR}
)
set_tests_properties(tag_management_layout_contract_test PROPERTIES TIMEOUT 10)

add_executable(library_model_stress_test
    stress/library_model_stress_test.cpp
)
set_target_properties(library_model_stress_test PROPERTIES AUTOMOC ON)
target_include_directories(library_model_stress_test PRIVATE
    "${CMAKE_SOURCE_DIR}/qt/src"
)
target_link_libraries(library_model_stress_test PRIVATE agplayer_qt Qt6::Test)
agplayer_enable_warnings(library_model_stress_test)
add_test(NAME library_model_stress_test COMMAND library_model_stress_test)
set_tests_properties(library_model_stress_test PROPERTIES
    TIMEOUT 30
    LABELS stress
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
)

if (WIN32)
    # This contract intentionally performs a nested Ninja configure.  Do not
    # register it from ad-hoc NMake builds that do not carry the preset's
    # vcpkg toolchain context; the canonical Windows presets satisfy all three
    # prerequisites and exercise it below.
    if (CMAKE_GENERATOR MATCHES "Ninja" AND
        CMAKE_TOOLCHAIN_FILE AND VCPKG_INSTALLED_DIR)
        add_test(
            NAME audio_editor_feature_options_test
            COMMAND "${CMAKE_COMMAND}"
                "-DSOURCE_DIR=${CMAKE_SOURCE_DIR}"
                "-DTEST_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/feature-options-contract"
                "-DTOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}"
                "-DQT_PREFIX=${CMAKE_PREFIX_PATH}"
                "-DVCPKG_INSTALLED_DIR=${VCPKG_INSTALLED_DIR}"
                "-DNINJA_EXECUTABLE=${CMAKE_MAKE_PROGRAM}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/audio_editor_feature_options_test.cmake"
        )
        set_tests_properties(audio_editor_feature_options_test PROPERTIES TIMEOUT 120)
    endif()

    add_test(
        NAME audio_tools_layout_contract_test
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/scripts/audio_tools_layout_contract_test.ps1
                -SourceRoot ${CMAKE_SOURCE_DIR}
    )
    set_tests_properties(audio_tools_layout_contract_test PROPERTIES TIMEOUT 10)

    add_test(
        NAME phase6_translation_coverage_test
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/scripts/phase6_translation_coverage_test.ps1
                -SourceRoot ${CMAKE_SOURCE_DIR}
    )
    set_tests_properties(phase6_translation_coverage_test PROPERTIES TIMEOUT 10)

    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_test(
            NAME debug_runtime_deployment_test
            COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                    -File ${CMAKE_CURRENT_SOURCE_DIR}/scripts/debug_runtime_deployment_test.ps1
                    -AppPath $<TARGET_FILE:AgPlayer>
        )
        set_tests_properties(debug_runtime_deployment_test PROPERTIES TIMEOUT 10)
    endif()

    add_test(
        NAME runtime_deployment_test
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/scripts/runtime_deployment_test.ps1
                -AppPath $<TARGET_FILE:AgPlayer>
    )
    set_tests_properties(runtime_deployment_test PROPERTIES TIMEOUT 10)

    add_test(
        NAME single_instance_test
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/scripts/single_instance_test.ps1
                -AppPath $<TARGET_FILE:AgPlayer>
                -FixturePath ${SINE_WAV_FIXTURE}
    )
    set_tests_properties(single_instance_test PROPERTIES
        TIMEOUT 20
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
    )

    add_test(
        NAME installer_contract_test
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/scripts/installer_contract_test.ps1
    )
    set_tests_properties(installer_contract_test PROPERTIES
        ENVIRONMENT "AGPLAYER_INSTALLER_SCRIPT=${CMAKE_SOURCE_DIR}/installer/AgPlayer.iss"
    )
    add_test(
        NAME release_version_contract_test
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/scripts/release_version_contract_test.ps1
                -SourceRoot ${CMAKE_SOURCE_DIR}
    )
    file(RELATIVE_PATH AGPLAYER_TEST_BUILD_DIRECTORY
        "${CMAKE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}")
    add_test(
        NAME package_version_guard_test
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/scripts/package_version_guard_test.ps1
                -SourceRoot ${CMAKE_SOURCE_DIR}
                -BuildDirectory ${AGPLAYER_TEST_BUILD_DIRECTORY}
                -AppPath $<TARGET_FILE:AgPlayer>
    )
    set_tests_properties(package_version_guard_test PROPERTIES
        TIMEOUT 120
        RUN_SERIAL TRUE)
endif()
if(WIN32)
    add_test(NAME window_icon_contract_test
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass
            -File ${CMAKE_CURRENT_SOURCE_DIR}/scripts/window_icon_contract_test.ps1
            -SourceRoot ${CMAKE_SOURCE_DIR})
    add_test(NAME window_taskbar_native_test
        COMMAND window_controller_test taskbarActivationDoesNotCancelMinimize
            -o -,txt)
    set_tests_properties(window_taskbar_native_test PROPERTIES
        TIMEOUT 10
        ENVIRONMENT "QT_QPA_PLATFORM=windows"
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
    add_test(NAME windows_shell_runtime_test
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass
            -File ${CMAKE_CURRENT_SOURCE_DIR}/scripts/windows_shell_runtime_test.ps1
            -AppPath $<TARGET_FILE:AgPlayer>)
    set_tests_properties(windows_shell_runtime_test PROPERTIES
        TIMEOUT 20
        RUN_SERIAL TRUE)
endif()
