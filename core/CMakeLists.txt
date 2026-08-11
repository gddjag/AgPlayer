add_library(agplayer_core STATIC
    src/audio_engine.cpp
    src/c_api.cpp
    src/core_context.cpp
    src/decoder.cpp
    src/graphic_equalizer.cpp
    src/graphic_equalizer.hpp
    src/light_editor.cpp
    src/multitrack_editor.cpp
    src/multitrack_editor.hpp
    src/metadata_writer.cpp
    src/pitch_shifter.cpp
    src/playback_session.cpp
    src/transcoder.cpp
    src/waveform_analyzer.cpp
    src/bpm_analyzer.cpp
    src/bpm_analyzer.hpp
    src/waveform_cache.cpp
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
