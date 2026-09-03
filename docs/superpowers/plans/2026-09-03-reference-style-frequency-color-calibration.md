# Reference-Style Frequency Color Calibration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Calibrate AgPlayer's existing three-band waveform so it produces balanced red/green/blue primaries and rich additive intermediate colors with lightweight depth shading.

**Architecture:** Keep the existing single analyser/cache/renderer path. Change only the three-band feature extraction, the shared color-transfer helper, the three default colors, and the frequency-mode vertex layout; keep mix amplitude and playback-progress semantics unchanged.

**Tech Stack:** C++17, Qt 6 Scene Graph, QTest/CTest, existing IIR biquads and `.agwf` cache.

**Spec:** `docs/superpowers/specs/2026-09-03-reference-style-frequency-color-calibration-design.md`

## Global Constraints

- Low/Mid/High defaults are exactly `#FC0909`, `#03FF00`, `#0048FF`.
- Height uses only `mix`; progress changes only Alpha.
- Keep one analyser, one `.agwf` cache and one QSG waveform node.
- Do not add FFT, Spectral Centroid, FIR, Attack-Release, ShaderEffect, dependency, thread or user-facing algorithm setting.
- Tests must be observed failing before production changes.

---

### Task 1: Balanced Three-Band Feature Extraction

**Files:**
- Modify: `core/src/waveform_analyzer.hpp`
- Modify: `core/src/waveform_analyzer.cpp`
- Modify: `core/src/waveform_cache.cpp`
- Test: `tests/core/three_band_waveform_test.cpp`
- Test: `tests/core/waveform_cache_test.cpp`

**Interfaces:**
- Consumes: existing `WaveformBucketizer::add()` and `finish()` API.
- Produces: unchanged `mix/bass/mid/high` vectors with calibrated band values in `[0,1]`; `WaveformCache::key_for()` uses analysis schema5 while the payload stays v4.

- [ ] **Step 1: Add failing behavioral tests**

Add literal assertions that: a short 10 kHz burst retains a materially visible High value; an equal-amplitude mixed fixture does not leave High below 20% of both Low and Mid; silence stays zero; every output is finite and in `[0,1]`; and the current cache key no longer equals the recorded schema4 key fixture.

- [ ] **Step 2: Run the focused tests and verify the intended failures**

Run:

```powershell
ctest --test-dir build/msvc-debug -C Debug -R "^(three_band_waveform_test|waveform_cache_test)$" --output-on-failure
```

Expected: the new transient/balance and schema5 key assertions fail while existing assertions remain green.

- [ ] **Step 3: Implement RMS/peak extraction and robust normalization**

Maintain per-band sum-of-squares and peak vectors. At finish, calculate `0.80 * rms + 0.20 * peak`, 95th-percentile references, `0.35 * globalP95 + 0.65 * bandP95`, gains `{0.90, 1.00, 1.35}`, and the `0.015 * bandP95` noise floor. Preserve the selected aggregation for `mix` height only.

- [ ] **Step 4: Invalidate old analysis keys without changing the payload format**

Change only `analysis_schema_version` from `4U` to `5U`; retain `cache_version_v4`, `save_v4()` and `load_v4()`.

- [ ] **Step 5: Run the focused tests**

Run the Step 2 command. Expected: both tests pass with zero failures.

- [ ] **Step 6: Commit**

```powershell
git add core/src/waveform_analyzer.hpp core/src/waveform_analyzer.cpp core/src/waveform_cache.cpp tests/core/three_band_waveform_test.cpp tests/core/waveform_cache_test.cpp
git commit -m "feat: balance three-band waveform energy"
```

### Task 2: Additive Color Transfer and Fixed Defaults

**Files:**
- Modify: `qt/src/frequency_color_mix.hpp`
- Modify: `qt/src/frequency_color_waveform_settings.cpp`
- Test: `tests/qt/waveform_item_test.cpp`
- Test: `tests/qt/settings_controller_test.cpp`
- Test: `tests/qml/tst_main_window.qml`
- Test: `tests/qml/tst_immersive_integration.qml`

**Interfaces:**
- Consumes: calibrated `[0,1]` band values from Task 1.
- Produces: unchanged `mixFrequencyColor(double,double,double,QColor,QColor,QColor)` signature and unchanged three-color settings API.

- [ ] **Step 1: Add failing literal color tests**

Using the fixed RGB defaults, assert pure full-energy bands return the exact base colors; equal Low+Mid is bright yellow, Mid+High bright cyan, Low+High bright magenta, all three are near white; a 0.15 High contribution remains visibly blue; and mixed outputs are finite valid QColor values. Update settings tests to require the three exact new defaults and reset values.

- [ ] **Step 2: Run focused Qt tests and verify the intended failures**

Run:

```powershell
ctest --test-dir build/msvc-debug -C Debug -R "^(waveform_item_test|settings_controller_test)$" --output-on-failure
```

