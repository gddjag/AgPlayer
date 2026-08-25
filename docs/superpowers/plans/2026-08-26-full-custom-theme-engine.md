# AgPlayer Full Custom Theme Engine

## Goal

Replace the user-facing Accent/Highlight configuration with one theme skin color. Default keeps the existing neutral Light/Dark/System appearances, while Preset and Custom colors generate a complete palette from a single seed.

## Global Constraints

- Default keeps the existing neutral surface tokens, uses base Accent `#007AFF`, and uses current-track surface `rgba(#8F57C9, 0.34)` in both Light and Dark.
- Light, Dark, and System appearance modes remain unchanged and independent from the skin color choice.
- Generated skins derive background, surface, elevated, hover, pressed, divider, border, four text levels, disabled, Accent, Highlight, Focus, and current-track surface from one opaque uppercase `#RRGGBB` seed.
- Primary text targets 7:1 contrast; secondary and button text 4.5:1; tertiary, focus, and important borders 3:1.
- Semantic hues remain green/amber/red and only adapt for readability. Waveform, spectrum, editor, CUE, rating, and user tag colors remain independent.
- Use Qt/C++17 and existing QML components only. No new dependency, thread, timer, polling, cache, animation framework, shader, or color library.
- Follow TDD: each production behavior starts with a focused failing test and records RED/GREEN evidence.

## Task 1: Theme Engine and Palette

- Change `ThemeManager::Preferences` to `appearanceMode + skinMode + skinSeed`.
- Add `SkinMode { Default, Generated }` and a separate `currentTrackSurface` palette/token property.
- Preserve current neutral Default backgrounds, restore `#007AFF` default action/selection identity, and expose `rgba(#8F57C9, 0.34)` only for the current-playing row.
- Implement deterministic seed-based Light/Dark tonal palette generation and contrast solving in C++.
- Keep atomic palette replacement, one notification per real change, native `QPalette` synchronization, system event behavior, and semantic/media invariants.
- Rewrite/extend `theme_manager_test` with Default exact regression, 10 presets, extreme seeds, contrast, palette change, semantic invariance, system events, and no-repeat notification cases.

## Task 2: Settings Persistence and Synchronization

- Add `skinColorMode`, `skinPreset`, and `skinCustomColor` properties using the existing QSettings edit transaction.
- Defaults: System appearance, Default skin mode, `#D27722` custom editing value.
- Do not migrate old Accent/Highlight values into skin settings. Stop exposing/loading/saving them, but leave existing on-disk keys untouched.
- Make preview, save, cancel, reset, invalid-value fallback, and startup synchronization apply the complete skin palette.
- Extend `settings_controller_test` and synchronizer coverage; reset must not touch waveform/spectrum settings.

## Task 3: Settings UI, QML Contract, Translations, and QA Inputs

- Remove Accent, Highlight, and follow-Accent rows from Settings.
- Add one accessible `ThemeColorSelector` labelled `Theme skin color`, reusing Default, the existing 10 presets, Custom picker, keyboard behavior, and focus states.
- Map `Theme.currentTrackSurface` to the new C++ token; selected-row Highlight remains separate.
- Update Chinese, English, Thai, and Vietnamese translations.
- Replace Accent/Highlight QA inputs with skin mode/color inputs and update QML/static-color tests to verify the old controls are absent and the full palette updates.

## Task 4: QA, Documentation, Integration, and Release Evidence

- Update traceability and acceptance documentation for full-skin behavior and the Default blue/purple contract.
- Capture and inspect current-run Default and generated-skin screenshots for the settings, player, list, mini-player, menus/popups/tooltips, and audio tools.
- Run Release/Debug focused and full tests, QML lint, static color checks, real WAV playback switching, performance sampling, and diff review.
- Re-scan active session branches, integrate unique non-superseded committed code, then rerun validation.
- Package the verified Windows Release installer and copy the versioned EXE to the desktop with size and SHA-256 evidence.
