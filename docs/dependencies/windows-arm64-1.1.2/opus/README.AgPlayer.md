# AgPlayer Windows ARM64 Opus NEON configuration

This is the pinned Opus 1.5.2#1 port from vcpkg baseline `3ddaad9be959816602453ecb05533f8732464ef4`. Source version, SHA-512 and existing patch are unchanged.

Windows ARM64 configurations pass the standard `aarch64` processor alias to this dependency's CMake project, because upstream NEON detection only accepts lowercase `arm`/`aarch64` and skips canonical Windows `ARM64`. Other platforms and architectures retain the original port behavior. This does not change the MSVC target or PE machine.

Validate actual NEON compile definitions/source objects, resulting native ARM64 DLL, and Opus encode/decode round trips before release. Reassess this overlay when changing the vcpkg baseline.
