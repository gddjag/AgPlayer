# Execution ledger — plan: docs/development/2026-09-15-six-track-plan.md

Baseline d29bb41f. Main checkout HarmonyOS has unrelated uncommitted decoder/TimePitch/side nav/Harmony work; isolated worktree created and these files not copied.

| Tasks/interface | Preflight decision |
|---|---|
| Model -> mixer/controller | trackIndex 0..5, snapshot tracks/project format; publish interfaces before consumers compile |
| Model -> project save | schema3, old1/2 track1, internal legacy gain |
| Mixer -> preview/export/handoff | automation before sessionDSP, source frame mapping shared; no duplicate gain |
| Recorder -> controller | standalone QObject/device writer first; controller owns target track/cursor/media commit |
| UI -> controller | single controller state; no QML shadow document |
| All -> build files/tests | parent owns common CMake additions; workers send requested registration |

Ruling: parallelize disjoint file-owned implementation tasks with parent integration (user preference and active developer delegation); no concurrent writes to same module. Cost: interface integration needs a shared build gate.

## Status
- Implementation authorized and active; no packaging/publication. Worktree isolated from HarmonyOS.
- Old build baseline: 5/5 editor core tests passed. New QML contract RED confirmed: controller did not expose tracks. Mixer RED confirmed old path rejected overlapping tracks.
- Model/schema 3/migration/track undo/mixed frame coordinates implemented. First focused run: document 43, timeline 6, command 12 passed. Project 34 passed, 1 Windows symlink-permission skip; later source-alias guard added, pending final rerun.
- Shared mixer/DSP/output guard implemented. Focused renderer 17/17 plus playback/writer/TimePitch 3/3 passed. No hardware listening claim.
- Recording service implemented; fake-device PCM writer/state tests passed except a newly exposed stop/overflow race. Race fix in place, final rerun pending. macOS permission deployment Python tests: 28 passed, 2 host symlink skips. No Mac build/hardware claim.
- Controller six-track batch import/gain undo test 3/3 passed. Full old suite exposed obsolete single-track contracts plus a real loading/action notification ordering defect; production fix and contract updates in progress.
- Six-track UI replacement and per-clip detailed waveform publication in progress. No visual acceptance yet.
- Build dependency issue repaired: installed MSVC has only 2052 language; use correct Chinese showIncludes prefix and `chcp 65001`. Confirmed noise_reducer object dependencies 0 -> 12. Rebuilt only 151 generated objects with invalid zero-dependency records in this isolated build; no main checkout/build cleaned.
- All work remains uncommitted for integrated final review.

## 2026-09-19 — focused clip gestures follow-up

- Requested scope: preserve range drag-out, waveform context-menu volume envelope, envelope context-menu control point, and draggable clip-edge trim. Other in-progress six-track work preserved; no commit/package/release.
- Waveform context menu now creates a persisted unity envelope through the existing undoable document API. Right-click on its line adds an interpolated-gain point without jumping the volume; duplicate point hits retain removal. No second UI document state or audio-processing path added. Chinese/English labels included.
- Clip edges now take precedence over the coincident playhead. Exact exclusive end boundaries can be hit from the adjacent gap. Continuous trim still previews, commits one undo, and Esc restores the original. Updated the obsolete controller assertion that expected no preview notification; document revision/history assertions remain intact.
- RED observed for missing menu and left-edge/playhead capture. Final native Windows focused QML run: 9 passed / 0 failed (7 behavior cases plus setup/cleanup), `build/release/six-gesture-native-final.txt`.
- Full controller suite: 138 passed / 0 failed, `build/release/six-controller-gesture-final.txt`.
- Drag/export suite: 12 passed / 0 failed, `build/release/six-drag-final.txt`. New known-stereo-PCM test verifies exact 2048-frame selection WAV, six-track sum including envelope/track gains, muted-track output, and distinct cache assets. Existing threshold, cancellation, cache, TimePitch and verified-WAV URI cases retained.
- Incremental Release builds for the QML runner/controller/drag tests succeeded. `git diff --check` passed.
- Whole QML acceptance is NOT green: native whole-file execution stopped in `test_offlineSourceHasUsableRelinkEntry` with QWindowsDialogHelper cleanup failure. Offscreen run had 14 passes / 4 failures (`test_blankTrackClickClearsPreviouslySelectedClip`, `test_bodySelectionRetainsDragOutEntry`, `test_offlineSourceHasUsableRelinkEntry`, `test_rightClickCreatesVolumeLineThenControlPoint`) and a missing-font-directory warning; one measured page had negative available width. These are not hidden by the separate passing native gesture tests. Logs: `six-gesture-green.txt`, `six-gesture-offscreen.txt`.
- Physical Explorer/Finder receiver drag, macOS build/runtime, microphone and listening acceptance remain unverified. The above PCM test verifies generated drag assets, not an external app receiving the native drag.

