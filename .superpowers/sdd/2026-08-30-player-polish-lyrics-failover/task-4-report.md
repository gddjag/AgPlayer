# Task 4 report

## Delivered

- Added `AudioFileInfoPanel` as the shared 300 px portrait popup used by both `TrackList` and `LibraryManagerPage`.
- Kept the full 13-row information contract, including format, duration, sample rate, bit depth, channels, bit rate, file size, and full path. The visible path remains middle-elided while `Accessible.name` and the copy action expose the complete path.
- Both entry points now open the panel immediately from the real right-click menu and then request missing technical metadata asynchronously. A visible panel refreshes only when `trackDetailsChanged(trackId)` matches the track it is still displaying.
- `LibraryFileOperations::trackDetails()` is now a pure getter. It does not parse media, mutate the library model, emit `dataChanged`, or request a synchronous store flush.
- Added a persisted `metadataProbeAttempted` flag and completed the real channel-count chain through import, explicit refresh, model roles, and JSON save/load. Legacy JSON without the flag loads as `false` and is eligible for one on-demand probe.
- `LibraryModel` is the single in-flight arbiter shared by every `LibraryFileOperations` instance. It grants at most one claim for an unattempted track, records the expected canonical path, and accepts completion only for the same `trackId` and path generation.
- The on-demand probe runs on the Qt thread pool. Completion is queued back to the model thread through a `QPointer<LibraryModel>`. Matching success writes only real probed technical fields; matching failure, including a successful probe reporting zero channels, still persists `metadataProbeAttempted=true` so repeated right-clicks do not re-probe forever. Async completion emits `dataChanged` only and relies on the existing deferred `LibraryStore` save path; it never emits `flushRequested`.
- Removing, replacing, or relocating records clears stale in-flight state. A relocated record resets `metadataProbeAttempted=false`; an old completion cannot overwrite the new path or remove a newer claim.
- Preserved the shared popup's Escape handling, outside-press closing, fixed label column, scrolling, and real path-copy behavior.

## Red evidence

- Raw-row QML assertions failed before the metadata chain was implemented: both real right-click entry tests failed at the missing `sampleRate` value. Evidence: `build/release/task4-raw-red-track.txt` and `build/release/task4-raw-red-library.txt`.
- The second-round C++ tests were added before the async production API. The MSVC build failed at the expected missing contracts: `TrackRecord::metadataProbeAttempted`, `LibraryModel::beginMetadataProbe`, `LibraryModel::completeMetadataProbe`, the injectable `LibraryFileOperations(ProbeFunction, ...)` constructor, and `trackDetailsChanged`.

## Green verification

- MSVC Release build of `library_model_test`, `library_store_test`, `library_file_operations_test`, `import_controller_test`, and `qml_main_window_test`: passed and linked.
- Direct C++ tests:
  - `library_model_test`: 22 passed, 0 failed. Includes stale-path completion rejection and verifies that an old completion cannot steal the new path's claim. Evidence: `build/release/task4-async-library_model_test.txt`.
  - `library_store_test`: 8 passed, 0 failed. Covers `metadataProbeAttempted` JSON round-trip and legacy JSON defaulting to `false`. Evidence: `build/release/task4-async-library_store_test.txt`.
  - `library_file_operations_test`: 7 passed, 0 failed. Covers pure `trackDetails()`, non-blocking slow-probe dispatch, model-level in-flight deduplication across two operations instances, successful hydration, failed hydration, zero-channel hydration, retry suppression, and no `flushRequested`. Evidence: `build/release/task4-async-library_file_operations_test.txt`.
  - `import_controller_test`: 24 passed, 0 failed. Covers successful import marking the metadata probe as attempted. Evidence: `build/release/task4-async-import_controller_test.txt`.
- Real right-click QML entry tests wait for background hydration and then inspect `panel.rows[rowIndex].value` directly, not the rendered `—` fallback:
  - TrackList: 3 passed, 0 failed. Evidence: `build/release/task4-async-green-track.txt`.
  - LibraryManager: 3 passed, 0 failed. Evidence: `build/release/task4-async-green-library.txt`.
- Fresh complete `tst_main_window.qml` direct run: 114 passed, 0 failed, 1 expected offscreen `WM_DROPFILES` skip in 34.22 s. Evidence: `build/release/task4-async-full-main-window.txt`.
- Fresh related CTest command:
  - `ctest --test-dir build/release -R '^(library_model_test|library_store_test|library_file_operations_test|import_controller_test|qml_main_window_test)$' --output-on-failure`
  - Result: 5/5 passed in 61.81 s (`library_model_test` 1.23 s, `library_store_test` 0.85 s, `library_file_operations_test` 1.36 s, `import_controller_test` 29.40 s, `qml_main_window_test` 28.85 s).

## Remaining risk

- The raw non-empty metadata proof is intentionally limited to the generated, successfully probed WAV fixture. Some compressed/container formats legitimately do not expose optional values such as bit depth; the panel continues to render `—` for genuinely unavailable data and does not invent defaults.
- The offscreen Qt platform cannot exercise the native Windows `WM_DROPFILES` message. That route remains platform-gated and must be included in later native acceptance testing.
- The existing `build/release` Ninja state emitted a `premature end of file; recovering` warning while rebuilding, but the requested MSVC targets linked and the fresh direct/CTest runs above exited successfully. A clean release build remains part of the final integration gate.
