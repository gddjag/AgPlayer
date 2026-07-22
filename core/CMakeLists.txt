add_library(agplayer_core STATIC
    src/c_api.cpp
    src/core_context.cpp
)

target_include_directories(agplayer_core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_include_directories(agplayer_core SYSTEM
    PRIVATE
        "${FFMPEG_INCLUDE_DIRS}"
        "${MINIAUDIO_INCLUDE_DIRS}"
)
target_link_libraries(agplayer_core PRIVATE ${FFMPEG_LIBRARIES})

agplayer_enable_warnings(agplayer_core)
