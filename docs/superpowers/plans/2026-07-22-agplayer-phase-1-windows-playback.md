# AgPlayer Phase 1 Windows Playback MVP Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a real Windows audio playback MVP that imports local audio, decodes and plays it through a Qt/QML interface, renders a cached RGB waveform, and keeps the main and mini players synchronized.

**Architecture:** A Qt-free C++17 core owns FFmpeg decoding, miniaudio output, queue state, Gapless transitions, waveform analysis, and a standard C ABI. A thin Qt Bridge consumes only that C ABI and exposes models/controllers to QML. The QML layer faithfully implements the approved main-player and mini-player visuals without showing controls for later phases.

**Tech Stack:** Qt 6.7.0 (QML/Quick/Quick Controls 2), C++17, CMake 4.4, Ninja 1.13, MSVC v143, FFmpeg 8.1 via vcpkg, miniaudio 0.11.25, CTest, Qt Test, Qt Quick Test.

## Global Constraints

- Work only in `D:\ai\AgPlayer`.
- Windows is the only Phase 1 target; production core code must not depend on Qt or Windows UI types.
- Qt Bridge must consume the public standard C ABI; public core headers must not expose Qt, FFmpeg, or miniaudio types.
- Use C++17 and compile every owned target with warnings as errors.
- Only FFmpeg and miniaudio are linked in Phase 1; do not add SoundTouch, kissfft, a database, or speculative abstractions.
- Every visible control must work; do not show settings, docking, search/filter, lyrics, or audio-tool controls yet.
- Seek P95 must be below 20ms, stable working set below 60MB, and Gapless transitions free of pops in the recorded validation environment.
- Every task ends with focused tests, full regression where relevant, and a commit.
- Do not run `windeployqt`, CPack, installer generators, deployment scripts, or any EXE packaging workflow until the user explicitly requests packaging.

## References

- Approved design: `docs/superpowers/specs/2026-07-22-agplayer-phase-1-windows-playback-design.md`
- Main visual target: `C:\Users\Administrator\Desktop\音视频播放器\AgPlayer音频播放器完整版\播放页确定.jpg`
- Mini-player target: `C:\Users\Administrator\Desktop\音视频播放器\AgPlayer音频播放器完整版\迷你播放器.jpg`
- Brand assets: `C:\Users\Administrator\Desktop\音视频播放器\AgPlayer音频播放器完整版\LOGO透明.png` and `透明底AG2.png`
- Functional icon source: selected Remix Icon SVGs stored with their upstream license; use only individual UI icons, never as the AgPlayer brand.

## Planned File Structure

```text
CMakeLists.txt
CMakePresets.json
vcpkg.json
.gitignore
cmake/Warnings.cmake
assets/brand/logo-lockup.png
assets/brand/logo-mark.png
assets/icons/*.svg
assets/licenses/RemixIcon-License.txt
core/CMakeLists.txt
core/include/agplayer/c_api.h
core/src/core_context.{hpp,cpp}
core/src/decoder.{hpp,cpp}
core/src/audio_engine.{hpp,cpp}
core/src/playback_session.{hpp,cpp}
core/src/pcm_ring_buffer.hpp
core/src/waveform_analyzer.{hpp,cpp}
core/src/waveform_cache.{hpp,cpp}
core/src/c_api.cpp
qt/CMakeLists.txt
qt/src/playback_controller.{hpp,cpp}
qt/src/library_model.{hpp,cpp}
qt/src/library_store.{hpp,cpp}
qt/src/import_controller.{hpp,cpp}
qt/src/waveform_item.{hpp,cpp}
qt/src/window_controller.{hpp,cpp}
qt/src/runtime_log.{hpp,cpp}
app/CMakeLists.txt
app/main.cpp
app/qml/AgPlayer/Main.qml
app/qml/AgPlayer/MiniPlayerWindow.qml
app/qml/AgPlayer/components/*.qml
app/qml/AgPlayer/theme/Theme.qml
tests/CMakeLists.txt
tests/core/*.cpp
tests/qt/*.cpp
tests/qml/tst_*.qml
tests/fixtures/generated/
tools/fixture_generator.cpp
tools/seek_benchmark.cpp
tools/memory_probe.ps1
design-qa.md
```

---

### Task 1: Re-enable the Windows toolchain and create a warning-clean build skeleton

**Files:**
- Create: `.gitignore`
- Create: `CMakeLists.txt`
- Create: `CMakePresets.json`
- Create: `vcpkg.json`
- Create: `cmake/Warnings.cmake`
- Create: `core/CMakeLists.txt`
- Create: `tests/CMakeLists.txt`
- Create: `tests/core/dependency_smoke_test.cpp`

**Interfaces:**
- Produces CMake targets `agplayer_core` and `dependency_smoke_test`.
- Produces presets `windows-msvc-debug` and `windows-msvc-release` using Qt at `D:/Qt/6.7.0/msvc2019_64` and vcpkg at `D:/vcpkg`.

- [ ] **Step 1: Restore the existing Visual Studio 2022 C++ workload**

Run in PowerShell:

```powershell
$setup = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\setup.exe'
$install = 'C:\Program Files\Microsoft Visual Studio\2022\Community'
Start-Process -Wait -WindowStyle Hidden -FilePath $setup -ArgumentList @(
  'modify', '--installPath', $install,
  '--add', 'Microsoft.VisualStudio.Workload.NativeDesktop',
  '--includeRecommended', '--passive', '--norestart'
)
& 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' `
  -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
  -property installationPath
```

Expected: the final command prints `C:\Program Files\Microsoft Visual Studio\2022\Community`. If it does not, stop this task and repair the Visual Studio installation before creating source files.

- [ ] **Step 2: Write the dependency smoke test first**

```cpp
// tests/core/dependency_smoke_test.cpp
#include <libavcodec/avcodec.h>
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

int main() {
    return avcodec_version() > 0U && ma_version_string() != nullptr ? 0 : 1;
}
```

- [ ] **Step 3: Add the minimal manifest and build files**

```json
{
  "name": "agplayer",
  "version-string": "1.0.0-dev",
  "builtin-baseline": "3ddaad9be959816602453ecb05533f8732464ef4",
  "dependencies": [
    {
      "name": "ffmpeg",
      "default-features": false,
      "features": ["avcodec", "avformat", "swresample"]
    },
    "miniaudio"
  ]
}
```

```cmake
# cmake/Warnings.cmake
function(agplayer_enable_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /WX /permissive- /Zc:__cplusplus)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
endfunction()
```

```cmake
# CMakeLists.txt
cmake_minimum_required(VERSION 3.28)
project(AgPlayer VERSION 1.0.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Warnings)
include(CTest)
find_package(Qt6 6.7 REQUIRED COMPONENTS Core Gui Quick Qml QuickControls2 Concurrent Test QuickTest)
find_package(FFMPEG REQUIRED)
find_path(MINIAUDIO_INCLUDE_DIRS miniaudio.h REQUIRED)
add_subdirectory(core)
add_subdirectory(tests)
```

```cmake
# core/CMakeLists.txt
add_library(agplayer_core STATIC src/bootstrap.cpp)
target_include_directories(agplayer_core
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
  PRIVATE "${FFMPEG_INCLUDE_DIRS}" "${MINIAUDIO_INCLUDE_DIRS}")
