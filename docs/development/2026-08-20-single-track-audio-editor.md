# 单轨 AudioEvent 编辑器开发记录

## Phase 1 — AudioEvent 数据模型（2026-08-20）

### 需求追踪

- 规格 3.1：未新增运行时依赖；本阶段仅使用 C++ 标准库和既有 Qt Test。
- 规格 3.3：`SampleFrame` 固定为有符号 64 位，所有区间保持半开约定。
- 规格 5.1：`AudioEvent` 只以 `std::shared_ptr<const AudioSource>` 引用不可变源，不持有 PCM；验证源边界、时间线起点、淡入淡出及受限、排序的包络点。
- 规格 17 / 18.1：先 RED、后最小 GREEN，并为 Core 数据模型提供自动化测试。

### 修改文件

- `core/src/audio_editor/audio_event.hpp`：冻结 `SampleFrame`、`EventId`、`AudioSource`、`EnvelopePoint`、`FadeCurve`、`AudioEvent`，并提供 `isValid()` 与 `audibleFrames()`。
- `core/src/audio_editor/edit_command.hpp`：从新的域模型头获得唯一的 `SampleFrame` 定义。
- `core/src/audio_editor/audio_document.hpp`：仅删除已迁移到 `audio_event.hpp` 的重复 `AudioSource` 声明；未改变 `AudioDocument`、`AudioSpan` 或编辑行为。
- `core/CMakeLists.txt`、`tests/CMakeLists.txt`：注册域模型头与 `audio_event_test`。
- `tests/core/audio_event_test.cpp`：覆盖共享不可变 Source、源/时间线边界、淡入淡出、包络限制和超 32 位的两小时 SampleFrame。

### TDD 证据

**RED（预期失败）**

```powershell
cmake --preset windows-msvc-release
cmake --build build/release --target audio_event_test --parallel 4
ctest --test-dir build/release -R "^audio_event_test$" --output-on-failure
```

构建以预期原因失败：`fatal error C1083: cannot open include file: "audio_editor/audio_event.hpp"`。因测试可执行文件尚未生成，随后定向 CTest 报 `audio_event_test` `Not Run`。

**GREEN（通过）**

```powershell
cmd.exe /d /s /c 'call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build build\release --target audio_event_test --parallel 4 && ctest --test-dir build\release -R "^audio_event_test$" --output-on-failure'
```

输出：`1/1 Test #7: audio_event_test ... Passed`，`100% tests passed`。

### 构建与运行证据

VS x64 开发环境下执行：

```powershell
cmake --build --preset windows-msvc-release --parallel 4
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug --parallel 4
ctest --test-dir build/release -R "^audio_event_test$" --output-on-failure
ctest --test-dir build/debug -R "^audio_event_test$" --output-on-failure
```

- Release 全量构建：退出码 0。
- Debug 全量构建：退出码 0。
- Release 聚焦 CTest：`1/1` 通过。
- Debug 聚焦 CTest：`1/1` 通过。
- 本阶段为 header-only 域模型，无独立 UI、真实音频、硬件或应用运行路径可验收。

### 已知失败、未验证项与回退点

- 当前 PowerShell 没有 MSVC `INCLUDE` 环境变量；未载入 VS 开发环境时，首次 GREEN 编译报 `<cmath>` 未找到。使用 `VsDevCmd.bat -arch=x64 -host_arch=x64` 后通过，非产品代码问题。
- 未运行完整 CTest、真实音频、录音、UI、硬件、拖放或性能矩阵；它们不属于 Phase 1 的数据模型门禁。
- 回退点：`9226ad7409d2bdae1ea335a51b22d9eadc25fd65`（Phase 1 提交前 HEAD）。

## Phase 2 — 单轨 EventTimeline 与统一时间映射（2026-08-20）

### 需求追踪

- 规格 3.1 / 3.3：未增加运行时依赖；沿用冻结的有符号 64 位 `SampleFrame` 与半开区间约定。
- 规格 5.2：`EventTimeline` 按 `timelineStart` 排序保存事件，拒绝重叠；空隙不被压缩，总时长为最后事件的 `timelineStart + audibleFrames`。
- 规格 7.1：`TimelineSnapshot` 可直接作为 `TimePixelMapper` 的统一时间输入，并覆盖 44.1 / 48 / 96 kHz 的两小时端点回环精度。
- 规格 17 / 18.1：先记录缺失接口的 RED，再完成最小实现；覆盖命中、空隙、事务拒绝、revision 与映射。

