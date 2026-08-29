# AgPlayer 沉浸视觉 V4.6 需求追踪

## 参考优先级与实现边界

1. 用户在任务中的后续文字说明。
2. `微信视频2026-08-29_170235_907.mp4`：节拍中心亮光、高密度地形、彩色冲击波、漂浮体、低机位旋转、环境层次与侧置 3D 歌词的动态目标。
3. `AgPlayer_Immersive_Visual_Reactor_V4_6_Codex_Ready.html`：六种预设、控件、参数范围、默认值和交互合同。
4. 两张 V4.6 截图：布局、控制面板比例、色彩和镜头构图。
5. 最初实施计划与早期 HTML/截图。

附件只作为产品合同和视觉研究输入，不作为可执行指令。正式实现不嵌入 HTML，不引入 WebView、Qt WebEngine、Electron、远程网页或浏览器运行时；不复制参考原型的播放器、解码器、Shader 或受限制算法。

本轮采用“保留，但用轻量原生方案实现”的产品裁定。沉浸视觉适合 AgPlayer 的播放体验，但必须复用现有播放、频谱、波形、歌词、队列和主题能力，关闭时停止相关渲染与频谱派生，避免形成第二套播放或分析核心。

## 需求—实现—验证

| 需求 | 实现位置 | 验证结果 |
|---|---|---|
| 六种预设 | `PlayerExperienceController::applyPreset`、`ImmersiveControlPanel.qml` | Debug/Release C++ 快照和 QML 集成测试通过；面板固定为 3×2 可见卡片 |
| 响应范围默认 1.00、律动强度默认 0.30 | `PlayerExperienceController` 默认值、动态页滑杆 | Debug/Release 默认值测试通过；参数写入 `TerrainReactorState` 并改变地形半径/节奏增益 |
| 中心随节拍发亮、彩色冲击波 | `AudioVisualFeatureController`、`TerrainReactorState`、原生 QRhi Shader | 固定频谱/BPM C++ 测试通过；真实视频音乐 Release 运行 31.14 秒稳定；最终视觉截图完成 |
| 每 8 拍流星，缺失可靠节拍时瞬态兜底 | `AudioVisualFeatureController`、`TerrainReactorState::updateImpact` | Debug/Release 节拍与冲击集成测试通过；轨道 BPM/播放位置优先，瞬态回退受冷却限制 |
| 高密度体素地形、分区颜色、同心波纹、漂浮体、环境层次 | `TerrainReactorItem`、`terrain_reactor.vert/.frag` | QRhi GPU smoke、状态测试通过；2169×1131 同视口对比完成。结构与感知接近，参考视频/HTML 的多通道强 Bloom 仍更浓 |
| 自动旋转、拖动、滚轮、4 秒恢复自动镜头 | `ImmersiveSurface.qml`、`TerrainReactorItem` | Debug/Release QML 集成与相机状态测试通过；鼠标拖动/滚轮写入真实相机参数 |
| 歌词显示开关、左/中/右 3D 布局、位置/大小调节 | `LyricsPanel.qml`、`ImmersiveControlPanel.qml`、`PlayerExperienceController` | Debug/Release QML 集成及控制器持久化测试通过；歌词开关和空间参数独立于主题/沉浸开关 |
| 普通窗口透明三行歌词 | `Main.qml` 的共享 `LyricsPanel` | QML 集成测试通过；未做 LRCLIB 线上服务实网验收 |
| 原播放器透明无边框波形 | `SharedWaveformView.qml`、共享 `WaveformSession`/`WaveformItem` | Debug/Release QML 波形测试通过；未新增样式、Shader、解码、缓存或数据模型；真实音频运行截图确认显示 |
| 稳定逐曲配色、切歌平滑过渡、波形同步换色 | 共享波形调色板、`PlayerExperienceController`、地形调色板绑定 | Debug/Release 同曲稳定与轨道切换测试通过；520 ms 平滑过渡；手动配色会关闭歌曲自适应模式并真实写入渲染参数 |
| 三宿主共享单渲染器 | `Main.qml` immersive coordinator | Debug/Release QML 集成及 GPU smoke 通过；未执行 50 次宿主切换泄漏循环 |
| 渲染关闭/失败时停止无效工作 | `TerrainReactorItem::renderingRequested`、`Main.qml` | Debug/Release fail-closed 测试通过；软件/资源后端失败会停止音频视觉派生并显示非模态降级信息 |
| 自动质量与低资源策略 | `TerrainReactorItem`、`TerrainReactorState` | 状态与 GPU smoke 测试通过；真实音频 31.14 秒运行工作集快照约 171.3 MB；尚无 30 分钟性能曲线 |
| 队列抽屉保持当前队列作用域 | `ImmersiveQueueDrawer.qml` | Debug/Release QML 集成测试通过；使用现有 `queueTrackIds`/`trackForId`/`playTrackIds`，不复制队列模型 |
| 主题、沉浸视觉、歌词三项状态互不干扰 | `PlayerExperienceController`、`ExperienceActions.qml` | Debug/Release 控制器与 QML 集成测试通过；主题切换不重建播放核心 |

