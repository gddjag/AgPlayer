# AgPlayer Critical Player and Audio-Tools Repair Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Repair the remaining waveform, shared player/list layout, colour, editor, separation, metadata, and playback-ownership defects from the 2026-09-03 acceptance report.

**Architecture:** Preserve the existing Qt 6 QML/C++ controllers and deepen their shared contracts: one waveform renderer, one shared media-row layout, one native colour popup, recursive catalog discovery, and explicit playback ownership. Behavioural changes are test-first and UI changes are verified with native screenshots against the supplied references.

**Tech Stack:** Qt 6 Quick/QML, C++17, QSG geometry, Qt Multimedia, FFmpeg-backed metadata pipeline, native separation worker, Qt Test/Quick Test, CMake/CTest.

**Spec:** `docs/superpowers/specs/2026-09-03-critical-player-and-tools-repair-design.md`

## Global Constraints

- Keep playback, library, playlist, tag, waveform cache, and theme data models intact.
- Do not add WebEngine, a second waveform analyser, a second audio player, or a heavyweight dependency.
- Preserve the three frequency colours exactly: `#FC0909`, `#03FF00`, `#0048FF`.
- Use `Theme.qml` tokens and shared themed controls; screenshot annotations are never UI content.
- Metadata force mode must remain atomic and preserve the original on verification failure.

---

### Task 1: Full-width waveform and smooth theme-aware progress

**Files:**
- Modify: `qt/src/waveform_item.cpp`
- Modify: `app/qml/AgPlayer/theme/Theme.qml`
- Modify: `app/qml/AgPlayer/components/PlayerPane.qml`
- Modify: `app/qml/AgPlayer/components/MiniPlayerControls.qml`
- Modify: `app/qml/AgPlayer/components/RollingPlayerShell.qml`
- Modify: `qt/src/frequency_color_waveform_settings.*`
- Test: `tests/qt/waveform_item_test.cpp`
- Test: `tests/qml/tst_main_window.qml`
- Test: `tests/qml/tst_mini_player.qml`
- Test: `tests/qml/tst_rolling_theme.qml`

**Interfaces:**
- Consumes: `WaveformItem::waveformCursorX`, `FrequencyColorWaveformSettings::unplayedOpacity`.
- Produces: a complete edge-to-edge vertex span and a clipped played overlay for visual mode 3.

- [ ] **Step 1: Write failing edge and progress tests**

Assert that the first/last visible frequency vertices cover the first/last
device pixel and that QML uses an all-unplayed base plus a played clip whose
width equals `waveformCursorX` in visual mode 3.

- [ ] **Step 2: Run the focused RED tests**

Run: `ctest --test-dir build/release -C Release -R "^(waveform_item_test|qml_main_window_test|qml_mini_player_test|qml_rolling_theme_test)$" --output-on-failure`

Expected: the new edge/visual-mode-3 overlay assertions fail.

- [ ] **Step 3: Implement the minimal renderer and QML fix**

Map waveform stroke copies to the complete drawable pixel interval, render the
base pass with position zero, and show the existing clipped played pass for all
waveform modes. Derive dark/light unplayed presentation values from the stored
contrast without changing frequency RGB.

- [ ] **Step 4: Re-run the focused tests**

Expected: all four focused targets pass.

### Task 2: Player chrome, lyrics docking, and shared media-row geometry

**Files:**
- Modify: `app/qml/AgPlayer/Main.qml`
- Modify: `app/qml/AgPlayer/ListWindow.qml`
- Modify: `app/qml/AgPlayer/components/PlayerPane.qml`
- Modify: `app/qml/AgPlayer/components/PlayerControls.qml`
- Modify: `app/qml/AgPlayer/components/IntegratedPlayerShell.qml`
- Modify: `app/qml/AgPlayer/components/RollingPlayerShell.qml`
- Modify: `app/qml/AgPlayer/components/TrackList.qml`
- Modify: `app/qml/AgPlayer/components/SearchFilter.qml`
- Modify: `app/qml/AgPlayer/theme/Theme.qml`
- Test: `tests/qml/tst_main_window.qml`
- Test: `tests/qml/tst_integrated_theme.qml`
- Test: `tests/qml/tst_rolling_theme.qml`
- Test: `tests/qml/tst_player_controls.qml`

