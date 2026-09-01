# Hidden Basic Video Playback Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add hidden, local, basic video playback compatibility to AgPlayer while preserving its audio-first product surface and using the existing playback session as the single transport and clock authority.

**Architecture:** Keep the existing FFmpeg audio decoder, miniaudio output, `PlaybackController`, queue, and QML transport as the authoritative playback path. Add media-track classification, a playback-only silent clock for video-only files, an on-demand FFmpeg video decoder, a bounded frame queue, and one Qt Quick Scene Graph item. Load the video view only while the current item contains a real video stream, and synchronously cancel/release all video-side resources when leaving it.

**Tech Stack:** C++17, FFmpeg (`avformat`, `avcodec`, `avutil`, `swresample`, `swscale`), AgPlayer C ABI, Qt 6 Core/Quick/QML/Test, QML, CMake/vcpkg, PowerShell contract tests.

**Spec:** `docs/superpowers/specs/2026-09-01-hidden-basic-video-playback-design.md`

## Global Constraints

- Read `superpowers:test-driven-development/references/writing-good-tests.md` before editing the first test.
- Preserve the current dirty worktree. Stage and commit only files owned by the current task; never revert concurrent edits.
- Do not add Qt Multimedia, VLC, mpv, a second playback state machine, a video library, a video settings page, subtitles, networking, hardware decoding, editing, filters, quality selection, or HDR handling.
- Keep directory scanning and library browsing audio-only. A video enters only through a direct file path: drag/drop, existing playlist insertion, startup/file association, or explicit import of that file.
- `PlaybackController` remains the only queue, state, position, volume, speed, repeat, previous, and next authority. The video controller may observe or request decode work; it must not mirror those states.
- Pure-audio playback must create zero video decoder threads, frame buffers, textures, or video timers.
- Any production-code task follows RED → minimal GREEN → focused regression → diff review → task-scoped commit. If a RED test passes unexpectedly, stop and fix the test before implementation.
- Do not package an EXE in this plan. Real visual, interaction, audio-device, and resource-release evidence must precede any later packaging decision.

---

## Task 1: Add deterministic video fixtures and media-track classification

**Acceptance links:** V01, V02, V03, V04, V12

**Files:**

