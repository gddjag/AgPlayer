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
| 高密度体素地形、分区颜色、同心波纹、漂浮体、环境层次 | `TerrainReactorItem`、`terrain_reactor.vert/.frag`、`ImmersiveSurface.qml` | QRhi GPU smoke、状态测试通过；外围静态颗粒按响应场衰减，中央使用连续簇状峰场与稳定随机峰混合；窗口/全屏增加 Qt 自带 `MultiEffect` 轻量柔光，桌面 Eco 主动关闭该层 |
| 自动旋转、拖动、滚轮、4 秒恢复自动镜头 | `ImmersiveSurface.qml`、`TerrainReactorItem` | Debug/Release QML 集成与相机状态测试通过；鼠标拖动/滚轮写入真实相机参数 |
| 歌词显示开关、左/中/右 3D 布局、位置/大小调节 | `LyricsPanel.qml`、`ImmersiveControlPanel.qml`、`PlayerExperienceController` | Debug/Release QML 集成及控制器持久化测试通过；歌词开关和空间参数独立于主题/沉浸开关 |
| 普通窗口透明三行歌词 | `Main.qml` 的共享 `LyricsPanel` | QML 集成测试通过；未做 LRCLIB 线上服务实网验收 |
| 沉浸底部“频彩波形” | `SharedWaveformView.qml`、共享 `WaveformSession`/`WaveformItem` | 固定使用低频珊瑚红、中频青绿、高频蓝紫的语义混色；透明无边框，仅保留歌名/时间/波形；直接消费原有 mix/bass/mid/high 峰值，不新增解码、FFT、缓存或模型 |
| 全局四种波形与顺序 | `SettingsController`、`SettingsPage.qml`、双窗口/单窗口/迷你播放器共享控制 | 保留数值兼容：0 纯色、1 RGB、2 柱状频谱，新增 3 频彩；循环顺序固定 0→3→1→2→0；纯色仍为新安装默认 |
| 频彩低/中/高颜色自定义 | `SettingsController`、`SettingsPage.qml`、`WaveformItem` | 默认 `#FF647C` / `#3ED6AE` / `#8A7CFF`；设置页可独立修改并一键恢复，持久化/回滚/非法值修复测试覆盖 |
| 稳定逐曲配色、切歌平滑过渡、波形同步换色 | 共享波形调色板、`PlayerExperienceController`、地形调色板绑定 | Debug/Release 同曲稳定与轨道切换测试通过；520 ms 平滑过渡；手动配色会关闭歌曲自适应模式并真实写入渲染参数 |
| 三宿主共享单渲染器 | `Main.qml` immersive coordinator | Debug/Release QML 集成及 GPU smoke 通过；未执行 50 次宿主切换泄漏循环 |
| 渲染关闭/失败时停止无效工作 | `TerrainReactorItem::renderingRequested`、`Main.qml` | Debug/Release fail-closed 测试通过；软件/资源后端失败会停止音频视觉派生并显示非模态降级信息 |
| 自动质量与低资源策略 | `TerrainReactorItem`、`TerrainReactorState` | 状态与 GPU smoke 测试通过；真实音频 31.14 秒运行工作集快照约 171.3 MB；尚无 30 分钟性能曲线 |
| 队列抽屉保持当前队列作用域 | `ImmersiveQueueDrawer.qml` | Debug/Release QML 集成测试通过；使用现有 `queueTrackIds`/`trackForId`/`playTrackIds`，不复制队列模型 |
| 主题、沉浸视觉、歌词三项状态互不干扰 | `PlayerExperienceController`、`ExperienceActions.qml` | Debug/Release 控制器与 QML 集成测试通过；主题切换不重建播放核心 |
| 沉浸视觉作为独立主题窗口 | `ExperienceActions.qml`、`Main.qml`、`ImmersiveWindow.qml`、`WindowController` | 主题动作按经典→单窗口→沉浸视觉循环；进入时临时隐藏播放器与歌曲列表，退出后恢复此前主/迷你窗口和列表偏好；反应堆始终由独立无边框窗口承载 |
| 纯净顶层操作 | `ImmersiveSurface.qml`、`ImmersiveWindow.qml` | 已移除左上角品牌文字和面板文字按钮；右上角保留“返回窗口主题”和全屏图标；Esc 退出全屏的 QML 集成测试通过 |
| 最终 HTML 九项动态参数与开关 | `ImmersiveControlPanel.qml`、`PlayerExperienceController`、QRhi uniform/shader | 输入压缩、音频响应、响应范围、中心高光、律动强度、景深、主体清晰、自动旋转速度、律动灵敏度及爆发/流线开关均写入真实渲染快照；旧地形振幅等重复 UI 已移除 |

