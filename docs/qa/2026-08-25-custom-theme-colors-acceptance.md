# AgPlayer 自定义主题颜色系统验收记录

日期：2026-08-25（2026-08-26 完成最终集成与打包）
状态：主题功能、跨分支集成、Windows Release 验证与桌面安装包均已完成。

## 构建与自动化

| 项目 | 结果 | 边界 |
|---|---|---|
| MSVC Release 构建 | 通过 | `build/msvc-release-final`，MSVC 19.38 / Qt 6.7.0，AgPlayer 与全部测试目标完成 |
| MSVC Debug 构建 | 通过 | `build/msvc-debug-qa`，显式 `cl.exe`，复用现有 vcpkg Debug/Release 库 |
| Release 聚焦测试 | 13/13 通过 | 主题、设置、Picker、静态颜色、Mini、Audio Editor、格式能力/计划/端到端/矩阵/参考契约 |
| Debug 聚焦测试 | 13/13 通过 | 与 Release 同一组 |
| Release 全量 CTest | 107/107 通过 | 修复 `Qt6_DIR:UNINITIALIZED` 打包缓存类型后完整复跑 |
| Debug 全量 CTest | 105/108 通过 | 3 项非主题失败列于下方；Debug 主题聚焦集 13/13 通过 |
| QML lint | 通过，1 个已知提示 | `Theme.qml` 的 `import AgPlayer` 被 lint 视为未使用，但运行时单例解析需要，移除会使测试失败 |
| `git diff --check` | 通过 | 无空白错误 |

## 视觉与交互

- `build/qa/theme-presets/`：10 个预设，深色设置页，10/10 PASS。
- `build/qa/theme-critical/`：黑、白、黄 × Light/Dark × 设置/播放，12/12 PASS。
- `build/qa/theme-modes/`：Light、Dark、System × 设置/播放，6/6 PASS。
- `build/qa/theme-independent/`：Blue Accent + Purple Highlight 独立模式，Light/Dark × 设置/播放/迷你/列表，8/8 PASS。
- `build/qa/theme-focus-fixed/`：设置页紧凑布局无右侧裁切；Hex 制品名已规范化。
- 最终集成矩阵：中/英/泰/越 × Light/Dark/System × 10 个窗口状态，共 120 个状态均获得合格截图；中文 System 设置页曾在截图生成后的退出阶段出现一次 `0xC0000005`，越南文 System 列表曾出现一次 960×1152 尺寸漂移，两项独立复跑均 PASS。
- Product Design 复核最终结论：APPROVED。24×24 命中区、独立键盘焦点环、本地化读屏名称三项缺口均关闭。

截图只是视觉证据；Picker 键盘操作、设置事务、控件代表状态和 Palette 一致性另有自动化测试。

## 真实播放与资源

- `qa-main-smoke.ps1` 使用真实 `sine-440hz.wav` 完成加载、播放、截图和有序退出：PASS。
- Windows 交互流程中，WAV 播放时“暂停”控件持续可见；Accent 从默认切换为 Purple 后，主播放器、列表、设置页同步更新，播放状态仍为播放中。
- 相同 Release 冷启动、2.5 秒采样：
  - Default：CPU 1.344 s，Working Set 154.70 MiB，Private 174.32 MiB。
  - Purple Accent + System Blue independent Highlight：CPU 1.172 s，Working Set 154.08 MiB，Private 174.43 MiB。
  - 这是短时同机对照，不外推为长期性能基准；未观察到主题派生造成的常驻内存增长。

## 最终集成与打包

- 已合并 `codex/recover-complete-release@a3ad58c` 与 `codex/revised-ui@af93306` 的最新已提交内容。
- `codex/audio-editor-20260820@409b1f2` 已经位于最终 HEAD 的祖先链中，其功能又被恢复分支后续提交取代；无调用方的 `stop-fill.svg` 未作为额外死资源引入。
- `codex/ag-color-picker@6696fc0` 已经位于最终 HEAD 的祖先链中；主题分支另外完成最终键盘/读屏可访问性修正。
- 最终检查时其余本地 `codex/*` 会话分支最新 tip 也全部是 HEAD 祖先，不存在待合并的已提交代码。
- `package-windows.ps1` 完成版本、Qt/VC 运行库和 PE 版本守卫；打包后的独立目录再次用真实 WAV 完成加载、播放、截图和有序退出：PASS。
- 桌面安装包：`C:\Users\Administrator\Desktop\AgPlayer-Setup-1.0.0-x64.exe`，34,497,050 bytes（32.90 MiB），SHA-256 `D19E1D61BF561C33C1E9737885C8B75287AE1F804B5AB1FDE3D2E021B2F5192B`。

## 已知基线 / 非主题失败与观测

- Release 全量 107/107，无保留失败。
- Debug `audio_document_test` 进程无诊断直接返回失败；相同 Release 测试通过。
- Debug `qml_main_window_test` 在 35 秒门限超时；相同 Release 测试通过，Debug 主题/Picker/格式聚焦测试均通过。
- Debug `runtime_deployment_test` 检查 Release DLL 名称而失败；同一 Debug 构建的 `debug_runtime_deployment_test` 通过。
- 最终 UI 矩阵出现过一次退出崩溃和一次列表尺寸漂移，均为截图已生成后的间歇事件，独立复跑通过；不据此宣称该退出稳定性风险已不存在。

## 平台边界

- Windows：MSVC Debug/Release 构建、自动化、UI 矩阵和真实 WAV 播放中 Accent 切换已执行。
- Windows 系统设置本身的 Light/Dark 实时切换未修改主机注册表；System 事件路径由 `theme_manager_test` 和 System QA 状态覆盖。
- macOS/Linux：无对应主机，未执行实机验收，不宣称实机通过。