target_link_libraries(agplayer_core PRIVATE ${FFMPEG_LIBRARIES})
agplayer_enable_warnings(agplayer_core)
```

```cpp
// core/src/bootstrap.cpp
namespace agplayer { void bootstrap_anchor() noexcept {} }
```

```cmake
# tests/CMakeLists.txt
add_executable(dependency_smoke_test core/dependency_smoke_test.cpp)
target_include_directories(dependency_smoke_test PRIVATE "${FFMPEG_INCLUDE_DIRS}" "${MINIAUDIO_INCLUDE_DIRS}")
target_link_libraries(dependency_smoke_test PRIVATE ${FFMPEG_LIBRARIES})
agplayer_enable_warnings(dependency_smoke_test)
add_test(NAME dependency_smoke_test COMMAND dependency_smoke_test)
```

```json
{
  "version": 6,
  "configurePresets": [
    {
      "name": "windows-msvc-debug",
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/build/debug",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Debug",
        "CMAKE_TOOLCHAIN_FILE": "D:/vcpkg/scripts/buildsystems/vcpkg.cmake",
        "CMAKE_PREFIX_PATH": "D:/Qt/6.7.0/msvc2019_64",
        "VCPKG_TARGET_TRIPLET": "x64-windows"
      }
    },
    {
      "name": "windows-msvc-release",
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/build/release",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Release",
        "CMAKE_TOOLCHAIN_FILE": "D:/vcpkg/scripts/buildsystems/vcpkg.cmake",
        "CMAKE_PREFIX_PATH": "D:/Qt/6.7.0/msvc2019_64",
        "VCPKG_TARGET_TRIPLET": "x64-windows"
      }
    }
  ],
  "buildPresets": [
    { "name": "windows-msvc-debug", "configurePreset": "windows-msvc-debug" },
    { "name": "windows-msvc-release", "configurePreset": "windows-msvc-release" }
  ],
  "testPresets": [
    { "name": "windows-msvc-debug", "configurePreset": "windows-msvc-debug", "output": { "outputOnFailure": true } },
    { "name": "windows-msvc-release", "configurePreset": "windows-msvc-release", "output": { "outputOnFailure": true } }
  ]
}
```

```gitignore
# .gitignore
/build/
/.cache/
/.vs/
/out/
/tests/fixtures/generated/
*.user
*.tmp
```

- [ ] **Step 4: Configure, build, and verify the test fails only if a dependency is unavailable**

Run from a Visual Studio developer shell:

```powershell
cmd /d /s /c ""C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=amd64 && cmake --preset windows-msvc-debug && cmake --build --preset windows-msvc-debug && ctest --preset windows-msvc-debug --output-on-failure"
```

Expected: configuration succeeds, both targets compile with no warnings, and `1/1 Test #1: dependency_smoke_test ... Passed` appears.

- [ ] **Step 5: Commit**

```powershell
git add .gitignore CMakeLists.txt CMakePresets.json vcpkg.json cmake core tests
git commit -m "build: bootstrap Qt audio development toolchain"
```

---

### Task 2: Establish the tested C ABI lifecycle and error model

**Files:**
- Create: `core/include/agplayer/c_api.h`
- Create: `core/src/core_context.hpp`
- Create: `core/src/core_context.cpp`
- Create: `core/src/c_api.cpp`
- Create: `tests/core/c_api_lifecycle_test.cpp`
- Delete: `core/src/bootstrap.cpp`
- Modify: `core/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces opaque `ag_player` handles, stable `ag_result` values, UTF-8 errors, and `ag_player_create/destroy/last_error`.
- Later tasks extend the same header; no Qt, FFmpeg, STL, or miniaudio type crosses this boundary.

- [ ] **Step 1: Write the failing lifecycle test**

```cpp
#include <agplayer/c_api.h>
#include <array>
#include <cassert>
#include <cstring>

int main() {
    ag_player* player = nullptr;
    assert(ag_player_create(&player) == AG_OK);
    assert(player != nullptr);
    std::array<char, 64> message{};
    size_t required = 0;
    assert(ag_player_last_error(player, message.data(), message.size(), &required) == AG_OK);
    assert(required == 1);
    assert(std::strcmp(message.data(), "") == 0);
    ag_player_destroy(player);
    ag_player_destroy(nullptr);
}
```

- [ ] **Step 2: Run the focused test and confirm the missing API failure**

Run: `cmake --build --preset windows-msvc-debug --target c_api_lifecycle_test`

Expected: compilation fails because `agplayer/c_api.h` does not exist.

- [ ] **Step 3: Implement the minimal ABI**

```c
// core/include/agplayer/c_api.h
#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct ag_player ag_player;
typedef enum ag_result {
    AG_OK = 0,
    AG_INVALID_ARGUMENT = 1,
    AG_IO_ERROR = 2,
    AG_UNSUPPORTED_FORMAT = 3,
    AG_DECODE_ERROR = 4,
    AG_DEVICE_ERROR = 5,
    AG_CANCELLED = 6,
    AG_INTERNAL_ERROR = 7
} ag_result;

ag_result ag_player_create(ag_player** out_player);
void ag_player_destroy(ag_player* player);
ag_result ag_player_last_error(const ag_player* player, char* buffer,
                               size_t capacity, size_t* required);

#ifdef __cplusplus
}
#endif
```

```cpp
// core/src/core_context.hpp
#pragma once
#include <string>
#include <utility>
namespace agplayer {
class CoreContext final {
public:
    const std::string& last_error() const noexcept { return last_error_; }
    void set_error(std::string value) { last_error_ = std::move(value); }
private:
    std::string last_error_;
};
}
```

Implement `ag_player` as a private wrapper containing `agplayer::CoreContext`. `ag_player_last_error` must always set `required` to UTF-8 byte count plus the null terminator, return `AG_INVALID_ARGUMENT` for a null handle or null `required`, allow a null buffer when capacity is zero, and null-terminate every successful copy.

Replace `bootstrap.cpp` in `core/CMakeLists.txt` with `core_context.cpp` and `c_api.cpp`, then delete the bootstrap anchor; it must not survive as dead scaffolding.

- [ ] **Step 4: Run lifecycle and ABI hygiene checks**

Run:

```powershell
cmake --build --preset windows-msvc-debug --target c_api_lifecycle_test
ctest --preset windows-msvc-debug -R c_api_lifecycle_test --output-on-failure
rg -n "QObject|QString|QImage|QThread|AVFormatContext|ma_device" core/include
```

Expected: the test passes and `rg` prints no matches.

- [ ] **Step 5: Commit**

```powershell
git add core tests
git commit -m "feat(core): add stable C API lifecycle"
```

---

### Task 3: Implement the bounded PCM buffer and deterministic playback state machine

**Files:**
- Create: `core/src/pcm_ring_buffer.hpp`
- Create: `core/src/playback_session.hpp`
- Create: `core/src/playback_session.cpp`
- Create: `tests/core/pcm_ring_buffer_test.cpp`
- Create: `tests/core/playback_session_test.cpp`
- Modify: `core/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces `PcmRingBuffer(float* interleaved, channels)` with non-blocking `write/read/clear` operations.
- Produces `PlaybackSession` with `Stopped/Loading/Playing/Paused/Error` states and `Sequential/RepeatOne/Shuffle` modes.

- [ ] **Step 1: Write failing buffer and state tests**

```cpp
// tests/core/pcm_ring_buffer_test.cpp
#include "pcm_ring_buffer.hpp"
#include <array>
#include <cassert>
int main() {
    agplayer::PcmRingBuffer buffer(4, 2);
    const std::array<float, 6> input{1,2,3,4,5,6};
    assert(buffer.write(input.data(), 3) == 3);
    std::array<float, 4> output{};
    assert(buffer.read(output.data(), 2) == 2);
    assert((output == std::array<float,4>{1,2,3,4}));
    buffer.clear();
    assert(buffer.available_frames() == 0);
}
```

```cpp
// tests/core/playback_session_test.cpp
#include "playback_session.hpp"
#include <cassert>
int main() {
    agplayer::PlaybackSession session;
    session.set_queue({"a", "b", "c"}, 0);
    assert(session.current_path() == "a");
    assert(session.next_index() == 1);
    session.set_mode(agplayer::PlaybackMode::RepeatOne);
    assert(session.next_index() == 0);
    session.mark_error("decode failed");
    assert(session.state() == agplayer::PlaybackState::Error);
}
```

- [ ] **Step 2: Verify both tests fail because the types are missing**

Run: `cmake --build --preset windows-msvc-debug --target pcm_ring_buffer_test playback_session_test`

Expected: missing-header compilation failures.

- [ ] **Step 3: Implement the minimum correct types**

