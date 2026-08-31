# Task 11: sequential lyrics-provider failover

## Scope

Added `LyricsProviderChain`, a non-owning sequential route orchestrator. It is
not wired into `LyricsService`, QML, translations, packaging, or a live network
path; those remain outside Task 11.

## TDD and verification evidence

- RED (VS2022 Community x64): after registering the target without production
  files, the prescribed build reached the new test and failed with C1083 for
  missing `lyrics_provider_chain.hpp`.
- The first full implementation build succeeded. Its first test run exposed
  two expectations for duplicated attempt prefixes (4 observed versus 2
  expected); the completion path was corrected so it prefixes accumulated
  chain attempts exactly once.
- Final build: `cmake --build --preset windows-msvc-release --target
  lyrics_provider_chain_test --parallel 4` completed successfully.
- Focused matrix passed 4/4: `lyrics_provider_chain_test`,
  `lyrics_ovh_provider_test`, `unison_lyrics_provider_test`, and
  `lyrics_service_test`.
- The Chain test was run three consecutive times with `ctest --repeat
  until-fail:3`; all three passed.

## Implemented contract

- Preserves the caller's Exact/Search stage and track while attempting supplied
  routes in order. A matcher seam can reject a non-empty search result without
  copying LyricsService scoring.
- Records normal no-match routes silently, emits typed `routeFailed` only for
  technical/rate/provider-unavailable routes, and keeps accumulated attempts
  on every terminal result.
- Tracks per-route technical streaks and uses a 600000 ms circuit cooldown;
  rate limits block only their route without increasing the streak. Cooldown
  permits one probe and concurrent requests skip that route until the probe
  completes.
- Uses a unique internal request ID on each provider dispatch and maps it back
  to the caller ID. Cancellation removes Chain state before provider
  cancellation, and late/duplicate/stale results are ignored.

## Cleanup and remaining integration work

- Removed one verified accidental root file named `'-'` (17,733-byte QtTest
  output created by a malformed local output argument). No `.artifacts` or
  unrelated file was touched.
- Task 12 must select and inject this Chain into `LyricsService`; this task
  intentionally leaves service/provider-chain integration unchanged.