**Interfaces:**
- Consumes: existing `TrackList` model roles and `WindowController` docking.
- Produces: fixed trailing columns, flexible media columns, shared cover-centred vertical layout, and a right-side classic lyrics panel.

- [ ] **Step 1: Add failing QML geometry assertions**

At normal and minimum widths assert stable duration/rating/favourite/BPM cell
widths, cover-centred content, symmetric outer control insets, compact context
menus, centred BPM filter contents, and classic lyrics height equal to the
docked player-plus-list pair.

- [ ] **Step 2: Run the shell/list RED tests**

Run: `ctest --test-dir build/release -C Release -R "^(qml_main_window_test|qml_integrated_theme_test|qml_rolling_theme_test|qml_player_controls_layout_test)$" --output-on-failure`

- [ ] **Step 3: Apply shared layout/token changes**

Use `RowLayout` alignment against cover height, fixed trailing `Layout.preferredWidth`
cells, flexible title/waveform cells, 12 px secondary text, symmetric 12 px
outer action insets, and shared title/content surfaces.

- [ ] **Step 4: Verify normal/minimum window geometry**

Expected: all shell/list tests pass without hiding required controls.

### Task 3: Rolling eight-beat viewport and coordinate mapping

**Files:**
- Modify: `app/qml/AgPlayer/components/RollingPlayerShell.qml`
- Modify: `qt/src/waveform_coordinate_mapper.*` only if the existing mapper cannot express the interval.
- Test: `tests/qml/tst_rolling_theme.qml`
- Test: `tests/qt/waveform_coordinate_mapper_test.cpp`

**Interfaces:**
- Consumes: playback BPM, duration, position, `WaveformItem::setVisibleRange`, `pixelForTime`, `timeForX`.
- Produces: `viewportSpanMs = 8 * 60000 / bpm` with bounded start/end and centre-guided seek.

- [ ] **Step 1: Add failing viewport, drag-seek, capsule, and overview tests**

Test start/middle/end positions, 60/120/180 BPM, pointer drag release, overview
hover, and the single BPM value contract.

- [ ] **Step 2: Run RED tests**

Run: `ctest --test-dir build/release -C Release -R "^(qml_rolling_theme_test|waveform_coordinate_mapper_test)$" --output-on-failure`

- [ ] **Step 3: Implement the bounded eight-beat viewport**

Keep the waveform item's pixel density fixed, change only `visibleStartMs` and
`visibleEndMs`, map drag delta through the same interval, and position the time
capsule from the fixed centre guide.

- [ ] **Step 4: Run GREEN tests**

Expected: rolling and mapper tests pass.

### Task 4: Shared colour picker and compact tag capsule

**Files:**
- Modify: `app/qml/AgPlayer/components/ColorField.qml`
- Modify: `app/qml/AgPlayer/components/TagManagementPanel.qml`
- Modify: `app/qml/AgPlayer/SettingsPage.qml`
- Test: `tests/qml/tst_main_window.qml`

**Interfaces:**
- Consumes: `ColorField.colorValue`, `defaultColor`, `colorEdited`.
- Produces: one fitted popup with an editable normalized hex field and compact digit-sized tag counts.

- [ ] **Step 1: Add failing popup bounds, paste, reuse, and capsule width tests**
- [ ] **Step 2: Run the relevant `qml_main_window_test` functions and record RED**
- [ ] **Step 3: Fit popup content, route tag editing through `ColorField`, and remove light shadow**
- [ ] **Step 4: Re-run QML tests and capture dark/light settings/tag screenshots**

### Task 5: Audio-editor trim and zoom rail correctness

**Files:**
- Modify: `app/qml/AgPlayer/components/audioeditor/EditorWaveformCanvas.qml`
- Modify: `app/qml/AgPlayer/components/tools/AudioEditorPage.qml`
- Modify: `qt/src/audio_editor/audio_editor_controller.cpp`
- Modify: `core/src/audio_editor/event_timeline.cpp` if rebasing is not already provided.
- Test: `tests/qml/tst_audio_editor.qml`
- Test: `tests/qml/tst_audio_editor_native_input.qml`
- Test: `tests/qt/audio_editor_controller_test.cpp`

