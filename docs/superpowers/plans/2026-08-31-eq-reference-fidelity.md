# EQ Reference Fidelity Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to execute this plan task by task.

**Goal:** Make AgPlayer's equalizer match the supplied 1672 x 941 reference at pixel-review quality while providing eighteen real EQ bands, a separate preamp, unchanged built-in preset identities/responses, working range and precision controls, and a real post-gain output level meter.

**Architecture:** Extend the existing lock-free graphic-EQ path rather than introducing another audio layer. The core owns an 18-band fixed-frequency DSP contract and publishes a read-only post-gain peak. `EqualizerController` owns preset/schema migration plus user-edit range and precision. QML remains a view over those real properties and commands.

**Tech Stack:** C++17, Qt 6/QML, QSettings, existing C API and audio callback, Catch2/Qt Test, CMake/MSVC.

**Spec:** `docs/development/2026-08-31-eq-reference-fidelity-design.md`

## Global Constraints

- The canonical bands are exactly `20, 31.5, 50, 80, 125, 200, 315, 500, 800, 1250, 2000, 3150, 5000, 8000, 10000, 12500, 16000, 20000` Hz, plus a separate preamp.
- Keep the eight built-in preset IDs and Chinese-facing names unchanged. Insert `0.0 dB` at 10 kHz so each preset preserves its previous audible response.
- Migrate stored 17-band data by inserting `0.0 dB` at 10 kHz. Migrate legacy 10-band data through the former 17-band interpolation and then insert `0.0 dB` at 10 kHz.
- DSP accepts `-18..+18 dB`. UI range options are exactly `6`, `12`, and `18 dB`, default `12 dB`.
- Precision modes are exactly high/medium/low with edit steps `0.1`, `0.5`, and `1.0 dB`; changing precision does not rewrite existing gains.
- Output level is measured from actual interleaved samples after EQ, replay gain, volume, mute, and transition fade, before spectrum/device handoff. It is a sample-peak meter, not a true-peak limiter.
- Do not allocate, lock, log, or call Qt from the audio callback. Keep the existing three-slot program mailbox and 25 ms EQ transition.
- Reuse existing SVG icon assets and current components. Add no third-party dependency and no handcrafted/fake visual asset.
- Preserve bypass and automatic clipping protection as real advanced controls even though they are not on the reference's primary toolbar.
- Do not package or claim hardware playback validation in this task.
- Use tests first for every behavior change, keep unrelated files untouched, and commit each task independently on `codex/eq-18band-reference`.

## Task 1: Make the core contract truly eighteen-band

**Files:**

- Modify: `tests/core/graphic_equalizer_test.cpp`
- Modify: `core/src/graphic_equalizer.hpp`
- Modify: `core/src/graphic_equalizer.cpp`
- Modify: `core/include/agplayer/c_api.h`

1. Extend the frequency-contract test to expect all 18 frequencies in order, including 10 kHz between 8 kHz and 12.5 kHz; add boundary assertions proving `-18` and `+18 dB` are accepted while values outside are rejected.
2. Add a focused 10 kHz processing test that prepares one non-zero 10 kHz band and confirms a 10 kHz sine's steady-state RMS changes in the expected direction.
3. Run `cmake --build build/release --target graphic_equalizer_test --parallel 4` and `ctest --test-dir build/release -C Release --output-on-failure -R ^graphic_equalizer_test$`; record the failing evidence before implementation.
4. Set `kGraphicEqBandCount` and `AG_EQUALIZER_BAND_COUNT` to 18, insert 10000 Hz at index 14, and widen validation/clamping to +/-18 dB without changing Q, mailbox, transition, or channel limits.
5. Rebuild and rerun the focused test until green, then inspect the diff for accidental API-layout or real-time-path changes.
6. Commit as `feat(eq): add real 10 kHz band`.

## Task 2: Preserve presets and add persisted range/precision controls

**Files:**

- Modify: `tests/qt/equalizer_controller_test.cpp`
- Modify: `qt/src/equalizer_controller.hpp`
- Modify: `qt/src/equalizer_controller.cpp`

1. Update preset tests to expect 18 gains and assert every built-in has exactly `0.0 dB` at index 14 while all prior indices retain their exact values.
2. Add migration tests for schema-2 17-band settings and legacy 10-band settings; both must produce 18 values with an inserted zero at 10 kHz and preserve user/custom-preset data.
3. Add controller tests for persisted range `6/12/18`, default 12, clamping all band gains and preamp in one logical change when the range shrinks, and rejecting unsupported values without corrupting state.
4. Add precision tests for high/medium/low steps `0.1/0.5/1.0`, persistence, edit quantization, and no retroactive value rewrite when precision changes.
5. Run the focused controller test and record its RED result.
6. Upgrade the settings schema to 3; implement explicit 17-to-18 insertion and 10-to-old-17 interpolation followed by insertion. Expand all built-ins with a zero at 10 kHz.
7. Add `gainRangeDb`, `precisionMode`, and `gainStepDb` properties/signals/invokables. Apply current range/step only to user edits; loading and preset application preserve exact valid DSP values. Range reduction clamps bands/preamp and submits one full snapshot.
8. Rebuild and rerun `equalizer_controller_test`; inspect persistence and signal-emission behavior, then commit as `feat(eq): add range precision and schema migration`.

