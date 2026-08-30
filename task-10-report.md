# Task 10: lyrics.ovh fallback provider

## Scope

Added a free, unauthenticated, read-only `LyricsOvhProvider`.  It uses only
`GET https://api.lyrics.ovh/v1/{artist}/{title}` and is not wired into the
provider chain or QML; those are Task 11/12 responsibilities.

## TDD evidence

- RED (VS2022 Community x64): after registering the test target and before
  adding production files, `cmake --build build/release --target
  lyrics_ovh_provider_test` failed with `C1083: cannot open include file:
  'lyrics_ovh_provider.hpp'`.
- GREEN: the same target built after the minimal provider implementation.
- Focused regression run:
  `ctest --test-dir build/release -R
  '^(lyrics_ovh_provider_test|unison_lyrics_provider_test|lyrics_service_test)$'
  --output-on-failure` passed 3/3.
- Translation coverage: `phase6_translation_coverage_test` passed 1/1.  This
  provider has no user-facing strings, so no translation catalog changed.

## Implemented contract

- Artist and title are encoded independently as UTF-8 path segments; `/` is
  encoded, and the URL is constructed from encoded bytes so `%` is not encoded
  twice.
- Both exact and search requests call the one lyrics.ovh endpoint. Search
  returns its single plain-text result as one `SearchResults` candidate.
- The only accepted successful payload shape is an object with a non-blank
  string `lyrics` value. It produces plain lyrics only, with no inferred timing
  and source `lyrics-ovh` / `lyrics.ovh` / `https://api.lyrics.ovh`.
- Empty local artist/title, HTTP 400/404, and blank successful lyrics return
  `NotFound`; 429 recognizes integer and HTTP-date `Retry-After` with a
  defensive 20-minute fallback; malformed JSON/schema, 5xx, timeout and
  network errors are technical failures.
- Cancellation, timeout, late fake completions and duplicate request IDs are
  isolated by checking that the finishing reply still owns the request ID
  before removing its mapping. A stale reply therefore cannot remove a newer
  request's mapping or emit a result.

## Remaining integration risks

- `LyricsService` currently does exact followed by search after `NotFound`.
  For this provider those requests are the same endpoint, so a 404 can repeat;
  Task 11/12 must prevent misleading failure accounting.
- The tests are deterministic fake-network tests only. No live lyrics.ovh
  request was made, so production availability, response latency and remote
  service behavior remain external risks.
