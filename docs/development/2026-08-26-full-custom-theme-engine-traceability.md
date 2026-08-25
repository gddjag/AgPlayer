# 全自定义主题引擎：预集成需求可追溯性（2026-08-26）

## 基线与范围

- 审核基线：`679c56c03a1a9b05f24592cec413bb791092cd4f`（`feat(theme): replace settings colors with skin selector`）。
- 本记录只覆盖 Task 4 的**预集成**证据；没有合并分支、打包或复制安装器，也不把此记录当作最终发布批准。
- Windows 证据来自本机 MSVC 19.38 / Qt 6.7.0；macOS、Linux 没有对应主机，未作实机声明。

## 需求到当前证据

| 已批准需求 | 当前实现边界 | 可重复证据 | 预集成结论 / 限制 |
| --- | --- | --- | --- |
| Default 保持中性 Light/Dark/System 表面，Action 为 `#007AFF`，播放中行是 `rgba(#8F57C9, 0.34)` | `ThemeManager` 的 Default palette 与 `Theme.currentTrackSurface` | Release `theme_manager_test`；`qml_theme_color_contract_test`；Default 30 张矩阵截图 | 自动化通过。当前 QA CLI 不能在同一列表截图中同时设定播放中行和仅选中行，紫色行的运行时视觉区分仍需可交互列表验收。 |
| 一个不透明 `#RRGGBB` Seed 生成完整普通 UI palette，涵盖 Red/Yellow/Black/White 极端色 | `SkinMode::Generated`、`--qa-skin` | Release/Debug `theme_manager_test`；`#FF0000`、`#FFD400`、`#000000`、`#FFFFFF` 各 15 张 Light/Dark/System 截图 | 通过当前自动检查和人工图片目检；不外推为长期视觉偏好结论。 |
| 外观 Light/Dark/System 独立于皮肤选择 | 三个 appearance mode 与 skin 设置分离 | 每个 Default/四个生成 skin 都生成 Light/Dark/System；测试矩阵 | 通过。System 截图在本机反映当前 OS dark 状态；没有修改 Windows 主题来验证实时 OS 事件，事件路径由单元测试覆盖。 |
| Settings 以一个可访问 Theme skin color 控件取代 Accent/Highlight/Follow | `SettingsPage.qml` 重用 `ThemeColorSelector` 和新设置 API | `qml_color_picker_test`、`settings_controller_test`、静态契约；Settings 截图 | 通过。截图可见 10 个色点、自定义输入、选中和按钮状态；实际读屏器尚未实测。 |
| 当前行与仅选中行使用不同 Token | `Theme.currentTrackSurface` 与 `selectedTrackSelection` | `qml_theme_color_contract_test`、`qml_main_window_test`（Release） | 静态/自动化通过；同一列表中的真实双状态可视对比仍是 QA harness 缺口。 |
| 普通 UI 不以 QML 内联色值或运行时 lighter/darker 推导；语义/媒体色独立 | C++ palette + QML Token 门面 | `cmake -DROOT=... -P cmake/CheckQmlThemeColors.cmake`；`qml_audio_editor_test` | 通过；目检中波形、录音/错误/评分等固定语义色未跟随 Red/Yellow/Black/White skin 改色，符合范围。 |
| 主界面、列表、迷你播放器和四个音频工具页面共享 palette | `Main.qml`、`ListWindow.qml`、`MiniPlayerWindow.qml`、`AudioToolsWindow.qml` | Default Light/Dark/System 的 `startup`、`playback`、`mini`、`settings`、`list`、`details`、`tool-0..3` 共 30 张截图 | 自动截图尺寸、非空、日志、深浅差异均通过；人工目检代表图未见裁切或空白。Menu、Popup、Tooltip 没有现成可控截图入口。 |
| 中文、英文、泰文、越文新文案完整 | 四个 TS/QM 目录 | `translation_catalog_test`（Release/Debug），编译生成四份 QM | 通过自动化；本轮视觉矩阵为中文以控制范围，其他三种语言尚未重做屏幕目检。 |
| 真实 WAV 播放时皮肤状态不破坏播放 | 正常 controller/engine 路径，`--qa-play` 仅是注入输入 | Default 与 `#FF0000` 各自 WAV 播放、截图、QA 日志和有序退出 | 通过两个独立启动状态。CLI 在运行期间不能自动切换皮肤，不能把两个进程结果声称为同一会话热切换。 |

## 证据产物

- `build/qa/2026-08-26-theme-preintegration/default/`：中文 Default × Light/Dark/System × 10 个可控 surface，30 张，`matrix.csv` 逐项 `PASS`。
- `build/qa/2026-08-26-theme-preintegration/{red,yellow,black,white}/`：每个 skin × Light/Dark/System × playback/mini/settings/list/tool-0，合计 60 张，四份 `matrix.csv` 均为 `PASS`。
- `build/qa/2026-08-26-theme-preintegration/playback-performance/`：Default 和 Red 的真实 WAV screenshot 与运行日志。

详细命令、人工视觉发现、测试基线和集成门禁见 `docs/qa/2026-08-26-full-custom-theme-engine-preintegration-acceptance.md`。
