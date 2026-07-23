add_library(agplayer_qt STATIC
    src/audio_tools_controller.cpp
    src/audio_tools_controller.hpp
    src/bpm_analyzer.hpp
    src/format_converter.cpp
    src/format_converter.hpp
    src/global_hotkey_manager.cpp
    src/global_hotkey_manager.hpp
    src/import_controller.cpp
    src/import_controller.hpp
    src/library_filter_model.cpp
    src/library_filter_model.hpp
    src/library_model.cpp
    src/library_model.hpp
    src/library_store.cpp
    src/library_store.hpp
    src/light_editor_controller.cpp
    src/light_editor_controller.hpp
    src/metadata_editor.cpp
    src/metadata_editor.hpp
    src/pitch_shifter.cpp
    src/pitch_shifter.hpp
    src/playback_controller.cpp
    src/playback_controller.hpp
    src/qml_registration.cpp
    src/qml_registration.hpp
    src/runtime_log.cpp
    src/settings_controller.cpp
    src/settings_controller.hpp
    src/runtime_log.hpp
    src/speed_adjuster.cpp
    src/speed_adjuster.hpp
    src/window_controller.cpp
    src/window_controller.hpp
    src/waveform_item.cpp
    src/waveform_item.hpp
)

set_target_properties(agplayer_qt PROPERTIES AUTOMOC ON)
target_include_directories(agplayer_qt PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(agplayer_qt
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        agplayer_core
    PRIVATE
        Qt6::Concurrent
)

agplayer_enable_warnings(agplayer_qt)
