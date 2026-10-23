# Windows ARM64 1.1.2 dependency build material

This directory contains the complete AgPlayer build overlays for SoundTouch,
LAME and Opus, copied byte-for-byte from the build inputs. It includes the
actual patches and build recipes, together with the vcpkg MIT license in
`VCPKG-LICENSE.txt`. `SHA256SUMS` identifies every file in this directory other
than the checksum list itself. These are source/build materials, with no
application source or binary toolchain included.

The configured vcpkg baseline is
[`3ddaad9be959816602453ecb05533f8732464ef4`](https://github.com/microsoft/vcpkg/tree/3ddaad9be959816602453ecb05533f8732464ef4).
`SOURCES.json` records the exact upstream versions, official source/archive
locations, archive SHA-512 values required by the recipes, licenses and pinned
baseline ports. The archive hashes identify the unpatched input archives;
the local SHA-256 list identifies the overlays. Obtain the upstream source
archive and apply the recipe's patches in their listed order.

| Library | Upstream source | Overlay version | Windows ARM64 modification |
| --- | --- | --- | --- |
| SoundTouch | [2.4.0](https://codeberg.org/soundtouch/soundtouch/src/tag/2.4.0) | 2.4.0#2 | Shared C++ library, installation fix and MSVC ARM64 optimization patch |
| LAME | [3.100](https://sourceforge.net/projects/lame/files/lame/3.100/) | 3.100#17 | Native ARM64 Release DLL via upstream `Makefile.MSVC` |
| Opus | [1.5.2](https://github.com/xiph/opus/tree/v1.5.2) | 1.5.2#1 | Windows ARM64 CMake processor alias to enable upstream NEON detection |

## Modification and arithmetic details

SoundTouch's existing installation patch moves the install component declaration
and defaults the optional SoundStretch utility off. Its port explicitly builds
`SoundTouch.dll` with `BUILD_SHARED_LIBS=ON`. The ARM64 patch puts `/O2` and
`/fp:precise` in compiler options rather than definitions, recognizes Windows
`ARM64` for `SOUNDTOUCH_USE_NEON`, and avoids the GCC-only `-mfpu=neon` option
under MSVC. Floating-point samples remain in use. The source archive SHA-512
is the value in this overlay, which differs from the baseline SoundTouch port;
the complete current recipe is included rather than claiming an unchanged port.
This macro corrects CMake's ARM recognition; it does not enable handwritten
NEON dispatch in SoundTouch 2.4.0. The `FIRFilter` and `TDStretch` factories
only have MMX/SSE optimized branches. On ARM64, `detectCPUextensions()` returns
zero and the generic implementations are selected even with
`SOUNDTOUCH_USE_NEON=1`. Native `/O2 /fp:precise` verification observed 20
compiler-generated vector instruction lines in `BPMDetect.cpp.obj`. Eight
output cases matched the original-source precise-arithmetic baseline exactly
(maximum absolute error `0.0`). This compiler SIMD evidence is separate from
the explicit upstream NEON paths verified in Opus and FFmpeg; it is not a
measured speedup claim.

LAME retains the baseline source, four inherited patches, configuration header
and encoder/HIP decoder export table. The added branch applies only to Windows
ARM64, dynamic library/dynamic CRT, Release and no optional frontend. It uses
upstream NMake with `CFG=STD`, `/MD /O2 /Ob2 /GL /fp:precise` and `/LTCG`,
disables x86 assembly/MMX and selects the ARM64 linker machine. Other target
branches remain in the complete portfile. LAME has no claimed handwritten ARM
NEON implementation in this build. No fast-math option is added.

Opus retains the baseline archive and `fix-pkgconfig-version.patch`. The extra
Windows ARM64 branch passes `CMAKE_SYSTEM_PROCESSOR=aarch64` to this dependency's
CMake project so upstream's case-sensitive detection sees ARM64. This alias
does not change the compiler target. The inspected native configuration uses
floating-point mode (`OPUS_FIXED_POINT=OFF`), `OPUS_FLOAT_APPROX=OFF`,
`OPUS_DISABLE_INTRINSICS=OFF` and `OPUS_USE_NEON=ON`; compiled upstream NEON
objects and an ARM64 DLL were observed. This is distinct from the existing
MinGW branch, which disables intrinsics. No codec source algorithm is patched.

These records describe the selected build inputs and dependency-level
verification. They do not assert that the final installer or all runtime tests
have already been verified. The final package owner must match its actual DLLs
to these inputs before publication.

## Runtime replacement and notices

SoundTouch, LAME and Opus remain separate DLLs. Keep their normal dynamic
loading/replacement behavior; do not add an integrity restriction that prevents
replacing a library with an ABI-compatible ARM64 build. Qt's corresponding
replacement boundary also remains in effect. Library license texts accompany
the installer under `licenses/runtime/`; Qt 6.8.3 has its own separate notice
record. This material records provenance and modifications; it does not make a
legal-compliance conclusion or a written source-supply offer.

This versioned directory is added separately on the public repository's `main`
branch with the ARM64 release addition. The historical `v1.1.2` tag remains
unchanged and does not contain these later materials.
