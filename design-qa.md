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

---

# Design QA — 人声伴奏分离 — 2026-08-30

## Source and current implementation

- Source: `C:\Users\Administrator\Desktop\音视频播放器\AgPlayer音频播放器完整版\人声伴奏分离.png` (1672 × 941).
- Current implementation: real 3-minute FLAC loaded through the production controller; models remain honestly uninstalled and history/results remain empty.
- Combined comparison inputs: `build/qa/vocal-separation-final/comparison-1672x941-loaded-dpi100-v2.png`, `comparison-1672x941-loaded-dpi125-v2.png`, `comparison-1672x941-loaded-dpi150-v2.png`, and `comparison-1672x941-loaded-dpi200-v2.png`.

## Findings

- The deep-blue workbench, five-item top navigation, source waveform, model/custom cards, 70/30 work/history split, stem rows and fixed primary action follow the reference hierarchy.
- 100%, 125%, 150% and 200% DPI captures have no visible clipping, overlap, displaced navigation or inaccessible primary action.
- The implementation screenshot and reference do not have the same product state. The reference contains installed/downloading cards, five completed stems and populated history; the current honest screenshot contains a real selected source but no installed model or completed job.
- Production code was not given screenshot-only fake data. A final same-state comparison must be captured after installing the approved models and completing a real job through the UI.

## UI correction iteration

- Reference-size implementation: `build/qa/vocal-separation-rework/1672x941-final-v2.png` (1672 × 941, real 44.1 kHz WAV loaded through the production controller).
- Same-image comparison input: `build/qa/vocal-separation-rework/comparison-1672x941-final-v2.png` (reference left, implementation right, identical viewport).
- Responsive captures: `1280x720-final-v2.png`, `880x560-final-v2.png`, and `1920x1080-final-v2.png` in the same directory.
- The top input/preview and model deck are now deliberately shorter than the reference, matching the correction request and reserving more vertical space for the five result tracks.
- The input preview has a leading play control, shared progress position and clickable player `WaveformItem`; all five result rows use the same renderer and waveform settings instead of a page-specific waveform style.
- Model cards expose tier/badge, name, provider, description, official repository, size and state in a horizontally scrollable deck, including a truthful safe-manifest custom-model entry.
- Output tracks expose distinct shipped icons, descriptions, checked state and explicit unsupported state. The bottom bar now includes shared playback transport, time, export and start actions.
- Output format, output directory, device selection, completion actions and history table/actions use local dark controls and real controller data. At 880 × 560, settings and history correctly move to page tabs while the primary action remains visible.
- Independent review found and the implementation corrected three interaction/layout defects: history rows now open their own output path, reserved local filename characters are converted by `QUrl::fromLocalFile`, and 1100–1439px history rows use a readable stacked detail layout. Repository links are also keyboard focusable and exposed as accessible buttons.
- The correction screenshots still represent a loaded-input/uninstalled-model state; therefore completed waveforms and populated history cannot be compared to the reference without completing a real separation job.

final result: passed

Release note: this result covers the requested loaded-input/uninstalled-model UI state. A completed-model state comparison and real audio-hardware interaction remain separate release gates and are not claimed here.

## Final compact-workbench refinement

- Final same-image comparison input: `build/qa/vocal-separation-final/comparison-1672x941-refined.png` (reference left, implementation right).
- Final responsive captures: `build/qa/vocal-separation-final/1672x941.png`, `1280x720.png`, `880x560.png`, and `1920x1080.png`.
- The source preview is 116px high, has no cover, keeps filename/size/duration visible, and extends the shared player `WaveformItem` across the remaining width. Input playback no longer advances result waveforms.
- Model cards are 174px high and selected by clicking the entire card. Tier/badge and all required metadata remain visible; the horizontal scrollbar sits below the card frame.
- Output-track selectors use distinct licensed Lucide/project icons, always-visible checked or unchecked controls, compact widths, and a reserved truthful backup-address dialog. No placeholder download URL is presented as real.
- Each result row places volume before the shared waveform renderer and has no per-row transport. One bottom transport previews the current result source.
- The bottom bar now contains only playback/time, re-separation, accompaniment export, vocal export, all-stem export, and start. WAV/FLAC/MP3 and Auto/CPU/GPU remain in the right settings panel.
- At 880x560 the settings/history tab and scrolling workbench intentionally avoid simultaneous full-page display; the two-row bottom actions and primary action remain reachable.

final result: passed

---

# Audio editor interaction and responsive gate — 2026-08-31

## Reference and measured layouts

