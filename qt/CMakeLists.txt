find_package(Qt6 6.7 REQUIRED COMPONENTS ShaderTools)

add_library(agplayer_qt STATIC
    src/audio_editor/audio_editor_waveform_item.cpp
    src/audio_editor/audio_editor_waveform_item.hpp
    src/audio_editor/audio_editor_controller.cpp
    src/audio_editor/audio_editor_controller.hpp
    src/audio_editor/editor_playback_adapter.cpp
    src/audio_editor/editor_playback_adapter.hpp
    src/audio_editor/editor_action_model.cpp
    src/audio_editor/editor_action_model.hpp
    src/audio_editor/editor_viewport.cpp
    src/audio_editor/editor_viewport.hpp
    src/audio_editor/project_document.cpp
    src/audio_editor/project_document.hpp
    src/audio_editor/selection_drag_controller.cpp
    src/audio_editor/selection_drag_controller.hpp
    src/audio_editor/playback_clip_drag_adapter.cpp
    src/audio_editor/playback_clip_drag_adapter.hpp
    src/audio_file_discovery.cpp
    src/audio_file_discovery.hpp
    src/audio_preview_controller.cpp
    src/audio_preview_controller.hpp
    src/file_association_controller.cpp
    src/file_association_controller.hpp
    src/frequency_color_waveform_settings.cpp
    src/frequency_color_waveform_settings.hpp
    src/frequency_color_mix.hpp
    src/filename_processor.cpp
    src/filename_processor.hpp
    src/filename_transform_engine.cpp
    src/filename_transform_engine.hpp
    src/filename_validator.cpp
    src/filename_validator.hpp
    src/rename_plan.cpp
    src/rename_plan.hpp
    src/rename_transaction.cpp
    src/rename_transaction.hpp
    src/rename_journal_store.cpp
    src/rename_journal_store.hpp
    src/editor_timeline_math.cpp
    src/editor_timeline_math.hpp
    src/equalizer_controller.cpp
    src/equalizer_controller.hpp
    src/audio_tools_controller.cpp
    src/audio_tools_controller.hpp
    src/audio_visual_feature_controller.cpp
    src/audio_visual_feature_controller.hpp
    src/bpm_analyzer.cpp
    src/bpm_analyzer.hpp
    src/cache_janitor.cpp
    src/cache_janitor.hpp
    src/format_converter.cpp
    src/format_converter.hpp
    src/format_conversion_plan.cpp
    src/format_conversion_plan.hpp
    src/format_conversion_task_model.cpp
    src/format_conversion_task_model.hpp
    src/format_conversion_filter_model.cpp
    src/format_conversion_filter_model.hpp
    src/global_hotkey_manager.cpp
    src/global_hotkey_manager.hpp
    src/rolling_keyboard_handler.cpp
    src/rolling_keyboard_handler.hpp
    src/import_controller.cpp
    src/import_controller.hpp
    src/library_filter_model.cpp
    src/library_filter_model.hpp
    src/library_file_operations.cpp
    src/library_file_operations.hpp
    src/library_model.cpp
    src/library_model.hpp
    src/lyrics_cache.cpp
    src/lyrics_cache.hpp
    src/lyrics_line_model.cpp
    src/lyrics_line_model.hpp
    src/lyrics_provider.cpp
    src/lyrics_provider.hpp
    src/lyrics_provider_chain.cpp
    src/lyrics_provider_chain.hpp
    src/lrcapi_lyrics_provider.cpp
    src/lrcapi_lyrics_provider.hpp
    src/lyrics_ovh_provider.cpp
    src/lyrics_ovh_provider.hpp
    src/unison_lyrics_provider.cpp
    src/unison_lyrics_provider.hpp
    src/lyrics_service.cpp
    src/lyrics_service.hpp
    src/resource_folder_controller.cpp
    src/resource_folder_controller.hpp
    src/library_navigation_model.cpp
    src/library_navigation_model.hpp
    src/library_store.cpp
    src/library_store.hpp
    src/tag_model.cpp
    src/tag_model.hpp
    src/tag_filter_model.cpp
    src/tag_filter_model.hpp
    src/tag_store.cpp
    src/tag_store.hpp
    src/terrain_reactor_item.cpp
    src/terrain_reactor_item.hpp
    src/terrain_reactor_state.cpp
    src/terrain_reactor_state.hpp
    src/metadata_editor.cpp
    src/metadata_editor.hpp
    src/metadata_text.cpp
    src/metadata_text.hpp
    src/native_drop_router.cpp
    src/native_drop_router.hpp
    src/vocal_separation_catalog.cpp
    src/vocal_separation_catalog.hpp
    src/vocal_separation_installer.cpp
    src/vocal_separation_installer.hpp
    src/external_separation_runtime.cpp
    src/external_separation_runtime.hpp
    src/vocal_separation_history.cpp
    src/vocal_separation_history.hpp
    src/vocal_separation_controller.cpp
    src/vocal_separation_controller.hpp
    src/playback_controller.cpp
    src/playback_controller.hpp
    src/video_playback_controller.cpp
    src/video_playback_controller.hpp
    src/video_frame_item.cpp
    src/video_frame_item.hpp
    src/player_experience_controller.cpp
    src/player_experience_controller.hpp
    src/playback_state_store.cpp
    src/playback_state_store.hpp
    src/replay_gain_scanner.cpp
    src/replay_gain_scanner.hpp
    src/resource_path.cpp
    src/resource_path.hpp
    src/separation_process_client.cpp
    src/separation_process_client.hpp
    ${CMAKE_SOURCE_DIR}/worker/src/separation_protocol.cpp
    ${CMAKE_SOURCE_DIR}/worker/src/separation_protocol.hpp
    src/playlist_model.cpp
    src/playlist_model.hpp
    src/qml_registration.cpp
    src/qml_registration.hpp
    src/runtime_log.cpp
    src/frequency_color_waveform_settings.cpp
    src/frequency_color_waveform_settings.hpp
    src/settings_controller.cpp
    src/settings_controller.hpp
    src/update_checker.cpp
    src/update_checker.hpp
    src/translation_manager.cpp
    src/translation_manager.hpp
    src/track_waveform_thumbnail_provider.cpp
    src/track_waveform_thumbnail_provider.hpp
    src/track_waveform_thumbnail_item.cpp
    src/track_waveform_thumbnail_item.hpp
    src/runtime_log.hpp
    src/waveform_provider.cpp
    src/waveform_provider.hpp
    src/waveform_coordinate_mapper.cpp
    src/waveform_coordinate_mapper.hpp
    src/window_controller.cpp
    src/window_controller.hpp
    src/waveform_item.cpp
    src/waveform_item.hpp
)

