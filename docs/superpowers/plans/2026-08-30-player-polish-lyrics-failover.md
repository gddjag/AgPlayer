# Player Polish and Free Lyrics Failover Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver the eleven approved player, settings, installer, Windows-shell, waveform-progress, and three-route free-lyrics fixes without regressing the frequency-colour Mix waveform.

**Architecture:** Keep playback, theme, and window state in their existing controllers while extracting reusable QML transport/volume controls for the classic and integrated layouts. Put provider-specific HTTP parsing behind the existing `LyricsProvider` abstraction, then add a sequential `LyricsProviderChain` with per-route health and expose typed source/failure metadata through `LyricsService`. Every subsystem is implemented test-first and committed at an independent review gate.

**Tech Stack:** C++17, Qt 6.7 (Core, Network, Quick, Test), QML/Qt Quick Controls, CMake presets, CTest, Inno Setup, PowerShell QA, Windows native messages.

**Spec:** `docs/superpowers/specs/2026-08-30-player-polish-lyrics-failover-design.md`

**Verified upstream read contracts (checked 2026-08-30):**
- LRCLIB: `https://github.com/tranxuanthang/lrclib` and `https://lrclib.net/api/get|search` (MIT, completely free read API).
- Unison: `https://github.com/better-lyrics/unison` and `https://unison.boidu.dev/lyrics|lyrics/search` (read-only access; required display attribution `Lyrics from Unison (https://unison.boidu.dev)`).
- lyrics.ovh: `https://github.com/NTag/lyrics.ovh` and `https://api.lyrics.ovh/v1/{artist}/{title}` (MIT server, plain-text fallback).

## Global Constraints

- Baseline is `main` at or after `196d9a5`; preserve its low/mid/high frequency Mix waveform and colour blending.
- AgPlayer is free and noncommercial; use no paid lyrics API, account, user key, or bundled service runtime.
- Send only title, artist, album, and duration to lyrics services; never send local paths or audio bytes.
- Reuse shared QML components and `Theme` tokens for Dark, Light, and System; do not fork business logic by theme.
- Do not change decoding, playback queue rules, EQ DSP values, or controller timer frequency.
- Do not package or publish an EXE during the implementation tasks; packaging remains a later explicit release action.
- Before every commit, run `git diff --check`, inspect `git diff --stat`, and stage only the files named by that task.

---

### Task 1: Build reusable classic, integrated, and mini player control layouts

**Files:**
- Create: `app/qml/AgPlayer/components/TransportControls.qml`
- Create: `app/qml/AgPlayer/components/PlayerVolumeControl.qml`
- Create: `app/qml/AgPlayer/components/IntegratedPlayerControls.qml`
- Modify: `app/qml/AgPlayer/components/PlayerControls.qml`
- Modify: `app/qml/AgPlayer/components/MiniPlayerControls.qml`
- Modify: `app/qml/AgPlayer/components/ExperienceActions.qml`
- Modify: `app/qml/AgPlayer/Main.qml`
- Modify: `assets/icons/lyrics.svg`
- Modify: `assets/icons/immersive-visual-mode.svg`
- Modify: `app/CMakeLists.txt`
- Create: `tests/scripts/player_action_icon_contract_test.ps1`
- Modify: `tests/CMakeLists.txt`
- Modify: `tests/qml/tst_main_window.qml`
- Modify: `tests/qml/tst_mini_player.qml`
- Modify: `tests/qml/tst_immersive_integration.qml`

**Interfaces:**
- Consumes: `PlaybackController`, `SettingsController.themeMode`, `SettingsController.windowLayoutTheme`, `WindowController`, `PlayerExperienceController`.
- Produces: `TransportControls.openEqualizerRequested()`, `PlayerVolumeControl.emptyMode`, and `IntegratedPlayerControls.openEqualizerRequested()`.
- Produces object names used by QA: `equalizerButton`, `waveformModeButton`, `previousButton`, `playPauseButton`, `nextButton`, `modeButton`, `lyricsActionButton`, `muteButton`, `themeModeButton`, and `windowLayoutButton`.

- [ ] **Step 1: Add failing layout-order tests**

Add a shared assertion to `tst_main_window.qml` and explicit mini assertions:

```qml
function verifyAscendingX(parent, names) {
    var previousX = -1
    for (var i = 0; i < names.length; ++i) {
        var item = findChild(parent, names[i])
        verify(item !== null, "missing " + names[i])
        verify(item.visible, names[i] + " must be visible")
        var x = item.mapToItem(parent, 0, 0).x
        verify(x > previousX, names[i] + " is out of order")
        previousX = x
    }
}

function test_integrated_player_control_order() {
    SettingsController.windowLayoutTheme = "single-window"
    var controls = findChild(mainWindow, "integratedPlayerControls")
    verifyAscendingX(controls, [
        "listWindowButton", "audioToolsButton", "equalizerButton",
        "waveformModeButton", "previousButton", "playPauseButton",
        "nextButton", "modeButton", "lyricsActionButton", "muteButton",
        "themeModeButton", "immersiveActionButton", "windowLayoutButton"
    ])
}
```

In `tst_mini_player.qml`, require:

```qml
verifyAscendingX(controls, [
    "miniWaveformModeButton", "miniPreviousButton", "miniPlayPauseButton",
    "miniNextButton", "miniModeButton", "miniMuteButton"
])
verify(findChild(controls, "lyricsActionButton") === null)
verify(findChild(controls, "immersiveActionButton") === null)
```

Change the immersive integration test so it no longer expects a lyrics action in the mini player. Require normal/compact lyrics icon dimensions `20`/`16` in the shared action tests.

Add a static icon contract that locks the two repository assets to the user-supplied SVGs at `E:/Administrator/下载/歌词.svg` and `E:/Administrator/下载/沉浸视觉模式.svg`. The test stores the SHA-256 values of the committed canonical files and also parses each file as XML to require an SVG `viewBox`; register it as `player_action_icon_contract_test` in `tests/CMakeLists.txt`.

- [ ] **Step 2: Run the focused QML tests and verify they fail**

