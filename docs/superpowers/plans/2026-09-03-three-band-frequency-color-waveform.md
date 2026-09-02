# Three-Band Frequency Color Waveform Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace AgPlayer's Spectral Centroid and eight-color palette with one lightweight Low / Mid / High energy frequency-color waveform.

**Architecture:** The existing waveform analyzer remains the single producer of amplitude and three band-energy arrays, with cutoffs at 250 Hz and 4 kHz. The single `.agwf` cache stores those arrays, and the existing QSG vertex-color renderers mix three configurable base colors at paint time while playback progress changes alpha only.

**Tech Stack:** C++17, Qt 6 / QML, Qt Quick scene graph, CMake / CTest

**Spec:** `docs/superpowers/specs/2026-09-03-three-band-frequency-color-waveform-design.md`

## Global Constraints

- Product name remains “频彩波形”.
- Data is exactly Low / Mid / High; settings expose exactly three base colors.
- Defaults are Low `#8B3DFF`, Mid `#FFB000`, High `#002FA7`.
- Waveform height is the original amplitude; playback progress changes opacity only.
- Delete FFT, Hann-window, Spectral Centroid, `spectralIndex`, and eight-palette-only code.
- Keep one analyzer, one `.agwf` cache, and one renderer path; add no dependency, shader, worker, or cache.
- Settings-only color changes must not trigger audio decoding, analysis, or cache writes.

---

### Task 1: Replace the analyzer contract and invalidate obsolete caches

