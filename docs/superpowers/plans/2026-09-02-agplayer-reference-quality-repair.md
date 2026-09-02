# AgPlayer Reference Quality Repair Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Repair the approved player, list, waveform, audio-tool, separation, and metadata regressions without creating theme-specific business logic.

**Architecture:** Keep controllers, playback state, library data, and waveform policy shared. Consolidate shell differences into explicit layout profiles and repair tool behavior at controller/core boundaries, with focused regression tests before each implementation.

**Tech Stack:** C++17, Qt 6, Qt Quick/QML, Qt Test, CTest, FFmpeg, ONNX model worker.

**Spec:** `docs/superpowers/specs/2026-09-02-agplayer-reference-quality-repair-design.md`

## Global Constraints

- Do not modify immersive-only renderer, reactor, shader, or immersive tests.
- Preserve existing uncommitted theme-skin, lyrics, and immersive icon work.
- Do not duplicate playback, list data, lyrics state, or model discovery per theme.
- Do not weaken staged metadata write, backup, readback, atomic replacement, or rollback safety.
- Do not add paid services, API keys, permanent polling, or unbounded background work.
- Every production fix starts with a regression test that fails for the reported reason.
- Packaging is not part of this plan.

---

### Task 1: Classic geometry, shared list profiles, navigation icons, and drop routing

**Files:**
- Modify: `app/qml/AgPlayer/Main.qml`
- Modify: `qt/src/window_controller.cpp`
- Modify: `app/qml/AgPlayer/components/TrackList.qml`
- Modify: `app/qml/AgPlayer/components/SideNavigation.qml`
- Modify: `app/qml/AgPlayer/theme/Theme.qml`
- Modify: `app/qml/AgPlayer/ListWindow.qml`
- Modify: `app/qml/AgPlayer/components/IntegratedPlayerShell.qml`
- Modify: `app/qml/AgPlayer/components/RollingPlayerShell.qml`
- Modify: `qt/src/native_drop_router.cpp`
- Test: `tests/qt/window_controller_test.cpp`
- Test: `tests/qt/native_drop_router_test.cpp`
- Test: `tests/qt/library_manager_controller_test.cpp`
- Test: `tests/qml/tst_main_window.qml`
- Test: `tests/qml/tst_integrated_theme.qml`
- Test: `tests/qml/tst_rolling_theme.qml`

**Interfaces:**
- Produces: `TrackList.layoutProfile` with values `classic`, `integrated`, and `rolling`.
- Produces: Theme navigation icon/action size tokens.
- Preserves: `LibraryManagerController.classifyDropUrl()` as the URL classifier.

- [ ] Add failing geometry tests for fresh `863 x 266`, independent shell geometry, and preservation of a user-resized classic geometry.
- [ ] Run `ctest --test-dir build/release -C Release -R '^(window_controller_test)$' --output-on-failure` and confirm the old `960 x 298` fallback fails.
- [ ] Change both QML initialization and C++ classic fallback to `863 x 266` without clearing existing per-shell settings.
- [ ] Add failing QML tests at 863, 960, 1180, 1440, and 1672 DIP proving required trailing columns remain visible and only song/waveform width grows.
- [ ] Replace scattered list presentation booleans with `layoutProfile` and centralized column-width functions; keep one delegate and one data model.
- [ ] Add failing navigation tests proving library, favorites, tags, resource root, and resource folder icons share one visual size and at least a 28-pixel action extent.
- [ ] Add Theme tokens and bind every navigation node to them.
- [ ] Add failing drag tests for readable playlist hover, file and directory drops in each shell, duplicate directories, invalid URLs, and rejection of internal-track drops on resource folders.
- [ ] Route every shell through the same classifier/submission path and make resource-directory registration trigger import/refresh completion before reporting success.
- [ ] Run the six focused Qt/QML test targets and confirm they pass.

### Task 2: Shared player actions, waveform contrast, compact settings, information, and lyrics

**Files:**
- Modify: `app/qml/AgPlayer/components/PlayerControls.qml`
- Modify: `app/qml/AgPlayer/components/MiniPlayerControls.qml`
- Modify: `app/qml/AgPlayer/components/IntegratedPlayerControls.qml`
- Modify: `app/qml/AgPlayer/components/RollingPlayerShell.qml`
- Modify: `app/qml/AgPlayer/components/ExperienceActions.qml`
- Modify: `app/qml/AgPlayer/components/SharedWaveformView.qml`
- Modify: `app/qml/AgPlayer/components/ColorField.qml`
- Modify: `app/qml/AgPlayer/SettingsPage.qml`
- Modify: `app/qml/AgPlayer/components/AudioFileInfoPanel.qml`
- Modify: `app/qml/AgPlayer/components/LyricsPanel.qml`
- Modify: lyrics provider/service C++ files identified by `search_graph` before editing.
- Test: `tests/qml/tst_player_controls.qml`
- Test: `tests/qml/tst_main_window.qml`
- Test: `tests/qml/tst_integrated_theme.qml`
- Test: `tests/qml/tst_rolling_theme.qml`
- Test: `tests/qt/waveform_item_test.cpp`
- Test: lyrics provider/service Qt tests.

