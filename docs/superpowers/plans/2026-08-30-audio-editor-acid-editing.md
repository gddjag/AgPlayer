# Audio Editor ACID-Style Editing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the audio editor own playback, restore direct waveform range selection, add independently selectable ACID-style clips, complete editing commands, and replace the primary time/pitch engine with Signalsmith Stretch.

**Architecture:** Keep the existing `AudioDocument`, `EventTimeline`, `EditorPlaybackAdapter`, and `ITimePitchEngine` seams. Add one lightweight clip-selection state to the controller, split QML pointer hit regions by responsibility, and add Signalsmith behind the existing DSP factory with SoundTouch as a fallback.

**Tech Stack:** C++17, Qt 6/QML, CMake, Catch2/QtTest/QuickTest, Signalsmith Stretch 1.3.2, SoundTouch fallback.

**Spec:** `docs/development/audio-editor-acid-editing-design.md`

## Global Constraints

- Work only in `D:/ai/AgPlayer/.worktrees/audio-editor-no-recording-20260828` on `codex/audio-editor-no-recording-20260828`.
- Do not restore recording, package an EXE, push, or merge other worktrees.
- Use decimal string Event IDs across QML; never coerce IDs through JavaScript Number.
- Every behavior change starts with a failing test and is followed by focused regression tests.
- Do not add a second player or a runtime DSP DLL.

---

### Task 1: Playback ownership and first Space

**Files:**
- Modify: `qt/src/audio_editor/audio_editor_controller.hpp`
- Modify: `qt/src/audio_editor/audio_editor_controller.cpp`
- Modify: `app/qml/AgPlayer/Main.qml`
- Modify: `app/qml/AgPlayer/AudioToolsWindow.qml`
- Test: `tests/qt/audio_editor_controller_test.cpp`
- Test: `tests/qml/tst_audio_editor.qml`

**Interfaces:**
- Consumes: `PlaybackController::stop()`, `AudioEditorController::activate()`, `AudioEditorController::playPause()`.
- Produces: `AudioEditorController::editorPlaybackOwnsPlayer() const noexcept` and one-window Space routing.

- [ ] Add a controller test that starts main playback, calls `activate()`, and expects the main controller to be stopped before editor playback is prepared.
- [ ] Run `audio_editor_controller_test` and verify the new ownership test fails because `activate()` only emits a signal.
- [ ] Add a QuickTest that opens Audio Tools with focus on a button, presses Space once, and expects one editor playback transition and no main playback transition.
- [ ] Run `qml_audio_editor_test` and verify the first-Space test fails.
- [ ] Implement ownership acquisition in `activate()`, release in `deactivate()`, and use `Qt.WindowShortcut` for the Audio Tools Space shortcut while disabling the main shortcut when Audio Tools owns playback.
- [ ] Run both focused tests and verify they pass.

### Task 2: Separate range-selection and clip gesture surfaces

**Files:**
- Modify: `app/qml/AgPlayer/components/audioeditor/EditorWaveformCanvas.qml`
- Modify: `qt/src/audio_editor/audio_editor_controller.hpp`
- Modify: `qt/src/audio_editor/audio_editor_controller.cpp`
- Test: `tests/qml/tst_audio_editor_native_input.qml`
- Test: `tests/qt/audio_editor_controller_test.cpp`

**Interfaces:**
- Produces: `Q_PROPERTY(QString selectedEventId READ selectedEventId NOTIFY selectedEventChanged)`, `selectEvent(QString)`, `clearEventSelection()`.
- QML clip strip: object name `editorEventHeaderInteraction`, height 24 logical px.

- [ ] Add native pointer tests proving a drag in waveform body creates a sample range and a drag in the top strip moves the selected event.
- [ ] Run the native QML test and verify body drag fails because `editorEventBodyInteraction` fills the event.
- [ ] Add controller tests for selecting a decimal-string event ID and clearing stale selection after undo.
- [ ] Implement controller clip-selection state and expose it to QML.
- [ ] Replace the full-event move MouseArea with a 24 px top strip; leave the background selection layer reachable below it and retain 9 px trim handles.
- [ ] Add a 1 px clip boundary and selected overlay driven by `selectedEventId`.
- [ ] Run controller and native QML tests.

### Task 3: Exact gain-line hit testing

**Files:**
- Modify: `app/qml/AgPlayer/components/audioeditor/EditorWaveformCanvas.qml`
- Test: `tests/qml/tst_audio_editor_native_input.qml`
- Test: `tests/qt/audio_editor_controller_test.cpp`

**Interfaces:**
- Gain curve hit tolerance: 6 logical px above/below the rendered combined curve.
- Envelope point hit target: existing 18×18 logical px.

