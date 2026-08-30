# Audio Editor No-Recording Responsive Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver the reference-matched, responsive AgPlayer audio editor with recording fully removed and timeline playback/editing made reliable.

**Architecture:** Keep `AudioDocument`, `EventTimeline`, `EditorPlaybackAdapter`, `EditorViewport`, and the existing visible-peak pipeline. Remove the recording branch end-to-end, then add narrow document/controller contracts for timeline clearing and shared split-boundary trim; QML consumes those contracts through the existing decimal-string Event IDs. Window DPI handling remains in `WindowController`, while page layout becomes continuously responsive instead of switching at one breakpoint.

**Tech Stack:** C++17, Qt 6/QML, Qt Test, CMake/CTest, PowerShell visual-contract scripts.

**Spec:** `docs/superpowers/specs/2026-08-28-audio-editor-no-recording-responsive-design.md`

## Global Constraints

- Use `C:\Users\Administrator\Desktop\音视频播放器\AgPlayer音频播放器完整版\音频编辑.png` as the only visual reference.
- Keep four real tool tabs; do not restore the deleted fake vocal-separation installer page.
- Add no third-party dependencies and do not mix format-converter changes into this branch.
- Preserve decimal-string Event IDs across the QML boundary.
- One pointer gesture must produce at most one Undo item.
- Visible peak buckets must remain bounded by `2 × logical viewport width × DPR`.
- Do not report pixel parity until reference/candidate comparison at the same viewport passes design QA.

---

### Task 1: Remove the recording subsystem end-to-end

**Files:**
- Delete: `core/src/audio_editor/recording_session.cpp`
- Delete: `core/src/audio_editor/recording_session.hpp`
- Delete: `tests/core/recording_session_test.cpp`
- Delete: `tests/core/recording_hardware_smoke.cpp`
- Delete: `tests/qt/manual_recording_capture.hpp`
- Modify: `CMakeLists.txt`
- Modify: `core/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`
- Modify: `qt/src/audio_editor/audio_editor_controller.hpp`
- Modify: `qt/src/audio_editor/audio_editor_controller.cpp`
- Modify: `qt/src/audio_editor/editor_action_model.cpp`
- Modify: `app/qml/AgPlayer/components/tools/AudioEditorPage.qml`
- Modify: `app/qml/AgPlayer/components/audioeditor/EditorWaveformCanvas.qml`
- Modify: `app/qml/AgPlayer/theme/Theme.qml`
- Modify: `qt/src/theme_manager.hpp`
- Modify: `qt/src/theme_manager.cpp`
- Test: `tests/qt/audio_editor_controller_test.cpp`
- Test: `tests/qml/tst_audio_editor.qml`
- Test: `tests/scripts/audio_tools_layout_contract_test.ps1`
- Test: `tests/cmake/audio_editor_feature_options_test.cmake`

**Interfaces:**
- Consumes: existing non-recording `AudioEditorController` document/playback/export contracts.
- Produces: a controller and QML surface with no `RecordingSession`, recording state, recording property, action, signal, shortcut, control, or build option.

- [ ] **Step 1: Add failing absence contracts**

Add QML assertions:

```qml
compare(findChild(page, "editorRecordingTransport"), null)
compare(findChild(page, "inspectorRecordingGroup"), null)
compare(findChild(page, "editorRecordShortcut"), null)
compare(findChild(page, "editorPauseRecordingShortcut"), null)
compare(findChild(page, "editorStopRecordingShortcut"), null)
```

Add a PowerShell production scan that fails if any production file contains:

```powershell
$forbidden = @('RecordingSession', 'recordingSupported', 'startRecording',
               'editor.newRecording', 'editorRecordingTransport',
               'inspectorRecordingGroup')
```

- [ ] **Step 2: Run the RED tests**

Run:

```powershell
ctest --test-dir build/msvc-release -R "qml_audio_tools_test|audio_tools_layout_contract" --output-on-failure
```

