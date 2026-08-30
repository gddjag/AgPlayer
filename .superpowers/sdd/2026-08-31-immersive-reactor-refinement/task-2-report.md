# Task 2 report — original soft-luminous QRhi terrain reactor

## Scope and clean-room statement

- Implemented only in the Task 2 renderer state, vertex shader, renderer item
  telemetry/ingestion boundary, and the two authorized terrain tests.
- No uploaded media or external repository source was opened or used. The
  response model and numeric bounds below were derived from the product
  criteria in `task-2-brief.md`.
- Preserved the existing immutable feature snapshot, renderer/resource owner,
  automatic quality order, one instance buffer, and one `drawIndexed` call.
  No dependency, decoder, FFT, cache, frame loop, pass, QML, lyrics, waveform,
  queue, playback, window, or packaging change was introduced.
- The six preset values were intentionally left unchanged: the existing
  complete-snapshot test still proves all six are distinct, and the revised
  bounded mappings give every relevant value a live effect without churn.

## RED evidence

Release test build (after loading the Visual Studio developer environment):

```powershell
cmake --build build/release --config Release --target terrain_reactor_state_test
```

The four new state cases were then run directly with the Release Qt/vcpkg
runtime on `PATH`. They failed for the intended missing behavior:

- equal-strength low/mid maximum `15.7608`, high maximum `16.8`;
- representative core peak/median/neighbour mean
  `21.9124 / 7.92719 / 13.8458` (peak/median about `2.76`);
- fixed-seed floating-cube median/maximum `1.54361 / 2.04598`;
- `peakBoost=0` and `peakBoost=1` produced the same terrain height.

Summary: `2 passed, 4 failed`, exit `4`. All failures were assertion failures
at the new product criteria, not compiler or fixture errors. An earlier run
without the VS/runtime environment was discarded as invalid evidence because
it could not locate system headers/DLLs.

The accelerated GPU test was also run before the shader edit, with a fixed
dark window background, fixed seed, disabled motion/events, and equal input
strength. The old shader failed the height-subordinate visual assertion:

- high-frequency occupied pixels: `26,348`;
- low/mid occupied pixels: `26,310`;
- allowed backend edge tolerance: `8` pixels (actual excess: `38`);
- localized sheen pixels: `9,721 / 129,600`;
- plain/sheen light totals: `9,986,456 / 10,671,006`.

Summary: `2 passed, 1 failed`, exit `1`.

The independent review correctly rejected that first GPU assertion as too
close to backend raster edges and not synchronized to the requested renderer
snapshot. The original RED above is retained as history, but it is not the
acceptance gate used below.

## Independent-review follow-up RED/GREEN

Non-finite state RED was recorded before the boundary fix:

```powershell
build/release/tests/terrain_reactor_state_test.exe `
  nonFiniteInputsUseFiniteBoundedFallbacks