**Interfaces:**
- Consumes: current trim commands and editor document duration.
- Produces: left trim rebased to zero, right trim shortened in place, and a handle-free 30% zoom rail.

- [ ] **Step 1: Add failing left/right trim and zoom-rail tests**
- [ ] **Step 2: Run editor RED tests**
- [ ] **Step 3: Reuse the existing document move/rebase command and correct QML styling**
- [ ] **Step 4: Run editor GREEN tests and capture the reference state**

### Task 6: Recursive separation catalog, runtime capability, and waveform recovery

**Files:**
- Modify: `qt/src/vocal_separation_catalog.*`
- Modify: `qt/src/vocal_separation_controller.*`
- Modify: `qt/src/separation_process_client.*`
- Modify: `app/qml/AgPlayer/components/tools/VocalSeparationPage.qml`
- Test: `tests/qt/vocal_separation_catalog_test.cpp`
- Test: `tests/qt/vocal_separation_controller_test.cpp`
- Test: `tests/qml/tst_vocal_separation.qml`

**Interfaces:**
- Consumes: configured model root, supported `.onnx/.pth/.th` descriptors, runtime/device probes, waveform provider.
- Produces: recursive `DetectedModel` rows carrying path/provider/runtime/stems/runnable reason and bounded download controls with retry.

- [ ] **Step 1: Add failing nested-directory, multi-model, missing-runtime, auto-device, retry, and waveform tests**
- [ ] **Step 2: Run separation RED tests**
- [ ] **Step 3: Implement recursive discovery and honest capability selection**
- [ ] **Step 4: Restore source/stem waveform requests and keep all card controls inside bounds**
- [ ] **Step 5: Run separation GREEN tests plus one real short-file CPU smoke; run GPU smoke only when the probe reports support**

### Task 7: Atomic metadata force write and playback ownership

**Files:**
- Modify: `qt/src/metadata_editor.cpp`
- Modify: `core/src/metadata_writer.cpp`
- Modify: `qt/src/audio_preview_controller.cpp`
- Modify: `app/main.cpp`
- Test: `tests/qt/metadata_editor_test.cpp`
- Test: `tests/core/metadata_writer_test.cpp`
- Test: `tests/qt/audio_preview_controller_test.cpp`
- Test: `tests/qt/playback_controller_test.cpp`

**Interfaces:**
- Consumes: staged metadata write, decoder probe, main playback controller, tool preview controller.
- Produces: atomic verified replacement and explicit exclusive playback ownership signals.

- [ ] **Step 1: Add failing tests for the reported reread path and bidirectional playback interruption**
- [ ] **Step 2: Run metadata/playback RED tests**
- [ ] **Step 3: Fix the failing verification stage without weakening stream integrity and connect ownership stops in `app/main.cpp`**
- [ ] **Step 4: Run metadata/playback GREEN tests**

### Task 8: Final review, native screenshots, and full verification

**Files:**
- Modify: `docs/development/2026-09-03-critical-player-and-tools-repair.md`
- Modify: `design-qa.md`

**Interfaces:**
- Consumes: Tasks 1-7.
- Produces: reproducible acceptance evidence and a reviewed diff.

- [ ] **Step 1: Build and lint**

Run: `cmake --build build/release --config Release --parallel 4`

Run: `cmake --build build/release --target all_qmllint --parallel 4`

- [ ] **Step 2: Run all Release tests**

Run: `ctest --test-dir build/release -C Release -j1 --output-on-failure`

- [ ] **Step 3: Capture and compare required native UI states**

Capture classic, mini, integrated, rolling, settings picker/tag, editor, and
separation in dark/light at matching viewports. Compare each reference and
implementation image together; fix P0/P1/P2 issues.

- [ ] **Step 4: Review the final diff**

Run: `git diff --check`

Inspect every changed file for unrelated changes, duplicate abstractions,
lifecycle mistakes, redraw hot loops, and unverified claims.

- [ ] **Step 5: Record acceptance**

Write exact commands/results and remaining hardware/runtime risks. Set
`design-qa.md` to `final result: passed` only after the visual gate passes.

