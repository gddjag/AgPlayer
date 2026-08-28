# Task 3 report — QRhi Terrain Reactor renderer

## Scope and commit

- Implementation commit: `6609754` — `feat(visuals): add QRhi terrain reactor renderer`.
- Base supplied for this task: `a963b3c`.
- The implementation is confined to the Terrain Reactor item/state/shaders,
  QML type registration, minimal Qt/test CMake wiring, and focused tests. It
  does not change lyrics, QML hosts/panels/queues, waveform, playback, audio
  decoding, or the Task 1 controller.

## RED → GREEN evidence

1. `terrain_reactor_state_test` was registered before production sources. Its
   first build failed with MSVC C1083 because `terrain_reactor_state.hpp` did
   not exist. The initial GREEN covered deterministic scene layout and colors,
   bounded audio mapping/height, quality hysteresis, work gating, renderer
   generations, and camera recovery.
2. `terrain_reactor_item_test` was then registered before the QQuickRhiItem. Its
   first build failed with MSVC C1083 because `terrain_reactor_item.hpp` did not
   exist. The implementation then compiled and linked against Qt Quick and
   GuiPrivate, while `qt_add_shaders` generated both offline `.qsb` assets.
3. A quality-contract RED failed to compile because
   `QualityConfiguration::particleCount` did not exist. GREEN makes the first
   degradation step reduce real particles, then meteors, ripple count, grid,
   and finally internal render-target scale, with the required 2 s / 8 s / 5 s
   hysteresis and cooldown.
4. The software-backend lifecycle test first timed out after Qt reported
   `No QRhi found ... QQuickRhiItem will not be functional`. The item had
   missed the initial window association when constructed with a parent. After
   synchronizing the current window in the constructor, the assertions passed
   but teardown exposed Qt's `class destructor may have already run` fatal:
   a derived window callback survived into the QQuickItem base destructor.
   Explicit derived-destructor disconnects fixed that root cause. The final
   test proves software mode reports a diagnostic and leaves frame/upload
   counters unchanged.

## Implemented behavior

- `TerrainReactorItem` is a registered `QQuickRhiItem`; its renderer owns QRhi
  buffers, bindings, pipeline, and resource-update batches on the scene-graph
  render thread. `synchronize()` copies only an immutable GUI-thread snapshot.
- Rendering uses one indexed cube mesh and one instance buffer for a seeded
  terrain grid, floating cubes, meteors, and particles. The shaders provide a
  cyan-white center, cyan-blue/coral/pink sectors, multiple concentric ripples,
  audio-shaped heights, floating motion, particle bursts, and falling meteors.
- Input is limited to Task 1 `AudioVisualFeatureController` bands, energy,
  spectral flux, kick, and snare. The renderer performs no FFT or decoding.
- `active`, item visibility, host exposure, and window exposure gate scheduling.
  Inactive/hidden/minimized states do not advance animation/frame/upload
  counters. Software or null QRhi backends fail closed with a diagnostic.
- Camera snapshots support orbit, bounded zoom, beat punch/decay, automatic
  rotation, and a four-second manual-control pause before recovery. Mouse input
  remains deliberately outside this task for Task 4.
- Automatic quality reduction changes the actual instance/ripple counts and
  fixed render-target size. QRhi update batches are submitted once or explicitly
  released, and resource generations change only after invalidation/recreation.

## Validation and remaining limits

- Passed: `ctest --test-dir build/debug -R
  "(player_experience_controller|terrain_reactor_(state|item))_test"
  --output-on-failure` — 3/3 tests.
- Passed: Debug builds of `AgPlayer`, `terrain_reactor_state_test`,
  `terrain_reactor_item_test`, and `player_experience_controller_test` with
  MSVC `/W4 /WX`; Debug `AgPlayer.exe` linked and Qt deployment completed.
- Passed: `git diff --check` before the implementation commit. The final staged
  implementation contained only the eleven Task 3 files listed in the commit.
- Verified at runtime: Qt offscreen/software backend fails closed without a
  frame or upload and tears down safely.
- Not verified: a real accelerated D3D11/OpenGL/Vulkan device render, visual
  screenshot acceptance, GPU timing/VRAM/resource-loss soak, or integrated
  host interaction. The actual Terrain Reactor host and mouse wiring belong to
  Task 4, so accelerated visual acceptance must be performed after integration.

## Review round 1 — lifecycle, style, effects, and real GPU evidence

### Correction commit and RED evidence

- Correction implementation: `c35787f` —
  `fix(visuals): harden terrain reactor lifecycle`.
- Tests were extended before production changes. The first state-test build
  failed because finite meteor trail/collision collections, `MeteorPhase`,
  incremental camera synchronization, and `FramePacer` did not exist. The
  first item-test build likewise failed on the missing Task 1 style snapshot,
  shared renderer lifecycle, event-filter, and high-DPI APIs.
- The initial accelerated smoke used Direct3D11 and exposed an incremental-build
  ABI mismatch after the item header layout changed while `agplayer_qt` still
  contained the old object. Rebuilding that target removed the shared-pointer
  crash and revealed the real lifecycle RED: resources were created in
  `initialize()` before the first `synchronize()`, but Ready notification was
  lost because no item snapshot was available yet (`actual Inactive`,
  `expected Ready`). The renderer now creates resources independently of an
  old snapshot, caches status, and publishes it after synchronization with the
  current active snapshot.

