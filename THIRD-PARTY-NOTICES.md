# Third-party notices

## Release runtime inventory

The Windows release ships the license texts in `licenses/runtime/`.  Their
versions, upstream locations and deployed-file mapping are recorded in
`licenses/runtime/README.md`.  That inventory is generated from the configured
Release vcpkg instance, rather than from a generic dependency list.

| Component | Version | How it is used in the Windows release | License text shipped |
| --- | --- | --- | --- |
| Qt | 6.7.0 | Dynamically deployed `Qt6*.dll`, plugins and QML modules | `licenses/runtime/Qt-LGPL-3.0-only.txt` |
| SoundTouch | 2.4.0 | Dynamically deployed, replaceable `SoundTouch.dll` | `licenses/runtime/SoundTouch-LGPL-2.1-only.txt` |
| FFmpeg | 8.1.2 | Dynamically deployed codec/format/resampling/scaling DLLs | `licenses/runtime/FFmpeg-LGPL-2.1-or-later.txt` |
| LAME | 3.100 | Dynamically deployed `libmp3lame.dll` | `licenses/runtime/LAME-LGPL-2.0-only.txt` |
| Opus | 1.5.2 | Dynamically deployed `opus.dll` | `licenses/runtime/Opus-BSD-3-Clause.txt` |
| libogg | 1.3.6 | Dynamically deployed `ogg.dll` | `licenses/runtime/libogg-BSD-3-Clause.txt` |
| libvorbis | 1.3.7 | Dynamically deployed Vorbis DLLs | `licenses/runtime/libvorbis-BSD-3-Clause.txt` |
| miniaudio | 0.11.25 | Header compiled into AgPlayer | `licenses/runtime/miniaudio-Unlicense-or-MIT-0.txt` |
| ONNX Runtime C API headers | 1.24.4 | Headers compiled into `AgSeparationWorker.exe`; this target does not bundle `onnxruntime.dll` | `licenses/runtime/ONNX-Runtime-MIT.txt` |

The optional ONNX Runtime binaries and models are downloaded after installation
and are not part of this package inventory.  Their distribution terms must be
evaluated for the exact download selected at that time.

The Qt entry covers the Qt framework's LGPL text.  Qt plugins and modules may
include separately licensed third-party components; release engineering must
compare the actual `windeployqt` output with Qt 6.7's official third-party
attribution material before declaring the Qt portion complete.  The current
observed deployment record and its copied official license texts are in
`licenses/runtime/Qt-6.7-DEPLOYED-ATTRIBUTIONS.md`.

Providing these notices and license texts does not, by itself, establish LGPL
compliance.  In particular, the distributor remains responsible for source,
relinking/replacement and installation-information obligations that apply to
the final artifact.

## MPEG analysis-window coefficients

The experimental lossless-identification MP3 probe includes analysis-window
coefficients derived from FFmpeg n7.0. The source attribution and accompanying
license are shipped in `licenses/lossless-mp3-window-NOTICE.md` and
`licenses/FFmpeg-LGPL-2.1-or-later.txt`.

## WebGL Noise — 2D simplex kernel

Source: https://github.com/ashima/webgl-noise/tree/705a24f80b8e59a905ac5a31de2ea7aa6dce16f0

The textureless 2D simplex kernel in `qt/shaders/terrain_reactor.vert` is
adapted from this MIT-licensed source, not from the visual-reference application.
The legacy permutation is retained for reproducible terrain samples.

Copyright (C) 2011 by Ashima Arts (Simplex noise)
Copyright (C) 2011-2016 by Stefan Gustavson (Classic noise and others)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

## Signalsmith Stretch 1.3.2

Source: `Signalsmith-Audio/signalsmith-stretch`, commit
`57b93f4e9206a089a45387eaa39bdc9f310d3308`. The project is integrated as
headers through CMake `FetchContent`; it does not add a runtime DLL.

MIT License

Copyright (c) 2022 Geraint Luff / Signalsmith Audio Ltd.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## Signalsmith Linear 0.3.1

Source: `Signalsmith-Audio/linear`, tag `0.3.1`. This is the header-only
dependency used by Signalsmith Stretch.

MIT License

Copyright (c) 2025 Signalsmith Audio

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
