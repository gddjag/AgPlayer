# AgPlayer Windows ARM64 LAME build

Based on the unmodified mp3lame 3.100#17 port and patches from vcpkg baseline `3ddaad9be959816602453ecb05533f8732464ef4`. The source version, SHA-512, public export table, encoder and HIP decoder stay unchanged.

The additional branch uses the upstream Makefile.MSVC for Windows ARM64 dynamic /MD Release-only builds without the optional frontend. It retains /O2, /GL, /LTCG and precise arithmetic; x86 ASM/MMX is excluded for this ARM target. All other target/linkage/frontend configurations retain the original port behavior. This permits a reproducible native ARM64 toolchain without requiring MSBuild.

Validate native PE machine, encoding and HIP decode exports, the FFmpeg format matrix and actual MP3 round trips before publication. Reassess this overlay when updating the vcpkg baseline.
