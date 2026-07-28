# AgPlayer Product Completion Design

**Status:** Approved by the user on 2026-07-28

## Goal

Turn the existing Qt 6/QML + C++17 codebase into a genuinely usable Windows
MVP whose visible controls work with real local audio. Automated unit tests are
supporting evidence only; a phase is complete only after the built application
passes the corresponding real-file and real-device workflow.

## Current Failure Baseline

- `Main.qml` sends an undefined `files` value from `FileDialog`, so the primary
  import flow fails before `ImportController` receives any URLs.
- Playback is then invoked without a valid queue and logs `AG_INVALID_ARGUMENT`.
- Waveform analysis has no valid current track because import never completed.
- `SettingsPage.qml` mixes layout-managed children with anchors, producing
  recursive-layout and undefined-behavior warnings.
- Existing tests overuse controller harnesses and the null audio backend. They
  do not prove that a user can import, hear, seek, or edit a real file.

## Delivery Strategy

Keep the existing C++ decoder, playback, waveform, DSP, and persistence
implementations where real integration tests prove them correct. Repair the
shortest shared path from QML to those components. Do not rewrite working core
code and do not add a second implementation beside an existing one.

The work is divided into independently shippable phases. Each phase follows:

1. Reproduce the user-visible failure.
2. Add the smallest failing automated check at the correct boundary.
3. Fix the shared root cause.
4. Run focused tests, then the full non-stress suite.
5. Launch the real executable with real audio and exercise the visible controls.
6. Inspect the runtime log and fix every new error or warning.
7. Perform the phase-specific manual acceptance check.
8. Commit only after every gate passes.

Failure of any gate blocks the next phase.

## Architecture Boundaries

- `core/`: Qt-independent decoding, playback, waveform analysis, DSP, editing,
  metadata, and C API. No QML or Qt UI dependency may be introduced.
- `qt/src/`: Qt adapters, application models, controllers, async jobs, settings,
  persistence, and platform integration.
- `app/qml/AgPlayer/`: presentation and direct user interaction only. QML calls
  registered controllers; it must not duplicate audio or persistence logic.
- `tests/core/`: deterministic codec, DSP, waveform, queue, and export tests.
- `tests/qt/`: adapter and model integration tests.
- `tests/qml/`: real component behavior and application workflow tests.
- `scripts/qa-*.ps1`: repeatable built-application smoke and visual matrices.

## Global Constraints

- Qt 6 / QML + C++17.
- Windows MVP first; preserve a Qt-independent C/C++ core for later platforms.
- Supported UI languages: Chinese, English, Thai, and Vietnamese only.
- Supported playback formats: MP3, WAV, FLAC, AAC, M4A, OGG, Opus, and WMA.
- No fake controls, placeholder pages, hidden no-op actions, ads, analytics, or
  audio uploads.
- No new dependency unless the installed stack cannot meet a measured
  requirement.
- Reuse before adding; delete unreferenced duplicate components and dead paths.
- Compiler warnings, QML errors, QML warnings, and runtime errors must be zero
  for the accepted workflow.
- Target memory below 60 MB, seek latency below 20 ms, 10,000-track library
  interaction at 60 FPS, and the smallest practical deployment.
- Do not package an installer until the user explicitly requests packaging.

## Phase Design

### Phase 0: Truthful Runtime Baseline

Stabilize the active worktree, preserve intentional uncommitted changes, add
real media fixtures, archive a clean runtime log, and create one executable
smoke flow. Remove unused `ComingSoonPage.qml` and any other resource proven to
have no reference. The accepted executable must start and close without QML
errors, missing resources, recursive layouts, or log encoding corruption.

### Phase 1: Import and Library Persistence

Repair multi-file selection, folder selection, drag-and-drop import, metadata
probing, duplicate handling, progress/error reporting, and library persistence.
The application must import every supported format through the visible UI and
restore the same library after restart.

### Phase 2: Real Playback and Waveform

Connect list selection and player controls to a valid core queue and real
Windows audio device. Complete play, pause, previous, next, seek, volume, and
repeat behavior. Generate/cache waveform layers asynchronously and support
solid, RGB, and spectrum modes with click/drag seeking and hover time.

Acceptance requires audible output on the target device and a waveform whose
position matches the heard audio.

### Phase 3: Playlist, Search, and Large Library

Complete playlist CRUD, favorite, rating, history, sorting, keyword highlighting,
star filtering, BPM dual-range filtering, manual BPM entry, 200 ms debounce,
empty results, and clear filters. Use `QAbstractListModel` plus QML `ListView`
delegate reuse; prove 10,000-track loading and scrolling with measured results.

### Phase 4: Settings, Themes, and Localization

Repair the scrollable settings page and ensure every visible setting can be
applied, saved, cancelled, restored, and reloaded. Validate dark, light, and
system modes including icons. Compile and inspect Chinese, English, Thai, and
Vietnamese translations page by page, with system-font fallback and no mojibake.

### Phase 5: Window System and Mini Player

Complete independent main, playlist, and mini windows; four-direction 15 px
snap; group drag; independent detach; synchronized minimize/hide; persisted
positions; always-on-top mini mode; and full mini-player controls.

### Phase 6: Five Audio Tools

Validate each tool independently with real input, audible preview, cancellable
processing, progress, output validation, and re-import:

1. Batch format conversion.
2. Six-track light editing with clip movement, trim, snap, zoom, history,
   fades, mute, BPM change, and beat alignment.
3. Speed/BPM adjustment with detection, markers, preview, and export.
4. Pitch adjustment with semitone/cents, duration preservation, voice
   protection, smoothing, preview, and export.
5. Batch metadata and filename editing.

Processing must not interrupt main-player playback unless exclusive device
ownership makes that impossible and a clear error is shown.

### Phase 7: Performance, Recovery, and Portability

Measure memory, seek latency, library load/scroll, waveform generation, and
long-run playback. Cover corrupt files, missing files, device loss, disk-full,
cancel, restart, and crash recovery. Confirm the public C API remains free of Qt
types and run desktop portability builds where the environment is available.

### Phase 8: Final Product Acceptance

Create a requirement-to-evidence matrix. Click every visible control, play every
format, export from every tool, inspect all theme/language combinations, run
stress tests, inspect logs, and perform target-device listening. Packaging is a
separate, user-authorized phase after this gate.

## Testing Policy

- Unit tests cannot close a user-visible requirement by themselves.
- Mock/null-backend tests cover deterministic failure handling only.
- At least one test per critical flow must launch the production executable or
  production QML module with production controllers.
- Real-file fixtures must include Unicode paths and metadata.
- Real-device listening is mandatory for playback completion.
- Screenshot comparison verifies layout, not functionality.
- The final report distinguishes automated, visual, and human evidence.

## Lightweight and Deletion Policy

Before adding a helper, search the graph for an existing function and trace its
callers. Fix a shared function once rather than patching each QML caller.
Delete unused QML pages, icons, dead compatibility branches, duplicate helper
functions, obsolete tests, and abandoned resources only after reference and
runtime-resource checks prove they are unused. Track binary size and working
set after every phase so regressions are caught immediately.

