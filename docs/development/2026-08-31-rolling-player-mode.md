# AgPlayer 滚动播放模式：集成与视觉 QA

日期：2026-08-31
范围：Task 6 独立验收记录；不修改播放器、QML、Core 或部署目录。

## 需求到证据

| 验收项 | 本轮证据 | 结论 |
| --- | --- | --- |
| 滚动外壳、固定中心播放线、总览定位、Scratch 映射、速度/BPM/保音高/缩放控件 | 最新 `qml_rolling_theme_test`，Release，14/14 | 通过自动化合同；真实音频与听感仍未验证 |
| 三套主外壳共用控制条、图标顺序、1000 DIP 下展开音量不重叠 | `qml_player_controls_layout_test`，Release，1.35 s | 通过自动化合同 |
| 设置持久化及滚动外壳相关窗口规则 | `settings_controller_test`，Release，2.08 s | 通过自动化合同 |
| 独立歌单磁吸、窗口几何及 DPI 转换 | `window_controller_test`（0.65 s）与 `window_mixed_dpi_transition_test`（0.39 s），Release | 通过自动化合同 |
| QML 静态检查 | `qmllint`：`Main.qml`、`RollingPlayerShell.qml`、`WaveformSession.qml`、`ImmersiveSurface.qml`、`IntegratedPlayerShell.qml` | 通过；退出码 0、无输出 |
| 默认、最小及超宽窗口 | 隔离 QA 进程的原生 `grabWindow()` PNG：1440×480、1000×420、2560×480 | 可见区域无裁切或控件重叠；仅覆盖截图状态 |
| 100/125/150/200% 缩放 | 隔离 QA 进程四张 1440×480 逻辑像素 PNG，退出码均为 0，日志无 WARN/ERROR/FATAL/QML 错误 | Qt 进程级缩放渲染通过；不等同于真实多显示器可操作性 |
| 与提供参考图的视觉核对 | 同画布比较图，左为参考、右为当前 QA 实现 | 未通过像素/视觉一致性验收，见下方发现 |
| Release 体积净增不超过 1 MiB | 没有可与当前脏工作树精确对应的干净部署基线；本轮未打包 | 未验证 |
| 真实设备、格式矩阵、听感、Scratch 延迟和 CPU/内存门槛 | 本轮未运行 | 未验证 |

## 本轮执行记录

构建目录：`build/task3b1-release`（Release）。第一次直接调用 Ninja 时，PowerShell 未加载 MSVC 的 `INCLUDE` 环境，报 `C1083: algorithm`；改为 `VsDevCmd.bat` 后目标 `qml_main_window_test` 成功构建。该环境问题不是产品编译错误。

```text
ctest --test-dir build/task3b1-release -C Release \
  -R '^(qml_rolling_theme_test|qml_player_controls_layout_test)$' --output-on-failure

初始构建：2/2 passed，2.55 s。

ctest --test-dir build/task3b1-release -C Release \
  -R '^(settings_controller_test|window_controller_test|window_mixed_dpi_transition_test)$' \
  --output-on-failure

初始构建：3/3 passed，3.15 s。
```

最后一次 P1 修复后，在 `build/task3a-release` 重建 Release
`qml_main_window_test`。手动执行最新 `qml_rolling_theme_test` 为 14/14 通过，
`qml_player_controls_layout_test` 为 4/4 通过；再对上述五个相关 QML 文件执行
`qmllint`，退出码 0、无输出。该结果仅覆盖滚动主题的定向 QML 合同，不覆盖
真实设备、格式矩阵或部署验收。

共享 `WaveformProvider` 在轨道加载时一次生成 `mix/bass/mid/high` 层；各播放器外壳
复用同一份数据，切换普通、RGB 与频彩渲染模式不会重新分析音频。回归断言覆盖
模式切换期间 generation 保持不变，以及滚动播放器继续读取旧基线的低/中/高频
颜色与强度设置。

