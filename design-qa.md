# Design QA — Phase 6 final rereview — 2026-08-23

## Source visual truth

- Approved reference: `C:\Users\Administrator\Desktop\音视频播放器\AgPlayer音频播放器完整版\音频编辑.png` (`1672x941`).
- Candidate content is the real two-second `sine-440hz.wav` fixture. Reference-only filename, device, signal level, BPM, selection, Formant, playback, recording, and export results are intentionally not fabricated.

## Final Release and Debug matrix

- Release: `build/qa/phase6/release-review-final5/tools-zh-theme0-tool0-{1672x941,1280x720,880x560}.png`
- Debug: `build/qa/phase6/debug-review-final5/tools-zh-theme0-tool0-{1672x941,1280x720,880x560}.png`
- Release source/candidate and mask: `build/qa/phase6/comparisons-review-final5/release-editor-1672x941-final5-{comparison,difference-mask}.png`
- Debug source/candidate and mask: `build/qa/phase6/comparisons-review-final5/debug-editor-1672x941-final5-{comparison,difference-mask}.png`

Both matrix runs loaded the real WAV, wrote all three exact logical sizes, and
exited zero. The threshold-12 Release and Debug masks each report 580,106
changed pixels (`0.368707`) and mean maximum-channel difference `34.318`. The
large mask is expected from honest real-sine data plus required disabled or
absent later-phase controls, rather than a geometry acceptance threshold.

## Manual P0/P1/P2 review

- P0: none. The exact title, tab/command order, A–E inspector, waveform,
  transport cards, shortcut card, and status bar remain unobstructed at
  1672x941 in both configurations.
- P1: none. At 1280 the inspector remains scrollable. At 880 the complete
  first-viewport Play entry and `编辑设置` entry remain reachable; Play is
  visibly disabled because Phase 12 has not supplied a backend, while the
  settings entry reaches the E group.
- P2: none. Unsupported record/play controls are gray and low-opacity rather
  than visually active. E shows persisted `24-bit` and an empty output
  directory as `--`, never as the project path. The dead Space binding and
  active-playback hint are absent; the visible copy says `播放：Phase 12 接入`.
- Release and Debug candidates are visually equivalent. No modal, fake device,
  fake data, new asset, or later-phase interaction appears in any capture.

## Closed final-rereview findings

- Persisted project export settings now validate and round-trip bit depth, with schema-compatible default 24 and exact E-group binding.
- Rejected Move/Trim gestures clear their local candidate and notify QML to redraw the real timeline without changing revision or Undo history.
- Viewport decoding is single-flight: one active task plus one replaceable latest pending request. Invalid requests cancel the active generation and discard pending work; no PCM cache was added.
- Capability-false `stopPlayback()` returns false without moving the persisted playhead or dirtying the project. Dead Space/test-only/actionRevision state was removed.
- Exact extraction covers 90 context-scoped Phase 6 QML sources across the
  editor shell and `ToolSidebar` in zh/en/th/vi; en/th/vi contain completed
  locale translations without Chinese fallback, and placeholder sets match.

## Quality-final nonvisual closure

- The final quality fixes only change controller gesture lifetime, checked
  integer validation, automated tests, and development records. No QML geometry,
  color, typography, asset, or rendering input changed, so the accepted final5
  six-image matrix and comparison masks remain the applicable visual evidence.
- Successful create/open/open-project/clear replacements now discard any staged
  Move/Trim overlay at the document commit boundary. Failed opens preserve the
  active gesture. Overflowing preview coordinates are rejected before publishing
  a candidate, without a notification or history/revision mutation.

final result: passed
