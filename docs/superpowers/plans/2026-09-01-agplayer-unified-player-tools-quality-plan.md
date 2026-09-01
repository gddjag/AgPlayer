# AgPlayer Unified Player and Audio Tools Quality Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the approved in-scope AgPlayer waveform, player-shell, lyrics, editor, separation, metadata, and audio-tool corrections with shared behavior across themes.

**Architecture:** Extend existing shared C++ controllers and Qt Quick scene-graph items, then make each QML shell consume shared action/style contracts. Implement independent regression-tested commits for waveform policy, thumbnails, player shells, lyrics, rolling mode, editor, separation, metadata/preview arbitration, and final visual polish. Requirement 8 (immersive visual implementation) remains exclusively owned by its dedicated session.

**Tech Stack:** C++17, Qt 6.7, Qt Quick/QML, Qt Quick Controls, Qt Test, CTest, FFmpeg, miniaudio, Inno Setup.

**Spec:** `docs/superpowers/specs/2026-09-01-agplayer-unified-player-tools-quality-design.md`

## Global Constraints

- Do not edit `ImmersiveWindow.qml`, `ImmersiveSurface.qml`, terrain-reactor C++/shader files, or immersive-only tests.
- Keep official model URLs first; mirrors are fallback routes and every activated file must pass expected size and SHA-256 checks.
- Metadata force mode may relax container-normalization comparisons but may never bypass audio-preservation validation or rollback.
- Reuse `TransportControls.qml`, `TrackList.qml`, `SharedWaveformView.qml`, theme tokens, and existing controllers before adding a new abstraction.
- Preserve current public interfaces unless this plan names the exact extension.
- Each task begins with a failing focused test and ends with a diff review and commit.
- Record requirement evidence in `docs/development/2026-09-01-unified-player-tools-quality.md` as each task completes.
- Do not package until the user separately requests packaging after final acceptance.

## Requirement-to-task map

| Requirement IDs | Owning task |
|---|---|
| 1, 2, 10 | Task 1 — shared waveform policy and settings UI |
| 5 | Task 2 — thumbnail fast path |
| 3, 4, 6, 11, 12, 20 | Task 3 — player shells, dialogs, About, and EQ |
| 7 | Task 4 — lyrics |
| 9, 13 | Task 5 — dual and rolling waveforms |
| 14, 15 | Task 6 — audio editor |
| 16 | Task 7 — separation |
| 17, 18, 19 | Task 8 — metadata, tool navigation, preview focus |
| 8 | External dedicated session; compatibility only in Task 9 |
| 21 | Task 9 — complete acceptance and cleanup |

---

### Task 1: Shared waveform style, palette labels, thumbnail brightness, and compact color popup

**Files:**
- Modify: `qt/src/frequency_color_waveform_settings.hpp`
- Modify: `qt/src/frequency_color_waveform_settings.cpp`
- Modify: `qt/src/settings_controller.hpp`
- Modify: `qt/src/settings_controller.cpp`
- Modify: `qt/src/waveform_item.cpp`
- Modify: `app/qml/AgPlayer/SettingsPage.qml`
- Modify: `app/qml/AgPlayer/components/ColorField.qml`
- Modify: `app/qml/AgPlayer/components/SharedWaveformView.qml`
- Modify: `tests/qt/settings_controller_test.cpp`
- Modify: `tests/qt/waveform_item_test.cpp`
- Modify: `tests/qml/tst_main_window.qml`
- Create: `docs/development/2026-09-01-unified-player-tools-quality.md`

**Interfaces:**
- Produces: `SettingsController::trackWaveformBrightness() -> double`, range `0.20..1.00`, default `0.66`.
- Produces: `SettingsController::setTrackWaveformBrightness(double)` and `trackWaveformBrightnessChanged()`.
- Preserves: `FrequencyColorWaveformSettings::unplayedOpacity`, used by every full waveform shell.
- Produces QML palette label model with exactly eight frequency-band names.

- [ ] **Step 1: Add failing persistence and default tests**

Add to `settings_controller_test.cpp`:

```cpp
void SettingsControllerTest::trackWaveformBrightnessDefaultsAndPersists()
{
    QSettings persisted;
    persisted.clear();
    {
        SettingsController settings;
        QCOMPARE(settings.trackWaveformBrightness(), 0.66);
        settings.setTrackWaveformBrightness(0.72);
        QCOMPARE(settings.trackWaveformBrightness(), 0.72);
        QCOMPARE(persisted.value(
                     QStringLiteral("appearance/trackWaveformBrightness"))
                     .toDouble(),
                 0.72);
    }
    SettingsController reloaded;
    QCOMPARE(reloaded.trackWaveformBrightness(), 0.72);
    persisted.clear();
}
```

Add waveform assertions that visual mode 3 uses full alpha for played samples,
shared `unplayedOpacity` for unplayed samples, and solid mode defaults to
`#9098A6` for its base color.

- [ ] **Step 2: Run the focused tests and confirm failure**

Run:

```powershell
ctest --test-dir build/release --output-on-failure -R "^(settings_controller_test|waveform_item_test|qml_main_window_test)$"
```

Expected: the brightness API, frequency labels, compact popup geometry, and
`#9098A6` assertions fail before production changes.

- [ ] **Step 3: Implement the shared settings property**

Add this property contract to `SettingsController`:

