# AgPlayer Phase 6 — Audio Tools Completion

**Goal:** Make format conversion, six-track light editing, BPM speed adjustment,
pitch shifting, and metadata editing fully operational from QML through the
Qt bridge to the C++ core, with real preview, cancellation, output files, and
error recovery.

**Constraints:** Reuse the existing core and controllers, keep processing off
the UI thread, add no dependency, preserve four-language support, and do not
package an EXE.

**Status:** Completed on 2026-07-29. Debug build and 44/44 tests pass; all five
tool pages were visually verified in dark and light themes.

## Task 1 — Lock real controller behavior with integration tests

- Add one end-to-end Qt test for format conversion, BPM speed export, pitch
  export, metadata write/rename, and output collision handling.
- Extend light-editor tests for cut, copy, paste, delete, split, merge, crop,
  undo, and redo.
- Extend QML tests so every visible light-editor toolbar and preview control is
  connected to a real action.

## Task 2 — Complete the light-editor interaction contract

- Add selected-clip editing commands to `LightEditor`; keep a single in-memory
  clip clipboard and reuse the existing undo/redo snapshots.
- Split into an empty track, merge compatible adjacent fragments, and crop at
  the playhead without introducing a second timeline model.
- Wire the toolbar and selected-track preview to `AudioPreviewController`.
- Keep wheel zoom, shift-wheel pan, clip drag, trim handles, snapping, BPM
  unification, mute/solo/lock, and export active.

## Task 3 — Harden the four remaining processing controllers

- Make pitch-shift cancellation reach the native cancel token.
- Ensure controller destruction cannot leave background work using destroyed
  state.
- Validate input/output parameters before starting and preserve non-colliding
  output naming.
- Make batch conversion use bounded parallel jobs while keeping per-file status
  and aggregate progress thread-safe.

## Task 4 — Verify real files and production QML

- Export WAV/MP3/FLAC through the applicable tools and reopen every output with
  the native metadata/decoder API.
- Verify BPM adjustment, pitch shift, multitrack export, metadata application,
  cover handling, batch rename, preview/seek/volume, cancellation, and errors.
- Regenerate all four translation catalogs and require zero unfinished, empty,
  or corrupted entries.
- Capture every tool page in dark and light themes; inspect runtime logs.

## Task 5 — Review, regress, and commit

Run the focused tests, full Debug build/CTest suite, `git diff --check`, and a
focused review. Fix all Critical/Important findings, update the phase status,
and commit Phase 6 independently.
