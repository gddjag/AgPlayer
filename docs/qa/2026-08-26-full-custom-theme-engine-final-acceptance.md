# 全自定义主题引擎：最终集成与 Windows 交付验收（2026-08-26）

## 结论

Theme Engine、单一皮肤设置、跨分支有效提交和 Windows 安装包已完成。生产代码基线为 `e792e14`；最终分支只在其后追加验收文档。Release 构建与 108/108 CTest 通过，274/274 视觉矩阵通过，5/5 真实 WAV 播放路径 smoke 通过，独立打包目录 smoke 通过。Debug 构建通过，但全量测试仍保留三个可重复的既有失败，因此不把本记录表述为 Debug 全绿或跨平台实机全绿。

## 功能与集成范围

- `ThemeManager` 以 `appearanceMode + skinMode + skinSeed` 生成完整普通 UI Palette；Seed 保留为原始不透明 sRGB 身份，派生背景、表面、文字、边界、Disabled、Action、Selection、Focus 和播放中状态。
- Default 继续使用中性 Light/Dark 表面，Action 固定为 `#007AFF`；播放中行固定为 `QColor(143, 87, 201, 87)`，即 `#8F57C9`、alpha 约 0.34。
- Settings 只保留一个“主题皮肤颜色”选择器；旧 Accent/Highlight/Follow 属性没有生产 API 或 UI，只在兼容测试中确认旧磁盘键不被破坏。
- `c039a04` 修复选择预设/自定义时的中间 Palette：先写目标值、再启用模式；回归测试锁定一次选择只发出一次 `paletteChanged`。
- `e792e14` 修复黑色深色与白色浅色皮肤的重要 Action 图标对比度；表驱动测试要求 Action 对 background/surface 均不低于 4.5:1，同时黑白灰保持无色相身份。
- 波形、频谱、编辑器、CUE、评分、收藏、录音和用户标签色继续使用独立媒体/语义 Token，不从皮肤 Seed 派生。
- 已适配集成 `codex/custom-theme-colors` 的 RGB 波形设置、列表布局/运行时几何、元数据批处理语义、响应式音频工具和安装器素材。`codex/audio-editor-20260820@409b1f2` 是未合入的旧 16 文件编辑器路线，功能已被当前后续实现取代；`codex/ag-color-picker` 和 `main` 的旧计划/旧主题路线不回放。

## 构建、测试与静态检查

| 检查 | 最终结果 | 边界 |
| --- | --- | --- |
| MSVC Release 构建 | PASS | Qt 6.7.0，x64。 |
| Release 全量 CTest | PASS，108/108，146.77 s | 包含 ThemeManager、Settings、QML、音频工具、部署与安装器契约。 |
| MSVC Debug 构建 | PASS | 最终主题和 QML 均重新编译。 |
| Debug 全量 CTest | 103/109；失败项复跑后剩 3 项 | 首轮还出现 `recovery_test`、`library_manager_controller_test`、`import_controller_test` 三个间歇失败；三项串行复跑 3/3 PASS。持续失败为 `audio_document_test`、`qml_main_window_test` 35 s 超时、以及在 Debug 目录执行 Release `runtime_deployment_test` 缺 `Qt6Core.dll`；对应 `debug_runtime_deployment_test` PASS。 |
| Debug 主题聚焦 | ThemeManager、Settings、Color Picker、色彩契约、翻译均 PASS | `qml_audio_editor_test` 在有并行视觉负载时一次无输出失败，独立复跑 PASS；`qml_main_window_test` 仍为上述 Debug 超时。 |
| QML 静态色彩契约 | PASS | `QML theme color classification passed`。 |
| `qmllint --bare` | exit 0，80 warnings | 71 `missing-type`、9 `unresolved-alias`，均为现有独立 lint 类型信息/局部 alias 警告；无 error。 |
| `git diff --check` | PASS | 最终文档提交前执行。 |

## 视觉与 Product Design 验收

证据根：`build/qa/2026-08-26-theme-final-current/`。

- Default：中/英/泰/越 × Light/Dark/System × 10 个 surface，共 120/120 PASS。
- 10 个预设：中文 × Light/Dark/System × playback/mini/settings/list/tool-0，共 150/150 PASS。
- 极端黑色 Dark 与白色 Light：playback/settings 各 2 张，共 4/4 PASS。
- 合计 274 张矩阵截图、274 条 CSV 记录，全部通过尺寸、非空、外角透明、运行日志和深浅差异检查。
- 主代理和独立视觉审核都实际打开了 Default 四语言设置、Default 播放、Red Light/Dark 设置、Gold Light 列表及 Black/White 播放/设置；确认生成皮肤会改变整个普通 UI 层级，黑/白极端皮肤的激活列表图标分别为浅灰/深灰，不再出现黑底黑或白底白。

视觉边界：QA harness 仍不能显式打开 Menu、Popup、Tooltip，也不能在同一列表 fixture 同时构造“播放中”和“仅选中”两行；这些状态有 Palette/QML 自动测试，但没有最终截图。英文/越南文窄侧栏长标题有截断，英语/泰语/越南语设置页仍可见部分既有波形小节中文回退；新主题样式/皮肤选择器文案本身已覆盖四语言。

## WAV 路径与性能采样

`build/qa/2026-08-26-theme-final-current/playback-performance/` 保留：

- Default、Red、Gold、Black、White 各一次真实 `sine-440hz.wav` 启动，共 5/5 ExitCode 0。
- 每次生成 960×298 有效截图，日志文件存在且问题扫描没有 WARN/ERROR/FATAL、QQml、ReferenceError 或 TypeError。
- `playback-runs.csv` 保留完整命令、参数、PID、ExitCode、截图与日志字段；`process-samples.csv` 保留 169 条 ISO 时间戳 CPU/Working Set 原始采样。
- 五次短启动的累计 CPU 终值为 1.47–1.77 s，Working Set 峰值为 177.62–179.23 MiB。该数据只用于发现皮肤间明显失控，没有长期稳态、同会话热切换或统计基准含义。

此项仅证明正常播放器真实 WAV 路径能够进入播放态、截图并有序退出；没有现场听音或声卡设备观测，不作听觉验收声明。

## 安装包与独立目录验证

- 版本：`1.0.0`；应用 PE：x64 Windows GUI，FileVersion `1.0.0.0`。
- `package-windows.ps1` 重新部署 Qt 6、VC Runtime 和音频 DLL，并由 Inno Setup 6.7.3 生成版本化安装器。
- 独立 `build/package/AgPlayer/AgPlayer.exe` 使用真实 WAV、Dark + Red 皮肤 smoke：ExitCode 0，截图 41,636 bytes，日志 0 bytes、问题扫描为空。
- 桌面文件：`C:\Users\Administrator\Desktop\AgPlayer-Setup-1.0.0-x64.exe`。
- 大小：34,378,392 bytes（32.79 MiB）。
- SHA-256：`A1098B33B681454A5DE42CE9403FE74AAD8480237F7AAC9B54E53FC5388648C2`。

## 尚未执行或不宣称通过

- 没有 Windows 外观设置实时切换的人工 OS 事件观察；System/Unknown 路径由 ThemeManager 自动测试覆盖。
- 没有真实声卡听音、读屏器宣布或同一进程的运行中皮肤热切换人工验收。
- macOS/Linux 没有对应主机、构建、视觉或播放实机证据；只共享同一 Qt 事件/算法测试路径。
- Debug 三个持续失败保留为基线门禁，不能被 Release 108/108 掩盖。