```cpp
Q_PROPERTY(double trackWaveformBrightness
           READ trackWaveformBrightness
           WRITE setTrackWaveformBrightness
           NOTIFY trackWaveformBrightnessChanged)
```

Persist it under `appearance/trackWaveformBrightness`, clamp finite values to
`0.20..1.00`, and use `0.66` for missing/invalid values. Emit and save only when
the value changes.

- [ ] **Step 4: Normalize waveform settings UI and color popup**

Use the model:

```qml
readonly property var spectralBandLabels: [
    qsTr("最低频"), qsTr("低频"), qsTr("低中频"), qsTr("中频"),
    qsTr("中高频"), qsTr("高频"), qsTr("更高频"), qsTr("最高频")
]
```

Remove visible hex labels, retain color swatches, restore the solid base color to
`#9098A6`, add the 66% thumbnail-brightness slider after thumbnail color, and
bind it directly to `SettingsController.trackWaveformBrightness`.

Set the color popup preferred size to `360 x 430`, cap it to 90% of available
screen geometry, and preserve HSV, preview, confirm, cancel, keyboard focus, and
escape behavior.

- [ ] **Step 5: Run focused tests and QML lint**

```powershell
cmake --build build/release --target AgPlayer settings_controller_test waveform_item_test qml_main_window_test --parallel
cmake --build build/release --target all_qmllint
ctest --test-dir build/release --output-on-failure -R "^(settings_controller_test|waveform_item_test|qml_main_window_test)$"
```

- [ ] **Step 6: Capture settings evidence and commit**

Capture dark/light settings screenshots showing the compact popup, eight labels,
solid base color, and 66% slider. Record paths and test results in the development
record.

```powershell
git diff --check
git add qt/src/frequency_color_waveform_settings.* qt/src/settings_controller.* qt/src/waveform_item.cpp app/qml/AgPlayer/SettingsPage.qml app/qml/AgPlayer/components/ColorField.qml app/qml/AgPlayer/components/SharedWaveformView.qml tests/qt/settings_controller_test.cpp tests/qt/waveform_item_test.cpp tests/qml/tst_main_window.qml docs/development/2026-09-01-unified-player-tools-quality.md
git commit -m "fix(waveform): unify palette contrast and list brightness"
```

### Task 2: Thumbnail fast path and cross-theme list brightness

**Files:**
- Modify: `qt/src/track_waveform_thumbnail_provider.hpp`
- Modify: `qt/src/track_waveform_thumbnail_provider.cpp`
- Modify: `qt/src/track_waveform_thumbnail_item.hpp`
- Modify: `qt/src/track_waveform_thumbnail_item.cpp`
- Modify: `app/qml/AgPlayer/components/TrackWaveformThumbnail.qml`
- Modify: `app/qml/AgPlayer/components/TrackList.qml`
- Modify: `app/qml/AgPlayer/components/IntegratedPlayerShell.qml`
- Modify: `app/qml/AgPlayer/components/RollingPlayerShell.qml`
- Modify: `tests/qt/track_waveform_thumbnail_provider_test.cpp`
- Modify: `tests/qt/track_waveform_thumbnail_item_test.cpp`
- Modify: `tests/stress/track_waveform_thumbnail_stress_test.cpp`
- Modify: `tests/qml/tst_main_window.qml`
- Modify: `tests/qml/tst_rolling_theme.qml`

**Interfaces:**
- Preserves: `request(trackId, sourcePath, generation, visiblePriority)`.
- Produces diagnostics keys: `visibleQueueDepth`, `backgroundQueueDepth`,
  `cacheHitCount`, `cacheMissCount`, and `firstVisibleLatencyMs`.
- Consumes: `SettingsController.trackWaveformBrightness`.

- [ ] **Step 1: Add performance and priority regression tests**

Extend provider tests to prove:

```cpp
provider.request("background", backgroundPath, 1, false);
provider.request("visible", visiblePath, 1, true);
QTRY_COMPARE_SPY_WITH_TIMEOUT(readySpy, 1, 1000);
QCOMPARE(readySpy.at(0).at(0).toString(), QStringLiteral("visible"));
```

Add tests for cancellation after delegate reuse, cached delivery without worker
creation, bounded 256-track completion work, and brightness-only recoloring that
does not rebuild geometry.

- [ ] **Step 2: Capture a cold/warm baseline and run failures**

```powershell
ctest --test-dir build/release --output-on-failure -R "^(track_waveform_thumbnail_provider_test|track_waveform_thumbnail_item_test|track_waveform_thumbnail_stress_test|qml_main_window_test|qml_rolling_theme_test)$"
```

Record cache directory state, first ten visible-row time, row count, window size,
and display scale before changing production code.

- [ ] **Step 3: Implement bounded compact delivery**

Keep the public request API. Split the pending queue internally into visible and
background order, ensure visible requests preempt background requests, and emit
cached compact peaks synchronously. Downsample scene-graph geometry to the row's
physical pixel width; do not upload all 2048 buckets when fewer pixels are
visible. Spectral bytes may recolor an existing node after the envelope is shown.

- [ ] **Step 4: Bind brightness once in the shared thumbnail component**

Expose one `brightness` property in `TrackWaveformThumbnail.qml` and bind it to
the shared setting. `TrackList.qml` must instantiate this shared component in all
shells; integrated and rolling shells must not apply a second opacity multiplier.