Expected: additive-mixture and default-color assertions fail against the old HSL-restored purple/orange/ultramarine implementation.

- [ ] **Step 3: Implement the color transfer**

For each energy calculate `pow(pow(clamp(x,0,1),0.55),1.20)`. Decode base colors to linear light, add unnormalised weighted channels, apply `(1-exp(-1.35*x))/(1-exp(-1.35))` with `[0,1]` clamp, then encode to sRGB. Preserve exact pure full-energy base colors and remove HSL recovery.

- [ ] **Step 4: Change defaults only, without new settings**

Set Low/Mid/High defaults and reset behavior to `#FC0909/#03FF00/#0048FF`. Do not add gamma, gain, attack, release or filter controls.

- [ ] **Step 5: Run focused Qt tests**

Run the Step 2 command. Expected: both tests pass with zero failures.

- [ ] **Step 6: Commit**

```powershell
git add qt/src/frequency_color_mix.hpp qt/src/frequency_color_waveform_settings.cpp tests/qt/waveform_item_test.cpp tests/qt/settings_controller_test.cpp tests/qml/tst_main_window.qml tests/qml/tst_immersive_integration.qml
git commit -m "feat: enrich three-band additive colors"
```

### Task 3: Lightweight Frequency-Waveform Depth Shading

**Files:**
- Modify: `qt/src/waveform_item.cpp`
- Test: `tests/qt/waveform_item_test.cpp`
- Modify: `docs/development/2026-09-03-three-band-frequency-color-waveform.md`

**Interfaces:**
- Consumes: `VertexColor` returned by existing `mixColor()`.
- Produces: four vertices per frequency-mode stroke (top-to-center and center-to-bottom); all other modes retain two vertices per stroke.

- [ ] **Step 1: Add failing geometry/color tests**

Assert frequency mode allocates two line segments per stroke; center vertices share the full progress-adjusted Alpha; edge vertices use 55% of that Alpha; center RGB is lighter than edge RGB; played and unplayed geometry positions are identical; and non-frequency modes retain their current vertex counts.

- [ ] **Step 2: Run the focused renderer test and verify failure**

Run:

```powershell
ctest --test-dir build/msvc-debug -C Debug -R "^waveform_item_test$" --output-on-failure
```

Expected: the new four-vertex depth-shading assertions fail against the existing uniform two-vertex line.

- [ ] **Step 3: Implement frequency-only vertex shading**

For visual mode3 write top/center and center/bottom segments. Derive edge RGB by multiplying linear-light center RGB by `0.72`, center RGB by `1.08` with clamping, and edge Alpha by `0.55`; retain the original Alpha at both center vertices. Update color-only refresh to write the same four-vertex contract. Do not alter spectrum or ordinary waveform paths.

- [ ] **Step 4: Run renderer and shared-waveform regressions**

Run:

```powershell
ctest --test-dir build/msvc-debug -C Debug -R "^(waveform_item_test|track_waveform_thumbnail_item_test|waveform_provider_test)$" --output-on-failure
```

Expected: all selected tests pass with zero failures.

- [ ] **Step 5: Update the implementation record**

Record the new energy calibration, color transfer, cache schema5 key, three defaults, renderer shading and actual verification results. Do not claim screenshot or hardware validation until performed.

- [ ] **Step 6: Commit**

```powershell
git add qt/src/waveform_item.cpp tests/qt/waveform_item_test.cpp docs/development/2026-09-03-three-band-frequency-color-waveform.md
git commit -m "feat: add frequency waveform depth shading"
```

### Task 4: Integration Verification and Visual Evidence

**Files:**
- Modify only if a verified defect requires a fix.
- Evidence: `build/qa/reference-style-frequency-color/`

**Interfaces:**
- Consumes: completed Tasks 1-3.
- Produces: fresh build/test/diff evidence and real UI screenshots; no packaging.

- [ ] **Step 1: Build Debug**

```powershell
cmake --build build/msvc-debug --config Debug
```

- [ ] **Step 2: Run all registered tests**

```powershell
ctest --test-dir build/msvc-debug -C Debug --output-on-failure
```

- [ ] **Step 3: Run the existing main-player smoke and capture screenshots**

Use the repository's existing QA launcher/smoke path to load a real track, select frequency-color mode, and save player/settings screenshots under `build/qa/reference-style-frequency-color/`. Confirm visually that the settings show only the three fixed defaults and the waveform contains visible blue/cyan/magenta detail without height/progress regression.

- [ ] **Step 4: Review the final diff**

```powershell
git diff --check d39984013ae25996087eddd1537a50fb8a61503b..HEAD
git status --short
git diff --stat d39984013ae25996087eddd1537a50fb8a61503b..HEAD
```

Inspect correctness, cache invalidation, vertex counts, allocations, color-only updates and absence of unrelated changes.