```powershell
cmake --build --preset windows-msvc-release --target qml_main_window_test qml_mini_player_test --parallel 4
ctest --test-dir build/release -R '^(qml_main_window_test|qml_mini_player_test|qml_immersive_integration_test)$' --output-on-failure
```

Expected: FAIL because integrated controls still instantiate classic `PlayerControls`, mini still contains `ExperienceActions`, and lyrics icons are 18/15 px.

- [ ] **Step 3: Extract transport and volume controls without changing behavior**

Move the existing EQ, waveform, previous, play/pause, next, and mode buttons into `TransportControls.qml`:

```qml
RowLayout {
    id: root
    signal openEqualizerRequested()
    property bool compact: false
    spacing: compact ? 4 : 16
    // Existing buttons retain their objectName, icon, tooltip, and controller call.
}
```

Move `mainVolumeControl` into `PlayerVolumeControl.qml` with:

```qml
Item {
    id: root
    property bool emptyMode: false
    property bool expanded: false
    readonly property alias muteButton: muteButton
    // Preserve the 120 ms open timer, 2000 ms close timer, slider binding,
    // mute action, and existing object names.
}
```

Make classic `PlayerControls.qml` compose the extracted controls so its centre remains `EQ → waveform → previous → play → next → mode → volume`.

- [ ] **Step 4: Implement integrated and mini orders**

`IntegratedPlayerControls.qml` is one `RowLayout` in this exact order:

```qml
RowLayout {
    objectName: "integratedPlayerControls"
    ToolButton { objectName: "listWindowButton"; onClicked: WindowController.toggleListWindow() }
    ToolButton { objectName: "audioToolsButton"; onClicked: WindowController.showAudioTools() }
    TransportControls { onOpenEqualizerRequested: root.openEqualizerRequested() }
    ExperienceActions { showImmersive: false; showLyrics: true }
    PlayerVolumeControl { emptyMode: root.emptyMode }
    ToolButton {
        objectName: "themeModeButton"
        icon.source: Theme.icon("brush-line")
        onClicked: SettingsController.themeMode = (SettingsController.themeMode + 1) % 3
    }
    ExperienceActions { showImmersive: true; showLyrics: false }
    ToolButton {
        objectName: "windowLayoutButton"
        icon.source: Theme.icon("player-shell-mode")
        onClicked: SettingsController.windowLayoutTheme = "dual-window"
    }
}
```

Classic secondary actions become `audio tools → lyrics → theme → immersive → window layout`; the classic layout button sets `single-window`. Replace the integrated component in `Main.qml` with `IntegratedPlayerControls`.

In `MiniPlayerControls.qml`, delete `ExperienceActions`, move `miniModeButton` after `miniNextButton`, and keep it immediately before `miniVolumeControl`. Add `objectName` values `miniPreviousButton`, `miniPlayPauseButton`, and `miniNextButton` to make order testable.

Set `ExperienceActions` icon size to:

```qml
icon.width: root.compact ? 16 : 20
icon.height: root.compact ? 16 : 20
```

Replace the two repository SVG payloads with the user-supplied artwork while preserving their scalable `viewBox`; do not rasterize them or recolour paths in the asset file because `ThemedIcon` supplies the runtime tint.

- [ ] **Step 5: Register new QML files and run focused tests**

Add all three new components to the QML module source list in `app/CMakeLists.txt`, rebuild, and rerun the Step 2 command. Expected: PASS.

- [ ] **Step 6: Commit the player-control composition**

```powershell
git add -- app/qml/AgPlayer/components/TransportControls.qml app/qml/AgPlayer/components/PlayerVolumeControl.qml app/qml/AgPlayer/components/IntegratedPlayerControls.qml app/qml/AgPlayer/components/PlayerControls.qml app/qml/AgPlayer/components/MiniPlayerControls.qml app/qml/AgPlayer/components/ExperienceActions.qml app/qml/AgPlayer/Main.qml assets/icons/lyrics.svg assets/icons/immersive-visual-mode.svg app/CMakeLists.txt tests/scripts/player_action_icon_contract_test.ps1 tests/CMakeLists.txt tests/qml/tst_main_window.qml tests/qml/tst_mini_player.qml tests/qml/tst_immersive_integration.qml
git commit -m "fix(player): align controls across window layouts"
```

### Task 2: Make the 18-band EQ window compact without shrinking text

**Files:**
- Modify: `app/qml/AgPlayer/EqualizerWindow.qml`
- Modify: `tests/qml/tst_equalizer_visual.qml`

**Interfaces:**
- Consumes: existing `EqualizerController` bands, presets, enabled state, and `EqualizerBandSlider`.
- Produces: default `860 × 520`, minimum `760 × 480`, with all 18 bands visible at default width and horizontal scrolling below the content width.

- [ ] **Step 1: Write failing size, band-count, and font tests**

```qml
function test_compact_window_contract() {
    compare(equalizerWindow.width, 860)
    compare(equalizerWindow.height, 520)
    compare(equalizerWindow.minimumWidth, 760)
    compare(equalizerWindow.minimumHeight, 480)
    compare(findChildrenByPrefix(equalizerWindow, "equalizerBand-").length, 18)
    compare(findChild(equalizerWindow, "equalizerTitle").font.pixelSize, 20)
    compare(findChild(equalizerWindow, "equalizerBand-0-frequency").font.pixelSize, 14)
    compare(findChild(equalizerWindow, "equalizerBand-0-value").font.pixelSize, 12)
}
```

Add a second case that sets width to `760` and verifies the band flickable has `contentWidth > width` and `interactive === true`.

- [ ] **Step 2: Verify the test fails**

```powershell
cmake --build --preset windows-msvc-release --target qml_main_window_test --parallel 4
ctest --test-dir build/release -R '^qml_equalizer_visual_test$' --output-on-failure
```

Expected: FAIL on the old `1000 × 600`, `880 × 520` contract.

- [ ] **Step 3: Implement compact responsive geometry**

Set the window geometry to:

```qml
width: 860
height: 520
minimumWidth: 760
minimumHeight: 480
readonly property bool spacious: width >= 1000 && height >= 600
```