```cpp
// core/src/playback_session.hpp
#pragma once
#include <cstddef>
#include <string>
#include <vector>
namespace agplayer {
enum class PlaybackState { Stopped, Loading, Playing, Paused, Error };
enum class PlaybackMode { Sequential, RepeatOne, Shuffle };
class PlaybackSession final {
public:
    void set_queue(std::vector<std::string> paths, std::size_t start_index);
    void set_mode(PlaybackMode mode) noexcept { mode_ = mode; }
    std::size_t next_index() const;
    std::size_t previous_index() const;
    const std::string& current_path() const;
    PlaybackState state() const noexcept { return state_; }
    void mark_error(std::string message);
private:
    std::vector<std::string> paths_;
    std::size_t index_ = 0;
    PlaybackMode mode_ = PlaybackMode::Sequential;
    PlaybackState state_ = PlaybackState::Stopped;
    std::string error_;
};
}
```

Implement `PcmRingBuffer` with preallocated interleaved storage and atomic read/write frame counters. Reject zero channels/capacity in the constructor, never allocate in `read` or `write`, and copy at most the available capacity. Implement deterministic shuffle by accepting an injected next-index function in tests rather than using a global random generator.

- [ ] **Step 4: Run focused tests, Thread Sanitizer-equivalent static checks, and full regression**

Run:

```powershell
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug -R "pcm_ring_buffer|playback_session" --output-on-failure
ctest --preset windows-msvc-debug --output-on-failure
```

Expected: all tests pass and the build emits no warnings.

- [ ] **Step 5: Commit**

```powershell
git add core tests
git commit -m "feat(core): add playback state and PCM buffering"
```

---

### Task 4: Decode real media and expose immutable UTF-8 metadata through the C ABI

**Files:**
- Create: `core/src/decoder.hpp`
- Create: `core/src/decoder.cpp`
- Create: `tools/fixture_generator.cpp`
- Create: `tests/core/decoder_test.cpp`
- Modify: `core/include/agplayer/c_api.h`
- Modify: `core/src/c_api.cpp`
- Modify: `core/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces `Decoder::open/read/seek` with planar-to-interleaved float conversion through swresample.
- Produces opaque `ag_metadata` and UTF-8 getter functions whose returned views remain valid until destroy.

- [ ] **Step 1: Add a generated public-domain sine-wave fixture and failing metadata test**

`tools/fixture_generator.cpp` must write a deterministic two-second, 44.1kHz, stereo PCM WAV containing a -12dBFS 440Hz sine wave using only `std::ofstream`. Register it as a build dependency for the decoder test.

```cpp
// tests/core/decoder_test.cpp
#include <agplayer/c_api.h>
#include <cassert>
#include <cstring>
int main(int argc, char** argv) {
    assert(argc == 2);
    ag_metadata* metadata = nullptr;
    assert(ag_metadata_open(argv[1], &metadata) == AG_OK);
    assert(metadata != nullptr);
    assert(ag_metadata_sample_rate(metadata) == 44100);
    assert(ag_metadata_channels(metadata) == 2);
    assert(ag_metadata_duration_ms(metadata) >= 1990);
    assert(std::strcmp(ag_metadata_format(metadata), "wav") == 0);
    ag_metadata_destroy(metadata);
}
```

- [ ] **Step 2: Verify the metadata API is missing**

Run: `cmake --build --preset windows-msvc-debug --target decoder_test`

Expected: compilation fails on `ag_metadata` and its functions.

- [ ] **Step 3: Add exact public metadata declarations and decoder behavior**

```c
typedef struct ag_metadata ag_metadata;
ag_result ag_metadata_open(const char* utf8_path, ag_metadata** out_metadata);
void ag_metadata_destroy(ag_metadata* metadata);
const char* ag_metadata_title(const ag_metadata* metadata);
const char* ag_metadata_artist(const ag_metadata* metadata);
const char* ag_metadata_album(const ag_metadata* metadata);
const char* ag_metadata_format(const ag_metadata* metadata);
int ag_metadata_sample_rate(const ag_metadata* metadata);
int ag_metadata_channels(const ag_metadata* metadata);
int ag_metadata_bits_per_sample(const ag_metadata* metadata);
long long ag_metadata_bit_rate(const ag_metadata* metadata);
long long ag_metadata_duration_ms(const ag_metadata* metadata);
const unsigned char* ag_metadata_cover(const ag_metadata* metadata, size_t* size,
                                       const char** mime_type);
```

`Decoder::open` must use `avformat_open_input`, `avformat_find_stream_info`, `av_find_best_stream`, and `avcodec_open2`; `read` must use send/receive APIs and convert every source layout to interleaved `AV_SAMPLE_FMT_FLT`; `seek` must call `avformat_seek_file`, flush the codec, reset resampling state, and discard decoded samples until the requested timestamp. Read title/artist/album from stream metadata first and container metadata second. Copy attached-picture bytes into owned storage before closing FFmpeg packets.

- [ ] **Step 4: Run decoder, malformed-file, seek, and ABI tests**

Add assertions for an empty file returning `AG_UNSUPPORTED_FORMAT`, a truncated WAV returning a stable error without leaking, and seeking the sine fixture to 1500ms producing frames whose timestamps are not earlier than the target tolerance.

Run:

```powershell
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug -R "decoder|c_api" --output-on-failure
```

Expected: focused tests pass with no warnings and no Qt identifiers appear under `core/include`.

- [ ] **Step 5: Commit**

```powershell
git add core tests tools
git commit -m "feat(core): decode audio and expose media metadata"
```

---

### Task 5: Drive miniaudio from the decoder without blocking the audio callback

**Files:**
- Create: `core/src/audio_engine.hpp`
- Create: `core/src/audio_engine.cpp`
- Create: `tests/core/audio_engine_test.cpp`
- Modify: `core/src/core_context.hpp`
- Modify: `core/src/core_context.cpp`
- Modify: `core/include/agplayer/c_api.h`
- Modify: `core/src/c_api.cpp`
- Modify: `core/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces C API load/play/pause/stop/seek/volume/mute/snapshot operations.
- `AudioEngine` supports the normal Windows backend and a miniaudio null backend for deterministic automated tests.

- [ ] **Step 1: Write a failing end-to-end playback state test**

```cpp
#include <agplayer/c_api.h>
#include <cassert>
#include <chrono>
#include <thread>
int main(int argc, char** argv) {
    assert(argc == 2);
    ag_player_config config{};
    config.backend = AG_AUDIO_BACKEND_NULL;
    ag_player* player = nullptr;
    assert(ag_player_create_with_config(&config, &player) == AG_OK);
    assert(ag_player_load(player, argv[1]) == AG_OK);
    assert(ag_player_play(player) == AG_OK);
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    ag_playback_snapshot snapshot{};
    assert(ag_player_snapshot(player, &snapshot) == AG_OK);
    assert(snapshot.state == AG_PLAYING);
    assert(snapshot.position_ms > 0);
    assert(ag_player_pause(player) == AG_OK);
    assert(ag_player_seek(player, 1000) == AG_OK);
    assert(ag_player_set_volume(player, 0.25f) == AG_OK);
    ag_player_destroy(player);
}
```

- [ ] **Step 2: Confirm the playback API is missing**

Run: `cmake --build --preset windows-msvc-debug --target audio_engine_test`

Expected: compilation fails on the new configuration and playback symbols.

- [ ] **Step 3: Implement the audio engine and public commands**

Add to `c_api.h`:

```c
typedef enum ag_audio_backend { AG_AUDIO_BACKEND_DEFAULT = 0, AG_AUDIO_BACKEND_NULL = 1 } ag_audio_backend;
typedef enum ag_playback_state { AG_STOPPED, AG_LOADING, AG_PLAYING, AG_PAUSED, AG_ERROR } ag_playback_state;
typedef struct ag_player_config { ag_audio_backend backend; unsigned int buffer_frames; } ag_player_config;
typedef struct ag_playback_snapshot {
    ag_playback_state state;
    long long position_ms;
    long long duration_ms;
    float volume;
    int muted;
} ag_playback_snapshot;
ag_result ag_player_create_with_config(const ag_player_config*, ag_player**);
ag_result ag_player_load(ag_player*, const char* utf8_path);
ag_result ag_player_play(ag_player*);
ag_result ag_player_pause(ag_player*);
ag_result ag_player_stop(ag_player*);
ag_result ag_player_seek(ag_player*, long long position_ms);
ag_result ag_player_set_volume(ag_player*, float volume);
ag_result ag_player_set_muted(ag_player*, int muted);
ag_result ag_player_snapshot(const ag_player*, ag_playback_snapshot*);
```