- Visual reference inspected: `C:/Users/Administrator/Desktop/音视频播放器/AgPlayer音频播放器完整版/音频编辑.png`.
- The production QuickTest measures the 1672×941 shell-content reference at
  1672×822: timeline workspace `(12,152,1304,451)`, playback `(12,618,1304,112)`,
  shortcut card `(12,730,1304,67)`, and status `(0,797,1328,25)`.
- At 1280×720, the workspace is `y=152,h=226`, ruler `y=152,h=44`, track
  `y=196,h=162`, scrollbar `y=366,h=16`, transport `390..502`, shortcut card
  `509..576`, and status `576..601`.  The two 13px shortcut rows remain
  horizontally reachable by wheel input when content overflows.
- At 880×560, the former card `(y=568,h=103)` overlapped the playback panel and
  extended to `y=671`, 111px below the clipped page.  The narrow production
  geometry is now workspace `y=140,h=83`, ruler `y=140,h=20`, track
  `y=160,h=48`, scrollbar `y=211,h=12`, playback `229..333`, shortcut card
  `341..408`, and status `416..441`.  Both shortcut rows, their horizontal
  divider and the narrow playback entry are above the status overlay; the card
  does not cover transport.

## Functional evidence

- A desktop native-window QML journey uses the real generated WAV fixture
  through the production drop route, then performs mouse/key input for split,
  right-clip selection, copy/paste, trim, mute, fade, delete, undo,
  body-range loop and first focused Space playback.  The 880×560 smoke is
  deliberately smaller: real import, page bounds, waveform-body selection,
  Escape cancellation, click playback and Space stop.
- Clearing a document now clears a stale failed-playback error, so the status
  bar no longer survives a successful clear as an obsolete visible error.
- A paste colliding with occupied timeline space inserts its clipboard span in
  one history entry: the containing event is split only when needed, trailing
  events shift right, and clones receive IDs before an automatic right split.
  Gap paste keeps the existing non-ripple behavior.

## Automated verification

- Release CTest: `event_edit_test` (15/15), `audio_editor_controller_test`
  (1/1, fixture injected by CTest), `qml_audio_editor_test` and
  `qml_audio_editor_native_input_test` (2/2).
- Debug CTest: `qml_audio_editor_test` and
  `qml_audio_editor_native_input_test` (2/2).
- `qmllint` (Qt 6.7) completed with zero warnings for `AudioEditorPage.qml` and
  `EditorWaveformCanvas.qml`.

## Explicit limits

- No physical hardware audio/output-device or subjective listening check was
  performed.
- No external desktop screenshot capture was taken for this gate; the geometry
  evidence is production QML test measurement, not a visual pixel-diff.

## Audio editor review fix round 1 — 2026-08-31

### Corrected shell evidence

- The earlier `b0dc59f` capture set at
  `build/qa/audio-editor/task7-final` is **not** acceptance evidence.  It
  captured the pre-fix shell with the 880 transport/shortcut content clipped,
  and the 1280 transport/card boundary overlapping by four pixels.
- The real `AudioToolsWindow` shell, measured in production QML tests, leaves
  editor-page heights of 822px (1672×941 window), 601px (1280×720), and 441px
  (880×560).  These are shell content dimensions, not a bare 560px page.
- At 1280 shell content, the compact desktop layout is transport `390..502`,
  shortcut `509..576`, status `576..601`.  At 880 shell content it is transport
  `229..333`, shortcut `341..408`, status `416..441`; the 13px two-row card
  and main playback entry are contained without overlaying each other.

### Functional and visual evidence

- Production-shell QML checks cover the 1280 non-overlap and all 880 page
  bounds, including both shortcut rows and the status region.  The real-WAV
  compact smoke verifies narrow playback-entry/primary-button bounds and real
  mouse/Space playback plus selection; it does not duplicate the desktop
  envelope/edit chain.
- The native edit journey observes `mute` through the existing
  `timelineEventViews` map and verifies the paste route, new clone selection,
  event-count growth and Undo/Redo.  Exact automatic split, ripple and source
  ranges are asserted by C++ core/controller tests.
- QA screenshot capture for audio-editor import now waits on existing
  `hasDocument` and `viewportChannelPeaks` readiness rather than guessing with
  a fixed delay.  A missing waveform becomes an invalid artifact, not a false
  successful capture.

### Acceptance boundary

- New real-WAV screenshots are written under
  `build/qa/audio-editor/task7-final-verified` at 1672×941, 1280×720 and
  880×560.  They show the compact 880 transport and both shortcut rows, and
  1280 no longer overlaps the card.  They are engineering evidence only: the
  1672 implementation still differs materially from the reference in palette,
  cyan-vs-blue waveform colour, typography/density, control borders and
  inspector styling.  Pixel-level acceptance therefore remains blocked.
