# AgColorPicker 开发与验收记录（2026-08-25，复核修订 2）

## 结论与范围

**状态：PARTIAL / ACCEPTANCE_BLOCKED。** 实现、离屏 QML 行为、原生 Qt 组件视觉、MSVC Release 构建和 Release smoke 都有证据；但普通 Release 应用窗口不能被 Windows Computer Use 定位。真正的 Settings 页面导航、点击、取消、接受、实时主题切换、接受后重启持久化和恢复原值尚未完成端到端 Windows 验收。本记录不以 QML harness 或脚本 smoke 冒充该验收。

实现库存严格固定为 `b9842cd..2dcc5a5`：

- `app/CMakeLists.txt`
- `app/qml/AgPlayer/components/AgColorPicker.qml`
- `app/qml/AgPlayer/components/ColorField.qml`
- `app/qml/AgPlayer/components/ColorScale.js`
- `docs/superpowers/plans/2026-08-25-ag-color-picker.md`
- `tests/CMakeLists.txt`
- `tests/qml/tst_color_picker.qml`

该范围不包含验收文档。可复现的提交库存如下：

- `04f4575 docs: plan shared color picker implementation` 是**范围内**的计划文档提交（父提交为 `b9842cd`）。
- `124d14a docs: record color picker acceptance` 和 `4570d62 docs: refine color picker acceptance evidence` 是**范围外**的 documentation-only 验收记录提交。
- 本记录的本次修订由其自身的 documentation-only follow-up commit 承载；不改变上述实现范围。

## 历史基线、当前集成与保护范围

`git show b9842cd` 显示历史基线为 `ColorField.qml` 内 **1 个内联 `ColorDialog`**，以及 `SettingsPage.qml` 中 **10 条** 原始 `targetProperty:` 调用。

当前静态结果：

- `AgColorPicker.qml`：**1** 个共享 picker 定义。
- `ColorField.qml`：**1** 个共享集成点，引用 `AgColorPicker {`；`SettingsPage.qml` 的 **10 条** `targetProperty:` 调用仍存在且未改名。
- 生产 `app`/`qt` 范围 `ColorDialog|QColorDialog`：0 命中。
- `b9842cd..2dcc5a5` 对 `SettingsPage.qml`、`settings_controller.cpp/.hpp`、`waveform_item.cpp`、`playback_controller.cpp` 和 `core/` 的 diff：0。
- `git diff --check b9842cd..2dcc5a5`：exit 0。

因此本次仅替换颜色选择 UI；颜色属性、默认值、QSettings 键、设置事务、波形渲染、播放与 FFmpeg 均不在实现 diff 中。

## 原生 Qt 视觉证据（组件级，不是 Release Settings 级）

以下 Windows.Graphics.Capture 图像实际加载最终 `AgColorPicker.qml` 于隔离的 native Qt 6.7 `qmlscene` 临时 import harness，且已通过图像查看器人工复核：

- `build/evidence/2026-08-25-ag-color-picker/picker-light-native.jpg` — **462 × 552**，28,220 bytes。
- `build/evidence/2026-08-25-ag-color-picker/picker-dark-native.jpg` — **462 × 552**，25,839 bytes。

精确来源、命令/方法、源提交关系、尺寸和 SHA-256 见忽略的 `build/evidence/2026-08-25-ag-color-picker/picker-native-provenance.md`：qmlscene 使用忽略的 `picker-visual-harness.qml` 与 `imports/AgPlayer/qmldir` 加载 `2dcc5a5` 的最终生产 `AgColorPicker.qml`；Windows Computer Use 以 `sky.get_window_state(include_screenshot: true)` 的 Windows.Graphics.Capture data URL 原字节写入这两份 JPG。它们**不是** `contentItem.grabToImage()` 的输出（保留 harness 中独立的 diagnostic PNG 路径亦非上述 JPG）。

可见约 360 px 宽紧凑 picker；顶部圆形色样、`#63316B` HEX 和 ×；`R 99 / G 49 / B 107` 统一行；三条 RGB 渐变滑块；`#63316B` 的 5 × 2 色卡（`#F8EBFA` 至 `#251028`）；`#63316B` 上的双层选中环与 ✓。light/dark 图中外框、输入面和文本 chrome 随 `Theme` 改变，而十个色卡保持同一标签/颜色。

该 harness 证明共享组件能在原生 Qt 6.7 呈现目标结构和 live Theme-bound chrome；它**不证明** Release Settings 的导航、`ColorField` 写入、鼠标/键盘点击、关闭策略或重启持久化。

实际 Release smoke 图像为 `build/evidence/2026-08-25-ag-color-picker/release-runtime-main-smoke.png`（1104 × 342，45,111 bytes）；它证明受控 QA 路径可播放，但不显示 picker。

## 要求到证据映射