- Create: `tools/video_fixture_generator.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `core/src/decoder.hpp`
- Modify: `core/src/decoder.cpp`
- Modify: `core/include/agplayer/c_api.h`
- Modify: `core/src/c_api.cpp`
- Modify: `tests/core/decoder_test.cpp`

- [ ] **Step 1: Add a deterministic, encoder-independent fixture generator.**

  Create a small FFmpeg-based tool that muxes raw BGRA video and PCM S16LE audio instead of depending on an external `ffmpeg.exe` or optional encoders. It must emit:

  - `video-with-audio.avi`: 320×180, 30 fps, 2 seconds, raw video + stereo PCM;
  - `video-only.avi`: same video, no audio stream;
  - `audio-with-attached-picture.mka`: PCM audio plus one embedded, static PNG packet marked `AV_DISPOSITION_ATTACHED_PIC`; keep the encoded 1×1 PNG bytes in the generator so no image encoder is required;
  - `rotated-video.mov`: a short video with 90-degree display-matrix rotation metadata.

  The generated pixels should change by frame index, allowing tests to prove that different frames were decoded.

  In `tests/CMakeLists.txt`, link `video_fixture_generator` to the same FFmpeg libraries as the core, generate all four outputs under `${CMAKE_CURRENT_BINARY_DIR}/fixtures/video`, expose a `video_fixtures` custom target, make relevant tests depend on it, and pass the four absolute fixture paths as test arguments.

- [ ] **Step 2: Write failing metadata tests.**

  Extend `decoder_test.cpp` with C++ and public C ABI assertions equivalent to:

  ```cpp
  MediaMetadata av;
  assert(probe_media_metadata(videoWithAudio, av) == AG_OK);
  assert(av.has_audio);
  assert(av.has_video);
  assert(av.video_width == 320);
  assert(av.video_height == 180);

  MediaMetadata videoOnly;
  assert(probe_media_metadata(videoOnlyPath, videoOnly) == AG_OK);
  assert(!videoOnly.has_audio);
  assert(videoOnly.has_video);

  MediaMetadata coverOnly;
  assert(probe_media_metadata(attachedPicturePath, coverOnly) == AG_OK);
  assert(coverOnly.has_audio);
  assert(!coverOnly.has_video);
  ```

  Add C ABI assertions for the same values:

  ```cpp
  assert(ag_metadata_has_audio(metadata) == 1);
  assert(ag_metadata_has_video(metadata) == 1);
  assert(ag_metadata_video_width(metadata) == 320);
  assert(ag_metadata_video_height(metadata) == 180);
  ```

- [ ] **Step 3: Run RED.**

  ```powershell
  cmake --build --preset windows-msvc-release --target video_fixture_generator decoder_test
  ctest --test-dir build/release -C Release -R "^decoder_test$" --output-on-failure
  ```

  Expected: compilation fails because media-kind fields and C ABI accessors do not exist.

- [ ] **Step 4: Implement stream classification once in the core metadata probe.**

  Extend `MediaMetadata` with stable defaults:

  ```cpp
  bool has_audio = false;
  bool has_video = false;
  int video_width = 0;
  int video_height = 0;
  ```

  During `avformat_find_stream_info`, mark any audio stream as audio. Mark a video stream as real video only when:

  ```cpp
  stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO
      && (stream->disposition & AV_DISPOSITION_ATTACHED_PIC) == 0
  ```

  Record coded width/height from the first real video stream. Metadata probing succeeds when either a real audio or real video stream exists; it returns `AG_UNSUPPORTED_FORMAT` only when neither exists. Do not alter the strict audio decoder behavior in this task.

  Add null-safe C ABI accessors returning `0` for a null handle:

  ```c
  int ag_metadata_has_audio(const ag_metadata* metadata);
  int ag_metadata_has_video(const ag_metadata* metadata);
  int ag_metadata_video_width(const ag_metadata* metadata);
  int ag_metadata_video_height(const ag_metadata* metadata);
  ```

- [ ] **Step 5: Run GREEN and focused regressions.**

  ```powershell
  cmake --build --preset windows-msvc-release --target video_fixture_generator decoder_test transcoder_test metadata_writer_test
  ctest --test-dir build/release -C Release -R "^(decoder_test|transcoder_test|metadata_writer_test)$" --output-on-failure
  ```

- [ ] **Step 6: Review and commit.**

  Confirm attached pictures never set `has_video`, no decoder behavior changed, and generated fixtures stay in the build tree. Then commit only Task 1 files:

  ```powershell
  git add -- tools/video_fixture_generator.cpp tests/CMakeLists.txt core/src/decoder.hpp core/src/decoder.cpp core/include/agplayer/c_api.h core/src/c_api.cpp tests/core/decoder_test.cpp
  git commit -m "test: classify audio and video media streams"
  ```

---

## Task 2: Persist and import hidden media-kind metadata

**Acceptance links:** V01, V02, V12, V15

**Files:**

- Modify: `qt/src/library_model.hpp`
- Modify: `qt/src/library_model.cpp`
- Modify: `qt/src/library_store.cpp`
- Modify: `qt/src/import_controller.cpp`
- Modify: `tests/qt/library_model_test.cpp`
- Modify: `tests/qt/library_store_test.cpp`
- Modify: `tests/qt/import_controller_test.cpp`

- [ ] **Step 1: Write failing model, persistence, and import tests.**

  Add `TrackRecord::hasAudio` and `TrackRecord::hasVideo` expectations for three fixture classes. Verify new records round-trip through JSON and old JSON without the keys loads both values as `false`. Verify direct import of `video-only.avi` succeeds, while folder discovery remains audio-only.

  Add QML roles:

  ```cpp
  QCOMPARE(model.data(index, LibraryModel::HasAudioRole).toBool(), true);
  QCOMPARE(model.data(index, LibraryModel::HasVideoRole).toBool(), true);
  ```

- [ ] **Step 2: Run RED.**

  ```powershell
  cmake --build --preset windows-msvc-release --target library_model_test library_store_test import_controller_test
  ctest --test-dir build/release -C Release -R "^(library_model_test|library_store_test|import_controller_test)$" --output-on-failure
  ```

  Expected: compilation fails for missing fields/roles.

- [ ] **Step 3: Implement minimal persistence and import mapping.**

  Add:

  ```cpp
  bool hasAudio = false;
  bool hasVideo = false;
  ```

  to `TrackRecord`, add `HasAudioRole`/`HasVideoRole`, expose the roles as `hasAudio`/`hasVideo`, serialize as JSON booleans, and default missing keys to `false`. Map `ag_metadata_has_audio/video()` in `ImportController::probeMetadata`.

  Do not add video entries to library navigation, filters, folder expansion, or settings. Existing direct-path import stays the only import change.

- [ ] **Step 4: Run GREEN and focused regressions.**

  ```powershell
  cmake --build --preset windows-msvc-release --target library_model_test library_store_test import_controller_test audio_file_discovery_test
  ctest --test-dir build/release -C Release -R "^(library_model_test|library_store_test|import_controller_test|audio_file_discovery_test)$" --output-on-failure
  ```

- [ ] **Step 5: Review and commit.**

  Confirm old library JSON remains loadable and folder discovery did not gain video extensions.

  ```powershell
  git add -- qt/src/library_model.hpp qt/src/library_model.cpp qt/src/library_store.cpp qt/src/import_controller.cpp tests/qt/library_model_test.cpp tests/qt/library_store_test.cpp tests/qt/import_controller_test.cpp
  git commit -m "feat: retain hidden media stream kind"
  ```

---

## Task 3: Add a playback-only silent clock for video-only files

**Acceptance links:** V03, V05, V08, V11

**Files:**

- Modify: `core/src/decoder.hpp`
- Modify: `core/src/decoder.cpp`
- Modify: `core/src/audio_engine.cpp`
- Modify: `tests/core/decoder_test.cpp`
- Modify: `tests/core/audio_engine_test.cpp`

- [ ] **Step 1: Write failing strict-versus-playback tests.**

  Tests must prove both sides of the boundary:

  ```cpp
  Decoder strict;
  assert(strict.open(videoOnlyPath) == AG_UNSUPPORTED_FORMAT);

  Decoder playback;
  DecoderOpenOptions options;
  options.allow_silent_video_clock = true;
  assert(playback.open(videoOnlyPath, options) == AG_OK);
  assert(playback.output_format().sample_rate == 48000);
  assert(playback.output_format().channels == 2);
  ```

  Read blocks and verify samples are zeros, timestamps increase, EOS occurs at the container duration, and seek resumes within one output block of the target. Add an audio-engine test proving the normal player can load/play/seek the video-only fixture while converter/analyzer-style strict callers still reject it.

- [ ] **Step 2: Run RED.**

  ```powershell
  cmake --build --preset windows-msvc-release --target decoder_test audio_engine_test
  ctest --test-dir build/release -C Release -R "^(decoder_test|audio_engine_test)$" --output-on-failure
  ```

  Expected: compilation fails for `allow_silent_video_clock`.

- [ ] **Step 3: Implement the silent source inside `Decoder`, gated by the option.**

  Add:

  ```cpp
  bool allow_silent_video_clock = false;
  ```

  to `DecoderOpenOptions`. If no audio stream exists, the option is true, and a real video stream exists, initialize a finite 48 kHz stereo float source based on container/stream duration. `read()` returns zero-filled blocks with monotonically increasing `timestamp_frame`/`timestamp_ms`; `seek()` and `seekFrame()` update the silent cursor; `close()` clears the mode. All default callers remain strict.

  Set the option only in `PlaybackDecoder::open_interruptible()`. Do not set it in converters, analyzers, waveform extraction, metadata writing, or audio editor paths.

- [ ] **Step 4: Run GREEN and regression tests.**

  ```powershell
  cmake --build --preset windows-msvc-release --target decoder_test audio_engine_test transcoder_test waveform_analyzer_test audio_source_probe_test
  ctest --test-dir build/release -C Release -R "^(decoder_test|audio_engine_test|transcoder_test|waveform_analyzer_test|audio_source_probe_test)$" --output-on-failure
  ```

- [ ] **Step 5: Review and commit.**

  Check duration overflow, zero/unknown duration rejection, seek clamping, and that the default option preserves every non-playback behavior.

  ```powershell
  git add -- core/src/decoder.hpp core/src/decoder.cpp core/src/audio_engine.cpp tests/core/decoder_test.cpp tests/core/audio_engine_test.cpp
  git commit -m "feat: clock video-only playback silently"
  ```

---

## Task 4: Add the cancellable FFmpeg video decoder C ABI

**Acceptance links:** V02, V04, V06, V07, V10, V14

**Files:**

- Create: `core/src/video_decoder.hpp`
- Create: `core/src/video_decoder.cpp`
- Modify: `core/CMakeLists.txt`
- Modify: `core/include/agplayer/c_api.h`
- Modify: `core/src/c_api.cpp`
- Modify: `vcpkg.json`
- Create: `tests/core/video_decoder_test.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `tests/scripts/runtime_deployment_test.ps1`

