# Task 5: 320 kbps default and two-row shortcuts

## Scope delivered

- A fresh `ProjectExportSettings` now defaults to `320000` bit/s.  The audio-editor
  new-project factory preserves that value, so the setting is serialized to a new
  `.agproj` and restored on reopen.
- The default is intentionally independent of the codec: no WAV export or codec
  selection logic changed.  Existing encoder behavior remains responsible for
  ignoring a lossy bitrate where WAV does not use one.
- The shortcut card is split into two fixed 13 logical-pixel horizontal rows: the
  first five command groups and the remaining four mouse/timeline groups.  At the
  1672-pixel reference width both natural row widths fit without scrolling; at
  1280x720 and 880x560 each row remains a `HorizontalFlick` and keeps 13-pixel
  labels, with the last label reachable at the row's maximum content offset.
- The card remains below the playback panel and the existing status overlay stays
  later in the visual stack; no reference-external controls were added.

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