### 修改文件

- `core/src/audio_editor/event_timeline.hpp/.cpp`：新增单轨 `EventTimeline` 和按值 `TimelineSnapshot`；仅成功插入时增加 revision，拒绝无效、重复 ID、溢出与区间重叠。
- `core/src/audio_editor/time_pixel_mapper.hpp`：接受 `TimelineSnapshot`，以其 `totalFrames` 作为唯一映射范围。
- `core/src/audio_editor/audio_document.*`、`document_renderer.*`、`document_writer.*`、`time_pitch_session.*`：为既有单事件、未变换快照提供受限适配；多事件、空隙或效果事件明确拒绝，正式多事件渲染仍留给后续导出阶段。
- `core/CMakeLists.txt`、`tests/CMakeLists.txt`：编译 Timeline 并注册 `event_timeline_test`。
- `tests/core/event_timeline_test.cpp`、`tests/core/time_pixel_mapper_test.cpp`：覆盖保留空隙、半开命中、重叠事务、revision、单事件适配及长时长映射。

### TDD 与调试证据

**RED（预期失败）**

```powershell
cmake --build build/release --target event_timeline_test time_pixel_mapper_test --parallel 4
```

在实现前，两个新测试均以 `fatal error C1083: cannot open include file: "audio_editor/event_timeline.hpp"` 失败。新增单事件适配测试后，未声明适配函数时也以 `C3861: 'singleEventDocumentSnapshot': identifier not found` 失败。

**GREEN（通过）**

```powershell
ctest --test-dir build/release -R "^(audio_event_test|event_timeline_test|time_pixel_mapper_test|audio_document_test)$" --output-on-failure
ctest --test-dir build/debug -R "^(audio_event_test|event_timeline_test|time_pixel_mapper_test|audio_document_test)$" --output-on-failure
```

Release 与 Debug 均为 `4/4` 通过。

曾有 `audio_document_test` 在增量重链后发生 `0xc0000005`。稳定复现表明只影响含已改 `AudioDocument` 头的旧测试对象；`ninja -t deps` 显示该对象依赖数为 0，且 `rules.ninja` 的 `msvc_deps_prefix` 与本机中文 `/showIncludes` 输出编码不符。`--clean-first` 全量重编/重链后该测试稳定通过，确认是构建依赖跟踪的本地化编码问题，不是 Timeline 或文档业务逻辑缺陷；本任务未修改构建系统。

### 构建与未验证项

- `cmake --build build/release --clean-first --parallel 4`：通过。
- `cmake --build build/debug --clean-first --parallel 4`：通过。
- 已执行 `git diff --check`。
- 未运行完整 CTest、UI、真实播放/录音/硬件或正式多事件导出；后者明确不属于本阶段。
- 回退点：`ad52f84`（Phase 1 冻结 `audio_event.hpp` 后的 Task 2 起点）。

## Phase 3 — 移动与修剪（2026-08-20）

### 需求追踪

- 规格 5.2 / 6 / 17.3：新增 `EventTimeline::moveEvent()` 与
  `trimEvent()`，操作均先验证候选状态，再以排序后的元数据副本原子替换；失败时
  Event、顺序、总时长和 revision 不变。
- Move 只改变 `timelineStart`；Trim 只改变 `sourceStart`、`sourceEnd` 与调用方
  显式指定的锚点 `timelineStart`。两者均保留不可变 Source 及全部其余 Event
  元数据。
- `TimelineEditCommand` 只保存一个 Event 的前后元数据，`execute/undo` 在当前
  状态精确匹配时才应用，避免覆盖并发/过期编辑；没有 PCM、解码器、编码器或 Peak
  重建依赖。
- 当前 `AudioEditorController::runDocumentCommand()` 会清空可见波形缓存并调用
  `rebuildEditorPeaks()`。本阶段没有把 Move/Trim 接入该旧连续 `AudioDocument`
  路径，避免把“无 Peak 重建”的合同伪装成可用 UI；Phase 4 迁移文档真源后再建立
  控制器入口。