## 2026-09-19 — remove the replaced editor and validate real FLAC samples

- User explicitly requested deleting the old single-track editor instead of maintaining both implementations. Only `EditorSixTrackWorkspace`, `EditorWaveformCanvas` and the shared `EditorSlider` remain in the editor QML component directory. The deleted command/status/summary components have no application registrations or translation contexts. Removed unused old segment-navigation/time-format/modifier forwarding code from the page.
- Deleted `tst_audio_editor.qml`, `tst_audio_editor_native_input.qml` and their incomplete dedicated test harness. Shared workflow assertions were migrated to `tst_six_track_workflow.qml`; real native drop/playback assertions to `tst_six_track_native_input.qml`, using the complete audio-tools harness. Single-track geometry, absent-recording, reject-multiple-files and no-gap-trim assumptions are intentionally removed, not retained as a second product contract. Six-track clip gestures, layout, import and controller/core tests cover their replacements. The old-project reader is still data migration into six tracks, not an old editor UI or playback path.
- Latest native Windows QML results: workflow 18/18, native input 4/4, six-track layout/gestures 18/18. Logs: `six-only-workflow.txt`, `six-only-native_input.txt`, `six-only-editor.txt` under `build/release`. Native input includes a real Qt URL drop of three WAV files into separate tracks and real fixture playback with control focus. This does not certify Explorer/Finder receiving an outbound drag.
- Whole six-track QML offscreen/native runs are now green. Earlier negative-width/menu/relink failures came from interacting before the first window layout; tests now wait for rendering. The relink test intentionally uses Qt's non-native dialog to avoid asynchronous Windows shell-dialog teardown; native OS file-picker acceptance remains separate. Reference/small/minimum/light screenshots are in `build/release/six-closeout-*.png`; inspected the reference image, not claiming a complete 2-pixel overlay certification.
- Windows `AgPlayer` Release build passed. Fixed a real Windows header min/max macro collision in frame-conversion helpers. The playback button now uses TabFocus, retaining the single shell Space shortcut and playback-ownership assertions.
- Real sample directory supplied by the user: `F:\无损音乐\流行音乐`. Both FLACs reproduced a failure after all declared PCM had decoded (12,101,617 and 11,256,949 source frames). Non-audio trailing bytes were treated as another FLAC packet/frame.
- Shared FLAC-boundary helper is used by decoder and both transcode/normalization paths. It accepts the end only after a successfully decoded frame reaches STREAMINFO's exact source-frame count, not on arbitrary errors or rounded duration. Unknown/mismatching lengths still fail on malformed trailers. Reference: https://www.rfc-editor.org/rfc/rfc9639.html#section-8.2 . No source music was modified; temporary WAV validation outputs were removed after checking.
- Both originals passed full PCM and analysis decoding, head/middle/tail seeks, waveform generation, editor source analysis, lossless analysis and WAV conversion (`flac-external-workflows.txt`). Synthetic trailer regression first failed and then passed; the regression additionally checks 44.1→48 kHz output length and rejects malformed data before the declared end. `decoder_test` passed.
- Metadata Title fallback is display-only in the library model and applies to its QML track maps: blank/whitespace -> disk basename without extension. Rename notifications include TitleRole. Stored metadata is not replaced with this fallback. Focused model checks passed (5 including setup/cleanup); metadata editor writes with missing or existing untouched titles passed (5 including setup/cleanup and existing write workflow).
- Immersive top-right buttons now fade in only in their mouse-proximity zone and are disabled while hidden. Native pointer test passed (3 including setup/cleanup).
- Changes remain in the isolated `codex/pc-six-track-editor` worktree. Main HarmonyOS uncommitted changes were not overwritten. Harmony's decoder build points at shared `core/src/decoder.cpp`; these shared fixes still need integration/build on the other platform branches. No Mac/Harmony device validation, hardware microphone/listening certification, installer packaging or publication is claimed.
