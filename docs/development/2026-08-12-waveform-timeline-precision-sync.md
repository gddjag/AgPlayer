# AgPlayer 波形时间轴精准同步系统升级开发记录

## 基本信息

- 日期：2026-08-12
- 工作树：`D:\ai\AgPlayer\.worktrees\revised-ui`
- 分支：`codex/revised-ui`
- 原始需求：`C:\Users\Administrator\Desktop\AgPlayer_波形时间轴精准同步系统升级需求.md`
- 状态：实现完成；第三方播放器对照结论待独立同机测量

## 实现记录

- 新增 `WaveformCoordinateMapper`，统一时间、真实 PCM sample、peak、pixel 双向映射。
- `WaveformItem` 在 GUI 线程的几何变更阶段发布实际 `renderWidth`；游标、点击、Hover、已播放 clip 均调用同一 mapper。
- C++ 提供 `waveformCursorX`，QML 只显示，不再自行计算播放比例。
- `PeakSnapshot` 保存 `totalSamples`、`sampleRate`、`peakCount` 和 peaks；数据来自真实解码帧数，不由毫秒近似反推。
- Core C API 新增真实 waveform sample 元数据读取；WaveformCache v2 以兼容 flags 扩展保存该元数据。旧缓存仅升级重建一次。
- resize 只重映射 Scene Graph 顶点；PeakSnapshot 不替换、不重新分析音频、不重建缓存；顶点数量不变时复用 node、vertex 与重采样缓冲区。
- 主播放器与迷你播放器均透传完整 PeakSnapshot 元数据，并以同一 `waveformCursorX` 裁剪已播放层、定位 1 px 游标。

## 需求证据矩阵

| 需求 | 实现/验证证据 | 状态 |
|---|---|---|
| 唯一 WaveformCoordinateMapper | `waveform_coordinate_mapper.*`；禁用公式扫描无命中 | 通过 |
| 时间↔Pixel ≤0.5 px | 0/60000/150000/299999 ms × 500/1000/3840 px 表驱动测试 | 通过 |
| 时间→Sample→Peak→GPU→Pixel | Analyzer 真实帧数→C API→Cache→Provider→PeakSnapshot→Mapper→QSGGeometry | 通过 |
| GPU 自适应渲染 | Qt 6 Scene Graph `QSGGeometry`；resize 复用 node/vertex storage | 通过 |
| 游标统一映射 | `waveformCursorX` + QML 1 px guide | 通过 |
| 点击 Seek 统一映射 | `timeForX()`→`pixelToTime()`；真实 PlaybackController seek | 通过 |
| 已播放颜色统一映射 | `playedWaveformClip.width === waveformCursorX`，误差 ≤0.5 px | 通过 |
| resize 不重新分析/缓存 | resize 测试保持原 peaks/PeakSnapshot；Provider 不参与 resize | 通过 |
| 800×500 / 1920×1080 / 3840×2160 | QML 窗口级三尺寸回归 | 通过 |
| 100% / 125% / 150% DPI | 物理像素测试；125%/150% Qt Scale Factor 真实 SceneGraph 播放截图 | 通过 |
| 多显示器 | Windows 检测两屏；QQuickWindow 在每个 QScreen 上验证物理像素边界 | 通过 |
| 不增加 CPU/播放负担 | 300 次 resize Debug 构建 <1 s；顶点内存复用；真实解码播放成功 | 通过预算 |
| 不影响现有架构 | Qt6/QML+C++17、FFmpeg、WaveformCache、PeakSnapshot 均保留 | 通过 |
| 超越 AIMP/foobar2000/rekordbox | AgPlayer 自身误差门槛 ≤0.5 px 已达成；第三方尚未以同文件/同设备量测 | 待独立对照 |

## TDD 与验证记录

1. RED：Mapper 测试因缺少类型失败；GREEN：新增 mapper 后通过。
2. RED：Provider 缺少 `_sampleRate/_totalSamples/_peakCount`；GREEN：真实解码元数据链路通过。
3. RED：PeakSnapshot 无可观察元数据；GREEN：Item 保留并暴露只读属性。
4. QML 主窗口：59 passed，1 个既有无关媒体库摘要卡失败，1 skipped；全部波形用例通过。
5. 相关 CTest：`waveform_cache_test`、`playback_controller_test`、`waveform_coordinate_mapper_test`、`waveform_item_test`、`waveform_provider_test`、`qml_mini_player_test`，6/6 通过。
6. 真实 QA：Debug AgPlayer 通过 FFmpeg 解码并播放 `sine-440hz.wav`，生成 100%、125%、150% SceneGraph 截图；游标与颜色边界重合。