### 修改文件

- `core/src/audio_editor/event_timeline.hpp/.cpp`：添加事务式移动/修剪及私有替换。
- `core/src/audio_editor/timeline_edit_command.hpp/.cpp`：添加小型、可逆的 Move/Trim
  命令。
- `core/CMakeLists.txt`、`tests/CMakeLists.txt`：编译命令实现并注册测试。
- `tests/core/timeline_edit_command_test.cpp`：覆盖元数据不变、Source 共享、锚点、
  排序、相邻边界、单帧、碰撞、未知 ID、no-op、溢出、revision 与
  execute→undo→execute。

### TDD 与验证证据

**RED（预期失败）**

```powershell
cmake --build --preset windows-msvc-release --target timeline_edit_command_test --parallel 4
```

在实现前，测试以预期原因失败：`fatal error C1083: cannot open include file:
"audio_editor/timeline_edit_command.hpp"`。

**GREEN（通过）**

```powershell
ctest --test-dir build/release -R "^timeline_edit_command_test$" --output-on-failure
ctest --test-dir build/release -R "^audio_editor_controller_test$" --output-on-failure
ctest --test-dir build/debug -R "^(timeline_edit_command_test|audio_editor_controller_test)$" --output-on-failure
```

- Release：新命令测试 `1/1` 通过；既有控制器回归测试在先生成 `decoder_fixture`
  后 `1/1` 通过。
- Debug：两项聚焦测试 `2/2` 通过。
- Release 与 Debug 完整构建目标均已完成；`git diff --check` 通过。

### 已知失败、未验证项与回退点

- 定向构建未自动生成控制器测试的 `decoder_fixture`，CTest 会向不存在的 fixture
  路径传值并使既有控制器测试退出 10；先构建 `decoder_fixture` 后测试通过，未修改
  生产或测试逻辑。
- 在另一个并发 Ninja 访问同一 build 目录时可见 `premature end of file` 警告和短暂
  的 EXE 文件锁；后续验证已串行执行。该环境竞争不改变 Phase 3 代码。
- 未运行完整 CTest、真实编辑播放、UI、硬件、离线多 Event 导出或性能矩阵；它们不
  属于本阶段门禁。
- 回退点：`421c39f`（Phase 3 提交前 HEAD）。

### 审查修正（round 2）

- 已撤回提前暴露到 `AudioDocument` 和 `AudioEditorController` 的 Move/Trim 入口：
  现有播放、保存、导出与波形仍读取传统 `DocumentSnapshot`；局部接入会造成显示时长
  与实际音频分叉，违反单一真源。
- Phase 3 的已批准验收面仅为 `EventTimeline` 的核心命令。它不依赖 decoder、encoder
  或 Peak 构建；碰撞和 stale execute/undo 都以完整快照与 revision 验证事务回滚。
- Phase 4 的首要门槛是迁移 Document/Controller 的所有调用方至单一 EventTimeline
  真源，然后才能公开 Move/Trim UI 或控制器入口；在此之前控制器测试仅作为既有回归。

## Phase 5 — 差量 Undo/Redo 与轻量 `.agproj`（2026-08-20）

### 需求追踪

- `P5-HIST-01`（规格 6 / 17.5，Task 5 ruling 1–5）：`AudioDocument` 持有唯一
  `EventTimeline` 与有界历史；命令只保留受影响 Event 元数据，默认 256 条 / 1 MiB，
  支持失败事务、Redo 失效、最旧优先淘汰及显式 Move/Trim 手势合并。
- `P5-PROJ-01`（规格 13，Task 5 ruling 6–7）：schema v1 通过 `QSaveFile`
  原子保存 Source、Event、Marker、Selection、Playhead、Viewport 与 Export Settings；
  64 位整数使用十进制字符串，不保存 PCM、Peak、Decoded、Render、Cache、Handoff、
  Clipboard 或临时路径。