## 已执行验证

- Debug：从干净构建目录完成应用全量构建；功能聚焦目标在最终 Diff 后再次验证。
- Release：应用全量构建、11 个功能聚焦测试与 QML lint 在最终 Diff 后再次验证。
- Release 全量 `ctest` 最终复跑：119/121 通过。功能相关 GPU smoke、主题颜色分类、沉浸 QML 集成均通过；剩余失败为当前机器无录音后端的 `audio_editor_controller_test`，以及已有安装版 AgPlayer 正在运行时 Windows 前台激活受限的 `windows_shell_runtime_test`，两项均未标记通过。
- 测试覆盖：设置持久化、频彩波形几何与颜色语义、音频视觉特征、体验控制器、反应堆状态、RHI item、GPU smoke、共享波形、音频冲击 QML、迷你播放器和沉浸集成。
- GPU 冲击高光在 Debug/Release 均通过；迷你播放器测试夹具按“先销毁窗口、再派发延迟事件”的顺序清理，Release 连续三次及最终聚焦回归均通过。
- QML lint 只有 `Theme.qml`、`WaveformSession.qml`、`SharedWaveformView.qml` 的未使用 import 信息提示，无错误。
- Release 使用用户参考视频的真实音频连续运行 31.14 秒，无崩溃；进程在证据采集后主动结束。日志仅有一次剪贴板重试警告。
- 2169×1131 固定视口和固定合成频谱下完成原生截图，并把最终 HTML 截图与原生实现放入同一对比输入检查。
- 独立 Diff Review 最初发现两项 P1：失败后端未停止派生、色块只读。两项均已修复并由测试覆盖；当前 Review 无未解决 P0/P1。
- `git diff --check` 和受限来源标识符扫描在最终文档更新后再次执行，结果记录于本任务最终交付。

## 尚未执行与剩余风险

- 未执行 Windows 100%/125%/150%/200% 与 1080p/1440p/4K 的完整 DPI 矩阵。
- 未执行 macOS/Linux 真实运行；跨平台结论仅限同一 Qt/QRhi 核心代码路径可编译设计，不标记平台验收通过。
- 未执行 50 次三宿主开关、设备丢失/恢复和 30 分钟沉浸 soak；31.14 秒运行不能替代长期稳定性结论。
- 未执行 LRCLIB 线上实网查询；本地歌词服务自动测试通过，不扩大为网络服务可用性结论。
- 未制作安装包。新增柔光只使用项目 Qt 6.7 自带 `QtQuick.Effects`，没有第三方运行时；安装包增量目标需在后续正式打包时测量，当前不声称已完成包体验收。
- 视觉已经降低外围颗粒噪声、增加中央簇状高峰与节奏亮度、强化彩色环带并加入受质量策略控制的柔光层。参考原型/视频的多通道后期和空气雾化仍更强，不宣称逐像素一致。

## 2026-08-30 独立主题窗口与反应堆复核