Expected: FAIL because recording QML and controller symbols still exist.

- [ ] **Step 3: Delete recording-only code and simplify shared paths**

Delete the files and all recording fields/methods. Simplify viewport waveform generation to use only:

```cpp
const qint64 totalFrames = has_document_ ? document_.totalFrames() : 0;
const int renderChannels = 1;
```

Keep `miniaudio`, `generatedMediaDirectory()`, `uniqueGeneratedMediaPath()`, `insertSourceAtCursor()`, `Processing`, and shared QtConcurrent infrastructure.

- [ ] **Step 4: Run focused build and tests**

Run:

```powershell
cmake --build build/msvc-release --config Release --target agplayer_core agplayer_qt qml_audio_tools_test audio_editor_controller_test
ctest --test-dir build/msvc-release -R "audio_editor_controller_test|qml_audio_tools_test|audio_tools_layout_contract|audio_editor_feature_options" --output-on-failure
```

Expected: all selected tests PASS and the production scan finds zero forbidden recording symbols.

- [ ] **Step 5: Commit**

```powershell
git add -A
git commit -m "refactor(editor): remove recording subsystem"
```

---

### Task 2: Rebuild the reference layout and global Space transport

**Files:**
- Modify: `app/qml/AgPlayer/AudioToolsWindow.qml`
- Modify: `app/qml/AgPlayer/components/tools/ToolSidebar.qml`
- Modify: `app/qml/AgPlayer/components/tools/AudioEditorPage.qml`
- Modify: `app/qml/AgPlayer/components/audioeditor/FileSummaryBar.qml`
- Test: `tests/qml/tst_audio_editor.qml`
- Test: `tests/qt/audio_tools_end_to_end_test.cpp`

**Interfaces:**
- Consumes: recording-free `AudioEditorController`, `AudioToolsController.currentTool`, existing transport methods.
- Produces: continuous `mainWidth`, 320–372 px Inspector, A–D groups, full-width transport, and one window-level Space shortcut.

- [ ] **Step 1: Replace legacy geometry tests with RED reference/responsive tests**

Assert the `1672×849` content geometry from the reference, including:

```qml
verifyGeometry("editorMainColumn", 0, 0, 1328, 849)
verifyGeometry("editorInspector", 1328, 0, 344, 849)
compare(findChild(page, "inspectorTempoTitle").text, "A. 速度 / BPM")
compare(findChild(page, "inspectorPitchTitle").text, "B. 升调降调")
compare(findChild(page, "inspectorPreservePitchTitle").text, "C. 保持音调")
compare(findChild(page, "inspectorExportTitle").text, "D. 导出设置")
```

For widths `1800`, `1672`, `1500`, `1280`, and `880`, assert `main.width + visibleInspector.width === page.width` within 1 px and all transport/export controls remain reachable.

Add a focus regression that focuses command buttons, transport buttons, a slider, and a ComboBox, presses Space, and verifies only `AudioEditorController.playPause()` changes state.

- [ ] **Step 2: Run RED QML tests**

```powershell
ctest --test-dir build/msvc-release -R "qml_audio_tools_test|audio_tools_end_to_end_test" --output-on-failure
```

Expected: FAIL on old A–E layout, split transport, breakpoint clipping, and focus-bound Space behavior.

- [ ] **Step 3: Implement continuous layout and full transport**

Use continuous properties instead of `referenceLayout` for width allocation:

```qml
readonly property bool compactLayout: width < 1000
readonly property real inspectorWidth: compactLayout ? 350
    : Math.max(320, Math.min(372, width * 0.215))
readonly property real mainWidth: compactLayout ? width : width - inspectorWidth
```

Compute waveform height from available page height. Rebuild the bottom row with current time at left and previous/back/play/forward/next/stop controls. Set non-input buttons to `Qt.NoFocus` where keyboard focus is not required.