Keep compact font values unchanged. Wrap the 18-band row in a horizontal `Flickable` whose content width is the larger of its viewport and the 18 fixed band slots; preserve the existing slider widths and DSP bindings.

- [ ] **Step 4: Run the focused test and commit**

```powershell
ctest --test-dir build/release -R '^qml_equalizer_visual_test$' --output-on-failure
git add -- app/qml/AgPlayer/EqualizerWindow.qml tests/qml/tst_equalizer_visual.qml
git commit -m "fix(eq): compact the eighteen band window"
```

Expected: PASS.

### Task 3: Left-align the shared audio-tools navigation

**Files:**
- Modify: `app/qml/AgPlayer/components/tools/ToolSidebar.qml`
- Modify: `tests/scripts/audio_tools_layout_contract_test.ps1`
- Modify: `tests/qml/tst_audio_editor.qml`
- Modify: `tests/qml/tst_vocal_separation.qml`
- Modify: `tests/qml/tst_format_converter.qml`
- Modify: `tests/qml/tst_metadata_editor.qml`
- Modify: `tests/qml/tst_filename_process.qml`

**Interfaces:**
- Consumes: tool IDs `0, 4, 1, 2, 3`.
- Produces: a shared top navigation ordered Audio Editor, Vocal Separation, Format Conversion, Metadata Edit, Filename Processing, with one fixed left inset.

- [ ] **Step 1: Lock the x-coordinate and order contract**

Add the same assertion to each page fixture:

```qml
var nav = findChild(window, "audioToolsTopNav")
var first = findChild(nav, "audioToolNav_0")
compare(first.mapToItem(nav, 0, 0).x, 12)
verifyAscendingX(nav, ["audioToolNav_0", "audioToolNav_4", "audioToolNav_1",
                       "audioToolNav_2", "audioToolNav_3"])
```

Change the PowerShell contract to reject a leading fill spacer and require exactly one trailing fill spacer.

- [ ] **Step 2: Run tests and verify failure**

```powershell
ctest --test-dir build/release -R '^(qml_audio_editor_test|qml_vocal_separation_test|qml_format_converter_test|qml_metadata_editor_test|qml_filename_process_test|audio_tools_layout_contract_test)$' --output-on-failure
```

Expected: at least the centred workbench variants FAIL.

- [ ] **Step 3: Remove page-dependent centring**

Delete the leading `Item { Layout.fillWidth: !navigation.referenceWorkbench }`. Give the row a fixed left margin of 12, keep one trailing `Item { Layout.fillWidth: true }`, and use the same preferred button width for every tool instead of `referenceWorkbench` / `separationWorkbench` branches.

- [ ] **Step 4: Rerun and commit**

```powershell
ctest --test-dir build/release -R '^(qml_audio_editor_test|qml_vocal_separation_test|qml_format_converter_test|qml_metadata_editor_test|qml_filename_process_test|audio_tools_layout_contract_test)$' --output-on-failure
git add -- app/qml/AgPlayer/components/tools/ToolSidebar.qml tests/scripts/audio_tools_layout_contract_test.ps1 tests/qml/tst_audio_editor.qml tests/qml/tst_vocal_separation.qml tests/qml/tst_format_converter.qml tests/qml/tst_metadata_editor.qml tests/qml/tst_filename_process.qml
git commit -m "fix(audio-tools): left align shared navigation"
```

### Task 4: Reuse a compact portrait audio-file information popup

**Files:**
- Create: `app/qml/AgPlayer/components/AudioFileInfoPanel.qml`
- Modify: `app/qml/AgPlayer/components/TrackList.qml`
- Modify: `app/qml/AgPlayer/components/LibraryManagerPage.qml`
- Modify: `app/CMakeLists.txt`
- Modify: `tests/qml/tst_main_window.qml`

**Interfaces:**
- Consumes: `rows: [{ key: string, label: string, value: string, copyable: bool }]`.
- Produces: `AudioFileInfoPanel.rows`, `AudioFileInfoPanel.fullPath`, and `copyRequested(string value)`.

- [ ] **Step 1: Add failing popup contract tests**

```qml
function verifyFileInfoPanel(panel) {
    compare(panel.width, 300)
    compare(panel.height, Math.min(panel.availableHeight, 470))
    verify(findChild(panel, "audioFileInfoScroll") !== null)
    var path = findChild(panel, "audioFileInfoValue-path")
    compare(path.elide, Text.ElideMiddle)
    compare(path.Accessible.name, panel.fullPath)
}
```

Open both the track-list context action and library-manager context action, verify the same component/object names, field count, Escape closing, and path copy signal.

- [ ] **Step 2: Run and verify the test fails**

```powershell
ctest --test-dir build/release -R '^qml_main_window_test$' --output-on-failure
```

Expected: FAIL because `TrackList` still uses a 340–420 px popup and the two implementations are separate.

- [ ] **Step 3: Implement the shared portrait panel**

Create the shared popup with exact geometry and value behavior:

```qml
Popup {
    id: root
    property var rows: []
    property string fullPath: ""
    signal copyRequested(string value)
    width: 300
    height: Math.min(availableHeight, 470)
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    ScrollView {
        objectName: "audioFileInfoScroll"
        anchors.fill: parent
        // Repeater creates fixed label column and flexible value column.
        // Path uses Text.ElideMiddle and exposes full text via Accessible.name.
    }
}
```

Replace both local popups with `AudioFileInfoPanel`. Supply every existing file field; do not drop format, duration, sample rate, bit depth, channels, bitrate, file size, or path.

- [ ] **Step 4: Rebuild, test, and commit**

```powershell
cmake --build --preset windows-msvc-release --target qml_main_window_test --parallel 4
ctest --test-dir build/release -R '^qml_main_window_test$' --output-on-failure
git add -- app/qml/AgPlayer/components/AudioFileInfoPanel.qml app/qml/AgPlayer/components/TrackList.qml app/qml/AgPlayer/components/LibraryManagerPage.qml app/CMakeLists.txt tests/qml/tst_main_window.qml
git commit -m "fix(library): use compact file information panel"
```

