# Task 3 report — cinematic spatial lyrics and semantic settings groups

## Scope and clean-room statement

- Implemented only in `LyricsPanel.qml`, `ImmersiveControlPanel.qml`, and the
  existing immersive QML integration test. `ImmersiveSurface.qml` already met
  the tested safe-zone contract and was not changed.
- No uploaded HTML, screenshot, image, or video was opened or compared. No
  external repository code, layout, timing, constant, asset, shader, or
  algorithm was copied or adapted. The presentation and numeric values are an
  original native-QML implementation derived from `task-3-brief.md`.
- No C++, renderer, shader, waveform, queue, lyrics service/model/timing,
  playback, window, dependency, persistence key, or packaging file changed.
- The solution uses existing `Text`, `Rotation`, `ParallelAnimation`, layouts,
  and controller properties. It adds no QtQuick3D, ShaderEffect, blur pass,
  timer, dependency, or new user-facing setting.

## Baseline and RED evidence

The untouched Release integration suite passed before test edits:

```powershell
ctest --test-dir build/release -C Release `
  -R '^qml_immersive_integration_test$' --output-on-failure
```

Result: `1/1` CTest test and `12/12` QML functions passed.

Each behavior was then introduced test-first and run directly through the
Release Quick Test executable with the same offscreen/software environment as
CTest. Exact behavior REDs were:

1. `test_spatial_lyrics_have_bounded_cinematic_hierarchy`: `11 passed,
   1 failed`; failed at the missing `cinematicLyricsStage` /
   `cinematicLyricsPerspective` selectors before any production edit. The
   case also requires current-line scale/opacity dominance and a constrained
   long line using `Text.Wrap`, `maximumLineCount == 2`, and right elision.
2. `test_spatial_lyric_placement_is_mirrored_and_non_spatial_stays_flat`:
   `12 passed, 1 failed`; the existing left tilt was `-22` degrees at maximum
   depth and failed the independently specified `abs(angle) <= 18` bound.
3. `test_spatial_lyrics_settle_latest_line_without_residual_animation`:
   `14 passed, 1 failed`; failed because the bounded settle animation did not
   exist. The case subsequently covers latest-wins rapid updates, empty
   context lines, Loading/NotFound/Offline/Error fallbacks, non-spatial mode,
   disabled presentation, and hidden lyrics.
4. `test_dynamics_controls_are_grouped_by_meaning_and_remain_wired`:
   `15 passed, 1 failed`; failed because the terrain/light/motion/impact group
   selectors did not exist. The case uses a disposable real control panel and
   exercises controller round-trips rather than source-text assertions.

An initial build attempt without the Visual Studio developer environment
failed to locate the C++ standard library and was discarded as invalid RED
evidence. All subsequent builds loaded `VsDevCmd.bat` first.

## Cinematic lyric behavior

Stable test selectors added:

- `cinematicLyricsStage`
- `cinematicLyricsPerspective`
- `cinematicLyricsSettleAnimation`
- Existing `previousLyricLine`, `currentLyricLine`, and `nextLyricLine` names
  remain unchanged.

Spatial-mode behavior:

- The current line is the sole full-opacity focal anchor. Its final scale is
  `1.04..1.09` across the existing depth range; previous and next context lines
  remain smaller and quieter.
- Left/right placement uses a mirrored Y-axis rotation of `6..16` degrees;
  center remains frontal. Existing left/center/right text alignment is
  preserved.
- Every lyric label wraps only in spatial mode, is capped at two visual lines,
  and elides beyond that bound. Empty previous/next lines do not occupy the
  spatial stack.
- A Ready current-line change restarts one `ParallelAnimation`: opacity settles
  in `180 ms`, scale in `220 ms`, both with `OutCubic`. Restart is explicitly
  stop/reset/start, so queued text does not leave residual animation and the
  latest bound text remains authoritative.
- Loading, NotFound, Offline, and Error fallback text is immediately readable
  without the entrance animation. Spatial-mode off, item disabled, or lyrics
  hidden stops and resets the animation.
- Root opacity is untouched and remains bound exclusively to the existing
  `lyricOpacity` user setting.

Non-spatial/shared-player behavior remains non-cinematic: rotation is zero,
current scale/opacity remain `1`, wrapping remains `NoWrap` with one line, the
existing context-depth scale formulas remain unchanged, and the glass panel,
offset/retry/import actions, and manual-follow pause are unchanged.

## Real-surface safe-zone evidence

The real existing `ImmersiveSurface` was exercised in windowed immersive mode
for Left, Center, and Right placement at both extremes:

- position X/Y `0/0`, size/depth `60/0`;
- position X/Y `100/100`, size/depth `140/100`.

All combinations already remained at least `20 px` inside both horizontal
edges, below a `56 px` top safe zone, and at least `12 px` above the existing
waveform. This characterization passed before any surface edit, so no
unnecessary `ImmersiveSurface.qml` change was made.

## Settings grouping and unchanged wiring

The existing controls are now grouped without changing their property names,
ranges, defaults, values, toggle semantics, or renderer-facing controller:

- Terrain: `inputCompression`, `audioResponse`, `responseRange`,
  `subjectClarity`.
- Light: `centerHighlight`, `depthOfField`, `songAdaptiveColorEnabled`,
  `streamHighlightEnabled`.
- Motion: `autoRotateSpeed`, `rhythmSensitivity`, `autoRotate`,
  `idleBreathingEnabled`, `floatingCubesEnabled`.
- Impact: `rhythmStrength`, `ripplesEnabled`, `burstEnabled`,
  `meteorsEnabled`.

All nine `dynamicSlider_*` and all eight `effectToggle_*` object names remain
unchanged. Slider `from`, `to`, controller-bound `value`, and `onMoved` logic
were moved intact; toggle `checked`, the existing `autoRotate` numeric special
case, and `onToggled` logic were moved intact. The test exercises one slider
in every group and one toggle in every applicable group, then restores all
controller values in cleanup.

The panel remains in the existing vertical `ScrollView`. Its
`contentWidth: availableWidth`, horizontal `AlwaysOff` policy, and forced
`contentX == 0` binding were not changed; the existing real-panel regression
continues to assert no horizontal overflow.

## GREEN and regression evidence

After the final minimal QML edits, the direct Quick Test run passed all
`16/16` QML functions. The full CTest integration target was then repeated
three times consecutively:

```powershell
ctest --test-dir build/release -C Release `
  -R '^qml_immersive_integration_test$' `
  --repeat until-fail:3 --output-on-failure --no-tests=error