**Interfaces:**
- Consumes: `TrackList.layoutProfile` from Task 1.
- Produces: shared action profiles and one theme-popup anchoring function.
- Produces: one shared spectral progress/clip binding.

- [ ] Add failing object-order tests for classic, mini, integrated, and rolling actions, including mini theme-before-waveform and integrated playlist/lyrics entries.
- [ ] Add failing geometry tests for vertical centering, safe margins, mini waveform clipping, and theme popup placement above its invoking icon at 100% and 150% DPI.
- [ ] Implement declarative action profiles and mapped, screen-clamped popup anchoring while retaining shared handlers.
- [ ] Add waveform tests at 0%, 0.1%, 50%, 99.9%, and 100% proving identical dark/light/system contrast and continuous clipping without node/geometry rebuild.
- [ ] Bind every non-immersive shell to `SharedWaveformView` progress policy and correct rolling visible-range density and overview hover mapping.
- [ ] Add failing compact-picker tests for geometry, accept/cancel, keyboard focus, eight-label order, spacing, restore default, and persistence.
- [ ] Replace the system-size-dependent picker with the shared compact application picker and bind all spectral slots.
- [ ] Add failing portrait-info tests for `130 x 438`, long-path elision/copy, vertical scrolling, repeated open, and identical dimensions from all entry points.
- [ ] Reuse one info window shell and scale its content inside the fixed portrait bounds.
- [ ] Revalidate the three live free lyrics routes and their attribution/response contracts before editing providers.
- [ ] Add provider tests for success, no match, timeout, TLS, 429, 5xx, bad JSON, cancellation, late response, failover order, all-failed summary, and preserved state after close/theme switch.
- [ ] Implement structured lyrics route outcomes and a close request that hides the host without destroying shared state.
- [ ] Run the focused player, waveform, settings, info, and lyrics tests.

### Task 3: Shared audio-tool chrome, equalizer, and audio-editor behavior

**Files:**
- Create or Modify: shared tool-window title/navigation component under `app/qml/AgPlayer/components/tools/`.
- Modify: `app/qml/AgPlayer/AudioToolsWindow.qml`
- Modify: `app/qml/AgPlayer/components/tools/ToolSidebar.qml`
- Modify: `app/qml/AgPlayer/EqualizerWindow.qml`
- Modify: `app/qml/AgPlayer/components/EqualizerBandSlider.qml`
- Modify: `app/qml/AgPlayer/components/tools/AudioEditorPage.qml`
- Modify: `qt/src/audio_editor/audio_editor_controller.cpp`
- Modify: `qt/src/audio_editor/editor_viewport.cpp`
- Test: `tests/qml/tst_audio_editor.qml`
- Test: `tests/qml/tst_equalizer_visual.qml`
- Test: `tests/scripts/audio_tools_layout_contract_test.ps1`
- Test: `tests/qt/audio_editor_controller_test.cpp`
- Test: `tests/core/document_writer_test.cpp`

**Interfaces:**
- Produces: fixed left-aligned tool navigation and shared window-chrome geometry.
- Produces: selection-aware configured-directory export entry point.
- Preserves: editor viewport API, track gain controller state, and EQ DSP/controller calls.

- [ ] Add failing layout tests proving all five navigation tabs share the same left origin/height and audio-tools/EQ window buttons share one compact size.
- [ ] Implement shared chrome/navigation tokens or component without changing page loaders or controllers.
- [ ] Add failing EQ tests for 18 readable bands, uniform type/thumb geometry, no clipping at default size, and horizontal access at minimum size.
- [ ] Stop compressing nineteen columns below their safe width; use horizontal scrolling and retain keyboard, wheel, double-click, preset, reset, and enable behavior.
- [ ] Add failing editor tests proving `editorTrackGain`, `editorTrackGainLabel`, and `editorTimelineZoomRange` are absent and remaining timeline/transport/shortcut regions do not overlap at 1280x720 and 880x560.
- [ ] Remove only those QML controls, reclaim layout space, and retain Ctrl+wheel zoom plus Shift+wheel pan.
- [ ] Update shortcut tests and copy to `Ctrl+右键 = 选择片段 / 双击右键取消` while keeping envelope shortcuts truthful.
- [ ] Add a controller test that sets a partial viewport, crops to selection, and expects `0..newTotalFrames`; add control tests proving split/trim preserve the viewport.
- [ ] Fit the viewport only after successful crop-to-selection.
- [ ] Add full and selection export tests for output frame count, invalid selection rejection, nonzero range start, failure cleanup, and truthful success state.
- [ ] Expose selection-aware configured-directory export and bind it without changing full export behavior.
- [ ] Run focused editor, EQ, navigation, and writer tests.