### Task 5: Reduce application and installer languages to Chinese and English

**Files:**
- Modify: `app/qml/AgPlayer/SettingsPage.qml`
- Modify: `qt/src/translation_manager.cpp`
- Modify: `qt/src/settings_controller.cpp`
- Modify: `CMakeLists.txt`
- Modify: `translations/agplayer_zh.ts`
- Modify: `translations/agplayer_en.ts`
- Delete: `translations/agplayer_th.ts`
- Delete: `translations/agplayer_vi.ts`
- Modify: `installer/AgPlayer.iss`
- Delete: `installer/languages/Vietnamese.isl`
- Modify: `tests/qt/translation_manager_test.cpp`
- Modify: `tests/qt/settings_controller_test.cpp`
- Modify: `tests/qml/tst_main_window.qml`
- Modify: `tests/scripts/phase6_translation_coverage_test.ps1`
- Modify: `tests/scripts/installer_contract_test.ps1`

**Interfaces:**
- Consumes: persisted language code and `TranslationManager::normalizedLanguage(QString)`.
- Produces: supported codes exactly `{"zh", "en"}`; unsupported stored values normalize to `zh`.

- [ ] **Step 1: Write failing language and installer contracts**

```cpp
QCOMPARE(TranslationManager::supportedLanguages(), QStringList({"zh", "en"}));
QCOMPARE(TranslationManager::normalizedLanguage(QStringLiteral("th")), QStringLiteral("zh"));
QCOMPARE(TranslationManager::normalizedLanguage(QStringLiteral("vi")), QStringLiteral("zh"));
```

In QML, require the visible model texts to be exactly `中文` and `English`, and reject `CN`, `US`, flag emoji, Thai, and Vietnamese strings. Require `themeModeDark.text === qsTr("深色")`.

Update the installer script contract to require:

```powershell
if ($installer -notmatch '(?m)^ShowLanguageDialog=no\r?$' -or
    $installer.IndexOf('Name: "chinesesimplified"') -gt
    $installer.IndexOf('Name: "english"') -or
    $installer -match 'Name:\s*"thai"|Name:\s*"vietnamese"') {
    throw "Installer must start in Chinese without a language dialog"
}
```

- [ ] **Step 2: Run the focused tests and verify failure**

```powershell
cmake --build --preset windows-msvc-release --target translation_manager_test settings_controller_test qml_main_window_test --parallel 4
ctest --test-dir build/release -R '^(translation_manager_test|settings_controller_test|qml_main_window_test|phase6_translation_coverage_test|installer_contract_test)$' --output-on-failure
```

Expected: FAIL because four app/installer languages and “深色（默认）” remain.

- [ ] **Step 3: Implement the two-language runtime contract**

Change `TranslationManager::supportedLanguages()` to:

```cpp
return {QStringLiteral("zh"), QStringLiteral("en")};
```

Remove Thai/Vietnamese QML options and CMake translation inputs. Normalize stored `th`/`vi` to `zh` through the existing settings load path. Change visible theme strings to `深色` / `Dark`, remove CN/US/flag prefixes, and delete the two unused `.ts` catalogs.

- [ ] **Step 4: Make normal installer startup Chinese without a selector**

Set:

```ini
ShowLanguageDialog=no
LanguageDetectionMethod=none
```

Keep Simplified Chinese first and English second for explicit `/LANG=english` compatibility. Remove Thai/Vietnamese language entries and their custom-message rows; delete `Vietnamese.isl`.

- [ ] **Step 5: Rebuild translations, rerun tests, and commit**

```powershell
cmake --build --preset windows-msvc-release --parallel 4
ctest --test-dir build/release -R '^(translation_manager_test|settings_controller_test|qml_main_window_test|phase6_translation_coverage_test|installer_contract_test)$' --output-on-failure
git add -A -- app/qml/AgPlayer/SettingsPage.qml qt/src/translation_manager.cpp qt/src/settings_controller.cpp CMakeLists.txt translations installer tests/qt/translation_manager_test.cpp tests/qt/settings_controller_test.cpp tests/qml/tst_main_window.qml tests/scripts/phase6_translation_coverage_test.ps1 tests/scripts/installer_contract_test.ps1
git commit -m "fix(localization): keep Chinese and English only"
```

### Task 6: Make integrated waveform progress continuously clipped

**Files:**
- Modify: `app/qml/AgPlayer/components/IntegratedPlayerShell.qml`
- Modify: `tests/qml/tst_waveform.qml`
- Modify: `tests/scripts/integrated_shell_contract_test.ps1`

**Interfaces:**
- Consumes: `WaveformItem.waveformCursorX`, decoded waveform layers, duration, and existing frequency Mix colour properties.
- Produces: `integratedWaveformPlayedClip.width === integratedWaveform.waveformCursorX` without using discrete `playedCount` to draw the visible progress boundary.

- [ ] **Step 1: Add a failing continuous-progress test**

```qml
function test_integrated_progress_uses_pixel_clip() {
    var base = findChild(shell, "integratedWaveform")
    var clip = findChild(shell, "integratedWaveformPlayedClip")
    var played = findChild(shell, "integratedPlayedWaveform")
    verify(base !== null && clip !== null && played !== null)
    compare(clip.width, base.waveformCursorX)
    compare(played.width, base.width)
    compare(played.position, played.duration)
}
```

Add a data loop with fractional progress values `0.001, 0.1234, 0.5005, 0.999` and require pixel clip width to increase monotonically even when the peak index would remain unchanged. Update the PowerShell contract to reject a single position-coloured integrated waveform.

- [ ] **Step 2: Run and verify failure**

```powershell
ctest --test-dir build/release -R '^(qml_waveform_test|integrated_shell_contract_test)$' --output-on-failure
```

Expected: FAIL because integrated mode still paints playback progress inside one `WaveformItem`.

- [ ] **Step 3: Reuse the established double-layer clip pattern**

In `IntegratedPlayerShell.qml`, keep the current base waveform at `position: 0`, then add:

```qml
Item {
    objectName: "integratedWaveformPlayedClip"
    width: integratedWaveform.waveformCursorX
    height: integratedWaveform.height
    clip: true
    WaveformItem {
        objectName: "integratedPlayedWaveform"
        enabled: false
        width: integratedWaveform.width
        height: integratedWaveform.height
        position: duration
        // Bind every mode, colour, frequency layer, strength, density,
        // thickness, amplitude, duration, layers, and visible range to base.
    }
}
```

Do not modify `waveform_analyzer`, `waveform_cache`, Mix colour blending, or playback timer frequency.

- [ ] **Step 4: Rerun and commit**

```powershell
ctest --test-dir build/release -R '^(qml_waveform_test|integrated_shell_contract_test|qml_mini_player_test)$' --output-on-failure
git add -- app/qml/AgPlayer/components/IntegratedPlayerShell.qml tests/qml/tst_waveform.qml tests/scripts/integrated_shell_contract_test.ps1
git commit -m "fix(waveform): smooth integrated playback progress"
```

### Task 7: Restore Windows taskbar click semantics for the frameless window group

**Files:**
- Modify: `qt/src/window_controller.hpp`
- Modify: `qt/src/window_controller.cpp`
- Modify: `tests/qt/window_controller_test.cpp`
- Modify: `tests/scripts/windows_shell_runtime_test.ps1`

**Interfaces:**
- Consumes: existing `WindowController::nativeEventFilter`, registered main/list windows, `WM_SYSCOMMAND`, `SC_MINIMIZE`, and `SC_RESTORE`.
- Produces: `ensureTaskbarWindowStyles(QWindow*)` on Windows and deterministic foreground/background/minimized taskbar behavior.

- [ ] **Step 1: Add a failing native style test**

```cpp
#ifdef Q_OS_WIN
const HWND hwnd = reinterpret_cast<HWND>(window.winId());
const LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
QVERIFY(style & WS_SYSMENU);
QVERIFY(style & WS_MINIMIZEBOX);
#endif
```

Add message-driven cases for visible foreground `SC_MINIMIZE`, minimized `SC_RESTORE`, and visible background activation. Keep the existing test proving taskbar activation does not cancel an in-flight minimize.

- [ ] **Step 2: Run and verify failure**

```powershell
cmake --build --preset windows-msvc-release --target window_controller_test --parallel 4
ctest --test-dir build/release -R '^window_controller_test$' --output-on-failure
```

Expected: style assertion or taskbar transition FAILS on the current frameless window contract.

- [ ] **Step 3: Apply the minimum Shell-compatible native styles**

Implement a Windows-only helper:

```cpp
void WindowController::ensureTaskbarWindowStyles(QWindow* window)
{
#ifdef Q_OS_WIN
    if (window == nullptr) return;
    const HWND hwnd = reinterpret_cast<HWND>(window->winId());
    LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
    style |= WS_SYSMENU | WS_MINIMIZEBOX;
    SetWindowLongPtr(hwnd, GWL_STYLE, style);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE
                 | SWP_FRAMECHANGED);
#endif
}
```

Call it when registering or recreating the main taskbar window. Preserve frameless flags and existing `WM_SYSCOMMAND` routing. Do not turn generic `WM_ACTIVATE` into a hide command.

- [ ] **Step 4: Add a real Shell smoke path**

Extend `windows_shell_runtime_test.ps1` to launch the app, locate the window by AppUserModelID, invoke a real taskbar click, and assert this sequence: visible foreground → minimized, minimized → restored/foreground, background visible → foreground. Run it for `dual-window` and `single-window` settings without moving the stored geometry.

- [ ] **Step 5: Run tests and commit**

```powershell
ctest --test-dir build/release -R '^(window_controller_test|windows_shell_runtime_test)$' --output-on-failure
git add -- qt/src/window_controller.hpp qt/src/window_controller.cpp tests/qt/window_controller_test.cpp tests/scripts/windows_shell_runtime_test.ps1
git commit -m "fix(windows): restore taskbar window toggling"
```

### Task 8: Add typed lyrics source, route-attempt, and cache metadata

**Files:**
- Modify: `qt/src/lyrics_provider.hpp`
- Modify: `qt/src/lyrics_provider.cpp`
- Modify: `qt/src/lyrics_cache.hpp`
- Modify: `qt/src/lyrics_cache.cpp`
- Modify: `tests/qt/lyrics_service_test.cpp`

**Interfaces:**
- Produces `LyricsProvider::Source`, `LyricsProvider::RouteAttempt`, source-aware `Candidate`, and `Result.attempts`.
- Produces cache fields `providerId`, `providerName`, `sourceUrl`, `attribution`, and `synchronized`.

- [ ] **Step 1: Write failing serialization and metadata tests**

```cpp
LyricsProvider::Candidate candidate;
candidate.source = {"unison", "Unison", QUrl("https://unison.boidu.dev"),
                    "Lyrics from Unison (https://unison.boidu.dev)", true};
candidate.syncedLyrics = QStringLiteral("[00:01.00]line");
LyricsCache::Entry entry{LyricsLineModel::parseLrc(candidate.syncedLyrics.toUtf8()),
                         candidate.source, false, true};
QVERIFY(cache.save(track, entry));
const auto loaded = cache.load(track);
QCOMPARE(loaded->source.providerId, QStringLiteral("unison"));
QCOMPARE(loaded->source.attribution,
         QStringLiteral("Lyrics from Unison (https://unison.boidu.dev)"));
QVERIFY(loaded->synchronized);
```

Also load a legacy cache entry containing only the old `source` string and require it to remain readable.

- [ ] **Step 2: Run and verify compilation failure**

```powershell
cmake --build --preset windows-msvc-release --target lyrics_service_test --parallel 4
```

Expected: FAIL because the new types and cache fields do not exist.

- [ ] **Step 3: Add the exact shared types**

```cpp
struct Source final {
    QString providerId;
    QString providerName;
    QUrl sourceUrl;
    QString attribution;
    bool supportsSyncedLyrics = false;
};
struct RouteAttempt final {
    QString providerId;
    QString providerName;
    QString diagnostic;
    int httpStatus = 0;
    qint64 retryAfterMs = 0;
    bool offline = false;
};
```

