# AgPlayer Integrated 单窗口主题实施计划

## 目标

在集成基线 `5148160` 上增加第二套 Integrated 单窗口主题皮肤，同时保持 Classic 双窗口默认行为。播放器核心、曲库、标签、波形、拖出渲染管线和缓存均复用现有实现；任意时刻只加载一套完整 Shell，不新增第三方依赖，也不制作安装包。

## 实施边界

1. 新增独立持久化的 `playerShellMode`，Classic 与 Integrated 独立保存窗口几何。
2. `Main.qml` 保留唯一窗口与共享模型，通过 `Loader` 切换 Shell；Classic 才创建 `ListWindow`。
3. 新增 `IntegratedPlayerShell.qml`，按 1672×941 参考布局实现左库、中列表、右标签、全宽波形和底部播放栏。
4. 复用 `TagManagementPanel`，仅增加紧凑 Flow/Wrap 与折叠参数；Integrated 左栏隐藏标签管理入口。
5. 扩展现有 `WaveformItem` 可见时间范围和光标锚定缩放；选择区状态使用毫秒。
6. 在 `PlaybackController` 增加选择区、循环和曲目切换清理；复用已有快照轮询。
7. 新增薄的 `PlaybackClipDragAdapter`，继续使用 `SelectionDragController` 与 `HandoffAssetManager`，仅越过系统拖动阈值后生成持久的 24-bit PCM WAV。
8. 测试先行，分别记录 Debug、Release、QML lint、运行 smoke、视觉对比、交互与性能证据。

## 验收重点

- Classic 默认主题及原入口、窗口行为和视觉不变。
- Integrated 主区域在 1672×941、100% DPI 下边界误差不超过 2px，并覆盖 1280×720、1440×900 与 100/125/150/200% DPI。
- 切换 Shell 不停止播放、不重建核心、不重新分析波形；共享搜索与标签状态保持。
- 100ms 最小选区、8倍缩放、循环边界、选区外 seek、曲目变化清理和真实文件拖出均有自动测试。
- 不新增部署 DLL/第三方包；Release EXE 与 QML 模块相对基线增长不超过 1 MiB。
- Windows 桌面、文件夹及已安装编辑器进行真实外部拖放验证；不可用目标明确标记阻塞。