- [ ] **Step 5: Verify performance, lifecycle, and theme synchronization**

```powershell
cmake --build build/release --parallel
ctest --test-dir build/release --output-on-failure -R "^(track_waveform_thumbnail_provider_test|track_waveform_thumbnail_item_test|track_waveform_thumbnail_stress_test|qml_main_window_test|qml_rolling_theme_test)$"
```

Accept only if cached first-ten latency is at most 300 ms, cold first-visible
latency is at most one second, queues stay bounded, and visible delegates never
receive stale generations.

- [ ] **Step 6: Review and commit**

```powershell
git diff --check
git add qt/src/track_waveform_thumbnail_* app/qml/AgPlayer/components/TrackWaveformThumbnail.qml app/qml/AgPlayer/components/TrackList.qml app/qml/AgPlayer/components/IntegratedPlayerShell.qml app/qml/AgPlayer/components/RollingPlayerShell.qml tests/qt/track_waveform_thumbnail_* tests/stress/track_waveform_thumbnail_stress_test.cpp tests/qml/tst_main_window.qml tests/qml/tst_rolling_theme.qml docs/development/2026-09-01-unified-player-tools-quality.md
git commit -m "perf(library): prioritize compact visible waveforms"
```

### Task 3: Shared player action profiles, centered transport, list layout, dialogs, About, and EQ

**Files:**
- Create: `app/qml/AgPlayer/components/PlayerActionProfile.qml`
- Modify: `app/CMakeLists.txt`
- Modify: `app/qml/AgPlayer/components/TransportControls.qml`
- Modify: `app/qml/AgPlayer/components/PlayerControls.qml`
- Modify: `app/qml/AgPlayer/components/IntegratedPlayerControls.qml`
- Modify: `app/qml/AgPlayer/components/MiniPlayerControls.qml`
- Modify: `app/qml/AgPlayer/components/RollingPlayerShell.qml`
- Modify: `app/qml/AgPlayer/components/EmptyStartup.qml`
- Modify: `app/qml/AgPlayer/components/PlayerVolumeControl.qml`
- Modify: `app/qml/AgPlayer/components/TrackList.qml`
- Modify: `app/qml/AgPlayer/components/SearchFilter.qml`
- Modify: `app/qml/AgPlayer/components/AudioFileInfoPanel.qml`
- Modify: `app/qml/AgPlayer/SettingsPage.qml`
- Modify: `app/qml/AgPlayer/EqualizerWindow.qml`
- Modify: `app/qml/AgPlayer/components/EqualizerBandSlider.qml`
- Modify: `tests/qml/tst_player_controls.qml`
- Modify: `tests/qml/tst_main_window.qml`
- Modify: `tests/qml/tst_integrated_theme.qml`
- Modify: `tests/qml/tst_mini_player.qml`
- Modify: `tests/qml/tst_rolling_theme.qml`
- Modify: `tests/qml/tst_equalizer_visual.qml`
- Modify: `tests/scripts/player_action_icon_contract_test.ps1`

**Interfaces:**
- Produces: a data-only `PlayerActionProfile` containing ordered action IDs for
  leading, transport, and trailing slots.
- Preserves: existing action handlers in `TransportControls.qml`.
- Produces: shared icon visual size 20 and hit target at least 36.

- [ ] **Step 1: Add failing shell-layout contracts**

Add QML assertions for exact reference order, equal icon size, full-strip center,
right-growing volume, anchored theme popup, rolling-shell left alignment, startup
actions, a rolling action profile with no waveform-mode switch, portrait
file-info width `228`, cover width `180`, compact filter height,
About copy/version line, and 18-band EQ typography/radius.

The transport-centering assertion must compare scene coordinates:

```qml
compare(Math.round(transport.mapToItem(playerStrip, 0, 0).x
                   + transport.width / 2),
        Math.round(playerStrip.width / 2))
```

- [ ] **Step 2: Run shell tests and confirm failures**

```powershell
ctest --test-dir build/release --output-on-failure -R "^(qml_player_controls_layout_test|qml_main_window_test|qml_integrated_theme_test|qml_mini_player_test|qml_rolling_theme_test|qml_equalizer_visual_test|player_action_icon_contract_test)$"
```

- [ ] **Step 3: Implement data-only action profiles and centered layout**

Move no action handler into the profile. Use anchors or a three-zone layout where
the transport group's center is bound to the entire strip center. Render volume
as an overlay/flyout growing right. Use the rolling profile's explicit left
placement without changing action behavior.

- [ ] **Step 4: Apply shared list, popup, dialog, About, filter, and EQ geometry**

Use `TrackList.qml` for integrated and rolling list rows. Anchor the theme chooser
to its invoking icon with an above-placement fallback. Make file information a
228-pixel portrait scroll panel around the 180-pixel cover. Use one compact
control-height token for search/rating/BPM/clear. Read About version from the
registered application version, render `免费、轻便、纯净`, then
`版本号：v<version>` on its own line. Reduce EQ radius/font weight/spacing while
keeping all 18 controls readable.

- [ ] **Step 5: Run focused tests, QML lint, and screenshot matrix**

```powershell
cmake --build build/release --target all_qmllint
ctest --test-dir build/release --output-on-failure -R "^(qml_player_controls_layout_test|qml_main_window_test|qml_integrated_theme_test|qml_mini_player_test|qml_rolling_theme_test|qml_equalizer_visual_test|player_action_icon_contract_test)$"
```

