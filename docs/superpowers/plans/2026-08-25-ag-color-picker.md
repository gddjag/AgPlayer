# AgPlayer Shared Color Picker Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace AgPlayer's one system `ColorDialog` with a single reusable native-QML `AgColorPicker.qml` used by all ten existing color settings without changing their properties, defaults, keys, reset behavior, or rendering bindings.

**Architecture:** Keep `ColorField.qml` as the stable entry/binding adapter and replace only its internal picker. Put deterministic color conversion and palette generation in a stateless `ColorScale.js`; put popup state and visual behavior in `AgColorPicker.qml`; preserve all existing `SettingsController[targetProperty]` writes.

**Tech Stack:** Qt 6.7, Qt Quick, Qt Quick Controls, QML JavaScript library, Qt Quick Test, CMake/CTest, MSVC/Ninja.

**Spec:** `docs/development/2026-08-25-ag-color-picker-design.md`

## Global Constraints

- Change only the color selection UI; do not change any color purpose, QML property, signal, QSettings key, default color, load/save flow, live preview, settings transaction, reset behavior, or theme-management mechanism.
- Preserve the ten existing `ColorField` bindings in `SettingsPage.qml` verbatim, including the currently crossed `waveformRgb*` and `spectrumRgb*` relationships.
- Preserve `QString`/`#RRGGBB` storage and the existing C++ `QColor::HexRgb` normalization path.
- Use one source implementation, `AgColorPicker.qml`; do not add another picker implementation or platform-specific picker.
- Use Qt 6 + Qt Quick/QML only. No WebView, WebEngine, HTML/CSS embedding, browser UI, third-party UI framework, Timer, background thread, ShaderEffect, disk cache, or new dependency.
- Bind popup chrome directly to the existing `Theme` singleton. Theme changes must not alter the ten candidate color values.
- Popup cancellation by `×`, Escape, or outside click must not update a setting; only clicking a candidate emits `colorAccepted` and closes immediately.
- Do not modify waveform SceneGraph, playback, decoding, FFmpeg, or audio-thread code.
- Preserve all unrelated working-tree changes; stage and commit only files owned by the current task.

## File Structure

- Create `app/qml/AgPlayer/components/ColorScale.js`: pure normalization, conversion, HSL palette, and luminance functions.
- Create `app/qml/AgPlayer/components/AgColorPicker.qml`: the only picker UI and popup state machine.
- Modify `app/qml/AgPlayer/components/ColorField.qml`: stable trigger/binding adapter that invokes `AgColorPicker`.
- Modify `app/CMakeLists.txt`: register the QML and JS files in the existing `AgPlayer` module.
- Create `tests/qml/tst_color_picker.qml`: behavior tests for the real JS, picker, Theme bindings, and one real setting integration.
- Modify `tests/CMakeLists.txt`: register `qml_color_picker_test` using the existing `qml_main_window_test` executable.
- Create `docs/development/2026-08-25-ag-color-picker.md`: implementation and acceptance evidence.

---

### Task 1: Pure color scale and isolated QML test target

**Files:**
- Create: `tests/qml/tst_color_picker.qml`
- Modify: `tests/CMakeLists.txt:652-675`
- Create: `app/qml/AgPlayer/components/ColorScale.js`
- Modify: `app/CMakeLists.txt:95-123`

**Interfaces:**
- Consumes: literal HEX/RGB values only; no Theme or SettingsController state.
- Produces: `normalizeHex(value) -> string`, `hexToRgb(value) -> object|null`, `rgbToHex(r,g,b) -> string`, `buildPalette(value) -> array`, and `isLight(value) -> bool`.

- [ ] **Step 1: Register a focused Quick Test and write failing literal expectations**

Add a second CTest entry using the existing `qml_main_window_test` executable and the same offscreen environment:

```cmake
add_test(NAME qml_color_picker_test
    COMMAND qml_main_window_test
        -input ${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_color_picker.qml)
set_tests_properties(qml_color_picker_test PROPERTIES
    TIMEOUT 20
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_CONTROLS_STYLE=Basic;AGPLAYER_TEST_AUDIO=${SINE_WAV_FIXTURE}"
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin"
)
```

