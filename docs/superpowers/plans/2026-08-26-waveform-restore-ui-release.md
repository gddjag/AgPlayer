# AgPlayer 波形恢复、界面修复、分支集成与发布

## Global constraints

- Integrate on `codex/custom-theme-colors`; never overwrite newer recovered work with an older branch.
- Restore the previous waveform/spectrum color settings contract without reverting waveform coordinate, density, cache, or renderer performance fixes.
- Remove the ineffective player/list glass feature completely.
- Preserve cancelled vocal-separation and voice-clone plugins as absent.
- Use regression-first fixes, focused review commits, a clean final worktree, and an optimized Windows installer copied to the desktop.

## Task 1 — Waveform, spectrum, settings, and color picker

- Restore solid unplayed/played waveform colors, RGB base/start/middle/end colors, and a played-vs-unplayed RGB region selector.
- Keep spectrum settings to solid vs custom three-stop RGB only.
- Preserve legacy user values with non-destructive migration and keep the playback guide default off.
- Remove glass settings and bindings.
- Make the custom picker's top-left swatch open the traditional color dialog with cancel-safe semantics.

## Task 2 — Library, tags, and mini player

- Keep the search/rating/BPM/clear footer under the center track list only.
- Tighten title-to-thumbnail spacing, preserve exactly ten default rows, narrow artist/album columns, and enlarge navigation arrows.
- Make tag pills compact lightweight glass capsules and prioritize title width in tag mode.
- Keep mini metadata in one line: artist, album, optional tags, rating, favorite.

## Task 3 — Taskbar and metadata

- Restore native Windows minimize/restore behavior for the main/list group without changing geometry or docking.
- Reproduce and repair real single/batch metadata edits with stable target snapshots, stream-copy temp output, read-back verification, atomic replacement, rollback, and per-file results.

## Task 4 — Audio tools and installer

- Use the untinted transparent brand logo and responsive wide/medium/narrow audio-tool layouts.
- Keep all four tool pages inside their bounds at 880x560, 1000x720, 1280x720, and 1672x942.
- Replace distorted Inno wizard artwork with correctly proportioned brand assets and verify multilingual/DPI rendering.

## Task 5 — Integration, verification, and release

- Audit every worktree and branch; selectively port only completed, non-duplicated behavior.
- Run focused tests, full CTest, QML lint, Release build, installer smoke tests, and desktop copy verification.
- Commit by functional group and finish with a clean worktree.
