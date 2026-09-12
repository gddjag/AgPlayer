# AgPlayer UI Design System

## 1. Design direction

AgPlayer uses Codex-style window hierarchy and WeChat-style desktop density:

- native, restrained, quiet, and efficient;
- a lighter navigation rail beside a darker content canvas in dark mode;
- a light-grey navigation rail beside a near-white content canvas in light mode;
- purple emphasis in dark mode and blue emphasis in light mode;
- hierarchy through alignment, spacing, typography, dividers, and tone rather
  than card nesting, gradients, glow, blur, or heavy shadows.

Business layout, playback behaviour, models, and C++ interfaces are not visual
design concerns and must not be changed as part of style-only work.

## 2. Theme and semantic colour roles

All application colours come from `Theme.qml`. Pages must bind to semantic
roles rather than hard-coded values.

| Role | Dark | Light |
| --- | --- | --- |
| `titleBarSurface` | `#202329` | `#E5E8ED` |
| `navigationSurface` | `#24272D` | `#ECEEF2` |
| `contentSurface` | `#181A1D` | `#FAFAFB` |
| `surface` | `#1E2125` | `#FFFFFF` |
| `surfaceElevated` | `#25282E` | `#FFFFFF` |
| `surfaceHover` | `#2B2F35` | `#E4E7EC` |
| `surfacePressed` | `#343941` | `#D9DDE4` |
| `selectedSurface` | `#302A45` | `#E8E8FB` |
| `opaqueBorder` | `#353941` | `#D5D9E0` |
| `accent` | `#7657E8` | `#1F1ED9` |
| `textPrimary` | `#F2F3F5` | `#202328` |
| `textSecondary` | `#B7BBC2` | `#5E646D` |
| `textTertiary` | `#858B95` | `#898F98` |

Success, warning, error, recording, favourite, rating, waveform, spectrum, and
editor colours are domain colours. They remain independent from the brand
accent and require a `theme-color-allow` comment when defined outside Theme.

Selected and playing states may not rely on colour alone. At least one of text
weight, icon, leading marker, label, or shape must reinforce the state.

## 3. Typography

Use the native UI family. Windows explicitly uses Microsoft YaHei UI so Chinese
never falls back to a serif or document font; Segoe UI Variable remains the
Latin fallback. Other platforms keep their application font and native CJK
fallback.

| Token | Size | Use |
| --- | ---: | --- |
| `fontSizeCaption` | 12 | time, waveform ruler, tertiary metadata |
| `fontSizeMeta` | 12 | secondary labels and descriptions |
| `fontSizeBody` | 14 | lists, controls, inputs, menus |
| `fontSizeBodyStrong` | 14 | setting titles and emphasized body text |
| `fontSizeSection` | 16 | section headers |
| `fontSizePageTitle` | 20 | page title |

Normal and Medium are the default weights. DemiBold is reserved for section and
page titles, selected navigation labels, and the current track title. Regular
application pages do not use display typography above 20 px.

User-facing text uses these semantic tokens instead of page-local numeric
sizes. The 12 px caption is the minimum for ordinary labels; smaller text is
limited to non-essential visualization annotations that cannot carry actions or
status by themselves.

## 4. Spacing, radii, and dimensions

The base grid is 4 px. Supported spacing is 4, 8, 12, 16, 24, and 32 px.