- [ ] **Step 1: Define tests against a small opaque C contract.**

  Add tests for open/read/EOF, seek, pixel content changes, display geometry, rotation, cancellation, null arguments, unsupported files, and repeated create/open/close cycles. The public contract must contain no FFmpeg or Qt types:

  ```c
  typedef struct ag_video_decoder ag_video_decoder;

  typedef struct ag_video_frame {
      uint32_t struct_size;
      const unsigned char* data;
      size_t data_size;
      int width;
      int height;
      int stride;
      int pixel_format;       /* AG_VIDEO_PIXEL_FORMAT_BGRA8 */
      int64_t pts_ms;
      int sar_num;
      int sar_den;
      int rotation_degrees;   /* normalized: 0, 90, 180, 270 */
      int end_of_stream;
  } ag_video_frame;

  ag_result ag_video_decoder_create(ag_video_decoder** out_decoder);
  ag_result ag_video_decoder_open(ag_video_decoder* decoder, const char* utf8_path);
  ag_result ag_video_decoder_read(ag_video_decoder* decoder, ag_video_frame* out_frame);
  ag_result ag_video_decoder_seek(ag_video_decoder* decoder, int64_t position_ms);
  void ag_video_decoder_cancel(ag_video_decoder* decoder);
  void ag_video_decoder_close(ag_video_decoder* decoder);
  void ag_video_decoder_destroy(ag_video_decoder* decoder);
  ```

  Document that frame data remains valid only until the next read/seek/close/destroy call on that decoder.

