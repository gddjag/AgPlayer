# AgPlayer 最新集成修复与发布计划

## Global Constraints

- Integration base: `codex/recover-complete-release`; review every completed branch before packaging and selectively replay only missing, compatible work.
- Remove all remaining vocal-separation and voice-clone routes, assets, tests, and package entries.
- Preserve the 208 px navigation width, docked-window geometry, cross-monitor pixel size, and ten-row default list height.
- Use existing Qt/QML, FFmpeg, Windows and theme infrastructure. Do not add a heavyweight dependency.
- All behavior changes require a failing regression test before production changes. Validate real file/device flows in addition to mocks and contracts.
- Commit each task independently. Final Release build, full CTest, visual matrix, installed-app smoke test, clean worktree, desktop installer and SHA-256 are required.

## Task 1 - Library, tags, navigation, mini player, EQ and settings visuals

- Restore favorite/rating clicks without breaking title-area track dragging; add spacing between artist, album and rating.
- Bold track titles, tighten title-to-thumbnail spacing, use a 14-16 px visible-row waveform, and preserve title width in tag mode.
- Put the search/filter bar on its own full-width bottom row. Stop the tag panel above it.
- Sort tags by track count descending with a stable name tie-breaker; use low-cost translucent glass pills.
- Keep navigation at 208 px while tightening indentation, arrow slots and icon gaps. Propagate playlist renames immediately.
- Package and map the seven user-supplied SVG assets without recoloring; use the existing red filled heart for favorites.
- Make EQ titlebar controls and toggles compact while retaining accessible hit targets.
- Give the mini player separate artist/album/tag fields; only show a non-empty tag; align equal-size rating and favorite controls.
- Fix mojibake for track/title/artist/album/tag text through deterministic metadata decoding and normalization without damaging valid Unicode.
- Request waveform thumbnails only for visible delegates, prioritize current viewport rows, cancel stale work, and keep cache density independent of row width.
- Use theme-aware translucent purple for the playing row. Use shared native-style BPM slider, switches and checkboxes with dark/light/focus/disabled states.
- Add flag and language text to the existing `zh/en/th/vi` language choices.
- Add a default-off player/list glass-background setting. Use native Windows backdrop when available and a safe translucent fallback elsewhere.
- Simplify waveform/spectrum colors to `solid/custom` plus played/unplayed colors; remove progress RGB and migrate old settings once.

## Task 2 - Audio editor, playback and recording

- Re-request waveform data after every edit mutation; show explicit asynchronous playback preparation and cancellation.
- Keep `Space` and the play button working with real files and stop the main player before editor playback/recording.
- Render one centered visual mixdown while preserving source channels; use continuous peak envelopes and real-sample antialiased lines at high zoom.
- Make the center gain line draggable, keep double-click automation points and fade handles undoable.
- Default selection playback to loop; right-click cancels. Keep only bottom-left `拖出片段`, top-right range, and green playhead glass capsules.
- Add shortcut tooltips to editor, playback and recording controls.
- Use the default WASAPI device; update playhead, live waveform and green input meter every 33 ms without cancelling the historical waveform task.
- Analyze BPM from selection first; run BPM, speed, pitch and preserve-pitch previews asynchronously with progress, cancellation, task generations and detailed errors.
- Initialize export settings to Desktop, valid format/sample-rate/channels and 320 kbps for lossy codecs; report progress, result path and errors.
- Make the audio-tools shell title `AgPlayer · 音频工具` and implement wide/medium/narrow responsive layouts.

## Task 3 - Converter, metadata, filename processing and shared settings

- Output exactly MP3, FLAC, WAV, AAC, Opus, OGG, ALAC and AIFF. Rebuild valid parameters when format changes.
- Use 320 kbps when a lossy encoder supports it; hide bitrate for lossless formats.
- Move concurrency beside total progress; allow 1-10 and default to 5.
- Default metadata, cover, directory structure and video extraction options to enabled.
- Make completed/failed summaries interactive task filters.
- Preserve covers only when the source has one and the target genuinely supports it; a cover limitation must not fail the audio conversion.
- Metadata fields start untouched; edits replace, deleting an existing value clears, untouched fields remain. Freeze one target snapshot for preflight/write/readback/refresh.
- Make automatic numbering and remove-numbering default off and strictly mutually exclusive in UI and backend normalization.
- Link general export, transcoding output and pitch presets from Settings to the audio tools as one shared source of truth.
- Set the default cache limit to 10 GB and migrate only untouched legacy defaults.

## Task 4 - Windows shell, versioning and installer

- Keep the process, PE, top-level windows, shortcuts and registry on `AgPlayer.Desktop` with the same brand icon.
- Restore native taskbar group minimize/restore semantics without resizing or breaking docking/DPI; secondary windows stay out of separate taskbar groups.
- Generate the About version and installer version from the build version source; remove hard-coded display versions.
- Add installer language selection with Chinese as the default and the supported app languages as choices; use the brand logo on installer pages.

## Task 5 - Integration, QA, commits and release

- Review unique commits from completed branches and selectively integrate only compatible work not already represented in the base.
- Remove cancelled plugin remnants and obsolete duplicate code/resources.
- Run focused tests after each task and an independent task review before continuing.
- Run full Release build/CTest/QML checks, dark/light visual matrices, real audio conversion/metadata/editor playback, 30-second microphone capture, taskbar and multi-DPI dock checks.
- Create an optimized complete installer, install/smoke test it, copy it to `C:\Users\Administrator\Desktop\AgPlayer-Setup-Latest.exe`, and report commit, size, SHA-256, tests and manual-only checks.