## 已知非本次问题

- `tst_main_window.qml::test_library_manager_summary_cards_are_real_filters` 仍失败，与波形升级无关。
- `MiniPlayerControls.qml` 既有 `expanded is not defined` 警告，与本次升级无关。

## 封装决策

- 扩展现有代码与测试：已完成。
- 新建 Skill/子代理/自动化：跳过；这是一次性架构升级，无两次以上重复证据，现有测试工具已覆盖。

## 2026-08-12 复测后追加修复

用户在真实歌曲复测时确认鼓点与进度线仍有明显距离。本轮继续按原需求的精度门槛排查，定位并修复三项独立根因：

1. `WaveformItem::resampleValues()` 旧降采样把余数全部分配在前段。2000 个 peak 映射到 600 个顶点时，前 200 桶每桶 4 点、后 400 桶每桶 3 点，造成非线性时间压缩；位于源数据正中间的脉冲被绘制到 `x=266.444`，而正确位置是 `x=300`，偏差 33.556 px。现改为按 `index * sourceCount / targetCount` 计算每个桶的全局均匀边界。
2. 主/迷你播放器曾优先使用容器元数据时长，且 `applyWaveformDuration()` 不会把完整 PCM 分析时长应用到播放核心。编码器延迟、尾部 padding 或 VBR 估算会把整条波形拉伸/压缩。现以完整解码 PCM 的 `_durationMs` 为权威时间轴，同步写回播放核心；容器时长仅在分析完成前作回退。
3. 导入器曾忽略音频文件内有效 BPM 标签并直接分析；界面又把 127.5 强制显示为 128。现优先采用 20–400 范围内的嵌入式 BPM，缺失/无效时才分析，并在主面板、歌曲列表、媒体库详情中保留一位有效小数。
4. 旧媒体库条目无需删除重导：当前歌曲波形分析完成后，优先重读文件 BPM 标签；无标签且启用自动 BPM 时采用本次分析值，并持久化刷新 `BpmRole`。

### 新增回归证据

- RED→GREEN：中段脉冲降采样像素由 `266.444` 修正到目标中点 `300 ± 1 px`。
- RED→GREEN：完整 PCM 时长 `1875 ms` 现在覆盖容器时长 `2000 ms`，播放 snapshot、Seek、波形共用同一时轴。
- RED→GREEN：带 `127.50 BPM` 标签的 MP3 导入后精确得到 `127.5`；界面显示 `127.5 BPM`，不再丢失小数。
- 新增 8 秒、120 BPM 点击轨端到端测试：16 个 500 ms 节拍的 waveform peak 均落在目标时间 `±14 ms` 内；分析 BPM 为 `120 ±1`。
- 原速证据：`audio_engine_test` 与 `queue_gapless_test` 通过；播放设备跟随曲目原采样率，位置由实际渲染帧数/曲目采样率计算，无 tempo/rate 处理。
- 核心关联 CTest：`library_model_test`、`bpm_analyzer_test`、`audio_engine_test`、`queue_gapless_test`、`import_controller_test`、`playback_controller_test`、`waveform_coordinate_mapper_test`、`waveform_item_test`、`waveform_provider_test`，9/9 通过。
- 定向 QML 回归：主窗口波形/Seek/尺寸/BPM 8/8 通过；迷你播放器精确时长/clip 4/4 通过。
- Debug 与 Release `AgPlayer` 完整构建通过。最新 Release 经正常导入/播放路径完成真实运行 smoke：进程正常退出、日志无 WARN/ERROR/FATAL、主窗口截图 37179 bytes。未生成安装包。
- Debug 构建在自动截图退出清理阶段出现 Qt6Quickd 访问冲突；Release 同路径通过，属于 Debug/Qt 清理层遗留，未作为发布通过证据。

### 本轮封装判断

- 扩展现有测试：已完成；同一波形链路已出现两轮实测反馈，新增节拍轨、降采样像素、精确时长与 BPM 回归最轻量且可持续。
- 新建 Skill/子代理/自动化：跳过；现有 CTest/QML QA 已充分覆盖，另建工具会重复。
- 待更多证据：第三方播放器同文件、同硬件的量化对照；当前没有可复现实测数据，不作超越性结论。
