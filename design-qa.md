# Design QA — Tag management reference UI — 2026-08-22

## Source visual truth

- Product source: `C:/Users/Administrator/Desktop/音视频播放器/AgPlayer音频播放器完整版/标签管理.png`.
- Repository copy: `design-qa/source-tag-management-1447x1087.png` (1447×1087).
- Required state: dark theme, Tag Management selected, populated navigation/tree, shared track list, populated three-column tag panel, player loaded and playing.

## Current implementation evidence

- Real Debug AgPlayer executable: `build/msvc-debug/app/AgPlayer.exe`.
- Safe state: `--qa-test-mode` with an isolated library/settings/cache root under `build/qa/task7-20260822`; no real user library, playlists, tags, settings, cache or monitored folders were used.
- Tag page: `design-qa/implementation-tag-three-column-1447.png` (1447×570).
- Normal page: `design-qa/implementation-non-tag-two-column.png` (1104×570).
- Playback: `design-qa/implementation-tag-playing-main-1447.png` (1447×342) and `design-qa/implementation-tag-playing-list-1447.png` (1447×570).
- Cache state: `design-qa/implementation-cache-hit-and-miss-visible.png` (1104×570), containing one existing v2 thumbnail cache hit and one cache miss that remained blank without a cache-only decode/write.
- Diagnostic 1447×1087 implementation canvas: `design-qa/implementation-composite-blocked-1447x1087.png`. It contains unscaled real main/list window grabs, a two-pixel dock overlap and 177 pixels of explicit bottom padding. It is not a same-frame desktop capture.
- True equal-canvas comparison opened and inspected: `design-qa/combined-final-blocked-source-vs-real-app.png` (2918×1121; each compared panel is exactly 1447×1087). Focused list region: `design-qa/combined-final-blocked-lower-region.png`.

## Environment and density coverage

| Requested scale | Real-app render artifact | Captured logical pixels | Physical desktop reachability |
| --- | --- | --- | --- |
| 100% | `design-qa/tag-dpi-1-dark.png` | 1447×570 | Not externally verified |
| 125% | `design-qa/tag-dpi-125-dark.png` | 1447×570 | Not externally verified |
| 150% | `design-qa/tag-dpi-15-dark.png` | 1447×570 | Not externally verified |
| 175% | `design-qa/tag-dpi-175-dark.png` | 1447×570 | Not externally verified |
| 200% | `design-qa/tag-dpi-2-dark.png` | 1447×570 | Not externally verified |

The real app-owned QQuickWindow capture path rendered complete logical content at all five per-process Qt scale factors. This does not prove that the physical Windows desktop, title controls and three independent scroll areas remained reachable; external UI observation was interrupted before that check.

Dark and light were exercised through the real application theme path: `design-qa/tag-theme-dark.png` and `design-qa/tag-theme-light.png`, both 1447×570. Both rendered without white-on-white content or clipping, but the light state did not receive an externally operated interaction pass.

## Blocking visual rubric

| Check | Result | Severity / evidence |
| --- | --- | --- |
| Typography and copy | Not accepted | P2: implementation has substantially smaller/sparser typography and synthetic fixture copy; populated reference copy was not reproduced. |
| Spacing and column widths | Partially fixed | P1 fixed: tag page now has navigation + the shared list + 328px tag panel at a 1284 minimum, while ordinary pages remove the panel and reclaim the width. The exact reference lower-window height is still not reproduced. |
| Row heights | Code/test verified only | 62px with thumbnails and 42px without remain covered by QML contracts; the 42px state was not externally compared in the reference state. |
| Colors and selected state | Not accepted | P2: reference uses a muted purple selected row; current real app uses a bright blue playing row. Tag-pill colors/counts cannot be judged because representative real tags were not populated. |
| Borders and radii | Partially accepted | No clipping or broken radii observed in the captured panels; the reference's finer border/contrast treatment still differs. |
| Scroll state | Blocked | Left, middle and right independent scrolling was not externally driven or captured. |
| Image quality/assets | Blocked | Real waveform thumbnails rendered, but reference cover art, metadata and tag corpus were unavailable in the isolated state. |
| Tag panel | Structurally fixed, content blocked | Search/Add and narrow panel are present only on Tag Management; real pill layout, counts, rename/color/delete menus and scrolling were not exercised. |
| Two-/three-column switching | Passed by production-path QML tests | One `sharedTrackList` instance survives tag → playlist/library/favorites/resource → tag; search, selection and playback identity are retained. |