In one `.cpp` file define `MINIAUDIO_IMPLEMENTATION`. Preallocate the PCM ring buffer before starting the device. The miniaudio callback may only read frames, apply the current atomic volume/mute value, zero-fill underruns, and update a sample counter. A dedicated decode thread handles open/read/seek and fills the ring buffer. All thread stop flags must be observed before `ma_device_uninit` and object destruction.

- [ ] **Step 4: Run the null-backend test repeatedly and inspect shutdown**

Run:

```powershell
cmake --build --preset windows-msvc-debug
1..50 | ForEach-Object { ctest --preset windows-msvc-debug -R audio_engine_test --output-on-failure; if($LASTEXITCODE){break} }
ctest --preset windows-msvc-debug --output-on-failure
```

Expected: all 50 repetitions pass, no process remains after each run, and full regression passes.

- [ ] **Step 5: Commit**

```powershell
git add core tests
git commit -m "feat(core): play decoded audio through miniaudio"
```

---

### Task 6: Add queue control, repeat/shuffle modes, and pop-free Gapless transitions

**Files:**
- Modify: `core/src/playback_session.hpp`
- Modify: `core/src/playback_session.cpp`
- Modify: `core/src/audio_engine.hpp`
- Modify: `core/src/audio_engine.cpp`
- Modify: `core/include/agplayer/c_api.h`
- Modify: `core/src/c_api.cpp`
- Create: `tests/core/queue_gapless_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces `ag_player_set_queue`, `ag_player_next`, `ag_player_previous`, `ag_player_set_mode`, and current-track index in snapshots.
- Preloads the next decoder before the current track drains; incompatible stream parameters are converted to the device format through the existing resampler.
- Keeps internal `AudioEngine::render(float*, size_t)` and `buffered_frames()` directly testable; neither is part of the public C ABI.

- [ ] **Step 1: Write a failing two-track Gapless test**

Generate two one-second WAV fixtures whose end/start samples form a continuous 440Hz sine wave. Exercise the same internal `AudioEngine::render` method used by the miniaudio callback so the test can inspect output without adding a production observer interface.

```cpp
#include "audio_engine.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <chrono>
#include <thread>
#include <vector>

static float boundary_delta(const std::vector<float>& samples, std::size_t index) {
    assert(index > 0 && index < samples.size());
    return std::abs(samples[index] - samples[index - 1]);
}

int main(int argc, char** argv) {
    assert(argc == 3);
    agplayer::AudioEngine engine({agplayer::Backend::Null, 44100, 2, 2048});
    engine.set_queue({argv[1], argv[2]}, 0);
    engine.play();
    while (engine.buffered_frames() < 2048U) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::vector<float> captured;
    std::array<float, 512 * 2> block{};
    while (captured.size() < 44100U * 2U * 2U) {
        engine.render(block.data(), 512);
        captured.insert(captured.end(), block.begin(), block.end());
        std::this_thread::sleep_for(std::chrono::milliseconds(12));
    }
    const std::size_t track_boundary = 44100U * 2U;
    assert(boundary_delta(captured, track_boundary) < 0.05f);
}
```

- [ ] **Step 2: Verify the queue API and transition logic are missing**

Run: `cmake --build --preset windows-msvc-debug --target queue_gapless_test`

Expected: compilation fails on queue symbols.

- [ ] **Step 3: Implement queue ABI and decoder preloading**

```c
typedef enum ag_playback_mode { AG_MODE_SEQUENTIAL, AG_MODE_REPEAT_ONE, AG_MODE_SHUFFLE } ag_playback_mode;
ag_result ag_player_set_queue(ag_player*, const char* const* utf8_paths,
                              size_t count, size_t start_index);
ag_result ag_player_next(ag_player*);
ag_result ag_player_previous(ag_player*);
ag_result ag_player_set_mode(ag_player*, ag_playback_mode);
```

Extend `ag_playback_snapshot` with `size_t track_index`, `size_t track_count`, and `ag_playback_mode mode`. When the active decoder has less than two device buffers remaining, open and prime the next decoder on the decode thread. Append its converted PCM immediately after the current stream; never stop/restart the miniaudio device at a compatible transition. Repeat-one must seek the existing decoder to zero and refill. Shuffle must use a per-session `std::mt19937` and avoid immediately repeating the current track when count is greater than one.

- [ ] **Step 4: Run sequential, repeat-one, shuffle, previous-at-start, and failed-next-track cases**

Run:

```powershell
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug -R "queue|gapless|playback_session" --output-on-failure
ctest --preset windows-msvc-debug --output-on-failure
```

Expected: the synthetic boundary delta is below the threshold, all modes pass, and a broken next file becomes a recoverable error rather than a loop.

- [ ] **Step 5: Commit**

```powershell
git add core tests tools
git commit -m "feat(core): add queue modes and gapless playback"
```

---

### Task 7: Analyze, cancel, serialize, and invalidate real waveform data

**Files:**
- Create: `core/src/waveform_analyzer.hpp`
- Create: `core/src/waveform_analyzer.cpp`
- Create: `core/src/waveform_cache.hpp`
- Create: `core/src/waveform_cache.cpp`
- Create: `tests/core/waveform_analyzer_test.cpp`
- Create: `tests/core/waveform_cache_test.cpp`
- Modify: `core/include/agplayer/c_api.h`
- Modify: `core/src/c_api.cpp`
- Modify: `core/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces synchronous, cancellation-aware `ag_waveform_analyze` for background callers.
- Produces owned `ag_waveform` peak data and stable cache keys based on canonical path, size, mtime, and cache format version.

- [ ] **Step 1: Write failing analyzer and cache tests**

```cpp
int main(int argc, char** argv) {
    assert(argc == 2);
    ag_cancel_token* token = ag_cancel_token_create();
    ag_waveform* waveform = nullptr;
    assert(ag_waveform_analyze(argv[1], 512, token, nullptr, nullptr, &waveform) == AG_OK);
    assert(ag_waveform_count(waveform) > 100);
    assert(ag_waveform_peak(waveform, 0) >= 0.0f);
    assert(ag_waveform_peak(waveform, 0) <= 1.0f);
    const std::filesystem::path path(argv[1]);
    const auto first_key = agplayer::WaveformCache::key_for(path);
    const auto first_mtime = std::filesystem::last_write_time(path);
    std::filesystem::last_write_time(path, first_mtime + std::chrono::seconds(1));
    assert(agplayer::WaveformCache::key_for(path) != first_key);
    ag_waveform_destroy(waveform);
    ag_cancel_token_destroy(token);
}
```

- [ ] **Step 2: Verify waveform symbols are absent**

Run: `cmake --build --preset windows-msvc-debug --target waveform_analyzer_test waveform_cache_test`

Expected: compilation fails on waveform and cancellation APIs.

- [ ] **Step 3: Implement the exact ABI and binary cache format**

```c
typedef struct ag_waveform ag_waveform;
typedef struct ag_cancel_token ag_cancel_token;
typedef void (*ag_progress_callback)(float progress, void* user_data);
ag_cancel_token* ag_cancel_token_create(void);
void ag_cancel_token_cancel(ag_cancel_token*);
void ag_cancel_token_destroy(ag_cancel_token*);
ag_result ag_waveform_analyze(const char* utf8_path, size_t target_points,
                              const ag_cancel_token*, ag_progress_callback,
                              void* user_data, ag_waveform** out_waveform);
size_t ag_waveform_count(const ag_waveform*);
float ag_waveform_peak(const ag_waveform*, size_t index);
void ag_waveform_destroy(ag_waveform*);
```

Decode in bounded chunks, combine channels by maximum absolute amplitude, bucket the full duration into at most `target_points`, and normalize once at completion. Cache files use magic `AGWF`, version `1`, source size, source mtime, point count, and little-endian float peaks. Write to a sibling `.tmp` file and atomically rename only after a successful flush. Reject bad magic, unsupported versions, non-finite peaks, truncated files, and source metadata mismatches.

`WaveformCache` exposes the internal testable operations `static std::string key_for(const std::filesystem::path&)`, `load`, and `save`; these are C++ core APIs, not additions to the public C ABI.

