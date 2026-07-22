cmake_minimum_required(VERSION 3.28)

if(POLICY CMP0174)
    cmake_policy(SET CMP0174 NEW)
endif()

project(AgPlayer VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(Warnings)
include(CTest)

find_package(Qt6 6.7 REQUIRED COMPONENTS
    Core
    Gui
    Quick
    Qml
    QuickControls2
    Concurrent
    Test
    QuickTest
)
find_package(FFMPEG REQUIRED)
find_path(MINIAUDIO_INCLUDE_DIRS miniaudio.h REQUIRED)

add_subdirectory(core)
add_subdirectory(qt)
add_subdirectory(tests)