- `P5-PROJ-02`（Task 5 ruling 8–9）：加载先完整验证再替换活动文档；相对路径同时做
  词法与 canonical/junction 边界检查；Missing/IdentityMismatch 保持离线，完成 Relink
  前禁止播放和导出；保存点、Undo/Redo 与非历史工程状态共同决定 `modified`。
- `P5-PROJ-03`（轻量性与输入稳健性）：工程 JSON 最大 16 MiB；Sources、Events、
  Markers 各最多 4096；Envelope 总点数最多 65536，单 Event 仍受 64 点域上限约束。
  文件大小先于 `QJsonDocument` 分配检查，集合与 Envelope 预检先于 Source/Event 域对象
  分配及媒体 probe。单独 probe 默认 2 秒；工程加载的全部 probe 共享 5 秒绝对 deadline，
  FFmpeg interrupt callback 可中断单个超时源。
- `P5-CTRL-01`（Task 5 ruling 9–10）：Controller 提供工程保存/打开/Relink、Undo/Redo、
  dirty、Playhead 与 Viewport seam；工程保存不进入音频编码路径；本阶段不改 QML。

### 提交与关键文件

- `b52a41c` `feat(editor): add bounded undo and agproj persistence`：
  `timeline_undo_stack.*`、`timeline_edit_command.*`、`audio_document.*`、
  `project_document.*`、Controller 与三组 Phase 5 测试。
- `a5071d1` `fix(editor): close project persistence review gaps`：稳定 Source ID、
  Source identity、保存点 dirty、Discard gate、Export Settings seam 与旧 `saveAs` 合同。
- `ae3a0fc` `fix(editor): harden project validation and viewport sync`：
  `audio_source_probe.*`、Project 验证、尾部删除/剪切后的 Viewport 同步及空工程状态。
- `a0086f8` `fix(editor): enforce project offline and state invariants`：canonical 路径边界、
  unresolved Source 媒体门禁，以及非历史工程状态保存点。
- 最终门禁修复集（本次提交）：`audio_source_probe.*`、`timeline_edit_command.cpp`、
  `project_document.cpp`、Controller、对应四组测试与本记录。它加入工程资源预算、
  Move/Trim 单 Event 热路径、持久状态 savepoint、modified clear gate 及当前引用源离线门禁；
  未新增线程、运行时依赖或 QML 修改。

### TDD RED / GREEN 与验证证据

**RED（资源预算实现前）**

- Release `project_document_test` 成功编译，但 CTest 为 `0/1`，测试进程退出码 5；新增的
  超大文件、Sources、Events、Markers 与 Envelope 总量五类输入均未得到资源上限错误。
- Release `audio_source_probe_test` 构建按预期失败：C2660（缺少 deadline 重载）与
  C2039（缺少 `timed_out` 结果字段）。

**GREEN（本轮聚焦范围）**

```powershell
cmake --build build/release --target project_document_test audio_source_probe_test --parallel 4
ctest --test-dir build/release -R "^(project_document_test|audio_source_probe_test)$" --output-on-failure
# 2/2 passed（最终复跑：project 1.03 s，probe 0.05 s）

cmake --build build/debug --target project_document_test audio_source_probe_test --parallel 4
ctest --test-dir build/debug -R "^(project_document_test|audio_source_probe_test)$" --output-on-failure
# 2/2 passed（最终复跑：project 1.47 s，probe 0.05 s）
```

- 稳定共享树最终验证：Phase 5 聚焦矩阵 Release `7/7`（3.88 s）、Debug `7/7`
  （6.30 s）；完整构建 Release `337/337`、Debug `71/71`，均 exit 0。Release 构建仍出现
  仓库既有的 `premature end of file; recovering` 警告，随后完整重建并链接成功。
- Release `AgPlayer.exe --qa-test-mode` 以隐藏窗口启动，精确工作树路径的进程保持存活
  5 秒，再按 PID 停止。这只是启动/部署 smoke，不是 UI、音频或硬件验收。

### 未验证项、验收边界与回退点

- 未运行完整 CTest、真实 `.agproj` 多源长延迟/网络源矩阵、UI 交互、真实播放/导出、
  音频硬件或完整 Phase 5 acceptance；上述 7/7 聚焦测试、双配置完整构建和启动 smoke
  不代表完整产品验收。