Capture startup, dual, integrated, mini, rolling, file-info, About, and EQ in
dark/light modes at reference and minimum sizes.

- [ ] **Step 6: Review and commit**

```powershell
git diff --check
git add app/CMakeLists.txt app/qml/AgPlayer/components/PlayerActionProfile.qml app/qml/AgPlayer/components/TransportControls.qml app/qml/AgPlayer/components/PlayerControls.qml app/qml/AgPlayer/components/IntegratedPlayerControls.qml app/qml/AgPlayer/components/MiniPlayerControls.qml app/qml/AgPlayer/components/RollingPlayerShell.qml app/qml/AgPlayer/components/EmptyStartup.qml app/qml/AgPlayer/components/PlayerVolumeControl.qml app/qml/AgPlayer/components/TrackList.qml app/qml/AgPlayer/components/SearchFilter.qml app/qml/AgPlayer/components/AudioFileInfoPanel.qml app/qml/AgPlayer/SettingsPage.qml app/qml/AgPlayer/EqualizerWindow.qml app/qml/AgPlayer/components/EqualizerBandSlider.qml tests/qml/tst_player_controls.qml tests/qml/tst_main_window.qml tests/qml/tst_integrated_theme.qml tests/qml/tst_mini_player.qml tests/qml/tst_rolling_theme.qml tests/qml/tst_equalizer_visual.qml tests/scripts/player_action_icon_contract_test.ps1 docs/development/2026-09-01-unified-player-tools-quality.md
git commit -m "fix(ui): unify player shell actions and compact dialogs"
```

### Task 4: Lyrics placement, auto-hide chrome, failover diagnostics, and icon clarity

**Files:**
- Modify: `qt/src/lyrics_provider_chain.hpp`
- Modify: `qt/src/lyrics_provider_chain.cpp`
- Modify: `qt/src/lyrics_service.hpp`
- Modify: `qt/src/lyrics_service.cpp`
- Modify: `app/qml/AgPlayer/Main.qml`
- Modify: `app/qml/AgPlayer/components/LyricsPanel.qml`
- Modify: `app/qml/AgPlayer/components/IntegratedPlayerShell.qml`
- Modify: `app/qml/AgPlayer/components/RollingPlayerShell.qml`
- Modify: `tests/qt/lyrics_provider_chain_test.cpp`
- Modify: `tests/qt/lyrics_service_test.cpp`
- Modify: `tests/qml/tst_main_window.qml`
- Modify: `tests/qml/tst_integrated_theme.qml`
- Modify: `tests/qml/tst_rolling_theme.qml`

**Interfaces:**
- Produces route diagnostics with `provider`, `state`, and `reason` for each
  attempted free provider.
- Preserves the existing provider chain order and automatic failover.
- Produces QML state `chromeVisible`; lyric text visibility is independent.

- [ ] **Step 1: Add failing route and geometry tests**

Test three sequential failures, selected route, surfaced failed route names,
timeout advancement, panel bottom above player top, three-second pointer-leave
chrome hide, persistent lyric text, 20-pixel icons, circular refresh icon, and
tooltips.

- [ ] **Step 2: Run focused failures**

```powershell
ctest --test-dir build/release --output-on-failure -R "^(lyrics_provider_chain_test|lyrics_service_test|qml_main_window_test|qml_integrated_theme_test|qml_rolling_theme_test)$"
```

- [ ] **Step 3: Implement diagnostic propagation and panel hosting**

Preserve network requests in providers. Add structured attempt diagnostics in
the chain/service, host the panel in list-space rather than player-space, and use
a single-shot 3000 ms QML timer that hides glass/actions only. Do not touch any
immersive component.

- [ ] **Step 4: Verify offline, timeout, not-found, and success cases**

```powershell
cmake --build build/release --parallel
ctest --test-dir build/release --output-on-failure -R "^(lyrics_provider_chain_test|lyrics_service_test|qml_main_window_test|qml_integrated_theme_test|qml_rolling_theme_test)$"
```

- [ ] **Step 5: Capture dark/light lyric evidence and commit**

```powershell
git diff --check
git add qt/src/lyrics_* app/qml/AgPlayer/Main.qml app/qml/AgPlayer/components/LyricsPanel.qml app/qml/AgPlayer/components/IntegratedPlayerShell.qml app/qml/AgPlayer/components/RollingPlayerShell.qml tests/qt/lyrics_* tests/qml/tst_main_window.qml tests/qml/tst_integrated_theme.qml tests/qml/tst_rolling_theme.qml docs/development/2026-09-01-unified-player-tools-quality.md
git commit -m "fix(lyrics): dock panel and expose provider failover"
```

### Task 5: Dual-window completeness and rolling-player precision/layout

**Files:**
- Modify: `qt/src/waveform_item.cpp`
- Modify: `qt/src/waveform_provider.cpp`
- Modify: `app/qml/AgPlayer/components/PlayerPane.qml`
- Modify: `app/qml/AgPlayer/components/SharedWaveformView.qml`
- Modify: `app/qml/AgPlayer/components/RollingPlayerShell.qml`
- Modify: `app/qml/AgPlayer/components/TrackList.qml`
- Modify: `tests/qt/waveform_item_test.cpp`
- Modify: `tests/qt/waveform_provider_test.cpp`
- Modify: `tests/qml/tst_main_window.qml`
- Modify: `tests/qml/tst_rolling_theme.qml`

