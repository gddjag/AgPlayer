cmake_minimum_required(VERSION 3.28)

foreach(required IN ITEMS SOURCE_DIR TEST_BINARY_DIR TOOLCHAIN_FILE QT_PREFIX
                          VCPKG_INSTALLED_DIR SOUNDTOUCH_DIR NINJA_EXECUTABLE
                          C_COMPILER CXX_COMPILER)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Missing required test argument: ${required}")
    endif()
endforeach()

file(REMOVE_RECURSE "${TEST_BINARY_DIR}")

string(REPLACE ";" "\\;" escaped_qt_prefix "${QT_PREFIX}")
set(configure_arguments
        -S "${SOURCE_DIR}"
        -B "${TEST_BINARY_DIR}"
        -G Ninja
        -DCMAKE_BUILD_TYPE=Release
        "-DCMAKE_MAKE_PROGRAM=${NINJA_EXECUTABLE}"
        "-DCMAKE_C_COMPILER=${C_COMPILER}"
        "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}"
        "-DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE}"
        "-DCMAKE_PREFIX_PATH=${escaped_qt_prefix}"
        "-DVCPKG_INSTALLED_DIR=${VCPKG_INSTALLED_DIR}"
        "-DSoundTouch_DIR=${SOUNDTOUCH_DIR}"
        -DVCPKG_MANIFEST_MODE=OFF
        -DVCPKG_TARGET_TRIPLET=x64-windows
)

list(APPEND configure_arguments -DAG_ENABLE_NATIVE_RECORDING=ON)

if(DEFINED VCVARS_BAT AND NOT "${VCVARS_BAT}" STREQUAL "")
    if(NOT EXISTS "${VCVARS_BAT}")
        message(FATAL_ERROR "MSVC environment script is missing: ${VCVARS_BAT}")
    endif()
    set(configure_command "\"${CMAKE_COMMAND}\"")
    foreach(argument IN LISTS configure_arguments)
        string(REPLACE "\"" "\\\"" escaped_argument "${argument}")
        string(APPEND configure_command " \"${escaped_argument}\"")
    endforeach()
    set(configure_batch "${TEST_BINARY_DIR}-configure.cmd")
    file(WRITE "${configure_batch}"
        "@echo off\r\n"
        "call \"${VCVARS_BAT}\" >nul\r\n"
        "if errorlevel 1 exit /b %errorlevel%\r\n"
        "${configure_command}\r\n")
    execute_process(
        COMMAND cmd.exe /D /C "${configure_batch}"
        RESULT_VARIABLE configure_result
        OUTPUT_VARIABLE configure_stdout
        ERROR_VARIABLE configure_stderr
    )
else()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" ${configure_arguments}
        RESULT_VARIABLE configure_result
        OUTPUT_VARIABLE configure_stdout
        ERROR_VARIABLE configure_stderr
    )
endif()

if(NOT configure_result EQUAL 0)
    message(FATAL_ERROR
        "Feature option configure failed (${configure_result})\n"
        "stdout:\n${configure_stdout}\n"
        "stderr:\n${configure_stderr}")
endif()

file(READ "${TEST_BINARY_DIR}/CMakeCache.txt" cache)

foreach(enabled_option IN ITEMS AG_ENABLE_AUDIO_EDITOR AG_ENABLE_TIME_PITCH)
    if(NOT cache MATCHES "${enabled_option}:BOOL=ON")
        message(FATAL_ERROR
            "${enabled_option} must default to BOOL=ON in the configured project")
    endif()
endforeach()

if(cache MATCHES "AG_ENABLE_NATIVE_RECORDING")
    message(FATAL_ERROR
        "AG_ENABLE_NATIVE_RECORDING must not remain in the configured project")
endif()