- 5 秒工程预算已由共享绝对 deadline 与 FFmpeg interrupt callback 实现；真实慢盘、
  UNC、损坏大媒体上的墙钟上界仍需独立系统测试。`QFileInfo` 等文件系统元数据调用不受
  FFmpeg callback 中断。
- 最终门禁修复集的回退点为 `a0086f8`；若回退本次提交，会同时移除资源预算、共享 probe
  deadline、Move/Trim 热路径、persisted-state dirty、modified clear gate 与引用源 issue 同步。

### Final gate closure — save symmetry and offline continuity (2026-08-22)

- `ProjectDocument::save()` now enforces the same 4,096 Sources/Events/Markers,
  65,536 aggregate Envelope points and 16 MiB final JSON limits as load before
  atomic output. A rejected save leaves the prior project intact and does not
  cause controller clean-state transition.
- Project probe timeouts and a consumed shared five-second budget now yield
  `Unavailable` source issues without aborting transactional project open;
  subsequent existing sources are not probed beyond budget and remain eligible
  for relink. QML issue kind is `unavailable`.
- Polling playback and stopping playback share the persisted-playhead update
  path used by frame seek, preserving the Phase 5 persisted-dirty contract.
  Load, save and relink reject the reserved `UINT64_MAX` source ID.
- Strict RED evidence covered resource-overrun saves, project budget exhaustion,
  reserved-ID load/save/relink and playback dirty tracking. GREEN evidence:
  final Release focused Phase 5 matrix 7/7 (9.34 s) and Debug 7/7 (12.35 s). Release
  and Debug full builds passed (Release 337/337; Debug 43/43 incremental graph).
  Release app smoke remains intentionally delegated to the final lead gate; no
  full CTest, real UNC/slow-media, hardware or UI acceptance is claimed here.

### Final independent-review P2 closure (2026-08-22)

- Selection playback positioning now calls the persisted playhead update before
  fallible preview setup, so a failed preview still updates `modified` and
  `documentChanged` according to the established Phase 5 savepoint rule.
- Source IDs are allocated from the lowest free valid value in
  `1..UINT64_MAX-1`; recording reserves its ID and checks the active 4,096
  source budget before document insertion.
- Controller source records and offline issues keep current plus bounded
  Undo/Redo-reachable sources. That reachability boundary preserves an Undo
  restore's original source ID, then releases stale entries after history
  eviction. The controller test covers `Unavailable → Relink → issue clear`.
- RED: Release project/controller targets failed 0/2. GREEN: Release focused
  matrix passed 7/7 in 15.96 s; Debug passed 7/7 in 20.47 s. Full Release build
  passed 337/337 (known Ninja recovery warning), and Debug passed 76/76. App
  smoke, full CTest, actual recording hardware, UI and slow/UNC source stress
  remain deliberately unverified.

### Clipboard reachability P2 closure (2026-08-22)

- Source-record/issue compaction now includes source pointers held by the
  document clipboard as well as current timeline and bounded Undo/Redo deltas.
  This is required because Paste can reintroduce an otherwise expired source.
- A real IdentityMismatch source was Cut, its Cut command evicted by 257 edits,
  then pasted. RED observed no issue and an enabled export action; GREEN
  retains source ID 2, restores `identityMismatch`, and keeps export disabled.
- Final focused matrix: Release 7/7 in 15.80 s; Debug 7/7 in 20.52 s. Full
  builds passed Release 337/337 (known Ninja recovery warning) and Debug 68/68.
  App smoke, full CTest, recording hardware and long-running source stress
  remain deliberately unverified.

## Phase 6 — Precise reference timeline UI (2026-08-22)

### Requirement traceability

- `P6-MAP-01`: `EditorViewport::panByPixels`, anchored zoom, selection,
  ruler, hit/event geometry, scrollbar and exact playhead share the frame/pixel
  mapping seam. Long-file and both-clamp tests pass in Release and Debug.
- `P6-EVENT-01`: QML receives decimal-string Event IDs and minimal event/source
  boundaries; Move/Trim/duplicate gestures explicitly begin/end and coalesce
  to one bounded-history item, including ID `9007199254740993`.
