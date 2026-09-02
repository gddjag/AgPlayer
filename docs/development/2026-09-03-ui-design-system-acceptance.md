# UI Design System Acceptance — 2026-09-03

## Decision

Accepted direction: Codex-style desktop window hierarchy with WeChat-style
list density and efficiency. Dark mode uses purple `#7657E8`; light mode uses
blue `#1F1ED9`. Existing product structure, playback behaviour, data models,
and C++ interfaces remain unchanged.

## Delivered scope

- Added semantic theme tokens for title bar, navigation, content, interaction
  states, typography, spacing, radii, and control dimensions.
- Added shared themed buttons, icon buttons, text fields, sliders, list rows,
  and panels; standardized existing switches, checkboxes, combo boxes, range
  sliders, and menu items.
- Applied the hierarchy and density contract to the classic, integrated, and
  rolling player shells, the detached track list, settings, and audio tools.
- Added a repository contract that rejects unreviewed raw QML colours and
  verifies required token use on core surfaces.
- Retained allowlisted media-domain colours for waveform, spectrum, equalizer,
  rating, favourite, and user-editable visual content.

## Visual evidence

The following screenshots were captured from the Release build using the real
Qt application and fixture audio:

- `build/qa/ui-design-system/zh-dark-playback.png`
- `build/qa/ui-design-system/zh-dark-settings.png`
- `build/qa/ui-design-system/zh-dark-list.png`
- `build/qa/ui-design-system/zh-dark-tool-1.png`
- `build/qa/ui-design-system/zh-light-playback.png`
- `build/qa/ui-design-system/zh-light-settings.png`
- `build/qa/ui-design-system/zh-light-list.png`
- `build/qa/ui-design-system/zh-light-tool-1.png`

The matrix validates image dimensions, transparent outer corners where the
surface contract requires them, and distinct dark/light rendering. Manual
review confirmed the sidebar/content tone separation, compact list rhythm,
small radii, restrained borders, and theme-specific accents.

## Verification contract

- Build `AgPlayer` and `all_qmllint` in Release.
- Run `qml_design_system_test` and
  `ui_design_system_usage_contract_test` for future UI changes.
- Run the focused shell, list, settings, and audio-tools regression tests.
- Re-run `scripts/qa-final-ui-matrix.ps1` when shared tokens or controls change.
- Treat compilation as necessary but not sufficient; approve UI work only after
  real screenshots are reviewed.

Packaging, pushing, and merging to the main branch are outside this acceptance
scope.