## P0 / P1 / P2 / P3 history

- P0: none observed.
- P1 fixed: normal library/playlist/resource pages reserved the tag column. The panel and divider are now fully hidden and the same center list reclaims the width.
- P1 fixed: docked tag state was clipped to the 1104px player width. Docking now honors the active list page minimum and promotes both windows without hard-coding the reference width; returning to a normal page does not cause width churn, and a later user resize remains authoritative.
- P1 fixed: test mode's default thumbnail cache could resolve into Documents. It now stays under the isolated test cache location; normal application defaults are unchanged.
- P1 fixed: rapid real-library import/model mutation could leave a `Qt.callLater` thumbnail request bound to a destroyed QML context. The deferred request is now owned by a zero-delay child Timer and is cancelled with the delegate.
- P1 unresolved: no one-frame, same-state 1447×1087 Windows desktop capture exists. The equal-canvas composite intentionally exposes the 177px height/state gap and cannot be used as pass evidence.
- P1 unresolved: representative real tags, playlists and monitored resource-folder state were not safely populated and exercised through the externally controlled window.
- P1 unresolved: physical 100–200% DPI reachability, required CRUD/drag/drop/scroll/search/sort flows, and playback seek/next/pause-resume plus simultaneous list interaction remain unexecuted.
- P2 unresolved: selection color, typography/density, artwork/metadata and populated tag-pill visuals differ materially from the reference.
- P3: fixture titles and abbreviated navigation counts are appropriate only for diagnostic evidence, not final reference acceptance.

## Interaction and playback coverage

- Executed in the real Debug AgPlayer process through its isolated controller paths: WAV import, thumbnail cache generation/hit, cache-only miss display, playback start and real position advance. The playback evidence shows `track-01`, pause state, 0:01/0:02 position, main waveform and BPM 210.
- The configured Windows audio output path was opened by AgPlayer. No human audible-quality observation was made, so no claim is made about sound, beat accuracy, glitches or acoustic output quality.
- Not executed through external real-window input: seek, next, pause/resume, simultaneous playback + list scroll/search, independent three-pane scrolling, tag filter and CRUD/cancel, playlist create/import/rename/delete, monitored-folder add/remove, single/multi drag preview/cancel/drop, and real-window sort/search/filter.
- Automated production-path QML tests cover these controller/UI contracts, but automated tests are not substituted for the missing real-window acceptance steps.

## Computer Use recovery history and limitation

- Fresh `@oai/sky` sessions were initialized and window/app lists refreshed; a responsive native AgPlayer HWND existed but was not enumerable.
- A safe `.lnk` using only isolated QA arguments launched the real worktree executable, but the AgPlayer window still was not exposed for external observation/injection.
- `sky.launch_app` cannot pass command-line arguments, so it was not used to launch the executable against the user's real default state.
- A minimal `Qt.Window | Qt.FramelessWindowHint` enumeration hypothesis was built and launched only with the isolated `.lnk`; the user interrupted the final observation with Escape. The experiment is inconclusive, both temporary hunks were reverted, PID 16608 was verified as the worktree executable and stopped, and no further Computer Use input was issued.

## Verification

- Debug `AgPlayer`, `qml_main_window_test`, `settings_controller_test` and `window_controller_test` built successfully.
- Focused CTest: 5/5 passed (`window_controller`, settings, thumbnail provider/item/contract).
- Task 7 shared-list transition: 3 passed, 0 failed.
- Full QML: 79 passed, 0 failed, 1 skipped; the skip is the native WM_DROPFILES test under the offscreen platform.
- Thumbnail lifecycle deterministic RED: 2 passed, 1 failed because the destroyed wrapper dispatched one provider request; GREEN after the child Timer fix: 3 passed, 0 failed with zero requests/cancels and no warning. The earlier real-import sequence also changed from invalid-context failure to pass.

## Exit criteria

No P0 was observed in the captured states and the discovered structural/lifecycle P1 defects are fixed, but the mandatory real-window interactions, physical DPI reachability and reference-equivalent same-frame evidence are incomplete. P1/P2 acceptance findings therefore remain open.

final result: blocked
