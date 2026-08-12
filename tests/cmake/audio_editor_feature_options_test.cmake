cmake_minimum_required(VERSION 3.28)

foreach(required IN ITEMS SOURCE_DIR TEST_BINARY_DIR TOOLCHAIN_FILE QT_PREFIX
                          VCPKG_INSTALLED_DIR NINJA_EXECUTABLE)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Missing required test argument: ${required}")
    endif()
endforeach()

file(REMOVE_RECURSE "${TEST_BINARY_DIR}")

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        -S "${SOURCE_DIR}"
        -B "${TEST_BINARY_DIR}"
        -G Ninja
        -DCMAKE_BUILD_TYPE=Release
        "-DCMAKE_MAKE_PROGRAM=${NINJA_EXECUTABLE}"
        "-DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE}"
        "-DCMAKE_PREFIX_PATH=${QT_PREFIX}"
        "-DVCPKG_INSTALLED_DIR=${VCPKG_INSTALLED_DIR}"
        -DVCPKG_MANIFEST_MODE=OFF
        -DVCPKG_TARGET_TRIPLET=x64-windows
        -DAG_ENABLE_NATIVE_RECORDING=OFF
    RESULT_VARIABLE configure_result
    OUTPUT_VARIABLE configure_stdout
    ERROR_VARIABLE configure_stderr
)

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

if(NOT cache MATCHES "AG_ENABLE_NATIVE_RECORDING:BOOL=OFF")
    message(FATAL_ERROR
        "AG_ENABLE_NATIVE_RECORDING=OFF must survive configuration")
endif()