- [ ] **Step 2: Run RED.**

  ```powershell
  cmake --build --preset windows-msvc-release --target video_decoder_test
  ```

  Expected: compilation fails because the decoder contract does not exist.

- [ ] **Step 3: Implement the core decoder and scaling path.**

  Use `avformat_open_input`, `avformat_find_stream_info`, `av_find_best_stream`, `avcodec_send_packet`/`receive_frame`, `best_effort_timestamp`, and `sws_scale` to produce tightly owned BGRA8 storage. Exclude `AV_DISPOSITION_ATTACHED_PIC`. Read sample aspect ratio and display-matrix rotation, and normalize rotation without physically rotating every decoded frame.

  `ag_video_decoder_cancel()` sets an atomic flag that the FFmpeg interrupt callback observes. `open`, packet reads, decode loops, and seek must return a stable cancelled result without leaking contexts. `close()` is idempotent and resets cancellation so the handle can be reopened.

- [ ] **Step 4: Add `swscale` explicitly and validate runtime deployment.**

  Add the vcpkg FFmpeg `swscale` feature. Extend the runtime DLL contract for the bundled FFmpeg major version (currently `swscale-9.dll`) without weakening existing DLL checks.

- [ ] **Step 5: Run GREEN and sanitizer-like lifecycle loops available on Windows.**

  ```powershell
  cmake --preset windows-msvc-release
  cmake --build --preset windows-msvc-release --target video_decoder_test AgPlayer
  ctest --test-dir build/release -C Release -R "^(video_decoder_test|runtime_deployment_test)$" --output-on-failure
  ```

  Run the repeated lifecycle data row at least 100 times inside the test process.

- [ ] **Step 6: Review and commit.**

  Check integer/stride overflow, `struct_size`, EOF semantics, timestamp rescaling, rotation signs, cancellation races, and cleanup on every failed open stage.

  ```powershell
  git add -- core/src/video_decoder.hpp core/src/video_decoder.cpp core/CMakeLists.txt core/include/agplayer/c_api.h core/src/c_api.cpp vcpkg.json tests/core/video_decoder_test.cpp tests/CMakeLists.txt tests/scripts/runtime_deployment_test.ps1
  git commit -m "feat: expose cancellable FFmpeg video decoding"
  ```

---

## Task 5: Synchronize bounded video decoding to the existing playback session

**Acceptance links:** V02, V05, V06, V07, V08, V10, V11, V13, V14

**Files:**