| Token | Value | Use |
| --- | ---: | --- |
| `radiusXs` | 4 | checkbox, badge, compact status |
| `radiusSm` | 6 | button, input, selected list row |
| `radiusMd` | 8 | dialog, major panel, window |
| `radiusLg` | 8 | compatibility alias; no oversized cards |
| `titleBarHeight` | 40 | application title bar |
| `navigationWidthCompact` | 192 | narrow navigation |
| `navigationWidth` | 216 | standard navigation |
| `navigationWidthExpanded` | 240 | maximum navigation |
| `playerInspectorWidth` | 280 | integrated tag/lyrics inspector |
| `playerTagPanelWidth` | 264 | rolling tag/lyrics inspector |
| `playerBottomBarHeight` | 80 | integrated playback bar |
| `rollingOverviewHeight` | 128 | rolling overview at standard height |
| `rollingOverviewHeightCompact` | 112 | rolling overview below 700 px |
| `rollingWaveformHeight` | 176 | rolling waveform at standard height |
| `rollingWaveformHeightCompact` | 152 | rolling waveform below 800 px |
| `controlHeightCompact` | 28 | compact/icon controls |
| `controlHeight` | 32 | standard controls and inputs |
| `controlHeightProminent` | 36 | primary actions |
| `navigationRowHeight` | 36 | navigation entry |
| `listRowHeight` | 40 | compact single-line row |
| `mediaListRowHeight` | 48 | track row with cover or two lines |
| `settingsRowHeight` | 48 | settings row |
| `tableHeaderHeight` | 36 | table header |
| `sliderTrackHeight` | 2 | slider visual track |
| `sliderHandleExtent` | 10 | slider visual handle |
| `minimumInteractionExtent` | 28 | minimum pointer target |

Playback transport rings may use the semantic playing/paused colors defined by
`Theme.playRingPlaying` and `Theme.playRingPaused`. These state colors are an
intentional exception to navigation selection colors; they must not be reused
for ordinary selection or page emphasis.

The 10 px slider handle is visual geometry only. Its pointer and keyboard
interaction area remains at least 28 px, so compact appearance must not reduce
usability.

Pills use half-height radius only for tags or short status labels. A page must
not place every section in a bordered rounded card.

## 5. Window and page composition

Major window regions use tonal hierarchy before borders: title bar,
navigation, content, and elevated controls are distinct semantic surfaces.
Adjacent workspace columns use a 1 px divider or 1 px layout gap. They do not
become separately rounded cards. Inner page margins are 8 or 12 px; 16 px is
reserved for page edges and major section separation.

- Integrated player: 192 px navigation below 1300 px, otherwise 216 px; 280 px
  inspector; 80 px bottom bar; 40 px search; 56 px cover; icon and transport
  groups remain vertically centred.
- Rolling player: 192 px navigation; 264 px inspector; 128/112 px overview;
  176/152 px main waveform; 64 px control bar. The fixed playhead is 2 px with
  an accent cap. EQ, waveform mode, theme, immersive, and mini-player actions
  remain available at the 1000 px minimum width.
- Settings: one selected category is shown at a time. Rows are 48 px, titles
  and descriptions share a baseline grid, and trailing controls align to one
  right edge. A switch does not sit in an extra container unless that
  container communicates a separate state.
- Audio tools: the toolbar and page tabs share the 32/36 px control scale.
  Empty workspaces expand, while status, shortcut, summary, and action regions
  use explicit content-driven heights. The audio editor waveform spans the
  full timeline width; no unused track-header rail or transport text labels
  are shown.
- Tables and lists: headers are 36 px; compact rows are 40 px and media rows
  are 48 px. Fixed trailing columns must remain visible at minimum window
  width; the title and waveform columns absorb width changes first.

Text is one line when label and value form a short setting or status pair.
Descriptions, paths, validation help, and error recovery instructions use a
second line. Controls never share a line when doing so forces clipping or
reduces their pointer target below 28 px.

## 6. Component policy

Pages use the shared `Themed*` controls. Extend the shared component instead of
copying its visuals into a page.

Required shared components include button, icon button, text field, combo box,
slider, range slider, switch, checkbox, menu item, list row, panel, dialog,
tooltip, and scroll bar. Page-level specialisation may change content and width,
but not typography, state colours, radii, focus treatment, or base height.

Production pages use semantic typography and shared controls now, not as an
optional later cleanup. A direct Qt Quick Control is accepted only inside a
reviewed domain-specific component whose interaction cannot be represented by
the shared control (for example a waveform scrubber or colour-spectrum slider).
The repository contract freezes those explicit exceptions per file: reducing
an exception cannot create quota elsewhere, and new files start with zero.

