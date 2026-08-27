# Task 2 report — local lyrics, cache, synchronization, and LRCLIB

## Scope and commit

- Implementation commit: `ec06aac` — `feat(lyrics): add local cache and LRCLIB service`
- Base supplied for this task: `4bdc121`.
- This report is a dedicated evidence record; it does not alter QML UI, terrain,
  waveform, queue behavior, or playback algorithms.

## RED → GREEN evidence

1. The first RED build was run after registering `lyrics_service_test` and before
   adding its source modules. CMake generation failed with
   `Cannot find source file: src/lyrics_cache.cpp`, proving the intended service
   boundary did not yet exist.
2. After the minimal parser/cache/model/provider/service implementation,
   `lyrics_service_test` passed its initial parser, cache, offset/model,
   exact-to-search, cancellation, NotFound, and technical-failure cases.
3. A second RED test, `onlineResultWritesReusableCacheWithTimedLines`, failed:
   the fetched service had one time line but the cache-reused service had zero.
   Root cause was moving the document lines into the model before atomically
   saving the cache entry. The single fix writes the entry before populating the
   model; the focused suite then passed.
4. The final focused run passed `lyrics_service_test`, `library_model_test`, and
   `playback_controller_test` (3/3). Debug `AgPlayer` built successfully.

## Files and behavior

- `qt/src/lyrics_line_model.*`: QAbstractListModel time-line roles; LRC parsing
  handles UTF-8 BOM/metadata, CRLF, multiple timestamp forms, offset, duplicates,
  untimed text, and malformed lines without assigning a time.
- `qt/src/lyrics_cache.*`: per-track SHA-256 key from normalized path, size,
  modified time, and duration; JSON reads and atomic `QSaveFile` writes under the
  configured cache directory.
- `qt/src/lyrics_provider.*`: one injectable provider seam and the only concrete
  provider, LRCLIB. It uses asynchronous `QNetworkAccessManager`, exact then
  search endpoints, an identifying AgPlayer User-Agent, 15-second timeout,
  cancellation, low priority prefetch requests, and `Retry-After` handling.
- `qt/src/lyrics_service.*`: local ordering (embedded → sidecar → cache → exact
  → search → manual import), synchronized current/previous/next lines and offset,
  five-second follow pause state, candidate compatibility scoring, current + next
  request cap, 20-minute three-error degraded window, and sanitized diagnostics.
- `app/main.cpp`, `qml_registration.*`, CMake, and tests wire the singleton into
  app startup and the focused test target.

## Deliberate choices

- Only LRCLIB is implemented. `LyricsProvider` is a single injectable seam for
  tests/future replacement, not a provider registry, selector, or switching UI.
- Diagnostics contain aggregate provider health only. No local full path, request
  URL, song metadata, or lyric text is logged or returned.
- The cache avoids audio hashing and adds no library-wide scan. Network activity
  starts only while lyrics are enabled; stale requests are canceled on track
  change.

## Validation and remaining limits

- Passed: focused tests above; Debug `AgPlayer` build; `git diff --check` before
  the implementation commit.
- `qml_main_window_test` was rebuilt after stale build artifacts exposed the old
  QML-registration ABI. It then ran but failed at category-scroll and Windows
  native-dialog cases (`qwindowsdialoghelpers.cpp` assertion). Those failures
  were not baseline-confirmed in this worktree. Limited impact analysis found no
  `LyricsService` reference in the tested QML and Task 2 only registers a lazy
  singleton, so no QML source was changed to avoid masking an unproven unrelated
  regression.
- Not verified: live LRCLIB traffic/rate-limit behavior, real audio playback
  synchronization, manual QML lyrics panel (Task 4), platform-specific runtime
  behavior outside Windows, and hardware/network soak testing.
