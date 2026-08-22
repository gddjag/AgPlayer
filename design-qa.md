# Design QA — Phase 6 final-review correction — 2026-08-22

## Source visual truth

- Approved reference: `C:\Users\Administrator\Desktop\音视频播放器\AgPlayer音频播放器完整版\音频编辑.png` (`1672x941`).
- Candidate content is the real two-second `sine-440hz.wav` fixture. Reference-only filename, device, signal level, BPM, selection, Formant, playback, recording, and export results are not fabricated.

## Final Release and Debug matrix

- Release: `build/qa/phase6/release-review-final4/tools-zh-theme0-tool0-{1672x941,1280x720,880x560}.png`
- Debug: `build/qa/phase6/debug-review-final4/tools-zh-theme0-tool0-{1672x941,1280x720,880x560}.png`
- Release source/candidate and visible mask: `build/qa/phase6/comparisons-review-final4/release-editor-1672x941-final4-{comparison,difference-mask}.png`
- Debug source/candidate and visible mask: `build/qa/phase6/comparisons-review-final4/debug-editor-1672x941-final4-{comparison,difference-mask}.png`

Both six-image matrix runs loaded the real WAV and exited zero. All artifacts
have the requested logical dimensions and contain no modal. The threshold-12
Release mask reports 579,876 changed pixels (`0.368561`) and mean maximum-channel
difference `34.297`; Debug reports 579,877 (`0.368562`) and `34.297`. The large
content mask is expected from the honest two-second sine waveform and the
required omission/disabled treatment of future capabilities, not a geometry
acceptance threshold.

## Manual P0/P1/P2 review

- P0: none. The exact title, underlined tab order, command order, A–E inspector,
  waveform, two transport cards, shortcut card, and status bar are present and
  unobstructed at the reference size.
- P1: none. At 1280 the inspector is independently scrollable. At 880 the
  complete disabled Play entry and active `编辑设置` entry remain in the first
  viewport; opening settings and scrolling to the end places the disabled E
  group Export action fully inside the page. These mapped bounds are exercised
  by QML, not inferred from `visible` alone.
- P2: none. Recording, BPM, speed, pitch, preserve-pitch, playback, and export
  remain visible but capability-gated. The recording indicator is muted gray,
  the playback icon/ring and companion controls use explicit disabled opacity,
  and the 880 Play entry uses the same disabled treatment. Accessible names,
  roles, and disabled `enabled` state are QML-tested.
- The source/candidate hierarchy and existing icon assets are retained. No
  emoji, handcrafted SVG, new raster asset, fake device/data, or later-phase
  interaction was introduced.

## Closed iteration findings

- Removed the old DocumentRenderer preview/BPM/export paths and the
  editor-owned second player; all five future backend capabilities are
  explicitly false in Phase 6.
- E reads persisted project export settings without writable QML shadow state;
  unsupported values show `--` and the action stays disabled.
- Replaced whole-visible-range PCM retention with cancelable streaming peak
  buckets bounded by channels times viewport width; total Scene Graph points
  are bounded across channels to `2 * logical width`.
- Playhead, Move, and Trim preview locally and commit once on release. Split's
  toolbar action only selects scissors mode; `S`/`Ctrl+B` perform the split.
- Removed the duplicate shell Space shortcut. Actual wheel events now apply
  `1.25` zoom-in and `0.8` zoom-out, while ruler and scrollbar use the shared
  frame/pixel viewport seam.
- The final visual correction replaces the misleading bright recording red and
  playback green with visibly disabled gray-blue controls in all six captures.

final result: passed