Start `tst_color_picker.qml` by importing `ColorScale.js` directly and asserting hand-derived literals:

```qml
import QtQuick
import QtTest
import "../../app/qml/AgPlayer/components/ColorScale.js" as ColorScale

TestCase {
    name: "AgColorPicker"

    function test_normalization_and_rgb_round_trip() {
        compare(ColorScale.normalizeHex("63316b"), "#63316B")
        compare(ColorScale.normalizeHex("#abc"), "#AABBCC")
        compare(ColorScale.normalizeHex("not-a-color"), "")
        compare(ColorScale.rgbToHex(99, 49, 107), "#63316B")
        const rgb = ColorScale.hexToRgb("#63316B")
        compare(rgb.r, 99)
        compare(rgb.g, 49)
        compare(rgb.b, 107)
    }

    function test_reference_palette_is_exact() {
        const expected = [
            "#F8EBFA", "#E9D2EC", "#D6B9DB", "#C09CC6", "#A76BB0",
            "#63316B", "#512C57", "#432248", "#341938", "#251028"
        ]
        const actual = ColorScale.buildPalette("#63316B")
        compare(actual.length, expected.length)
        compare(actual.join(","), expected.join(","))
    }
}
```

- [ ] **Step 2: Configure/build and verify RED**

Run:

```powershell
cmake --preset windows-msvc-release
cmake --build --preset windows-msvc-release --target qml_main_window_test
ctest --test-dir build/release -C Release -R '^qml_color_picker_test$' --output-on-failure
```

Expected: FAIL because `ColorScale.js` does not yet exist or its functions are unavailable. Fix only test/configuration errors until the failure is specifically due to the missing production implementation.

- [ ] **Step 3: Implement the minimal pure functions**

Create `.pragma library` JavaScript with:

```javascript
function clamp(value, minimum, maximum) {
    var number = Number(value)
    if (!isFinite(number))
        number = minimum
    return Math.min(maximum, Math.max(minimum, number))
}

function normalizeHex(value) {
    var text = String(value || "").trim().replace(/^#/, "")
    if (/^[0-9a-fA-F]{3}$/.test(text))
        text = text.replace(/(.)/g, "$1$1")
    return /^[0-9a-fA-F]{6}$/.test(text) ? "#" + text.toUpperCase() : ""
}
```

Implement standard RGB/HSL conversion and this exact palette curve, clamping final lightness to `[0.035, 0.98]`:

```javascript
[
  ["10",  l + (1-l)*0.93, Math.min(1, s*1.55)],
  ["20",  l + (1-l)*0.82, Math.min(1, s*1.10)],
  ["30",  l + (1-l)*0.70, Math.min(1, s*0.86)],
  ["40",  l + (1-l)*0.56, Math.min(1, s*0.72)],
  ["50",  l + (1-l)*0.36, Math.min(1, s*0.82)],
  ["100", l,               s],
  ["120", l*0.84,          Math.min(1, s*0.88)],
  ["140", l*0.68,          Math.min(1, s*0.96)],
  ["160", l*0.52,          Math.min(1, s*1.05)],
  ["180", l*0.36,          Math.min(1, s*1.12)]
]
```

Use the original base RGB object for index `100`; all other steps use `hslToRgb`. `isLight` uses WCAG relative luminance and the HTML threshold `> 0.46`.

Add `qml/AgPlayer/components/ColorScale.js` under the existing `QML_FILES` list.

- [ ] **Step 4: Verify GREEN**

Run the three commands from Step 2 again. Expected: `qml_color_picker_test` PASS with both tests and no QML warnings.

- [ ] **Step 5: Self-review and commit**

Run:

```powershell
git diff --check
git diff -- app/qml/AgPlayer/components/ColorScale.js app/CMakeLists.txt tests/qml/tst_color_picker.qml tests/CMakeLists.txt
```

