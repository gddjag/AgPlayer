# Player runtime polish acceptance

## Scope

- Regenerate full-track waveform caches at 32,768 analysis points so the rolling 8-beat viewport keeps source detail while zooming.
- Keep endpoint/DPI mapping shared by the main and mini full-track views and invalidate sparse schema-5 caches.
- Allow the integrated/rolling waveform thumbnail column to start playback on double-click.
- Remove tag-capsule shadows and result-track export circles.
- Keep the vocal-separation primary action available to explain missing runtime/model prerequisites.
- Constrain immersive preset cards to the narrow control panel.

## Verification

- Release build: `cmake --build build/release --config Release`.
- Full Release suite: 163/163 tests passed.
- Immersive integration repeat: 20/20 consecutive runs passed.
- Chinese dark/light visual matrix: startup, playback, and mini captures passed (6 captures).
- Rolling-player QA capture completed with the fixed high-density, time-windowed waveform.
- `git diff --check` passed; line-ending notices are repository-normalization warnings only.

## Evidence

Generated evidence is under the ignored build directory:

- `build/qa/player-runtime-polish/zh-dark-playback.png`
- `build/qa/player-runtime-polish/zh-dark-mini.png`
- `build/qa/player-runtime-polish/zh-dark-rolling.png`
- Corresponding light-theme captures and `matrix.csv`

The 2-second sine fixture has nearly constant amplitude, so its waveform appears rectangular; it is used here to verify full-width endpoint coverage rather than musical shape. Source-density and endpoint behavior are covered by the waveform provider/item tests.
