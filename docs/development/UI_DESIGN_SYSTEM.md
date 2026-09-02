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

Use the native UI family. Windows resolves to Segoe UI Variable where
available, with Microsoft YaHei UI for Chinese. Other platforms keep their
application font and native CJK fallback.

| Token | Size | Use |
| --- | ---: | --- |
| `fontSizeCaption` | 11 | time, waveform ruler, tertiary metadata |
| `fontSizeMeta` | 12 | secondary labels and descriptions |
| `fontSizeBody` | 13 | lists, controls, inputs, menus |
| `fontSizeBodyStrong` | 14 | setting titles and emphasized body text |
| `fontSizeSection` | 16 | section headers |
| `fontSizePageTitle` | 20 | page title |

Normal and Medium are the default weights. DemiBold is reserved for section and
page titles, selected navigation labels, and the current track title. Regular
application pages do not use display typography above 20 px.

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
| `controlHeightCompact` | 28 | compact/icon controls |
| `controlHeight` | 32 | standard controls and inputs |
| `controlHeightProminent` | 36 | primary actions |
| `navigationRowHeight` | 36 | navigation entry |
| `listRowHeight` | 40 | compact single-line row |
| `mediaListRowHeight` | 48 | track row with cover or two lines |
| `settingsRowHeight` | 48 | settings row |
| `tableHeaderHeight` | 36 | table header |
| `sliderTrackHeight` | 3 | slider visual track |
| `sliderHandleExtent` | 12 | slider visual handle |
| `minimumInteractionExtent` | 28 | minimum pointer target |

Pills use half-height radius only for tags or short status labels. A page must
not place every section in a bordered rounded card.

## 5. Component policy

Pages use the shared `Themed*` controls. Extend the shared component instead of
copying its visuals into a page.

Required shared components include button, icon button, text field, combo box,
slider, range slider, switch, checkbox, menu item, list row, panel, dialog,
tooltip, and scroll bar. Page-level specialisation may change content and width,
but not typography, state colours, radii, focus treatment, or base height.

## 6. Interaction states

Every interactive component handles Default, Hover, Pressed, Selected, Focus,
Disabled, Loading, and Error where applicable.

- Hover changes tone without changing geometry.
- Pressed uses the pressed semantic surface; no layout jump.
- Keyboard focus uses a 2 px theme accent ring.
- Disabled content remains readable and does not accept input.
- Loading preserves the original control extent.
- Error explains the cause next to the relevant control; red alone is not an
  explanation.
- Transitions are 120-160 ms and limited to colour, opacity, or small position
  changes. Infinite decorative animation, live blur, and glow are prohibited.

## 7. Page and review contract

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
these rules. It checks the required hierarchy tokens in core surfaces and
rejects raw hexadecimal colours in application QML. A legitimate domain colour
must carry an inline `theme-color-allow:` explanation so that the exception is
visible in review. New generic controls belong in `components/Themed*.qml`;
page-local copies of shared control visuals are not accepted.
