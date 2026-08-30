# Task 5: 320 kbps default and two-row shortcuts

## Scope delivered

- A fresh `ProjectExportSettings` now defaults to `320000` bit/s.  The audio-editor
  new-project factory preserves that value, so the setting is serialized to a new
  `.agproj` and restored on reopen.
- The default is intentionally independent of the codec: no WAV export or codec
  selection logic changed.  Existing encoder behavior remains responsible for
  ignoring a lossy bitrate where WAV does not use one.
- The shortcut card is split into two 16 logical-pixel horizontal rows with
  13-pixel labels: the first five command groups and the remaining four
  mouse/timeline groups.  At the
  1672-pixel reference width both natural row widths fit without scrolling; at
  1280x720 and 880x560 each row remains a `HorizontalFlick` and keeps 13-pixel
  labels, with the last label reachable at the row's maximum content offset.
- The card remains below the playback panel and the existing status overlay stays
  later in the visual stack; no reference-external controls were added.

## Follow-up review correction

Independent review found that a 13-pixel font can have an implicit text height
greater than 13, so the earlier 13-pixel rows could clip vertically.  The rows
now use 16 logical pixels, and every label contract checks
`implicitHeight <= row.height`.  The card is moved to y=730 at the 1672x941
reference geometry: its bottom is exactly the status bar's y=797, so a visible
status overlay no longer covers either row.

The two `WheelHandler`s are direct Flickable children with `target` set to their
full Flickable row.  Runtime Flickable parenting correctly exposes them below
the row `contentItem`, rather than the short content `Row`; the QuickTest checks
that parent and the full-row target for both handlers.

TDD follow-up: the new height/implicit-height/handler/overlay tests failed first
against the 13-pixel-row implementation.  After the fix, the full XML QuickTest
run reported 55 passed test functions and only the two pre-existing failures
listed below.

## TDD evidence

- RED: added `freshExportSettingsPersistDefault320Kbps`, which failed against the
  previous zero default before implementation.  The two-row QuickTest contracts
  also failed against the previous single `editorShortcutFirstRow`/nine-group UI.
- GREEN: implemented the defaults and two rows, then rebuilt the Release targets.

## Verification

| Command / check | Result |
| --- | --- |
| `cmake --build build/agent-controller-release --config Release --target project_document_test --parallel 1` | passed after loading `vcvars64.bat` |
| `ctest --test-dir build/agent-controller-release -C Release --output-on-failure -R '^project_document_test$'` | passed (1/1) |
| `D:/Qt/6.7.0/msvc2019_64/bin/qmllint.exe -I build/agent-controller-release/app app/qml/AgPlayer/components/tools/AudioEditorPage.qml` | passed |
| `ctest --test-dir build/agent-controller-release -C Release -V --output-on-failure -R '^audio_editor_controller_test$'` | passed (1/1) |
| Full `qml_audio_editor_test` XML log | 57 test functions passed; 2 existing failures listed below.  All three Task 5 shortcut tests passed. |
| `git diff --check` | passed |

The full QML run is not green because of these two pre-existing, non-Task-5
failures (the RED run before the production change already returned exit code 2):

1. `test_referenceTransportShortcutAndStatusCopy` at `tst_audio_editor.qml:483`:
   `editorStatusBar.visible` is `true`, expected `false`.  This is state left by
   prior export/status activity; Task 5 leaves the status overlay in the same
   existing visual stack so it remains reachable.
2. `test_selectionEnablesLoopAndTimelineClicksClearItPrecisely` at
   `tst_audio_editor.qml:849`: `AudioEditorController.playing` is `false`,
   expected `true` in the selection-loop scenario.  This is playback behavior
   outside this task's default/export/layout scope.

No full CTest suite was claimed or run for this task.

## Final review correction: visible input viewport

The earlier follow-up description of direct `WheelHandler` children is superseded.
Qt Quick reparents a handler declared in a `Flickable` to `contentItem`, so it
cannot make the unused viewport to the right of a short content row receive a
wheel event.  Each shortcut row now has a transparent sibling `Item` in the
shortcut card, with geometry bound exactly to that row's viewport.  A single
full-size `MouseArea` inside that item accepts `Qt.NoButton`, has no hover state,
cannot steal a drag, and handles only `wheel`; it clamps `contentX` from
`wheel.angleDelta.y`.  There is no competing same-row `WheelHandler`.

The screenshot correction is also reflected in the final layout: the keyboard
icon/title and the first five groups share the first row; the remaining four
groups form the second row below a horizontal divider.  All label text remains
13 px and each group retains its vertical divider.  Reducing only the previous
extra per-group padding lets both natural rows fit at 1672 px without scrolling.

The final QuickTest RED cases covered overlay-to-row geometry, both wide-row
blank areas, and a real 1280x720 wheel event that advances an overflowing,
visible row to its maximum `contentX`.  The final GREEN XML run reports 57
passing test functions.  The two failures remain the unrelated baseline cases
above (`test_referenceTransportShortcutAndStatusCopy:483` and
`test_selectionEnablesLoopAndTimelineClicksClearItPrecisely:975`).

At 880x560 the existing page's narrow layout positions the shortcut card at
y=568 while the page clips at y=560; it is therefore not a physically reachable
wheel target.  Task 5 preserves its static two-row, 13-px, horizontal-access
contract there, but does not falsely claim a real pointer event in that clipped
location.  Making the card visible at that size requires responsive vertical
reflow (without covering the playback controls), tracked as a Task 7 concern.