## 7. Interaction states

Every interactive component handles Default, Hover, Pressed, Selected, Focus,
Disabled, Loading, and Error where applicable.

- Hover changes tone without changing geometry.
- Pressed uses the pressed semantic surface; no layout jump.
- Keyboard focus uses a 2 px theme accent ring.
- Disabled content remains readable and does not accept input.
- Loading preserves the original control extent and does not accept activation.
- Error explains the cause visibly next to the relevant control; red alone is
  not an explanation.
- Transitions are 120-160 ms and limited to colour, opacity, or small position
  changes. Infinite decorative animation, live blur, and glow are prohibited.

## 8. Page and review contract

New pages must:

1. use Theme semantic tokens and shared controls;
2. provide dark, light, and system-mode screenshots;
3. verify 1280x720, 1440x900, and 1920x1080 at 100%, 125%, and 150% scale;
4. verify keyboard navigation, focus, hover, pressed, selected, disabled, loading,
   empty, and error states as applicable;
5. run `all_qmllint`, focused QML tests, and the relevant functional regression
   tests;
6. preserve idle CPU/GPU behaviour and list virtualisation.

Exceptions must be documented beside the property and covered by a focused
test. Compilation alone is not visual acceptance.

The `ui_design_system_usage_contract_test` is the repository guardrail for
these rules. It checks required hierarchy tokens and real shared-component use
in core surfaces, rejects raw hexadecimal colours and numeric page-local font
sizes in application QML, and blocks growth of the direct-control exception
baseline. A legitimate domain colour or visualization-only font calculation
must carry an inline `theme-color-allow:` or `typography-size-allow:` reason so
that the exception is visible in review. New generic controls belong in
`components/Themed*.qml`; page-local copies are not accepted.

## 9. macOS platform policy (all future updates and additions)

The macOS edition targets macOS 13+ and follows Apple's official Human Interface
Guidelines. This is a standing user requirement, including new features and later
updates. `ui-ux-pro-max` supplements this policy; generic web/mobile presets do
not override Apple's macOS guidance or the approved AgPlayer layout.

- Preserve the existing content layout, navigation and complete Windows feature
  set. Reuse Theme and shared controls; macOS styling must not redesign Windows.
- Use the system application font, semantic light/dark colours, visible keyboard
  focus and accessible labels. Existing 28 pt desktop targets remain the baseline.
- Place close/minimise/full-screen controls at the left. Keep window-specific
  close confirmation and unsaved-work protection. Prefer system APIs for native
  behaviour; label custom QML controls honestly and validate them on a real Mac.
- Provide the system application menu, Settings (Command-comma), Quit (Command-Q),
  Close Window (Command-W) and Minimise (Command-M). Display native shortcut symbols
  while storing portable key sequences. Closing a window and quitting are distinct
  actions; retain the user's configured close behaviour.
- Show environment/model errors and recovery actions inside the affected cards;
  retain one-click setup and official model compatibility links. Do not hide a
  feature because a model or acceleration provider is unavailable.
- Do not require macOS 26 visual APIs or raise the macOS 13 minimum. Avoid new
  runtime dependencies, decorative effects and changes to playback behaviour.
- Review changed surfaces on macOS for focus, pointer hit areas, window actions,
  standard shortcuts and light/dark appearance. Windows previews, lint and CI
  compilation are not physical Mac visual/audio acceptance.

Official references (checked 2026-09-13):
[Designing for macOS](https://developer.apple.com/design/human-interface-guidelines/designing-for-macos/),
[Windows](https://developer.apple.com/design/human-interface-guidelines/windows),
[Keyboards](https://developer.apple.com/design/human-interface-guidelines/keyboards),
[Settings](https://developer.apple.com/design/human-interface-guidelines/settings),
[Accessibility](https://developer.apple.com/design/human-interface-guidelines/accessibility).
