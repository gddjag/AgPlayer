# AgPlayer Reference Regression Repair Implementation Plan

**Spec:** `docs/superpowers/specs/2026-09-01-agplayer-reference-regression-repair-design.md`

## Constraints

- Do not edit immersive-rendering-owned files.
- Preserve shared business controllers and theme tokens.
- Add a failing focused regression check before each behavior fix.
- Use the supplied reference images for visual acceptance.
- Never weaken file/model integrity to make an operation appear successful.

## Task 1: Shared shell controls and shell-specific list presentation

1. Add/extend QML contract tests for classic/startup/integrated/rolling action
   order, icon sizing, popup anchoring, list columns, ten rolling rows, footer
   boundaries, and divider opacity.
2. Keep one shared list delegate/model/actions contract and introduce explicit
   classic versus single-window presentation inputs; remove duplicated shell
   behavior.
3. Recompose each shell's action slots using shared action controls.
4. Import the static Qt-compatible library SVG.
5. Run focused QML/layout tests and capture dark/light reference screenshots.

## Task 2: Waveform policy, geometry, and settings

1. Add renderer tests for solid base color, dark/light spectral contrast, resize
   invalidation, right-edge geometry, rolling density, hover mapping, and
   position-update coalescing.
2. Bind solid and spectral settings to the correct properties and implement the
   shared background-aware unplayed policy.
3. Repair size/source/DPR invalidation and rolling viewport sampling.
4. Compact the palette and color picker.
5. Run focused C++/QML tests plus waveform resize/theme/runtime checks.

## Task 3: Lyrics, file information, EQ, and tool navigation

1. Test provider failover diagnostics, close behavior, portrait file-info bounds,
   EQ size/18-band presence, and the five-page navigation contract.
2. Verify the three free routes against current public endpoints; retain
   embedded/sidecar/cache precedence and replace only demonstrably dead routes
   with legitimate free services.
3. Add the lyrics close action and compact the two dialogs.
4. Make every tool page consume the shared left-aligned navigation component.
5. Run network-isolated provider-chain tests, QML tests, and screenshots.

## Task 4: Audio editor interactions

1. Add native-input tests for upward/downward volume mapping, zoom chrome opacity,
   lower transport bounds, shortcut centering, and panel gaps.
2. Apply the minimum QML/controller fixes without changing edit semantics.
3. Export and probe a real fixture, then run editor unit/QML tests.

## Task 5: Separation discovery, download, waveforms, and export

1. Add local-server and temporary-model-root tests for direct mirror download,
   failure fallback, known/custom/incompatible ONNX detection, waveform loading,
   and direct per-stem export.
2. Implement safe custom-model scanning/probing and correct self/collision-safe
   output behavior.
3. Repair page bindings and compact the start action/model cards.
4. Run installer/controller/QML tests and a manual directory scan.

## Task 6: Metadata verified fallback

1. Add a container fixture that reproduces packet-framing normalization while
   preserving decoded audio, plus corruption/rollback negative cases.
2. Keep strict validation first and add decoded-audio-equivalence fallback only
   for the proven normalization case.
3. Verify tag readback, audio fingerprint, duration/format contract, backup,
   atomic replacement, and original preservation on failure.

## Task 7: Integration and release

1. Run focused tests after every task and review each diff.
2. Run fresh Release configure/build, QML lint/static checks, complete CTest,
   application launch, playback/seek/theme/list/lyrics/editor/separation/metadata
   interaction checks, and dark/light visual comparison.
3. Review for duplicated handlers, dead code, stale async callbacks, unbounded
   work, extra rendering, and accidental immersive-file edits.
4. Commit the complete verified code, confirm `git status --porcelain` is empty,
   run the repository's official Windows packaging process, smoke-test the
   packaged executable, and copy the final installer/EXE to the desktop.
