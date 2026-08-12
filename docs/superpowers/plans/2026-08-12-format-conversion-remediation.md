# Format Conversion Remediation Implementation Plan

> Superseded by `docs/superpowers/plans/2026-08-13-format-conversion-full-replica.md`, which covers the user-approved full UI replica and complete functional scope.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver the reference-image format conversion workbench with only real FFmpeg-backed controls and safe batch output.

**Architecture:** Extend the existing `FormatConverter` queue adapter rather than replace the transcode core. Its task data and invokable configuration surface become the single QML contract; the QML page consumes it to render the four-zone reference layout. Existing staged output validation and atomic commit remain the final conversion path.

**Tech Stack:** Qt 6/QML, C++17, Qt Test/QuickTest, FFmpeg libraries, CMake/Ninja/MSVC x64.

## Global Constraints

- Do not invoke `ffmpeg.exe`; use only the linked FFmpeg library APIs.
- Do not add fake tasks, fake progress, hard-coded available formats, or silent codec/container fallback.
- Change only the format converter, its direct tests, translations, and this documentation unless a minimal shared change is unavoidable.
- Preserve current staged-file validation and atomic commit behaviour.
- Do not package an installer or executable.

---

### Task 1: Converter plan and task-view contract

**Files:**
- Modify: `qt/src/format_converter.hpp`
- Modify: `qt/src/format_converter.cpp`
- Modify: `tests/qt/audio_tools_end_to_end_test.cpp`

**Interfaces:**
- Produces `Q_INVOKABLE QVariantMap previewSelected(...)` and a `pendingConfirmation` task state.
- Produces task maps with `selected`, `stage`, `errorDetail`, `resolvedProfile` and real `progress`.

- [ ] **Step 1: Write failing tests**

Add `formatConverterPreflightReturnsResolvedProfileBeforeStarting` to load a generated WAV and assert `previewSelected("mp3", 192000, 44100, 2, ...)` returns `ready == true`, `requiresConfirmation == false`, and a non-empty `resolvedProfile`. Add a parameter mismatch input that asserts `requiresConfirmation == true` and no task becomes `Converting`.

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build/debug --target audio_tools_end_to_end_test && ctest --test-dir build/debug -R '^audio_tools_end_to_end_test$' --output-on-failure`

Expected: FAIL because `previewSelected` is not exposed.

- [ ] **Step 3: Implement minimal preflight and task data**

Add the invokable, derive profile data from the selected real output format and inputs, reject unavailable output/invalid parameters, and return a stable QML map. Extend `files()` with selected/stage/detail data and keep running jobs immutable.

- [ ] **Step 4: Re-run focused test**

Run the Step 2 command. Expected: PASS.

### Task 2: Real output settings and conflict choices

**Files:**
- Modify: `qt/src/format_converter.hpp`
- Modify: `qt/src/format_converter.cpp`
- Modify: `tests/qt/audio_tools_end_to_end_test.cpp`

**Interfaces:**
- Consumes `setConflictPolicy(QString)` and `setKeepCover(bool)`.
- Produces one of `auto-number`, `skip`, `overwrite`, `ask` plans without overwriting before final atomic commit.

- [ ] **Step 1: Write failing tests**

Add tests proving `skip` leaves a pre-existing target untouched and reports a skipped task, while `overwrite` produces a validated replacement only after successful conversion.

- [ ] **Step 2: Run test to verify it fails**

Run the focused test command from Task 1. Expected: FAIL because no conflict policy exists.

- [ ] **Step 3: Implement minimal policy plumbing**

Expose the policy, preserve `auto-number` as default, mark `ask` tasks as pending confirmation, and map `skip` and `overwrite` to the existing output reservation and atomic commit path. Keep cover as an explicit capability/preflight outcome; do not claim preservation when target support is absent.

- [ ] **Step 4: Re-run focused test**

Run the Step 2 command. Expected: PASS.

### Task 3: Reference-image page interactions

**Files:**
- Modify: `app/qml/AgPlayer/components/tools/FormatConvertPage.qml`
- Modify: `tests/qml/tst_error_states.qml`
- Modify: `tests/scripts/audio_tools_layout_contract_test.ps1`

**Interfaces:**
- Consumes `FormatConverter` task maps and preflight/conflict properties.
- Produces object names for toolbar, filters, task table, settings panel, output options, and bottom control bar.

- [ ] **Step 1: Write failing UI/layout tests**

Add contract assertions for the reference four zones and controls: toolbar search/filter, task status chips, right A/B/C settings groups, and bottom progress/control bar. Add QML test assertions that search and selected checkboxes change only visible/queued entries.

- [ ] **Step 2: Run tests to verify failure**

Run: `ctest --test-dir build/debug -R '^(audio_tools_layout_contract_test|qml_error_states_test)$' --output-on-failure`

Expected: FAIL because the reference object names and interactions are absent.

- [ ] **Step 3: Implement page incrementally**

Preserve the existing visual tokens and top navigation. Render the screenshot layout with responsive split panels, actual format capability buttons, dynamic encoding settings, C output options, real task state chips/search, selected checkbox queueing, and the total-progress bottom bar. Bind start through preflight and require confirmation only for actual plan differences/conflicts.

- [ ] **Step 4: Re-run UI/layout tests**

Run the Step 2 command. Expected: PASS.

### Task 4: Regression, build, and real conversion verification

**Files:**
- Modify: `tests/qt/audio_tools_end_to_end_test.cpp`
- Modify: `docs/superpowers/plans/2026-08-12-format-conversion-remediation.md`

- [ ] **Step 1: Extend focused real-format matrix only for new contracts**

Keep every exposed format’s existing real conversion/reopen assertion and add one selected-task/plan confirmation path that verifies output is absent before confirmation and valid after confirmation.

- [ ] **Step 2: Build and run focused tests**

Run: `cmake --build build/debug --target audio_tools_end_to_end_test qml_error_states_test AgPlayer && ctest --test-dir build/debug -R '^(audio_tools_end_to_end_test|qml_error_states_test|audio_tools_layout_contract_test)$' --output-on-failure`

- [ ] **Step 3: Run full relevant CTest verification**

Run: `ctest --test-dir build/debug --output-on-failure`

- [ ] **Step 4: Record evidence in final report**

Report changed files, actual available formats/encoders observed in the test run, exact test totals, build result, real format conversion result, and remaining explicit limitations. Do not claim production readiness without the evidence.