Move the unique Space shortcut into `AudioToolsWindow.qml` with application context, enabled only while the audio editor is current and no editable text control is composing.

- [ ] **Step 4: Run focused QML tests**

```powershell
cmake --build build/msvc-release --config Release --target qml_audio_tools_test audio_tools_end_to_end_test
ctest --test-dir build/msvc-release -R "qml_audio_tools_test|audio_tools_end_to_end_test" --output-on-failure
```

Expected: PASS at all tested sizes and focus targets.

- [ ] **Step 5: Commit**

```powershell
git add app/qml/AgPlayer tests/qml/tst_audio_editor.qml tests/qt/audio_tools_end_to_end_test.cpp
git commit -m "feat(editor): rebuild responsive reference layout"
```

---

### Task 3: Stabilize cross-screen window geometry

**Files:**
- Modify: `app/qml/AgPlayer/AudioToolsWindow.qml`
- Modify: `qt/src/window_controller.hpp`
- Modify: `qt/src/window_controller.cpp`
- Test: `tests/qt/window_controller_test.cpp`

**Interfaces:**
- Consumes: `WindowController::geometryForDpiChange`, registered audio-tools `QWindow`.
- Produces: native title-bar system move and audio-tools DPI changes that preserve logical size while accepting the OS suggested position.

- [ ] **Step 1: Add RED DPI and move tests**

Add a table-driven test for 100%, 125%, and 150% transitions verifying:

```cpp
QCOMPARE(result.size(), currentLogicalSize);
QVERIFY(targetAvailableGeometry.contains(result.topLeft()));
```

Update the QML contract to require `startSystemMove()` and forbid manual `window.x +=` / `window.y +=` dragging.

- [ ] **Step 2: Run RED tests**

```powershell
ctest --test-dir build/msvc-release -R "window_controller_test|audio_tools_layout_contract" --output-on-failure
```

Expected: FAIL because audio tools currently preserve the previous native pixel size and drag by QML coordinate deltas.

- [ ] **Step 3: Implement native movement and logical-size DPI handling**

On title-bar drag call:

```qml
if (window.visibility !== Window.Maximized)
    window.startSystemMove()
```

Remove the audio-tools-specific native-pixel-size restoration from `WM_DPICHANGED`; use the suggested position and derive the target native size from the current logical size and new DPR.

- [ ] **Step 4: Run window tests**