**Files:**
- Modify: `core/src/waveform_analyzer.hpp`
- Modify: `core/src/waveform_analyzer.cpp`
- Modify: `core/src/waveform_cache.hpp`
- Modify: `core/src/waveform_cache.cpp`
- Modify: `core/include/agplayer/c_api.h`
- Modify: `core/src/c_api.cpp`
- Rename: `tests/core/spectral_centroid_waveform_test.cpp` to `tests/core/three_band_waveform_test.cpp`
- Modify: `tests/core/waveform_analyzer_test.cpp`
- Modify: `tests/core/waveform_cache_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: decoded interleaved PCM passed to `WaveformBucketizer::add` and `WaveformAnalyzer::analyze`.
- Produces: one `WaveformCacheData` containing `mix`, `bass`, `mid`, and `high`; the C API exposes only the existing mix and three-band accessors.

- [ ] **Step 1: Write failing three-band analyzer and cache tests**

  Replace centroid assertions with synthetic 100 Hz, 1 kHz, and 8 kHz signals and require the intended band to dominate. Update cache tests so a valid record contains only four equal-length float arrays and a record from the obsolete algorithm revision is rejected.

```cpp
assert(low100.bass > low100.mid && low100.bass > low100.high);
assert(mid1000.mid > mid1000.bass && mid1000.mid > mid1000.high);
assert(high8000.high > high8000.bass && high8000.high > high8000.mid);
assert(loaded.mix == data.mix);
assert(loaded.bass == data.bass);
assert(loaded.mid == data.mid);
assert(loaded.high == data.high);
```

- [ ] **Step 2: Run focused tests and confirm the old API/format fails the new contract**

  Run from the configured build directory:

```powershell
ctest --test-dir build -C Debug -R "(spectral_centroid_waveform_test|waveform_analyzer_test|waveform_cache_test)" --output-on-failure
```

  Expected: at least the renamed target/configuration or new 4 kHz dominance and no-spectral-field assertions fail before implementation.

- [ ] **Step 3: Delete centroid calculation and simplify the analyzer**

  Remove the `include_spectral_index` constructor argument, FFT buffers, window coefficients, spectral accumulators, centroid frame method, output pointer, and C API spectral functions. Change the band split constants to:

```cpp
constexpr double kBassCutoffHz = 250.0;
constexpr double kMidLowCutoffHz = 250.0;
constexpr double kMidHighCutoffHz = 4000.0;
constexpr double kHighCutoffHz = 4000.0;
```

  Keep the current low-pass / band-pass / high-pass sample loop and peak aggregation as the only analysis path.

- [ ] **Step 4: Remove `spectral_index` from the cache and bump the algorithm revision**

  Serialize and validate only `mix/bass/mid/high`. Increase the waveform cache algorithm revision so existing 2 kHz or centroid-bearing cache entries take the existing miss-and-reanalyze path.

- [ ] **Step 5: Run the renamed core tests**

```powershell
cmake --build build --config Debug --target three_band_waveform_test waveform_analyzer_test waveform_cache_test
ctest --test-dir build -C Debug -R "(three_band_waveform_test|waveform_analyzer_test|waveform_cache_test)" --output-on-failure
```

  Expected: all selected tests pass, and an exact source search finds no analyzer/cache/C-API `spectralIndex`, FFT, Hann, or Spectral Centroid symbols.

### Task 2: Make the provider and thumbnail pipeline consume the existing three bands

**Files:**
- Modify: `qt/src/waveform_provider.hpp`
- Modify: `qt/src/waveform_provider.cpp`
- Modify: `qt/src/track_waveform_thumbnail_provider.hpp`
- Modify: `qt/src/track_waveform_thumbnail_provider.cpp`
- Modify: `tests/qt/waveform_provider_test.cpp`
- Modify: `tests/qt/track_waveform_thumbnail_provider_test.cpp`

**Interfaces:**
- Consumes: the Task 1 `WaveformCacheData { mix, bass, mid, high }` and the existing C API band accessors.
- Produces: layer maps containing exactly `mix`, `bass`, `mid`, `high`; thumbnails receive quantized amplitude plus quantized Low / Mid / High arrays from the same cache entry.

- [ ] **Step 1: Write failing provider tests**

```cpp
QCOMPARE(layers.value(QStringLiteral("mix")).toList().size(), 4);
QCOMPARE(layers.value(QStringLiteral("bass")).toList().size(), 4);
QCOMPARE(layers.value(QStringLiteral("mid")).toList().size(), 4);
QCOMPARE(layers.value(QStringLiteral("high")).toList().size(), 4);
QVERIFY(!layers.contains(QStringLiteral("spectralIndex")));
```

  Require a frequency-color request to reuse a complete cached four-layer waveform and not start a second centroid-specific job.

- [ ] **Step 2: Run provider tests and verify failure**

```powershell
cmake --build build --config Debug --target waveform_provider_test track_waveform_thumbnail_provider_test
ctest --test-dir build -C Debug -R "(waveform_provider_test|track_waveform_thumbnail_provider_test)" --output-on-failure
```

- [ ] **Step 3: Collapse frequency-color provider logic into the normal waveform job**

  Delete `JobKind::FrequencyColor`, spectral readiness checks, centroid-only analysis calls, and spectral-index conversion helpers. Treat four equal-size arrays as complete for frequency coloring and emit them through the existing layer map.

- [ ] **Step 4: Quantize and cache thumbnail Low / Mid / High values once**

  Replace thumbnail `spectralIndex` payloads with three equal-length `QByteArray` band payloads generated from the already loaded cache. Preserve the existing thumbnail worker and cache; do not add a second provider or decode.

- [ ] **Step 5: Run provider tests**

```powershell
ctest --test-dir build -C Debug -R "(waveform_provider_test|track_waveform_thumbnail_provider_test)" --output-on-failure
```

  Expected: both provider tests pass and frequency-color mode triggers no centroid-specific analysis branch.

### Task 3: Replace the eight-stop palette with three base-color settings

**Files:**
- Modify: `qt/src/frequency_color_waveform_settings.hpp`
- Modify: `qt/src/frequency_color_waveform_settings.cpp`
- Modify: `qt/src/settings_controller.cpp`
- Modify: `app/qml/AgPlayer/SettingsPage.qml`
- Modify: `tests/qt/settings_controller_test.cpp`
- Modify: `tests/qml/tst_main_window.qml`

**Interfaces:**
- Consumes: `QSettings` and QML user color edits.
- Produces: `lowColor`, `midColor`, and `highColor` properties plus existing `unplayedOpacity`; emits `changed()` for render-only updates.

- [ ] **Step 1: Write failing settings tests**

```cpp
QCOMPARE(settings.frequencyColorWaveform()->lowColor(), QColor("#8B3DFF"));
QCOMPARE(settings.frequencyColorWaveform()->midColor(), QColor("#FFB000"));
QCOMPARE(settings.frequencyColorWaveform()->highColor(), QColor("#002FA7"));
```

  QML tests must find exactly `frequencyLowColor`, `frequencyMidColor`, and `frequencyHighColor`, verify persistence/reset, and prove the old `spectralPaletteColor0` control is absent.

- [ ] **Step 2: Run settings tests and verify failure**

```powershell
cmake --build build --config Debug --target settings_controller_test
ctest --test-dir build -C Debug -R "(settings_controller_test|tst_main_window)" --output-on-failure
```

- [ ] **Step 3: Implement the three-color settings object**

  Replace palette list normalization and indexed mutation with three validated `QColor` properties. Save stable keys under the existing frequency-color group, remove writes of the eight legacy palette keys, and load defaults when legacy-only settings are present.

- [ ] **Step 4: Replace the eight QML color rows with three labeled controls**

  Reuse the existing settings-page color-field component and layout tokens. Bind the controls to Low / Mid / High properties and keep the current reset and unplayed-opacity controls.

- [ ] **Step 5: Run settings tests**

```powershell
ctest --test-dir build -C Debug -R "(settings_controller_test|tst_main_window)" --output-on-failure
```

  Expected: exactly three colors are editable, persisted, and reset to the approved defaults.

### Task 4: Render automatic three-band mixtures in the existing QSG nodes

**Files:**
- Modify: `qt/src/waveform_item.hpp`
- Modify: `qt/src/waveform_item.cpp`
- Modify: `qt/src/track_waveform_thumbnail_item.hpp`
- Modify: `qt/src/track_waveform_thumbnail_item.cpp`
- Modify: `app/qml/AgPlayer/components/SharedWaveformView.qml`
- Modify: `app/qml/AgPlayer/components/IntegratedPlayerShell.qml`
- Modify: `app/qml/AgPlayer/components/RollingPlayerShell.qml`
- Modify: `app/qml/AgPlayer/components/PlayerPane.qml`
- Modify: `app/qml/AgPlayer/components/MiniPlayerControls.qml`
- Modify: `app/qml/AgPlayer/components/TrackWaveformThumbnail.qml`
- Modify: `app/qml/AgPlayer/SettingsPage.qml`
- Modify: `tests/qt/waveform_item_test.cpp`
- Modify: `tests/qt/track_waveform_thumbnail_item_test.cpp`
- Modify: `tests/qml/tst_audio_visual_impact.qml`
- Modify: `tests/qml/tst_integrated_theme.qml`
- Modify: `tests/qml/tst_mini_player.qml`
- Modify: `tests/qml/tst_rolling_theme.qml`
- Modify: `tests/qml/tst_immersive_integration.qml`

**Interfaces:**
- Consumes: layer arrays `mix/bass/mid/high`, three `QColor` settings, progress, and unplayed opacity.
- Produces: unchanged waveform vertices with per-vertex mixed RGB and alpha-only progress treatment.

- [ ] **Step 1: Write failing renderer tests**

  Add deterministic color-helper coverage for pure and combined bands and keep the scene-node reuse assertion:

```cpp
QCOMPARE(mixFrequencyColor(1.0, 0.0, 0.0, low, mid, high), low);
QCOMPARE(mixFrequencyColor(0.0, 1.0, 0.0, low, mid, high), mid);
QCOMPARE(mixFrequencyColor(0.0, 0.0, 1.0, low, mid, high), high);
QVERIFY(mixFrequencyColor(1.0, 1.0, 0.0, low, mid, high) != low);
QVERIFY(mixFrequencyColor(1.0, 1.0, 0.0, low, mid, high) != mid);
```

  Assert played/unplayed vertices keep identical positions and RGB while only alpha differs.

- [ ] **Step 2: Run renderer tests and verify failure**

```powershell
cmake --build build --config Debug --target waveform_item_test track_waveform_thumbnail_item_test
ctest --test-dir build -C Debug -R "(waveform_item_test|track_waveform_thumbnail_item_test)" --output-on-failure
```

- [ ] **Step 3: Implement one shared three-color mix helper**

  Add a small Qt-side helper used by both main and thumbnail renderers. Clamp energies, apply square-root compression, convert base colors from sRGB to linear RGB, compute the weighted mixture, then apply bounded saturation/lightness recovery before converting back to sRGB. Return the exact base color for a pure one-hot band.

- [ ] **Step 4: Replace spectral-index properties and painting branches**

  Main waveform snapshots and thumbnail items carry Low / Mid / High arrays. Bind three color properties from QML. Reuse the current `QSGVertexColorMaterial`, geometry nodes, visible-range resampling, and update scheduling. Remove every `spectralPalette` and `spectralIndex` property.

- [ ] **Step 5: Run renderer and QML contract tests**

```powershell
ctest --test-dir build -C Debug -R "(waveform_item_test|track_waveform_thumbnail_item_test|tst_audio_visual_impact|tst_integrated_theme|tst_mini_player|tst_rolling_theme|tst_immersive_integration)" --output-on-failure
```

  Expected: pure bands use the configured defaults, mixtures are automatically generated, settings recolor without replacing geometry, and progress changes alpha only.

### Task 5: Remove residual old code and perform full acceptance

**Files:**
- Modify: `docs/development/2026-09-01-spectral-centroid-frequency-waveform.md`
- Create: `docs/development/2026-09-03-three-band-frequency-color-waveform.md`
- Modify: any directly related test/build manifest still naming the removed centroid feature.

**Interfaces:**
- Consumes: completed Tasks 1–4.
- Produces: source tree with one documented three-band implementation and reproducible validation evidence.

- [ ] **Step 1: Delete obsolete test target names and document supersession**

  Rename the core test target to `three_band_waveform_test`, mark the 2026-09-01 centroid record as superseded, and record the final data contract, defaults, cache revision, commands, and observed results in the new development record.

- [ ] **Step 2: Run exact residual searches**

```powershell
rg -n "spectral(Index|_index|Palette|Centroid)|kFft|Hann" core/src core/include qt/src app/qml tests
rg -n "#123ECF|#E82718|spectralPaletteColor" qt/src app/qml tests
```

  Expected: no matches belonging to the frequency-color waveform. Unrelated DSP tests such as vocal-separation Hann windows may remain and must not be deleted.

- [ ] **Step 3: Configure and build the application and focused tests**

```powershell
cmake -S . -B build -DAGPLAYER_BUILD_TESTS=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug -R "(three_band_waveform|waveform_analyzer|waveform_cache|waveform_provider|waveform_item|track_waveform_thumbnail|settings_controller|tst_main_window|tst_audio_visual_impact|tst_integrated_theme|tst_mini_player|tst_rolling_theme|tst_immersive_integration)" --output-on-failure
```

- [ ] **Step 4: Run the app and visually inspect the actual UI**

  Open Settings and confirm three color controls with the approved defaults. Load the decoder fixture or an available local track, switch to 频彩波形, capture the main waveform and list thumbnail, and verify rich mixed colors, amplitude-shaped height, opacity-only progress, alignment, HiDPI legibility, and no obvious redraw stutter.

- [ ] **Step 5: Perform final Diff Review**

```powershell
git diff --check
git status --short
git diff --stat
git diff -- core/src core/include qt/src app/qml tests docs
```

  Review correctness, cache invalidation, array length guards, QML bindings, scene-node lifetime, allocations in paint loops, dead code, and unrelated changes. Do not claim packaging, hardware playback, or full-suite health unless those checks were actually run.