- Hardware-device routing, audible quality, latency and subjective listening
  remain unverified.

### Final gate evidence

- Release and Debug builds pass.  All 13 audio-editor-focused tests pass in
  both full CTest runs, including production-window native input and the
  1672/1280/880 layout contract.
- Application-owned Release and Debug startup smokes import the real WAV and
  produce non-empty 880×560 captures at
  `build/qa/audio-editor/final-smoke-release-postreview-20260831.png` and
  `build/qa/audio-editor/final-smoke-debug-postreview-20260831.png`.
- The broader repository is not all green: full Release is 108/112 and full
  Debug is 105/113.  Diagnosed failures live in unchanged import/library,
  thumbnail, tag-theme, main-window, deployment or Windows-shell baselines;
  they are not counted as editor visual acceptance.
- Visual result remains **blocked**, not passed.  The three target-size
  captures prove reachability and non-overlap, but still do not match the
  supplied 1672×941 reference pixel-for-pixel.

---

# 人声伴奏分离界面 Design QA

## Comparison target

- Source visual truth: `C:\Users\ADMINI~1\AppData\Local\Temp\codex-clipboard-dc0cf0b8-60ca-4409-961a-49c192c5f6c0.png`
- Rendered implementation: `D:\ai\AgPlayer\.worktrees\vocal-separation-native\build\qa\separation-1672x941.png`
- Viewport: AgPlayer 音频工具窗口 `1672 × 941`，Windows x64，深色主题，中文界面。
- Pixels and density: source `1671 × 944`，implementation `1672 × 941`；均按原始像素 1:1 放入比较画布，没有缩放。附件本身与目标窗口有 1px 宽、3px 高差异，不影响区域比例判断。
- State: source 为“模型下载/已安装、任务已完成并存在历史”；implementation 为真实短 WAV 已载入、模型未安装、等待输入。数据状态不同，因此不把下载进度、结果波形密度和历史行数量作为视觉偏差。

## Evidence

- Full-view comparison: `D:\ai\AgPlayer\.worktrees\vocal-separation-native\build\qa\separation-reference-vs-implementation.png`
- Focused model/input comparison: `D:\ai\AgPlayer\.worktrees\vocal-separation-native\build\qa\separation-model-settings-comparison.png`
- Focused output/progress/stem comparison: `D:\ai\AgPlayer\.worktrees\vocal-separation-native\build\qa\separation-progress-tracks-comparison.png`
- Responsive captures:
  - `D:\ai\AgPlayer\.worktrees\vocal-separation-native\build\qa\separation-880x560.png`
  - `D:\ai\AgPlayer\.worktrees\vocal-separation-native\build\qa\separation-1280x720.png`
  - `D:\ai\AgPlayer\.worktrees\vocal-separation-native\build\qa\separation-1920x1080.png`

Focused comparisons were required because model metadata, status copy, checkboxes, the thin progress track and per-stem volume controls are not legible enough in the full 3343px-wide comparison.

## Findings

No actionable P0/P1/P2 visual differences remain in the states that can be compared honestly.

- Fonts and typography: small metadata labels, model headings, section headings and bottom transport time retain the existing AgPlayer font system and match the reference hierarchy. Truncation is limited to long provider/description strings and is intentional at narrower widths.
- Spacing and layout rhythm: input preview, four-card deck, output selector row, five stem rows, 70/30 main/sidebar split and large bottom action bar follow the source composition. The 880px layout switches to page tabs and a two-row action grid without overlap.
- Colors and visual tokens: local navy surfaces, cyan selection, orange source transport, green result transport/progress and muted unsupported stems follow the requested semantics. The empty progress outline is a low-opacity neutral gray; only completed fraction and percentage use green.
- Image quality and assets: the screen uses the existing AgPlayer icon pipeline plus licensed Lucide assets; no emoji, text glyph substitutes, CSS drawings or placeholder image boxes are used.
- Copy and content: model cards expose real catalog metadata. The custom card no longer promises nonexistent automatic discovery and clearly says only trusted catalog models execute. “完成后打开目录” remains visible and is unchecked by default.

Expected state-only differences:

- The reference has downloaded/completed model cards, populated waveforms and history. The implementation capture intentionally shows real waiting/not-downloaded state; no fake production state was injected.
- The reference primary action is enabled blue; the implementation action is correctly disabled until a trusted model is installed.

## Comparison history