```powershell
cmake --build build/msvc-release --config Release --target window_controller_test qml_audio_tools_test
ctest --test-dir build/msvc-release -R "window_controller_test|qml_audio_tools_test|audio_tools_layout_contract" --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit**

```powershell
git add app/qml/AgPlayer/AudioToolsWindow.qml qt/src/window_controller.* tests/qt/window_controller_test.cpp
git commit -m "fix(windows): stabilize audio tools across displays"
```

---

### Task 4: Make clear, selection looping, and split-boundary trim transactional

**Files:**
- Modify: `core/src/audio_editor/audio_document.hpp`
- Modify: `core/src/audio_editor/audio_document.cpp`
- Modify: `qt/src/audio_editor/audio_editor_controller.hpp`
- Modify: `qt/src/audio_editor/audio_editor_controller.cpp`
- Modify: `app/qml/AgPlayer/components/audioeditor/EditorCommandBar.qml`
- Modify: `app/qml/AgPlayer/components/audioeditor/EditorWaveformCanvas.qml`
- Test: `tests/core/audio_document_test.cpp`
- Test: `tests/qt/audio_editor_controller_test.cpp`
- Test: `tests/qml/tst_audio_editor.qml`

**Interfaces:**
- Consumes: `AudioDocument::applyCandidate`, `beginEventGesture/endEventGesture`, string Event IDs, `loopEnabled`.
- Produces: `bool AudioDocument::clearTimeline()`, `bool AudioDocument::trimSharedBoundary(EventId leftId, EventId rightId, SampleFrame sourceBoundary)`, and matching Controller invokables using string IDs.

- [ ] **Step 1: Add RED core and controller tests**

Cover:

```cpp
QVERIFY(document.clearTimeline());
QCOMPARE(document.totalFrames(), 0);
QVERIFY(document.undo());
QCOMPARE(document.timelineSnapshot().events.size(), originalCount);
```

For two adjacent events created by a split, move the shared source boundary left and right. Assert both events remain adjacent, source ranges remain valid, envelopes are reframed, and one gesture adds one Undo entry.

Controller tests must assert `setSelection()` enables looping, polling at the end seeks to the start, clicking outside clears selection before seek, and right-click clear disables looping.

- [ ] **Step 2: Run RED tests**

```powershell
ctest --test-dir build/msvc-release -R "audio_document_test|audio_editor_controller_test|qml_audio_tools_test" --output-on-failure
```

Expected: FAIL because clear is transient-only, loop defaults false, right-click is absent, and adjacent clips cannot extend through their shared boundary.

- [ ] **Step 3: Implement minimal document/controller contracts**

Implement clear through the existing history path:

```cpp
bool AudioDocument::clearTimeline()
{
    if (timeline_.snapshot().events.empty()) return false;
    return applyCandidate({});
}
```

Implement shared-boundary trim by copying the candidate event vector, reframing both same-source adjacent events around one source boundary, then calling `applyCandidate()` once. Reject different sources, gaps, overlaps, invalid boundaries, and parameter mismatches.

Make `setSelection()` enable loop and `clearSelection()` disable it. In QML, right-click selection calls `clearSelection()`; left-click outside selection clears it before `seekFrame()`.

- [ ] **Step 4: Run core/controller/QML tests**

```powershell
cmake --build build/msvc-release --config Release --target audio_document_test audio_editor_controller_test qml_audio_tools_test
ctest --test-dir build/msvc-release -R "audio_document_test|audio_editor_controller_test|qml_audio_tools_test" --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit**

```powershell
git add core/src/audio_editor qt/src/audio_editor app/qml/AgPlayer/components/audioeditor tests/core tests/qt tests/qml
git commit -m "feat(editor): make timeline edits and selection playback reliable"
```

---

### Task 5: Produce one high-fidelity waveform that never disappears spuriously

**Files:**
- Modify: `qt/src/audio_editor/audio_editor_controller.hpp`
- Modify: `qt/src/audio_editor/audio_editor_controller.cpp`
- Modify: `qt/src/audio_editor/audio_editor_waveform_item.cpp`
- Modify: `app/qml/AgPlayer/components/audioeditor/EditorWaveformCanvas.qml`
- Test: `tests/qt/audio_editor_controller_test.cpp`
- Test: `tests/qt/audio_editor_waveform_item_test.cpp`
- Test: `tests/qml/tst_audio_editor.qml`

**Interfaces:**
- Consumes: existing `ViewportWaveformJob` generation/cancellation and NaN gap representation.
- Produces: one non-cancelling centered amplitude envelope, DPR-aware bounded peak requests, and publish status that distinguishes an intentional empty timeline range from unavailable decode data.

- [ ] **Step 1: Add RED waveform tests**

Create an antiphase stereo fixture (`left=+0.5`, `right=-0.5`) and assert the published single envelope remains approximately `[-0.5,+0.5]`, not zero. Deep-zoom tests assert adjacent buckets contain sample detail without long repeated plateaus, generated point count stays within budget, and NaN gaps are not bridged.

Block source decode during split, trim, crop, gain, and envelope edits; assert last-good peaks remain visible until the newest generation publishes. A true visible range with no events must still publish intentional blank peaks.

- [ ] **Step 2: Run RED tests**

```powershell
ctest --test-dir build/msvc-release -R "audio_editor_controller_test|audio_editor_waveform_item_test|qml_audio_tools_test" --output-on-failure
```

Expected: FAIL on antiphase cancellation, nearest-neighbor stair steps, multiple QML center lines, or unavailable decode overwriting last-good peaks.