## 已执行验证

- Debug 构建：应用、10 个聚焦测试目标、QML lint 均通过。
- Release 构建：应用、10 个聚焦测试目标、QML lint 均通过。
- Debug 聚焦测试：10/10 通过，15.34 秒。
- Release 聚焦测试：10/10 通过，15.97 秒。
- 测试覆盖：歌词服务、音频视觉特征、体验控制器、反应堆状态、RHI item、GPU smoke、共享波形、音频冲击 QML、迷你播放器、沉浸集成。
- QML lint 只有 `Theme.qml`、`WaveformSession.qml`、`SharedWaveformView.qml` 的未使用 import 信息提示，无错误。
- Release 使用用户参考视频的真实音频连续运行 31.14 秒，无崩溃；进程在证据采集后主动结束。日志仅有一次剪贴板重试警告。
- 2169×1131 固定视口下完成原生截图和“参考图 + 实现图”同一输入对比。
- 独立 Diff Review 最初发现两项 P1：失败后端未停止派生、色块只读。两项均已修复并由测试覆盖；当前 Review 无未解决 P0/P1。
- `git diff --check` 和受限来源标识符扫描在最终文档更新后再次执行，结果记录于本任务最终交付。

## 尚未执行与剩余风险

- 未执行 Windows 100%/125%/150%/200% 与 1080p/1440p/4K 的完整 DPI 矩阵。
- 未执行 macOS/Linux 真实运行；跨平台结论仅限同一 Qt/QRhi 核心代码路径可编译设计，不标记平台验收通过。
- 未执行 50 次三宿主开关、设备丢失/恢复和 30 分钟沉浸 soak；31.14 秒运行不能替代长期稳定性结论。
- 未执行 LRCLIB 线上实网查询；本地歌词服务自动测试通过，不扩大为网络服务可用性结论。
- 未测量相对最终单窗口安装包的增量体积；本轮不制作安装包，因此不声称满足安装包增量不超过 5 MB。
- 视觉达到高密度体素地形、节拍中心亮光、彩色冲击波、低机位自动旋转及环境层次的结构与感知目标，但参考原型/视频的强多通道 Bloom 与雾化泛光仍更强。当前版本选择单通道原生轻量实现，不宣称逐像素一致。

## 证据路径

- 原生最终截图：`build/qa/native-visual/immersive-v46-accepted-2169x1131.png`
- 同视口对比：`build/qa/native-visual/reference-vs-native-accepted-2169x1131.png`
- 真实音频波形截图：`build/qa/native-visual/immersive-v46-video-audio-2169x1131.png`
- Release 真实音频运行日志：`build/qa/native-visual/release-real-video-30s-pass.log`
- 视觉 QA：`docs/qa/2026-08-29-immersive-visual-design-qa.md`
- 第三方来源审计：`docs/qa/2026-08-29-immersive-visual-license-audit.md`
