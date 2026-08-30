# Eighteen-control graphic equalizer

## Scope

AgPlayer now exposes 17 fixed graphic-EQ frequency bands plus a separate
preamp control, matching the supplied eighteen-control reference layout.  The
audio-editor and recording modules are intentionally outside this change.

## DSP contract

- Frequencies: 20, 31.5, 50, 80, 125, 200, 315, 500, 800, 1.25k, 2k,
  3.15k, 5k, 8k, 12.5k, 16k and 20k Hz.
- The application defaults each peaking filter to a shared Q of 2.145,
  suitable for the approximately two-thirds-octave spacing used by this
  layout. The lower-level C API intentionally remains configurable.
- Gain range is -12 dB to +12 dB in 0.1 dB UI steps.  Preamp uses the same
  range and auto clip protection remains enabled by default.
- Built-in presets: Flat, Deep Bass, Classical, Pop, Rock, Vocal, EDM and
  Jazz. Each non-flat preset is swept across 20 Hz-20 kHz without automatic
  protection and reserves at least 0.5 dB of preamp headroom after overlapping
  filter responses are combined.

`AG_EQUALIZER_BAND_COUNT` changed from 10 to 17.  The C API and the application
are rebuilt together; downstream binary consumers must rebuild against the new
header.

## Settings migration

Legacy ten-band settings and custom presets are migrated once by logarithmic
frequency interpolation. The saved schema records version 2 and a band count
of 17 so the migration is not repeated. All migrated live curves are labelled
`custom`, including retained identifiers such as `rock`, because interpolation
does not produce the new built-in reference curve. The one exception is a
legacy `flat` state whose preamp and every band are still exactly zero.

## UI and accessibility

- Wide windows use the supplied Apple-style proportions, large native-looking
  slider handles, a response curve, an independent preamp and a spacious
  footer.  The default and minimum windows retain compact controls.
- Narrow windows keep all controls reachable through the existing horizontal
  band scroller.
- Double-click resets a band, mouse wheel and arrow keys fine tune it, and all
  bands expose frequency and gain through accessibility.
- The preset display reads `Custom` after manual tuning.

## Verification

- Core response tests cover every center frequency and the 20 kHz band at
  44.1 kHz sample rate.
- Controller tests cover the eight presets, headroom, response curve, legacy
  settings migration and custom preset persistence.
- QML interaction tests cover 17 bands plus preamp, keyboard, wheel and reset
  interaction. A separately registered visual test renders the wide, default
  and minimum viewports without claiming pixel-diff equivalence.
- Manual visual QA placed the supplied 1675 x 943 reference beside an actual
  Release capture at the same viewport and inspected the combined image. The
  generated comparison artifact is kept outside the source tree so it does not
  add binary weight to the application repository.

The Windows entry point disables only the stale per-user QML disk cache and
continues to use the ahead-of-time compiled QML embedded by `qt_add_qml_module`.
This prevents an upgraded build from rendering an older equalizer control tree.