- [ ] **Step 4: Test progress monotonicity, cancellation, corruption, read-only fallback, and regression**

Run:

```powershell
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug -R "waveform" --output-on-failure
ctest --preset windows-msvc-debug --output-on-failure
```

Expected: cancellation returns `AG_CANCELLED`, corrupt caches are ignored and rebuilt, no `.tmp` remains after failure, and all tests pass.

- [ ] **Step 5: Commit**

```powershell
git add core tests
git commit -m "feat(core): analyze and cache real waveforms"
```

---

### Task 8: Import and persist the Phase 1 music library through a virtualized Qt model

**Files:**
- Create: `qt/CMakeLists.txt`
- Create: `qt/src/library_model.hpp`
- Create: `qt/src/library_model.cpp`
- Create: `qt/src/library_store.hpp`
- Create: `qt/src/library_store.cpp`
- Create: `qt/src/import_controller.hpp`
- Create: `qt/src/import_controller.cpp`
- Create: `tests/qt/library_model_test.cpp`
- Create: `tests/qt/library_store_test.cpp`
- Create: `tests/qt/import_controller_test.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces QML type `LibraryModel` with roles `trackId`, `path`, `title`, `artist`, `album`, `format`, `sampleRate`, `bitDepth`, `bitRate`, `durationMs`, `fileSize`, `coverUrl`, `favorite`, `available`, `importError`.
- Produces `ImportController::importUrls(QList<QUrl>)`, `progress`, `busy`, and per-file error reporting.

- [ ] **Step 1: Write failing model, persistence, and duplicate-import tests**

```cpp
void LibraryModelTest::rolesAndFavoritePersist() {
    LibraryModel model;
    TrackRecord track;
    track.path = QStringLiteral("C:/music/a.wav");
    track.title = QStringLiteral("A");
    model.append(track);
    QCOMPARE(model.rowCount(), 1);
    QVERIFY(model.setFavorite(0, true));
    QTemporaryDir dir;
    LibraryStore store(dir.filePath("library.json"));
    QVERIFY(store.save(model.tracks()));
    const auto loaded = store.load();
    QCOMPARE(loaded.size(), 1);
    QVERIFY(loaded.front().favorite);
}
```

```cpp
void ImportControllerTest::deduplicatesCanonicalPathsAndContinuesAfterFailure() {
    LibraryModel model;
    const ProbeFunction probe = [](const QString& path) {
        if (path.endsWith(QStringLiteral("good.wav"))) {
            TrackRecord track;
            track.path = path;
            track.title = QStringLiteral("Good");
            return ProbeResult{AG_OK, track, {}};
        }
        return ProbeResult{AG_DECODE_ERROR, {}, QStringLiteral("decode failed")};
    };
    ImportController importer(&model, probe);
    QSignalSpy finished(&importer, &ImportController::finished);
    importer.importUrls({url("good.wav"), url("./good.wav"), url("bad.wav")});
    QVERIFY(finished.wait());
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(importer.errors().size(), 1);
}
```

- [ ] **Step 2: Confirm the Qt bridge target and classes are missing**

Run: `cmake --build --preset windows-msvc-debug --target library_model_test library_store_test import_controller_test`

Expected: missing-target or missing-header failures.

- [ ] **Step 3: Implement the model and atomic JSON store**

```cpp
class LibraryModel final : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role { TrackIdRole = Qt::UserRole + 1, PathRole, TitleRole, ArtistRole,
                AlbumRole, FormatRole, SampleRateRole, BitDepthRole, BitRateRole,
                DurationMsRole, FileSizeRole, CoverUrlRole, FavoriteRole,
                AvailableRole, ImportErrorRole };
    Q_ENUM(Role)
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    Q_INVOKABLE bool setFavorite(int row, bool favorite);
    Q_INVOKABLE void playRow(int row);
};
```

Probe metadata through `ag_metadata_open` on `QtConcurrent::run`, copy every UTF-8 value before destroying the C handle, and marshal model mutations back through queued Qt calls. Copy embedded cover bytes to the application cache under a content hash and expose that local URL; when no cover exists, expose the supplied brand mark. Persist with `QSaveFile` and compact `QJsonDocument`; merge save requests with a 250ms single-shot timer. Compute `TrackId` from canonical path plus file identity data, not display metadata. On load, retain missing files but set `available=false`.

Define `TrackRecord` as the concrete Qt value object containing the listed roles. Define `ProbeResult { ag_result result; TrackRecord track; QString error; }` and `using ProbeFunction = std::function<ProbeResult(const QString&)>`; production supplies a C-ABI-backed probe and tests supply the lambda above.

- [ ] **Step 4: Run focused tests under repeated imports and full regression**

Run:

```powershell
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug -R "library|import_controller" --output-on-failure
ctest --preset windows-msvc-debug --output-on-failure
```

Expected: tests pass, a bad file does not cancel a good file, JSON is valid after forced process-safe atomic replacement, and no GUI-thread probe occurs.

- [ ] **Step 5: Commit**

```powershell
git add CMakeLists.txt qt tests
git commit -m "feat(qt): import and persist the music library"
```

---

### Task 9: Expose playback and shared window state to QML

**Files:**
- Create: `qt/src/playback_controller.hpp`
- Create: `qt/src/playback_controller.cpp`
- Create: `qt/src/window_controller.hpp`
- Create: `qt/src/window_controller.cpp`
- Create: `tests/qt/playback_controller_test.cpp`
- Create: `tests/qt/window_controller_test.cpp`
- Modify: `qt/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces QML singleton `PlaybackController` with read-only state and invokable commands.
- Produces QML singleton `WindowController` with `mainVisible`, `miniVisible`, `alwaysOnTop`, and close semantics.

- [ ] **Step 1: Write failing state synchronization tests**

```cpp
void PlaybackControllerTest::commandsReflectOnlyCoreSnapshots() {
    const QByteArray path = qgetenv("AGPLAYER_TEST_WAV");
    QVERIFY(!path.isEmpty());
    ag_player_config config{AG_AUDIO_BACKEND_NULL, 2048};
    ag_player* core = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &core), AG_OK);
    QCOMPARE(ag_player_load(core, path.constData()), AG_OK);
    {
        PlaybackController controller(core);
        QSignalSpy stateChanged(&controller, &PlaybackController::stateChanged);
        controller.play();
        QTRY_COMPARE(controller.state(), PlaybackController::Playing);
        QVERIFY(stateChanged.count() >= 1);
        controller.pause();
        QTRY_COMPARE(controller.state(), PlaybackController::Paused);
    }
    ag_player_destroy(core);
}
```

```cpp
void WindowControllerTest::switchingWindowsDoesNotRecreatePlayback() {
    WindowController windows;
    windows.showMini();
    QVERIFY(windows.miniVisible());
    QVERIFY(!windows.mainVisible());
    windows.showMain();
    QVERIFY(windows.mainVisible());
    QVERIFY(!windows.miniVisible());
}
```

- [ ] **Step 2: Verify the controller types are missing**

Run: `cmake --build --preset windows-msvc-debug --target playback_controller_test window_controller_test`

Expected: missing-class failures.

- [ ] **Step 3: Implement exact QML-facing properties and commands**

`PlaybackController` accepts a live `ag_player*` created with the C ABI; it does not own or bypass the handle. Its properties are `state`, `positionMs`, `durationMs`, `volume`, `muted`, `mode`, `trackIndex`, `trackCount`, `currentTrackId`, and `errorMessage`. Commands: `play`, `pause`, `togglePlayback`, `seek`, `next`, `previous`, `setVolume`, `toggleMuted`, `cycleMode`, `playRow`, and `toggleFavorite`. Poll core snapshots at 30Hz maximum and emit a Qt signal only when a value changed. Configure CTest with `AGPLAYER_TEST_WAV` pointing at the generated fixture.

`WindowController::showMini()` hides the main window only after the mini window is ready; `showMain()` performs the inverse; `requestClose()` stops playback, flushes the library store, cancels waveform work, and then calls `QCoreApplication::quit`. `setAlwaysOnTop` updates the mini window flags without creating a new window object.

- [ ] **Step 4: Run focused and full Qt tests**

Run:

