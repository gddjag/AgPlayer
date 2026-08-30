# AgPlayer Custom Theme Colors Implementation Plan

> Approved by the user on 2026-08-25. Source requirements: `C:/Users/Administrator/Desktop/AgPlayer 自定义主题颜色系统开发提示词｜跨平台原生自适应版.md`.

## Global constraints

- Work only on `codex/custom-theme-colors`, based on `codex/recover-complete-release@9868504`.
- Do not touch the dirty `recover-complete-release` worktree or mix unrelated audio-editor changes.
- Reuse the native picker from `codex/ag-color-picker@124d14a`.
- Do not add theme libraries, platform abstraction layers, polling, timers, threads, shaders, global color animations, or third-party color code.
- Preserve waveform, spectrum, RGB frequency, editor waveform/selection/marker/beat-grid/playhead, equalizer curve, CUE, rating, format badge, and user tag colors.
- Use test-first development. Each production change must be preceded by a focused failing test or contract check.
- Do not package, push, or merge. Commit only theme-related code, tests, translations, and acceptance evidence.

### User amendment — 2026-08-25

After the theme-specific tasks above are complete and reviewed, audit and integrate the latest **committed** work from other active session branches, resolve conflicts by feature ownership, rerun integrated verification, and package the resulting latest Windows EXE to the desktop. This later instruction supersedes the no-merge/no-package restriction only for the post-theme delivery stage; it does not authorize mixing uncommitted worktree changes, pushing remotes, or packaging before integrated verification.

## Task 1: ThemeManager and palette algorithm

Create `ThemeManager` and an internal complete palette value. Construct and calculate it before QML/window creation. Expose neutral surfaces, four text levels, border/divider/disabled tokens, accent/highlight state tokens, focus, and independent semantic colors. Implement stable preset IDs and exact seeds, default `#D27722`, sRGB/HSL derivation, WCAG relative luminance/contrast, foreground selection, and neutral seed preservation. React to `QStyleHints::colorSchemeChanged`; for `Unknown`, fall back to `ApplicationPaletteChange` and window-color luminance. Replace and notify only when the whole palette changes; update the Qt application palette from the same source. Add table-driven `theme_manager_test` for presets, extreme seeds, contrast, semantic independence, follow/independent highlight, system events, de-duplicated notification, and no timer activity.

## Task 2: Settings persistence and transactions

Extend `SettingsController` with `accentMode`, `accentPreset`, `accentCustomColor`, `highlightFollowAccent`, `highlightMode`, `highlightPreset`, and `highlightCustomColor`. Keep appearance values `Dark=0`, `Light=1`, `System=2`, change missing/default appearance to System, validate stable preset IDs and uppercase opaque `#RRGGBB`, preserve legacy `appearance/themeMode`, and safely fall back on invalid values. Integrate all fields with existing begin/commit/cancel/reset behavior and ensure reset never changes waveform or spectrum settings. Extend `settings_controller_test` first for defaults, persistence, legacy/invalid migration, live edit, commit/cancel/reset, and media-setting independence.

## Task 3: Native picker and Appearance settings UI

Port `AgColorPicker.qml`, `ColorScale.js`, `ColorField.qml`, and `tst_color_picker.qml` from `codex/ag-color-picker@124d14a`. Add compact Accent and Highlight selectors to the existing Appearance card: Default, ten circular preset swatches, and Custom. Show selection with outline, check, and slight sizing while preserving swatch color. When follow-accent is on, disable independent Highlight choices and restore the last independent choice when turned off. Preserve keyboard focus, Enter/Space, accessible names, and immediate preview within the existing settings transaction. Add English, Chinese, Thai, and Vietnamese strings. Add QML tests first for preset/custom selection, keyboard activation, preview, cancel/commit, blue accent plus purple highlight, and follow-accent restoration.

## Task 4: QML singleton integration and token migration

Register `ThemeManager` as the global QML singleton before loading the engine. Reduce `Theme.qml` to a color-token proxy plus existing sizing/font/media constants, retaining compatibility aliases such as `panel`, `elevated`, `primaryText`, and `cyan`. Delete duplicate palette decisions in `main.cpp` and remove delayed `Component.onCompleted` theme synchronization in `Main.qml`. Migrate controls by meaning: Accent for active/action/focus/link states; Highlight for list/menu/ComboBox/tree/tag selection; neutral `surfaceHover` for ordinary hover. Split favorite/error/danger/recording/success/ratingGold. Do not globally replace hex values. Add or update focused tests and a static color-classification test before migration changes.

## Task 5: QA automation, visual audit, documentation, and commits

Extend QA parameters for accent/highlight/follow state and cover Light, Dark, System, presets, black/white/yellow key cases, multilingual settings, and representative windows/controls. Record requirement-to-evidence traceability under `docs/development/` and visual/playback/performance/platform status under `docs/qa/`. Use Product Design on real application screenshots to review hierarchy, spacing, selection, focus, and cross-window consistency. Run Debug/Release builds, focused and full CTest, QML lint, real WAV playback theme switching, UI matrix, static color classification, `git diff --check`, and final diff review. Record baseline failures honestly and mark macOS/Linux real-device acceptance unexecuted when hosts are unavailable. Commit as reviewable theme-only commits and report final HEAD.