- [ ] Add a pointer test that double-clicks 20 px away from the curve and expects no Envelope point.
- [ ] Run the test and verify it fails because the current 24 px MouseArea accepts it.
- [ ] Add a pointer test that double-clicks within 5 px and expects one point, then drags the point in both axes and expects one Undo item.
- [ ] Implement a pointer-position-aware curve hit test; reject presses/double-clicks outside 6 px and let them propagate to range selection.
- [ ] Remove independent top fade-node pointer surfaces; keep fade rendering and curve-menu behavior on central Envelope points.
- [ ] Run QML and controller tests.

### Task 4: ACID-style command targeting

**Files:**
- Modify: `core/src/audio_editor/audio_document.hpp`
- Modify: `core/src/audio_editor/audio_document.cpp`
- Modify: `qt/src/audio_editor/audio_editor_controller.cpp`
- Test: `tests/core/event_edit_test.cpp`
- Test: `tests/qt/audio_editor_controller_test.cpp`

**Interfaces:**
- Produces document operations `copyEvent(EventId)`, `cutEvent(EventId)`, `deleteEvent(EventId)`, `silenceEvent(EventId)`, `fadeEvent(EventId, bool fadeIn)`; each is atomic and Undoable.

- [ ] Add core tests proving selected-event delete/copy/cut/mute/fade affect exactly one split clip and preserve neighboring clips.
- [ ] Run the core test and verify the new APIs are absent.
- [ ] Implement the minimal event-targeted document commands using existing timeline commands and clipboard metadata.
- [ ] Add controller tests for priority: selected event first, time range second, crop range-only, paste at playhead selects the fresh ID.
- [ ] Update `triggerAction()` to follow the priority contract and keep one Undo entry per action.
- [ ] Run core and controller tests.

### Task 5: Export default and two-row shortcuts

**Files:**
- Modify: `qt/src/audio_editor/project_document.cpp`
- Modify: `app/qml/AgPlayer/components/tools/AudioEditorPage.qml`
- Test: `tests/qt/project_document_test.cpp`
- Test: `tests/qml/tst_audio_editor.qml`

**Interfaces:**
- New project `ProjectExportSettings::bitRate = 320000`.
- Shortcut rows remain 13 logical px and expose all groups at 1672×941.

- [ ] Add a project test expecting a fresh document to persist 320000 bit/s and a QuickTest expecting two shortcut rows with all labels reachable.
- [ ] Run both and verify failures against zero bitrate and one Flickable row.
- [ ] Set the persisted default and change the shortcut card to two fixed rows with responsive horizontal Flickables.
- [ ] Run both focused tests.

### Task 6: Signalsmith Stretch engine

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `core/CMakeLists.txt`
- Create: `core/src/signalsmith_time_pitch_engine.cpp`
- Modify: `core/src/time_pitch_engine.cpp`
- Modify: `core/src/time_pitch_engine.hpp`
- Test: `tests/core/time_pitch_session_test.cpp`
- Test: `tests/core/editor_playback_stream_test.cpp`
- Modify: `THIRD-PARTY-NOTICES.md` if present, otherwise create it.

**Interfaces:**
- `create_time_pitch_engine()` returns Signalsmith by default.
- `create_soundtouch_time_pitch_engine()` remains the fallback factory.
- Signalsmith source is pinned to commit `57b93f4e9206a089a45387eaa39bdc9f310d3308` and provides a header-only CMake target.

- [ ] Add DSP tests for 1.0x/0 cent passthrough, 0.75x/1.5x duration, ±12 semitone frequency, finite bounded samples, and realtime/offline parameter parity.
- [ ] Run the tests and record the current SoundTouch baseline; the new engine-identity/parity expectations must fail.
- [ ] Add pinned FetchContent integration and the MIT notice without a runtime DLL.
- [ ] Implement deinterleave/process/interleave buffering, latency pre-roll/flush, tempo ratio, transpose, formant compensation, reset, and a direct passthrough path.
- [ ] Make the factory prefer Signalsmith and fall back to SoundTouch only on configuration failure.
- [ ] Run the DSP tests in Release and Debug.

### Task 7: Full command, visual, and regression gate

**Files:**
- Modify: `tests/qml/tst_audio_editor.qml`
- Modify: `tests/qml/tst_audio_editor_native_input.qml`
- Modify: `design-qa.md`

**Interfaces:**
- No new production API beyond Tasks 1–6.

- [ ] Add an end-to-end QML scenario: import fixture, split, select right clip, copy/paste, trim, mute, fade, delete, undo, range-select, loop, and Space playback ownership.
- [ ] Run Release focused tests and fix only failures caused by this plan.
- [ ] Run Debug focused tests and fix only failures caused by this plan.
- [ ] Run full Release and Debug CTest, `git diff --check`, and a startup smoke.
- [ ] Capture 1672×941, 1280×720, and 880×560 visual evidence; update `design-qa.md` with exact pass/fail scope.
- [ ] Review the final diff for duplicate player/DSP paths, stale fade handles, dead QML branches, and unbounded allocations.
- [ ] Commit as `fix(editor): restore acid-style editing and high-quality stretch` only after the verified diff is clean.