```powershell
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug -R "playback_controller|window_controller" --output-on-failure
ctest --preset windows-msvc-debug --output-on-failure
```

Expected: no optimistic UI-state divergence, both windows observe the same controller, and shutdown is idempotent.

- [ ] **Step 5: Commit**

```powershell
git add qt tests
git commit -m "feat(qt): bridge playback and window state to QML"
```

---

### Task 10: Render and interact with the RGB waveform through the Qt Scene Graph

**Files:**
- Create: `qt/src/waveform_item.hpp`
- Create: `qt/src/waveform_item.cpp`
- Create: `tests/qt/waveform_item_test.cpp`
- Create: `tests/qml/tst_waveform.qml`
- Modify: `qt/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces QML type `WaveformItem` with `peaks`, `position`, `duration`, `hoverPosition`, `analysisProgress`, and `seekRequested(qint64)`.
- Uses one geometry node for peak bars and a small constant number of material/draw sections; it does not create one QML item per peak.

- [ ] **Step 1: Write failing geometry and interaction tests**

```cpp
void WaveformItemTest::mapsPointerToClampedTime() {
    WaveformItem item;
    item.setWidth(1000);
    item.setDuration(200000);
    QCOMPARE(item.timeForX(-10), 0);
    QCOMPARE(item.timeForX(250), 50000);
    QCOMPARE(item.timeForX(1200), 200000);
}
```

```qml
function test_drag_emits_one_committed_seek() {
    seekSpy.clear()
    mousePress(waveform, 100, 20)
    mouseMove(waveform, 600, 20)
    mouseRelease(waveform, 600, 20)
    compare(seekSpy.count, 1)
}
```

The test fixture contains `SignalSpy { id: seekSpy; target: waveform; signalName: "seekRequested" }` next to the `WaveformItem` instance.

- [ ] **Step 2: Verify the QQuickItem implementation is missing**

Run: `cmake --build --preset windows-msvc-debug --target waveform_item_test qml_waveform_test`

Expected: missing type/target failures.

- [ ] **Step 3: Implement Scene Graph geometry and pointer semantics**

Derive from `QQuickItem`, set `ItemHasContents`, and override `updatePaintNode`. Copy immutable peak data on the GUI thread and hand a revisioned vector to the render thread. Generate vertical line pairs centered on the item midline. Color normalized x positions with stops cyan `#00D4FF`, blue `#1688FF`, violet `#7B2FF7`, magenta `#E62E9B`, red `#FF4057`. Draw the played fraction at full opacity and the remainder at reduced opacity. Hover updates a preview time; drag commits one seek on release; click commits immediately.

- [ ] **Step 4: Run render, resize, empty-data, and input tests**

Run:

```powershell
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug -R "waveform_item|qml_waveform" --output-on-failure
ctest --preset windows-msvc-debug --output-on-failure
```

Expected: tests pass at zero width/duration, repeated resize does not leak nodes, and one drag produces one committed seek.

- [ ] **Step 5: Commit**

```powershell
git add qt tests
git commit -m "feat(ui): render interactive RGB waveform"
```

---

### Task 11: Build the faithful main-player window with only working controls

**Files:**
- Create: `assets/brand/logo-lockup.png`
- Create: `assets/brand/logo-mark.png`
- Create: `assets/icons/*.svg`
- Create: `assets/licenses/RemixIcon-License.txt`
- Create: `app/CMakeLists.txt`
- Create: `app/main.cpp`
- Create: `app/qml/AgPlayer/Main.qml`
- Create: `app/qml/AgPlayer/theme/Theme.qml`
- Create: `app/qml/AgPlayer/components/TitleBar.qml`
- Create: `app/qml/AgPlayer/components/PlayerPane.qml`
- Create: `app/qml/AgPlayer/components/PlayerControls.qml`
- Create: `app/qml/AgPlayer/components/TrackList.qml`
- Create: `app/qml/AgPlayer/components/EmptyLibrary.qml`
- Create: `tests/qml/tst_main_window.qml`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces executable target `AgPlayer` and QML module `AgPlayer`.
- Every visible main-window control routes to `PlaybackController`, `LibraryModel`, `ImportController`, or `WindowController`.

- [ ] **Step 1: Import supplied brand assets and a pinned functional icon set**

Copy `LOGO透明.png` to `assets/brand/logo-lockup.png` and `透明底AG2.png` to `assets/brand/logo-mark.png` without re-encoding. Download the official Remix Icon `4.9.0` archive, copy its license, and select only these line/fill pairs where applicable: play, pause, skip-back, skip-forward, repeat, repeat-one, shuffle, heart, volume-up, volume-mute, playlist, music, folder-open, pushpin, fullscreen/restore, subtract, checkbox-blank-circle, and close. Keep upstream SVG files unchanged; color them through QML image tinting only when Qt's image provider preserves shape quality.

Verify:

```powershell
Get-FileHash assets/brand/logo-lockup.png,assets/brand/logo-mark.png
Get-ChildItem assets/icons -Filter *.svg | Measure-Object
Test-Path assets/licenses/RemixIcon-License.txt
```

Expected: both logo hashes exist, all required icons exist, and the license file is present.

- [ ] **Step 2: Write the failing QML interaction test**

```qml
function test_visible_controls_have_actions() {
    verify(findChild(mainWindow, "importButton"))
    verify(findChild(mainWindow, "playPauseButton"))
    verify(findChild(mainWindow, "previousButton"))
    verify(findChild(mainWindow, "nextButton"))
    verify(findChild(mainWindow, "modeButton"))
    verify(findChild(mainWindow, "volumeSlider"))
    verify(findChild(mainWindow, "miniPlayerButton"))
    compare(findChild(mainWindow, "settingsButton"), null)
    compare(findChild(mainWindow, "audioToolsButton"), null)
}
```

- [ ] **Step 3: Verify no QML application exists yet**

Run: `cmake --build --preset windows-msvc-debug --target qml_main_window_test`

Expected: missing QML module/target failure.

- [ ] **Step 4: Implement the app module and approved visual hierarchy**

```cpp
// app/main.cpp
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <qqml.h>
#include <agplayer/c_api.h>
#include "import_controller.hpp"
#include "library_model.hpp"
#include "playback_controller.hpp"
#include "waveform_item.hpp"
#include "window_controller.hpp"
int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("AgPlayer"));
    app.setOrganizationName(QStringLiteral("AgPlayer"));
    ag_player* core = nullptr;
    if (ag_player_create(&core) != AG_OK) return 2;
    int result = 1;
    {
        LibraryModel library;
        PlaybackController playback(core);
        ImportController importer(&library);
        WindowController windows;
        qmlRegisterSingletonInstance("AgPlayer", 1, 0, "LibraryModel", &library);
        qmlRegisterSingletonInstance("AgPlayer", 1, 0, "PlaybackController", &playback);
        qmlRegisterSingletonInstance("AgPlayer", 1, 0, "ImportController", &importer);
        qmlRegisterSingletonInstance("AgPlayer", 1, 0, "WindowController", &windows);
        qmlRegisterType<WaveformItem>("AgPlayer", 1, 0, "WaveformItem");
        QQmlApplicationEngine engine;
        engine.loadFromModule("AgPlayer", "Main");
        if (!engine.rootObjects().isEmpty()) result = app.exec();
    }
    ag_player_destroy(core);
    return result;
}
```

`Theme.qml` is a singleton containing the approved tokens: background `#0A0A0F`, panel `#0E1118`, border `#253140`, cyan `#00D4FF`, violet `#7B2FF7`, favorite red `#FF334D`, primary text `#F5F7FA`, secondary text `#9AA4B2`, radii `8/12/18`, spacing `4/8/12/16/24/32`, UI font `Microsoft YaHei UI` for Chinese, and `Segoe UI` as the Windows fallback.

`Main.qml` uses a 1448×1086 reference size, a frameless title bar with working minimize/maximize/close, a top player area and a virtualized `ListView` below. `PlayerPane` contains embedded cover/fallback Logo, title, artist/album, real media badges, `WaveformItem`, elapsed/duration labels, and controls. `TrackList` shows only roles implemented in Phase 1. Empty, importing, playable, unavailable, and import-error states must be visually distinct and actionable. Add `Accessible.name`, keyboard focus, Enter/Space activation, and visible focus rings to every control.