Confirm no Theme/settings access exists in `ColorScale.js`, then commit only the four task files:

```powershell
git add -- app/qml/AgPlayer/components/ColorScale.js app/CMakeLists.txt tests/qml/tst_color_picker.qml tests/CMakeLists.txt
git commit -m "feat: add deterministic color scale"
```

---

### Task 2: Native AgColorPicker popup behavior and visual structure

**Files:**
- Modify: `tests/qml/tst_color_picker.qml`
- Create: `app/qml/AgPlayer/components/AgColorPicker.qml`
- Modify: `app/CMakeLists.txt:95-123`

**Interfaces:**
- Consumes: `ColorScale.normalizeHex`, `hexToRgb`, `rgbToHex`, `buildPalette`, `isLight`; live `Theme` properties.
- Produces: `property color baseColor`, `property color selectedColor`, read-only candidate color model, `signal colorAccepted(color color)`, and `openForColor(initialColor)`.

- [ ] **Step 1: Extend the QML test with failing popup contracts**

Instantiate the production type in the test:

```qml
AgColorPicker {
    id: picker
}
SignalSpy {
    id: acceptedSpy
    target: picker
    signalName: "colorAccepted"
}
```

Add independently observable tests:

- `openForColor("#63316B")` initializes both states and the exact ten candidates.
- Editing the real HEX field to `#123456` changes `baseColor` and candidates but not `selectedColor` or `acceptedSpy.count`.
- Editing the real R/G/B fields and moving each real Slider updates the other controls and normalized HEX.
- Invalid HEX followed by editing-finished restores the last valid HEX.
- Clicking a real candidate emits exactly one matching color and closes immediately.
- Clicking `×`, pressing Escape, and closing by outside press emit nothing.
- Switching `Theme.mode` while open changes popup chrome properties but leaves a copied candidate array byte-for-byte unchanged.

Use `objectName` only on real interactive elements needed by the tests (`agColorPicker`, `colorPickerHex`, `colorPickerR/G/B`, `colorPickerR/G/BSlider`, `colorCandidate-0..9`, `colorPickerClose`). Assertions must inspect state/signal/UI behavior, not source text.

- [ ] **Step 2: Verify RED**

Run:

```powershell
cmake --build --preset windows-msvc-release --target qml_main_window_test
ctest --test-dir build/release -C Release -R '^qml_color_picker_test$' --output-on-failure
```

Expected: FAIL because `AgColorPicker` is not a registered type. Fix only test syntax/setup until that is the failure.

- [ ] **Step 3: Implement popup state and synchronization**

Create `AgColorPicker.qml` as a `Popup` with:

```qml
property color baseColor: "#63316B"
property color selectedColor: "#63316B"
readonly property var candidateColors: ColorScale.buildPalette(normalizedBaseHex)
signal colorAccepted(color color)

function openForColor(initialColor) {
    const normalized = ColorScale.normalizeHex(initialColor)
    const value = normalized.length > 0 ? normalized : "#000000"
    selectedColor = value
    setBaseHex(value)
    open()
}
```

Maintain a single normalized base HEX as the source of truth. Guard programmatic TextInput/Slider synchronization so one user edit causes one base update and one palette rebuild. Do not add a Timer or animation-dependent acceptance.

Set:

```qml
modal: false
focus: true
closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
```

Candidate click order is fixed: assign `selectedColor`, emit `colorAccepted(selectedColor)`, then `close()` in the same handler.

- [ ] **Step 4: Implement the approved visual contract**

Use `ColumnLayout`, `RowLayout`, `GridLayout`, native `TextInput`/`Slider`, `Repeater`, `Rectangle`, and `AbstractButton`:

