# AgPlayer Critical Player and Audio-Tools Repair Design

## Goal

Close the remaining visual and runtime defects reported on 2026-09-03 without
adding another player, waveform analyser, theme system, colour picker, or model
runtime. The supplied screenshots are acceptance references; their red boxes,
arrows, and annotations are not application content.

## Scope and shared contracts

1. The classic player, mini player, integrated player, and rolling player keep
   one `WaveformItem` renderer and one `WaveformSession`. A full-track renderer
   must cover the complete item width, including the final sample bucket.
2. Frequency-colour progress is a smooth clipped overlay: the unplayed pass is
   rendered once at the theme-aware unplayed opacity and the played pass is
   clipped at `waveformCursorX`. It never recolours frequency data.
3. The existing three-band palette remains the only frequency palette: Low
   `#FC0909`, Mid `#03FF00`, High `#0048FF`. Dark and light themes may use
   different presentation opacity floors so both progress contrast and the
   unplayed waveform remain legible.
4. Classic, integrated, and rolling surfaces keep their current models and
   controller interfaces. Shared trailing list columns are fixed; the
   title/metadata and thumbnail columns absorb width changes.
5. Cover-led rows and now-playing headers use the same vertical contract:
   title on the first line, artist/album/tags/rating on the second where the
   surface requires it, metadata on the third, all vertically centred against
   the cover.
6. All colour-edit entry points use the existing native `ColorField` popup.
   Hex text is editable and copy/paste capable. No WebEngine or duplicate
   page-local picker is introduced.
7. The audio editor keeps the existing event/document model. Trimming the left
   boundary rebases the retained audio to timeline zero; trimming the right
   boundary shortens the document end. The zoom rail is a single translucent
   range with no stray handle block.
8. Vocal separation keeps the native controller/worker design. Model discovery
   is recursive under the configured root, creates one catalog row per
   supported model, and records the exact runtime requirement. ONNX models may
   not be shown as runnable when the ONNX runtime is unavailable. GPU automatic
   selection is capability based and falls back to CPU with an explicit reason.
9. Main playback and every audio-tool preview share an exclusive playback
   ownership rule: starting one stops the other before audio begins.

## Acceptance requirements

- `P01`: classic and mini full-track waveforms reach both visual edges and do
  not drop the tail after resize or theme changes.
- `P02`: frequency-colour played/unplayed regions are visibly distinct in all
  dark themes and the unplayed region remains readable in light mode.
- `P03`: classic control spacing, title-bar tone, lyrics docking, and context
  menu sizing match the supplied references at the normal and minimum sizes.
- `P04`: all cover-led headers and rows use the shared vertical alignment and
  reduced secondary text size.
- `P05`: integrated and rolling lists use the same column/layout contract;
  trailing duration/rating/favourite columns remain stable while the flexible
  content grows. Shared filter controls are vertically centred.
- `P06`: rolling mode shows an unscaled fixed-pixel eight-beat viewport; the
  centre guide is the seek target and the current-time capsule is aligned with
  it. Overview progress and hover mapping use the same coordinate mapper.
- `P07`: rolling header order, compact height, darker viewport, equal gaps,
  single BPM value, and enlarged reset/step icons match the screenshot intent.
- `P08`: the native colour popup fits its content at supported scale factors,
  accepts pasted hex values, and is reused by settings and tag colour editing.
- `P09`: tag count segments size to their digits with compact horizontal
  padding and no light-theme drop shadow.
- `P10`: editor trim/zoom behaviour and theme typography match the stated
  contract.
- `P11`: recursive model detection finds supported files at arbitrary depth,
  adds all valid models, reports runtime/GPU capability honestly, and can start
  a supported installed model. Download controls stay in their cards and
  include retry; source/result waveforms are restored.
- `P12`: metadata update uses the existing staged/atomic replacement path and
  verifies the intended tag values and decodable audio stream. Verification
  failure preserves the original and reports the exact failed stage; “force”
  never means corrupting or silently replacing the source.
- `P13`: player and tool preview playback are mutually exclusive.

## Verification

- Add RED/GREEN C++ or QML tests for every changed behavioural contract.
- Run `AgPlayer`, `all_qmllint`, focused waveform/player/list/editor/separation/
  metadata tests, then the full Release CTest suite.
- Capture dark and light screenshots for classic, mini, integrated, rolling,
  settings colour picker, editor, and separation surfaces.
- Compare screenshots at the same viewport/state and keep `design-qa.md` at
  `final result: blocked` until all P0/P1/P2 mismatches in this scope are fixed.

