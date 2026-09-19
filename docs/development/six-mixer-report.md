# Shared six-track streaming mixer

Scope: the isolated `codex/pc-six-track-editor` worktree only. No packaging,
installer changes, main checkout changes, or source-media edits.

## Implementation

- `TimelineMixer` owns one lazy decoder per fixed track, with per-track sorted
  event cursors and 4096-frame output chunks. It reads only intersecting clips;
  seeks close worker-owned decoders and reposition all six cursors.
- The existing Decoder handles each source's rate/channel conversion. Its seek
  coordinate is explicitly the decoder output/project rate. Absolute source
  boundaries are mapped with the model's integer rational helper.
  FFmpeg's existing mono-to-stereo matrix contributes sqrt(0.5) per channel;
  this is source channel conversion, not track-count normalization.
- Clip gain/envelope/fades, track mute/gain, and preserved legacy master gain
  apply in project frames before summation. The sum is not normalized or divided
  by track count. Non-finite source samples cannot contaminate the DSP input.
- EditorPlaybackStream passes the mixed PCM to one existing session TimePitch
  engine. Finite output samples clamp to [-1, 1] only at the final output stage.
  Invalid final samples become zero.
- DocumentRenderer and TimePitchSession consume the same EditorPlaybackStream,
  including selection bounds, tempo/pitch/keepPitch/formant settings and flush.
  The previous post-TimePitch automation paths are removed.
- WAV rendering writes bounded chunks and fixes the header using the actual
  flushed frame count. Cancellation interrupts decoder I/O and removes partial
  output. Encoding, verification, and atomic commit retain DocumentWriter.
- Explicit project format is retained, including legacy mono. New documents
  supply stereo through the model. Aggregate legacy test snapshots without a
  sample rate preserve their first source's original rate/channel layout.
- DocumentWriter and DocumentRenderPipeline share an original-source overwrite
  guard, evaluated before replacing the request snapshot with processed PCM.
  It covers every clip source and metadata source, including filesystem aliases.

## Verification evidence

- RED: six concurrent real PCM events were rejected by the old playback stream;
  `sixTracksMixRealPcmBeforeSessionEffects` failed with 0 vs 16000 samples.
- Initial GREEN: DocumentRendererTest 12/12, editor_playback_stream_test and
  time_pitch_session_test 2/2. Covers six real PCM tracks and gains/mutes,
  identical preview/offline DSP output, resampled long-tail seeks, exact trim
  offsets, final clipping, automation-before-DSP against a baked PCM fixture,
  flush, missing decoder source, reusable buffers, and existing output ownership.
- Final GREEN: DocumentRendererTest **17/17** (including setup/cleanup), and
  editor_playback_stream_test, document_writer_test, time_pitch_session_test
  **3/3** via CTest. Added cases prove mixed 8k/16k synchronization, repeated
  backward/forward seeks, a 100-hour timeline beyond signed 32-bit frames,
  resampled ramp trim/seek source coordinates, non-finite PCM, and cancellation
  after the first actual rendered chunk.
- Guard RED: the explicit Overwrite mode previously succeeded when the output
  was a second-track original fixture. Final GREEN rejects both second-track and
  metadata-source targets at 100% and 125% speed and preserves their PCM bytes.
- Logs: `build/release/six-mixer-red.txt`, `six-mixer-guard-red.txt`, and
  `six-mixer-final.txt`. CTest command:
  `ctest --test-dir build/release -R '^(editor_playback_stream_test|time_pitch_session_test|document_writer_test)$' --output-on-failure`.

Build note: initial generated MSVC show-includes prefix was mojibake, preventing
header dependency tracking. A stale writer object caused an old-signature link
error; deleting that one generated object and recompiling restored the build.
The parent corrected the generated prefix and rebuilt the affected objects;
final verification used that corrected dependency graph.

Final owned diff was checked. No commit was created, per the parent's updated
integration boundary. WAV intermediates retain the existing RIFF 4-GiB limit;
the mixer itself does not impose a 60-minute timeline limit.

These are core PCM/build results, not physical-device listening, native UI,
recording, full-product acceptance, or macOS validation.