- Create: `qt/src/video_playback_controller.hpp`
- Create: `qt/src/video_playback_controller.cpp`
- Modify: `qt/src/playback_controller.hpp`
- Modify: `qt/src/playback_controller.cpp`
- Modify: `qt/src/library_model.hpp`
- Modify: `qt/CMakeLists.txt`
- Modify: `qt/src/qml_registration.hpp`
- Modify: `qt/src/qml_registration.cpp`
- Modify: `app/main.cpp`
- Create: `tests/qt/video_playback_controller_test.cpp`
- Modify: `tests/qt/playback_controller_test.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Add a successful-seek signal test to `PlaybackController`.**

  Add `seekCommitted(qint64 positionMs)` and prove it emits only after `ag_player_seek()` returns `AG_OK`; failed seeks emit nothing. This signal is synchronization evidence, not a second seek command.

- [ ] **Step 2: Write failing `VideoPlaybackController` tests with diagnostics.**

  The controller API should expose only visual/resource state:

  ```cpp
  Q_PROPERTY(bool visible READ visible NOTIFY visibleChanged)
  Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
  Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
  Q_PROPERTY(bool workerRunning READ workerRunning NOTIFY diagnosticsChanged)
  Q_PROPERTY(int queuedFrameCount READ queuedFrameCount NOTIFY diagnosticsChanged)
  Q_PROPERTY(qint64 queuedFrameBytes READ queuedFrameBytes NOTIFY diagnosticsChanged)
  Q_PROPERTY(quint64 frameSerial READ frameSerial NOTIFY frameChanged)
  ```

  Tests must prove:

  - audio-only current items never start the worker;
  - video items start one worker and become visible;
  - queue depth never exceeds 3 and memory never exceeds 64 MiB;
  - pause fills at most the bound then stops decoding;
  - play, seek, speed ratio, previous, next, and track switches follow `PlaybackController`;
  - old video records with `hasVideo == false` get one bounded asynchronous re-probe based on a supported video suffix and are not re-probed repeatedly;
  - stop/return cancels, joins, clears frames, and reports `workerRunning == false` within 500 ms under normal local-file conditions and always before the 2 s acceptance timeout;
  - destruction and 50 repeated video/audio switches leave zero workers and zero queued bytes.

- [ ] **Step 3: Run RED.**

  ```powershell
  cmake --build --preset windows-msvc-release --target playback_controller_test video_playback_controller_test
  ctest --test-dir build/release -C Release -R "^(playback_controller_test|video_playback_controller_test)$" --output-on-failure
  ```

  Expected: compilation fails for the missing signal/controller.

- [ ] **Step 4: Implement one on-demand worker and a bounded queue.**

  `VideoPlaybackController` owns the worker lifecycle but observes `PlaybackController::currentTrackId`, `state`, `positionMs`, `speedRatio`, and `seekCommitted`. Use one C++ worker thread only while a real video is current. The worker owns the `ag_video_decoder`; cancellation is requested through the C ABI before joining.

  Keep a mutex-protected queue of at most 3 immutable frame snapshots and 64 MiB total. The worker blocks on a condition variable when full. Position updates select the newest frame whose PTS is not later than the current audio clock tolerance, discard older late frames, and wake the worker. A committed seek increments a generation, clears the queue, requests decoder seek, and rejects any old-generation frame.

  Use `PlaybackController`'s position polling; do not add a 16 ms video timer. When paused, the bounded queue may decode ahead only until full. On stop, audio-only switch, video switch, open failure, return, or destruction: cancel → wake → join → clear queue → clear displayed snapshot → set invisible.

- [ ] **Step 5: Register one runtime singleton without duplicating playback state.**

  Add `VideoPlaybackController* videoPlaybackController = nullptr;` to `AgPlayerQmlRuntimeModels`. Construct the controller in `app/main.cpp` after `LibraryModel` and `PlaybackController`, pass it to QML registration, and register it as `VideoPlaybackController`. Test harnesses may pass a test instance; they must not get a hidden production worker.

- [ ] **Step 6: Run GREEN and focused regressions.**

  ```powershell
  cmake --build --preset windows-msvc-release --target playback_controller_test video_playback_controller_test
  ctest --test-dir build/release -C Release -R "^(playback_controller_test|video_playback_controller_test)$" --output-on-failure
  ```

- [ ] **Step 7: Review and commit.**

  Check ownership and lock ordering, GUI-thread blocking, stale-generation delivery, cancellation races, all exit paths, and pure-audio zero-allocation/thread evidence.

  ```powershell
  git add -- qt/src/video_playback_controller.hpp qt/src/video_playback_controller.cpp qt/src/playback_controller.hpp qt/src/playback_controller.cpp qt/src/library_model.hpp qt/CMakeLists.txt qt/src/qml_registration.hpp qt/src/qml_registration.cpp app/main.cpp tests/qt/video_playback_controller_test.cpp tests/qt/playback_controller_test.cpp tests/CMakeLists.txt
  git commit -m "feat: synchronize video frames to audio playback"
  ```

---

## Task 6: Render frames through one Qt Quick Scene Graph item

**Acceptance links:** V02, V04, V06, V10, V14

**Files:**

- Create: `qt/src/video_frame_item.hpp`
- Create: `qt/src/video_frame_item.cpp`
- Modify: `qt/CMakeLists.txt`
- Modify: `qt/src/qml_registration.cpp`
- Create: `tests/qt/video_frame_item_test.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write failing item lifecycle and geometry tests.**

  Instantiate the item in a `QQuickWindow`, attach a test `VideoPlaybackController`, present known frames, and verify update requests, letterbox geometry, sample-aspect-ratio correction, 90/180/270-degree rotation, controller detachment, window invalidation, and `releaseResources()` behavior. A test-only counter may expose live texture-node state in Debug/test builds; do not add a production QML diagnostic property.

- [ ] **Step 2: Run RED.**

  ```powershell
  cmake --build --preset windows-msvc-release --target video_frame_item_test
  ```

  Expected: compilation fails for the missing item.

