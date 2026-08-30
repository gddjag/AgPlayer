# AgPlayer 自定义主题颜色系统开发追踪

日期：2026-08-25  
分支：`codex/custom-theme-colors`  
基线：`codex/recover-complete-release@9868504`；最终集成见下方。

## 架构边界

- `ThemeManager` / `ThemePalette` 是应用颜色计算的唯一来源，并在 QML 引擎和窗口创建前注册。
- `Theme.qml` 只代理 C++ Token、尺寸和字体，并保留旧 Token 兼容别名。
- 颜色推导只使用 Qt `QColor`、sRGB/HSL、相对亮度与对比度公式；没有新增主题包、第三方颜色库、线程、轮询、Shader 或动画框架。
- 用户 Seed 以不透明大写 `#RRGGBB` 保存，派生色不回写 Seed。
- 主/迷你播放器波形、列表缩略波形、频谱、音频编辑器波形/选区/Marker/Beat Grid/游标、Equalizer 曲线、CUE、评分、格式徽标和用户标签色不由 Accent/Highlight 派生。

## 需求到证据

| 需求 | 实现 | 自动化 / 证据 |
|---|---|---|
| Light / Dark / System 与系统事件 | `qt/src/theme_manager.*`、`app/main.cpp` | `theme_manager_test` 覆盖系统事件、Unknown palette 回退、相同输入不重复通知 |
| Accent / Highlight 预设、自定义与跟随 | `ThemePalette`、`SettingsController` | 10 个预设、10 个极端 Seed、跟随/独立表驱动测试 |
| 4.5:1 / 3:1 对比度与按钮文字选择 | `ThemeManager` 颜色求解 | `theme_manager_test` 对 Light/Dark 全量断言 |
| 设置即时预览、保存、取消、恢复默认 | `SettingsController` 编辑事务和 `ThemeSettingsSynchronizer` | `settings_controller_test` 覆盖默认、迁移、无效值、Commit/Cancel/Reset |
| 原生颜色选择器与键盘/读屏 | `ThemeColorSelector.qml`、`AgColorPicker.qml` | `qml_color_picker_test` 覆盖预设、自定义、Enter/Space、24px 命中区、独立焦点环、本地化读屏名 |
| Accent / Highlight 语义迁移 | QML 控件及 `Theme.qml` 兼容门面 | `qml_theme_color_contract_test` 禁止普通 UI 新增未分类 Hex、`Qt.lighter/darker` 与运行时颜色计算 |
| 原生控件、Popup、Menu、Tooltip 同 Palette | `ThemeManager::applyApplicationPalette()` | QML 代表状态测试与 UI 矩阵 |
| 波形和媒体固定颜色不变 | `Theme.qml` 固定媒体兼容 Token、白名单分类 | `theme_manager_test`、`qml_audio_editor_test`、静态颜色契约 |
| 中/英/泰/越文案 | `translations/agplayer_{zh,en,th,vi}.ts` | 构建生成四套 QM；预设颜色读屏名称全部本地化 |
| QA 可指定主题颜色状态 | `app/main.cpp` QA 参数、`qa-final-ui-matrix.ps1` | `--qa-accent`、`--qa-highlight`、`--qa-highlight-follow`、`--qa-settings-section` |

## 提交结构

实现按 ThemeManager、设置持久化、Picker/设置 UI、运行时集成、QML Token、QA 与可访问性分开提交。主题分支不包含原工作树的未提交音频编辑改动；最终集成只接收其他会话已经提交的分支提交。

## 最终跨会话集成

- 合并 `codex/recover-complete-release@a3ad58c`，对应 merge commit `7853ecf`。
- 合并 `codex/revised-ui@af93306` 的最新格式参数矩阵和 AIFF 支持，对应 merge commit `32d4539`。
- `codex/audio-editor-20260820@409b1f2` 已在最终 HEAD 祖先链中，并被恢复分支后续实现取代；无调用方的 `stop-fill.svg` 未额外引入。
- `codex/ag-color-picker@6696fc0` 已在最终 HEAD 祖先链中；主题分支继续补齐键盘焦点、24px 命中区、读屏选中态和色阶名称。
- 最终以 `git merge-base --is-ancestor` 检查全部本地 `codex/*` 会话分支最新 tip，均已包含于 HEAD。
- `scripts/package-windows.ps1` 兼容 CMake 缓存中的 `Qt6_DIR:PATH`、`FILEPATH` 与 `UNINITIALIZED` 类型，同时保留路径存在性和 PE 版本守卫。

最终 Release 全量 CTest 107/107；Debug 全量 105/108，三个剩余失败均有 Release 对照和明确边界，详见 `docs/qa/2026-08-25-custom-theme-colors-acceptance.md`。

## 明确未扩展范围

- 不实现 OKLCH、全局颜色动画、平台主题适配层、缓存、后台线程或新的主题依赖。
- 不在此分支推送、自动合并远端或制作非 Windows 安装器；已按用户补充要求生成 Windows 安装包并复制到桌面。
- macOS/Linux 仅由同一 Qt 事件路径和自动化测试覆盖；没有对应主机实机证据。
