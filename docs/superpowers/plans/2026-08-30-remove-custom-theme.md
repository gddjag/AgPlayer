# Remove Custom Theme Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove every runtime and settings path for custom skins, retain only System/Light/Dark with Dark as the default, and restore the pre-feature native Qt color dialog for media color fields.

**Architecture:** Replace the C++ generated-palette subsystem with the pre-feature fixed-token `Theme.qml` model. Preserve `themeMode` numeric compatibility (`0=Dark`, `1=Light`, `2=System`) while deleting all skin state, gradient backdrops, custom picker code, and QA plumbing. Restore `ColorField.qml` to `QtQuick.Dialogs.ColorDialog` so waveform and spectrum color editing remains available.

**Tech Stack:** C++17, Qt 6.7, QML/Qt Quick Controls, CMake/CTest, PowerShell QA.

**Spec:** `docs/superpowers/specs/2026-08-30-remove-custom-theme-design.md`

## Global Constraints

- Do not integrate another branch, rewrite shared history, package an EXE, or push.
- Preserve non-theme audio tools, list, metadata, translation, waveform, spectrum, editor, Equalizer, CUE, rating, and tag behavior.
- Keep persisted `themeMode` values `Dark=0`, `Light=1`, `System=2`; missing/invalid/reset defaults to Dark.
- Do not erase legacy skin keys from user storage; stop reading and writing them.
- Make source deletion real: no hidden custom-skin UI, dormant palette engine, dead gradient resource, or callable QA flag.

---

### Task 1: Lock the three-mode settings contract

**Files:**
- Modify: `tests/qt/settings_controller_test.cpp`
- Modify: `qt/src/settings_controller.hpp`
- Modify: `qt/src/settings_controller.cpp`

**Interfaces:**
- Consumes: `SettingsController::themeMode()`, `setThemeMode(int)`, settings edit transaction methods.
- Produces: one appearance property, `themeMode`, with missing/invalid/reset value `0`.

- [ ] **Step 1: Rewrite the focused tests to require Dark defaults and absence of skin behavior**

Keep assertions equivalent to:

```cpp
SettingsController settings;
QCOMPARE(settings.themeMode(), 0);

QSettings persisted;
persisted.setValue(QStringLiteral("appearance/themeMode"), 99);
SettingsController invalid;
QCOMPARE(invalid.themeMode(), 0);

for (const int mode : {0, 1, 2}) {
    persisted.setValue(QStringLiteral("appearance/themeMode"), mode);
    SettingsController compatible;
    QCOMPARE(compatible.themeMode(), mode);
}
```

Remove skin-specific test cases and add a static/API assertion that the header has no `skinColorMode`, `skinPreset`, or `skinCustom` properties.

- [ ] **Step 2: Run the focused test and verify it fails before implementation**

Run:

```powershell
cmake --build --preset windows-msvc-release --target settings_controller_test --parallel 4
ctest --test-dir build/release -R '^settings_controller_test$' --output-on-failure
```

Expected: FAIL because the current default is System and skin APIs still exist.

- [ ] **Step 3: Remove skin state from SettingsController**

Delete skin `Q_PROPERTY` declarations, getters, setters, signals, fields, normalization helpers, persistence reads/writes, edit snapshots, and reset/commit/cancel branches. Set both the member initializer and reset/load fallback for `themeMode_` to `0`; clamp invalid disk values to `0` rather than converting them to System.

- [ ] **Step 4: Rebuild and verify the focused test passes**

Run the Task 1 commands again. Expected: PASS.

- [ ] **Step 5: Commit the settings removal**

```powershell
git add -- qt/src/settings_controller.hpp qt/src/settings_controller.cpp tests/qt/settings_controller_test.cpp
git commit -m "refactor(settings): remove custom skin state"
```

### Task 2: Restore fixed Theme.qml and remove the C++ palette engine

**Files:**
- Delete: `qt/src/theme_manager.hpp`
- Delete: `qt/src/theme_manager.cpp`
- Delete: `tests/qt/theme_manager_test.cpp`
- Modify: `qt/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`
- Modify: `qt/src/qml_registration.hpp`
- Modify: `qt/src/qml_registration.cpp`
- Modify: `app/main.cpp`
- Modify: `tests/qt/qml_main_window_test_main.cpp`
- Modify: `tests/qt/qml_audio_editor_test_main.cpp`
- Modify: `tests/qt/qml_audio_tools_test_main.cpp`
- Modify: `app/qml/AgPlayer/theme/Theme.qml`
- Modify: `app/qml/AgPlayer/Main.qml`

**Interfaces:**
- Consumes: `SettingsController.themeMode`, `Application.styleHints.colorScheme`, `SystemPalette`.
- Produces: fixed Light/Dark compatibility tokens from `Theme.qml`; no `ThemeManager` QML singleton.

- [ ] **Step 1: Change QML tests/static contracts to require fixed three-mode tokens and no ThemeManager**

Assertions must cover:

