# AgPlayer Phase 0 Runtime Baseline Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Restore a truthful production-executable baseline by fixing the broken
file-import action, eliminating settings-page QML warnings at their shared root,
removing the unused placeholder page, and adding an isolated real-executable
smoke gate.

**Architecture:** Keep the current C++ core and Qt controllers. Fix the QML
boundary that passes selected file URLs, correct the common `SettingRow`
container instead of patching every child, and reuse the production executable
for QA with isolated `QStandardPaths` storage.

**Tech Stack:** Qt 6 Quick/QML, C++17, Qt Quick Test, CTest, PowerShell, existing
FFmpeg/miniaudio runtime.

## Global Constraints

- Work in `D:\ai\AgPlayer\.worktrees\revised-ui`.
- Preserve the six pre-existing uncommitted files; do not stage or revert them.
- Reuse the existing `ImportController`, `LibraryModel`, `RuntimeLog`, and QA CLI.
- Add no dependency and no alternate importer, player, logger, or settings model.
- A passing unit test is insufficient: the production executable smoke must pass.
- Runtime `[WARN]` and `[ERROR]` lines introduced by the smoke flow fail the gate.
- Delete only code whose references and QML resource registration are both absent.
- Do not package an EXE or installer.

---

### Task 1: Make the visible file-import action pass real URLs

**Files:**
- Modify: `app/qml/AgPlayer/Main.qml:43-67`
- Modify: `tests/qt/qml_main_window_test_main.cpp:44-88`
- Modify: `tests/qml/tst_main_window.qml:43-57`
- Modify: `tests/CMakeLists.txt:372-395`

**Interfaces:**
- Consumes: `ImportController.importUrls(QList<QUrl>)`,
  `LibraryModel.count`, `AGPLAYER_TEST_AUDIO`.
- Produces: QML function `importFiles(urls)` used by both the native
  `FileDialog` and the QML integration test.

- [ ] **Step 1: Add the failing real-import QML test**

In `qml_main_window_test_main.cpp`, expose the generated fixture before loading
`Main.qml`:

```cpp
const QString fixture =
    QString::fromLocal8Bit(qgetenv("AGPLAYER_TEST_AUDIO"));
engine->rootContext()->setContextProperty(
    "testAudioUrl", QUrl::fromLocalFile(fixture));
```

Add `<QUrl>` and fail setup when the fixture is empty.

In `tst_main_window.qml`, add:

```qml
function test_import_files_reaches_real_controller() {
    compare(LibraryModel.count, 0)
    mainWindow.importFiles([testAudioUrl])
    tryVerify(function() { return LibraryModel.count === 1 }, 5000)
}
```

In `tests/CMakeLists.txt`, add `decoder_fixture` as a dependency of
`qml_main_window_test` and append
`AGPLAYER_TEST_AUDIO=${SINE_WAV_FIXTURE}` to its test environment.

- [ ] **Step 2: Run the test and verify the regression is exposed**

Run:

```powershell
ctest --test-dir build/msvc-debug -C Debug -R '^qml_main_window_test$' --output-on-failure
```

Expected: FAIL because `Main.importFiles` does not exist.

- [ ] **Step 3: Route the dialog through one reusable QML function**

Change `Main.qml` to:

```qml
function importFiles(urls) {
    ImportController.importUrls(urls)
}
```

Change the dialog handler to:

```qml
onAccepted: mainWindow.importFiles(selectedFiles)
```

Do not add a second importer or copy controller logic into QML.

- [ ] **Step 4: Run focused import and QML tests**

Run:

```powershell
ctest --test-dir build/msvc-debug -C Debug -R '^(import_controller_test|qml_main_window_test)$' --output-on-failure
```

Expected: both tests PASS and the QML test imports the generated WAV fixture.

- [ ] **Step 5: Commit only Task 1 files**

```powershell
git add app/qml/AgPlayer/Main.qml tests/qt/qml_main_window_test_main.cpp tests/qml/tst_main_window.qml tests/CMakeLists.txt
git commit -m "fix: connect file dialog to real import"
```

### Task 2: Eliminate settings layout warnings at the shared root

**Files:**
- Modify: `app/qml/AgPlayer/SettingsPage.qml:407-429`
- Modify: `tests/CMakeLists.txt:390-395`

**Interfaces:**
- Consumes: QML `RowLayout` default-child behavior.
- Produces: `SettingRow.content`, a default alias whose children belong to the
  plain `contentContainer` item rather than the outer layout.

- [ ] **Step 1: Turn QML warnings into a failing test**

Append `QT_FATAL_WARNINGS=1` to the `qml_main_window_test` environment in
`tests/CMakeLists.txt`.

- [ ] **Step 2: Run the settings-opening test and verify failure**

Run:

```powershell
ctest --test-dir build/msvc-debug -C Debug -R '^qml_main_window_test$' --output-on-failure
```

Expected: FAIL while opening settings with
`Detected anchors on an item that is managed by a layout`.

- [ ] **Step 3: Fix the common container instead of every setting**

In `SettingsPage.qml`, replace:

```qml
property alias content: contentContainer.children
```

with:

```qml
default property alias content: contentContainer.children
```

This moves every declarative `SettingRow` child under `contentContainer`,
where anchors are valid, without duplicating fixes across all rows.

