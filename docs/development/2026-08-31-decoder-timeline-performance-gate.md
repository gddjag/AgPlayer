# Decoder timeline performance gate — 2026-08-31

## Scope

This record isolates the ordinary waveform-analysis performance regression
between `f0344c9` and the rolling-player integration worktree.  It does not
cover frequency-colour rendering, QML, or audio-engine changes.

## Root cause

`Decoder::Impl::open()` gained an unconditional `set_output_timeline()` call.
That call derives duration, timestamp quantisation, and leading padding from
the stream even for `DecoderDownmix::Preserve`.  The ordinary waveform
analyser opens a preserving decoder twice and only consumes PCM for counting
and bucketing; it never reads those timeline fields.  In contrast, the
frequency-colour analyser opens `AnalysisMono` and requires all timeline
fields for `FrequencyTimelineCursor` and its accumulator.

## Change

Only `AnalysisMono` opens now call `set_output_timeline()`.  Preserve opens
still initialise the resampler and populate sample rate/channels, but leave
`has_timeline` false.  This keeps the frequency analyser's contract while
removing unnecessary open-time work from ordinary waveform, playback, and
scratch-backfill decoder opens.  No dependency was added.

## TDD evidence

Added a `decoder_test` assertion that a preserving decoder has no timeline.
Before the production change, Release CTest failed at that assertion with
`0xc0000409`; after the change it passed.

The test also resets and reads a thread-local, test-only timeline-derivation
counter.  Two Preserve opens must leave it at zero; the first AnalysisMono
open must raise it to one.  This proves the avoided work directly instead of
inferring it only from the output-format state.

## Validation

Release CTest command:

```powershell
ctest --test-dir build\task3b1-release -C Release -R "^(decoder_test|waveform_analyzer_test|frequency_color_waveform_analyzer_test|audio_engine_test|scratch_audio_engine_test|playback_time_pitch_test)$" --output-on-failure -j 1
```

Earlier rolling-player regression matrix: 6/6 passed in 18.94 seconds
(decoder, ordinary waveform, frequency waveform, audio engine, Scratch audio
engine, and playback time/pitch).  After adding the derivation seam, the
focused decoder/ordinary-waveform/frequency-waveform matrix passed 3/3 in
3.61 seconds.

The cross-task isolated high-priority comparison had previously measured a
4.774770349% hot ordinary-waveform slowdown (MAD 0.434564%).  A post-change
number is intentionally not recorded here: the checked-out baseline probe and
the main-worktree probe use different process-priority setup, so comparing
their raw medians would be invalid.  Re-run the existing isolated 3% gate with
the same high-priority probe on both revisions before accepting the performance
claim.

## Remaining validation

- Re-run the paired high-priority baseline/candidate gate after this decoder
  change is included in the candidate revision.
- Real-device playback, Scratch latency, and package-size checks remain owned
  by their respective rolling-player acceptance tracks.
