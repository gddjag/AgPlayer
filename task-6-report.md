# Task 6 — Continuous Integrated Waveform Progress

## Scope

- Worktree: `D:\ai\AgPlayer\.worktrees\player-polish-lyrics-20260830`
- Base: `9e44c41ad24b1953f34472ea830a33401ed77bab`
- Production change: `IntegratedPlayerShell.qml` only.
- No playback timer, waveform analyzer/cache, Mix algorithm, classic/mini production code, or package output was changed.

## Root cause and fix

The integrated waveform used one `WaveformItem` with its `position` bound to
playback time. Its played-colour boundary therefore advanced only as the next
rendered peak bucket was crossed. `PlaybackController` already uses a 17 ms
precise timer, and the existing classic layout proved that `waveformCursorX`
provides a continuous pixel coordinate.

The integrated shell now uses the same two-pass rendering contract:

1. `integratedWaveform` remains unplayed (`position: 0`) and receives playback
   through `cursorPosition`.
2. `integratedPlayedWaveform` is fully played (`position: duration`) and is
   clipped by `integratedWaveformPlayedClip.width`, bound exactly to
   `integratedWaveform.waveformCursorX`.
3. The duplicate binds visual/range/analysis settings to the base. The existing
   mode path assigns either peaks or layers, never both, to both renderers.

## TDD evidence

Before production editing, the added integrated contract and real-shell tests
were run. `qml_waveform_test` passed its lower-level continuous cursor check;
`integrated_shell_contract_test` failed with `Integrated base waveform must
remain unplayed while its cursor follows playback`; the full integrated theme
test also failed because the required clip and played items were absent.

After the minimal production change, the regression tests cover:

- same-bucket fractional positions `0.001` and `0.0011`, plus `0.1234`,
  `0.5005`, and `0.999`, with strictly increasing pixel clip width;
- reset to zero, zero/unknown duration, and paused cursor stability;
- full played duplicate size/position and exact clip binding;
- frequency Mix layers, colours, strength, amplitude, density, line width,
  analysis progress, visible-range synchronization, and spectrum peaks.

## Baseline-contract correction

Two old `tst_integrated_theme.qml` assertions and two static-contract checks
were already incompatible with the base's Task 1 integration: `Main.qml`
injects `IntegratedPlayerControls`, while the old tests still searched for
classic `PlayerControls` objects (`playerSecondaryActions` and
`centerPlaybackControls`) and a removed `showListWindowButton: false` binding.
They were updated only to assert the current integrated layout's real controls,
its ordered actions, and the absence of obsolete controls. No production
control code was changed.

## Fresh verification

- VS2022 `VsDevCmd` Release build of `qml_main_window_test` and
  `qml_waveform_test`: passed.
- Required CTest selection: 7/7 passed.
  `playback_controller_test`, `waveform_item_test`, `qml_waveform_test`,
  `qml_main_window_test`, `integrated_shell_contract_test`,
  `qml_integrated_theme_test`, and `qml_mini_player_test`.
- Direct full `tst_integrated_theme.qml`: 26 passed, 0 failed.
- Direct full `tst_main_window.qml`: 114 passed, 0 failed, 1 skipped because
  `WM_DROPFILES` requires the native qwindows platform rather than offscreen.

Visual dark/light/system, DPI, and real-audio acceptance remain Task 13 work;
this task makes no packaging or full-release claim.
