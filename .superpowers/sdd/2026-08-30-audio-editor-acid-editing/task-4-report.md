# Task 4 report: ACID-style command targeting

## RED

1. `tests/core/event_edit_test.cpp` first named the missing public document
   operations for a split right-hand clip.  With the MSVC environment loaded,
   the focused build failed at the intended boundary:

   ```text
   AudioDocument has no member copyEvent/cutEvent/deleteEvent/silenceEvent/fadeEvent
   ```

2. `tests/qt/audio_editor_controller_test.cpp` then selected clip `"2"` while
   the time range selected left clip `"1"`.  Before routing changed, the
   controller test failed because `editor.silenceSelection` followed the time
   range; the selected right clip did not receive the edit.  Static inspection
   also confirmed `refreshActions()` required a time selection, so a lone
   `selectedEventId` could not enable clip commands.

## Implementation

- Added atomic document commands `copyEvent`, `cutEvent`, `deleteEvent`,
  `silenceEvent`, and `fadeEvent`.  Timeline-changing commands each use one
  `applyCandidate()` undo entry; copy updates only clipboard metadata.
- Routed cut, copy, delete, mute, fade-in, and fade-out to a valid selected
  decimal event ID before falling back to the existing time-range command.
- Kept crop time-range-only, enabled clip commands when either a valid range
  or event selection exists, and refreshed action availability when event
  selection changes.
- After paste, identify the fresh event ID from before/after snapshots and
  select its decimal-string representation.

## Verification

```powershell
cmake --build build/agent-core-release --config Release --target event_edit_test --parallel 4
ctest --test-dir build/agent-core-release -C Release -R '^event_edit_test$' --output-on-failure

cmake --build build/agent-controller-release --config Release --target audio_editor_controller_test --parallel 4
ctest --test-dir build/agent-controller-release -C Release -R '^audio_editor_controller_test$' --output-on-failure

git diff --check
```

Results: `event_edit_test` passed `1/1`; `audio_editor_controller_test` passed
`1/1` in 21.30 seconds.  The controller run emitted existing FFmpeg skipped
sample timestamp warnings but exited successfully.  Direct new-controller
coverage also passed: `5 passed, 0 failed`.

## Commit

`fix(editor): target clip commands before time selection`

## Risks

- Validation is limited to the two requested focused Release targets; no full
  CTest, Debug, QML visual, hardware playback, package, or deployment claim is
  made here.
- `timelineEventViews()` does not expose mute state, so selected-event mute is
  verified at document level; controller routing shares the same selected-ID
  priority branch and is covered for fade/delete/copy/paste plus action state.
