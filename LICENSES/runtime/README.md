# Runtime third-party license material

`scripts/package-windows.ps1` must copy this directory recursively to
`licenses/runtime/` in every Windows release.  The names in this directory are
the release-contract paths; do not substitute a package-manager cache path in
the installer.

## Inventory source

The version and license records below were taken from the configured Release
vcpkg instance, `build/release/vcpkg_installed/x64-windows`, using the manifest
baseline `3ddaad9be959816602453ecb05533f8732464ef4`.

| Component | Release version | Release artifact / use | License material | Upstream source |
| --- | --- | --- | --- | --- |
| Qt | 6.7.0 | `Qt6*.dll`, plugins and QML modules deployed by `windeployqt` | `Qt-LGPL-3.0-only.txt` | <https://code.qt.io/cgit/qt/qtbase.git/tree/LICENSES/LGPL-3.0-only.txt?h=v6.7.0> |
| SoundTouch | 2.4.0 | `SoundTouch.dll` | `SoundTouch-LGPL-2.1-only.txt` | <https://codeberg.org/soundtouch/soundtouch/src/tag/2.4.0> |
| FFmpeg | 8.1.2 | `avcodec-62.dll`, `avformat-62.dll`, `avutil-60.dll`, `swresample-6.dll`, `swscale-9.dll` | `FFmpeg-LGPL-2.1-or-later.txt` | <https://github.com/FFmpeg/FFmpeg/tree/n8.1.2> |
| LAME | 3.100 | `libmp3lame.dll` | `LAME-LGPL-2.0-only.txt` | <https://sourceforge.net/projects/lame/files/lame/3.100/> |
| Opus | 1.5.2 | `opus.dll` | `Opus-BSD-3-Clause.txt` | <https://github.com/xiph/opus/tree/v1.5.2> |
| libogg | 1.3.6 | `ogg.dll` | `libogg-BSD-3-Clause.txt` | <https://github.com/xiph/ogg/tree/v1.3.6> |
| libvorbis | 1.3.7 | `vorbis.dll`, `vorbisenc.dll`, `vorbisfile.dll` | `libvorbis-BSD-3-Clause.txt` | <https://github.com/xiph/vorbis/tree/v1.3.7> |
| miniaudio | 0.11.25 | header compiled into AgPlayer | `miniaudio-Unlicense-or-MIT-0.txt` | <https://github.com/mackron/miniaudio/tree/0.11.25> |
| ONNX Runtime C API headers | 1.24.4 | headers compiled into `AgSeparationWorker.exe`; no ORT DLL is bundled by this target | `ONNX-Runtime-MIT.txt` | <https://github.com/microsoft/onnxruntime/tree/v1.24.4/include/onnxruntime> |

The copied vcpkg copyright files are authoritative for the exact dependency
instances used by this Release build.  The Qt license file is from the Qt
6.7.0 source tag because the installed SDK does not contain a standalone
license-text file.

`Qt-6.7-DEPLOYED-ATTRIBUTIONS.md` records the observed deployed Qt modules,
their configured bundled third-party code and the exact Qt source-license files
included in this directory.  It must be refreshed from the final package stage.

## LGPL release boundary

This directory provides notice and license text; it is not a conclusion that a
release complies with LGPL or any other license.  The Windows release must keep
Qt and SoundTouch dynamically linked and must not block a recipient from
replacing those shipped DLLs.  A release owner must also make the corresponding
source, modification status, and any required installation information
available as the applicable licenses require.

Qt libraries and plugins can themselves contain separately licensed third-party
code.  Before shipping, compare the actual `windeployqt` output against the Qt
6.7 third-party attribution material and include every attribution applicable
to the deployed Qt modules/plugins.  This repository does not infer that the
single Qt LGPL text covers those embedded components.