**Interfaces:**
- Preserves decoded duration and cache generation metadata.
- Uses peak density derived from physical pixels and visible time range.
- Keeps rolling waveform `visualMode` fixed at `3` and never consumes the global
  waveform-mode selection for this shell.
- Adds no timer that rebuilds full waveform geometry on position updates.

- [ ] **Step 1: Add failing half-waveform and rolling-density tests**

Create tests that resize the dual window before cache completion, verify the
rightmost peak remains non-empty without a theme toggle, assert source/cache/DPR
changes invalidate geometry, and prove rolling position updates only dirty color
or translation state. Add QML assertions for metadata, favorite, rating, hover
capsule, reduced margins/height, left transport, and shared row layout.
Assert the rolling waveform remains in frequency-color mode after every global
waveform-mode change and that no rolling player action exposes a waveform toggle.

- [ ] **Step 2: Reproduce and record frame/geometry baseline**

```powershell
ctest --test-dir build/release --output-on-failure -R "^(waveform_item_test|waveform_provider_test|qml_main_window_test|qml_rolling_theme_test)$"
```

Record resize sequence, theme, DPR, peak count, geometry rebuild count, and
rolling frame-time p95.

- [ ] **Step 3: Fix invalidation and physical-pixel resampling**

Invalidate on source, cache generation, width, visible range, and effective DPR.
Resample from the retained peak pyramid to physical width. Keep playhead and
played/unplayed changes on the color/position fast path. Reuse the shared hover
time conversion and metadata/list components.

- [ ] **Step 4: Verify behavior and capture rolling/dual screenshots**

```powershell
cmake --build build/release --parallel
ctest --test-dir build/release --output-on-failure -R "^(waveform_item_test|waveform_provider_test|qml_main_window_test|qml_rolling_theme_test)$"
```

- [ ] **Step 5: Review and commit**

```powershell
git diff --check
git add qt/src/waveform_item.cpp qt/src/waveform_provider.cpp app/qml/AgPlayer/components/PlayerPane.qml app/qml/AgPlayer/components/SharedWaveformView.qml app/qml/AgPlayer/components/RollingPlayerShell.qml app/qml/AgPlayer/components/TrackList.qml tests/qt/waveform_item_test.cpp tests/qt/waveform_provider_test.cpp tests/qml/tst_main_window.qml tests/qml/tst_rolling_theme.qml docs/development/2026-09-01-unified-player-tools-quality.md
git commit -m "fix(player): stabilize full and rolling waveforms"
```

### Task 6: Audio editor volume, range zoom, event selection, compact layout, and export feedback

**Files:**
- Modify: `qt/src/audio_editor/audio_editor_controller.hpp`
- Modify: `qt/src/audio_editor/audio_editor_controller.cpp`
- Modify: `app/qml/AgPlayer/components/tools/AudioEditorPage.qml`
- Modify: `app/qml/AgPlayer/components/audioeditor/EditorWaveformCanvas.qml`
- Modify: `app/qml/AgPlayer/components/audioeditor/EditorSlider.qml`
- Modify: `app/qml/AgPlayer/components/audioeditor/EditorCommandBar.qml`
- Modify: `tests/qt/editor_viewport_test.cpp`
- Modify: `tests/qt/audio_editor_controller_test.cpp`
- Modify: `tests/qml/tst_audio_editor.qml`
- Modify: `tests/qml/tst_audio_editor_native_input.qml`

**Interfaces:**
- Reuses: `EditorViewport::setVisibleRange(qint64 startFrame, qint64 endFrame)`;
  QML converts the two handle ratios using the existing total-frame property.
- Uses existing `AudioEditorController::selectEvent(QString)`.
- Uses existing `progress`, `lastExportPath`, `stateChanged`, and
  `exportResultChanged` for real export UI state.

- [ ] **Step 1: Add failing pointer, zoom, selection, and export tests**

Test upward drag and wheel-up increase volume, downward drag and wheel-down
decrease it, full initial range, two-handle zoom/pan, `Ctrl+right-click` selection,
selected-event highlight/edit targeting, shortcut copy, compact panel geometry,
progress fill, success label, failure label, and cancellation reset.

Use the existing viewport contract:

```cpp
QVERIFY(viewport.setVisibleRange(totalFrames / 4, totalFrames * 3 / 4));
QCOMPARE(viewport.visibleStartFrame(), totalFrames / 4);
QCOMPARE(viewport.visibleEndFrame(), totalFrames * 3 / 4);
```

- [ ] **Step 2: Run focused failures**

```powershell
ctest --test-dir build/release --output-on-failure -R "^(editor_viewport_test|audio_editor_controller_test|qml_audio_editor_test|qml_audio_editor_native_input_test)$"
```

- [ ] **Step 3: Implement normalized range and native pointer semantics**

Map the two handles to normalized document ratios, reject spans below the current
minimum visible frames, preserve span while dragging the middle range, and route
all changes through `EditorViewport`. Invert only the vertical pointer mapping so
top means larger volume. Handle wheel direction explicitly. On Ctrl+right-click,
call `selectEvent(id)` before any edit action and render a shared translucent
selection token.

- [ ] **Step 4: Bind real export state and compact the lower layout**

