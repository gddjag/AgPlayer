# AgColorPicker 验收与开发记录（2026-08-25）

## 范围与基线

目标是用唯一的原生 QML `AgColorPicker.qml` 替代原有系统颜色选择入口，并保持十个既有设置属性、默认值、QSettings 键、设置事务、波形渲染、播放和 FFmpeg 行为不变。实现基线为 `b9842cd`，验收时 HEAD 为 `2dcc5a5`。

本记录只报告本次实际执行得到的证据；尤其不把 Windows 上的自动测试描述为 macOS 或真实桌面交互验收。

## 变更边界与静态残留扫描

`git diff --name-only b9842cd..HEAD` 的实际变更文件为：

- `app/CMakeLists.txt`
- `app/qml/AgPlayer/components/AgColorPicker.qml`
- `app/qml/AgPlayer/components/ColorField.qml`
- `app/qml/AgPlayer/components/ColorScale.js`
- `docs/superpowers/plans/2026-08-25-ag-color-picker.md`
- `tests/CMakeLists.txt`
- `tests/qml/tst_color_picker.qml`

扫描结果：

- 共享 picker 定义：1 个，`app/qml/AgPlayer/components/AgColorPicker.qml`。
- 共享调用器：1 个，`ColorField.qml`；`SettingsPage.qml` 中原有 `targetProperty:` 行为 10 条，因此十个颜色设置仍经同一调用器进入 picker。
- `app` 与 `qt` 的 `ColorDialog|QColorDialog` 扫描：0 命中。
- `AgColorPicker.qml` 的 `ShaderEffect|Canvas|Image { |QtQuick.Dialogs` 扫描：0 命中。
- `git diff --check`：通过（exit 0）。
- 保护范围 `SettingsPage.qml`、`settings_controller.cpp/.hpp`、`waveform_item.cpp` 的基线 diff：0；另查 `qt/src/playback_controller.cpp` 与 `core/`：0。

## 构建、测试与 lint（Windows / MSVC Release）

使用现存并已核验的 `build/msvc-release` 缓存：Ninja、Release、`cl.exe` 为 `...MSVC/14.38.33130/.../Hostx64/x64/cl.exe`，Qt 为 `D:/Qt/6.7.0/msvc2019_64`。没有编辑 `CMakePresets.json`；命名 preset 的二进制目录是 `build/release`，不作为本次 MSVC 验收目录。

首次构建尝试因当前 PowerShell 未载入 MSVC 标准库环境而失败：`fatal error C1083: 无法打开包括文件: “type_traits”`。随后通过 `vcvars64.bat` 载入相同的已验证 MSVC 环境重跑，结果如下。

| 命令 | 结果 |
| --- | --- |
| `cmake --build build/msvc-release --target AgPlayer qml_main_window_test qml_mini_player_test settings_controller_test`（`vcvars64.bat` 后） | PASS，exit 0；构建并部署实际 Release `app/AgPlayer.exe`。 |
| `ctest --test-dir build/msvc-release -C Release -R '^(qml_color_picker_test|qml_main_window_test|qml_mini_player_test|settings_controller_test)$' --output-on-failure` | PASS，4/4，11.77 s。 |
| `cmake --build build/msvc-release --target agplayer_app_qml_qmllint`（`vcvars64.bat` 后） | PASS，exit 0。 |
| `cmake --build build/msvc-release --target all_qmllint`（`vcvars64.bat` 后） | PASS，exit 0。 |
| `ctest --test-dir build/msvc-release -C Release -R '^runtime_deployment_test$' --output-on-failure` | PASS，1/1，0.42 s。 |
| `scripts/qa-main-smoke.ps1 -BuildDirectory build/msvc-release` | PASS；实际 Release 加载并播放 fixture、保存截图且日志中没有 WARN/ERROR/FATAL。 |

`qml_color_picker_test` 是 CTest 名称，刻意复用 `qml_main_window_test` 可执行程序而不是独立 CMake build target；因此首次错误地请求 `--target qml_color_picker_test` 返回 `ninja: error: unknown target 'qml_color_picker_test'`。修正为该测试实际依赖的 `qml_main_window_test` 后，上表 4/4 CTest 已覆盖它。

两次 qmllint 都仅输出同一条非本任务既有 notice：`app/qml/AgPlayer/components/audioeditor/RecordingInspectorSection.qml:3:1: Unused import (QtQuick.Dialogs)`；工具返回 exit 0，未报告 picker lint error。

## 要求到证据的映射

