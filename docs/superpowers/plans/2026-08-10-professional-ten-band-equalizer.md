# Professional Ten-Band Equalizer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a real, low-latency ten-band graphic equalizer to AgPlayer's main playback PCM path, with a compact 780x460 desktop UI, presets, persistence, automatic anti-clipping, and measured DSP evidence.

**Architecture:** Decode and buffering remain unchanged. A fixed-capacity, allocation-free `GraphicEqualizerProcessor` is inserted in `AudioEngine::Impl::render()` immediately after the PCM ring-buffer read and before ReplayGain/volume. Control-thread code validates settings, computes RBJ coefficients and protection gain, then publishes complete versioned programs through a bounded SPSC mailbox. The callback uses preallocated per-channel filter banks and smooth dry/wet/program transitions.

**Tech Stack:** C++17, Qt 6/QML, existing AgPlayer C API, GoogleTest/QtTest, CMake, QSettings.

## Global Constraints

- Main-player playback only; audio-tool preview and export paths remain unchanged.
- Fixed frequencies: 31.25, 62.5, 125, 250, 500, 1k, 2k, 4k, 8k, 16k Hz.
- Gains and preamp: -12.0..+12.0 dB, 0.1 dB UI step; Q=1.414.
- Supported sample rates: 44.1/48/88.2/96/192 kHz.
- No locks, logging, file I/O, allocation, container growth, or coefficient calculation in the audio callback.
- Every channel has independent biquad state. No limiter; automatic protection is response-derived attenuation with 0.5 dB margin.
- Default and minimum EQ window size is 780x460; it may be enlarged.
- Existing user changes in the dirty worktree must be preserved.

---

### Task 1: Core RBJ Program Builder and Response Model

**Files:**
- Create: `core/src/graphic_equalizer.hpp`
- Create: `core/src/graphic_equalizer.cpp`
- Create: `tests/core/graphic_equalizer_test.cpp`
- Modify: `core/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

- [ ] Write failing tests for the ten exact center frequencies, gain/preamp bounds, supported sample rates, non-finite rejection, and Nyquist-safe high-band handling.
- [ ] Write failing response tests proving every individual +6 dB and -6 dB band measures within +/-0.3 dB at its center frequency.
- [ ] Write failing tests for flat response, cascade response, and automatic protection reserving 0.5 dB without attenuating a flat program.
- [ ] Run `cmake --build --preset windows-msvc-debug --target graphic_equalizer_test` and record the expected compile/test failure.
- [ ] Implement `GraphicEqSettings`, `BiquadCoefficients`, `GraphicEqProgram`, fixed frequency constants, RBJ peaking coefficient construction, response evaluation, parameter validation, and protection calculation.
- [ ] Keep coefficient and response math in `double`; normalize `a0`; mark bands above a safe Nyquist ratio inactive rather than emitting unstable coefficients.
- [ ] Run the target test and commit only the new DSP builder/test/CMake hunks.

### Task 2: Allocation-Free Realtime Processor

**Files:**
- Modify: `core/src/graphic_equalizer.hpp`
- Modify: `core/src/graphic_equalizer.cpp`
- Modify: `tests/core/graphic_equalizer_test.cpp`

- [ ] Add failing tests for flat bit-near-transparent output, independent left/right states, impulse stability, NaN/Inf containment, and full-scale finite output.
- [ ] Add failing tests for a 25 ms program crossfade, 25 ms bypass dry/wet crossfade, rapid update coalescing, reset, and sample-rate rebuild.
- [ ] Add an allocation counter around `process()` and prove zero callback-time allocations after construction.
- [ ] Implement a fixed-capacity SPSC program mailbox, two preallocated filter banks, per-channel Direct Form II Transposed state, and newest-program coalescing at block boundaries.
- [ ] Implement per-sample preamp/protection smoothing and equal-power old/new plus dry/wet crossfades; use a flat fast path when every effective gain is zero.
- [ ] Run the DSP test under all five sample rates and commit the realtime processor.

### Task 3: Insert DSP into AudioEngine and Expose Atomic C API

**Files:**
- Modify: `core/src/audio_engine.hpp`
- Modify: `core/src/audio_engine.cpp`
- Modify: `core/src/core_context.hpp`
- Modify: `core/src/core_context.cpp`
- Modify: `core/include/agplayer/c_api.h`
- Modify: `core/src/c_api.cpp`
- Modify: `tests/core/audio_engine_test.cpp`
- Modify: `tests/core/c_api_lifecycle_test.cpp`

- [ ] Add failing engine tests proving EQ is after decode/ring-buffer read and before ReplayGain/volume, and proving flat settings preserve the prior render result.
- [ ] Add failing lifecycle tests for one-call complete snapshots, bounds errors, status reads, load/seek reset, and sample-rate changes.
- [ ] Add `ag_equalizer_settings` and `ag_equalizer_status`; expose one atomic `ag_player_set_equalizer()` call and one status getter.
- [ ] Insert `equalizer_.process(output, frames_read, channels)` directly after `ring_buffer_->read()` in `AudioEngine::Impl::render()`.
- [ ] Rebuild/publish a program on sample-rate change; clear filter state on load, seek, stop, and device reinitialization without blocking the callback.
- [ ] Run core tests plus existing audio-engine/C-API regressions and commit.

### Task 4: Qt Controller, Band Model, Presets, and Persistence

**Files:**
- Create: `qt/src/equalizer_controller.hpp`
- Create: `qt/src/equalizer_controller.cpp`
- Modify: `qt/src/playback_controller.hpp`
- Modify: `qt/src/playback_controller.cpp`
- Modify: `qt/src/qml_registration.hpp`
- Modify: `qt/src/qml_registration.cpp`
- Modify: `qt/CMakeLists.txt`
- Create: `tests/qt/equalizer_controller_test.cpp`
- Modify: `tests/qt/playback_controller_test.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] Add failing Qt tests for ten rows, two-way gain updates, bounds/rounding, enable, bypass, reset, preamp, protection, and complete snapshot delivery.
- [ ] Add failing tests for built-in preset selection and custom preset save/rename/delete, including restart restoration through an isolated QSettings store.
- [ ] Implement a compact `QAbstractListModel` with frequency, label, gain, minimum, maximum, and step roles.
- [ ] Implement built-ins: Flat, Bass Boost, Bass Cut, Vocal Clarity, Treble Boost, Treble Cut, Rock. Presets only update the same real EQ settings.
- [ ] Persist current state, last preset, and custom presets under an `equalizer` QSettings group using atomic value updates; built-ins cannot be renamed/deleted.
- [ ] Route every controller mutation through one debounced complete C-API snapshot and publish protection/status changes back to QML.
- [ ] Run Qt controller/playback tests and commit.

