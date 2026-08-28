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