```

Result: `3/3` consecutive executions passed (`16.64 s`). One earlier complete
run exposed an existing queue-open `140 ms` timing assertion once; the lyric
case passed in that run, no queue code/test was changed, and the same suite
then passed once plus the final three consecutive executions.

Focused Release regression:

```powershell
ctest --test-dir build/release -C Release `
  -R '^(qml_immersive_integration_test|player_experience_controller_test|window_controller_test)$' `
  --output-on-failure --no-tests=error
```

Result: `3/3` tests passed (`8.77 s`).

QML lint:

```powershell
cmake --build build/release --config Release `
  --target agplayer_app_qml_qmllint --parallel 2
```

Result: exit `0`. It reported only pre-existing informational unused-import
messages in `Theme.qml`, `WaveformSession.qml`, and `SharedWaveformView.qml`;
neither changed Task 3 file produced a warning.

`git diff --check` passed with no whitespace error; Git printed only the
repository's LF-to-CRLF checkout notices.

## Remaining risks

- Per the Task 3 boundary, no GUI was launched and no screenshot was produced.
  Task 4 owns real-window usability and internal deterministic visual evidence.
- The safe-zone integration ran in the current Windows offscreen test window.
  DPI/platform visual evidence remains a Task 4 concern; no macOS/Linux claim
  is made.
- Qt Quick's `Text` rendering and exact line breaks vary with platform fonts;
  acceptance intentionally asserts a two-line maximum rather than a specific
  glyph break.
