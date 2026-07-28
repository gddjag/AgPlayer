# AgPlayer Phase 7 — Reliability, Performance, Security

**Status:** Completed 2026-07-29

**Goal:** Prove the current implementation remains correct under Release
optimization, large libraries, repeated production startup/shutdown, and local
security/privacy constraints; remove only code proven unused or duplicated.

**Constraints:** Add no dependency, retain the C ABI boundary, keep all audio
processing local, preserve the four languages and both themes, and do not
package an EXE.

## Task 1 — Release build and regression

- Configure a fresh Ninja Release tree with the existing Qt/vcpkg toolchain.
- Build with the existing `/W4 /WX` gate.
- Run all 44 tests in Release and fix every failure.

## Task 2 — Full scale and runtime reliability

- Run the native waveform/cache stress test with 10,000 tracks.
- Run the 10,000-row library search/sort test in Release.
- Repeat the production executable smoke across startup, playback, screenshot,
  and ordered shutdown; require empty warning/error logs.
- Record elapsed time, cache size, and peak working set.

## Task 3 — Security and privacy audit

- Verify there is no unintended network path, embedded credential, shell
  execution, or unsafe output overwrite.
- Verify user-selected paths are validated and asynchronous controllers cancel
  and join before destruction.
- Confirm no feedback placeholder, private mailbox, or hidden recipient is
  exposed.

## Task 4 — Lightweight code audit

- Use the code graph to inspect high-complexity/hot paths and unused symbols.
- Delete only code proven unreachable from C++, QML resources, and tests.
- Avoid architectural refactors without a measured correctness or performance
  benefit.

## Task 5 — Final gate

- Re-run Debug and Release builds/tests, production smoke, translation checks,
  and `git diff --check`.
- Update phase evidence and commit independently.

## Evidence

- Debug and Release both build cleanly with `/W4 /WX`; CTest is 44/44 in
  45.80 s and 24.35 s respectively.
- The full native 10,000-track run analyzed 10,000/10,000 files with zero
  failures in 307.409 s, used 8.47 MiB of waveform cache, and peaked at
  15.78 MiB working set.
- Release seek benchmark: 100/100 successful, 0.0179 ms median and 0.0319 ms
  P95 against the 20 ms gate.
- Production startup/playback/screenshot/ordered-shutdown smoke passed six
  consecutive Release runs; the final screenshot was 45,152 bytes.
- Removed the fixed-success update placeholder so the offline application does
  not claim a network check that never occurred.
- Metadata batch rename now operates on a worker-owned snapshot, publishes
  results on the UI thread, rejects clear while busy, and reports collision
  suffixes accurately. It sanitizes filenames, blocks directory traversal, and
  uses a dedicated content-change signal that preserves the user's selection.
  Regression tests failed before these fixes and pass after them.
- No unintended HTTP client, credential, process/shell execution, background
  telemetry, or private mailbox string was found. The only external URL is the
  explicit user-triggered official-site action required by Settings.
- All four catalogs contain 462 finished entries and zero unfinished entries.
- Independent review reports no remaining Critical or Important finding.