- width `Math.min(360, Overlay.overlay ? Overlay.overlay.width - 20 : 360)` with non-negative lower bound;
- 16 px radius, 1 px border, approximately 13 px padding;
- 25 px top swatch; 25 px close button;
- 34 px RGB container, three equal fields and separators;
- 7 px RGB gradient tracks and 26 × 17 px white double-circle handles;
- 5 columns × 2 rows, 5 px gaps, 44 px candidate height;
- displayed indices `10,20,30,40,50,100,120,140,160,180` and uppercase HEX;
- selected candidate double ring and `✓` badge;
- direct bindings to `Theme.isLight`, `Theme.elevated`, `Theme.panel`, `Theme.primaryText`, `Theme.secondaryText`, `Theme.border`, and `Theme.hoverSurface`.

Use lightweight nested rectangles for the shadow/handle; do not use `ShaderEffect`, `Canvas`, images, or an additional theme state property.

Add `qml/AgPlayer/components/AgColorPicker.qml` to `app/CMakeLists.txt`.

- [ ] **Step 5: Verify GREEN and lint**

Run:

```powershell
cmake --build --preset windows-msvc-release --target qml_main_window_test
ctest --test-dir build/release -C Release -R '^qml_color_picker_test$' --output-on-failure
cmake --build --preset windows-msvc-release --target agplayer_app_qml_qmllint
```

Expected: focused tests and QML lint PASS without binding or import warnings.

- [ ] **Step 6: Self-review and commit**

Confirm the candidate array is unchanged across `Theme.mode` changes and no Timer/Web/Shader/API duplication exists. Then:

```powershell
git diff --check
git add -- app/qml/AgPlayer/components/AgColorPicker.qml app/CMakeLists.txt tests/qml/tst_color_picker.qml
git commit -m "feat: add shared QML color picker"
```

---

### Task 3: Integrate every existing ColorField without changing bindings

**Files:**
- Modify: `tests/qml/tst_color_picker.qml`
- Modify: `app/qml/AgPlayer/components/ColorField.qml`

**Interfaces:**
- Consumes: `AgColorPicker.openForColor(colorValue)` and `colorAccepted(color)`.
- Produces unchanged `ColorField.colorValue`, `targetProperty`, `colorEdited(string)` and `SettingsController[targetProperty] = value` behavior.

- [ ] **Step 1: Add a failing integration test against one real setting**

Instantiate:

```qml
ColorField {
    id: integratedField
    colorValue: SettingsController.waveformSolidBaseColor
    targetProperty: "waveformSolidBaseColor"
}
```

Test that clicking the real `ColorField` trigger opens its `AgColorPicker` initialized from `SettingsController.waveformSolidBaseColor`; clicking a known candidate updates `SettingsController.waveformSolidBaseColor` once and closes. A second open followed by cancellation must leave the setting unchanged. Restore the original setting in `cleanup()`.

Also assert the entry displays normalized uppercase HEX and the picker is reachable by `objectName`, so the test fails against the old native `ColorDialog` implementation.

- [ ] **Step 2: Verify RED**

Run:

```powershell
cmake --build --preset windows-msvc-release --target qml_main_window_test
ctest --test-dir build/release -C Release -R '^qml_color_picker_test$' --output-on-failure
```

Expected: FAIL because the existing `ColorField` still opens `ColorDialog` and does not expose/use `AgColorPicker`.

- [ ] **Step 3: Replace only ColorField's picker UI**

- Remove `import QtQuick.Dialogs` and the `ColorDialog` block.
- Keep `colorValue`, `targetProperty`, `colorEdited`, `onColorEdited`, and `normalized()` semantics.
- Render a 106 × 32 trigger showing the existing color and uppercase HEX; clicking anywhere opens the embedded `AgColorPicker` with `colorValue`.
- Wire only:

```qml
AgColorPicker {
    id: picker
    objectName: "colorFieldPicker"
    onColorAccepted: color => root.colorEdited(
        root.normalized(color))
}
```

- Do not edit any of the ten `ColorField` call sites or any C++ settings/rendering file.

- [ ] **Step 4: Verify focused and binding regression tests**

Run:

```powershell
cmake --build --preset windows-msvc-release --target qml_main_window_test settings_controller_test AgPlayer
ctest --test-dir build/release -C Release -R '^(qml_color_picker_test|qml_main_window_test|settings_controller_test)$' --output-on-failure
```