## Task 3: Publish a real post-gain output level meter

**Files:**

- Modify: `tests/core/audio_engine_test.cpp`
- Modify: `core/include/agplayer/c_api.h`
- Modify: `core/src/audio_engine.cpp`
- Modify: `core/src/audio_engine.hpp`
- Modify: `core/src/core_context.cpp`
- Modify: `core/src/c_api.cpp`
- Modify: `tests/qt/equalizer_controller_test.cpp`
- Modify: `qt/src/equalizer_controller.hpp`
- Modify: `qt/src/equalizer_controller.cpp`

1. Add an integration test that plays the existing audio fixture through the null backend, polls `ag_player_equalizer_status`, and proves `output_peak_db` becomes finite/non-silent while playing, decreases materially after volume reduction, and returns to the floor when muted/stopped.
2. Add controller tests proving `refreshStatus()` exposes the C API meter value through an `outputPeakDb` property and emits only when the visible value changes.
3. Run the two focused tests and record the failing evidence.
4. In the existing final sample-scaling loop, accumulate absolute sample peak with no extra pass or allocation. Store it in an atomic with immediate attack and approximately 12 dB/second release; reset/expose the floor as `-120 dB` outside active playback.
5. Append `output_peak_db` to `ag_equalizer_status`, thread it through `CoreContext`/C API, and expose it from `EqualizerController`. Keep status reads lock-free and do not label the meter true-peak.
6. Rebuild and rerun core/controller tests; inspect callback code for allocations, locks, logging, and division-by-zero, then commit as `feat(eq): add post gain output meter`.

## Task 4: Rebuild the QML surface to the supplied reference

**Files:**

- Modify: `tests/qml/tst_equalizer_visual.qml`
- Modify: `tests/qml/tst_main_window.qml`
- Modify: `app/qml/AgPlayer/EqualizerWindow.qml`
- Modify: `app/qml/AgPlayer/components/EqualizerBandSlider.qml`
- Modify: `app/qml/AgPlayer/components/EqualizerResponseCurve.qml`
- Modify: `translations/agplayer_zh.ts`

1. Extend the QML test state to the exact reference values: eighteen gains `2, 1.5, 0, -1, 0.5, -0.5, -1.5, -0.5, 0.5, 1.5, 2, 1, 2, 1.5, 1, 0, -1, -2`, preamp `-1.5`, range `12`, precision high, enabled true, at `1672 x 941`.
2. Add interaction assertions for enable, preset selection, save, manage, reset, range, precision, band keyboard/wheel/drag semantics, preamp, and status-meter refresh. Verify advanced bypass/auto-protection controls remain reachable in Manage.
   Update the existing main-window EQ contract from 17 to 18 bands and keep its interaction assertions aligned with the rebuilt surface.
3. Run/build `qml_main_window_test` and `qml_equalizer_visual_test`; record the RED result before QML implementation.
4. Match the reference's title bar, 72 px header rhythm, toolbar geometry, response panel, 18-band plus preamp slider grid, separators, selected segmented controls, footer labels, meter blocks, colors, radii, typography, and spacing. Reuse `save-3-line.svg`, `list-unordered.svg`, and `arrow-go-back-line.svg`.
5. Bind every visible control to `EqualizerController`; poll `refreshStatus()` at 30 Hz only while the window is visible. Use the same 18-band frequency array and selected range in both response curve and sliders.
6. Preserve responsive behavior at existing 1180 x 680 and 880 x 520 acceptance sizes without horizontal clipping or loss of controls.
7. Update translations, rebuild, and rerun QML tests; commit as `feat(eq): match 18 band reference interface`.

## Task 5: Visual QA, regression verification, and development record

**Files:**

- Modify: `docs/development/2026-08-31-eq-reference-fidelity-design.md`
- Create: `docs/development/2026-08-31-eq-reference-fidelity-verification.md`
- Create: `design-qa.md`

1. Build the Release app and all EQ-related tests from a fresh MSVC environment.
2. Run focused core, controller, and QML tests, then the complete test suite. Record exact pass/fail counts and any unrelated pre-existing failures without broadening scope.
3. Capture the implementation at exactly `1672 x 941` in the same custom state as the reference. Place reference and implementation side-by-side in one comparison image.
4. Perform senior-design QA against that combined image. Fix every P0/P1/P2 mismatch, repeat capture/comparison, and write `design-qa.md` with the exact final line `final result: passed` or `final result: blocked`.
5. Verify range/precision/reset/save/manage/preset/bypass/protection interactions and real PCM output-meter behavior; do not substitute visual-only assertions for DSP evidence.
6. Run whitespace/diff review, inspect every changed file, and document what was validated plus remaining hardware/true-peak limitations.
7. Commit as `test(eq): verify reference fidelity and interactions`.