- [ ] **Step 3: Implement the Scene Graph boundary.**

  Add a `QQuickItem` with:

  ```cpp
  Q_PROPERTY(VideoPlaybackController* controller READ controller WRITE setController NOTIFY controllerChanged)
  QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) override;
  void releaseResources() override;
  ```

  The GUI thread only receives immutable CPU frame snapshots and calls `update()`. `updatePaintNode()` runs on the render thread, creates/replaces the `QSGTexture` there, and uses `QSGSimpleTextureNode` for aspect-fit rendering. Never access `QSGTexture` from the decoder or GUI thread. Use BGRA-compatible `QImage::Format_ARGB32` with explicit stride and a deep copy at the C ABI boundary.

- [ ] **Step 4: Run GREEN and focused Quick regressions.**

  ```powershell
  cmake --build --preset windows-msvc-release --target video_frame_item_test waveform_item_test track_waveform_thumbnail_item_test
  ctest --test-dir build/release -C Release -R "^(video_frame_item_test|waveform_item_test|track_waveform_thumbnail_item_test)$" --output-on-failure
  ```

- [ ] **Step 5: Review and commit.**

  Verify render-thread-only texture ownership, texture deletion on scene invalidation/controller switch, no per-frame QML object creation, and correct geometry under HiDPI.

  ```powershell
  git add -- qt/src/video_frame_item.hpp qt/src/video_frame_item.cpp qt/CMakeLists.txt qt/src/qml_registration.cpp tests/qt/video_frame_item_test.cpp tests/CMakeLists.txt
  git commit -m "feat: render video frames with Qt Scene Graph"
  ```

---

## Task 7: Add the hidden video view and basic shared transport

**Acceptance links:** V01, V02, V05, V06, V07, V08, V09, V11, V13, V15

**Files:**

- Create: `app/qml/AgPlayer/components/VideoPlaybackView.qml`
- Create: `app/qml/AgPlayer/components/VideoTransportBar.qml`
- Modify: `app/qml/AgPlayer/components/TransportControls.qml`
- Modify: `app/qml/AgPlayer/Main.qml`
- Modify: `app/CMakeLists.txt`
- Create: `tests/qml/tst_video_playback_view.qml`
- Modify: `tests/CMakeLists.txt`
- Modify: `tests/qml/tst_main_window.qml`

- [ ] **Step 1: Write failing QML contract tests.**

  Test object names and user-visible behavior, not implementation strings:

  - the top-level `Loader` is inactive for audio and active for video;
  - the view contains `VideoFrameItem`, previous, play/pause, next, seek, current/total time, volume/mute, speed, fullscreen, and return controls;
  - it contains no video navigation, video library, quality, filter, subtitle, URL, open-video, or playback-mode control;
  - return requests `PlaybackController.stop()`, exits fullscreen, and restores the previous audio UI;
  - Escape exits fullscreen only; when already windowed it does not stop playback;
  - switching video → audio unloads the view;
  - loading and error states remain inside the same minimal surface.

- [ ] **Step 2: Run RED.**

  ```powershell
  cmake --build --preset windows-msvc-release --target qml_main_window_test
  ctest --test-dir build/release -C Release -R "^(qml_video_playback_view_test|qml_main_window_test)$" --output-on-failure
  ```

  Expected: QML type/files are missing.

- [ ] **Step 3: Make `TransportControls` reusable without exposing audio/video extras.**

  Add default-compatible flags:

  ```qml
  property bool showEqualizer: true
  property bool showWaveformMode: true
  property bool showPlaybackMode: true
  ```

  Bind only the corresponding buttons' `visible` properties. Existing callers keep all prior behavior; the video bar sets all three to `false` and reuses previous/play-pause/next.

- [ ] **Step 4: Implement a restrained video surface.**

  `VideoPlaybackView.qml` is a borderless, opaque-black frame area with a single lower control strip using existing `Theme` tokens. `VideoTransportBar.qml` binds directly to `PlaybackController`:

  - seek slider: `from: 0`, `to: Math.max(1, PlaybackController.durationMs)`, commit via `PlaybackController.seek(value)`;
  - time labels: existing application time formatter or one small shared formatter function;
  - volume: reuse `PlayerVolumeControl`;
  - speed: a compact control constrained to the existing supported range 0.75×–1.50× and calling `setSpeedRatio()`;
  - fullscreen and return: emit requests to `Main.qml`.

  In `Main.qml`, place one top-level `Loader` above all player shells:

  ```qml
  Loader {
      id: videoPlaybackLoader
      anchors.fill: parent
      z: 1000
      active: VideoPlaybackController.visible
      sourceComponent: VideoPlaybackView {
          onFullscreenRequested: root.enterVideoFullscreen()
          onReturnRequested: root.leaveVideoPlayback()
      }
  }
  ```

  `leaveVideoPlayback()` first exits fullscreen if necessary, then calls `PlaybackController.stop()`. Use the same window's `showFullScreen()`/restore path; do not create a second window. Keep the original audio UI loaded underneath so it returns without rebuilding state.

