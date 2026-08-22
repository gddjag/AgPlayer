# Design QA — Phase 6 precise reference timeline UI — 2026-08-22

## Source visual truth

- Approved editor reference: `C:\Users\Administrator\Desktop\音视频播放器\AgPlayer音频播放器完整版\音频编辑.png`
- Reference dimensions: `1672x941`.
- Candidate uses the real `sine-440hz.wav` fixture. It does not copy the reference's demo filename, device, BPM, selection, or unsupported Formant capability.

## Captured evidence

- Release 1672x941: `build/qa/phase6/release-smoke/tools-zh-theme0-tool0-1672x941.png`
- Release responsive matrix: `build/qa/phase6/release-final2/tools-zh-theme0-tool0-1280x720.png` and `build/qa/phase6/release-final3/tools-zh-theme0-tool0-880x560.png`
- Debug matrix: `build/qa/phase6/debug-final/tools-zh-theme0-tool0-1672x941.png`, `build/qa/phase6/debug-final/tools-zh-theme0-tool0-1280x720.png`, and `build/qa/phase6/debug-final3/tools-zh-theme0-tool0-880x560.png`
- Source plus final Release candidate: `build/qa/phase6/comparisons-final/release-editor-1672x941-comparison.png`
- Visible Release difference mask: `build/qa/phase6/comparisons-final/release-editor-1672x941-difference-mask.png`
- Debug comparison and mask: `build/qa/phase6/comparisons-final/debug-editor-1672x941-comparison.png` and `debug-editor-1672x941-difference-mask.png`

The Release comparison uses equal 1672x941 logical dimensions. Its threshold-12
mask reports 620,264 changed pixels (`0.394231`) and a mean maximum-channel
difference of `37.510`. This numeric result is not used as a pixel-identity
gate: most changed pixels are the honest two-second sine waveform and the
required absence of reference-only demo data and later-phase capabilities.

## Visual and interaction findings

- The exact title, left-aligned underlined tab order, command order, A–E inspector, two transports, shortcut card, and status bar are present.
- The 1672x941 global object geometries are covered by QML tests with a two-pixel tolerance and visually match the approved hierarchy.
- At 1280x720 the main workbench and scrollable inspector remain usable. At 880x560 the main workbench is vertically scrollable and the `编辑设置` control opens the scrollable inspector overlay, so playback and Export remain reachable.
- No fake device, signal level, BPM, selection, save/export result, or Formant row is shown. Later-phase crop/fade/mute actions remain honestly disabled.
- Existing icon-library assets are used; no emoji, handcrafted SVG, or new raster assets were introduced.

## Iteration closure

- P0 fixed: the first real-WAV capture opened a discard modal because setting viewport width marked an empty session modified. A controller regression now guards that state and the final captures contain no modal.
- P1 fixed: Export stayed visually disabled after opening audio because a QML invokable call had no observable dependency. The action model's `dataChanged` signal now invalidates the binding; the final Release capture shows Export enabled.
- P2 fixed: title/logo treatment, bordered toolbar buttons, file-summary hierarchy, transport button treatment, and narrow inspector access were aligned with the reference without adding assets.
- P1 fixed: the 880x560 first viewport exposed only the upper part of the scrollable playback card. A complete, enabled narrow-screen Play/Pause entry now remains beside `编辑设置`; the QML test also opens the inspector, scrolls it to the end, and proves the E-group Export button is fully inside the viewport.
- No executable visual P0, P1, or P2 mismatch remains in the six final screenshots.

## Known non-visual baseline

All Debug screenshots are written successfully and have the requested dimensions.
The final 880 capture required Qt's existing software RHI because the default
Debug `grabWindow()` path stalled twice; the exact QA PIDs were verified and
stopped. After a successful capture, the Debug app exits with the repository's existing Qt cleanup crash
(`Qt6Cored.dll`, `0xc0000005`, offset `0x7d97a`). Release capture and launch
exit cleanly. The crash is reported as a remaining Debug-runtime baseline and
was not hidden by weakening the QA script.

final result: passed
