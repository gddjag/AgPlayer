# Script contract tests run through the native PowerShell on Windows and
# PowerShell Core on macOS when those script tests are selected. Native C++/QML
# tests do not require PowerShell, and it is never a shipped dependency.
if(WIN32)
    set(AGPLAYER_POWERSHELL_EXECUTABLE powershell)
    set(AGPLAYER_NATIVE_QPA windows)
elseif(APPLE)
    set(AGPLAYER_POWERSHELL_EXECUTABLE pwsh)
    set(AGPLAYER_NATIVE_QPA cocoa)
else()
    set(AGPLAYER_POWERSHELL_EXECUTABLE pwsh)
    set(AGPLAYER_NATIVE_QPA xcb)
endif()

add_executable(application_file_open_test
    qt/application_file_open_test.cpp
    ${CMAKE_SOURCE_DIR}/app/agplayer_application.hpp)
set_target_properties(application_file_open_test PROPERTIES AUTOMOC ON)
target_include_directories(application_file_open_test PRIVATE ${CMAKE_SOURCE_DIR}/app)
target_link_libraries(application_file_open_test PRIVATE Qt6::Widgets Qt6::Test)
agplayer_enable_warnings(application_file_open_test)
add_test(NAME application_file_open_test COMMAND application_file_open_test)
set_tests_properties(application_file_open_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
add_test(NAME macos_target_contract_test
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/scripts/macos_target_contract_test.cmake)

add_executable(visual_kick_response_test qt/visual_kick_response_test.cpp)
add_executable(sonic_audio_parity_test qt/sonic_audio_parity_test.cpp)
target_include_directories(sonic_audio_parity_test PRIVATE "${CMAKE_SOURCE_DIR}/qt/src")
target_link_libraries(sonic_audio_parity_test PRIVATE Qt6::Core)
target_compile_definitions(sonic_audio_parity_test PRIVATE SONIC_AUDIO_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/sonic-audio-ec8ecbae.json")
agplayer_enable_warnings(sonic_audio_parity_test)
add_test(NAME sonic_audio_parity_test COMMAND sonic_audio_parity_test)
set_tests_properties(sonic_audio_parity_test PROPERTIES ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
add_executable(visual_music_probe EXCLUDE_FROM_ALL qt/visual_music_probe.cpp)
target_include_directories(visual_music_probe PRIVATE "${CMAKE_SOURCE_DIR}/qt/src" "${CMAKE_SOURCE_DIR}/core/src")
target_link_libraries(visual_music_probe PRIVATE agplayer_core Qt6::Core)
agplayer_enable_warnings(visual_music_probe)
add_executable(visual_terrain_response_test qt/visual_terrain_response_test.cpp)
target_include_directories(visual_terrain_response_test PRIVATE "${CMAKE_SOURCE_DIR}/qt/src")
agplayer_enable_warnings(visual_terrain_response_test)
add_test(NAME visual_terrain_response_test COMMAND visual_terrain_response_test)
target_include_directories(visual_kick_response_test PRIVATE "${CMAKE_SOURCE_DIR}/qt/src")
agplayer_enable_warnings(visual_kick_response_test)
add_test(NAME visual_kick_response_test COMMAND visual_kick_response_test)

add_executable(visual_spectrum_features_test qt/visual_spectrum_features_test.cpp)
target_include_directories(visual_spectrum_features_test PRIVATE "${CMAKE_SOURCE_DIR}/qt/src")
target_link_libraries(visual_spectrum_features_test PRIVATE Qt6::Core)
agplayer_enable_warnings(visual_spectrum_features_test)
add_test(NAME visual_spectrum_features_test COMMAND visual_spectrum_features_test)
set_tests_properties(visual_spectrum_features_test PROPERTIES ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")

add_executable(visual_spectrum_analyzer_test qt/visual_spectrum_analyzer_test.cpp)
target_include_directories(visual_spectrum_analyzer_test PRIVATE "${CMAKE_SOURCE_DIR}/qt/src")
agplayer_enable_warnings(visual_spectrum_analyzer_test)
add_test(NAME visual_spectrum_analyzer_test COMMAND visual_spectrum_analyzer_test)

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
set(VIDEO_FIXTURE_DIR "${DECODER_FIXTURE_DIR}/video")
set(VIDEO_WITH_AUDIO_FIXTURE "${VIDEO_FIXTURE_DIR}/video-with-audio.avi")
set(VIDEO_ONLY_FIXTURE "${VIDEO_FIXTURE_DIR}/video-only.avi")
set(AUDIO_WITH_ATTACHED_PICTURE_FIXTURE
    "${VIDEO_FIXTURE_DIR}/audio-with-attached-picture.mka")
set(ROTATED_VIDEO_FIXTURE "${VIDEO_FIXTURE_DIR}/rotated-video.mov")
set(ROTATED_180_VIDEO_FIXTURE "${VIDEO_FIXTURE_DIR}/rotated-180-video.mov")
set(ROTATED_270_VIDEO_FIXTURE "${VIDEO_FIXTURE_DIR}/rotated-270-video.mov")
set(NON_SQUARE_SAR_VIDEO_FIXTURE "${VIDEO_FIXTURE_DIR}/non-square-sar.mov")
set(MULTI_VIDEO_FIXTURE "${VIDEO_FIXTURE_DIR}/attached-and-multi-video.mkv")

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

add_executable(visual_pcm_tap_test core/visual_pcm_tap_test.cpp)
target_include_directories(visual_pcm_tap_test PRIVATE "${CMAKE_SOURCE_DIR}/core/src")
target_link_libraries(visual_pcm_tap_test PRIVATE agplayer_core)
agplayer_enable_warnings(visual_pcm_tap_test)
add_test(NAME visual_pcm_tap_test COMMAND visual_pcm_tap_test)

add_executable(scratch_pcm_window_test
    core/scratch_pcm_window_test.cpp
)
target_include_directories(scratch_pcm_window_test PRIVATE
    "${CMAKE_SOURCE_DIR}/core/src"
)
target_link_libraries(scratch_pcm_window_test PRIVATE agplayer_core)
agplayer_enable_warnings(scratch_pcm_window_test)
add_test(NAME scratch_pcm_window_test COMMAND scratch_pcm_window_test)

add_executable(scratch_command_mailbox_test
    core/scratch_command_mailbox_test.cpp
)
target_include_directories(scratch_command_mailbox_test PRIVATE
    "${CMAKE_SOURCE_DIR}/core/src"
)
target_link_libraries(scratch_command_mailbox_test PRIVATE agplayer_core)
agplayer_enable_warnings(scratch_command_mailbox_test)
add_test(NAME scratch_command_mailbox_test COMMAND scratch_command_mailbox_test)

add_executable(scratch_renderer_test
    core/scratch_renderer_test.cpp
)
target_include_directories(scratch_renderer_test PRIVATE
    "${CMAKE_SOURCE_DIR}/core/src"
)
target_link_libraries(scratch_renderer_test PRIVATE agplayer_core)
agplayer_enable_warnings(scratch_renderer_test)
add_test(NAME scratch_renderer_test COMMAND scratch_renderer_test)

add_executable(scratch_stress_test
    core/scratch_stress_test.cpp
)
target_include_directories(scratch_stress_test PRIVATE
    "${CMAKE_SOURCE_DIR}/core/src"
)
target_link_libraries(scratch_stress_test PRIVATE agplayer_core)
agplayer_enable_warnings(scratch_stress_test)
add_test(NAME scratch_stress_test COMMAND scratch_stress_test)
set_tests_properties(scratch_stress_test PROPERTIES
    TIMEOUT 75
    LABELS stress
)

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
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
)

add_executable(editor_playback_stream_test
    core/editor_playback_stream_test.cpp
)
add_dependencies(editor_playback_stream_test decoder_fixture)
target_include_directories(editor_playback_stream_test PRIVATE
    "${CMAKE_SOURCE_DIR}/core/src"
)
target_link_libraries(editor_playback_stream_test PRIVATE agplayer_core)
agplayer_enable_warnings(editor_playback_stream_test)
add_test(NAME editor_playback_stream_test
    COMMAND editor_playback_stream_test "${SINE_WAV_FIXTURE}"
)
set_tests_properties(editor_playback_stream_test PROPERTIES
    TIMEOUT 60
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
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
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
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
        "PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
)

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

add_executable(video_fixture_generator
    "${CMAKE_SOURCE_DIR}/tools/video_fixture_generator.cpp"
)
target_include_directories(video_fixture_generator SYSTEM PRIVATE
    "${FFMPEG_INCLUDE_DIRS}"
)
target_link_libraries(video_fixture_generator PRIVATE ${FFMPEG_LIBRARIES})
agplayer_enable_warnings(video_fixture_generator)

add_custom_command(
    OUTPUT
        "${VIDEO_WITH_AUDIO_FIXTURE}"
        "${VIDEO_ONLY_FIXTURE}"
        "${AUDIO_WITH_ATTACHED_PICTURE_FIXTURE}"
        "${ROTATED_VIDEO_FIXTURE}"
        "${ROTATED_180_VIDEO_FIXTURE}"
        "${ROTATED_270_VIDEO_FIXTURE}"
        "${NON_SQUARE_SAR_VIDEO_FIXTURE}"
        "${MULTI_VIDEO_FIXTURE}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${VIDEO_FIXTURE_DIR}"
    COMMAND $<TARGET_FILE:video_fixture_generator>
        "${VIDEO_WITH_AUDIO_FIXTURE}"
        "${VIDEO_ONLY_FIXTURE}"
        "${AUDIO_WITH_ATTACHED_PICTURE_FIXTURE}"
        "${ROTATED_VIDEO_FIXTURE}"
        "${ROTATED_180_VIDEO_FIXTURE}"
        "${ROTATED_270_VIDEO_FIXTURE}"
        "${NON_SQUARE_SAR_VIDEO_FIXTURE}"
        "${MULTI_VIDEO_FIXTURE}"
    DEPENDS video_fixture_generator
    VERBATIM
)
add_custom_target(video_fixtures DEPENDS
    "${VIDEO_WITH_AUDIO_FIXTURE}"
    "${VIDEO_ONLY_FIXTURE}"
    "${AUDIO_WITH_ATTACHED_PICTURE_FIXTURE}"
    "${ROTATED_VIDEO_FIXTURE}"
    "${ROTATED_180_VIDEO_FIXTURE}"
    "${ROTATED_270_VIDEO_FIXTURE}"
    "${NON_SQUARE_SAR_VIDEO_FIXTURE}"
    "${MULTI_VIDEO_FIXTURE}"
)

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

add_executable(scratch_backfill_worker_test
    core/scratch_backfill_worker_test.cpp
)
add_dependencies(scratch_backfill_worker_test decoder_fixture)
target_include_directories(scratch_backfill_worker_test PRIVATE
    "${CMAKE_SOURCE_DIR}/core/src"
)
target_link_libraries(scratch_backfill_worker_test PRIVATE agplayer_core)
agplayer_enable_warnings(scratch_backfill_worker_test)
add_test(NAME scratch_backfill_worker_test
    COMMAND scratch_backfill_worker_test "${SINE_WAV_FIXTURE}"
)
set_tests_properties(scratch_backfill_worker_test PROPERTIES
    TIMEOUT 10
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
)

set(SCRATCH_LONG_WAV_FIXTURE
    "${DECODER_FIXTURE_DIR}/scratch-long.wav"
)
add_custom_command(
    OUTPUT "${SCRATCH_LONG_WAV_FIXTURE}"
    COMMAND $<TARGET_FILE:fixture_generator>
        "${SCRATCH_LONG_WAV_FIXTURE}" 0 1764000 44100 2
    DEPENDS fixture_generator
    VERBATIM
)
add_custom_target(scratch_long_fixture DEPENDS "${SCRATCH_LONG_WAV_FIXTURE}")

add_executable(scratch_audio_engine_test
    core/scratch_audio_engine_test.cpp
)
add_dependencies(scratch_audio_engine_test decoder_fixture scratch_long_fixture)
target_include_directories(scratch_audio_engine_test PRIVATE
    "${CMAKE_SOURCE_DIR}/core/src"
)
target_link_libraries(scratch_audio_engine_test PRIVATE agplayer_core)
agplayer_enable_warnings(scratch_audio_engine_test)
add_test(NAME scratch_audio_engine_test
    COMMAND scratch_audio_engine_test
        "${SINE_WAV_FIXTURE}"
        "${SCRATCH_LONG_WAV_FIXTURE}"
)
set_tests_properties(scratch_audio_engine_test PROPERTIES
    TIMEOUT 20
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
)

add_executable(scratch_audio_callback_test
    core/scratch_audio_callback_test.cpp
)
add_dependencies(scratch_audio_callback_test scratch_long_fixture)
target_include_directories(scratch_audio_callback_test PRIVATE
    "${CMAKE_SOURCE_DIR}/core/src"
)
target_link_libraries(scratch_audio_callback_test PRIVATE agplayer_core)
agplayer_enable_warnings(scratch_audio_callback_test)
add_test(NAME scratch_audio_callback_test
    COMMAND scratch_audio_callback_test "${SCRATCH_LONG_WAV_FIXTURE}"
)
set_tests_properties(scratch_audio_callback_test PROPERTIES
    TIMEOUT 10
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
)

add_executable(scratch_c_api_test
    core/scratch_c_api_test.cpp
)
add_dependencies(scratch_c_api_test decoder_fixture)
target_link_libraries(scratch_c_api_test PRIVATE agplayer_core)
agplayer_enable_warnings(scratch_c_api_test)
add_test(NAME scratch_c_api_test
    COMMAND scratch_c_api_test "${SINE_WAV_FIXTURE}"
)
set_tests_properties(scratch_c_api_test PROPERTIES
    TIMEOUT 10
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
)

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
add_dependencies(decoder_test decoder_fixture video_fixtures)
target_include_directories(decoder_test PRIVATE
    "${CMAKE_SOURCE_DIR}/core/src"
)
target_link_libraries(decoder_test PRIVATE agplayer_core)

agplayer_enable_warnings(decoder_test)

add_test(NAME decoder_test COMMAND decoder_test
    "${SINE_WAV_FIXTURE}"
    "${VIDEO_WITH_AUDIO_FIXTURE}"
    "${VIDEO_ONLY_FIXTURE}"
    "${AUDIO_WITH_ATTACHED_PICTURE_FIXTURE}"
    "${ROTATED_VIDEO_FIXTURE}"
)
set_tests_properties(decoder_test PROPERTIES
    TIMEOUT 10
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
)

add_executable(video_decoder_test
    core/video_decoder_test.cpp
)
add_dependencies(video_decoder_test video_fixtures)
target_link_libraries(video_decoder_test PRIVATE agplayer_core)
target_include_directories(video_decoder_test PRIVATE
    "${CMAKE_SOURCE_DIR}/core/src"
)
target_compile_definitions(video_decoder_test PRIVATE
    AGPLAYER_VIDEO_DECODER_TESTING=1
)
agplayer_enable_warnings(video_decoder_test)
add_test(NAME video_decoder_test COMMAND video_decoder_test
    "${VIDEO_WITH_AUDIO_FIXTURE}"
    "${VIDEO_ONLY_FIXTURE}"
    "${AUDIO_WITH_ATTACHED_PICTURE_FIXTURE}"
    "${ROTATED_VIDEO_FIXTURE}"
    "${ROTATED_180_VIDEO_FIXTURE}"
    "${ROTATED_270_VIDEO_FIXTURE}"
    "${NON_SQUARE_SAR_VIDEO_FIXTURE}"
    "${MULTI_VIDEO_FIXTURE}"
    "${SINE_WAV_FIXTURE}"
)
set_tests_properties(video_decoder_test PROPERTIES
    TIMEOUT 30
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
)

add_executable(c_api_header_c_test
    core/c_api_header_c_test.c
)
target_include_directories(c_api_header_c_test PRIVATE
    "${CMAKE_SOURCE_DIR}/core/include"
)
add_test(NAME c_api_header_c_test COMMAND c_api_header_c_test)

add_executable(waveform_analyzer_test
    core/waveform_analyzer_test.cpp
)

add_executable(playback_time_pitch_test
    core/playback_time_pitch_test.cpp
)
add_dependencies(playback_time_pitch_test decoder_fixture)
target_include_directories(playback_time_pitch_test PRIVATE
    "${CMAKE_SOURCE_DIR}/core/src"
)
target_link_libraries(playback_time_pitch_test PRIVATE agplayer_core)
agplayer_enable_warnings(playback_time_pitch_test)
add_test(NAME playback_time_pitch_test
         COMMAND playback_time_pitch_test "${SINE_WAV_FIXTURE}")
set_tests_properties(playback_time_pitch_test PROPERTIES TIMEOUT 20)
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

add_executable(three_band_waveform_test
    core/three_band_waveform_test.cpp
)
target_include_directories(three_band_waveform_test PRIVATE
    "${CMAKE_SOURCE_DIR}/core/src"
)
target_link_libraries(three_band_waveform_test PRIVATE agplayer_core)
agplayer_enable_warnings(three_band_waveform_test)
add_test(NAME three_band_waveform_test COMMAND three_band_waveform_test)
set_tests_properties(three_band_waveform_test PROPERTIES TIMEOUT 10)

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
add_dependencies(audio_engine_test decoder_fixture video_fixtures)
target_link_libraries(audio_engine_test PRIVATE agplayer_core)

agplayer_enable_warnings(audio_engine_test)

add_test(NAME audio_engine_test COMMAND audio_engine_test
    "${SINE_WAV_FIXTURE}"
    "${VIDEO_ONLY_FIXTURE}"
)
set_tests_properties(audio_engine_test PROPERTIES TIMEOUT 10)

add_executable(output_level_meter_test
    core/output_level_meter_test.cpp
)
target_link_libraries(output_level_meter_test PRIVATE agplayer_core)
agplayer_enable_warnings(output_level_meter_test)
add_test(NAME output_level_meter_test COMMAND output_level_meter_test)
set_tests_properties(output_level_meter_test PROPERTIES TIMEOUT 10)

add_executable(output_level_c_api_test
    core/output_level_c_api_test.cpp
)
add_dependencies(output_level_c_api_test decoder_fixture)
target_link_libraries(output_level_c_api_test PRIVATE agplayer_core)
agplayer_enable_warnings(output_level_c_api_test)
add_test(NAME output_level_c_api_test
    COMMAND output_level_c_api_test "${SINE_WAV_FIXTURE}"
)
set_tests_properties(output_level_c_api_test PROPERTIES
    TIMEOUT 10
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
)

add_executable(stem_preview_mixer_test
    core/stem_preview_mixer_test.cpp
)
target_include_directories(stem_preview_mixer_test PRIVATE
    "${CMAKE_SOURCE_DIR}/core/src"
)
target_link_libraries(stem_preview_mixer_test PRIVATE agplayer_core)
agplayer_enable_warnings(stem_preview_mixer_test)
add_test(NAME stem_preview_mixer_test COMMAND stem_preview_mixer_test)
set_tests_properties(stem_preview_mixer_test PROPERTIES
    # Windows antivirus/file-indexing can transiently hold the generated WAV
    # fixtures after the assertions finish.  The test normally completes in
    # well under one second; this avoids treating delayed temp cleanup as an
    # audio-mixer failure in the complete serial suite.
    TIMEOUT 30
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
)

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

add_executable(rolling_keyboard_handler_test
    qt/rolling_keyboard_handler_test.cpp
    ../qt/src/rolling_keyboard_handler.cpp
    ../qt/src/rolling_keyboard_handler.hpp
)
set_target_properties(rolling_keyboard_handler_test PROPERTIES AUTOMOC ON)
target_include_directories(rolling_keyboard_handler_test PRIVATE "${CMAKE_SOURCE_DIR}/qt/src")
target_link_libraries(rolling_keyboard_handler_test PRIVATE Qt6::Quick Qt6::Test)
add_test(NAME rolling_keyboard_handler_test COMMAND rolling_keyboard_handler_test)
set_tests_properties(rolling_keyboard_handler_test PROPERTIES TIMEOUT 15
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_CONTROLS_STYLE=Basic"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")

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

add_executable(terrain_column_material_test qt/terrain_column_material_test.cpp
    "${CMAKE_SOURCE_DIR}/qt/src/terrain_reactor_state.cpp")
add_executable(immersive_theme_catalog_test qt/immersive_theme_catalog_test.cpp)
set_target_properties(immersive_theme_catalog_test PROPERTIES AUTOMOC ON)
target_include_directories(immersive_theme_catalog_test PRIVATE "${CMAKE_SOURCE_DIR}/qt/src")
target_link_libraries(immersive_theme_catalog_test PRIVATE Qt6::Test)
agplayer_enable_warnings(immersive_theme_catalog_test)
add_test(NAME immersive_theme_catalog_test COMMAND immersive_theme_catalog_test)
set_tests_properties(immersive_theme_catalog_test PROPERTIES TIMEOUT 30
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
add_executable(terrain_column_mesh_test qt/terrain_column_mesh_test.cpp)
set_target_properties(terrain_column_mesh_test PROPERTIES AUTOMOC ON)
target_include_directories(terrain_column_mesh_test PRIVATE "${CMAKE_SOURCE_DIR}/qt/src")
target_link_libraries(terrain_column_mesh_test PRIVATE Qt6::Gui Qt6::Test)
set_property(SOURCE qt/terrain_column_mesh_test.cpp APPEND PROPERTY OBJECT_DEPENDS
    "${CMAKE_SOURCE_DIR}/qt/src/terrain_column_mesh.hpp;${CMAKE_SOURCE_DIR}/qt/src/terrain_reactor_gpu_data.hpp")
add_test(NAME terrain_column_mesh_test COMMAND terrain_column_mesh_test)
set_tests_properties(terrain_column_mesh_test PROPERTIES TIMEOUT 30
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
set_target_properties(terrain_column_material_test PROPERTIES AUTOMOC ON)
set_property(SOURCE qt/terrain_column_material_test.cpp APPEND PROPERTY OBJECT_DEPENDS
    "${CMAKE_SOURCE_DIR}/qt/src/terrain_reactor_gpu_data.hpp;${CMAKE_SOURCE_DIR}/qt/src/terrain_column_mesh.hpp;${CMAKE_SOURCE_DIR}/qt/src/terrain_shadow_map.hpp")
target_include_directories(terrain_column_material_test PRIVATE "${CMAKE_SOURCE_DIR}/qt/src")
target_link_libraries(terrain_column_material_test PRIVATE Qt6::Quick Qt6::GuiPrivate Qt6::Test)
qt_add_shaders(terrain_column_material_test terrain_column_study_shaders
    PREFIX "/terrain"
    BASE "${CMAKE_SOURCE_DIR}/qt"
    FILES "${CMAKE_SOURCE_DIR}/qt/shaders/terrain_reactor.vert"
          "${CMAKE_SOURCE_DIR}/qt/shaders/terrain_reactor.frag"
          "${CMAKE_SOURCE_DIR}/qt/shaders/terrain_shadow.frag")
qt_add_shaders(terrain_column_material_test terrain_column_study_shadow_vertex
    PREFIX "/terrain-shadow"
    DEFINES TERRAIN_SHADOW_PASS=1
    FILES "${CMAKE_SOURCE_DIR}/qt/shaders/terrain_reactor.vert"
    OUTPUTS shaders/terrain_shadow.vert.qsb)
agplayer_enable_warnings(terrain_column_material_test)
add_test(NAME terrain_column_material_test COMMAND terrain_column_material_test)
# Expanded native material suite includes 140 real render-frame comparisons
# and takes about seven minutes on the Windows integration GPU. This is only
# the outer watchdog; every per-frame assertion and QTRY deadline is unchanged.
set_tests_properties(terrain_column_material_test PROPERTIES TIMEOUT 600 RUN_SERIAL TRUE
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")

foreach(qt_test IN ITEMS
    filename_processing_test
    rename_transaction_test
    library_model_test
    lyrics_service_test
    tag_model_test
    tag_filter_model_test
    library_store_test
    playlist_model_test
    resource_folder_controller_test
    library_navigation_model_test
    resource_path_test
    library_file_operations_test
    playback_state_store_test
    import_controller_test
    library_filter_model_test
    playback_controller_test
    video_playback_controller_test
    video_frame_item_test
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
    update_checker_test
    replay_gain_scanner_test
    translation_manager_test
    audio_preview_controller_test
    audio_visual_feature_controller_test
    player_experience_controller_test
    terrain_reactor_state_test
    terrain_reactor_item_test
    terrain_reactor_gpu_smoke_test
    editor_timeline_math_test
    audio_tools_end_to_end_test
    format_conversion_plan_test
    format_conversion_task_model_test
    audio_file_discovery_test
    file_association_controller_test
    native_drop_router_test
    equalizer_controller_test
    vocal_separation_install_test
    vocal_separation_history_test
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
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/$<IF:$<CONFIG:Debug>,debug/bin,bin>"
    )
endforeach()

target_compile_definitions(terrain_reactor_gpu_smoke_test PRIVATE
    AGPLAYER_TERRAIN_SHADER_SOURCE="${CMAKE_SOURCE_DIR}/qt/shaders/terrain_reactor.vert"
)

# Includes the real ten-second network deadline, not a shortened test-only timeout.
set_tests_properties(update_checker_test PROPERTIES TIMEOUT 20)

# This GPU suite runs targeted cases and data rows, each with its own
# discriminating QTRY deadline. Give only the outer CTest watchdog enough time
# to let those condition-based failures report their real assertion.
# Keep per-case QTRY/frame assertions unchanged; this is only the outer
# watchdog for the expanded renderer suite, not a performance budget.
set_tests_properties(terrain_reactor_gpu_smoke_test PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)

# This suite includes real notice-expiry waits and can exceed the generic
# ten-second Qt test budget on a busy Windows runner.
set_tests_properties(lyrics_service_test PROPERTIES TIMEOUT 30)

# The provider suite exercises spectral thumbnails across all render modes and
# takes roughly 23 seconds on a clean Windows Release build.
set_tests_properties(track_waveform_thumbnail_provider_test PROPERTIES TIMEOUT 30)

add_executable(unison_lyrics_provider_test
    qt/unison_lyrics_provider_test.cpp
)
set_target_properties(unison_lyrics_provider_test PROPERTIES AUTOMOC ON)
target_include_directories(unison_lyrics_provider_test PRIVATE
    "${CMAKE_SOURCE_DIR}/qt/src"
)
target_link_libraries(unison_lyrics_provider_test PRIVATE
    agplayer_qt Qt6::Test Qt6::Network
)
agplayer_enable_warnings(unison_lyrics_provider_test)
add_test(NAME unison_lyrics_provider_test COMMAND unison_lyrics_provider_test)
set_tests_properties(unison_lyrics_provider_test PROPERTIES
    TIMEOUT 10
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
)

add_executable(lyrics_ovh_provider_test
    qt/lyrics_ovh_provider_test.cpp
)
set_target_properties(lyrics_ovh_provider_test PROPERTIES AUTOMOC ON)
target_include_directories(lyrics_ovh_provider_test PRIVATE
    "${CMAKE_SOURCE_DIR}/qt/src"
)
target_link_libraries(lyrics_ovh_provider_test PRIVATE
    agplayer_qt Qt6::Test Qt6::Network
)
agplayer_enable_warnings(lyrics_ovh_provider_test)
add_test(NAME lyrics_ovh_provider_test COMMAND lyrics_ovh_provider_test)
set_tests_properties(lyrics_ovh_provider_test PROPERTIES
    TIMEOUT 10
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
)

add_executable(lrcapi_lyrics_provider_test
    qt/lrcapi_lyrics_provider_test.cpp
)
set_target_properties(lrcapi_lyrics_provider_test PROPERTIES AUTOMOC ON)
target_include_directories(lrcapi_lyrics_provider_test PRIVATE
    "${CMAKE_SOURCE_DIR}/qt/src"
)
target_link_libraries(lrcapi_lyrics_provider_test PRIVATE
    agplayer_qt Qt6::Test Qt6::Network
)
agplayer_enable_warnings(lrcapi_lyrics_provider_test)
add_test(NAME lrcapi_lyrics_provider_test COMMAND lrcapi_lyrics_provider_test)
set_tests_properties(lrcapi_lyrics_provider_test PROPERTIES
    TIMEOUT 10
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
)

add_executable(lyrics_provider_chain_test
    qt/lyrics_provider_chain_test.cpp
)
set_target_properties(lyrics_provider_chain_test PROPERTIES AUTOMOC ON)
target_include_directories(lyrics_provider_chain_test PRIVATE
    "${CMAKE_SOURCE_DIR}/qt/src"
)
target_link_libraries(lyrics_provider_chain_test PRIVATE
    agplayer_qt Qt6::Test
)
agplayer_enable_warnings(lyrics_provider_chain_test)
add_test(NAME lyrics_provider_chain_test COMMAND lyrics_provider_chain_test)
set_tests_properties(lyrics_provider_chain_test PROPERTIES
    TIMEOUT 10
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
)

target_include_directories(format_conversion_plan_test PRIVATE
    "${CMAKE_SOURCE_DIR}/core/src"
)
add_dependencies(audio_visual_feature_controller_test decoder_fixture)
set_tests_properties(audio_visual_feature_controller_test PROPERTIES
    ENVIRONMENT "AGPLAYER_TEST_AUDIO=${SINE_WAV_FIXTURE}"
)
target_link_libraries(vocal_separation_install_test PRIVATE Qt6::Network)
set_tests_properties(vocal_separation_install_test PROPERTIES TIMEOUT 30)
set_tests_properties(vocal_separation_history_test PROPERTIES TIMEOUT 30)

foreach(separation_test IN ITEMS
    separation_protocol_test
    separation_dsp_test
    separation_runtime_test
    separation_output_transaction_test
    separation_worker_engine_test
    separation_native_backend_test
    separation_real_model_test
)
    add_executable(${separation_test} "qt/${separation_test}.cpp")
    set_target_properties(${separation_test} PROPERTIES AUTOMOC ON)
    target_include_directories(${separation_test} PRIVATE
        "${CMAKE_SOURCE_DIR}/worker/src"
    )
    target_link_libraries(${separation_test} PRIVATE
        ag_separation_worker_core Qt6::Core Qt6::Test)
    agplayer_enable_warnings(${separation_test})
    add_test(NAME ${separation_test} COMMAND ${separation_test})
    set_tests_properties(${separation_test} PROPERTIES
        TIMEOUT 30
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
    )
endforeach()

set_tests_properties(separation_real_model_test PROPERTIES TIMEOUT 3600)
target_include_directories(separation_real_model_test PRIVATE
    "${CMAKE_SOURCE_DIR}/core/src"
    "${CMAKE_SOURCE_DIR}/core/include"
)

add_library(separation_fake_ort SHARED
    qt/separation_fake_ort.cpp
)
target_include_directories(separation_fake_ort PRIVATE
    "${CMAKE_SOURCE_DIR}/worker/third_party/onnxruntime/include"
)
set_target_properties(separation_fake_ort PROPERTIES
    PREFIX ""
    OUTPUT_NAME "onnxruntime_test"
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)
agplayer_enable_warnings(separation_fake_ort)
target_compile_definitions(separation_runtime_test PRIVATE
    AG_SEPARATION_FAKE_ORT_PATH="$<TARGET_FILE:separation_fake_ort>"
)
target_include_directories(separation_runtime_test PRIVATE
    "${CMAKE_SOURCE_DIR}/worker/third_party/onnxruntime/include"
    "${CMAKE_SOURCE_DIR}/qt/src"
)
target_link_libraries(separation_runtime_test PRIVATE agplayer_qt)
add_dependencies(separation_runtime_test separation_fake_ort)
target_include_directories(separation_native_backend_test PRIVATE
    "${CMAKE_SOURCE_DIR}/core/src"
    "${CMAKE_SOURCE_DIR}/core/include"
)
target_include_directories(separation_native_backend_test SYSTEM PRIVATE
    "${FFMPEG_INCLUDE_DIRS}"
)

if(TARGET AgSeparationWorker)
add_executable(separation_controller_test_worker
    qt/separation_controller_test_worker.cpp
)
set_target_properties(separation_controller_test_worker PROPERTIES AUTOMOC ON)
target_include_directories(separation_controller_test_worker PRIVATE
    "${CMAKE_SOURCE_DIR}/worker/src"
)
target_link_libraries(separation_controller_test_worker PRIVATE
    ag_separation_worker_core Qt6::Core
)
agplayer_enable_warnings(separation_controller_test_worker)

add_executable(separation_process_client_test
    qt/separation_process_client_test.cpp
)
set_target_properties(separation_process_client_test PROPERTIES AUTOMOC ON)
target_include_directories(separation_process_client_test PRIVATE
    "${CMAKE_SOURCE_DIR}/qt/src"
)
target_link_libraries(separation_process_client_test PRIVATE
    agplayer_qt Qt6::Test
)
target_compile_definitions(separation_process_client_test PRIVATE
    AG_SEPARATION_CONTROLLER_TEST_WORKER_PATH="$<TARGET_FILE:separation_controller_test_worker>"
)
add_dependencies(separation_process_client_test separation_controller_test_worker)
agplayer_enable_warnings(separation_process_client_test)
add_test(NAME separation_process_client_test COMMAND separation_process_client_test)
set_tests_properties(separation_process_client_test PROPERTIES
    # Aggregate budget for roughly twenty real Windows process launches.
    # Per-action cancellation/event-loop deadlines remain asserted in the test.
    TIMEOUT 60
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)

add_executable(vocal_separation_controller_test
    qt/vocal_separation_controller_test.cpp
)
set_target_properties(vocal_separation_controller_test PROPERTIES AUTOMOC ON)
target_include_directories(vocal_separation_controller_test PRIVATE
    "${CMAKE_SOURCE_DIR}/qt/src"
)
target_link_libraries(vocal_separation_controller_test PRIVATE
    agplayer_qt Qt6::Test
)
target_compile_definitions(vocal_separation_controller_test PRIVATE
    AG_SEPARATION_CONTROLLER_TEST_WORKER_PATH="$<TARGET_FILE:separation_controller_test_worker>"
)
add_dependencies(vocal_separation_controller_test
    separation_controller_test_worker decoder_fixture)
agplayer_enable_warnings(vocal_separation_controller_test)
add_test(NAME vocal_separation_controller_test COMMAND vocal_separation_controller_test)
set_tests_properties(vocal_separation_controller_test PROPERTIES
    TIMEOUT 120
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;AGPLAYER_TEST_AUDIO=${SINE_WAV_FIXTURE}"
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
)

add_executable(separation_blocking_worker
    qt/separation_blocking_worker.cpp
)
set_target_properties(separation_blocking_worker PROPERTIES AUTOMOC ON)
target_include_directories(separation_blocking_worker PRIVATE
    "${CMAKE_SOURCE_DIR}/worker/src"
)
target_link_libraries(separation_blocking_worker PRIVATE
    ag_separation_worker_core Qt6::Core
)
agplayer_enable_warnings(separation_blocking_worker)

add_executable(separation_worker_process_test
    qt/separation_worker_process_test.cpp
)
set_target_properties(separation_worker_process_test PROPERTIES AUTOMOC ON)
target_include_directories(separation_worker_process_test PRIVATE
    "${CMAKE_SOURCE_DIR}/worker/src"
)
target_link_libraries(separation_worker_process_test PRIVATE
    ag_separation_worker_core Qt6::Core Qt6::Test
)
target_compile_definitions(separation_worker_process_test PRIVATE
    AG_SEPARATION_WORKER_PATH="$<TARGET_FILE:AgSeparationWorker>"
    AG_SEPARATION_BLOCKING_WORKER_PATH="$<TARGET_FILE:separation_blocking_worker>"
)
add_dependencies(separation_worker_process_test AgSeparationWorker separation_blocking_worker)
agplayer_enable_warnings(separation_worker_process_test)
add_test(NAME separation_worker_process_test COMMAND separation_worker_process_test)
set_tests_properties(separation_worker_process_test PROPERTIES
    TIMEOUT 60
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
)
endif()

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

add_executable(selection_drag_controller_test
    qt/selection_drag_controller_test.cpp
)
set_target_properties(selection_drag_controller_test PROPERTIES AUTOMOC ON)
target_include_directories(selection_drag_controller_test PRIVATE
    "${CMAKE_SOURCE_DIR}/qt/src"
    "${CMAKE_SOURCE_DIR}/core/src"
)
target_link_libraries(selection_drag_controller_test PRIVATE
    agplayer_qt Qt6::Test Qt6::Widgets
)
agplayer_enable_warnings(selection_drag_controller_test)
add_dependencies(selection_drag_controller_test decoder_fixture)
add_test(NAME selection_drag_controller_test COMMAND selection_drag_controller_test)
set_tests_properties(selection_drag_controller_test PROPERTIES
    TIMEOUT 30
    ENVIRONMENT "AGPLAYER_EDITOR_FIXTURE=${SINE_WAV_FIXTURE};QT_QPA_PLATFORM=offscreen"
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
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
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
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
add_dependencies(qml_audio_editor_test decoder_fixture)
# Keep JUnit output at the CTest run level. A fixed per-test QTest path is
# shared by concurrent Windows runs and can remain exclusively locked. CI
# should use: ctest --output-junit <run-unique-path>
add_test(NAME qml_audio_editor_test
    COMMAND qml_audio_editor_test
        -input "${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_audio_editor.qml"
        -o -,txt
)
set_tests_properties(qml_audio_editor_test PROPERTIES
    TIMEOUT 120
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_CONTROLS_STYLE=Basic;QML2_IMPORT_PATH=${CMAKE_BINARY_DIR}/app;AGPLAYER_TEST_AUDIO=${SINE_WAV_FIXTURE}"
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
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
    TIMEOUT 120
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
# visual-unit test and the software scene graph so it remains independent of
# the host GPU state without changing the native-drop test's qwindows contract.
set_tests_properties(waveform_item_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
)
set_tests_properties(native_drop_router_test PROPERTIES
    # Native drag-and-drop is a qwindows integration contract.  Pin the
    # platform explicitly so an offscreen parent test environment cannot
    # silently disable the native path.
    ENVIRONMENT "QT_QPA_PLATFORM=${AGPLAYER_NATIVE_QPA}"
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/bin"
)

add_test(
    NAME translation_catalog_test
    COMMAND "${CMAKE_COMMAND}"
        "-DCATALOGS=${CMAKE_SOURCE_DIR}/translations/agplayer_zh.ts;${CMAKE_SOURCE_DIR}/translations/agplayer_en.ts"
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

add_test(
    NAME removed_custom_theme_contract_test
    COMMAND "${CMAKE_COMMAND}"
        "-DROOT=${CMAKE_SOURCE_DIR}"
        -P "${CMAKE_SOURCE_DIR}/cmake/CheckRemovedCustomTheme.cmake"
)
set_tests_properties(removed_custom_theme_contract_test PROPERTIES TIMEOUT 10)

add_test(
    NAME ui_design_system_usage_contract_test
    COMMAND "${CMAKE_COMMAND}"
        "-DROOT=${CMAKE_SOURCE_DIR}"
        -P "${CMAKE_SOURCE_DIR}/cmake/CheckUiDesignSystemUsage.cmake"
)
set_tests_properties(ui_design_system_usage_contract_test PROPERTIES TIMEOUT 10)

set_tests_properties(import_controller_test PROPERTIES
    # The 10K indexing contract itself permits up to 30 seconds.  The CTest
    # watchdog includes process startup and Windows real-time scanning, so keep
    # a separate margin without weakening the test's internal 30-second limit.
    TIMEOUT 180
    RUN_SERIAL TRUE
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
add_test(NAME qml_audio_visual_impact_test
    COMMAND qml_waveform_test
        -input ${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_audio_visual_impact.qml)
set_tests_properties(qml_audio_visual_impact_test PROPERTIES
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
    TIMEOUT 120
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_CONTROLS_STYLE=Basic;AGPLAYER_TEST_AUDIO=${SINE_WAV_FIXTURE}"
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
)
add_test(NAME qml_tag_capsule_test
    COMMAND qml_main_window_test
        -input ${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_tag_capsule.qml)
add_test(NAME qml_macos_window_controls_test
    COMMAND qml_main_window_test
        -input ${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_macos_window_controls.qml)
add_test(NAME qml_mac14_regressions_test
    COMMAND qml_main_window_test -input ${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_mac14_regressions.qml)
add_test(NAME qml_lossless_file_import_test
    COMMAND qml_main_window_test -input ${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_lossless_file_import.qml)
set_tests_properties(qml_mac14_regressions_test qml_lossless_file_import_test PROPERTIES
    TIMEOUT 60
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_CONTROLS_STYLE=Basic;AGPLAYER_TEST_AUDIO=${SINE_WAV_FIXTURE}"
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
)
set_tests_properties(qml_macos_window_controls_test PROPERTIES
    TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_CONTROLS_STYLE=Basic"
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
)
set_tests_properties(qml_tag_capsule_test PROPERTIES
    TIMEOUT 20
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_CONTROLS_STYLE=Basic;AGPLAYER_TEST_AUDIO=${SINE_WAV_FIXTURE}"
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
)
add_test(NAME qml_video_playback_view_test
    COMMAND qml_main_window_test
        -input ${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_video_playback_view.qml)
set_tests_properties(qml_video_playback_view_test PROPERTIES
    TIMEOUT 20
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_CONTROLS_STYLE=Basic;AGPLAYER_TEST_AUDIO=${SINE_WAV_FIXTURE}"
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
)
add_test(NAME qml_equalizer_visual_test
    COMMAND qml_main_window_test -input ${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_equalizer_visual.qml)
set_tests_properties(qml_equalizer_visual_test PROPERTIES
    TIMEOUT 20
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_CONTROLS_STYLE=Basic;AGPLAYER_TEST_AUDIO=${SINE_WAV_FIXTURE}"
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
)
add_test(NAME integrated_shell_contract_test
    COMMAND "${AGPLAYER_POWERSHELL_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass
        -File ${CMAKE_CURRENT_SOURCE_DIR}/scripts/integrated_shell_contract_test.ps1
        -SourceRoot ${CMAKE_SOURCE_DIR})
set_tests_properties(integrated_shell_contract_test PROPERTIES TIMEOUT 10)
add_test(NAME qml_integrated_theme_test
    COMMAND qml_main_window_test
        -input ${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_integrated_theme.qml)
set_tests_properties(qml_integrated_theme_test PROPERTIES
    TIMEOUT 25
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_CONTROLS_STYLE=Basic;AGPLAYER_TEST_AUDIO=${SINE_WAV_FIXTURE}"
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
)
add_test(NAME qml_design_system_test
    COMMAND qml_main_window_test
        -input ${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_design_system.qml)
set_tests_properties(qml_design_system_test PROPERTIES
    TIMEOUT 20
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_CONTROLS_STYLE=Basic"
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
)
add_test(NAME qml_themed_dialog_test
    COMMAND qml_main_window_test
        -input ${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_themed_dialog.qml)
set_tests_properties(qml_themed_dialog_test PROPERTIES
    TIMEOUT 20
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_CONTROLS_STYLE=Basic;AGPLAYER_TEST_TRANSLATIONS=1"
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
)
add_test(NAME qml_about_version_test
    COMMAND qml_main_window_test
        -input ${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_main_window.qml
        MainWindowControls::test_settings_y_about_separates_product_version_and_promise)
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
    TIMEOUT 120
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_CONTROLS_STYLE=Basic;QT_QUICK_BACKEND=software"
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
)
add_test(NAME qml_immersive_integration_test
    COMMAND qml_mini_player_test
        -o ${CMAKE_CURRENT_BINARY_DIR}/qml-immersive-integration.xml,junitxml
        -input ${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_immersive_integration.qml)
set_tests_properties(qml_immersive_integration_test PROPERTIES
    TIMEOUT 120
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_CONTROLS_STYLE=Basic;QT_QUICK_BACKEND=software"
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
)

# Native-only visual probe: the software integration suite above covers the
# fallback, but cannot verify a QQuickRhiItem texture sampled by ShaderEffect.
add_test(NAME qml_immersive_custom_colors_test
    COMMAND qml_mini_player_test
        -o ${CMAKE_CURRENT_BINARY_DIR}/qml-immersive-custom-colors.txt,txt
        -input ${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_immersive_custom_colors.qml)
set_tests_properties(qml_immersive_custom_colors_test PROPERTIES
    TIMEOUT 60
    RUN_SERIAL TRUE
    ENVIRONMENT "QT_QUICK_CONTROLS_STYLE=Basic"
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
)

add_test(NAME qml_immersive_glow_gpu_test
    COMMAND qml_mini_player_test
        -input ${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_immersive_glow.qml)
set_tests_properties(qml_immersive_glow_gpu_test PROPERTIES
    TIMEOUT 60
    RUN_SERIAL TRUE
    ENVIRONMENT "QT_QUICK_CONTROLS_STYLE=Basic"
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
    Qt6::QmlWorkerScript
)
target_compile_definitions(qml_audio_tools_test PRIVATE
    QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml"
)
agplayer_enable_warnings(qml_audio_tools_test)
add_dependencies(qml_audio_tools_test decoder_fixture)

add_test(NAME qml_audio_editor_native_input_test
    COMMAND qml_audio_tools_test
        -input ${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_audio_editor_native_input.qml
        -o ${CMAKE_CURRENT_BINARY_DIR}/qml-audio-editor-native-input.xml,junitxml)
set_tests_properties(qml_audio_editor_native_input_test PROPERTIES
    # Native Windows input can exceed 30 seconds in Debug while real fixture
    # playback and window activation complete.
    TIMEOUT 60
    ENVIRONMENT "QT_QPA_PLATFORM=${AGPLAYER_NATIVE_QPA};QT_QUICK_CONTROLS_STYLE=Basic;QML2_IMPORT_PATH=${CMAKE_BINARY_DIR}/app;AGPLAYER_TEST_AUDIO=${SINE_WAV_FIXTURE}"
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:AgPlayer>;PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/bin"
)

add_test(NAME qml_format_converter_test
    COMMAND qml_audio_tools_test -o ${CMAKE_BINARY_DIR}/qml-format-converter.xml,junitxml
        -input ${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_format_converter.qml)
set_tests_properties(qml_format_converter_test PROPERTIES
    TIMEOUT 30
    RESOURCE_LOCK qml_audio_tools_runtime
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_CONTROLS_STYLE=Basic;AGPLAYER_TEST_AUDIO=${SINE_WAV_FIXTURE}"
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:AgPlayer>;PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
)

add_test(NAME qml_format_converter_matrix_test
    COMMAND qml_audio_tools_test -o ${CMAKE_BINARY_DIR}/qml-format-converter-matrix.xml,junitxml
        -input ${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_format_converter_matrix.qml)
set_tests_properties(qml_format_converter_matrix_test PROPERTIES
    TIMEOUT 90
    RESOURCE_LOCK qml_audio_tools_runtime
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
    RESOURCE_LOCK qml_audio_tools_runtime
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_CONTROLS_STYLE=Basic"
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
)

add_test(NAME qml_filename_process_test
    COMMAND qml_audio_tools_test -input ${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_filename_process.qml)
set_tests_properties(qml_filename_process_test PROPERTIES
    TIMEOUT 30
    RESOURCE_LOCK qml_audio_tools_runtime
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_CONTROLS_STYLE=Basic;AGPLAYER_TEST_AUDIO=${SINE_WAV_FIXTURE}"
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:AgPlayer>;PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
)

add_test(NAME qml_metadata_editor_test
    COMMAND qml_audio_tools_test -o ${CMAKE_BINARY_DIR}/qml-metadata-editor.txt,txt
        -input ${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_metadata_editor.qml)
set_tests_properties(qml_metadata_editor_test PROPERTIES
    TIMEOUT 30
    RESOURCE_LOCK qml_audio_tools_runtime
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_CONTROLS_STYLE=Basic;AGPLAYER_TEST_AUDIO=${SINE_WAV_FIXTURE}"
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:AgPlayer>;PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
)

add_test(NAME qml_metadata_status_filter_test
    COMMAND qml_audio_tools_test -o ${CMAKE_BINARY_DIR}/qml-metadata-status-filter.txt,txt
        -input ${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_metadata_status_filter.qml)
set_tests_properties(qml_metadata_status_filter_test PROPERTIES
    TIMEOUT 15
    RESOURCE_LOCK qml_audio_tools_runtime
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_CONTROLS_STYLE=Basic;AGPLAYER_TEST_AUDIO=${SINE_WAV_FIXTURE}"
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:AgPlayer>;PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
)

add_test(NAME qml_vocal_separation_test
    COMMAND qml_audio_tools_test
        -o ${CMAKE_BINARY_DIR}/qml-vocal-separation.txt,txt
        -input ${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_vocal_separation.qml)
set_tests_properties(qml_vocal_separation_test PROPERTIES
    TIMEOUT 30
    RESOURCE_LOCK qml_audio_tools_runtime
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_CONTROLS_STYLE=Basic;AGPLAYER_TEST_AUDIO=${SINE_WAV_FIXTURE}"
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:AgPlayer>;PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
)

add_test(NAME metadata_editor_layout_contract_test
    COMMAND "${AGPLAYER_POWERSHELL_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass
        -File ${CMAKE_CURRENT_SOURCE_DIR}/scripts/metadata_editor_layout_contract_test.ps1
        -SourceRoot ${CMAKE_SOURCE_DIR})

add_test(NAME format_converter_reference_contract_test
    COMMAND "${AGPLAYER_POWERSHELL_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass
        -File ${CMAKE_CURRENT_SOURCE_DIR}/scripts/format_converter_reference_contract_test.ps1
        -SourceRoot ${CMAKE_SOURCE_DIR})
set_tests_properties(format_converter_reference_contract_test PROPERTIES
    TIMEOUT 10)

add_test(NAME player_action_icon_contract_test
    COMMAND "${AGPLAYER_POWERSHELL_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass
        -File ${CMAKE_CURRENT_SOURCE_DIR}/scripts/player_action_icon_contract_test.ps1
        -SourceRoot ${CMAKE_SOURCE_DIR})
set_tests_properties(player_action_icon_contract_test PROPERTIES TIMEOUT 10)


target_include_directories(import_controller_test PRIVATE
    "${CMAKE_SOURCE_DIR}/tests/core"
)
target_include_directories(waveform_provider_test PRIVATE
    "${CMAKE_SOURCE_DIR}/tests/core"
)
target_include_directories(track_waveform_thumbnail_provider_test PRIVATE
    "${CMAKE_SOURCE_DIR}/core/src"
)

add_dependencies(import_controller_test decoder_fixture video_fixtures)
target_compile_definitions(import_controller_test PRIVATE
    AGPLAYER_TEST_AUDIO="${SINE_WAV_FIXTURE}"
    AGPLAYER_TEST_VIDEO_ONLY="${VIDEO_ONLY_FIXTURE}"
    AGPLAYER_TEST_AUDIO_WITH_ATTACHED_PICTURE="${AUDIO_WITH_ATTACHED_PICTURE_FIXTURE}"
)
add_dependencies(playback_controller_test decoder_fixture)
add_dependencies(video_playback_controller_test decoder_fixture video_fixtures)
target_compile_definitions(video_playback_controller_test PRIVATE
    AGPLAYER_TEST_WAV="${SINE_WAV_FIXTURE}"
    AGPLAYER_TEST_VIDEO_WITH_AUDIO="${VIDEO_WITH_AUDIO_FIXTURE}"
    AGPLAYER_TEST_VIDEO_ONLY="${VIDEO_ONLY_FIXTURE}"
    AGPLAYER_VIDEO_DECODER_TESTING=1
)
target_include_directories(video_playback_controller_test PRIVATE
    "${CMAKE_SOURCE_DIR}/core/src"
)
add_dependencies(player_experience_controller_test decoder_fixture)
add_dependencies(waveform_provider_test decoder_fixture)
set_tests_properties(import_controller_test PROPERTIES
    ENVIRONMENT "AGPLAYER_TEST_AUDIO=${SINE_WAV_FIXTURE}"
)
set_tests_properties(playback_controller_test PROPERTIES
    # This executable contains 52 integration cases whose combined wall time varies under load.
    # Keep a 30 s QtTest watchdog per case while allowing the complete suite 90 s.
    TIMEOUT 90
    ENVIRONMENT "AGPLAYER_TEST_WAV=${SINE_WAV_FIXTURE};QTEST_FUNCTION_TIMEOUT=30000"
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/$<IF:$<CONFIG:Debug>,debug/bin,bin>"
)
set_tests_properties(video_playback_controller_test PROPERTIES
    TIMEOUT 60
    ENVIRONMENT
        "AGPLAYER_TEST_WAV=${SINE_WAV_FIXTURE};AGPLAYER_TEST_VIDEO_WITH_AUDIO=${VIDEO_WITH_AUDIO_FIXTURE};AGPLAYER_TEST_VIDEO_ONLY=${VIDEO_ONLY_FIXTURE}"
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/bin"
)
set_tests_properties(player_experience_controller_test PROPERTIES
    ENVIRONMENT "AGPLAYER_TEST_WAV=${SINE_WAV_FIXTURE}"
)
set_tests_properties(waveform_provider_test PROPERTIES
    TIMEOUT 120
    ENVIRONMENT "AGPLAYER_TEST_WAV=${SINE_WAV_FIXTURE}"
)
set_tests_properties(window_controller_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
set_tests_properties(playback_controller_test qml_main_window_test
    qml_integrated_theme_test
    qml_waveform_test
    PROPERTIES
    # Qt/FFmpeg global process state and the shared generated fixtures make
    # these integration tests deterministic only when they do not overlap.
    RUN_SERIAL TRUE
)
set_tests_properties(settings_controller_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
set_tests_properties(terrain_reactor_item_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
)
add_dependencies(terrain_reactor_item_test decoder_fixture)
target_compile_definitions(terrain_reactor_item_test PRIVATE AGPLAYER_TEST_WAV="${SINE_WAV_FIXTURE}")
set_tests_properties(track_waveform_thumbnail_item_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
set_tests_properties(video_frame_item_test PROPERTIES
    TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
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
add_test(NAME qml_rolling_theme_test
    COMMAND qml_main_window_test
        -input ${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_rolling_theme.qml)
set_tests_properties(qml_rolling_theme_test PROPERTIES
    TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_CONTROLS_STYLE=Basic;AGPLAYER_TEST_AUDIO=${SINE_WAV_FIXTURE}"
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
)

if(WIN32)
    # The offscreen plugin does not discover Windows fonts by itself. Use real
    # font metrics for compact text-fit assertions instead of placeholder boxes.
    set_property(TEST qml_rolling_theme_test APPEND PROPERTY ENVIRONMENT
        "QT_QPA_FONTDIR=$ENV{SystemRoot}/Fonts")
endif()

add_test(NAME qml_player_controls_layout_test
    COMMAND qml_main_window_test
        -input ${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_player_controls.qml)
set_tests_properties(qml_player_controls_layout_test PROPERTIES
    TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_CONTROLS_STYLE=Basic;AGPLAYER_TEST_AUDIO=${SINE_WAV_FIXTURE}"
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
)

add_test(NAME track_waveform_thumbnail_contract_test
    COMMAND "${AGPLAYER_POWERSHELL_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass
        -File ${CMAKE_CURRENT_SOURCE_DIR}/scripts/track_waveform_thumbnail_contract_test.ps1
        -SourceRoot ${CMAKE_SOURCE_DIR}
)
set_tests_properties(track_waveform_thumbnail_contract_test PROPERTIES TIMEOUT 10)

add_test(NAME tag_management_layout_contract_test
    COMMAND "${AGPLAYER_POWERSHELL_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass
        -File ${CMAKE_CURRENT_SOURCE_DIR}/scripts/tag_management_layout_contract_test.ps1
        -SourceRoot ${CMAKE_SOURCE_DIR}
)
set_tests_properties(tag_management_layout_contract_test PROPERTIES TIMEOUT 10)

add_test(NAME library_list_layout_contract_test
    COMMAND "${AGPLAYER_POWERSHELL_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass
        -File ${CMAKE_CURRENT_SOURCE_DIR}/scripts/library_list_layout_contract_test.ps1
        -SourceRoot ${CMAKE_SOURCE_DIR}
)
set_tests_properties(library_list_layout_contract_test PROPERTIES TIMEOUT 10)

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
    set(agplayer_swscale_runtime_dll
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/$<IF:$<CONFIG:Debug>,debug/bin,bin>/swscale-9.dll"
    )
    get_target_property(agplayer_runtime_directory AgPlayer BINARY_DIR)
    add_custom_target(agplayer_swscale_runtime
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${agplayer_swscale_runtime_dll}"
            "${agplayer_runtime_directory}"
        COMMENT "Deploying FFmpeg swscale runtime"
        VERBATIM
    )
    add_dependencies(AgPlayer agplayer_swscale_runtime)

    # This contract intentionally performs a nested Ninja configure.  Do not
    # register it from ad-hoc NMake builds that do not carry the preset's
    # vcpkg toolchain context; the canonical Windows presets satisfy all three
    # prerequisites and exercise it below.
    if (CMAKE_GENERATOR MATCHES "Ninja" AND
        CMAKE_TOOLCHAIN_FILE AND VCPKG_INSTALLED_DIR)
        string(REPLACE ";" "\\;" audio_editor_feature_options_qt_prefix
            "${CMAKE_PREFIX_PATH}")
        set(audio_editor_feature_options_arguments
            "-DSOURCE_DIR=${CMAKE_SOURCE_DIR}"
            "-DTEST_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/feature-options-contract"
            "-DTOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}"
            "-DQT_PREFIX=${audio_editor_feature_options_qt_prefix}"
            "-DVCPKG_INSTALLED_DIR=${VCPKG_INSTALLED_DIR}"
            "-DSOUNDTOUCH_DIR=${SoundTouch_DIR}"
            "-DNINJA_EXECUTABLE=${CMAKE_MAKE_PROGRAM}"
            "-DC_COMPILER=${CMAKE_C_COMPILER}"
            "-DCXX_COMPILER=${CMAKE_CXX_COMPILER}"
        )
        if (MSVC)
            get_filename_component(msvc_compiler_directory
                "${CMAKE_CXX_COMPILER}" DIRECTORY)
            get_filename_component(msvc_vc_directory
                "${msvc_compiler_directory}/../../../../../.." ABSOLUTE)
            set(msvc_vcvars64
                "${msvc_vc_directory}/Auxiliary/Build/vcvars64.bat")
            if (EXISTS "${msvc_vcvars64}")
                list(APPEND audio_editor_feature_options_arguments
                    "-DVCVARS_BAT=${msvc_vcvars64}")
            endif()
        endif()
        add_test(
            NAME audio_editor_feature_options_test
            COMMAND "${CMAKE_COMMAND}"
                ${audio_editor_feature_options_arguments}
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/audio_editor_feature_options_test.cmake"
        )
        set_tests_properties(audio_editor_feature_options_test PROPERTIES TIMEOUT 180)
    endif()

    add_test(
        NAME audio_tools_layout_contract_test
        COMMAND "${AGPLAYER_POWERSHELL_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/scripts/audio_tools_layout_contract_test.ps1
                -SourceRoot ${CMAKE_SOURCE_DIR}
    )
    set_tests_properties(audio_tools_layout_contract_test PROPERTIES TIMEOUT 10)

    add_test(
        NAME phase6_translation_coverage_test
        COMMAND "${AGPLAYER_POWERSHELL_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/scripts/phase6_translation_coverage_test.ps1
                -SourceRoot ${CMAKE_SOURCE_DIR}
    )
    set_tests_properties(phase6_translation_coverage_test PROPERTIES TIMEOUT 10)

    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_test(
            NAME debug_runtime_deployment_test
            COMMAND "${AGPLAYER_POWERSHELL_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass
                    -File ${CMAKE_CURRENT_SOURCE_DIR}/scripts/debug_runtime_deployment_test.ps1
                    -AppPath $<TARGET_FILE:AgPlayer>
        )
        set_tests_properties(debug_runtime_deployment_test PROPERTIES TIMEOUT 10)
    endif()

    add_test(
        NAME runtime_deployment_test
        COMMAND "${AGPLAYER_POWERSHELL_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/scripts/runtime_deployment_test.ps1
                -AppPath $<TARGET_FILE:AgPlayer>
    )
    set_tests_properties(runtime_deployment_test PROPERTIES TIMEOUT 10)

    add_test(
        NAME single_instance_test
        COMMAND "${AGPLAYER_POWERSHELL_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass
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
        NAME integrated_shell_lifecycle_smoke_test
        COMMAND $<TARGET_FILE:AgPlayer>
                --qa-test-mode
                --qa-integrated-shell-lifecycle-probe
                --qa-library
                ${CMAKE_CURRENT_BINARY_DIR}/integrated-shell-lifecycle-library.json
    )
    set_tests_properties(integrated_shell_lifecycle_smoke_test PROPERTIES
        TIMEOUT 20
        RUN_SERIAL TRUE
        ENVIRONMENT "QT_QUICK_BACKEND=software"
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
    )

    add_test(
        NAME installer_contract_test
        COMMAND "${AGPLAYER_POWERSHELL_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/scripts/installer_contract_test.ps1
    )
    set_tests_properties(installer_contract_test PROPERTIES
        ENVIRONMENT "AGPLAYER_INSTALLER_SCRIPT=${CMAKE_SOURCE_DIR}/installer/AgPlayer.iss"
    )
    add_test(
        NAME release_version_contract_test
        COMMAND "${AGPLAYER_POWERSHELL_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/scripts/release_version_contract_test.ps1
                -SourceRoot ${CMAKE_SOURCE_DIR}
    )
    file(RELATIVE_PATH AGPLAYER_TEST_BUILD_DIRECTORY
        "${CMAKE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}")
    add_test(
        NAME package_version_guard_test
        COMMAND "${AGPLAYER_POWERSHELL_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass
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
        COMMAND "${AGPLAYER_POWERSHELL_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass
            -File ${CMAKE_CURRENT_SOURCE_DIR}/scripts/window_icon_contract_test.ps1
            -SourceRoot ${CMAKE_SOURCE_DIR})
    add_test(NAME window_mixed_dpi_transition_test
        COMMAND window_controller_test auxiliaryWindowsKeepNativeSizeAcrossScreens
            -o -,txt)
    set_tests_properties(window_mixed_dpi_transition_test PROPERTIES
        TIMEOUT 20
        SKIP_REGULAR_EXPRESSION "hardware gap:"
        ENVIRONMENT "QT_QPA_PLATFORM=${AGPLAYER_NATIVE_QPA}"
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
    add_test(NAME window_taskbar_native_test
        COMMAND window_controller_test taskbarActivationDoesNotCancelMinimize
            -o -,txt)
    set_tests_properties(window_taskbar_native_test PROPERTIES
        TIMEOUT 10
        ENVIRONMENT "QT_QPA_PLATFORM=${AGPLAYER_NATIVE_QPA}"
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
    add_test(NAME windows_shell_runtime_test
        COMMAND "${AGPLAYER_POWERSHELL_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass
            -File ${CMAKE_CURRENT_SOURCE_DIR}/scripts/windows_shell_runtime_test.ps1
            -AppPath $<TARGET_FILE:AgPlayer>)
    set_tests_properties(windows_shell_runtime_test PROPERTIES
        TIMEOUT 20
        RUN_SERIAL TRUE)
    add_test(NAME windows_install_preflight_test
        COMMAND "${AGPLAYER_POWERSHELL_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass
            -File ${CMAKE_CURRENT_SOURCE_DIR}/scripts/windows_install_preflight_test.ps1)
    set_tests_properties(windows_install_preflight_test PROPERTIES TIMEOUT 30)
endif()
