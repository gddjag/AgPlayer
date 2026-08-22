# Format Conversion Reference V2 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Match the supplied 1672×941 format-conversion UI while closing the real preflight-to-conversion execution gap.

**Architecture:** Reuse the current `FormatConverter`, conversion planner, task/filter models, settings singleton, and FFmpeg-library pipeline. Change only the missing facade wiring, the existing QML components, focused tests, and visual evidence; no new runtime framework or dependency.

**Tech Stack:** Qt 6.7, QML/Quick Controls, C++17, Qt Concurrent/Test/QuickTest, linked FFmpeg libraries, CMake/Ninja/MSVC x64, PowerShell QA scripts.

**Spec:** `docs/superpowers/specs/2026-08-20-format-conversion-reference-v2-design.md`

## Global Constraints

- Visual target is exactly `1672×941`; latest image overrides older 942 px screenshots and conflicting visible navigation/footer details.
- The requirements document controls conversion behavior, safety, performance, and acceptance.
- Reuse the linked FFmpeg APIs; never invoke `ffmpeg.exe`.
- Add no dependency, production demo task, fake progress, fake success, silent fallback, or unrelated refactor.
- Preserve current uncommitted filename-processing work and commit only files owned by this plan.
- Use generated test media; do not package or install an executable.

---

### Task 1: Freeze and Execute the Real Preflight Plan

**Files:**
- Modify: `qt/src/format_converter.hpp`
- Modify: `qt/src/format_converter.cpp`
- Modify: `tests/qt/audio_tools_end_to_end_test.cpp`

**Interfaces:**
- Consumes: `build_format_conversion_plan(const QList<FormatPlanInput>&, const FormatConversionRequest&)`.
- Produces: `buildPreflight(request)` with resolved task plans and `confirmPendingPlan()` that consumes the frozen selection/request.

- [ ] **Step 1: Add failing preflight tests**

Add focused Qt cases that import generated WAV files, select a subset, call `buildPreflight`, then assert the returned plan contains the selected task IDs, normalized output format, resolved output paths, and `ready=true`. Add cases for video extraction disabled, `Ask` conflict confirmation, and a task removed between preview and confirmation.

```cpp
const QVariantMap plan = converter.buildPreflight({
    {QStringLiteral("outputFormat"), QStringLiteral("flac")},
    {QStringLiteral("outputDir"), outputDir},
    {QStringLiteral("conflictPolicy"), QStringLiteral("auto-number")},
    {QStringLiteral("keepMetadata"), true},
    {QStringLiteral("keepCover"), false},
    {QStringLiteral("preserveDirectories"), true},
    {QStringLiteral("extractAudio"), false}
});
QVERIFY(plan.value(QStringLiteral("ready")).toBool());
QCOMPARE(plan.value(QStringLiteral("taskCount")).toInt(), 1);
QVERIFY(!plan.value(QStringLiteral("tasks")).toList().isEmpty());
```

- [ ] **Step 2: Run RED**

```powershell
cmake --build build/release-verify --target audio_tools_end_to_end_test
ctest --test-dir build/release-verify -R '^audio_tools_end_to_end_test$' --output-on-failure
```

Expected: new preflight assertions fail because `buildPreflight()` currently returns only shallow availability/count data.

- [ ] **Step 3: Implement the minimum planner bridge**

Translate checked `FileEntry` records and the request map into existing plan structs, call `build_format_conversion_plan`, serialize task ID/output path/resolved profile/differences into `pendingPlan_`, and retain a private frozen request plus task IDs. Keep existing compatibility properties and methods.

- [ ] **Step 4: Make confirmation consume the frozen plan**

Reject stale plans, then call the existing queue path with the frozen parameters and selected task IDs. Preserve existing staging validation, atomic commit, progress, cancellation, and diagnostics.

- [ ] **Step 5: Run GREEN and regression**

```powershell
cmake --build build/release-verify --target format_conversion_plan_test audio_tools_end_to_end_test
ctest --test-dir build/release-verify -R '^(format_conversion_plan_test|audio_tools_end_to_end_test)$' --output-on-failure
```

Expected: all selected tests pass.

---

### Task 2: Match the 1672×941 Workbench and Preserve Real Settings

**Files:**
- Modify: `app/qml/AgPlayer/AudioToolsWindow.qml`
- Modify: `app/qml/AgPlayer/components/tools/ToolSidebar.qml`
- Modify: `app/qml/AgPlayer/components/tools/FormatConvertPage.qml`
- Modify: `app/qml/AgPlayer/components/tools/FormatTaskTable.qml`
- Modify: `app/qml/AgPlayer/components/tools/FormatSettingsPanel.qml`
- Modify: `tests/qml/tst_format_converter.qml`
- Modify: `tests/scripts/format_converter_reference_contract_test.ps1`