Use `progress` only while the export job is active, clip a green fill to the
button's progress width, show `✔已导出` only after `lastExportPath` is published,
and restore the retryable label on failure/cancel. Reduce transport height,
center it vertically, expand separator spacing, and remove fixed bottom spacers.

- [ ] **Step 5: Verify tests, visual states, and export a real fixture**

```powershell
cmake --build build/release --parallel
ctest --test-dir build/release --output-on-failure -R "^(editor_viewport_test|audio_editor_controller_test|qml_audio_editor_test|qml_audio_editor_native_input_test)$"
```

Export `tests/fixtures/sine-440hz.wav`, verify the output exists and probes, and
capture idle/exporting/exported/failure screenshots.

- [ ] **Step 6: Review and commit**

```powershell
git diff --check
git add qt/src/audio_editor/audio_editor_controller.hpp qt/src/audio_editor/audio_editor_controller.cpp app/qml/AgPlayer/components/tools/AudioEditorPage.qml app/qml/AgPlayer/components/audioeditor/EditorWaveformCanvas.qml app/qml/AgPlayer/components/audioeditor/EditorSlider.qml app/qml/AgPlayer/components/audioeditor/EditorCommandBar.qml tests/qt/editor_viewport_test.cpp tests/qt/audio_editor_controller_test.cpp tests/qml/tst_audio_editor.qml tests/qml/tst_audio_editor_native_input.qml docs/development/2026-09-01-unified-player-tools-quality.md
git commit -m "fix(editor): correct range volume and export interactions"
```

### Task 7: Separation download routes, model discovery, model UI, and result interactions

**Files:**
- Modify: `qt/src/vocal_separation_catalog.hpp`
- Modify: `qt/src/vocal_separation_catalog.cpp`
- Modify: `qt/src/vocal_separation_installer.hpp`
- Modify: `qt/src/vocal_separation_installer.cpp`
- Modify: `qt/src/vocal_separation_controller.hpp`
- Modify: `qt/src/vocal_separation_controller.cpp`
- Modify: `app/qml/AgPlayer/components/tools/VocalSeparationPage.qml`
- Modify: `tests/qt/vocal_separation_install_test.cpp`
- Modify: `tests/qt/vocal_separation_controller_test.cpp`
- Modify: `tests/qml/tst_vocal_separation.qml`

**Interfaces:**
- Changes `VocalDownloadFile::url` to `QList<VocalDownloadRoute> routes` using
  these exact catalog types:

```cpp
enum class VocalDownloadRouteKind {
    Official,
    PublicMirror,
    DomesticMirror,
};

struct VocalDownloadRoute {
    QString id;
    QString label;
    QUrl url;
    VocalDownloadRouteKind kind = VocalDownloadRouteKind::Official;
};

struct VocalDownloadFile {
    QString fileName;
    QList<VocalDownloadRoute> routes;
    qint64 bytes = 0;
    QString sha256;
};
```
- Produces controller properties `downloadRouteLabel`, `downloadPercent`, and
  `downloadFailoverNotice`.
- Produces invokables `useDomesticMirror(bool)`, `scanModelDirectory()`, and
  `exportStemToOutputDirectory(StemKind)`.
- Preserves expected bytes and SHA-256 on the file, independent of route.

- [ ] **Step 1: Add failing route, scan, preview, drag, and export tests**

Create local HTTP fixtures for official failure, mirror success, range restart,
bad hash rejection, all-route failure, and percentage aggregation. Add known-file
manual detection, compatible/unsupported ONNX contract detection, directory
change persistence, page-entry scan, explicit scan, one-active-stem guide,
pointer-drag volume, and no-dialog stem export tests.

Construct route fixtures explicitly:

```cpp
VocalDownloadFile file{
    "model.onnx",
    {{"official", "官方", officialUrl, VocalDownloadRouteKind::Official},
     {"domestic", "国内镜像", mirrorUrl,
      VocalDownloadRouteKind::DomesticMirror}},
    expectedBytes,
    expectedSha256
};
```

- [ ] **Step 2: Run focused failures**

```powershell
ctest --test-dir build/release --output-on-failure -R "^(vocal_separation_install_test|vocal_separation_controller_test|qml_vocal_separation_test)$"
```

- [ ] **Step 3: Implement verified route failover**

Keep official first. Transform Hugging Face paths to the approved HF-Mirror route
and GitHub Release paths to the approved domestic proxy route in the catalog.
Advance only on retryable network/HTTP failures. Keep or restart `.part` according
to proven range/content identity. Verify bytes and SHA-256 before atomic activation.
Expose route and aggregate percent without polling.

- [ ] **Step 4: Implement model-root scanning and supported ONNX inspection**

Default to `D:/agplayer/Models`, persist a changed absolute local directory,
watch the root and known subdirectories, scan on page entry and explicit `检测`,
and validate known catalog files by name/size/hash. Inspect unknown ONNX tensor
names/shapes against worker-supported profiles before listing them. Display exact
rejection reasons without deleting user files.

- [ ] **Step 5: Correct model/result UI semantics**

Apply the approved Chinese copy, orange/yellow glass quality badges, purple
`检测` button, green percentage progress, route switch/failover notice, per-stem
active guide, direct pointer volume drag, and direct export to configured output.
Only `更改输出目录` may open a directory picker.

- [ ] **Step 6: Verify with local servers and manual-directory fixtures**