1. Earlier P2: compact `880 × 560` bottom controls overlapped their allocated cells. Fix: replaced the overflowing compact row with an explicit two-row grid and kept the transport at least 44px high. Post-fix evidence: `separation-880x560.png`.
2. Earlier P2: the empty separation progress outline was visually too prominent. Fix: changed it to semi-transparent neutral gray while retaining green fill/percentage. Post-fix evidence: `separation-1672x941.png` and `separation-progress-tracks-comparison.png`.
3. Earlier P1 copy issue: the custom model card claimed automatic manifest discovery that production does not provide. Fix: retained the reference-specific custom card treatment but replaced the promise with truthful trusted-model/manual-directory copy. Post-fix evidence: `separation-model-settings-comparison.png`.
4. Earlier P2 requirement misunderstanding: “完成后打开目录” was removed instead of merely unchecked. Fix: restored the checkbox and completion behavior with default `false`. Post-fix evidence: `separation-1672x941.png` and the QML interaction test.

## Interaction and responsive checks

- Input and result transports use separate sources, colors, positions and volume state; Space controls the result transport.
- Model deck supports horizontal movement; card selection uses the full card.
- Stem volume supports click, drag, wheel and keyboard changes.
- GPU candidate selection remains available after hardware probe, with trusted-model validation deferred to job start and CPU fallback in Auto.
- QML separation checks and all four responsive captures completed successfully; after adding synchronized result-stem preview coverage, the final full Release test run passed 89/89 (340.11 seconds).

## Follow-up polish

- P3: repeat the same-state visual comparison after a real trusted model is installed and a real separation finishes, so populated result waveforms and history density can be compared directly. This is evidence work, not a remaining code/UI defect.

## Implementation checklist

- [x] Preserve source/reference layout and local design tokens.
- [x] Restore auto-open checkbox as visible and unchecked by default.
- [x] Verify compact and desktop breakpoints.
- [x] Verify focused model, progress and stem regions.
- [x] Keep real data states; do not inject fake production completion history.
final result: passed

---

# Design QA — 18 段图形均衡器 — 2026-08-31

## Source visual truth

- Reference: `C:/Users/Administrator/Desktop/音视频播放器/AgPlayer音频播放器完整版/18段标准EQ均衡器 预设：平直、重低音、古典、流行、摇滚、人声、EDM电子、爵士 .png`.
- Reference pixels: 1672 × 941, dark state, enabled, custom preset, ±12 dB, high precision, eighteen stated gains, preamp -1.5 dB, output -1.5 dB.
- The reference's displayed slider/curve positions exaggerate several positive dB values. Functional truth therefore requires linear dB mapping even where that differs visibly from the reference.

## Implementation evidence

- Final implementation: `design-qa/eq-reference-implementation-1672x941.png` (1672 × 941).
- Same-input comparison: `design-qa/eq-reference-comparison-1672x941.png` (3344 × 941), reference on the left and implementation on the right.
- Responsive captures: `design-qa/eq-reference-implementation-1180x680.png` and `design-qa/eq-reference-implementation-880x520.png`.
- Both sides of the final comparison were drawn with explicit pixel source/destination rectangles. DPI metadata (reference 72 DPI versus implementation 96 DPI) was not allowed to rescale either image.

## Comparison history

- Pass 1: blocked — the real DSP response line was visually separate from the band nodes; the slider scale was missing; toolbar icon/scale and narrow-window discoverability had P2 differences.
- Pass 2: blocked — the curve now passed through real controller gains and all other P2 findings were closed, but logarithmic frequency placement compressed 8–20 kHz and broke the reference's one-to-one rhythm with the sliders.
- Pass 3: passed — grid, nodes, labels and spline use equal band slots; the spline still uses real controller gains with linear dB mapping. No P0, P1 or P2 remained.

## Accepted P3 differences

- Toolbar elements retain isolated 3–5 px placement differences.
- Slider handles have a slightly heavier shadow and the panels are flatter than the reference.
- Footer vertical rhythm differs by roughly 2–3 px.
- The implementation intentionally does not copy the reference's numerically incorrect vertical exaggeration.
- At -1.5 dB the final meter blocks follow the real shared threshold mapping rather than the reference's decorative all-near-end state.

## Functional evidence

- All primary controls use real `EqualizerController` state; screenshot-only meter override defaults to NaN and is restricted to deterministic capture tests.
- The output meter receives post-EQ/replay-gain/volume/fade sample peaks from the audio engine and invalidates stale values across seek, stop, mute and device-switch boundaries.
- 1180 × 680 and 880 × 520 show persistent overflow indicators; automated bounds checks prove the output value and preamp can be fully reached.
- No new SVG was drawn. Save, manage and circular restore use existing repository assets.

No hardware listening or true-peak calibration was included in this visual acceptance.

final result: passed
