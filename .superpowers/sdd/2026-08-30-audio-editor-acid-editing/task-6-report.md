# Task 6 — Signalsmith Stretch engine report

## Scope

- Added pinned, SHA-256-verified CMake `FetchContent` sources for Signalsmith
  Stretch 1.3.2 (`57b93f4e9206a089a45387eaa39bdc9f310d3308`) and Signalsmith
  Linear 0.3.1.
- Added the header-only Signalsmith adapter behind the existing
  `ITimePitchEngine` boundary.  The normal factory prefers it and only selects
  the explicit SoundTouch factory if Signalsmith configuration fails; that
  fallback writes one diagnostic message per process.
- The adapter has an exact 1x/0-cent direct ring-buffer path.  Its processed
  path uses a 131072-frame fixed output ring and 8192-frame planar work
  buffers allocated during `configure()`, with Signalsmith pre-roll,
  input/output-latency compensation, accumulated frame-ratio rounding, and
  end-of-stream silence plus `flush()` tail handling.
- Added the MIT notices.  Neither dependency adds a runtime DLL.

## TDD evidence

RED command (MSVC Release):

```text
ctest --test-dir build/release-msvc --output-on-failure -R ^time_pitch_session_test$
```

Before implementation it failed as expected against the SoundTouch default:

```text
neutral realtime path withheld the first PCM block
```

The public behavior test does not inspect an engine name.  It covers exact
neutral passthrough, 0.75x/1.5x duration, +/-12 semitone frequency, finite and
bounded samples, and 257-frame realtime versus one-block processing parity.
It also covers reset isolation: a short silent stream after a processed stream
must not expose stale pre-roll samples.  That test was added during final
review, failed first with `reset leaked previous input into a new stream`, and
passed after `reset()` began clearing the preallocated pre-roll buffer.

## Review fix round 1

The review identified that a .25 effective playback rate could ask the fixed
8192-frame planar buffer for more than 8192 output frames, and that the old
offline caller supplied an entire render before receiving any output.

Additional RED evidence:

```text
ctest --test-dir build/release-msvc --output-on-failure -R ^time_pitch_session_test$
# failed: Signalsmith processing block exceeded fixed capacity
# failed: quarter-speed flush returned no output for a short stream

ctest --test-dir build/release-msvc --output-on-failure -R ^pitch_shifter_test$
# failed: 10-second, 48kHz offline render decoded to a frame count outside
# the expected 960000 +/- 4096 range
```

The former test uses 5000 and 8192 input frames with tempo 0.5 and rate 0.5;
it verifies non-empty output and approximately 4x duration.  It also now
compares aligned realtime/offline waveforms by correlation, not length alone.
The long offline test generates a 10-second 48kHz WAV, calls the real
`pitch_shift()` path, reopens the export, and verifies its approximately 20
second duration.

The unified engine boundary now exposes `setFormantPreservation(bool)`.
Signalsmith maps it to `setFormantFactor(1.0F, enabled)`; the SoundTouch
fallback applies the existing `FormantPreserver` on receive.  Realtime and
offline callers pass their existing formant options through that boundary and
do not apply a second output-stage formant pass.

## Review fix round 2

The bounded Signalsmith FIFO could report an error internally, but the old
boundary left that state private.  A caller could then mistake a zero-frame
`receive()` for normal latency or end-of-stream.

Additional RED evidence (MSVC Release build):

```text
cmake --build build/release-msvc --target time_pitch_session_test
# failed: time_pitch_session_test.cpp(221): error C2039: "failed" is not a
# member of agplayer::ITimePitchEngine
```

The new production test configures the real Signalsmith engine, sends one
131073-frame passthrough block without receiving, and requires that the
bounded-FIFO failure is observable.  `ITimePitchEngine::failed() noexcept`
now exposes that state: Signalsmith reports its existing failure flag,
SoundTouch reports false, and the preferred wrapper delegates to the active
engine.  `pitch_shift()` checks after every put, receive, and flush boundary;
it returns `AG_INTERNAL_ERROR` with a specific error message if the engine
fails.  `EditorPlaybackStream::read()` checks the same boundaries and returns
`AG_INTERNAL_ERROR`, never converting an engine failure into EOS.  The FIFO
contract is documented at the interface: callers must interleave put/receive.

## Verification

MSVC Release:

```text
cmake --build build/release-msvc --target time_pitch_session_test editor_playback_stream_test pitch_shifter_test
ctest --test-dir build/release-msvc --output-on-failure -R "^(time_pitch_session_test|editor_playback_stream_test|pitch_shifter_test)$"
# 3/3 passed (1.47s)
```

MSVC Debug:

```text
cmake --build build/debug-msvc --target time_pitch_session_test editor_playback_stream_test pitch_shifter_test
ctest --test-dir build/debug-msvc --output-on-failure -R "^(time_pitch_session_test|editor_playback_stream_test|pitch_shifter_test)$"
# 3/3 passed (14.96s)
```

Review round 2 re-verification (MSVC Release):

```text
cmake --build build/release-msvc --target time_pitch_session_test editor_playback_stream_test pitch_shifter_test
ctest --test-dir build/release-msvc --output-on-failure -R "^(time_pitch_session_test|editor_playback_stream_test|pitch_shifter_test)$"
# 3/3 passed (4.02s)
```

MSVC Debug:

```text
cmake --build build/debug-msvc --target time_pitch_session_test editor_playback_stream_test pitch_shifter_test
ctest --test-dir build/debug-msvc --output-on-failure -R "^(time_pitch_session_test|editor_playback_stream_test|pitch_shifter_test)$"
# 3/3 passed (27.28s)
```

## Remaining validation and risk

- No real hardware/device latency or listening A/B test was run.  Automated
  checks do not establish subjective artifact, transient, or formant quality.
- Formant preservation now follows the unified engine setting.  Automated
  A/B coverage proves finite, differing output, but it does not establish
  subjective quality for Signalsmith or the SoundTouch fallback.
- The fixed 131072-frame ring rejects a caller that does not interleave
  `put()`/`receive()`; the failure is now explicit and propagated as
  `AG_INTERNAL_ERROR` by offline and realtime production callers.  No sample
  is silently discarded.

## Files

- `CMakeLists.txt`
- `core/CMakeLists.txt`
- `core/src/time_pitch_engine.hpp`
- `core/src/time_pitch_engine.cpp`
- `core/src/signalsmith_time_pitch_engine.cpp`
- `core/src/pitch_shifter.cpp`
- `core/src/audio_editor/editor_playback_stream.cpp`
- `tests/core/time_pitch_session_test.cpp`
- `tests/core/pitch_shifter_test.cpp`
- `THIRD-PARTY-NOTICES.md`
- `.superpowers/sdd/2026-08-30-audio-editor-acid-editing/task-6-report.md`