set_target_properties(agplayer_qt PROPERTIES AUTOMOC ON)
qt_add_resources(agplayer_qt agplayer_separation_python
    PREFIX "/separation" BASE "resources" FILES
    resources/external_separation_worker.py
    resources/cuda-runtime-manifest.json)
if(BUILD_TESTING)
    target_compile_definitions(agplayer_qt PUBLIC
        AGPLAYER_VIDEO_FRAME_ITEM_TESTING=1
    )
endif()
target_include_directories(agplayer_qt
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    PRIVATE
        "${CMAKE_BINARY_DIR}/generated"
        "${CMAKE_SOURCE_DIR}/core/src"
        "${CMAKE_SOURCE_DIR}/worker/src"
)
target_link_libraries(agplayer_qt
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::Widgets
        agplayer_core
    PRIVATE
        Qt6::Concurrent
        Qt6::GuiPrivate
        Qt6::Network
)

# Ninja cannot infer MSVC /showIncludes dependencies when the compiler emits
# localized Chinese prefixes. These explicit edges keep QML type allocation in
# qml_registration.cpp in lockstep with TerrainReactorItem's class layout.
set_property(SOURCE src/qml_registration.cpp APPEND PROPERTY OBJECT_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/terrain_reactor_item.hpp;${CMAKE_CURRENT_SOURCE_DIR}/src/terrain_reactor_state.hpp"
)
set_property(SOURCE src/terrain_reactor_item.cpp APPEND PROPERTY OBJECT_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/terrain_reactor_item.hpp;${CMAKE_CURRENT_SOURCE_DIR}/src/terrain_reactor_state.hpp;${CMAKE_CURRENT_SOURCE_DIR}/src/terrain_reactor_gpu_data.hpp"
)
set_property(SOURCE src/terrain_reactor_item.cpp APPEND PROPERTY OBJECT_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/terrain_column_mesh.hpp;${CMAKE_CURRENT_SOURCE_DIR}/src/terrain_shadow_map.hpp")

qt_add_shaders(agplayer_qt agplayer_terrain_reactor_shaders
    PREFIX "/terrain"
    FILES
        shaders/terrain_reactor.vert
        shaders/terrain_reactor.frag
        shaders/terrain_shadow.frag
        shaders/terrain_lyrics.vert
        shaders/terrain_lyrics.frag
        shaders/terrain_glow_extract.frag
        shaders/terrain_glow_blur.frag
)
qt_add_shaders(agplayer_qt agplayer_terrain_shadow_vertex
    PREFIX "/terrain-shadow"
    DEFINES TERRAIN_SHADOW_PASS=1
    FILES shaders/terrain_reactor.vert
    OUTPUTS shaders/terrain_shadow.vert.qsb
)

if(WIN32)
    target_link_libraries(agplayer_qt PRIVATE dwmapi shell32)
endif()

if(APPLE)
    enable_language(OBJCXX)
    set_target_properties(agplayer_qt PROPERTIES
        OBJCXX_STANDARD 17
        OBJCXX_STANDARD_REQUIRED ON
        OBJCXX_EXTENSIONS OFF)
    target_sources(agplayer_qt PRIVATE
        src/macos_system_integration.hpp
        src/macos_system_integration.mm
    )
    target_link_libraries(agplayer_qt PRIVATE
        "-framework AppKit"
        "-framework ServiceManagement"
        "-framework Carbon"
        "-framework ApplicationServices"
        "-framework CoreServices"
        "-framework UniformTypeIdentifiers"
    )
endif()

agplayer_enable_warnings(agplayer_qt)