### Task 4: Separation download/model registry, multi-track guides, and metadata verification

**Files:**
- Modify: `app/qml/AgPlayer/components/tools/VocalSeparationPage.qml`
- Modify: `qt/src/vocal_separation_controller.cpp`
- Modify: `qt/src/vocal_separation_controller.hpp`
- Modify: `qt/src/vocal_separation_catalog.cpp`
- Modify: `qt/src/vocal_separation_catalog.hpp`
- Modify: `qt/src/vocal_separation_installer.cpp`
- Modify: `qt/src/vocal_separation_installer.hpp`
- Modify: `worker/src/trusted_profiles.cpp`
- Modify: `core/src/metadata_writer.cpp`
- Modify: `core/include/agplayer/metadata_writer.hpp`
- Modify: `qt/src/metadata_editor.cpp`
- Modify: `qt/src/metadata_editor.hpp`
- Test: `tests/qml/tst_vocal_separation.qml`
- Test: `tests/qt/vocal_separation_controller_test.cpp`
- Test: `tests/core/metadata_writer_test.cpp`

**Interfaces:**
- Produces: structured all-sources-exhausted download event.
- Produces: model registry entries with origin, compatibility, profile, path, hash, and rejection reason.
- Produces: metadata audio-equivalence classification and explicit strict/force policy.

- [ ] Add download-state QML tests proving progress, percentage, route, pause/mirror actions, and primary separation action remain inside their cards at normal/narrow layouts and 100/125/150% DPI.
- [ ] Re-layout the card with one reserved progress row and shorten/semibold the primary action without changing bindings.
- [ ] Add controller tests for official failure to domestic mirror, mirror exhaustion emitting one structured event, direct-mirror failure, runtime failure source, and cancellation not opening backup URLs.
- [ ] Implement route-state events and open backup URLs only after all automatic routes are exhausted.
- [ ] Add model tests for built-in fingerprint discovery, sidecar manifest, compatible MDX/Demucs tensor profiles, page-entry scan, manual detect, watcher refresh, and rejection of unknown shape/opset/hash/path traversal/symlink.
- [ ] Introduce the shared model registry and connect compatible discoveries to the existing selection/execution path only after worker validation.
- [ ] Add QML tests that Mix and Solo show an equal-X guide on every available result waveform across play, pause, seek, and mode switch.
- [ ] Bind result guides to the shared result preview position; keep Solo responsible only for audio routing.
- [ ] Add metadata fixtures covering MP3, FLAC, M4A, and OGG with normalized time base/timestamps/packet boundaries and unchanged audio payload or decoded PCM.
- [ ] Add failing tests for strict success on verified normalization, force fallback, requested-tag mismatch, unreadable stage, source mutation, decoded-audio mismatch, backup failure, atomic replace failure, and rollback cleanup.
- [ ] Replace packet-timestamp identity with classified audio equivalence while preserving mandatory field readback, backup, atomic replacement, and rollback.
- [ ] Expose force mode only as a verified sanitized-remux fallback and return a clear warning/result classification.
- [ ] Run focused separation, worker-profile, metadata-writer, and metadata-editor tests.

### Task 5: Integrated validation and final review

**Files:**
- Modify: acceptance notes under `docs/qa/` only with new evidence.

**Interfaces:**
- Consumes: all Tasks 1-4 outputs.
- Produces: a requirement-to-evidence matrix and release-readiness verdict.

- [ ] Build Release `AgPlayer`, Qt tests, and QML test host from the official VS developer shell.
- [ ] Run complete CTest with `ctest --test-dir build/release -C Release --output-on-failure` and record exact pass/fail counts.
- [ ] Run the repository QML lint and static/translation checks and record warnings separately from errors.
- [ ] Launch classic, integrated, rolling, and mini shells in dark/light/system themes at reference, minimum, and stretched sizes; capture screenshots after the final build.
- [ ] Perform real Windows drag/drop, theme popup, task switching, waveform playback/seek, lyrics failover, editor crop/full export/selection export, model scan, mirror failure, mix/solo preview, and metadata writes on copied fixtures.
- [ ] Review the final diff for duplicated handlers, theme-specific business logic, dead code, unbounded work, lifecycle errors, whitespace errors, and unrelated edits.
- [ ] Update the acceptance note with remaining risks; do not claim packaging or a clean release until every required gate passes.
