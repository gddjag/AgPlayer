# Task 12: free multi-route lyrics integration

## Scope

Integrated the already-implemented LRCLIB, Unison, and lyrics.ovh providers into
`LyricsService` without changing the provider or chain implementations. The
service now publishes typed source metadata, synchronized/plain-text state,
safe route notices, and one collapsed result per route. `LyricsPanel` renders
the source, scrollable plain text, nonmodal failover feedback, and the final
three-route outcome with shared theme tokens. Chinese and English catalogs cover
all new user-visible text.

## Contract matrix

- A low-confidence LRCLIB search result continues to Unison, whose acceptable
  result retains its source and synchronized-lyrics capability.
- Exact-stage attempts carry into search, lyrics.ovh is collapsed across both
  stages, and a technical result takes precedence over a normal no-match. The
  final UI model contains exactly three safe rows.
- Timed and plain-text cache reloads preserve provider, attribution,
  synchronization state, and the real lyrics content. Instrumental results
  never expose a plain-text body as untimed lyrics.
- Prefetch failures/successes, canceled late results, an old generation, the
  previous track, and an old notice timer cannot mutate the foreground track.
- There is no service-wide blackout: after three technical failures, the fourth
  manual retry issues a new request. The four legacy blackout-oriented test
  names were replaced with retryability-oriented names.
- Child-provider attempts already present in a chain result are retained once,
  without duplication.
- QML tests assert the real attribution string, a long wrapped plain-text body
  with `contentHeight > height` and actual scrolling, nonmodal feedback,
  functional retry/import calls, exactly three attempt delegates with no path
  or query sentinel, and the same panel/token behavior in dark and light modes.

## Crash root cause and regression guard

The earlier `0xc0000005` was a test-fixture defect: empty `TrackRecord.path`
values produced the same persistent-cache key across test cases. A later case
then hit that cache instead of issuing a provider request, leaving the request
list empty; an unguarded `constFirst()` invoked undefined behavior. Every new
network-path fixture now has an isolated `QTemporaryDir` path, and an automated
context audit found all 35 `exactRequests`/`searchRequests`
`constFirst()`/`constLast()` accesses preceded by a size/nonempty assertion
(`UNGUARDED_COUNT=0`). The prefetch test additionally uses the repository's real
WAV fixture because `PlaybackController::restoreQueue()` correctly rejects a
one-byte fake audio file.

No temporary `fprintf`, diagnostic pragma, external probe, or diagnostic-control
instrumentation remains in the owned files.

## Build and verification evidence

All builds used `build/task12-msvc-diagnostic2`, VS2022 Community MSVC 14.38,
and Ninja `-j1`.

- Built `lyrics_service_test`, `qml_mini_player_test`,
  `unison_lyrics_provider_test`, `lyrics_ovh_provider_test`, and
  `lyrics_provider_chain_test`: exit 0.
- Final focused matrix passed 6/6 in 22.78 s:
  `lyrics_service_test` (7.60 s), `unison_lyrics_provider_test`,
  `lyrics_ovh_provider_test`, `lyrics_provider_chain_test`,
  `qml_immersive_integration_test`, and
  `phase6_translation_coverage_test`.
- The high-risk service/chain set (low-match fallback, carried attempts,
  exact/search collapse, child attempts, foreground route failure) passed 100
  consecutive runs: 100 completed, 0 failures, 45.492 s.
- `qml_immersive_integration_test` passed five consecutive complete runs in
  28.53 s.
- `agplayer_app_qml_qmllint` exited 0. It reported only the pre-existing unused
  `AgPlayer` import in unmodified `WaveformSession.qml`.
- `release_translations` generated English and Chinese catalogs with 859
  finished and 0 unfinished entries in each catalog.
- `git diff --check`, conflict-marker scanning, and the added-line scan for
  temporary diagnostic probes all passed. The final scope contains only the
  seven Task 12 files plus this report.
- One expanded matrix run reached the public 10 s CTest budget and terminated
  `lyrics_service_test` at 10.84 s while the machine was under build/test load.
  There was no failed assertion: a direct fresh full run passed in 8.183 s and
  the next identical six-test matrix passed with that test at 7.60 s. The suite
  intentionally includes 4.3 s of real notice-timer ordering. Task 13 can give
  this binary a larger integration timeout without weakening the timing
  contract or shortening the production notice.

## Two-axis diff review and remaining risk

### Standards

The repository has no separate coding-standards file in this worktree, so the
review used existing local style plus the standard code-smell baseline. It
found no hard violation or actionable smell: no added diagnostic probes,
unsafe route fields, service-wide blackout state, unrelated provider/chain
edits, or speculative abstraction. The large test diff is a direct acceptance
matrix rather than production complexity.

### Specification

Every Task 12 requirement is mapped above to deterministic C++ or QML
assertions. No requested behavior was found missing or partial, and the diff
does not extend into live-network or packaging work assigned to Task 13.

Live provider availability, real-network latency, and visual/manual acceptance
remain Task 13 work and are not represented as completed here.