- [ ] **Step 5: Run GREEN, QML lint, and existing shell regressions.**

  ```powershell
  cmake --build --preset windows-msvc-release --target qml_main_window_test all_qmllint
  ctest --test-dir build/release -C Release -R "^(qml_video_playback_view_test|qml_main_window_test|qml_player_controls_layout_test|qml_integrated_theme_test|qml_rolling_theme_test)$" --output-on-failure
  ```

- [ ] **Step 6: Review and commit around concurrent QML edits.**

  Re-open the current `Main.qml`, `TransportControls.qml`, and `app/CMakeLists.txt` before applying/staging. Preserve every unrelated user edit. Review small/large window layouts, keyboard focus, accessible names, and ensure there is no second design system.

  ```powershell
  git add -p -- app/qml/AgPlayer/Main.qml app/qml/AgPlayer/components/TransportControls.qml app/CMakeLists.txt
  git add -- app/qml/AgPlayer/components/VideoPlaybackView.qml app/qml/AgPlayer/components/VideoTransportBar.qml tests/qml/tst_video_playback_view.qml tests/CMakeLists.txt tests/qml/tst_main_window.qml
  git diff --cached --check
  git commit -m "feat: show hidden basic video playback view"
  ```

---

## Task 8: Extend hidden direct-path and Windows association compatibility

**Acceptance links:** V01, V09, V12, V15

**Files:**

