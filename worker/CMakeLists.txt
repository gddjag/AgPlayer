add_library(ag_separation_worker_core STATIC
    src/cancellation_token.cpp
    src/cancellation_token.hpp
    src/demucs_adapter.cpp
    src/demucs_adapter.hpp
    src/mdx_adapter.cpp
    src/mdx_adapter.hpp
    src/native_worker_backend.cpp
    src/native_worker_backend.hpp
    src/ort_runtime.cpp
    src/ort_runtime.hpp
    src/ort_session.cpp
    src/ort_session.hpp
    src/output_transaction.cpp
    src/output_transaction.hpp
    src/separation_protocol.cpp
    src/separation_protocol.hpp
    src/trusted_profiles.cpp
    src/trusted_profiles.hpp
    src/worker_engine.cpp
    src/worker_engine.hpp
)

set_target_properties(ag_separation_worker_core PROPERTIES AUTOMOC ON)
target_include_directories(ag_separation_worker_core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime/include"
        "${CMAKE_SOURCE_DIR}/core/src"
)
target_include_directories(ag_separation_worker_core SYSTEM PRIVATE
    "${FFMPEG_INCLUDE_DIRS}"
)
target_link_libraries(ag_separation_worker_core
    PUBLIC Qt6::Core
    PRIVATE agplayer_core ${FFMPEG_LIBRARIES}
)
if(WIN32)
    target_link_libraries(ag_separation_worker_core PRIVATE dxgi)
endif()
agplayer_enable_warnings(ag_separation_worker_core)

add_executable(AgSeparationWorker src/main.cpp)
target_link_libraries(AgSeparationWorker PRIVATE ag_separation_worker_core)
agplayer_enable_warnings(AgSeparationWorker)
