# Task 1 report: experience state and audio visual features

## Implementation

- Added `PlayerExperienceController`, a QML-ready persisted state owner for
  immersive mode, host mode, lyrics visibility, panel visibility, desktop mouse
  passthrough, quality, color mode/five colors, seven visual parameters, five
  effect toggles, and the eight visual-only EQ gains.
- Preserved `SettingsController.playerShellMode` as the only shell-layout
  source. `togglePlayerShellMode()` only calls that existing setter and has no
  playback dependency or command path.
- Added `AudioVisualFeatureController`, which only attaches to
  `PlaybackController::spectrumChanged` while its explicit `active` state is
  true. It derives eight contiguous 16-bin bands, mean energy, positive-change
  spectral flux, and low/high transient kick/snare pulses; it neither decodes
  media nor calculates an FFT.
- Registered both controllers with the existing QML registration function and
  constructed the actual app instances during startup. Test QML harnesses retain
  compatible default singleton factories through optional registration arguments.
- Added focused controller tests and its CTest target.

## Files

- `qt/src/player_experience_controller.hpp`
- `qt/src/player_experience_controller.cpp`
- `qt/src/audio_visual_feature_controller.hpp`
- `qt/src/audio_visual_feature_controller.cpp`
- `qt/src/qml_registration.hpp`
- `qt/src/qml_registration.cpp`
- `app/main.cpp`
- `qt/CMakeLists.txt`
- `tests/CMakeLists.txt`
- `tests/qt/player_experience_controller_test.cpp`

## TDD evidence

### RED

Command:

```powershell
cmake --build build/debug --target player_experience_controller_test --config Debug
```

Observed expected failure before production controller files existed:

```text
tests\qt\player_experience_controller_test.cpp(1): fatal error C1083:
cannot open include file: 'audio_visual_feature_controller.hpp': No such file or directory
ninja: build stopped: subcommand failed.
```

### GREEN

The plain shell lacks the Visual Studio standard-library environment. Re-ran the
same target from `VsDevCmd` after the minimal implementation:

```powershell
& cmd.exe /d /c 'call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && cmake --build build\debug --target player_experience_controller_test --config Debug'
ctest --test-dir build/debug -R "^player_experience_controller_test$" --output-on-failure
```

Output:

```text
1/1 Test #65: player_experience_controller_test ... Passed
100% tests passed, 0 tests failed out of 1
```

## Regression and build verification

Commands:

```powershell
& cmd.exe /d /c 'call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && cmake --build build\debug --target player_experience_controller_test settings_controller_test playback_controller_test AgPlayer --config Debug'
ctest --test-dir build/debug -R "^(player_experience_controller_test|settings_controller_test|playback_controller_test)$" --output-on-failure
git diff --check
```

CTest output:

```text
1/3 Test #50: playback_controller_test ............ Passed    4.65 sec
2/3 Test #60: settings_controller_test ............ Passed    0.83 sec
3/3 Test #65: player_experience_controller_test ... Passed    0.10 sec
100% tests passed, 0 tests failed out of 3
```

The Debug `AgPlayer` target also built and deployed its Qt runtime successfully.
`git diff --check` completed without whitespace errors.

## Self-review

- State is intentionally independent: switching immersive state does not alter
  lyrics or shell state, and theme layout remains owned by `SettingsController`.
- The visual feature controller has a single input path from the existing
  published 128-bin spectrum. It disconnects while inactive, and direct
  spectrum processing also short-circuits while inactive.
- Tests cover defaults, persistence, invalid enum/color/list fallback,
  independent immersive/lyrics state, shell toggling, band/energy/flux/pulse
  derivation, and inactive zero-work counters.

## Concerns / handoff

- Task 4 must set `AudioVisualFeatureController.active` from actual renderer
  host visibility (including hidden/minimized transfer) rather than merely from
  the saved immersive-mode preference. This task intentionally leaves it off by
  default because no renderer host exists yet.
- The documented pre-existing `qml_main_window_test` failure was not changed or
  used as a success criterion.
