add_library(agplayer_voice_clone_controller STATIC
    src/voice_clone_controller.cpp
    src/voice_clone_controller.hpp
    src/voice_clone_worker_client.cpp
    src/voice_clone_worker_client.hpp
)
set_target_properties(agplayer_voice_clone_controller PROPERTIES
    AUTOMOC ON
    POSITION_INDEPENDENT_CODE ON
)
target_include_directories(agplayer_voice_clone_controller PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(agplayer_voice_clone_controller PUBLIC
    agplayer_qt
    Qt6::Core
    Qt6::Gui
    Qt6::Network
)
agplayer_enable_warnings(agplayer_voice_clone_controller)

add_library(agplayer_voice_clone_plugin MODULE
    src/voice_clone_plugin.cpp
    src/voice_clone_plugin.hpp
)
set_target_properties(agplayer_voice_clone_plugin PROPERTIES
    AUTOMOC ON
    PREFIX ""
    OUTPUT_NAME "agplayer_voice_clone"
)
target_include_directories(agplayer_voice_clone_plugin PRIVATE
    "${CMAKE_SOURCE_DIR}/qt/src"
)
target_link_libraries(agplayer_voice_clone_plugin PRIVATE
    agplayer_voice_clone_controller
    Qt6::Core
)
agplayer_enable_warnings(agplayer_voice_clone_plugin)
