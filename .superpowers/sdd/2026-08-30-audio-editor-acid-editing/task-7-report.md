# Task 7 — QML interaction and responsive gate

## Delivered behavior

- The two previous QML failures are resolved at their sources.  `clearDocument()`
  now clears a stale playback error; the old no-source selection test now
  asserts its actual unavailable-playback error instead of inventing `playing`.
- A native production-window test imports the generated WAV through the real
  drop route and drives split, clip selection, copy/paste, trim, mute, fade,
  delete, undo, range loop and first Space playback with QTest mouse/keyboard
  input.
- Narrow 880×560 layout compacts only the waveform/action band.  It preserves
  the reference two-row 13px shortcut card, transport and status ordering
  without enlarging the window or covering controls.  Wide reference geometry
  is unchanged.
- Clipboard paste now handles an occupied insertion point atomically.  It
  splits a containing event if necessary, ripples only later events by the
  clipboard span, and allocates clipboard clone IDs before auto-split IDs so
  the controller selects the new clone.

## RED evidence

- `test_referenceTransportShortcutAndStatusCopy`: status bar was visible after
  an earlier failed no-source play because `clearDocument()` left
  `errorMessage` populated.
- `test_selectionEnablesLoopAndTimelineClicksClearItPrecisely`: test expected
  playing from an untitled no-source document; routing correctly sought and
  cleared loop state, then playback reported unavailable.
- At 880×560, production geometry was transport `462..572`, shortcut
  `568..671`, page end `560`: 4px overlap plus 111px clipping.
- Native fixture E2E first failed at paste (`timelineEventViews.length` stayed
  2) because `EventTimeline::replace` correctly rejected overlap and old
  `pasteAt` supplied no insertion/ripple behavior.
- New core boundary RED: `pasteAt(400)` returned false after a full-track
  split/copy, before the paste implementation.

## GREEN verification

### Release

```text
ctest --test-dir build/task7-msvc-release --output-on-failure -R "^(event_edit_test|audio_editor_controller_test)$"
# 2/2 passed

ctest --test-dir build/task7-msvc-release --output-on-failure -R "^(qml_audio_editor_test|qml_audio_editor_native_input_test)$"
# 2/2 passed
```

- `event_edit_test`: 15 passed.  This includes full-track boundary insertion
  with 1000→1600 frames and one Undo, an internal playhead split preserving
  source ranges, collision ripple, and gap paste.
- `audio_editor_controller_test`: CTest fixture run passed.  It includes stale
  error clearing and selecting clone ID 2 rather than auto-right ID 3.
- `qmllint --json - -I app` reports both changed production QML files as
  success with no warnings.

### Debug

The isolated `build/task7-msvc-debug` focused QML CTest run passed:

```text
ctest --test-dir build/task7-msvc-debug --output-on-failure -R "^(qml_audio_editor_test|qml_audio_editor_native_input_test)$"
# 2/2 passed (46.9s)
```

## Remaining limits

- No hardware output/device latency or human listening pass was performed.
- No external desktop screenshot or pixel-diff exists; responsive acceptance
  is measured by production QML geometry and native window interaction.
