# Qt 6.7.0 deployed attribution record

This is a release-input record, not an assertion of legal compliance.  It was
prepared from the prior Windows package at `build/package/AgPlayer` and the
configured Qt installation at `D:/Qt/6.7.0/msvc2019_64`.  The final release
must repeat this inventory after `windeployqt` has run, because only the final
staging directory establishes what is distributed.

## Observed package scope

The inspected package contained Qt 6.7.0 DLLs for Core, Gui, Network, Qml,
Quick, Quick Controls 2, Quick Dialogs 2, Quick Layouts, Quick Shapes, Quick
Templates 2, Quick Window, SVG, Widgets, Concurrent, OpenGL, Qml Models,
Qml WorkerScript, and Labs FolderListModel.  It also contained
`platforms/qwindows.dll` and QML modules under `Qt.labs.folderlistmodel`,
`QtQml`, `QtQuick`, `QtQuick.Controls.Basic`, `QtQuick.Dialogs`,
`QtQuick.Layouts`, `QtQuick.NativeStyle`, `QtQuick.Shapes`,
`QtQuick.Templates`, and `QtQuick.Window`.

The installed Qt configuration records bundled (rather than system) zlib,
PCRE2, double-conversion, FreeType, HarfBuzz, JPEG, PNG and the Qt public
suffix list.  The following official Qt 6.7.0 source-license copies cover the
corresponding deployed modules and other compiled-in code paths in those
modules.

| Qt module / code path | Component and upstream version | Shipped file(s) |
| --- | --- | --- |
| Core | zlib 1.3.1; PCRE2 10.43 and SLJIT; double-conversion 3.3.0 | `Qt-zlib-1.3.1.txt`, `Qt-PCRE2-10.43.txt`, `Qt-PCRE2-SLJIT-10.43.txt`, `Qt-double-conversion-3.3.0.txt` |
| Core | Unicode UCD 30, Unicode CLDR 44.1; SipHash; BLAKE2; SHA-3/Keccak; TinyCBOR 0.6.0 | `Qt-Unicode-UCD-30.txt`, `Qt-Unicode-CLDR-44.1.txt`, `Qt-SipHash-CC0-1.0.txt`, `Qt-BLAKE2-CC0-or-Apache-2.0.txt`, `Qt-SHA3-BRG-BSD-2-Clause.txt`, `Qt-SHA3-Keccak-CC0-1.0.txt`, `Qt-TinyCBOR-0.6.0.txt` |
| Gui / Windows platform | FreeType 2.13.2 and its BDF, PCF and zlib portions; HarfBuzz 8.3.0; libjpeg-turbo 3.0.2 and IJG/zlib texts; libpng 1.6.43 | `Qt-FreeType-2.13.2.txt`, `Qt-FreeType-BDF.txt`, `Qt-FreeType-PCF.txt`, `Qt-FreeType-zlib.txt`, `Qt-HarfBuzz-8.3.0.txt`, `Qt-libjpeg-turbo-3.0.2.txt`, `Qt-libjpeg-IJG.txt`, `Qt-libjpeg-zlib.txt`, `Qt-libjpeg-COPYRIGHT.txt`, `Qt-libpng-1.6.43.txt` |
| Gui / RHI and image paths | OpenGL headers, Vulkan headers, D3D12 Memory Allocator, MiniEngine mipmap generator, smooth scaling, X server helper, Adobe Glyph List | `Qt-OpenGL-Headers-MIT.txt`, `Qt-Vulkan-Headers-Apache-2.0-or-MIT.txt`, `Qt-D3D12MemoryAllocator-MIT.txt`, `Qt-MiniEngine-MIT.txt`, `Qt-SmoothScale-BSD-2-Clause-and-Imlib2.txt`, `Qt-XServerHelper-X11-and-HPND.txt`, `Qt-Adobe-Glyph-List-BSD-3-Clause.txt` |
| Network | Public Suffix List and libpsl | `Qt-Public-Suffix-List-MPL-2.0.txt`, `Qt-libpsl-BSD-3-Clause.txt` |
| QML | JavaScriptCore Macro Assembler | `Qt-QML-MASM-BSD-2-Clause.txt` |
| SVG | XSVG | `Qt-XSVG-HPND-sell-variant.txt` |

Public-domain-attributed Qt Core algorithms (MD4, MD5 and SHA-1) have no
separate upstream license text in the Qt source tree; their attribution and
public-domain status are recorded in Qt's corresponding `qt_attribution.json`
files.

All entries above are copied verbatim from these official Qt 6.7.0 source
trees:

- <https://code.qt.io/cgit/qt/qtbase.git/tree/?h=v6.7.0>
- <https://code.qt.io/cgit/qt/qtdeclarative.git/tree/?h=v6.7.0>
- <https://code.qt.io/cgit/qt/qtsvg.git/tree/?h=v6.7.0>

`Qt-LGPL-3.0-only.txt` and `Qt-GPL-3.0-only.txt` are both shipped because the
LGPLv3 incorporates GPLv3 terms.  The Qt framework itself is deployed under
the selected LGPLv3 route for this release; the GPL text is not a claim that
AgPlayer is licensed under GPL.

## Source and replacement record

AgPlayer does not carry a Qt source patch or an overlay port.  The package
process deploys Qt's existing shared DLLs and plugins through `windeployqt`.
The release must preserve the deployed Qt DLL/plugin filenames and permit a
recipient to replace the LGPL Qt libraries; it must also provide the matching
Qt source or a valid written/source offer for the exact Qt 6.7.0 build it
ships.

SoundTouch is independently rebuildable from
`https://codeberg.org/soundtouch/soundtouch/src/tag/2.4.0` through the repository
overlay `cmake/vcpkg-overlays/soundtouch`.  Its `portfile.cmake` sets
`BUILD_SHARED_LIBS=ON`; `fix-install-includes.patch` changes only install
component placement and disables the `soundstretch` utility.  The shipped
`SoundTouch.dll` therefore remains the replaceable library boundary.  The
repository still needs an externally published, maintained source/offer URL
for the exact final release artifact; this document cannot create that service
or establish that all LGPL obligations have been met.