Add `Source source` to `Candidate`, `QList<RouteAttempt> attempts` to `Result`, and corresponding cache fields. Version new cache JSON while keeping the old parser fallback.

- [ ] **Step 4: Run and commit**

```powershell
cmake --build --preset windows-msvc-release --target lyrics_service_test --parallel 4
ctest --test-dir build/release -R '^lyrics_service_test$' --output-on-failure
git add -- qt/src/lyrics_provider.hpp qt/src/lyrics_provider.cpp qt/src/lyrics_cache.hpp qt/src/lyrics_cache.cpp tests/qt/lyrics_service_test.cpp
git commit -m "refactor(lyrics): carry source and route metadata"
```

### Task 9: Implement the Unison read-only provider

**Files:**
- Create: `qt/src/unison_lyrics_provider.hpp`
- Create: `qt/src/unison_lyrics_provider.cpp`
- Create: `tests/qt/unison_lyrics_provider_test.cpp`
- Modify: `qt/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: unauthenticated read-only `GET https://unison.boidu.dev/lyrics` and `/lyrics/search`.
- Produces: `UnisonLyricsProvider : LyricsProvider`; no submit, vote, report, account, or signing capability.

- [ ] **Step 1: Add failing URL and parser tests with the existing fake network manager pattern**

```cpp
provider.requestExact(1, {"Song", "Artist", "Album", 180000, false});
QCOMPARE(manager.requests.last().url.queryItemValue("song"), QStringLiteral("Song"));
QCOMPARE(manager.requests.last().url.queryItemValue("artist"), QStringLiteral("Artist"));
QCOMPARE(manager.requests.last().url.queryItemValue("duration"), QStringLiteral("180"));

reply->respond(200, R"({"success":true,"data":{"id":7,"song":"Song",
  "artist":"Artist","album":"Album","duration":180,
  "lyrics":"[00:01.00]Line","format":"lrc","syncType":"linesync"}})");
QCOMPARE(result.candidate.source.providerId, QStringLiteral("unison"));
QCOMPARE(result.candidate.source.attribution,
         QStringLiteral("Lyrics from Unison (https://unison.boidu.dev)"));
```

Cover plain format, 404, 429/Retry-After, timeout, 5xx, malformed JSON, `success:false`, cancellation, and search arrays.

- [ ] **Step 2: Verify the new target fails to configure/build**

```powershell
cmake --preset windows-msvc-release
cmake --build --preset windows-msvc-release --target unison_lyrics_provider_test --parallel 4
```

Expected: FAIL because the provider target does not exist.

- [ ] **Step 3: Implement read-only requests and strict parsing**

Build exact URLs with `QUrlQuery`. Accept `format == "lrc"` as synchronized and `format == "plain"` as plain text. Treat unsupported format or malformed required fields as `TechnicalError` with diagnostic `invalid-response`. Stamp every candidate with:

```cpp
Source{QStringLiteral("unison"), QStringLiteral("Unison"),
       QUrl(QStringLiteral("https://unison.boidu.dev")),
       QStringLiteral("Lyrics from Unison (https://unison.boidu.dev)"), true}
```

Reuse LRCLIB's timeout, User-Agent, HTTP classification, Retry-After, and cancellation behavior without adding a dependency.

- [ ] **Step 4: Run and commit**

```powershell
ctest --test-dir build/release -R '^unison_lyrics_provider_test$' --output-on-failure
git add -- qt/src/unison_lyrics_provider.hpp qt/src/unison_lyrics_provider.cpp tests/qt/unison_lyrics_provider_test.cpp qt/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(lyrics): add free Unison provider"
```

### Task 10: Implement the plain-text lyrics.ovh provider

**Files:**
- Create: `qt/src/lyrics_ovh_provider.hpp`
- Create: `qt/src/lyrics_ovh_provider.cpp`
- Create: `tests/qt/lyrics_ovh_provider_test.cpp`
- Modify: `qt/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `GET https://api.lyrics.ovh/v1/{artist}/{title}`.
- Produces: `LyricsOvhProvider : LyricsProvider` with plain text only and source ID `lyrics-ovh`.

- [ ] **Step 1: Add failing encoded-path and response tests**

```cpp
provider.requestExact(9, {"A/B Song", "AC DC", {}, 0, false});
QCOMPARE(manager.requests.last().url.host(), QStringLiteral("api.lyrics.ovh"));
QVERIFY(manager.requests.last().url.toEncoded().contains("AC%20DC"));
QVERIFY(manager.requests.last().url.toEncoded().contains("A%2FB%20Song"));
reply->respond(200, R"({"lyrics":"first line\nsecond line"})");
QCOMPARE(result.candidate.plainLyrics, QStringLiteral("first line\nsecond line"));
QVERIFY(result.candidate.syncedLyrics.isEmpty());
QVERIFY(!result.candidate.source.supportsSyncedLyrics);
```

Cover 404, timeout, 429, 5xx, malformed JSON, empty lyrics, search delegation, and cancellation.

- [ ] **Step 2: Verify the target fails, then implement**

```powershell
cmake --preset windows-msvc-release
cmake --build --preset windows-msvc-release --target lyrics_ovh_provider_test --parallel 4
```

Expected before implementation: target or compilation FAIL.

Construct path segments with percent encoding rather than string concatenation. `requestSearch()` performs the same exact lookup because the service exposes no metadata search endpoint. Stamp successful candidates with source name `lyrics.ovh`, URL `https://api.lyrics.ovh`, empty attribution, and `supportsSyncedLyrics=false`.

- [ ] **Step 3: Run and commit**

```powershell
ctest --test-dir build/release -R '^lyrics_ovh_provider_test$' --output-on-failure
git add -- qt/src/lyrics_ovh_provider.hpp qt/src/lyrics_ovh_provider.cpp tests/qt/lyrics_ovh_provider_test.cpp qt/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(lyrics): add lyrics ovh fallback"
```

### Task 11: Add sequential failover and per-route circuit breaking

