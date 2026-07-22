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