**Interfaces:**
- Consumes: `FormatConverter` facade and `SettingsController.defaultOutputDirectory`/`parallelJobs`.
- Produces: exact reference geometry, persisted output directory, compact capability-driven settings, row context operations, and a footer without the old visible extra controls.

- [ ] **Step 1: Add failing geometry and behavior tests**

Update QuickTest to `1672×941`; assert title 48, navigation 54, toolbar 60, bottom bar 114, settings width 445±1, main gap 8, visible info card, and object names for the reference footer controls. Assert the default output directory equals `SettingsController.defaultOutputDirectory` and edits write it back.

```qml
compare(testCase.height, 941)
compare(Math.round(findChild(page, "formatToolbar").height), 60)
compare(Math.round(findChild(page, "formatBottomBar").height), 114)
verify(findChild(page, "formatLocalProcessingHint").visible)
verify(!findChild(page, "formatFooterParallelJobs"))
verify(!findChild(page, "formatFooterOutputDirectory"))
```

- [ ] **Step 2: Run RED**

```powershell
cmake --build build/release-verify --target qml_audio_tools_test
ctest --test-dir build/release-verify -R '^(qml_format_converter_test|format_converter_reference_contract_test)$' --output-on-failure
```

Expected: height, footer, hint visibility, and settings persistence assertions fail.

- [ ] **Step 3: Apply the reference layout**

Set the window to 941 px, align the title/navigation metrics, remove the visible footer parallel/output controls, reposition summary/start/cancel with flexible space, compact B/C controls so the hint card is visible, add the existing funnel and audio-file icons, and retain the 8 px split with a 445 px settings panel.

- [ ] **Step 4: Bind settings and interactions**

Initialize and persist the output directory through `SettingsController`; place concurrency in the settings panel below the reference fold. Make the settings chevron collapse/expand. Add a row context menu using existing remove/retry/cancel/copy APIs without adding a visible column.

- [ ] **Step 5: Run GREEN and responsive checks**

```powershell
cmake --build build/release-verify --target qml_audio_tools_test AgPlayer
ctest --test-dir build/release-verify -R '^(qml_format_converter_test|format_converter_reference_contract_test|audio_tools_layout_contract_test|translation_catalog_test|source_encoding_test)$' --output-on-failure
```

Expected: selected tests pass with no QML load error.

---

### Task 3: Real Conversion Evidence and Blocking Design QA

**Files:**
- Modify: `tests/qt/qml_audio_tools_test_main.cpp`
- Create: `tests/qml/FormatConverterVisualFixture.qml`
- Modify: `tests/CMakeLists.txt`
- Replace format-conversion section: `design-qa.md`
- Create: `docs/qa/2026-08-20-format-conversion-reference-v2-evidence.md`

**Interfaces:**
- Consumes: finished facade and QML workbench.
- Produces: test-only same-state screenshot, side-by-side comparison, real-file output evidence, and honest pass/blocked QA.

- [ ] **Step 1: Add a test-only visual fixture**

Create a QML-only fixture with 12 visible rows matching the reference names/formats/status/progress, output capabilities, counts, and default settings. Inject it through `FormatConvertPage.converter`; never register it in `app/CMakeLists.txt` or production startup.

- [ ] **Step 2: Build and run all focused tests**

```powershell
cmake --build build/release-verify --target AgPlayer format_matrix_test format_conversion_plan_test format_conversion_task_model_test audio_tools_end_to_end_test qml_audio_tools_test
ctest --test-dir build/release-verify -R '^(format_matrix_test|format_conversion_plan_test|format_conversion_task_model_test|audio_tools_end_to_end_test|qml_format_converter_test|format_converter_reference_contract_test|audio_tools_layout_contract_test|translation_catalog_test|source_encoding_test)$' --output-on-failure
```

Expected: all selected tests pass.

- [ ] **Step 3: Verify a real UI-to-output conversion**

Generate a short WAV, add it through the facade, choose FLAC and the saved output directory, build and confirm the plan, wait for `Done`, and reopen/decode the final file. Repeat one currently available lossy format and record requested/resolved parameters and output paths.

- [ ] **Step 4: Capture and compare at the same state**

Capture the test fixture at exactly `1672×941`, compose it beside the source image without scaling, inspect full view plus task-table/settings/footer crops, and iterate until no P0/P1/P2 mismatch remains.

- [ ] **Step 5: Record verification**

Write the exact commit/worktree, build directory, linked FFmpeg capability results, commands, pass/fail counts, real input/output data, screenshot paths, interaction checks, and remaining P3 differences. Set `design-qa.md` to `final result: passed` only when the comparison actually passes; otherwise set `blocked` with the remaining findings.

- [ ] **Step 6: Final diff and full regression**

```powershell
git diff --check
ctest --test-dir build/release-verify --output-on-failure
git status --short
```

Expected: no whitespace error; full-suite result is recorded exactly, including any unrelated pre-existing failure.
