# AgPlayer Integrated 单窗口主题需求追踪

基线：`514816053dd6e201a176b00f2227f269aeec7a8e`  
分支：`codex/integrated-single-window-theme`

## 架构约束

| 要求 | 实现位置 | 验证 |
|---|---|---|
| 一个播放器核心，两套按需 Shell | `Main.qml` 的单一 `LibraryFilterModel`、`WaveformSession` 与 Shell `Loader`；`main.cpp` 按模式创建/销毁 `ListWindow` | `integrated_shell_lifecycle_smoke_test` |
| 独立 Shell 设置，不复用明暗主题 | `SettingsController.playerShellMode`，仅接受 Classic=0 / Integrated=1 | `settings_controller_test` |
| Classic 默认且行为保留 | 默认/非法值回退 Classic；Classic 时重建并注册列表窗 | `settings_controller_test`、`window_controller_test`、生命周期 smoke |
| Shell 间状态共享 | 搜索、标签搜索、当前模型与波形会话由 `Main.qml` 持有，不归 Shell 所有 | `integrated_shell_contract_test`、生命周期 smoke |
| 窗口几何独立 | `WindowController` 使用 Classic/Integrated 独立键；Integrated 1672×941，最小 1180×720 | `window_controller_test`、分辨率截图矩阵 |
| 不新增播放器/缓存/标签模型/依赖 | 复用现有 PlaybackController、TagModel、WaveformProvider、DocumentRenderPipeline | Release DLL 集合与基线完全一致 |

## UI 与交互

| 要求 | 实现 | 验证 |
|---|---|---|
| 52px 顶栏、248px 左栏、312px 右栏、120px 波形、83px 底栏 | `IntegratedPlayerShell.qml` | 1672×941 视觉对比；1280×720、1440×900 截图 |
| Integrated 左栏隐藏标签入口 | `SideNavigation.showTagManagementEntry=false` | `integrated_shell_contract_test`、截图 |
| 共用歌曲列表、搜索与标签面板 | 直接实例化 `TrackList`、`SearchFilter`、`TagManagementPanel` | `integrated_shell_contract_test` |
| 右侧标签紧凑、独立滚动、默认展开、可折叠 | `TagManagementPanel.compact/collapsible/expanded` | 视觉截图、布局契约测试 |
| 共享播放栏主题切换按钮 | `PlayerControls.shellModeSwitchButton` | 生命周期 smoke |
| 不增加图片/字体/图标框架 | 仅使用现有资源与 Theme token | Git diff、Release DLL 对比 |

## 波形、选区与拖出

| 要求 | 实现 | 验证 |
|---|---|---|
| 共享波形加载与防过期逻辑 | `WaveformSession.qml` 同时供 Classic/Integrated 使用 | `waveform_provider_test`、播放截图 |
| 可见时间范围和光标锚定缩放，最大 8× | `WaveformItem.visibleStartMs/visibleEndMs/zoomAt()`；顶层 `WaveSelectionOverlay` 转发 Ctrl+滚轮 | `waveform_item_test`、`waveform_coordinate_mapper_test`、`integrated_shell_contract_test` |
| 100ms 最小选区、首次框选 seek+播放+循环 | `PlaybackController` selection API 与 `WaveSelectionOverlay.qml`；精确时长缩短后重新夹紧或清除 | `playback_controller_test` |
| 选区外 seek 关闭循环但保留选区；换曲清除 | `disableSelectionLoopAndSeek()` 与曲目变更处理 | `playback_controller_test` |
| 不新增高频 Timer | 复用既有播放快照轮询做循环边界回跳 | 代码审查 |
| 越过系统拖动阈值后才导出 | `PlaybackClipDragAdapter` + `SelectionDragController` | `selection_drag_controller_test` |
| 24-bit PCM WAV、源采样率/声道、帧精确裁切、本地 URL | 复用 `HandoffAssetManager`/`DocumentRenderPipeline`，输出到 `AgPlayer Drag Clips` | `selection_drag_controller_test` |
| 导出不占用播放 Decoder | 每次请求构造短生命周期渲染请求，不接触 `PlaybackDecoder` | 代码审查、控制器测试 |

## 明确排除

- 未制作安装包。
- 未引入第二播放器、第二标签模型、第二波形缓存、常驻导出器或新转码管线。
- Windows 外部应用拖放兼容性需要在装有目标应用的交互式桌面上继续人工验收。
