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

## Verification

MSVC Release:

```text
cmake --build build/release-msvc --target time_pitch_session_test editor_playback_stream_test
ctest --test-dir build/release-msvc --output-on-failure -R "^(time_pitch_session_test|editor_playback_stream_test)$"
# 2/2 passed (1.11s)
```

MSVC Debug:

```text
cmake --build build/debug-msvc --target time_pitch_session_test editor_playback_stream_test
ctest --test-dir build/debug-msvc --output-on-failure -R "^(time_pitch_session_test|editor_playback_stream_test)$"
# 2/2 passed (11.65s)
```

## Remaining validation and risk

- No real hardware/device latency or listening A/B test was run.  Automated
  checks do not establish subjective artifact, transient, or formant quality.
- `ITimePitchEngine` has no formant setting.  Existing callers keep their
  established `FormantPreserver` path, so the public interface and Controller
  are unchanged; mapping that UI switch directly to Signalsmith would require
  an explicitly approved interface/caller change.
- The fixed 131072-frame ring intentionally reports a diagnostic and stops
  accepting data if a caller supplies more buffered audio than it receives.
  Normal production blocks interleave `put()`/`receive()`; no sample is
  silently discarded.

## Files

- `CMakeLists.txt`
- `core/CMakeLists.txt`
- `core/src/time_pitch_engine.hpp`
- `core/src/time_pitch_engine.cpp`
- `core/src/signalsmith_time_pitch_engine.cpp`
- `tests/core/time_pitch_session_test.cpp`
- `THIRD-PARTY-NOTICES.md`
- `.superpowers/sdd/2026-08-30-audio-editor-acid-editing/task-6-report.md`
