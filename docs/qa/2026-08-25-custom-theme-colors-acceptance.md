# AgPlayer 自定义主题颜色系统验收记录

日期：2026-08-25  
状态：主题功能验收通过；最终跨分支集成与桌面 EXE 打包待本会话后续步骤完成。

## 构建与自动化

| 项目 | 结果 | 边界 |
|---|---|---|
| MSVC Release 构建 | 通过 | `build/msvc-release-theme`，AgPlayer 与全部测试目标完成 |
| MSVC Debug 构建 | 通过 | `build/msvc-debug-qa`，显式 `cl.exe`，复用现有 vcpkg Debug/Release 库 |
| Release 主题聚焦测试 | 6/6 通过 | ThemeManager、Settings、Picker、静态颜色、Mini、Audio Editor |
| Debug 主题聚焦测试 | 6/6 通过 | 与 Release 同一组 |
| Release 全量 CTest | 首轮 101/107 | 主题引入的 `audio_tools_layout_contract_test` 旧语义契约已更新并单独通过；其余失败列于下方，最终集成后重跑 |
| Debug 全量 CTest | 102/108 | 6 项失败列于下方；主题聚焦测试全部通过 |
| QML lint | 通过，1 个已知提示 | `Theme.qml` 的 `import AgPlayer` 被 lint 视为未使用，但运行时单例解析需要，移除会使测试失败 |
| `git diff --check` | 通过 | 无空白错误 |

## 视觉与交互

- `build/qa/theme-presets/`：10 个预设，深色设置页，10/10 PASS。
- `build/qa/theme-critical/`：黑、白、黄 × Light/Dark × 设置/播放，12/12 PASS。
- `build/qa/theme-modes/`：Light、Dark、System × 设置/播放，6/6 PASS。
- `build/qa/theme-independent/`：Blue Accent + Purple Highlight 独立模式，Light/Dark × 设置/播放/迷你/列表，8/8 PASS。
- `build/qa/theme-focus-fixed/`：设置页紧凑布局无右侧裁切；Hex 制品名已规范化。
- Product Design 复核最终结论：APPROVED。24×24 命中区、独立键盘焦点环、本地化读屏名称三项缺口均关闭。

截图只是视觉证据；Picker 键盘操作、设置事务、控件代表状态和 Palette 一致性另有自动化测试。

## 真实播放与资源

- `qa-main-smoke.ps1` 使用真实 `sine-440hz.wav` 完成加载、播放、截图和有序退出：PASS。
- Windows 交互流程中，WAV 播放时“暂停”控件持续可见；Accent 从默认切换为 Purple 后，主播放器、列表、设置页同步更新，播放状态仍为播放中。
- 相同 Release 冷启动、2.5 秒采样：
  - Default：CPU 1.344 s，Working Set 154.70 MiB，Private 174.32 MiB。
  - Purple Accent + System Blue independent Highlight：CPU 1.172 s，Working Set 154.08 MiB，Private 174.43 MiB。
  - 这是短时同机对照，不外推为长期性能基准；未观察到主题派生造成的常驻内存增长。

## 已知基线 / 非主题失败

Release 全量测试中的非主题失败：

- `qml_main_window_test`：设置转码格式模型现有期望 3、实际 8；该失败在主题修改前已存在。
- `qml_format_converter_matrix_test`：Release 下 VBR 点击后仍为 CBR；Debug 同项通过，未由主题 QML 变更触及。
- `format_converter_reference_contract_test`：并行任务仍未按旧契约通过 `SettingsController` 同步。
- `track_waveform_thumbnail_stress_test`：既有压力失败。
- `phase6_translation_coverage_test`：既有 `AudioEditorPage/停止录音` 中文目录缺失。

Debug 额外/不同失败：`audio_document_test`、`qml_main_window_test` 35 秒超时、`runtime_deployment_test` 期望 Release DLL；另有上述格式契约、缩略波形压力和 Phase 6 翻译失败。Debug 专用 `debug_runtime_deployment_test` 通过。

## 平台边界

- Windows：MSVC Debug/Release 构建、自动化、UI 矩阵和真实 WAV 播放中 Accent 切换已执行。
- Windows 系统设置本身的 Light/Dark 实时切换未修改主机注册表；System 事件路径由 `theme_manager_test` 和 System QA 状态覆盖。
- macOS/Linux：无对应主机，未执行实机验收，不宣称实机通过。
