# Six-track visible waveform detail

Scope: only the controller's peak helpers, `eventPeaks`, viewport waveform job
request/start/clear, their cache/result fields, and a small constructor timer
connection. Other controller methods remain owned by the parent. QML is owned
by the recording/UI agent; this report does not claim visual acceptance.

## Delivered behavior

- The existing single-flight viewport waveform worker now publishes a local
  peak slice for every visible clip. `eventPeaks` returns that clip's actual
  visible intersection, without global viewport padding. The existing source
  peak pyramids supply an immediate coarse view while detail is pending.
- Source/project conversion uses the model's absolute-boundary rational frame
  helpers. Decoded detail bins map project bucket boundaries into source frame
  ranges, preserving transients across different source sample rates. Gain,
  envelope and fade coordinates remain project-frame coordinates.
- Each clip's read is reused to compose legacy `viewportChannelPeaks`, so there
  is no second decoder worker or complete source analysis for this feature.
- A 250-ms single-shot timer coalesces repeated viewport requests. Current and
  pending jobs keep the existing generation/cancel-token discipline, publish
  only the newest request, and preserve last-good composite peaks on unavailable
  source reads. Decoder I/O observes cancellation.
- Detail decoding remains bounded by the existing medium-zoom density rule and
  is not requested for viewports longer than five minutes. Source overview
  pyramids remain reusable through track mute/gain/move edits. Playhead seeks
  do not schedule waveform analysis.
- Cache publication emits `waveformChanged`; QML must depend on that signal
  when binding an invokable `eventPeaks` result.

## Verification

RED was observed before production edits:

- The existing `eventPeaks` returned one coarse bucket (2 scalar values) where
  the clip's 20 visible project frames required 40 actual min/max values.
- Two decode jobs started within the first 100 ms of rapid viewport changes,
  instead of waiting for the requested debounce interval.

GREEN:

- `six_track_waveform_test`: 3 functional tests passed (5/5 including setup and
  cleanup). Real 16-kHz source PCM is displayed in an 8-kHz project, the clip
  has no leading viewport padding, rapid changes coalesce, playhead seeks do
  not decode, and track edits perform no complete source overview analysis.
- 15 directly affected existing `audio_editor_controller_test` cases passed
  (17/17 including setup and cleanup): live gain gestures, last-good retention,
  unavailable reads, fractional DPR, gain/fades/envelopes, antiphase stereo,
  narrow envelope points, deep and medium zoom, exact source-frame seek,
  deferred analysis, blank gaps/latest requests, single-flight behavior,
  invalidation, and destruction waiting for active work.
- Logs: `build/release/six-waveform-red.txt`, `six-waveform-green.txt`, and
  `six-waveform-existing.txt`.
- Final relevant diff passes `git diff --check`. No commit or package created.

Physical-device listening, native UI screenshots, and macOS behavior were not
tested by this subtask.
