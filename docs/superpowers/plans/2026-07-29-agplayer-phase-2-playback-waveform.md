# AgPlayer Phase 2 Playback and Waveform Implementation Plan

> **For Codex:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Make imported tracks reliably play through the real Qt/C++ controller path, keep queue identity correct when library rows are filtered or unavailable, and provide a responsive, seekable waveform in every supported display mode.

**Architecture:** Keep `PlaybackController` as the only QML playback façade and the C API/core audio engine as the only playback state owner. Resolve UI metadata by stable `trackId`, not queue index. Keep waveform decoding/caching in `WaveformProvider` and rendering/pointer interaction in `WaveformItem`; QML only selects layers and forwards committed seeks.

**Tech Stack:** Qt 6 Quick/QML, C++17, Qt Test/Quick Test, existing FFmpeg/miniaudio core, CMake/CTest.

---

## Task 1: Adopt and verify the pending identity and waveform rendering changes

**Files:**
- Modify: `qt/src/library_model.hpp`
- Modify: `qt/src/library_model.cpp`
- Modify: `tests/qt/library_model_test.cpp`
- Modify: `qt/src/waveform_item.cpp`
- Modify: `tests/qt/waveform_item_test.cpp`
- Modify: `app/qml/AgPlayer/components/PlayerPane.qml`

- [ ] Verify `LibraryModel.count` and `indexForTrackId()` update correctly across append, duplicate rejection, and reset.
- [ ] Verify the player pane uses `PlaybackController.currentTrackId` instead of treating queue index as a library row.
- [ ] Verify the reduced waveform geometry budget preserves visual coverage and lowers render work.
- [ ] Run the focused C++ and QML tests before accepting the pending edits.

## Task 2: Connect committed waveform interaction to real playback seek

**Files:**
- Modify: `tests/qml/tst_main_window.qml`
- Modify: `app/qml/AgPlayer/components/PlayerPane.qml`

- [ ] Add a failing main-window test that proves a click/drag on `mainWaveform` changes `PlaybackController.positionMs`.
- [ ] Forward only the waveform's committed `seekRequested` signal to `PlaybackController.seek()`.
- [ ] Confirm hover and cancelled drags do not seek.
- [ ] Run `qml_main_window_test`, `qml_waveform_test`, and `waveform_item_test`.

## Task 3: Prove the playback queue and controls end to end

**Files:**
- Modify: `tests/qt/playback_controller_test.cpp`
- Modify: `tests/qml/tst_main_window.qml`
- Modify only if a test exposes a defect: `qt/src/playback_controller.cpp`
- Modify only if a test exposes a defect: `app/qml/AgPlayer/components/PlayerControls.qml`

- [ ] Add coverage for play/pause, seek, next, previous, volume, mute, and playback-mode transitions using generated audio fixtures.
- [ ] Prove unavailable library rows do not corrupt queue index or displayed metadata.
- [ ] Prove all visible player controls invoke the shared controller rather than local placeholder state.
- [ ] Make the smallest root-cause fix for any failure; do not add another playback abstraction.

## Task 4: Complete waveform modes, caching, and stale-result handling

**Files:**
- Modify: `tests/qt/waveform_item_test.cpp`
- Modify: `tests/qml/tst_main_window.qml`
- Modify only if required: `qt/src/waveform_provider.cpp`
- Modify only if required: `app/qml/AgPlayer/components/PlayerPane.qml`

- [ ] Prove pure/RGB/frequency settings select valid layers and update without re-decoding the current track.
- [ ] Prove stale asynchronous results cannot replace the current track waveform.
- [ ] Prove empty or failed tracks clear the waveform and expose no stale interaction range.
- [ ] Confirm cached loads and large peak arrays remain bounded and responsive.

## Task 5: Phase 2 verification and acceptance record

**Files:**
- Modify: `docs/qa/phase-status.md`
- Reuse: `scripts/qa-main-smoke.ps1`
- Reuse: `scripts/qa-library-smoke.ps1`

- [ ] Build Debug with zero compiler warnings/errors.
- [ ] Run all non-stress CTest tests.
- [ ] Run the production executable with real generated audio, import it, start playback, wait for waveform analysis, and capture logs/screenshots.
- [ ] Require zero QML/runtime warning, error, or fatal log lines.
- [ ] Record automated evidence and explicitly leave audible human hardware confirmation open if it cannot be observed automatically.
- [ ] Commit the verified Phase 2 implementation; do not package an EXE.
