# Six-track desktop editor UI verification

Date: 2026-09-15. Isolated worktree: `.worktrees/pc-six-track-editor`. No commits or release packages made by this worker.

## Delivered UI

- Replaced the old single-track/summary/shortcut-card workspace with seven reference toolbar actions, six fixed stable track headers, per-track mute/gain, a shared clip timeline, and recording/playback controls. Retained the existing functional A–D parameter/export controls and other tool pages.
- Right inspector remains beside the timeline: 380 px outer / 358 px content at reference width; 300 px outer in narrow windows. Track headers are 220 px normally and 180 px when compact. Rows never shrink below 86 px; smaller windows scroll tracks vertically instead of scaling down the whole page or hiding the inspector.
- Waveforms come from real source PCM through `eventPeaks` and the existing native `AudioEditorWaveformItem`. Clip geometry follows viewport changes; coarse/detail updates subscribe to waveform revision, not playhead motion. Empty/failed input is not represented by invented waveforms.
- Stable timeline mouse capture survives clip-view list rebuilding. Upper 24 px moves preserve grab offset; envelope points win hit testing. Shared split boundaries retain the existing trim gesture. Body selection is global in time. Ruler/body-playhead scrubbing uses the existing preview/final-seek controller contract; Esc cancels without committing on release.
- Clicking blank tracks clears the previously selected clip. Busy operations expose the actual cancellation control. Recording device errors remain visible rather than claiming success.
- The approved screenshot controls layout; `ui-ux-pro-max` was used only for supporting focus/desktop interaction review, not to replace the approved design or introduce web frameworks.

## Executed checks

- Incremental Release build: `qml_audio_tools_test` passed with the new QML components/resources.
- `tests/qml/tst_six_track_editor.qml`: **14 passed, 0 failed, 0 skipped**, Windows native platform, Qt 6.7. No QML warnings in `build/release/qml-six-final.txt`. Tests cover fixed tracks, actual imported PCM geometry, reference/small/minimum/light layout, continuous cross-track dragging and one undo, envelope-point priority/one undo, shared boundary trim, scrub/cancel, blank-track selection safety and busy cancellation.
- Focused editor cases in `tst_audio_tools_responsive.qml`: **6 passed, 0 failed** including init/cleanup, at page sizes 760×332, 998×442, 1065×479 and 1672×853. Right export action remains reachable after vertical scrolling. Log: `build/release/qml-six-responsive.txt`. Unrelated tool branches were not rerun.
- Observed RED regressions before final verification: stale one-pixel waveform geometry, shared boundary behavior, body scrub, blank-click stale clip selection, missing busy cancel control, ruler Esc restoration.

## Visual artifacts inspected

Actual native tool-window images, generated from six real imports of the repository's decoded `sine-440hz.wav` fixture (constant amplitude naturally produces a continuous band):

- `build/release/six-editor-reference.png` — 1672×941, dark.
- `build/release/six-editor-small.png` — 1000×700, dark.
- `build/release/six-editor-minimum.png` — 760×420, dark; vertical reachability of the sixth track is also asserted.
- `build/release/six-editor-light.png` — 1672×941, light.

The original tools-window title/navigation shell remains unchanged. This test machine reports no usable audio input; screenshots show that real error. These checks do not establish physical microphone capture, macOS/HIG runtime acceptance, subjective listening quality, old single-track-specific test compatibility, or full-application/release readiness. Parent-owned controller changes after these runs may require a focused rerun.
