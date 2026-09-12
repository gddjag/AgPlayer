add_library(agplayer_core STATIC
    src/audio_editor/audio_file_analyzer.cpp
    src/audio_editor/audio_file_analyzer.hpp
    src/audio_editor/audio_source_probe.cpp
    src/audio_editor/audio_source_probe.hpp
    src/audio_editor/audio_document.cpp
    src/audio_editor/audio_document.hpp
    src/audio_editor/audio_event.hpp
    src/audio_editor/automation_time_mapper.hpp
    src/audio_editor/event_timeline.cpp
    src/audio_editor/event_timeline.hpp
    src/audio_editor/timeline_edit_command.cpp
    src/audio_editor/timeline_edit_command.hpp
    src/audio_editor/timeline_undo_stack.cpp
    src/audio_editor/timeline_undo_stack.hpp
    src/audio_editor/document_renderer.cpp
    src/audio_editor/document_renderer.hpp
    src/audio_editor/document_render_pipeline.cpp
    src/audio_editor/document_render_pipeline.hpp
    src/audio_editor/editor_playback_stream.cpp
    src/audio_editor/editor_playback_stream.hpp
    src/audio_editor/editor_player_bridge.hpp
    src/audio_editor/document_writer.cpp
    src/audio_editor/document_writer.hpp
    src/audio_editor/noise_reducer.cpp
    src/audio_editor/noise_reducer.hpp
    src/audio_editor/peak_pyramid.cpp
    src/audio_editor/peak_pyramid.hpp
    src/audio_editor/time_pixel_mapper.hpp
    src/audio_editor/time_pitch_session.cpp
    src/audio_editor/time_pitch_session.hpp
    src/audio_engine.cpp
    src/audio_stream_source.hpp
    src/stem_preview_mixer.cpp
    src/stem_preview_mixer.hpp
    src/c_api.cpp
    src/core_context.cpp
    src/formant_preserver.cpp
    src/formant_preserver.hpp
    src/decoder.cpp
    src/video_decoder.cpp
    src/video_decoder.hpp
    src/graphic_equalizer.cpp
    src/graphic_equalizer.hpp
    src/metadata_writer.cpp
    src/pitch_shifter.cpp
    src/playback_time_pitch_stage.cpp
    src/playback_time_pitch_stage.hpp
    src/playback_session.cpp
    src/scratch_command_mailbox.hpp
    src/scratch_backfill_worker.cpp
    src/scratch_backfill_worker.hpp
    src/scratch_pcm_window.cpp
    src/scratch_pcm_window.hpp
    src/scratch_renderer.cpp
    src/scratch_renderer.hpp
    src/transcoder.cpp
    src/transcode_capability.cpp
    src/transcode_capability.hpp
    src/transcode_probe.cpp
    src/transcode_probe.hpp
    src/transcode_verifier.cpp
    src/transcode_verifier.hpp
    src/time_pitch_engine.cpp
    src/time_pitch_engine.hpp
    src/signalsmith_time_pitch_engine.cpp
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
target_link_libraries(agplayer_core PRIVATE
    ${FFMPEG_LIBRARIES}
    SoundTouch::SoundTouch
    signalsmith-stretch
)
if(APPLE)
    # Resolve Apple's audio frameworks at link time for signed applications.
    target_compile_definitions(agplayer_core PRIVATE MA_NO_RUNTIME_LINKING)
    target_link_libraries(agplayer_core PRIVATE
        "-framework CoreAudio" "-framework AudioToolbox" "-framework CoreFoundation")
endif()
if(BUILD_TESTING)
    target_compile_definitions(agplayer_core PRIVATE
        AGPLAYER_VIDEO_DECODER_TESTING=1
    )
endif()

# Stretch/Linear are header-only templates.  Keep their upstream MSVC
# conversion/shadowing warnings from weakening this project's /WX policy for
# any other translation unit.
if(MSVC)
    set_source_files_properties(src/signalsmith_time_pitch_engine.cpp
        PROPERTIES COMPILE_OPTIONS "/wd4244;/wd4267;/wd4305;/wd4127;/wd4456")
endif()

agplayer_enable_warnings(agplayer_core)
