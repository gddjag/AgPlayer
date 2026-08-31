# Task 9 — Free read-only Unison provider

## Scope

- Added `UnisonLyricsProvider` for unauthenticated, read-only `GET /lyrics` and
  `GET /lyrics/search` requests only.
- Added deterministic fake-network tests. No live endpoint, account, API key,
  write endpoint, LyricsService integration, provider chain, QML change, or
  packaging was used.

## TDD evidence

- Registered `unison_lyrics_provider_test` before creating the provider class.
  The VS2022 Community configure completed and the new target's RED build
  stopped at the deliberately absent `unison_lyrics_provider.hpp` include.
- Implemented the smallest provider needed for the tests: official routes and
  metadata query fields, JSON headers and priority, LRC/plain parsing, exact
  Unison attribution, strict invalid-response classification, rate-limit
  handling, timeout/network classification, and cancellation.

## Verification

- VS2022 Community / MSVC configure and focused build:
  `cmake --preset windows-msvc-release`
  `cmake --build --preset windows-msvc-release --target unison_lyrics_provider_test --parallel 4`
  — passed after implementation.
- `ctest --test-dir build/release -R '^(unison_lyrics_provider_test|lyrics_service_test)$' --output-on-failure`
  — 2/2 passed in 1.05 s.
- `git diff --check` — passed before commit.

## Independent-review correction

- Independent review found that replacing an in-flight request with the same
  `requestId` could let the stale reply remove the current reply's map entry.
- Added exact and search regressions that first failed on the stale-reply race,
  then changed replacement to cancel the old request and changed completion to
  compare the mapped reply before removing it.
- Rebuilt `unison_lyrics_provider_test` and `lyrics_service_test`; 2/2 passed in
  1.33 s after the correction, then both passed three consecutive runs (6/6).

## Remaining scope

Provider-chain failover and LyricsService/QML integration remain owned by later
tasks. The provider itself sends only title, artist, optional album, and
duration seconds; it never sends local paths or audio data.