- `P6-PEAK-01`: visible peaks follow EventTimeline/source intervals, preserve
  blank gaps, reject stale async generations, and remain bounded to two points
  per channel/logical pixel. The Scene Graph item uses one geometry buffer and
  exposes `generatedPointCount()` for non-QML verification.
- `P6-UI-01`: the approved 1672x941 geometry, exact navigation/toolbar order,
  inspector A–E groups and honest later-phase disabled states are tested.
  Obsolete Overview/transport/double-inspector QML and registrations are gone.
- `P6-RESP-01`: 1280 retains a scrollable inspector; 880 retains scrollable
  main content plus complete narrow Play/Pause and inspector entries. The QML
  test opens the inspector, scrolls through E, and checks Export's mapped bounds.
- `P6-QA-01`: Release/Debug screenshots use a real WAV at all three sizes;
  same-size source/candidate images and visible difference masks are recorded
  under `build/qa/phase6` and summarized in `design-qa.md`.

### TDD and validation evidence

- RED: missing viewport/waveform/controller seams failed before production;
  QML then reproduced the removed `visibleEndRatio`, false empty-session dirty
  modal, stale Export binding, and incomplete 880 first-viewport playback
  evidence. Logs are indexed in `task-6-report.md`.
- GREEN: final Phase 6 focused CTest is Release 5/5 and Debug 5/5. Full builds
  passed Release 334/334 and Debug 102 steps. Release QA screenshot launch
  loaded the real WAV and exited 0. `git diff --check` passed.
- Full Release CTest is 82/83 with only the tracked main-player
  `waveform_item_test` 0.020-vs-0.012 contract mismatch. Debug is 78/84; its
  non-Phase-6 list and isolated reruns are recorded in `task-6-report.md`.

### Visual acceptance and remaining boundary

- P0 false discard modal, P1 stale Export action, P1 narrow playback
  reachability, and executable P2 styling/access differences were fixed.
  Final Release images are `release-smoke` 1672, `release-final2` 1280, and
  `release-final3` 880; Debug equivalents are `debug-final` and `debug-final3`.
- The final 1672 Release comparison/mask is
  `build/qa/phase6/comparisons-final/release-editor-1672x941-*`.
  Its numeric difference is dominated by honest real-sine data and the absence
  of reference-only demo/later-phase values; no executable visual P0/P1/P2 remains.
- Debug screenshot cleanup still faults in Qt6Cored (`0xc0000005`, offset
  `0x7d97a`); the final 880 capture used Qt's existing software RHI after two
  default-RHI `grabWindow()` stalls. No packaging, hardware recording, or
  Phase 7+ DSP acceptance is claimed.

### Phase 6 final-review correction (2026-08-22)

- Phase 6 now exposes explicit false capabilities for recording, BPM,
  speed/pitch, playback, and export. The page keeps those reference controls
  visible but disabled and reads E from persisted project export settings;
  no device, preview, BPM, export, or writable QML shadow state is fabricated.
- The editor-owned player and old DocumentRenderer preview/BPM/export path were
  removed. Visible source slices stream decoder blocks directly into bounded
  peak buckets with generation cancellation between reads; Scene Graph point
  budgeting is total across stereo channels.
- Playhead, Move, and Trim use local drag candidates and commit once on release.
  Split's button selects scissors mode, while `S`/`Ctrl+B` split. One page-owned
  Space shortcut remains, real wheel events prove `1.25`/`0.8`, and shared
  viewport APIs own ruler/scrollbar mapping.
- Final visual P2 correction makes unsupported recording and playback controls
  visibly gray/low-opacity, including the 880 Play entry. Release and Debug
  final4 matrices at 1672x941, 1280x720, and 880x560 all exited zero; comparison
  and masks are under `build/qa/phase6/comparisons-review-final4`.
- Final focused matrices passed 5/5 in Release (12.25 s) and Debug (16.33 s).
  Full builds passed (Release recovered graph 334/334, Debug incremental 4/4).
  Full CTest remains Release 82/83 and Debug 79/84 with the exact unrelated
  baseline failures recorded in `task-6-report.md`. `design-qa.md` is passed
  only after manual review of the final4 source/candidate images and masks.