- [ ] **Step 5: Build and run QML tests plus a development launch**

Run:

```powershell
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug -R "qml_main_window|library|playback_controller" --output-on-failure
build\debug\app\AgPlayer.exe
```

Expected: tests pass; the app opens without QML warnings; importing a file updates real metadata and starts waveform analysis; all visible controls work. Closing the development app is allowed. Do not run deployment or packaging tools.

- [ ] **Step 6: Commit**

```powershell
git add assets app CMakeLists.txt tests
git commit -m "feat(ui): build the functional main player"
```

---

### Task 12: Build the synchronized, pin-capable mini player

**Files:**
- Create: `app/qml/AgPlayer/MiniPlayerWindow.qml`
- Create: `app/qml/AgPlayer/components/MiniPlayerControls.qml`
- Create: `tests/qml/tst_mini_player.qml`
- Modify: `app/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces a second QML `Window` sharing the same controller/model singletons.
- Supports pin, restore-main, minimize, close, waveform seek, play/pause, previous/next, mode, favorite, mute, and volume.

- [ ] **Step 1: Write failing shared-state and window-action tests**

```qml
QtObject {
    id: playbackFake
    property int positionMs: 0
    property int durationMs: 0
    property int pauseCalls: 0
    property bool playing: true
    function publishPlaying(position, duration) { positionMs = position; durationMs = duration; playing = true }
    function togglePlayback() { if (playing) pauseCalls += 1; playing = !playing }
}
QtObject {
    id: windowController
    property bool alwaysOnTop: false
    property bool mainVisible: false
    property bool miniVisible: true
    function setAlwaysOnTop(value) { alwaysOnTop = value }
    function showMain() { mainVisible = true; miniVisible = false }
}

function test_mini_and_main_share_state() {
    playbackFake.publishPlaying(25000, 286000)
    compare(mainPlayer.positionMs, 25000)
    compare(miniPlayer.positionMs, 25000)
    mouseClick(miniPlayer.playPauseButton)
    compare(playbackFake.pauseCalls, 1)
}

function test_pin_and_restore_are_real_actions() {
    mouseClick(miniPlayer.pinButton)
    compare(windowController.alwaysOnTop, true)
    mouseClick(miniPlayer.restoreButton)
    compare(windowController.mainVisible, true)
    compare(windowController.miniVisible, false)
}
```

- [ ] **Step 2: Verify the mini window is absent**

Run: `cmake --build --preset windows-msvc-debug --target qml_mini_player_test`

Expected: missing component failure.

- [ ] **Step 3: Implement the reference mini-player layout**

Use the supplied mini-player screenshot as the source of truth: wide translucent rounded surface, Logo and cover area at left, favorite accent, metadata and badges in the center, RGB waveform, primary circular play button, transport/mode controls, volume at right, and pin/restore/minimize/close in the title area. The window is frameless and draggable through `startSystemMove`. Omit rating until the rated-library phase because it would otherwise be decorative. Blur is used only when supported; the fallback is an opaque `#0B111B` surface with a thin cool-gray border. Do not create a new controller, queue, or core handle. `MiniPlayerWindow` exposes `property var playback` and `property var windows`, defaulting to the production singletons; the QML test overrides only these two values with the explicit fakes above.

- [ ] **Step 4: Run state, focus, pin, close, and repeated-switch tests**

Run:

```powershell
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug -R "mini_player|window_controller|playback_controller" --output-on-failure
1..50 | ForEach-Object { ctest --preset windows-msvc-debug -R qml_mini_player --output-on-failure; if($LASTEXITCODE){break} }
```

Expected: one shared state source, no extra audio device, no QML warnings, and all 50 switch repetitions pass.

- [ ] **Step 5: Commit**

```powershell
git add app tests
git commit -m "feat(ui): add synchronized mini player"
```

---

### Task 13: Complete recoverable errors, cancellation, and clean shutdown

**Files:**
- Modify: `core/src/audio_engine.cpp`
- Modify: `core/src/core_context.cpp`
- Modify: `core/src/waveform_analyzer.cpp`
- Modify: `qt/src/import_controller.cpp`
- Modify: `qt/src/library_model.cpp`
- Modify: `qt/src/window_controller.cpp`
- Create: `qt/src/runtime_log.hpp`
- Create: `qt/src/runtime_log.cpp`
- Create: `tests/core/recovery_test.cpp`
- Create: `tests/qt/shutdown_test.cpp`
- Create: `tests/qt/runtime_log_test.cpp`
- Create: `tests/qml/tst_error_states.qml`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces idempotent cancellation and shutdown.
- Produces user-visible recoverable states for corrupt/unsupported/missing files, cache failure, and audio-device failure.

- [ ] **Step 1: Write failing recovery and shutdown tests**

```cpp
void ShutdownTest::closeDuringWaveformWriteLeavesValidState() {
    Harness app;
    app.startLongWaveformJob();
    app.requestClose();
    QVERIFY(app.waitForExit(3000));
    QVERIFY(!QFileInfo::exists(app.cachePath() + ".tmp"));
    QVERIFY(app.libraryJsonIsValid());
    QCOMPARE(app.runningWorkerCount(), 0);
}
```

Add core tests for device initialization failure, device loss callback, missing source after import, corrupt cache, decoder failure mid-track, and repeated destroy/cancel calls.

Add a Qt test that writes one mapped core error, reopens the log file, and confirms it contains timestamp, severity, component, result code, and UTF-8 detail without containing raw PCM or cover bytes.

- [ ] **Step 2: Run focused tests and confirm incomplete recovery behavior**

Run: `ctest --preset windows-msvc-debug -R "recovery|shutdown|error_states" --output-on-failure`

Expected: at least one test fails because coordinated shutdown and error presentation are incomplete.

- [ ] **Step 3: Implement one shutdown sequence and explicit error mapping**

The shutdown order is fixed: reject new UI commands; cancel imports and waveform jobs; stop the decode thread; stop and uninitialize miniaudio; atomically finish or discard pending store writes; release core handles; quit Qt. Make every step idempotent. Map `ag_result` to short Chinese user messages plus a technical detail for logs. Never retry a failed track indefinitely. Device loss changes state to Paused/Error and exposes a retry action instead of terminating the process.

`RuntimeLog` installs one Qt message handler that appends UTF-8 lines to `%LOCALAPPDATA%\AgPlayer\logs\agplayer.log`. At startup, if the file exceeds 2MB, replace a single `.old` copy before opening a new file. Protect writes with one mutex, but never call it from the miniaudio callback. No additional logging dependency is allowed.

- [ ] **Step 4: Run recovery tests repeatedly and inspect for residual processes/files**

Run:

```powershell
cmake --build --preset windows-msvc-debug
1..50 | ForEach-Object { ctest --preset windows-msvc-debug -R "recovery|shutdown" --output-on-failure; if($LASTEXITCODE){break} }
Get-Process AgPlayer -ErrorAction SilentlyContinue
Get-ChildItem build -Recurse -Filter *.tmp
ctest --preset windows-msvc-debug --output-on-failure
```

Expected: 50 repetitions pass, no `AgPlayer` process or stale `.tmp` file remains, and full regression passes.

- [ ] **Step 5: Commit**

```powershell
git add core qt app tests
git commit -m "fix: make playback errors and shutdown recoverable"
```

---

### Task 14: Validate all required formats, Gapless, Seek latency, memory, and real hardware playback