```

The new NaN/positive-infinity/negative-infinity loop failed at
`std::isfinite(dynamics.inputCompression)` (`2 passed, 1 failed`, exit `1`).
The matching item-ingestion case failed because synthetic and live feature
bands were exposed as non-finite values. After the minimal fix, both targeted
tests passed `3/3`.

Fallbacks are explicit and finite before mapping/uniform upload:

- non-finite audio bands, energy, flux, event strength, and camera punch use
  `0` (silence/no event), then retain the existing `[0, 1]` bound;
- non-finite style values use the corresponding `RenderStyleSnapshot` default
  before the existing parameter-specific bound; visual-EQ ingestion uses
  neutral `0.5` and cinema shake uses its existing default;
- non-finite terrain instance coordinates use `0`, random uses bounded `0`,
  and explicit time falls back to the sanitized snapshot time, then `0`;
- every `RenderDynamics` and `VisualParameters` member is asserted finite and
  within its product range, while `terrainHeight` remains `0.035..36`.

Renderer synchronization also produced a real pre-production RED: the GPU
case required dynamic `renderedFeatureRevision`, `renderedStyleRevision`, and
`stableRenderedFrameCount` properties, and the old item failed because the
exact-revision acknowledgement did not exist. The final renderer publishes
the feature/style revision pair used by the completed render pass and a stable
pair count. Each capture now waits for the exact pair, at least three stable
rendered frames, and three additional `QQuickWindow::frameSwapped` signals.
The test explicitly requests frames while waiting because an otherwise idle
`QQuickRhiItem` does not promise continuous presentation.

The replacement product metric compares each driven frame with a silent
neutral frame, sorts per-pixel absolute RGB response, and sums the strongest
one percent. This is relative to both image size and the same-backend neutral
render, while focusing on visible relief instead of a rasterized silhouette.
The acceptance requirement is high-frequency response no more than `90%` of
equal-strength low/mid response, leaving a deliberate `10%` perceptual margin.
A temporary, locally reconstructed `d48c288` height-behavior mutation (never
committed) produced a valid RED on Direct3D 11:

- high strongest-1% response: `228,103`;
- low/mid strongest-1% response: `246,330`;
- ratio: `92.60%`, failing the required `<= 90%`;
- localized sheen: `5,561` pixels;
- plain/sheen light: `9,690,067 / 9,957,571`.

The mutation was immediately reversed with `apply_patch`.
`git diff --exit-code HEAD -- qt/shaders/terrain_reactor.vert` then confirmed
the refined shader was restored exactly, and the CMake target rebuilt QSB/RCC.

## Design and bound rationale

- Low bands now build central weight from a wide radial core plus a slow broad
  field. Mid bands use two long-wavelength, oppositely oriented ridges. High
  bands share a normalized `0.38 / 0.28 / 0.20 / 0.14` detail mix, so no one
  upper band can create a tower.
- The representative inner field allows a peak at most `1.65x` the median and
  `1.40x` its nearby mean. These deliberately leave visible layering while
  rejecting a lone coarse tower.
- `peakBoost` now scales only coherent high-frequency relief from `0.42x` to
  `1.0x`; the test requires a visible increase but caps the increase to avoid
  recreating towers.
- Floating cubes are deterministically `0.324..0.72` world units. The test
  permits a maximum of `0.85` and median of `0.68`, comfortably below a
  representative terrain cell's `6.72`-unit span.
- The GPU test requires strongest-1% high relief to stay at or below `90%` of
  equal-strength low/mid relief. Independently, it requires more than `1/3000`
  but fewer than `1/8` of pixels to gain visible sheen and caps whole-frame
  light increase at `8%`. These relative limits distinguish subordinate high
  detail and readable localized top shimmer from broad exposure lift.

## Changed parameter meanings

- Bands 0–1: broad core weight and slow breathing field.
- Bands 2–3: wide coherent shoulders/ridges.
- Bands 4–7: height-subordinate coherent detail plus sparse top-face sheen.
- `peakBoost`: bounded high-detail relief, no longer dead in the CPU model.
- `ripplesEnabled`: now also gates the CPU ripple and structural-ring model,
  matching the shader toggle.
- `centerHighlight`: broad dome/shoulder illumination rather than random
  center spikes.
- `streamHighlightEnabled`: sparse top-face flow/sparkle; side faces receive a
  stable darker light multiplier.
- `floatingCubesEnabled`: unchanged ownership/count behavior, with a smaller
  subordinate deterministic size distribution.
- Compression/response, response range, rhythm strength/sensitivity,
  clarity/depth, event envelopes, remaining toggles, and automatic-quality
  degradation retain their existing bounded mappings and wiring.

## GREEN and regression evidence

New state cases after the minimal C++ change:

- equal-strength low/mid vs high maxima: `14.7248 / 6.03987`;
- representative peak/median/neighbour mean:
  `16.2007 / 9.82142 / 15.1979`;
- floating-cube median/maximum: `0.542147 / 0.718587`;
- all four new cases passed; complete state suite passed `29/29`.

Offline shader compilation was exercised by the existing CMake target:

```text
Generating .qsb/shaders/terrain_reactor.vert.qsb
Running rcc for resource agplayer_terrain_reactor_shaders
```

Accelerated GPU GREEN on Direct3D 11 after exact-revision/presentation
synchronization:

- representative high/low-mid strongest-1% response:
  `97,413 / 308,502` (about `31.58%`, versus the `90%` limit);
- representative localized sheen: `6,126 / 129,600` pixels;
- representative plain/sheen light totals:
  `9,709,895 / 10,000,387` (about `2.99%`, versus the `8%` limit);
- isolated case passed `10/10` consecutively. Ratios remained approximately
  `31.6%..32.1%`; one DPI-scaled capture changed both raw sums to
  `219,996 / 690,993` while preserving the `31.84%` ratio;
- the complete GPU executable passed `10/10` consecutively, each execution
  reporting `6 passed, 0 failed`. After the final expanded ingestion assertion,
  the complete executable again passed `10/10` consecutively;
- Direct3D 11 was the only available accelerated backend in this environment.

Fresh final Release verification:

```powershell
ctest --test-dir build/release `
  -R '^(terrain_reactor_state_test|terrain_reactor_item_test|terrain_reactor_gpu_smoke_test|player_experience_controller_test)$' `
  --output-on-failure
```

Result: `4/4` tests passed in the final fresh run (`6.26 s`). This includes
lifecycle/ownership, zero-work, quality order, camera/impact, palette,
accelerated GPU smoke, and the unchanged six-preset snapshots.
`git diff --check` returned no whitespace errors (only the repository's
existing LF-to-CRLF checkout warnings).

## Remaining risks

- The accelerated image assertion was exercised on Windows Direct3D 11.
  Its neutral-relative, percentage-based limits avoid absolute edge/pixel
  assumptions, but Metal, Vulkan, and OpenGL were not available in this run.
- Per the task instruction, the GUI was not launched manually. Visual evidence
  is deterministic synthetic-spectrum state and accelerated frame analysis,
  not a subjective full-app usability session.
- Shader/state formulas are intentionally parallel but remain two source
  implementations; future tuning should keep their semantic weights aligned.
