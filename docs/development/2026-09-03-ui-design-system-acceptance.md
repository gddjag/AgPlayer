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
  panels, tabs, dialogs, tooltips, and scroll bars; standardized existing
  switches, checkboxes, combo boxes, range sliders, and menu items.
- Applied the hierarchy and density contract to the classic, integrated, and
  rolling player shells, the detached track list, settings, and audio tools.
- Added a repository contract that rejects unreviewed raw QML colours and
  verifies required token/shared-component use on core surfaces. It also blocks
  any increase above the reviewed 227-instance legacy direct-control baseline.
- Retained allowlisted media-domain colours for waveform, spectrum, equalizer,
  rating, favourite, and user-editable visual content.

## Visual evidence

The following screenshots were captured from the Release build using the real
Qt application and fixture audio:

- `build/qa/ui-design-system-final/zh-dark-playback.png`
- `build/qa/ui-design-system-final/zh-dark-settings.png`
- `build/qa/ui-design-system-final/zh-dark-list.png`
- `build/qa/ui-design-system-final/zh-dark-tool-1.png`
- `build/qa/ui-design-system-final/zh-light-playback.png`
- `build/qa/ui-design-system-final/zh-light-settings.png`
- `build/qa/ui-design-system-final/zh-light-list.png`
- `build/qa/ui-design-system-final/zh-light-tool-1.png`

The matrix validates image dimensions, transparent outer corners where the
surface contract requires them, and distinct dark/light rendering. Manual
review confirmed the sidebar/content tone separation, compact list rhythm,
small radii, restrained borders, and theme-specific accents.

High-DPI coverage was also captured for playback and settings in dark, light,
and system-following modes at 100%, 125%, and 150% scale. All 18 captures in
`build/qa/ui-design-system-hidpi/` passed their geometry checks; representative
125% and 150% captures were manually reviewed for clipping, text hierarchy,
control proportions, and sidebar/content separation.

## Verification contract

- Build `AgPlayer` and `all_qmllint` in Release.
- Run `qml_design_system_test` and
  `ui_design_system_usage_contract_test` for future UI changes.
- Run the focused shell, list, settings, and audio-tools regression tests.
- Re-run `scripts/qa-final-ui-matrix.ps1` when shared tokens or controls change.
- Treat compilation as necessary but not sufficient; approve UI work only after
  real screenshots are reviewed.

## Verification result

- Release all-target build: passed (420 build steps after the incremental app
  and QML lint build).
- Final Chinese visual matrix: 12 of 12 dark, light, and system-following
  captures passed. The dark/light surfaces were manually reviewed after the
  final shared-control migration.
- High-DPI visual matrix: 18 of 18 captures passed on the complete rerun across
  three themes, three scale factors, and two core surfaces. The first matrix
  attempt encountered one process teardown access violation after the 125%
  light-settings screenshot had already been saved; the exact case and the
  complete matrix both passed when rerun. This remains recorded as a
  non-deterministic QA teardown risk rather than a claimed product fix.
- UI design-system and focused shell regression group: passed.
- Full CTest run: 160 of 162 passed on the first run. The stem preview mixer hit
  its 10-second timeout under full-suite load, then passed independently in
  0.39 seconds. The remaining player-action icon contract is a pre-existing
  baseline mismatch: `lyrics.svg` and its contract script have identical Git
  object IDs at base `636c4d6` and at this branch head.

The unrelated icon contract was not changed as part of the UI remediation.

Packaging, pushing, and merging to the main branch are outside this acceptance
scope.