**Files:**
- Create: `tools/seek_benchmark.cpp`
- Create: `tools/memory_probe.ps1`
- Create: `tests/core/format_matrix_test.cpp`
- Create: `docs/qa/phase-1-audio-validation.md`
- Create: `tools/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces repeatable validation commands and a factual Phase 1 audio report.
- Uses local generated fixtures under a gitignored directory; no validation audio enters an eventual product package.

- [ ] **Step 1: Install a development-only FFmpeg CLI and generate the format matrix**

Install the CLI only for tests; it is not a runtime dependency and must not be copied beside the app.

```powershell
winget install --id Gyan.FFmpeg.Shared --exact --accept-package-agreements --accept-source-agreements
New-Item -ItemType Directory -Force tests\fixtures\generated | Out-Null
ffmpeg -y -f lavfi -i "sine=frequency=440:duration=4" -ac 2 -ar 44100 tests\fixtures\generated\source.wav
ffmpeg -y -i tests\fixtures\generated\source.wav tests\fixtures\generated\sample.mp3
ffmpeg -y -i tests\fixtures\generated\source.wav tests\fixtures\generated\sample.flac
ffmpeg -y -i tests\fixtures\generated\source.wav -c:a aac tests\fixtures\generated\sample.aac
ffmpeg -y -i tests\fixtures\generated\source.wav -c:a aac tests\fixtures\generated\sample.m4a
ffmpeg -y -i tests\fixtures\generated\source.wav -c:a libvorbis tests\fixtures\generated\sample.ogg
ffmpeg -y -i tests\fixtures\generated\source.wav -c:a libopus tests\fixtures\generated\sample.opus
ffmpeg -y -i tests\fixtures\generated\source.wav -c:a wmav2 tests\fixtures\generated\sample.wma
```

Expected: eight non-empty files (source WAV plus seven encoded variants) exist. Record `ffmpeg -version` in the validation report.

- [ ] **Step 2: Write the failing format and benchmark gates**

`format_matrix_test` iterates the explicit extensions `wav, mp3, flac, aac, m4a, ogg, opus, wma`, opens each through the C ABI, decodes at least 500ms, seeks to 2000ms, and confirms playback progresses with the null backend. It fails when any file is missing or skipped.

`seek_benchmark` performs 100 deterministic seeks after warm-up and prints JSON containing device/backend, file, samples, min, median, P95, and max. It exits nonzero when P95 is 20ms or more.

- [ ] **Step 3: Implement the memory probe and validation report schema**

```powershell
# tools/memory_probe.ps1
param([Parameter(Mandatory)][string]$Executable,
      [Parameter(Mandatory)][string]$AudioFile)
$process = Start-Process -PassThru -FilePath $Executable -ArgumentList @('--qa-play', $AudioFile)
try {
  Start-Sleep -Seconds 15
  $process.Refresh()
  $mb = [math]::Round($process.WorkingSet64 / 1MB, 2)
  [pscustomobject]@{ StableWorkingSetMB = $mb; LimitMB = 60; Passed = ($mb -lt 60) } |
    ConvertTo-Json
  if($mb -ge 60){ exit 1 }
} finally {
  if(!$process.HasExited){ $process.CloseMainWindow() | Out-Null; $process.WaitForExit(5000) | Out-Null }
}
```

The `--qa-play` switch is development-only behavior that opens the supplied file, begins playback, and otherwise uses the normal production path; it must not bypass controllers or fake state. `phase-1-audio-validation.md` records build type, machine, output device, files/formats, automated results, Seek JSON, working-set JSON, Gapless boundary measurement, and manual observations.

- [ ] **Step 4: Run automated format, Seek, Gapless, and memory gates**

Run:

```powershell
cmake --build --preset windows-msvc-release
ctest --preset windows-msvc-release -R "format_matrix|gapless" --output-on-failure
build\release\tools\seek_benchmark.exe tests\fixtures\generated\sample.flac
powershell -ExecutionPolicy Bypass -File tools\memory_probe.ps1 `
  -Executable build\release\app\AgPlayer.exe `
  -AudioFile tests\fixtures\generated\sample.flac
```

Expected: every format passes, Gapless has no measured pop, Seek P95 is below 20ms, and stable working set is below 60MB. Any failed gate triggers profiling/fixing and a full rerun; do not weaken thresholds.

- [ ] **Step 5: Perform real-device playback checks**

On the default Windows speaker/headphone device: play every generated format; drag Seek repeatedly; rapidly switch tracks; change volume/mute; switch main/mini 20 times; disconnect/reconnect the device if practical; run waveform analysis during playback; listen for pops, underruns, unexpected gain, or silence. Record the exact device and every result in `phase-1-audio-validation.md`.

- [ ] **Step 6: Commit**

```powershell
git add tools tests CMakeLists.txt docs/qa/phase-1-audio-validation.md
git commit -m "test: validate phase 1 audio quality and performance"
```

---

### Task 15: Run blocking native-app Design QA and the final Phase 1 self-audit

**Files:**
- Create: `design-qa.md`
- Create: `docs/qa/phase-1-final-report.md`
- Modify: `app/main.cpp` to add deterministic development-only screenshot arguments.
- Modify: QML/theme/assets files only when a recorded visual finding requires a fix.

**Interfaces:**
- Produces `design-qa.md` with exact `final result: passed` before handoff.
- Produces a final report containing test counts, formats, performance numbers, visual iterations, remaining P3 polish, and explicit confirmation that no packaging ran.

- [ ] **Step 1: Capture matching source and implementation evidence**

Add `--qa-screenshot-main <path>` and `--qa-screenshot-mini <path>` development arguments to `app/main.cpp`. After the normal production QML and controllers load, wait until real metadata and waveform data are ready, call `QQuickWindow::grabWindow()`, save the PNG, and exit through the normal shutdown path. The arguments must not substitute mock state.

Run the Release app with the generated demo track/state, at 1448×1086 and 100% Windows display scale:

```powershell
build\release\app\AgPlayer.exe --qa-play tests\fixtures\generated\sample.flac `
  --qa-screenshot-main docs\qa\screenshots\main-implementation-v1.png
build\release\app\AgPlayer.exe --qa-play tests\fixtures\generated\sample.flac `
  --qa-screenshot-mini docs\qa\screenshots\mini-implementation-v1.png
```

Preserve the original source paths and copy normalized comparison inputs into `docs/qa/screenshots/` only when needed for equal pixel dimensions. Record source pixels, captured pixels, QML logical window size, Windows display scale, and `QQuickWindow::devicePixelRatio()` in `design-qa.md`.

- [ ] **Step 2: Build true combined comparisons and write the first QA report**

Create combined side-by-side images containing source and implementation at equal scale. Review full views plus focused crops for player metadata, waveform/transport controls, track rows, title-bar icons, mini-player metadata, and volume controls. `design-qa.md` must explicitly cover fonts/typography, spacing/layout rhythm, colors/tokens, image quality/assets, copy/content, icons, states/interactions, responsiveness, and accessibility.

Use this terminal structure exactly:

```markdown
**Findings**
- [P1] Short issue title
  Location: component or region.
  Evidence: source does X; implementation does Y.
  Impact: user-visible consequence.
  Fix: exact QML/component/token change.

**Open Questions**
- None.

**Implementation Checklist**
- Ordered fixes.

**Follow-up Polish**
- Remaining P3 items only.

final result: blocked
```

- [ ] **Step 3: Fix and recapture until no P0/P1/P2 remains**

For each iteration: apply only the recorded fixes, rebuild, capture the same viewport/state, create a new combined comparison, and append comparison history with earlier findings, changes, and post-fix evidence. Do not pass based on separate image views or code inspection. P3 items may remain documented.

- [ ] **Step 4: Run the complete automated and manual self-audit**

Run:

```powershell
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug --output-on-failure
cmake --build --preset windows-msvc-release
ctest --preset windows-msvc-release --output-on-failure
git diff --check
$forbidden = @('TO' + 'DO', 'TB' + 'D', 'dummy data', 'windeployqt', 'cpack', 'CPack', 'installer') -join '|'
rg -n $forbidden --glob '!docs/superpowers/**' .
Get-Process AgPlayer -ErrorAction SilentlyContinue
```

Expected: Debug and Release tests all pass, no warnings, no whitespace errors, no dummy product data or packaging path, and no residual process. Review every visible control manually once more.

- [ ] **Step 5: Finalize the reports and commit**

`design-qa.md` must end with exactly `final result: passed`. `phase-1-final-report.md` must state: test count, required format matrix result, real device, Seek P95, stable working set, Gapless result, visual comparison history, known P3 items, and `EXE packaging performed: no`.

```powershell
git add design-qa.md docs/qa app qt core tests tools assets
git commit -m "test: complete phase 1 quality gate"
git status --short
```

Expected: clean working tree. Stop after reporting Phase 1 results; do not begin Phase 2 and do not package an EXE without a new explicit user instruction.
