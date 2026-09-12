# x64 selects host-independent port features; every target library is built
# for both slices. In particular FFmpeg's upstream vcpkg build.sh loops over
# VCPKG_OSX_ARCHITECTURES and uses lipo, including assembly per architecture.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)
set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_OSX_ARCHITECTURES "x86_64;arm64")
set(VCPKG_OSX_DEPLOYMENT_TARGET "13.0")
# Only Release dependencies are needed by the distribution preset.
set(VCPKG_BUILD_TYPE release)

# Clang can compile/link a Universal object, but preprocessing (-E) supports
# only one architecture. LAME's configure checks otherwise inherit both -arch
# flags from CC and reject every preprocessor. Both targets use the same LP64
# little-endian configuration; keep the actual CC/CXX builds Universal.
if(PORT STREQUAL "mp3lame")
    set(VCPKG_MAKE_CONFIGURE_OPTIONS "CPP=/usr/bin/clang -E")
endif()