- Modify: `qt/src/audio_file_discovery.hpp`
- Modify: `qt/src/audio_file_discovery.cpp`
- Modify: `qt/src/file_association_controller.cpp`
- Modify: `qt/src/settings_controller.cpp`
- Create: `tests/qt/audio_file_discovery_test.cpp`
- Modify: `tests/qt/file_association_controller_test.cpp`
- Modify: `tests/qt/settings_controller_test.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write failing extension and registry tests.**

  Add a separate `supportedVideoExtensions()` list containing exactly:

  ```text
  mp4, mkv, webm, mov, avi, m4v
  ```

  Tests must prove it is distinct from `supportedAudioExtensions()`, directory audio discovery still ignores these extensions, `unregisterAll()` cleans both groups, and enabling the existing “set as default player” flow registers video capabilities without adding visible per-video toggles. Disabling it removes them.

- [ ] **Step 2: Run RED.**

  ```powershell
  cmake --build --preset windows-msvc-release --target audio_file_discovery_test file_association_controller_test settings_controller_test
  ctest --test-dir build/release -C Release -R "^(audio_file_discovery_test|file_association_controller_test|settings_controller_test)$" --output-on-failure
  ```

- [ ] **Step 3: Implement the separate compatibility list.**

  Add `supportedVideoExtensions()` and `isSupportedVideoExtension()` beside the existing audio helpers, but do not feed them into directory enumeration. When the existing default-player setting is enabled, append the video list only for Windows capability/association registration. Do not add a video settings row or change `fileAssociations`' audio choices.

- [ ] **Step 4: Run GREEN and startup/import regressions.**

  ```powershell
  cmake --build --preset windows-msvc-release --target audio_file_discovery_test file_association_controller_test settings_controller_test import_controller_test single_instance_manager_test
  ctest --test-dir build/release -C Release -R "^(audio_file_discovery_test|file_association_controller_test|settings_controller_test|import_controller_test|single_instance_manager_test)$" --output-on-failure
  ```

- [ ] **Step 5: Review and commit.**

  Check case-insensitive suffix handling, shell quoting for paths with spaces/non-ASCII characters, cleanup of stale ProgIDs, and no automatic opt-in.

  ```powershell
  git add -- qt/src/audio_file_discovery.hpp qt/src/audio_file_discovery.cpp qt/src/file_association_controller.cpp qt/src/settings_controller.cpp tests/qt/audio_file_discovery_test.cpp tests/qt/file_association_controller_test.cpp tests/qt/settings_controller_test.cpp tests/CMakeLists.txt
  git commit -m "feat: register hidden video file compatibility"
  ```

---

## Task 9: Perform integrated, visual, performance, and lifecycle acceptance

**Acceptance links:** V01–V15

**Files:**

- Modify: `app/main.cpp` only if a minimal existing QA launch hook needs a video/fullscreen option
- Create: `tests/scripts/video_playback_acceptance.ps1`
- Modify: `docs/development/2026-09-01-hidden-basic-video-playback.md`

- [ ] **Step 1: Add a fail-fast acceptance harness, not a fake playback test.**

  The PowerShell harness accepts explicit real-media paths for MP4/H.264/AAC, MKV, WebM, MOV, AVI, M4V, and video-only media. It validates file existence, launches the Release app through the normal startup path, records exit code/log path, and fails if required samples are absent. It must not generate “success” from metadata probing alone.

  If the existing QA launch mechanism cannot request fullscreen deterministically, add only these hidden test flags to `app/main.cpp`:

  ```text
  --qa-play <path>
  --qa-video-fullscreen
  --qa-exit-after-ms <milliseconds>
  ```

  They must call the same import/play/fullscreen functions as users and remain undocumented in the visible UI.

- [ ] **Step 2: Build and run all automated checks.**

  ```powershell
  cmake --preset windows-msvc-release
  cmake --build --preset windows-msvc-release
  cmake --build --preset windows-msvc-release --target all_qmllint
  ctest --test-dir build/release -C Release --output-on-failure
  git diff --check
  ```

  Record exact pass/fail counts. Do not reinterpret a focused or Release-only pass as broader coverage.

- [ ] **Step 3: Run the real-media behavior matrix.**

  For each supplied real sample, verify launch/import, visible moving picture, audible audio when present, correct duration, pause/resume, seek, previous/next, volume/mute, 0.75×/1.0×/1.5× speed, fullscreen enter/exit, Escape, return, end-of-file, and video → audio restoration. Include at least one Unicode path and one path containing spaces.

  Run the Windows checks separately:

  - file association disabled: video double-click association is absent;
  - enabled: double-click forwards the path and playback starts;
  - single video drag/drop into the player and into the existing playlist;
  - folder import containing videos does not create a video library.

- [ ] **Step 4: Capture visual evidence.**

  Capture at minimum:

  - windowed 16:9 video with controls visible;
  - fullscreen video;
  - portrait/rotated video showing correct aspect;
  - paused state;
  - restored audio UI after return;
  - narrow and large window layouts.

  Compare alignment, typography, spacing, control states, theme consistency, letterboxing, and absence of a Qt-demo/second-player appearance. Correct any material issue through a new RED/GREEN focused cycle before continuing.

- [ ] **Step 5: Measure lightweight/resource acceptance.**

  Record diagnostics or profiler evidence for:

  - pure-audio playback: zero video workers, zero queued video frames/bytes, no video texture item loaded, no video timer;
  - video playback: one worker, queue ≤3 frames and ≤64 MiB;
  - normal 1080p30 SDR Release playback on the current machine: stable motion and acceptable CPU/GPU behavior;
  - return/stop/switch: worker and queue reach zero within 500 ms normally and always before 2 s;
  - 50 repeated audio/video switches: no growth in live workers, queued bytes, handles, or GPU resources.

  4K/60 and hardware decode are observations only, not completion gates.

- [ ] **Step 6: Perform final diff review and independent code review.**

  Review the full feature range from the docs commit to HEAD for correctness, regression, lifecycle, performance, dependency weight, public ABI safety, hidden product scope, and concurrent-work preservation. Then invoke `superpowers:requesting-code-review` and resolve all Blocker/Critical/Important findings with tests before claiming completion.

- [ ] **Step 7: Update the traceability record with evidence.**

  In `docs/development/2026-09-01-hidden-basic-video-playback.md`, mark V01–V15 only when supported by exact commands, sample names, screenshots/logs, and observed results. Leave unmet rows explicitly open with risk/reason; never mark them complete from code inspection alone.

- [ ] **Step 8: Commit only the final acceptance artifacts and any reviewed fixes.**

  ```powershell
  git add -- tests/scripts/video_playback_acceptance.ps1 docs/development/2026-09-01-hidden-basic-video-playback.md
  git add -p -- app/main.cpp
  git diff --cached --check
  git commit -m "test: verify hidden video playback acceptance"
  ```

---

## Final Completion Gate

- [ ] Every V01–V15 row has evidence or is explicitly reported as unverified.
- [ ] The complete Release build and CTest suite pass, or every unrelated/pre-existing failure is identified with fresh evidence.
- [ ] QML lint passes.
- [ ] Real video and real audio-device playback were observed; metadata-only testing is not counted as playback acceptance.
- [ ] Windowed/fullscreen/portrait/restored-audio screenshots were reviewed.
- [ ] Pure audio shows zero persistent video resources.
- [ ] Stop/return/switch release the video worker, CPU frames, and GPU texture within the stated bound.
- [ ] Final diff review and independent review have no unresolved Blocker/Critical/Important findings.
- [ ] No package/release claim is made.