### Task 5: Compact 780x460 Equalizer Window

**Files:**
- Create: `app/qml/AgPlayer/EqualizerWindow.qml`
- Create: `app/qml/AgPlayer/components/EqualizerBandSlider.qml`
- Create: `app/qml/AgPlayer/components/EqualizerResponseCurve.qml`
- Modify: `app/qml/AgPlayer/components/PlayerControls.qml`
- Modify: `app/qml/AgPlayer/Main.qml`
- Modify: `app/main.cpp`
- Modify: `app/CMakeLists.txt`
- Modify: `qt/src/window_controller.hpp`
- Modify: `qt/src/window_controller.cpp`
- Create: `tests/qml/tst_equalizer.qml`
- Create: `tests/qt/qml_equalizer_test_main.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] Add failing QML tests for the EQ entry, one-window behavior, exact 780x460 default/minimum size, ten sliders, preamp, preset actions, bypass, reset, and protection state.
- [ ] Reuse `assets/icons/equalizer-line.svg` beside the existing audio-tools entry; do not draw a replacement icon.
- [ ] Build the compact layout: top command row, response curve, ten vertical bands plus preamp, and bottom protection/status row.
- [ ] Implement double-click reset, wheel 0.1 dB, arrow 0.1 dB, PageUp/PageDown 1 dB, accessible names, visible keyboard focus, and live dB labels.
- [ ] Keep the response curve UI-thread-only and update only on controller changes; it must not poll the audio callback.
- [ ] Register/open/raise a single EQ auxiliary window through `WindowController`; keep it above the player/list window group.
- [ ] Run QML tests, qmllint, and a manual keyboard/mouse smoke test; commit.

### Task 6: Four-Language Text and Visual QA

**Files:**
- Modify: `translations/agplayer_zh.ts`
- Modify: `translations/agplayer_en.ts`
- Modify: `translations/agplayer_th.ts`
- Modify: `translations/agplayer_vi.ts`
- Modify: `scripts/qa-final-ui-matrix.ps1`
- Modify: `design-qa.md`

- [ ] Add all EQ strings in Chinese, English, Thai, and Vietnamese; run the existing UTF-8/translation checks.
- [ ] Extend screenshot QA with the 780x460 EQ window in dark, light, and follow-system modes at 100/125/150/200% DPI.
- [ ] Compare the rendered window with `10段标准EQ均衡器 .png` for hierarchy, alignment, labels, focus, disabled/bypassed state, and clipping; do not count screenshot creation alone as passing.
- [ ] Run translation and visual regression tests and commit.

### Task 7: Measured DSP Evidence and Final Verification

**Files:**
- Create: `tests/core/graphic_equalizer_measurement_test.cpp`
- Create: `scripts/qa-equalizer.ps1`
- Modify: `tests/CMakeLists.txt`
- Modify: `design-qa.md`

- [ ] Generate logarithmic sweep, impulse, full-scale stereo, and independent-channel fixtures in memory; process them through the production processor.
- [ ] Measure all ten bands at +/-6 dB for all five sample rates and export build-artifact CSV with target, measured value, and error.
- [ ] Verify flat transparency, no crosstalk, finite output, smoothing continuity, bypass continuity, protection margin, state restoration, and unchanged input-file hashes for a real music fixture.
- [ ] Run Debug and Release builds, complete test suite, qmllint, and the EQ QA script; archive command output and frequency-response CSV paths in `design-qa.md`.
- [ ] Launch AgPlayer and verify the visible EQ controls change the real main-player output. Mark physical sound-card/headphone listening as pending until a human confirms no click, zipper noise, interruption, or obvious distortion while dragging all bands and switching presets/bypass.
- [ ] Request code review, resolve findings, rerun all affected tests, and commit the final verification evidence.

## Completion Gate

- All automatic tests and measured-response thresholds pass.
- Release build adds no warnings, QML errors, layout loops, or untranslated EQ text.
- The audio callback remains allocation-free and lock-free.
- The 780x460 window is fully operable by mouse and keyboard and follows both effective system skins.
- Actual sound-card/headphone listening is explicitly confirmed; compilation alone is not completion.
- Do not package an EXE until the user separately requests packaging after acceptance.