- 当前修复只发生在 `codex/immersive-visual-lyrics` 隔离工作树，没有触碰正在打包的主线工作区。
- Release 全量构建和 `agplayer_app_qml_qmllint` 完成；QML lint 只有既有未使用 import 信息提示，无错误。
- Release 功能聚焦回归通过：体验控制器、地形状态、地形 RHI item、Direct3D 11 GPU smoke、迷你播放器、沉浸集成、主题颜色分类。
- Release 全量 `ctest` 最终为 119/121；本功能相关的 GPU smoke、主题颜色分类、沉浸 QML 集成及迷你播放器均通过。`audio_editor_controller_test` 在当前机器缺少录音后端时有 3 个录音断言失败；`windows_shell_runtime_test` 在已有安装版 AgPlayer 运行期间无法满足 Windows 前台窗口激活断言。两项限制均保留原始失败结论。
- 新增数值回归保证中心随机种子只负责细节，不再产生孤立高塔；连续空间簇峰负责成组柱体，冲击帧必须在 GPU 实测中形成宽范围增亮。
- 使用固定合成频谱在 1900×939 视口生成原生截图，并将用户最终参考截图缩放到同一 1900×939 后与原生实现放入一张 1900×1878 对比输入复核。后续用户真机试听仍是动态节奏手感的最终验收。

## 证据路径

- 原生最终固定频谱截图：`build/qa/native-visual/immersive-frequency-soft-final-v8-2169x1131.png`
- 同视口对比：`build/qa/native-visual/reference-vs-frequency-soft-final-v8-2169x1131.png`
- 真实音频波形截图：`build/qa/native-visual/immersive-v46-video-audio-2169x1131.png`
- 设置页四波形截图：`build/qa/native-visual/settings-frequency-color-waveform.png`
- Release 真实音频运行日志：`build/qa/native-visual/release-real-video-30s-pass.log`
- 视觉 QA：`docs/qa/2026-08-29-immersive-visual-design-qa.md`
- 第三方来源审计：`docs/qa/2026-08-29-immersive-visual-license-audit.md`

## 2026-08-30 本轮最终复核

- `WindowController` 新增沉浸展示生命周期，窗口切换测试覆盖主窗口、迷你播放器、歌曲列表偏好和返回恢复；没有创建第二播放器或改动播放队列。
- 主地形实例在 CPU 布局阶段裁成半径 84 的圆盘，外围星体独立分布；默认响应半径收拢为 56，默认相机距离调整为 180，并保留滚轮 42–220 的缩放范围，使黑色星空留白和中央反应堆比例更接近最终 HTML。
- 顶面流光只作用于柱体上表面，由既有高频能量、稳定单元相位和时间共同驱动；外圈透明度与亮度渐隐，避免整片方阵和全场发白。冲击波、漂浮体、流星和频彩波形的数据链未改。
- 动态页保留最终 HTML 的 9 个滑杆、8 个开关，并用现有八段频谱派生只读的 Warmth、Brightness、Sharpness、Smoothness、Density 五项反馈；低/高频占比及锐度、平滑度、密度公式按最终原型语义校准，kick 通过 520 ms 轻量衰减包络平滑影响锐度和密度，没有新增解码、FFT 或音频缓存。
- `TerrainReactorItem` 将八段频谱、能量、频谱通量和 kick/snare 作为只读 QML 属性暴露；集成回归通过真实 `TerrainReactorItem` 合成特征验证五项读数，不再由测试直接给面板赋值。`WindowController::showMain/showMini` 在沉浸展示期间只更新退出后的恢复目标，主窗口、迷你播放器和列表继续保持隐藏。
- Debug/Release 功能聚焦回归均为 13/13，QML lint 无错误。Release 全量 `ctest` 为 118/121；`import_controller_test` 隔离复跑 24/24 通过，`windows_shell_runtime_test` 在另一工作树的 AgPlayer 进程出现前曾隔离通过、之后受前台激活冲突限制；与本功能无关的 `audio_editor_controller_test` 仍在当前机器失败，因此不报告全套全绿。
- 最终固定频谱截图为 `design-qa/2026-08-30-immersive-revision-current-f.png`；最终 HTML 与原生实现的同输入对比为 `design-qa/2026-08-30-html-reference-vs-native-final.png`。