**Files:**
- Create: `qt/src/lyrics_provider_chain.hpp`
- Create: `qt/src/lyrics_provider_chain.cpp`
- Create: `tests/qt/lyrics_provider_chain_test.cpp`
- Modify: `qt/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: ordered `QList<Route>` containing LRCLIB, Unison, and lyrics.ovh providers.
- Produces: `LyricsProviderChain : LyricsProvider`, `routeFailed(quint64, RouteAttempt)`, three-failure circuit breaker, and injectable millisecond clock.

- [ ] **Step 1: Write the complete failing state-machine tests**

Define a fake provider and cover:

```cpp
QCOMPARE(chain.routeIds(), QStringList({"lrclib", "unison", "lyrics-ovh"}));

lrclib.complete(id, Result::notFound());
QCOMPARE(unison.exactRequests.size(), 1);       // silent no-match fallback
unison.complete(id, Result::technicalError(503, false, "server-error"));
QCOMPARE(lyricsOvh.exactRequests.size(), 1);    // technical fallback
lyricsOvh.complete(id, Result::found(plainCandidate));
QCOMPARE(finalResult.candidate.source.providerId, QStringLiteral("lyrics-ovh"));
```

Add cases for: search stage preserved across routes, first success stops, instrumental success stops, 429 obeys Retry-After and switches, three technical failures block only that route for 600000 ms, one probe after clock advance, cancellation only cancels the active provider, stale completion ignored, all-not-found returns `NotFound`, and mixed technical/all-failed returns attempts for every route.

- [ ] **Step 2: Verify the new target fails**

```powershell
cmake --preset windows-msvc-release
cmake --build --preset windows-msvc-release --target lyrics_provider_chain_test --parallel 4
```

Expected: target or compilation FAIL.

- [ ] **Step 3: Implement the chain state**

Use these exact public types:

```cpp
class LyricsProviderChain final : public LyricsProvider {
    Q_OBJECT
public:
    using Clock = std::function<qint64()>;
    struct Route final { QString id; QString name; LyricsProvider* provider = nullptr; };
    explicit LyricsProviderChain(QList<Route> routes, QObject* parent = nullptr,
                                 Clock clock = {});
    void requestExact(quint64, const Track&) override;
    void requestSearch(quint64, const Track&) override;
    void cancel(quint64) override;
    [[nodiscard]] QStringList routeIds() const;
signals:
    void routeFailed(quint64 requestId, const LyricsProvider::RouteAttempt& attempt);
};
```

For each request store stage, track, route index, attempts, and active provider. `NotFound` or empty search advances silently. `TechnicalError` and `RateLimited` append an attempt, emit `routeFailed`, then advance. Only `TechnicalError` increments the consecutive technical-failure count; three such failures set `blockedUntilMs = clock() + 600000`. A 429 does not increment that count and sets `blockedUntilMs = max(clock()+retryAfterMs, blockedUntilMs)`. A success resets that route's technical-failure count.

- [ ] **Step 4: Run and commit**

```powershell
ctest --test-dir build/release -R '^lyrics_provider_chain_test$' --output-on-failure
git add -- qt/src/lyrics_provider_chain.hpp qt/src/lyrics_provider_chain.cpp tests/qt/lyrics_provider_chain_test.cpp qt/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(lyrics): add resilient provider failover"
```

### Task 12: Integrate the provider chain, source attribution, and nonmodal notices

**Files:**
- Modify: `qt/src/lyrics_service.hpp`
- Modify: `qt/src/lyrics_service.cpp`
- Modify: `app/qml/AgPlayer/components/LyricsPanel.qml`
- Modify: `translations/agplayer_zh.ts`
- Modify: `translations/agplayer_en.ts`
- Modify: `tests/qt/lyrics_service_test.cpp`
- Modify: `tests/qml/tst_immersive_integration.qml`

**Interfaces:**
- Produces QML properties `sourceProvider`, `sourceAttribution`, `synchronizedLyrics`, `routeNotice`, and `routeAttempts`.
- Consumes: `LyricsProviderChain::routeFailed` and source-aware candidates/cache entries.

- [ ] **Step 1: Add failing service and QML tests**

```cpp
QSignalSpy noticeSpy(&service, &LyricsService::routeNoticeChanged);
chain->emitRouteFailure(id, {"lrclib", "LRCLIB", "timeout", 0, 0});
QCOMPARE(service.routeNotice().value("providerName").toString(), QStringLiteral("LRCLIB"));
QCOMPARE(service.routeNotice().value("diagnostic").toString(), QStringLiteral("timeout"));

provider->complete(id, Result::found(unisonCandidate));
QCOMPARE(service.sourceProvider(), QStringLiteral("Unison"));
QCOMPARE(service.sourceAttribution(),
         QStringLiteral("Lyrics from Unison (https://unison.boidu.dev)"));