| 要求 | 现有自动化 / 静态证据 | Windows 真实 UI 结论 |
| --- | --- | --- |
| 唯一原生 QML picker、约 360 px 紧凑弹窗、色样/HEX/关闭行、RGB 容器、三条渐变滑块、5×2 色卡、选中环与 ✓ | `AgColorPicker.qml` 静态结构扫描：`width` 上限 360、`columns: 5`、10 个 `colorCandidate-*`、selected ring/✓；`qml_color_picker_test` 通过。 | 未完成：未取得真实 popup 截图，不能以源代码或离屏测试替代视觉比对。 |
| `#63316B` 的十个精确候选色 | `test_reference_palette_is_exact` 在通过的 `qml_color_picker_test` 中比较 `#F8EBFA,#E9D2EC,#D6B9DB,#C09CC6,#A76BB0,#63316B,#512C57,#432248,#341938,#251028`。 | 未完成真实视觉确认。 |
| HEX/RGB/滑块同步与无效输入回退 | `test_normalization_and_rgb_round_trip`、`test_hex_edit_changes_base_and_candidates_only`、`test_rgb_fields_synchronize_hex_and_sliders`、`test_each_slider_synchronizes_rgb_fields_and_hex`、两项 invalid-input 测试均属于通过的 QML 测试。 | 未完成真实窗口输入确认。 |
| 色卡立即接受；×、Escape、外部点击取消 | `test_candidate_accepts_once_and_closes_immediately`、`test_close_button_cancels_without_acceptance`、`test_escape_cancels_without_acceptance`、`test_outside_press_cancels_without_acceptance` 通过。 | 未完成真实窗口点击确认。 |
| `ColorField` 接入、键盘可达、十个设置入口 | `test_color_field_commits_candidate_and_cancellation_preserves_setting` 和 keyboard/accessibility 测试通过；静态得到 10 个 `targetProperty:`。 | 未完成真实 Settings 页面导航/点击确认。 |
| 弹窗打开时主题即时变更、候选色不变 | `test_theme_switch_updates_chrome_not_candidates` 通过。 | 未完成真实 light/dark popup 截图确认。 |
| 已接受的颜色在重启后存在；设置页取消恢复旧值 | `settings_controller_test` 的波形颜色持久化/重载、设置事务 `beginEdit/cancelEdit/commitEdit` 覆盖底层机制；`qml_color_picker_test` 覆盖 candidate 写入与 picker cancellation。 | 未完成端到端真实 Settings 接受→退出/重启→重新读取→恢复原值；不应把底层测试称为桌面交互验收。 |

## Windows 运行与视觉证据

已存的真实 Release runtime screenshot：

- `build/evidence/2026-08-25-ag-color-picker/release-runtime-main-smoke.png` — 1104 × 342 px，45,111 bytes。它显示正常实际 Release 播放 smoke；它**不**显示颜色 picker，不能作为 picker 外观的替代证据。
- 同目录日志：`build-msvc.log`、`ctest.log`、`qmllint.log`、`runtime-smoke.log`、`ctest-color-picker-verbose.log`。

使用 Computer Use 的 `sky.launch_app` 对 `build/msvc-release/app/AgPlayer.exe` 进行实际窗口启动时，返回精确错误：`launched app did not expose a targetable window: process:D:\\ai\\AgPlayer\\.worktrees\\ag-color-picker\\build\\msvc-release\\app\\AgPlayer.exe`。按恢复规程立即执行 `sky.list_windows()`，其中没有任何 `AgPlayer` 窗口。由于不存在可唯一选择的目标窗口，未再注入点击、键盘或状态修改；原始注册表中 `HKCU\\Software\\AgPlayer\\AgPlayer` 的 `themeMode` 与相关颜色值均为未显式保存状态，因而也没有发生需要恢复的实际设置更改。

这与项目 runtime smoke 通过并不矛盾：smoke 使用 `--qa-test-mode`，自动截图后退出，只证明受控 QA 路径可运行，不能提供可由 Windows 自动化交互的 Settings/picker 窗口。因此本次没有伪造 light/dark picker 截图，也没有宣称完成 outside/Escape/×、接受、实时主题或重启持久化的 Windows 桌面验收。

## 平台范围与结论

共享 QML 的结构、离屏 QML 测试、MSVC Release 构建和 Windows runtime/deployment smoke 均有证据。macOS 没有在本次 Windows 主机上构建或运行；唯一可报告的 macOS 依据是共享 QML、无平台专属 picker 分支、无 `QColorDialog` 残留的静态事实。macOS 的实际 Popup 尺寸、主题、键盘/外点取消和持久化行为仍有残余运行风险。

**当前验收状态：DONE_WITH_CONCERNS。** 代码级/自动化证据为通过；真实 Windows Settings/picker 窗口未能由支持的自动化 API 绑定，故视觉、交互与重启端到端接受不能批准。后续需先恢复可目标化的普通 Release 窗口，再在记录原始主题/颜色后采集 light/dark popup 截图，完成取消、接受、主题即时更新、重启与恢复原值的人工/自动化复测。