QA 进程均使用 `--qa-test-mode` 和独立日志/状态，不读取或覆盖用户播放器设置。默认四个缩放截图使用 `sine-440hz.wav`；为避免单一正弦导致大波形近似矩形，额外使用 `multiband-qa.wav` 进行了 100% 默认、最小和超宽截图。

## 原生截图证据

- `artifacts/rolling-player-qa-20260831/rolling-1.png`：100%，1440×480，53,925 bytes。
- `artifacts/rolling-player-qa-20260831/rolling-125.png`：125%，1440×480，64,983 bytes。
- `artifacts/rolling-player-qa-20260831/rolling-15.png`：150%，1440×480，63,411 bytes。
- `artifacts/rolling-player-qa-20260831/rolling-2.png`：200%，1440×480，58,348 bytes。
- `artifacts/rolling-player-qa-20260831/rolling-multiband-100.png`：100%、真实频段 QA 信号，1440×480，89,836 bytes。
- `artifacts/rolling-player-qa-20260831/rolling-minimum.png`：1000×420，70,378 bytes。
- `artifacts/rolling-player-qa-20260831/rolling-ultrawide.png`：2560×480，114,115 bytes。
- `artifacts/rolling-player-qa-20260831/comparison-reference-vs-rolling-multiband-1440x480.png`：同画布视觉比较。参考图从 2172×724 等比缩放到 1440×480；右侧为同状态尺寸的实现截图。

## 视觉检查与限制

截图表明当前外壳已具备用户要求的结构：顶部全曲总览与紫红色进度、居中固定播放针、大波形、左侧播放控制、右侧速度/BPM/保持音调/缩放，以及右上双通道分段电平。最小和超宽截图没有可见裁切或彼此覆盖的控件。

但当前截图不能接受为参考图级别的最终视觉一致性：右侧实现在大波形中呈现低饱和青绿/粉灰分频层及宽灰色中轴，而参考是明亮、连续的彩色全频轮廓；QA 音频、默认封面、曲名、星级与元数据也不与参考曲目状态相同。控制条虽已通过顺序合同，当前 1440 DIP 的实际分组、尺寸和密度仍与参考图存在明显差异。因此本记录不把“结构可用”写成“视觉已验收”。

`QT_SCALE_FACTOR` 截图验证的是应用的进程级渲染与逻辑像素输出；尚未在真实 100/125/150/200% Windows 显示器、跨屏移动或不同物理 DPI 下操作窗口。自动化也未替代真实 WAV/FLAC/MP3 VBR/AAC、WASAPI 共享/独占、设备切换、导入→搓碟→换肤→退出及听感验收。

## 已修复的 P1 回归

最新 Release QML 测试的 XML 证据为
早先的失败证据保留在
`artifacts/rolling-player-qa-20260831/rolling-qtest-latest.xml`，但已不代表
当前源码：

- 大波形用虚拟首尾留白并以 `WaveformItem` 的公开坐标映射回校，覆盖
  `0/100/duration-100/duration` 四个位置均保持在固定中心针下。
- `currentTrack` 改为受库与波形会话修订驱动的显式缓存，消除了声明式循环。
- 一体化封面源先转为空字符串，避免把 `undefined` 写入 `QUrl`。
- Scratch 手势在约 35 ms 无移动时向核心发布 `updateScratch(0)`；松手和取消
  会停止计时器。

原生截图均在上述最后一次 QML 重建之前采集，只能作为此前状态的视觉证据，不能替代修复后的重新截图。

## 结论

设置/窗口与共享控制条自动化、隔离原生截图和当前滚动主题 QML 合同均提供正向证据；真实音频/硬件性能门槛、物理 DPI 操作和参考级视觉差异仍阻塞最终验收。本轮没有打包、提交、清理或删除任何内容。

final result: implementation QML contract green; full product acceptance remains blocked