### Closed review findings

- The GUI clock is the sole camera time origin. Renderer auto-rotation and
  audio punch are preserved while GUI orbit/zoom changes are applied as deltas;
  manual motion pauses auto-rotation for four seconds and then resumes.
- Fixed-seed meteors now have finite flight and collision phases. Three trail
  instances, eight white landing-ripple segments, and eight collision-burst
  particles per meteor share the existing instance buffer and single indexed
  draw with the terrain and other effects.
- A compact copied `RenderStyleSnapshot` carries Task 1 base/cool/warm/accent/
  peak colors, color mode/RGB sweep, eight visual-EQ gains, amplitude, motion,
  gradient, glow, cinema shake, auto-rotation, peak boost, and all effect
  toggles into renderer state and uniforms. No FFT, decoding, or playback code
  was added.
- Window Expose/Show/Hide/WindowStateChange events refresh scheduling gates.
  Item/shared atomics enforce one live renderer, reject duplicates with a
  diagnostic, and allocate globally monotonic resource generations.
- Eco cadence is capped by a deterministic 30 FPS frame pacer. Reduced internal
  targets use logical item size multiplied by effective DPR and internal scale;
  a 2x DPR test verifies the physical buffer dimensions.

### Round 1 verification

- Passed from a clean build: `cmake --build --preset windows-msvc-debug
  --target AgPlayer --clean-first --parallel 4` (245 build steps, exit 0).
  The post-clean qmlimportscanner initially reported missing old generated QML
  paths; CMake regenerated them and the full Debug application linked and
  deployed successfully.
- Passed: `ctest --test-dir build/debug -R
  "(player_experience_controller|terrain_reactor_(state|item|gpu_smoke))_test"
  --output-on-failure` — 4/4 tests.
- Passed on a real accelerated backend: direct Debug smoke reported
  `Terrain Reactor accelerated backend: Direct3D11`, with 3/3 QtTest cases and
  exit 0. It verified first activation with static synthetic bands, resource
  generation, frame/upload work, Off counter freeze and On recovery,
  minimized counter freeze and restored rendering, and one live renderer.
- Passed: offline qsb regeneration for both shaders, MSVC `/W4 /WX`, and final
  staged `git diff --check`.

### Remaining limits

- Direct3D11 was exercised, but OpenGL, Vulkan, Metal, GPU device-loss recovery,
  long VRAM/resource soak, and multi-monitor live-DPR transitions were not.
- Integrated host mouse input and screenshot-based visual acceptance remain
  Task 4 work. The smoke used deterministic synthetic Task 1-shaped features;
  it did not claim real audio playback or visual-design acceptance.

## Review round 2 — quality timing and one-shot punch events

### Correction commit and RED evidence

- Correction implementation: `47771d0` —
  `fix(visuals): separate quality timing and punch events`.
- Tests were changed before production code. The quality RED failed to compile
  because `AutomaticQualityController` had no `observeWorkSample()` or
  `advanceWallClock()` APIs. The punch RED independently failed on the missing
  `PunchEvent`, `PunchEventConsumer`, and `TerrainReactorItem::punchRevision()`.
  These failures established that the old contracts still coupled pacing delay
  to load and represented punch as persistent camera state.

### Closed review findings

- The automatic-quality loop now classifies only the current render-thread CPU
  submission segment, measured from instance/uniform preparation through QRhi
  command recording. Eco pacer skips and presentation/vsync delay are outside
  that work sample. Hysteresis and cooldown advance separately using elapsed
  wall time between allowed frames.
- The pure-logic test simulates 30 FPS Eco pacing on both 75 Hz and 165 Hz idle
  displays with 2 ms submission work and remains at full quality. It separately
  proves a sustained 40 ms work sample downgrades after 2.00 s and a 2 ms sample
  restores after 8.00 s while preserving the existing 5 s cooldown and ordered
  degradation contract.
- Punch is now a compact strength/revision event copied in the immutable render
  snapshot. The renderer consumes each monotonically newer revision once and
  owns the decaying envelope. Orbit/zoom revisions carry only manual camera
  deltas and cannot copy an old GUI punch back into renderer state. New events
  retrigger even when their strength equals or is below the previous event.

### Round 2 verification

- Passed: `ctest --test-dir build/debug -R
  "(player_experience_controller|terrain_reactor_(state|item|gpu_smoke))_test"
  --output-on-failure` — 4/4 tests.
- Passed: Debug `AgPlayer` build with MSVC `/W4 /WX`; the application target
  rebuilt after the terrain item ABI change and the build command exited 0.
- Passed on the accelerated backend: verbose GPU smoke explicitly reported
  `Terrain Reactor accelerated backend: Direct3D11`, with 3/3 QtTest cases,
  zero failures, zero skips, and exit 0.
- Passed: final implementation `git diff --cached --check`; the implementation
  commit contains only terrain state/item production files and their focused
  tests.

### Remaining limits

- The Direct3D11 smoke proves renderer/resource execution but does not inject a
  controlled 2 s real-GPU overload or validate hardware GPU timing. Automatic
  quality currently uses render-thread CPU submission cost, as requested, not
  GPU timestamp queries.
- OpenGL, Vulkan, Metal, device-loss recovery, extended VRAM/resource soak,
  integrated Task 4 mouse input, and screenshot-based visual acceptance remain
  unverified.