```qml
compare(Theme.effectiveMode, 0) // Dark
SettingsController.themeMode = 1
compare(Theme.effectiveMode, 1) // Light
SettingsController.themeMode = 2
compare(Theme.followsSystem, true)
```

Add a source contract that fails if `ThemeManager` remains registered or referenced by `Theme.qml`.

- [ ] **Step 2: Run the focused QML/static tests and verify they fail**

```powershell
ctest --test-dir build/release -R '^(qml_main_window_test|qml_theme_color_contract_test)$' --output-on-failure
```

Expected: FAIL while Theme.qml delegates to ThemeManager.

- [ ] **Step 3: Restore the fixed Theme.qml implementation**

Use `themeMode` semantics directly:

```qml
property int mode: SettingsController.themeMode
readonly property bool followsSystem: mode === 2
readonly property bool systemIsLight: Application.styleHints.colorScheme === Qt.Light
readonly property int effectiveMode: followsSystem ? (systemIsLight ? 1 : 0)
                                                   : (mode === 1 ? 1 : 0)
readonly property bool isLight: effectiveMode === 1
```

Retain compatibility token names needed by current QML but define them only from fixed Light/Dark values and `SystemPalette`; remove gradient/glass/generated-skin tokens.

- [ ] **Step 4: Delete ThemeManager and all registration/synchronizer plumbing**

Remove CMake sources/target, QML singleton pointers, main/test-harness construction, and `ThemeSettingsSynchronizer`. Main.qml must bind or synchronize Theme mode without delayed generated-palette setup.

- [ ] **Step 5: Rebuild and run focused tests**

```powershell
cmake --build --preset windows-msvc-release --parallel 4
ctest --test-dir build/release -R '^(settings_controller_test|qml_main_window_test|qml_audio_editor_test|qml_audio_tools_test|qml_mini_player_test|qml_theme_color_contract_test)$' --output-on-failure
```

Expected: PASS.

- [ ] **Step 6: Commit the runtime removal**

```powershell
git add -A -- qt app/main.cpp app/qml/AgPlayer/theme/Theme.qml app/qml/AgPlayer/Main.qml tests/qt tests/CMakeLists.txt
git commit -m "refactor(theme): restore fixed system light dark palettes"
```

### Task 3: Remove skin UI/backdrops and restore the native media color dialog

**Files:**
- Delete: `app/qml/AgPlayer/components/ThemeColorSelector.qml`
- Delete: `app/qml/AgPlayer/components/AgColorPicker.qml`
- Delete: `app/qml/AgPlayer/components/ColorScale.js`
- Delete: `app/qml/AgPlayer/components/SkinBackdrop.qml`
- Modify: `app/qml/AgPlayer/components/ColorField.qml`
- Modify: `app/qml/AgPlayer/SettingsPage.qml`
- Modify: `app/qml/AgPlayer/Main.qml`
- Modify: `app/qml/AgPlayer/SettingsWindow.qml`
- Modify: `app/qml/AgPlayer/MiniPlayerWindow.qml`
- Modify: `app/qml/AgPlayer/AudioToolsWindow.qml`
- Modify: `app/qml/AgPlayer/EqualizerWindow.qml`
- Modify: `app/qml/AgPlayer/components/DockedWindowFrame.qml`
- Modify: `app/CMakeLists.txt`
- Modify: `tests/qml/tst_color_picker.qml`
- Modify: `tests/qml/tst_main_window.qml`
- Modify: `tests/qml/tst_mini_player.qml`
- Modify: `tests/qml/tst_audio_editor.qml`

**Interfaces:**
- Consumes: `ColorField.colorValue`, `ColorField.targetProperty`, `SettingsController.themeMode`.
- Produces: settings UI with exactly three appearance buttons; media `ColorField` backed by `QtQuick.Dialogs.ColorDialog`.

- [ ] **Step 1: Replace picker/skin tests with removal and native-dialog contracts**

Static/QML expectations:

```qml
verify(findChild(settingsPage, "skinThemeColorSelector") === null)
verify(findChild(mainWindow, "skinBackdrop") === null)
```

Source contract expectations:

```cmake
if(NOT color_field_source MATCHES "QtQuick.Dialogs" OR
   NOT color_field_source MATCHES "ColorDialog")
    message(FATAL_ERROR "ColorField must use native Qt ColorDialog")
endif()
```

- [ ] **Step 2: Run the tests and verify they fail**

```powershell
ctest --test-dir build/release -R '^(qml_color_picker_test|qml_main_window_test|qml_mini_player_test|qml_audio_editor_test)$' --output-on-failure
```

Expected: FAIL because skin selectors/backdrops and AgColorPicker still exist.

- [ ] **Step 3: Remove the skin selector and every SkinBackdrop insertion**

SettingsPage must contain only a `Repeater` for System/Light/Dark. Window roots return to `Theme.background` or their prior fixed panel token. Remove deleted files from the QML module.

- [ ] **Step 4: Restore ColorField.qml to the native dialog**

The control must import `QtQuick.Dialogs` and use:

```qml
ColorDialog {
    id: picker
    title: qsTr("选择颜色")
    selectedColor: root.colorValue
    onAccepted: root.colorEdited(selectedColor.toString().toUpperCase())
}
```

Retain existing waveform/spectrum `targetProperty` wiring and text validation.

- [ ] **Step 5: Rebuild and run the focused QML suite**

Run the Task 3 focused command again. Expected: PASS.

- [ ] **Step 6: Commit the UI removal**

```powershell
git add -A -- app tests/qml tests/CMakeLists.txt
git commit -m "refactor(ui): remove custom skin controls"
```

### Task 4: Remove skin QA, translations, and obsolete records

**Files:**
- Modify: `scripts/qa-final-ui-matrix.ps1`
- Modify: `cmake/CheckQmlThemeColors.cmake`
- Modify: `translations/agplayer_zh.ts`
- Modify: `translations/agplayer_en.ts`
- Modify: `translations/agplayer_th.ts`
- Modify: `translations/agplayer_vi.ts`
- Delete: custom-theme and soft-gradient implementation/acceptance documents under `docs/development/` and `docs/qa/`
- Create: `docs/qa/2026-08-30-custom-theme-removal-acceptance.md`

**Interfaces:**
- Consumes: three `--qa-theme` appearance values and existing visual surfaces.
- Produces: QA paths with no skin/preset/gradient/picker parameters or translation contexts.

- [ ] **Step 1: Add a static removal gate**

The checker must reject runtime occurrences of:

```text
ThemeManager
SkinBackdrop
ThemeColorSelector
AgColorPicker
skinColorMode
skinPreset
skinCustom
--qa-skin
--qa-open-skin-picker
```

Exclude Git history and the removal design/plan/acceptance records from this gate.

- [ ] **Step 2: Run the gate and verify it fails**

```powershell
cmake -DROOT='D:/ai/AgPlayer/.worktrees/full-custom-theme-engine' -P cmake/CheckQmlThemeColors.cmake
```

Expected: FAIL until all runtime and QA references are removed.

- [ ] **Step 3: Remove obsolete QA parameters, translation contexts, and historical feature docs**

Keep `--qa-theme light|dark|system`, media color fixtures, and non-theme QA surfaces. Record deleted behavior, retained values, test evidence, and platform limits in the new acceptance document.

- [ ] **Step 4: Run lint, static checks, and translation tests**

```powershell
cmake --build build/release --target all_qmllint --parallel 4
cmake -DROOT='D:/ai/AgPlayer/.worktrees/full-custom-theme-engine' -P cmake/CheckQmlThemeColors.cmake
ctest --test-dir build/release -R '^(translation_manager_test|translation_catalog_test|phase6_translation_coverage_test)$' --output-on-failure
```

Expected: PASS with no custom-skin contexts or runtime references.

- [ ] **Step 5: Commit QA and documentation cleanup**

```powershell
git add -A -- scripts cmake translations docs tests/CMakeLists.txt
git commit -m "test(theme): verify custom skin removal"
```

### Task 5: Final verification and handoff

**Files:**
- Modify: `docs/qa/2026-08-30-custom-theme-removal-acceptance.md`

**Interfaces:**
- Consumes: completed Tasks 1-4.
- Produces: evidence-backed final commit with no branch integration or installer.

- [ ] **Step 1: Fresh Debug and Release builds**

```powershell
cmake --fresh --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug --parallel 4
cmake --fresh --preset windows-msvc-release
cmake --build --preset windows-msvc-release --parallel 4
```

- [ ] **Step 2: Run focused and full tests**

```powershell
ctest --test-dir build/release --output-on-failure
ctest --test-dir build/debug --output-on-failure
```

Record all pass/fail counts honestly; do not convert known Debug failures into passes.

- [ ] **Step 3: Run real WAV smoke and three-mode visual matrix**

```powershell
scripts/qa-main-smoke.ps1 -BuildDirectory build/release
scripts/qa-final-ui-matrix.ps1 -BuildDirectory build/release `
  -OutputDirectory build/qa/2026-08-30-custom-theme-removal `
  -Languages zh,en,th,vi -Themes system,light,dark -Surfaces playback,settings
```

Verify that Dark is selected after reset, each mode changes the whole fixed palette, no skin controls are visible, and waveform/spectrum color fields still open the native dialog.

- [ ] **Step 4: Run final source and Git gates**

```powershell
rg -n "ThemeManager|SkinBackdrop|ThemeColorSelector|AgColorPicker|skinColorMode|skinPreset|skinCustom|--qa-skin|--qa-open-skin-picker" app qt tests scripts cmake translations
git diff --check
git status --short --branch
git diff HEAD~4..HEAD --stat
```

Expected: no runtime skin match, no whitespace error, only intended removal changes.

- [ ] **Step 5: Review and commit final evidence**

```powershell
git add -- docs/qa/2026-08-30-custom-theme-removal-acceptance.md
git commit -m "docs(qa): record custom theme removal"
```

Report the final HEAD, actual test results, known baseline failures, and untested macOS/Linux behavior. Do not package or push.