- [ ] **Step 3: Implement centered amplitude mix and safe publish state**

For each decoded frame, calculate:

```cpp
float amplitude = 0.0F;
for (int channel = 0; channel < decoded.channels; ++channel)
    amplitude = std::max(amplitude, std::abs(decoded.sample(frame, channel)));
outputMin = -amplitude;
outputMax = amplitude;
```

Scale target points by window DPR, clamp to the global bucket budget, linearly interpolate geometry only across adjacent finite buckets, and preserve NaN separators. Publish all-NaN only when the current snapshot proves the visible interval contains no event; otherwise retain last-good peaks and schedule the newest pending job.

Remove the per-source-channel center-line Repeater and render one center line at `height / 2`.

- [ ] **Step 4: Run waveform tests**

```powershell
cmake --build build/msvc-release --config Release --target audio_editor_controller_test audio_editor_waveform_item_test qml_audio_tools_test
ctest --test-dir build/msvc-release -R "audio_editor_controller_test|audio_editor_waveform_item_test|qml_audio_tools_test" --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit**

```powershell
git add qt/src/audio_editor app/qml/AgPlayer/components/audioeditor tests/qt tests/qml
git commit -m "fix(editor): preserve detailed single waveform during edits"
```

---

### Task 6: Complete visual, regression, and lightweight acceptance

**Files:**
- Modify: `docs/qa/audio-editor/phase6/design-qa.md`
- Modify: `docs/qa/audio-editor-v2-user-guide.md`
- Create: `docs/qa/audio-editor/phase6/no-recording-reference-comparison.png`
- Create: `docs/qa/audio-editor/phase6/no-recording-diff-mask.png`

**Interfaces:**
- Consumes: Tasks 1–5 and the reference PNG.
- Produces: Release/Debug evidence, visual comparison artifacts, and a reviewed clean implementation branch.

- [ ] **Step 1: Run formatting and static checks**

```powershell
git diff --check
ctest --test-dir build/msvc-release -R "audio_editor|audio_tools|window_controller|format_converter|metadata|filename" --output-on-failure
ctest --test-dir build/msvc-debug -R "audio_editor|audio_tools|window_controller|format_converter|metadata|filename" --output-on-failure
```

Expected: zero failures attributable to this branch.

- [ ] **Step 2: Run complete builds and CTest**

```powershell
cmake --build build/msvc-release --config Release
cmake --build build/msvc-debug --config Debug
ctest --test-dir build/msvc-release --output-on-failure
ctest --test-dir build/msvc-debug --output-on-failure
```

Record any pre-existing baseline failure separately; fix every new failure.

- [ ] **Step 3: Run real-media and startup smoke**

Import real WAV, FLAC, and MP3; split, move shared boundary, trim, play a looping selection, adjust BPM/speed/pitch/Envelope, run noise reduction, export, and re-open the result. Run the existing Release and Debug startup smoke scripts and record exact commands/results.

- [ ] **Step 4: Capture and compare reference layouts**

Capture `1672×941`, `1280×720`, and `880×560`. Build a same-canvas comparison with the `1672×941` reference and candidate, generate a difference mask, and fix all executable P0/P1/P2 visual differences before marking `design-qa.md` passed.

- [ ] **Step 5: Perform Ponytail and code review**

Verify there are no unused recording symbols, speculative abstractions, new dependencies, duplicate waveform pipelines, or fake controls. Run final branch review against base `5148160` and fix all load-bearing findings.

- [ ] **Step 6: Commit final QA records**

```powershell
git add docs/qa
git commit -m "docs(qa): verify responsive no-recording editor"
```

- [ ] **Step 7: Create final implementation commit if required**

If review fixes changed code after the prior task commits:

```powershell
git add -A
git commit -m "fix(editor): finalize responsive no-recording timeline"
```

Confirm `git status --short` is empty.
