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
  direct-control growth against a per-file legacy baseline, so reductions in
  one old page cannot become quota for another and new files start at zero.
- Retained allowlisted media-domain colours for waveform, spectrum, equalizer,
  rating, favourite, and user-editable visual content.

## Visual evidence

The following screenshots were captured from the Release build using the real
Qt application and fixture audio:

- `build/qa/ui-remediation-final-v4/zh-dark-settings.png`
- `build/qa/ui-remediation-final-v4/zh-light-settings.png`
- `build/qa/ui-remediation-final-v4/zh-dark-tool-1.png`
- `build/qa/ui-remediation-final-v4/zh-light-tool-1.png`
- `build/qa/ui-remediation-final-v4/zh-dark-tool-3.png`
- `build/qa/ui-remediation-final-v4/zh-light-tool-3.png`
- `build/qa/ui-remediation-final-v4/zh-dark-tool-4.png`
- `build/qa/ui-remediation-final-v4/zh-light-tool-4.png`
- `build/qa/ui-remediation-tool0-final/zh-dark-tool-0.png`
- `build/qa/ui-remediation-tool0-final/zh-light-tool-0.png`
- `build/qa/ui-remediation-eq-1080/zh-dark-eq-1080x480.png`
- `build/qa/ui-remediation-eq-1080/zh-light-eq-1080x480.png`

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

- Release all-target build: passed, including `all_qmllint`. The only lint
  diagnostic is the existing informational unused import in
  `WaveformSession.qml`.
- Final remediation matrix: 10 of 10 dark/light settings and audio-tool
  captures passed. Focused audio-editor and 1080x480 equalizer matrices also
  passed and were manually reviewed for clipping, alignment, density, and
  theme-specific contrast.
- High-DPI visual matrix: 18 of 18 captures passed on the complete rerun across
  three themes, three scale factors, and two core surfaces. The first matrix
  attempt encountered one process teardown access violation after the 125%
  light-settings screenshot had already been saved; the exact case and the
  complete matrix both passed when rerun. This remains recorded as a
  non-deterministic QA teardown risk rather than a claimed product fix.
- UI design-system and focused shell/audio-tools regression group: 16 of 17
  passed. The remaining `qml_audio_editor_native_input_test` fails while
  simulating a native waveform double-click; the static/editor layout test and
  all other focused UI tests pass.
- Full CTest run after the final visual changes and contract update: 161 of 162
  passed. The only remaining failure is the same native input simulation. The
  format-converter contract now requires the new independently scrollable,
  compact panel and passes together with its QML test.

The native input simulation is tracked as a separate functional-input risk and
was not masked by changing production waveform behaviour during this visual
remediation.

Packaging, pushing, and merging to the main branch are outside this acceptance
scope.