QVERIFY(service.synchronizedLyrics());
```

Add QML cases requiring a nonmodal `lyricsRouteNotice`, `lyricsSourceText`, exact Unison attribution, lyrics.ovh text “纯文本歌词，无时间轴”, and an all-failed list showing each route result.

- [ ] **Step 2: Run and verify failure**

```powershell
cmake --build --preset windows-msvc-release --target lyrics_service_test qml_main_window_test --parallel 4
ctest --test-dir build/release -R '^(lyrics_service_test|qml_immersive_integration_test)$' --output-on-failure
```

Expected: FAIL because `LyricsService` still constructs only LRCLIB and hardcodes `lrclib` in diagnostics/cache.

- [ ] **Step 3: Construct the default three-route chain**

When no provider is injected, create one shared `QNetworkAccessManager`, then LRCLIB, Unison, lyrics.ovh, and:

```cpp
provider_ = new LyricsProviderChain({
    {QStringLiteral("lrclib"), QStringLiteral("LRCLIB"), lrclib},
    {QStringLiteral("unison"), QStringLiteral("Unison"), unison},
    {QStringLiteral("lyrics-ovh"), QStringLiteral("lyrics.ovh"), lyricsOvh},
}, this);
```

Retain constructor injection for unit tests. Connect `routeFailed` only when the provider is a chain. Remove service-wide route blackout/retry blocking; per-route health belongs to the chain. Keep aggregate status `Offline` only after the chain's final result.

- [ ] **Step 4: Remove hardcoded source names and expose QML state**

Save/apply `candidate.source` and synchronized capability. Convert attempts to a `QVariantList` with provider name, diagnostic, HTTP status, and retry delay. Emit a short-lived, nonmodal route notice for technical failures. Do not put query metadata, URL query strings, local paths, or lyrics text into diagnostics.

- [ ] **Step 5: Render notices and source attribution**

In `LyricsPanel.qml`, add:

```qml
Text {
    objectName: "lyricsSourceText"
    visible: root.service && root.service.status === LyricsService.Ready
    text: root.service ? root.service.sourceAttribution : ""
}
Rectangle {
    objectName: "lyricsRouteNotice"
    visible: root.service && root.service.routeNotice.providerName
    Text { text: qsTr("%1 线路不可用，已自动切换：%2")
                   .arg(root.service.routeNotice.providerName)
                   .arg(root.service.routeNotice.diagnostic) }
}
```

For lyrics.ovh success, show `qsTr("纯文本歌词，无时间轴")`. For final failure, repeat `routeAttempts` below the status without blocking playback.

- [ ] **Step 6: Run focused tests and commit**

```powershell
cmake --build --preset windows-msvc-release --parallel 4
ctest --test-dir build/release -R '^(lyrics_service_test|unison_lyrics_provider_test|lyrics_ovh_provider_test|lyrics_provider_chain_test|qml_immersive_integration_test)$' --output-on-failure
git add -- qt/src/lyrics_service.hpp qt/src/lyrics_service.cpp app/qml/AgPlayer/components/LyricsPanel.qml translations/agplayer_zh.ts translations/agplayer_en.ts tests/qt/lyrics_service_test.cpp tests/qml/tst_immersive_integration.qml
git commit -m "feat(lyrics): integrate free multi route search"
```

### Task 13: Run full regression, visual, network-failure, and Windows interaction acceptance

**Files:**
- Modify: `docs/development/player-polish-lyrics-acceptance.md`
- Modify only if evidence exposes a defect: files owned by Tasks 1–12 and their tests.

**Interfaces:**
- Consumes: all deliverables from Tasks 1–12.
- Produces: reproducible evidence mapped to requirements 1–11, with no release-package claim.

- [ ] **Step 1: Configure and build Debug and Release from the committed tree**

```powershell
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug --parallel 4
cmake --preset windows-msvc-release
cmake --build --preset windows-msvc-release --parallel 4
```

Expected: both builds exit 0.

- [ ] **Step 2: Run the complete test suites and static checks**

```powershell
ctest --test-dir build/debug --output-on-failure
ctest --test-dir build/release --output-on-failure
$qmlFiles = Get-ChildItem -LiteralPath app/qml/AgPlayer -Recurse -Filter *.qml | ForEach-Object FullName
& 'D:/Qt/6.7.0/msvc2019_64/bin/qmllint.exe' -I build/release/app/qml @qmlFiles
git diff --check
```

Expected: all configured tests pass; QML lint and whitespace checks exit 0. Record exact counts and elapsed time in the acceptance document.

- [ ] **Step 3: Capture the three-layout, three-theme visual matrix**

Use the existing `scripts/qa-final-ui-matrix.ps1` harness and capture dual, integrated, and mini controls in Dark, Light, and System. Compare against:

- `E:/Administrator/下载/微信图片_2026-08-30_150619_106.png`
- `E:/Administrator/下载/微信图片_2026-08-30_150555_936.png`

Record pixel dimensions, button order, icon sizes, EQ default/minimum window, left-aligned audio-tool tabs, and 300×470 file-info panel. Reject the build if any theme changes button availability or ordering.

- [ ] **Step 4: Perform real audio and progress acceptance**

Play a real local track for at least 60 seconds, seek forward/backward, pause/resume, change waveform mode, and switch all three themes. Record that the played clip moves every visible frame without bucket jumps and the low/mid/high Mix colour blend remains overlaid on one mirrored waveform.

- [ ] **Step 5: Perform deterministic lyrics failure acceptance**

Using the fake-network integration harness, execute: LRCLIB timeout → Unison success; LRCLIB 404 → Unison 429 → lyrics.ovh success; three LRCLIB technical failures → ten-minute circuit skip; all three fail; switch tracks while a request is in flight. Then make one live request per route and record HTTP result, source label, time-axis capability, attribution, and fallback notice. A live provider outage is recorded as an external availability risk, not hidden by fake success.

- [ ] **Step 6: Perform real Windows Shell and installer acceptance**

At 100% and 150% DPI, in dual and integrated layouts, verify actual taskbar clicks for foreground minimize, minimized restore, and background activation while settings/EQ is open. Compile the installer and launch it in a clean Windows VM; record that no language dialog appears and the first page is Chinese. Do not distribute the installer from this task.

- [ ] **Step 7: Final diff review and acceptance commit**

```powershell
git status --short
git diff --stat HEAD~12..HEAD
git diff --check HEAD~12..HEAD
git log --oneline --decorate -15
git add -- docs/development/player-polish-lyrics-acceptance.md
git commit -m "docs(qa): record player polish acceptance"
git status --short
```

Expected: the final status is clean; acceptance evidence maps every requirement 1–11 to an automated test and a real check. If a defect required code changes, commit the fix with its regression test before the acceptance-document commit.

## Implementation Order and Review Gates

- Tasks 1–5 are independent UI/settings work but must be reviewed one commit at a time because they share QML test harnesses.
- Task 6 starts only after Task 1 so the integrated control/waveform files are not edited concurrently.
- Task 7 is independent of lyrics and can run after the UI commits are stable.
- Lyrics tasks are strictly ordered `8 → 9/10 → 11 → 12`; Tasks 9 and 10 may be implemented independently after Task 8, but their CMake edits must be integrated serially.
- Task 13 begins only when Tasks 1–12 are committed and focused tests pass.
