# Task 8 Report — Typed Lyrics Source, Attempts, and Cache v2

## Scope

- Base: `df2b7e3161c5c61f637a48e7fa8f4a5ab79ea1f5` on
  `codex/player-polish-lyrics`.
- Worktree: `D:\ai\AgPlayer\.worktrees\player-polish-lyrics-20260830`.
- Production changes are limited to the provider/cache types and the mechanical
  `LyricsService` migration authorized by the Task 8 brief.
- No provider chain, QML properties, notifications, packaging, or live network
  calls were added.

## Implementation

- Added the exact `LyricsProvider::Source` and `RouteAttempt` value types,
  `Candidate::source`, and `Result::attempts`.
- LRCLIB parsing now assigns a stable source at candidate construction:
  `lrclib`, `LRCLIB`, `https://lrclib.net`, empty attribution, and synced-lyrics
  capability enabled.
- Changed `LyricsCache::Entry` to carry `document + Source + instrumental +
  synchronized`.
- New cache writes use JSON version 2 and persist provider ID/name, source URL,
  attribution, provider synced capability, actual synchronization, and the
  existing lyric document/instrumental data.
- Version 2 reads reject wrong types, empty provider identity, invalid or
  relative remote URLs, empty URLs for non-local sources, malformed document
  shapes, and synchronization metadata that contradicts the parsed timeline.
- Version 1 `manual` and `lrclib` entries remain readable. They map to full
  Sources and infer actual synchronization from timed lines. Loading does not
  scan, delete, or eagerly rewrite legacy cache files.
- `LyricsService` supplies typed embedded/sidecar/manual Sources and passes the
  provider candidate Source through network apply, prefetch, and cache paths.
  Its existing string diagnostic stores the resulting `providerId`; it no
  longer replaces network sources with a hard-coded `lrclib` value.
- Provider capability (`supportsSyncedLyrics`) remains independent from the
  actual cached result (`synchronized`), including instrumental results.

## TDD evidence

Baseline, before test changes:

```text
lyrics_service_test: 1/1 passed, 0 failed
```

The tests were changed first. The expected RED command was:

```powershell
cmake --build --preset windows-msvc-release --target lyrics_service_test --parallel 4
```

It exited 1 under the matching VS2022 Community developer environment. The
first contract failure was:

```text
lyrics_service_test.cpp(27): error C2039: "Source" is not a member of "LyricsProvider"
```

The same compilation also reported the expected missing `RouteAttempt`,
`Candidate::source`, `Result::attempts`, and `LyricsCache::Entry::synchronized`
members. Production code was added only after this RED was observed.

The resulting tests cover:

- Unison-shaped v2 round-trip with the exact required attribution;
- every written v2 source/synchronization metadata field;
- hand-written legacy v1 manual and LRCLIB fixtures;
- malformed v2 type, identity, URL, document-shape, and save validation;
- stable LRCLIB candidate source metadata;
- provider-source passthrough into reusable cache;
- embedded, sidecar, cache-hit, and instrumental regressions.

## Verification

All build commands used:

```text
C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat
```

Fresh Task 8 target build:

```powershell
cmake --build --preset windows-msvc-release --target lyrics_service_test --parallel 4
```

Result: exit 0.

Focused CTest:

```powershell
ctest --test-dir build/release -R '^lyrics_service_test$' --output-on-failure
```

Result: 1/1 passed, 0 failed.

Related cache and translation/static regression selection:

```powershell
ctest --test-dir build/release -R '^(lyrics_service_test|waveform_cache_test|translation_manager_test|translation_catalog_test|phase6_translation_coverage_test)$' --output-on-failure
```

Result: 5/5 passed, 0 failed.

`git diff --check` reported no whitespace errors. Diff review found no changes
outside Task 8 ownership and no early Task 9–12 behavior.

## Environment note

The existing release CMake cache points to the VS2022 Community 14.38 compiler.
Using the newer Build Tools developer shell mixes compiler 14.38 with STL 14.44
and produces `STL1001`; all authoritative build and test evidence above uses the
matching Community developer shell. CTest supplies the required Qt/vcpkg runtime
PATH for the test executable.
