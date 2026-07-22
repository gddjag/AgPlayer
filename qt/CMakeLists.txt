add_library(agplayer_qt STATIC
    src/import_controller.cpp
    src/import_controller.hpp
    src/library_model.cpp
    src/library_model.hpp
    src/library_store.cpp
    src/library_store.hpp
    src/qml_registration.cpp
    src/qml_registration.hpp
)

set_target_properties(agplayer_qt PROPERTIES AUTOMOC ON)
target_include_directories(agplayer_qt PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(agplayer_qt
    PUBLIC
        Qt6::Core
        Qt6::Qml
        agplayer_core
    PRIVATE
        Qt6::Concurrent
)

agplayer_enable_warnings(agplayer_qt)
