# Task 7 — QML interaction and responsive gate

## Delivered behavior

- The two previous QML failures are resolved at their sources.  `clearDocument()`
  now clears a stale playback error; the old no-source selection test now
  asserts its actual unavailable-playback error instead of inventing `playing`.
- A native production-window test imports the generated WAV through the real
  drop route.  The desktop journey drives split, clip selection, copy/paste,
  trim, mute, fade, delete, undo, range loop and first Space playback with
  QTest mouse/keyboard input.  The separate 880×560 smoke intentionally covers
  only real import, bounds, selection/Escape and click/Space playback.
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
- Three application-owned real-WAV screenshots exist, but no accepted
  reference pixel-diff or externally driven desktop interaction pass exists.
  Responsive acceptance is measured by production QML geometry and native
  window interaction.

## Review fix round 1

### Reopened RED

- The `b0dc59f` screenshots in `build/qa/audio-editor/task7-final` showed that
  the initial 880×560 fix had tested a bare 560px page, while the real shell
  provides only 441px.  Transport/shortcut content was clipped; at 1280 the
  card overlapped transport by 4px.
- Debug native CTest had a 30-second timeout while real Windows fixture input
  takes longer under Debug.  The test was killed at 31.23s even though the
  same binary completes the journey; this was a harness timeout, not a reason
  to weaken an assertion.
- QA tools screenshots used an unconditional 1.5s delay and could capture the
  1672 editor before its asynchronous viewport waveform was available.

### Fixes

- Responsive layout now branches on actual editor-page height: the real shell
  measures 822px at 1672×941, 601px at 1280×720, and 441px at 880×560.
  At 880 the workspace is `y=140,h=83`, ruler `y=140,h=20`, track
  `y=160,h=48`, scrollbar `y=211,h=12`, transport `229..333`, shortcut card
  `341..408`, and status `416..441`.  At 1280 they are workspace
  `y=152,h=226`, ruler `y=152,h=44`, track `y=196,h=162`, scrollbar
  `y=366,h=16`, transport `390..502`, shortcut `509..576`, and status
  `576..601`.
- The native test timeout is 60s, while tests still use real URLs and native
  mouse/key events.  The narrow real-WAV smoke verifies the compact controls,
  real waveform-body selection, Escape cancellation, click playback and Space
  stop without repeating the full desktop edit journey.
- QA capture polls existing `hasDocument` and `viewportChannelPeaks` with a
  bounded 10-second readiness condition, failing without an artifact when the
  waveform cannot be produced.
- Exact occupied-paste split/ripple/source-range semantics remain in the C++
  core/controller tests.  The QML route checks event-count growth, selected
  clone identity and Undo/Redo instead of duplicating timeline arithmetic.
- Signalsmith sessions now use a fixed seed so realtime and offline renders are
  reproducible.  Release and Debug `document_renderer_test` and
  `editor_playback_stream_test` verify the shared automation path.

### Evidence boundary

- Regenerated real-WAV artifacts are in
  `build/qa/audio-editor/task7-final-verified` for 1672×941, 1280×720 and
  880×560.  Visual inspection confirms full 880 transport/two shortcut rows
  and no 1280 transport/card overlap.  This is not a pixel-level reference
  pass: the implementation still differs materially from the supplied image
  in palette, waveform colour, density and control styling.
- Hardware output, audible quality, latency and subjective listening remain
  outside this automated gate.

## Final verification — 2026-08-31

- Full Release and Debug builds completed successfully after the final CMake
  reconfigure.  Qt 6.7 `qmllint` reports no warnings for the two changed QML
  production files.
- The 13-test editor matrix passed in both full suites: renderer, realtime
  stream, event edit/timeline, peak pyramid, tools E2E, viewport, editor QML,
  controller, waveform item, native input, feature options and layout
  contract.  Debug's stabilized stream benchmark completed in 43.63s; the
  feature-option configure completed in 119.79s and now has 180s CI headroom.
- Release full CTest completed 108/112.  Its four non-green results are outside
  this diff: a one-off `pitch_shifter_test` abort that passed immediately when
  rerun, the historical 10K import O(N²) timeout, two pre-existing unclassified
  TagManagementPanel colours, and the 35s main-window timeout.  A direct full
  main-window run completed in 52.373s and exposed 98 pass / 9 historical
  drag/drop-or-thumbnail failures / 1 skip rather than an editor hang.
- Debug full CTest completed 105/113.  The editor matrix remained green.  The
  eight non-green results are in unchanged library, thumbnail, theme,
  main-window, deployment or Windows-shell tests; `queue_gapless_test` and
  `windows_shell_runtime_test` passed on isolated rerun.  No unrelated source
  was changed to hide these baselines.
- Real Release and Debug `AgPlayer.exe` QA startup smokes exited 0 after real
  WAV import and produced non-empty 880×560 artifacts at
  `build/qa/audio-editor/final-smoke-release-postreview-20260831.png` and
  `build/qa/audio-editor/final-smoke-debug-postreview-20260831.png`.
- `git diff --check` passes.  The independent standards review found no new
  Critical/Important gap; the Ponytail pass removed redundant QML paste math,
  duplicated compact interaction coverage, repeated layout branches and a
  second full feature-option configure.