Expected: all selected tests PASS. If an existing unrelated baseline test fails, preserve its complete output and establish whether the failure reproduces on the task base commit before attributing it.

- [ ] **Step 5: Confirm exhaustive replacement and unchanged bindings**

Run:

```powershell
rg -n -g '*.qml' -g '*.cpp' -g '*.hpp' 'ColorDialog|QColorDialog|ColorPicker' app qt
rg -n 'targetProperty: "(waveform|spectrum).*Color"' app/qml/AgPlayer/SettingsPage.qml
git diff -- app/qml/AgPlayer/SettingsPage.qml qt/src/settings_controller.cpp qt/src/settings_controller.hpp qt/src/waveform_item.cpp
```

Expected: the only picker definition/reference is `AgColorPicker`; all ten original `targetProperty` lines remain; the settings and rendering files have no diff.

- [ ] **Step 6: Self-review and commit**

```powershell
git diff --check
git add -- app/qml/AgPlayer/components/ColorField.qml tests/qml/tst_color_picker.qml
git commit -m "feat: unify player color picker entry"
```

---

### Task 4: Visual acceptance, full verification, and development evidence

**Files:**
- Create: `docs/development/2026-08-25-ag-color-picker.md`
- Create evidence under an ignored build/evidence directory only; do not commit generated screenshots unless existing project policy explicitly requires it.

**Interfaces:**
- Consumes: completed picker and integration from Tasks 1–3.
- Produces: reproducible verification commands, dark/light visual evidence paths, and an honest Windows/macOS scope statement.

- [ ] **Step 1: Run full relevant build and automated verification**

Run fresh:

```powershell
cmake --preset windows-msvc-release
cmake --build --preset windows-msvc-release --target AgPlayer qml_main_window_test settings_controller_test
ctest --test-dir build/release -C Release -R '^(qml_color_picker_test|qml_main_window_test|qml_mini_player_test|settings_controller_test)$' --output-on-failure
cmake --build --preset windows-msvc-release --target agplayer_app_qml_qmllint
git diff --check
```

Record exact pass/fail counts and any pre-existing failures; do not generalize focused results to the whole suite.

- [ ] **Step 2: Perform Windows visual and interaction acceptance**

Launch the actual Release application through the existing project QA/runtime mechanism, open one color setting, and capture both light and dark states at approximately the supplied reference scale. Check:

- approximately 360 px popup width and compact height;
- top swatch/HEX/close-only row;
- unified RGB container and three gradient sliders;
- 5 × 2 candidates with exact reference palette for `#63316B`;
- selected ring and `✓`;
- outside/Escape/× cancellation and immediate candidate acceptance;
- open-popup live theme switching with unchanged candidates;
- accepted color survives app restart and settings-page cancel restores the prior value.

Store the evidence paths and dimensions in the development record. Do not claim macOS runtime validation from Windows; record shared-QML/static portability only.

- [ ] **Step 3: Write the requirement-to-evidence record**

Create `docs/development/2026-08-25-ag-color-picker.md` with:

- scan result: one old picker, ten callers;
- exact files changed;
- every interaction/algorithm/theme requirement mapped to an automated test or visual evidence;
- build/lint/test commands and exact results;
- confirmation that `SettingsPage.qml`, settings keys/defaults/reset logic, waveform rendering, playback, and FFmpeg were untouched;
- Windows evidence and explicit macOS residual risk.

- [ ] **Step 4: Final diff and commit**

```powershell
git status --short
git diff --check
git diff --stat b9842cd..HEAD
git diff -- app/qml/AgPlayer/SettingsPage.qml qt/src/settings_controller.cpp qt/src/settings_controller.hpp qt/src/waveform_item.cpp
git add -- docs/development/2026-08-25-ag-color-picker.md
git commit -m "docs: record color picker acceptance"
```

Expected: only planned picker/test/build/documentation files are committed; protected settings/rendering files have no diff.