- [ ] **Step 4: Re-run the fatal-warning QML test**

Run:

```powershell
ctest --test-dir build/msvc-debug -C Debug -R '^qml_main_window_test$' --output-on-failure
```

Expected: PASS with no QML warning or recursive-layout abort.

- [ ] **Step 5: Commit only Task 2 files**

```powershell
git add app/qml/AgPlayer/SettingsPage.qml tests/CMakeLists.txt
git commit -m "fix: route setting content outside row layout"
```

### Task 3: Remove the unused placeholder page

**Files:**
- Delete: `app/qml/AgPlayer/components/tools/ComingSoonPage.qml`
- Modify: `app/CMakeLists.txt:99`

**Interfaces:**
- Consumes: QML module resource list.
- Produces: no new interface; removes an unreferenced component.

- [ ] **Step 1: Prove the page has no caller**

Run:

```powershell
rg -n "ComingSoonPage|Coming Soon" app qt tests
```

Expected: only the page itself and its `app/CMakeLists.txt` registration match.
If another runtime reference appears, stop and retain the page.

- [ ] **Step 2: Remove the page and its resource registration**

Delete `ComingSoonPage.qml` and remove its entry from
`qt_add_qml_module(... QML_FILES ...)`.

- [ ] **Step 3: Build and run all QML tests**

Run:

```powershell
cmake --build build/msvc-debug --config Debug
ctest --test-dir build/msvc-debug -C Debug -R '^qml_' --output-on-failure
```

Expected: build succeeds and every QML test passes.

- [ ] **Step 4: Commit the deletion**

```powershell
git add app/CMakeLists.txt app/qml/AgPlayer/components/tools/ComingSoonPage.qml
git commit -m "chore: remove unused placeholder page"
```

### Task 4: Add an isolated production-executable smoke gate

**Files:**
- Modify: `app/main.cpp:47-111`
- Create: `scripts/qa-main-smoke.ps1`
- Modify: `docs/qa/phase-status.md`

**Interfaces:**
- Consumes: existing `--qa-play`, `--qa-screenshot-main`, production
  `AgPlayer.exe`, generated `sine-440hz.wav`, `RuntimeLog::defaultLogPath()`.
- Produces: CLI flags `--qa-test-mode` and `--qa-log <path>`; script exit code
  `0` only when the real executable imports, starts playback, renders a
  screenshot, exits, and adds no runtime warning/error.

- [ ] **Step 1: Add isolated QA mode to the existing CLI parser**

Move `RuntimeLog::install()` below the existing CLI argument block. Parse
`--qa-test-mode` into a boolean and `--qa-log <path>` into a `QString`. Before
installing the log or constructing `LibraryModel`, `SettingsController`, or any
path-backed service, call:

```cpp
if (qaTestMode) {
    QStandardPaths::setTestModeEnabled(true);
}
RuntimeLog::install(qaLogPath);
```

Keep the flag development-only and do not create a second application entry
point. An empty `qaLogPath` retains `RuntimeLog`'s current default-path behavior.

- [ ] **Step 2: Create the smoke script**

`scripts/qa-main-smoke.ps1` must:

1. Accept `-BuildDir`, defaulting to `build/msvc-debug`.
2. Resolve `app/AgPlayer.exe` and
   `tests/fixtures/sine-440hz.wav`.
3. Prepend Qt and vcpkg runtime directories to `PATH`, using the same directory
   rules already proven in `qa-audio-tools-matrix.ps1`.
4. Allocate explicit temporary PNG and UTF-8 log paths.
5. Launch:

```powershell
AgPlayer.exe --qa-test-mode --qa-log <log> --qa-play <fixture> --qa-screenshot-main <png>
```

6. Enforce a 20-second timeout and fail on non-zero exit.
7. Require a non-empty PNG.
8. Read the explicit UTF-8 log and fail if it contains `[WARN]` or `[ERROR]`.
9. Restore the caller's original `PATH` in `finally`.

- [ ] **Step 3: Run the production smoke**

Run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/qa-main-smoke.ps1
```

Expected: exit `0`, a valid main-window screenshot, no new warning/error log
line, and no change to the user's normal library/settings paths.

- [ ] **Step 4: Run the complete Phase 0 gate**

Run:

```powershell
cmake --build build/msvc-debug --config Debug
ctest --test-dir build/msvc-debug -C Debug --output-on-failure -E '^library_stress_test$'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/qa-main-smoke.ps1
git diff --check
```

Expected: zero compiler warnings/errors, all non-stress tests pass, smoke passes,
and no whitespace errors.

- [ ] **Step 5: Record evidence and commit**

Update `docs/qa/phase-status.md` with the exact commands and observed results.

```powershell
git add app/main.cpp scripts/qa-main-smoke.ps1 docs/qa/phase-status.md
git commit -m "test: gate production runtime baseline"
```

## Phase 0 Completion Gate

- Visible file import reaches the real `ImportController`.
- The generated WAV appears in the real `LibraryModel`.
- Opening settings emits no QML warning.
- The production executable loads and starts the fixture in isolated storage.
- The smoke screenshot is non-empty.
- Newly emitted runtime logs contain no warning or error.
- All non-stress tests pass.
- Pre-existing uncommitted files remain untouched and unstaged.
- The next detailed plan is Phase 1 import and persistence; it is written only
  after this gate passes.