```powershell
cmake --build build/release --parallel
ctest --test-dir build/release --output-on-failure -R "^(vocal_separation_install_test|vocal_separation_controller_test|qml_vocal_separation_test)$"
```

Also place a verified catalog model and one incompatible ONNX fixture into a
temporary model root, run detection, and record accepted/rejected UI evidence.

- [ ] **Step 7: Review and commit**

```powershell
git diff --check
git add qt/src/vocal_separation_catalog.hpp qt/src/vocal_separation_catalog.cpp qt/src/vocal_separation_installer.hpp qt/src/vocal_separation_installer.cpp qt/src/vocal_separation_controller.hpp qt/src/vocal_separation_controller.cpp app/qml/AgPlayer/components/tools/VocalSeparationPage.qml tests/qt/vocal_separation_install_test.cpp tests/qt/vocal_separation_controller_test.cpp tests/qml/tst_vocal_separation.qml docs/development/2026-09-01-unified-player-tools-quality.md
git commit -m "feat(separation): add verified mirror and model discovery flows"
```

### Task 8: Metadata force repair, shared tool navigation, and preview focus

**Files:**
- Create: `qt/src/audio_preview_focus_controller.hpp`
- Create: `qt/src/audio_preview_focus_controller.cpp`
- Modify: `qt/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`
- Modify: `app/main.cpp`
- Modify: `core/src/metadata_writer.hpp`
- Modify: `core/src/metadata_writer.cpp`
- Modify: `qt/src/metadata_editor.cpp`
- Modify: `qt/src/audio_preview_controller.hpp`
- Modify: `qt/src/audio_preview_controller.cpp`
- Modify: `qt/src/audio_editor/audio_editor_controller.hpp`
- Modify: `qt/src/audio_editor/audio_editor_controller.cpp`
- Modify: `app/qml/AgPlayer/AudioToolsWindow.qml`
- Modify: `app/qml/AgPlayer/components/tools/ToolSidebar.qml`
- Modify: `app/qml/AgPlayer/components/tools/AudioEditorPage.qml`
- Modify: `app/qml/AgPlayer/components/tools/VocalSeparationPage.qml`
- Modify: `app/qml/AgPlayer/components/tools/FormatConvertPage.qml`
- Modify: `app/qml/AgPlayer/components/tools/MetadataEditPage.qml`
- Modify: `app/qml/AgPlayer/components/tools/FilenameProcessPage.qml`
- Create: `tests/qt/audio_preview_focus_controller_test.cpp`
- Modify: `tests/core/metadata_writer_test.cpp`
- Modify: `tests/qt/audio_preview_controller_test.cpp`
- Modify: `tests/qt/audio_tools_end_to_end_test.cpp`
- Create: `tests/qml/tst_audio_tools.qml`
- Modify: `tests/qml/tst_metadata_editor.qml`
- Modify: `tests/scripts/audio_tools_layout_contract_test.ps1`

**Interfaces:**
- Produces `AudioPreviewFocusController::acquire(QString owner)` and
  `release(QString owner)`.
- Produces signal `stopRequested(QString ownerToStop)` before owner changes.
- Extends `MetadataEditPlan` with:

```cpp
enum class MetadataVerificationMode {
    Strict,
    ContainerNormalized,
};

MetadataVerificationMode verification_mode =
    MetadataVerificationMode::Strict;
```

  `ContainerNormalized` may ignore only proven FFmpeg/container normalization
  differences; it retains packet/payload identity, codec, channel, sample-rate,
  duration, transactional replacement, backup, and rollback checks.

- [ ] **Step 1: Reproduce metadata failure and add focus/navigation tests**

Copy the actual failing media fixture into a temporary test directory and assert
the current writer reports the same readback diagnostic. Add tests proving:

```cpp
QVERIFY(focus.acquire("main"));
QVERIFY(focus.acquire("audio-editor"));
QCOMPARE(stopSpy.takeFirst().at(0).toString(), QStringLiteral("main"));
QVERIFY(focus.acquire("separation"));
QCOMPARE(stopSpy.takeFirst().at(0).toString(), QStringLiteral("audio-editor"));
```

Add five-page navigation alignment/state tests and main-player-stop assertions
for every tool preview path.

- [ ] **Step 2: Run focused failures**

```powershell
ctest --test-dir build/release --output-on-failure -R "^(metadata_writer_test|metadata_editor_layout_contract_test|audio_preview_controller_test|audio_tools_end_to_end_test|qml_audio_tools_test|qml_metadata_editor_test|audio_tools_layout_contract_test)$"
```

- [ ] **Step 3: Trace and repair metadata verification at the source**

Log requested tags, muxer result, probed streams, packet-payload identity inputs,
and readback values in the failing test. Fix the first incorrect comparison or
mux option found. Strict mode remains the default. If the reproduced fixture
proves that FFmpeg changed only normalized container descriptors while the
existing packet/payload comparison and complete audio contract still pass, allow
the metadata editor to retry once with `ContainerNormalized`. Keep temporary
file, `.agbak`, atomic replacement, rollback, and audio contract checks. A failed
payload/codec/channel/rate/duration check must leave the original byte-for-byte
intact.

- [ ] **Step 4: Implement event-driven preview focus**

