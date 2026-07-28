# AgPlayer Phase 1 Import and Library Plan

**Goal:** Prove every supported format can be imported through the production
controller, persisted, and restored after restart without skipped tests or
runtime warnings.

**Constraints:** Reuse `ImportController`, `LibraryModel`, `LibraryStore`, and
the existing FFmpeg-backed core. Do not add a second media library or external
FFmpeg executable dependency. Preserve the existing uncommitted library and
waveform changes.

## Task 1: Deterministic eight-format fixtures

**Files**

- Add: `tests/tools/format_fixture_generator.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `tests/core/format_matrix_test.cpp`

**Steps**

1. Change `format_matrix_test` so a missing fixture directory or file fails
   instead of silently skipping; run it and record the red result.
2. Add one small generator that calls the existing public `ag_transcode` API to
   create MP3, FLAC, AAC, M4A, OGG, Opus, and WMA from the generated WAV.
3. Add a CMake custom target that creates all eight fixtures under the build
   tree and make `format_matrix_test` depend on it.
4. Build and run only `format_matrix_test`; require eight real data rows to
   decode, play through the null backend, advance, and seek.
5. Commit only the generator, test, and CMake wiring.

## Task 2: Production import and restart smoke

**Files**

- Modify: `app/main.cpp`
- Add: `scripts/qa-library-smoke.ps1`

**Steps**

1. Write the smoke script first. It must fail until the production executable
   supports an explicit QA library path, folder import, and list screenshot.
2. Add minimal QA-only arguments:
   `--qa-library`, `--qa-import-folder`, and `--qa-screenshot-list`.
3. First launch: import the eight-format folder through
   `ImportController::importFolder`, wait for completion, capture the real list
   window, and exit.
4. Verify the persisted JSON contains exactly the eight canonical paths and
   expected formats.
5. Second launch: load the same JSON without importing and capture the restored
   list window.
6. Require both logs to contain no `WARN`, `ERROR`, or `FATAL`.
7. Inspect both screenshots and commit the production smoke.

## Task 3: Import progress and error visibility

**Files**

- Add: `app/qml/AgPlayer/components/ImportStatusPanel.qml`
- Modify: `app/qml/AgPlayer/Main.qml`
- Modify: `app/qml/AgPlayer/components/EmptyLibrary.qml`
- Modify: `app/CMakeLists.txt`
- Modify: `tests/qml/tst_main_window.qml`
- Modify: `tests/qml/tst_error_states.qml`

**Steps**

1. Add failing QML checks proving busy progress and import errors are visible
   from the main window, including when no track has been accepted.
2. Extract one small shared status component bound to
   `ImportController.busy`, `progress`, and `errors`.
3. Reuse it in the main and empty-library surfaces; remove duplicated
   progress/error markup.
4. Verify retry opens the existing file dialog and no new controller logic is
   added in QML.
5. Run focused QML tests and the production library smoke.

## Task 4: Phase acceptance

1. Build the entire Debug tree with warnings as errors.
2. Run all 37 regular tests plus the now non-skipping eight-format matrix.
3. Run `qa-main-smoke.ps1` and `qa-library-smoke.ps1`.
4. Run `git diff --check` and inspect runtime logs.
5. Update `docs/qa/phase-status.md` with only fresh evidence.
6. Refresh `codebase-memory-mcp`.

**Not in this phase:** audible device acceptance, waveform seeking, playlist
CRUD, 10,000-track UI performance, audio-tool export acceptance, or packaging.
