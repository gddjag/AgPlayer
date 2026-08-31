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

final result: blocked — reference-state visual parity and final hardware interaction remain unverified

---

# Design QA — 频彩波形中心高亮与播放进度 — 2026-08-31

## Source visual truth and scope

- Source reference: `E:\Administrator\下载\微信图片_20260829202539_118_10.png` (1644 × 203 px).
- The source is a palette/layer-separation reference from another layout, not a pixel-identical AgPlayer screen. The scoped target is the low/mid/high transparent overlay, restrained center focus, and subtle played/unplayed transition; source typography, wallpaper, timing labels, and overall geometry are intentionally out of scope.
- Required AgPlayer state: frequency mode (`visualMode = 3`), real waveform analysis, playback active, and both dark and light themes.

## Implementation evidence

- Exact executable: `build/frequency-clean-release/app/AgPlayer.exe`.
- Dark native capture: `build/qa/frequency-waveform-subtle-progress-20260831/isolated-dark-frequency.png` (960 × 298 logical px).
- Light native capture: `build/qa/frequency-waveform-subtle-progress-20260831/isolated-light-frequency.png` (960 × 298 logical px).
- Combined source + focused waveform + full-view comparison opened and inspected: `build/qa/frequency-waveform-subtle-progress-20260831/comparison-frequency-waveform.png` (1280 × 850 px).
- Both captures used `multiband-qa.wav`, a deterministic 16-second PCM signal containing independently varying low, mid, and high components. AgPlayer loaded, analyzed, and rendered this signal through the production waveform path; the image does not contain hand-painted waveform data.
- QA processes ran with isolated settings on a non-interactive Windows desktop. The user’s open AgPlayer window and production settings were not clicked, closed, or overwritten.

## Density, viewport, and state normalization

- Both implementation captures use the same 960 × 298 logical window and the same 100% logical capture normalization.
- Both are captured around 00:01 / 00:16 with playback active and the same analyzed audio.
- The focused comparison crops the waveform band from each AgPlayer capture without altering its colors. Source and implementation widths differ, so each crop is scaled only for side-by-side visual judgment; no claim of pixel-perfect geometric equivalence is made.

## Visual fidelity rubric

| Surface | Result | Evidence |
| --- | --- | --- |
| Typography | Passed for scoped regression | Player title, metadata, time labels, and controls remain readable in both themes; typography was not changed by this task. |
| Spacing and layout | Passed | The complete waveform occupies the available width in both captures with no clipping, missing tail, overlap, or displaced controls. |
| Colors and tokens | Passed | Mint low, warm cream mid, and coral high remain separately legible as transparent layers on both backgrounds. Dark and light captures keep the same user-facing band colors; only theme-aware focus/progress treatment changes. |
| Center focus | Passed | Dark uses `#C8CDD2` at 0.58 opacity; light uses `#56616A` at 0.72 opacity. The existing 1 px guide and 5 px focus point remain restrained and do not turn into a bright white seam. |
| Playback progress | Passed | Played remains 1.00; unplayed is 0.90 dark / 0.92 light, with the existing ±12 logical-pixel feather. The transition is visible on inspection but no longer reads as a second saturated color block. |
| Image quality | Passed | Low/mid/high peaks remain crisp at the native logical viewport; transparency does not erase the waveform or introduce visible clipping/banding. |
| Copy and fixture data | Passed with QA qualification | `multiband-qa` and unknown metadata are explicit deterministic QA data, not production copy or a parity target. |

## Comparison history and findings

- An earlier interactive capture used a 440 Hz sine wave. It confirmed the center/progress treatment but could not prove three-band separation, so it was not used as final color evidence.
- The final deterministic multiband captures make all three layers visible. Against the source, AgPlayer keeps the same fresh green/warm cream/coral family while deliberately using lower chroma and alpha, matching the user’s request to avoid glare in dark and light themes.
- The light center guide is darker rather than white-gray because a white line would disappear into the light player surface. The dark guide remains a soft neutral light gray.
- No second waveform geometry was introduced; theme/progress changes operate on the existing single frequency waveform render path.

## Verification boundary

- This pass accepts the scoped frequency-waveform appearance and cross-theme behavior.
- It does not claim acoustic output quality, arbitrary user color combinations, physical high-DPI monitor reachability, or whole-product pixel parity with the external reference image.

final result: passed

---

# Design QA — 频彩波形 Open Color 跨主题配色 — 2026-08-31

## 标准与范围

- 本轮颜色标准独立采用 Open Color 开源色板（MIT）：低频 `#1098AD`、中频 `#F59F00`、高频 `#AE3EC9`。
- 本轮只调整颜色令牌、主题派生锚点、Mix 中性底层与中心高亮；频段提取、共享波形数据、单几何四层绘制和播放进度算法均保持不变。
- 用户仍只维护一套自定义颜色。浅色主题继续通过现有 OKLCH 感知差值映射自动派生，不新增第二套设置。
- 本节是当前配色结论；前一节记录的是已被后续反馈淘汰的历史方案。

## 原生证据

- 精确构建：`build/frequency-clean-release/app/AgPlayer.exe`。
- 深色截图：`build/qa/frequency-waveform-open-color-20260831/open-color-dark.png`（960 × 298）。
- 浅色截图：`build/qa/frequency-waveform-open-color-20260831/open-color-light.png`（960 × 298）。
- 两张截图使用相同的 `multiband-qa.wav`。音频经生产波形分析路径生成 Mix/Low/Mid/High 数据，未手绘、未合成第二套波形。
- QA 进程使用隔离设置和非交互 Windows 桌面；没有修改或关闭用户现有播放器状态。

## 视觉检查

| 检查项 | 结果 | 证据 |
| --- | --- | --- |
| 完整轮廓 | Passed | 深浅截图均完整覆盖 00:01–00:16 可视时间轴，无头尾裁切或局部缺失。 |
| 三频辨识 | Passed | 青蓝低频尖峰、琥珀中频主体、紫色高频细节在两种表面上保持不同色相；透明叠加处没有过曝。 |
| 深浅主题一致性 | Passed | 深色使用 Open Color 7 级令牌；浅色使用对应 8 级锚点，既不荧光刺眼，也未淡化为不可辨识。 |
| 中性底层 | Passed | 深色 `#868E96`/22%，浅色 `#495057`/24%，负责连续 Mix 轮廓而不抢三频层级。 |
| 中心焦点 | Passed | 深色 `#CED4DA`/54%，浅色 `#495057`/66%；1 px 引导线和 5 px 焦点保持可见但不形成高亮切割。 |
| 播放进度 | Passed | 仍为已播放 1.00、未播放深色 0.90/浅色 0.92，并保留 ±12 逻辑像素羽化；截图中只呈现轻微透明度差。 |
| 单一渲染路径 | Passed | C++ 测试验证节点/几何复用、顶点位置不变，仅更新四层预乘颜色；QML 测试验证主/迷你播放器不启用重复裁剪层。 |

## 验证边界

- Release 构建成功；频彩相关 13 项 C++/QML/生命周期测试全部通过。
- 已独立检查深色与浅色原生截图。用户最终审美接受仍以实际打开后的主观确认为准。
- 未声称覆盖任意自定义颜色组合、物理高 DPI 显示器可达性或声学输出质量。

final result: passed — implementation and internal cross-theme visual QA; pending user visual acceptance