The focus controller stores one owner string. `acquire(newOwner)` emits
`stopRequested(oldOwner)` before changing ownership; repeated acquisition by the
same owner is a no-op. `release(owner)` clears only a matching owner. Wire main
playback, editor playback, shared audio preview, and separation preview in
`app/main.cpp`; no timer or background thread is added.

- [ ] **Step 5: Unify the five tool navigation headers**

Make `ToolSidebar.qml` the single horizontal, left-aligned navigation renderer
used by audio editor, vocal separation, format conversion, metadata editor, and
filename processing. Pages expose content only and do not duplicate tab buttons.
Match the approved separation header's height, spacing, selected state, hover,
focus, and typography.

- [ ] **Step 6: Verify metadata integrity, focus arbitration, and navigation**

```powershell
cmake --build build/release --parallel
ctest --test-dir build/release --output-on-failure -R "^(metadata_writer_test|metadata_editor_layout_contract_test|audio_preview_focus_controller_test|audio_preview_controller_test|audio_tools_end_to_end_test|qml_audio_tools_test|qml_metadata_editor_test|audio_tools_layout_contract_test)$"
```

Probe the written copy, compare requested tags, audio stream contract, duration,
and packet/decoded payload evidence, then confirm original backup/rollback paths.

- [ ] **Step 7: Review and commit**

```powershell
git diff --check
git add qt/src/audio_preview_focus_controller.hpp qt/src/audio_preview_focus_controller.cpp qt/src/audio_preview_controller.hpp qt/src/audio_preview_controller.cpp qt/src/audio_editor/audio_editor_controller.hpp qt/src/audio_editor/audio_editor_controller.cpp qt/CMakeLists.txt tests/CMakeLists.txt app/main.cpp core/src/metadata_writer.hpp core/src/metadata_writer.cpp qt/src/metadata_editor.cpp app/qml/AgPlayer/AudioToolsWindow.qml app/qml/AgPlayer/components/tools/ToolSidebar.qml app/qml/AgPlayer/components/tools/AudioEditorPage.qml app/qml/AgPlayer/components/tools/VocalSeparationPage.qml app/qml/AgPlayer/components/tools/FormatConvertPage.qml app/qml/AgPlayer/components/tools/MetadataEditPage.qml app/qml/AgPlayer/components/tools/FilenameProcessPage.qml tests/core/metadata_writer_test.cpp tests/qt/audio_preview_focus_controller_test.cpp tests/qt/audio_preview_controller_test.cpp tests/qt/audio_tools_end_to_end_test.cpp tests/qml/tst_audio_tools.qml tests/qml/tst_metadata_editor.qml tests/scripts/audio_tools_layout_contract_test.ps1 docs/development/2026-09-01-unified-player-tools-quality.md
git commit -m "fix(tools): repair metadata writes and arbitrate previews"
```

### Task 9: Full integration, visual acceptance, performance review, and cleanup

**Files:**
- Modify: `docs/development/2026-09-01-unified-player-tools-quality.md`
- Modify only if evidence proves necessary: in-scope files changed by Tasks 1-8

**Interfaces:**
- Consumes every task's focused test contract and evidence.
- Produces no new feature interface.

- [ ] **Step 1: Rebase the requirement ledger against IDs 1-21**

Mark IDs 1-7 and 9-21 with code commit, test command, screenshot/performance
artifact, and result. Mark ID 8 exactly as `external dedicated session — no
completion claim in this session`.

- [ ] **Step 2: Run static and complete automated validation**

```powershell
cmake --fresh --preset windows-msvc-release
cmake --build --preset windows-msvc-release --parallel
cmake --build build/release --target all_qmllint
ctest --test-dir build/release --output-on-failure
git diff --check
```

Any failure is investigated and fixed at its root; failed tests are not hidden by
raising timeouts unless a direct executable run proves only the outer watchdog is
wrong.

- [ ] **Step 3: Run real interaction and audio validation**

Validate import, playback, seek, theme switching, volume expansion, dual/integrated/
mini/rolling shells, list thumbnails, lyrics failover, editor volume/range/select/
export, model scan/failover, per-stem preview/export, metadata write/readback, and
tool preview arbitration using real fixture files.

- [ ] **Step 4: Capture the complete dark/light visual matrix**

Capture reference-size and minimum-size evidence for startup, dual, integrated,
mini, rolling, settings/color popup, file info, lyrics chrome/text, audio editor
states, separation states, metadata result, tool navigation, and EQ. Compare
against the five approved user references.

- [ ] **Step 5: Perform final diff and performance review**

Inspect duplicated action handlers, theme-specific business logic, dead code,
unused imports, unbounded queues, stale async callbacks, scene-graph rebuilds,
file-handle/backup lifecycle, download trust boundaries, and GUI-thread blocking.
Confirm no immersive-owned file changed:

```powershell
git diff --name-only 67b5650..HEAD | Select-String -Pattern "Immersive|terrain_reactor|\.qsb|\.vert|\.frag"
```

Expected: no output from this session's commits.

- [ ] **Step 6: Commit final evidence and confirm clean worktree**

```powershell
git add docs/development/2026-09-01-unified-player-tools-quality.md
git diff --cached --check
git commit -m "docs: record unified player and tools acceptance"
git status --porcelain
```

Expected: empty worktree status. Report exact commits, tests, screenshots,
performance results, unverified physical-hardware cases, and the external status
of requirement 8. Do not package unless the user requests it after reviewing the
acceptance result.