| 要求 | 自动化 / 静态 / 视觉证据 | 实际 Settings 状态 |
| --- | --- | --- |
| reopen 初始化；base/selected 初始相同 | `test_open_initializes_both_states_and_candidates`；`openForColor()` 同时赋 `selectedColor` 并 `setBaseHex()`。 | BLOCKED |
| `#RGB` / `#RRGGBB`、可选 `#`、大写规范化 | `normalizeHex()` 的去可选 `#`、三位扩展和大写逻辑；`test_normalization_and_rgb_round_trip` 实测无 `#` 六位和带 `#` 三位输入。 | BLOCKED |
| HEX / RGB 0–255、无效回退 | hex/RGB 同步、three-slider、invalid HEX、invalid RGB Enter/focus-loss 测试；`IntValidator` 与 `restoreInvalidRgbInput()` 静态检查。 | BLOCKED |
| 三条 RGB 渐变公式 | `channelEndpoint(channel, 0/255)` 生成每条滑块两端颜色；`test_each_slider_synchronizes_rgb_fields_and_hex` 断言三条更新结果。 | BLOCKED |
| HSL 色阶、`#63316B` 精确色表、相对亮度 | `ColorScale.buildPalette()` HSL steps、`isLight()` 线性 sRGB luminance（阈值 0.46）静态检查；`test_reference_palette_is_exact` 比较完整十色。 | BLOCKED |
| base 编辑不接受，selected 仅候选接受 | hex/RGB/slider tests 都断言 `selectedColor` 不变；candidate test 断言一次 `colorAccepted`、写 selected、关闭。 | BLOCKED |
| × / Escape / 外部按压取消；候选立即接受 | close button、Escape、outside press、candidate accepts once/closes immediately 四项 QML 测试；`CloseOnEscape | CloseOnPressOutside` 静态检查。 | BLOCKED |
| light/dark live chrome，候选独立于主题 | `test_theme_switch_updates_chrome_not_candidates`；两张 native 图像显示 chrome 变更和相同色表。 | PARTIAL：组件视觉已证实，实际 Settings 内实时切换未证实。 |
| 5 × 2、约 360 px、swatch/HEX/×、RGB、sliders、selected ring/✓ | `width` 限制、`columns: 5`、10 candidates 和 ring/✓ 静态结构；两张 native 图像直接可见。 | PARTIAL：组件视觉已证实，实际 Settings 未证实。 |
| 无 Timer/Web/Shader/thread/cache/deps | `AgColorPicker.qml`、`ColorField.qml`、`ColorScale.js` 对 Timer、XMLHttpRequest、WebSocket、WebEngine、WorkerScript、QtConcurrent、ShaderEffect、Canvas、`Image {`、Thread、cache、setInterval/setTimeout 的扫描均为 0；未增加第三方依赖。 | 静态 PASS |
| 键盘可达 | `ColorField` 的 `Qt.StrongFocus`、`Accessible.Button`，以及 `test_color_field_is_focusable_accessible_and_keyboard_operable`（Space / Return）。 | BLOCKED：真实 Settings 键盘路径未操作。 |
| 接受后控制器持久化、默认值/重置、取消事务 | `waveformAppearanceSettingsClampPersistAndReset`（颜色 reloaded、reset defaults）和 `editSessionCanCommitOrCancel`；picker integration test 覆盖 candidate 写入与 picker cancellation。 | BLOCKED：实际 Settings accept → restart → restore original 未完成。 |
| 十个调用器与保护文件 | 上述 10 条 `targetProperty:`、一共享调用器及保护 diff 0。 | 静态 PASS |

## 构建、测试、lint 和日志

初版 MSVC Release 证据：`build/msvc-release` 在 `vcvars64.bat` 后构建通过；相关 CTest **4/4**（`settings_controller_test`、`qml_main_window_test`、`qml_color_picker_test`、`qml_mini_player_test`）通过；`agplayer_app_qml_qmllint` 与 `all_qmllint` exit 0；`runtime_deployment_test` **1/1** 通过；`qa-main-smoke.ps1 -BuildDirectory build/msvc-release` 通过。

保留日志：`build-msvc.log`、`ctest.log`、`ctest-color-picker-verbose.log`、`qmllint.log`、`all-qmllint.log`、`runtime-smoke.log`。两次 qmllint 都只输出同一条非本任务 notice：`RecordingInspectorSection.qml:3:1: Unused import (QtQuick.Dialogs)`，exit 0。

复核轮已在 `VsDevCmd.bat -arch=x64 -host_arch=x64` 下完整执行 fresh gate：

| 命令 | 结果 | 日志 |
| --- | --- | --- |
| `cmake --preset windows-msvc-release --fresh` | PASS，exit 0；MSVC 19.38.33133.0，配置/生成 727.6 s，输出 `build/release`。 | `round1-configure-msvc-retry.log` |
| `cmake --build --preset windows-msvc-release --target AgPlayer qml_main_window_test qml_mini_player_test settings_controller_test` | PASS，exit 0。 | `round1-build.log` |
| `ctest --test-dir build/release -C Release -R '^(qml_color_picker_test|qml_main_window_test|qml_mini_player_test|settings_controller_test)$' --output-on-failure` | PASS，**4/4**，13.38 s。 | `round1-ctest.log` |
| `cmake --build --preset windows-msvc-release --target agplayer_app_qml_qmllint all_qmllint` | PASS，exit 0。 | `round1-qmllint.log` |
| `ctest --test-dir build/release/tests -C Release -R ^runtime_deployment_test$ --output-on-failure` | PASS，**1/1**，0.35 s；在生成的 `tests/CTestTestfile.cmake` 目录运行。 | `round2-runtime-deployment-test.log` |

fresh configure 在首次真正调用前有两次无副作用的 shell quoting 失败（`VsDevCmd.bat` 未识别）；正确调用随后等待另一工作树的全局 `vcpkg-running.lock`，锁释放后正常完成。该等待不是源代码、测试或依赖失败。

## Windows / macOS 剩余风险

Computer Use 对普通 Release `AgPlayer.exe` 的启动返回 `launched app did not expose a targetable window`，紧接着 `sky.list_windows()` 无 AgPlayer 窗口。没有注入任何 picker 状态变更，因此不需要恢复用户颜色或主题。

Windows 验收仍需可定位的普通 Release 窗口以完成实际 Settings UI 的导航、outside/Escape/×、candidate accept、open-popup theme switch、accept 后重启、Settings cancel 和原值恢复。macOS 只具共享 QML / 静态证据，尚无 macOS runtime 结论。
