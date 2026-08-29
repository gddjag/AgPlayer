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

## 2026-08-29 单窗口调整追踪

| 调整要求 | 最小复用实现 | 验证 |
|---|---|---|
| 框选后默认循环；任意位置点击播放；右键清除；细 Handle | 复用 `PlaybackController` 选区 API；`WaveSelectionOverlay` 仅增加鼠标语义，2px 可视条保留 14px 命中区 | `qml_integrated_theme_test`、`playback_controller_test` |
| 选区时长与拖出胶囊为半透明毛玻璃白字 | 在既有 `Theme` 中增加四个选区玻璃 token，不引入图形依赖 | QML 主题契约、1672×941 合成对比 |
| 右栏标签/歌词页签和末端收起按钮 | 保持同一个 `TagManagementPanel`；Integrated Shell 只切换内容视图，页签状态由 `Main.qml` 持久持有 | `qml_integrated_theme_test`、Shell 契约、截图 |
| 标签胶囊毛玻璃且文字居中 | 扩展共享标签组件的紧凑样式参数 | 标签布局契约、截图 |
| 波形悬停时间、缩小间距、底部位置导航条 | 复用 `WaveformItem.timeForX()`；公开已有 `setVisibleRange()` 给导航条拖动 | `waveform_item_test`、`qml_integrated_theme_test` |
| 皮肤图标同步两套主题、播放栏按参考布局 | 两套 Shell 共用 `PlayerControls` 和上传的 `player-shell-mode.svg`；上传的 `side-panel-toggle.svg` 用于 Integrated 右栏 | `qml_main_window_test`、生命周期 smoke、截图 |
| 修复 Integrated 波形模式切换后消失 | 根因修复：`WaveformItem.setLayers()` 与 `setPeaks()` 会互相清空，Shell 现在按模式只写入一种数据源 | `qml_integrated_theme_test` 波形/频谱往返测试 |

本轮仍未增加播放器、模型、缓存、线程、DLL 或第三方包；所有新增行为留在现有共享控制器和 QML Shell 边界内。

## 2026-08-29 第二轮视觉修复

| 修复要求 | 最小实现 | 验证 |
|---|---|---|
| 标签管理/歌词扣边描边、当前项高亮 | 复用 `Theme` token，分别提供弱玻璃、悬停、活动与描边色；活动态在深浅主题下都比未选中态更明显 | `qml_integrated_theme_test`、1674×906 同画布对比 |
| 搜索/添加框更淡、侧栏开关更大 | 共享 `TagManagementPanel` 使用弱玻璃 token；Integrated 的既有开关资源扩大按钮与图标显示区 | `qml_integrated_theme_test` |
| 波形缩放/平移条更浅 | 仅替换导航轨道和窗口块的现有主题 token，无新绘制层或缓存 | `qml_integrated_theme_test`、运行截图 |
| 底栏三段内容垂直居中、封面放大、元数据显示 | Integrated 左侧摘要改为 66px 封面并直接读取当前 `trackForId()` 的真实元数据；共享 `PlayerControls` 中间与右侧组取消旧偏移 | `qml_integrated_theme_test`、1674×906 截图 |
| 皮肤按钮弹出三项菜单 | 共享 `PlayerControls` 增加真实菜单；Classic/Integrated 直接写入既有 `playerShellMode`；沉浸模式在独立实现尚未合入本分支时明确显示“待集成”且禁用 | `qml_integrated_theme_test` 双向 Shell 切换；未声称沉浸模式已可用 |

证据：`docs/qa/evidence/integrated-theme/reference-vs-current-1674x906.png`。本轮继续保持零新增 DLL、零第三方包，新增内容仅为 QML 和测试。

### 本轮验证记录

- Release 构建与 `all_qmllint`：通过；仅保留两个既有 unused-import 信息。
- Release 聚焦回归：`qml_main_window_test`、`qml_integrated_theme_test`、`tag_management_layout_contract_test`、`integrated_shell_lifecycle_smoke_test` 共 4/4 通过。
- Debug 聚焦回归：`qml_integrated_theme_test` 通过。
- Release 全量 CTest：114 个测试程序中 113 个通过；`audio_editor_controller_test` 内 91 项通过、4 项失败（三项为当前环境报告录音后端不支持，一项为离线源 identity-mismatch 波形预期）。本轮没有修改音频编辑器或录音模块，不能据此宣称全量通过。

## 2026-08-29 第三轮轻量视觉收敛

| 修复要求 | Integrated 专属实现 | 验证 |
|---|---|---|
| 表头降低并加粗 | 复用 `TrackList.integratedCompact`，仅 Integrated 使用 48px / DemiBold；Classic 保持 56px / Medium | `qml_integrated_theme_test`、Classic 主窗口筛选/网格用例 |
| 搜索描边淡化、苹果式 BPM 滑头、清空靠近 BPM | `SearchFilter.integratedStyle` 只切换软边框、玻璃滑头与布局 Spacer；共享组件默认值不变 | Debug/Release Integrated QML 测试、1672×941 截图 |
| 标签输入降低、侧栏图标缩小并折叠居中 | 共享标签面板仅在 `compact` 时采用 34px 和软边框；Integrated 开关为 30px 按钮/22px 图标 | QML 尺寸与中心轴断言 |
| 歌单/歌曲/标签/波形/播放栏描边淡化 | 新增一个 `Theme.integratedSoftOutline`，仅 Integrated 五个主表面和内部元数据使用 | QML 主题颜色契约、同画布对比 |
| 波形导航条更浅的流体玻璃 | 现有轨道/滑块降透明度并增加 1px 主题高光；不使用 Blur、Shader、缓存或新依赖 | QML 材质断言、截图 |

本轮仍是现有 QML 与 Theme token 的最小修改：零新增图片、DLL、线程、缓存和第三方包。

## 2026-08-29 第四轮播放栏与波形视口修复

| 修复要求 | 最小实现 | 验证 |
|---|---|---|
| 底部歌名显示更长 | Integrated 当前歌曲摘要上限由 450px 调整为 520px，宽屏比例由 32% 调整为 36% | 1672px 宽度断言、实机截图 |
| 换歌恢复完整波形 | 监听共享 `PlaybackController.currentTrackId`，在播放时长和新波形时长就绪后调用现有 `WaveformItem.setVisibleRange(0, duration)` | 先缩放至 25%–75%，换歌后断言恢复 0–100% |
| 波形和播放栏距离均衡 | 波形底部外边距由 12px 改为共享 `contentSpacing` 8px | QML 实际坐标断言、1672×941 截图 |

只修改 `IntegratedPlayerShell.qml`；没有改动 Classic、播放器核心、波形分析、缓存或依赖。
