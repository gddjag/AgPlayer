# 频彩波形（曜釉）验收记录

日期：2026-08-31
状态：功能与聚焦回归已验证；**整体发布验收未通过**（完整自动化套件有四项失败，真实 GPU / 音频设备与完整频彩视觉矩阵尚未完成）。
执行候选：`D:\ai\AgPlayer\.worktrees\codex-task8-r24-candidate`，基线 `f0344c9`，候选基于 `da4877c` 加 Task8 overlay 和协调的 decoder overlay。
构建：Release、MSVC x64、Ninja，依赖树 `D:\ai\AgPlayer\build\task3a-release\vcpkg_installed`。

## 结论

- 曜釉四层设置、单遍三频分析、FCW1 缓存、按需发布、四层 SceneGraph、四个播放器表面与频彩压力合同均有自动化覆盖；本次隔离候选的指定聚焦套件为 **16/16 通过**。
- 完整 Release 构建为 **377/377 Ninja 步骤成功**；完整 CTest 为 **136/140 通过、4 项失败**。失败不能被解释为“硬件跳过”，因此不能宣称完整发布验收完成。
- R29 普通波形不超过 3% 的性能结论为 **`ENVIRONMENT_UNSUITABLE`**：严格配对样本的离散度超过 MAD 门槛，既不是通过，也没有据此认定产品回归。
- 600 秒频彩压力测量达到其确定性缓存、内存、60 Hz 与几何稳定性条件；但该历史 overlay 运行没有完整候选 clean-state / 可执行文件 SHA 身份记录，按 Task8 规则只能作为 **evidence-only**，需要在 SHA 可识别的干净候选重跑后才能成为可复现发布证据。

## 要求与证据

| 要求 | 证据 | 结果 | 限制 / 说明 |
| --- | --- | --- | --- |
| 只在频彩模式进行单遍三频分析，普通模式保留旧路径 | `decoder_test`、`waveform_analyzer_test`、`frequency_color_waveform_analyzer_test`，2026-08-31 聚焦 CTest | 已验证 | 聚焦运行通过；协调 decoder 合同另有 3/3 Release 证据。 |
| LR4 三频、下混、Peak+RMS、共享归一化与降级 | `frequency_color_waveform_analyzer_test` | 已验证 | 本轮聚焦通过。 |
| FCW1 独立、可校验且不存颜色 | `frequency_color_waveform_cache_test`、`waveform_cache_test`；R31 五个 `.fcw` 均为 6,064 B | 已验证 | 6,064 B 小于 8,192 B 上限。 |
| 主波形先发布、三频后台发布、取消/缓存回退 | `waveform_provider_test` | 已验证 | 本轮聚焦通过。 |
| 四层原生图形、预乘 alpha、进度只改材质、软件回退与质量滞后 | `waveform_item_test`、`frequency_color_waveform_stress_test` | 已验证 | 加速 RHI 像素读回未验证。 |
| 每层深/浅颜色和 0–100% 透明度、恢复曜釉默认、迁移不覆盖自定义 | `settings_controller_test`、`qml_main_window_test`、`qml_audio_visual_impact_test` | 已验证 | 本轮聚焦通过。 |
| 主、迷你、集成、沉浸式表面与交互映射 | `qml_waveform_test`、`qml_main_window_test`、`qml_integrated_theme_test`、`qml_mini_player_test`、`qml_immersive_integration_test`、`waveform_coordinate_mapper_test` | 已验证 | 本轮聚焦通过；真实音频播放/人工 scrub 未覆盖。 |
| 深色、浅色、系统主题应用启动与截图 | `qa-main-smoke.ps1` 通过；`qa-final-ui-matrix.ps1` 在 zh、dark/light/system、playback/mini/settings 共 9 张截图通过 | 部分验证 | 截图当前选择的是 RGB 波形，并未通过可复现 QA 参数把运行时切至频彩模式，不能作为曜釉波形视觉验收。 |
| 100/125/150/200% DPI、720p/1080p/2K/4K、窄/宽/全屏、真实频彩视觉矩阵 | 无 | 未验证 | 当前矩阵脚本未公开频彩模式、DPI 或窗口尺寸维度；本任务未擅自改动该共享 QA 脚本。 |
| 频彩长时间播放进度不重建几何 | R31 `frequency-stress-600s.json` | evidence-only | 见下方 600 秒记录；需可识别干净候选重跑。 |
| 非频彩加载/CPU 回归 <= 3% | R29 robust ABBA 原始样本 | `ENVIRONMENT_UNSUITABLE` | 严格 MAD 不满足，不能下通过结论。 |
| Release 可执行文件和资源净增 <= 1 MiB | R29 app tree：218,728,000 B → 219,377,087 B，增量 649,087 B | evidence-only | 数值在 1 MiB 内，但与 R29 同属不能重现的历史 overlay 证据。 |

## 本轮执行记录

以下命令均在隔离候选与同一 Release 构建目录执行；`VsDevCmd.bat -arch=x64 -host_arch=x64`、Qt 6.7.0 和上述 vcpkg `bin` 均在同一进程环境中加载。

1. `cmake --build build/frequency-task8-r24-candidate-v2 --target all -j 2`：退出 `0`，377/377 成功。
2. Task9 指定聚焦 CTest：

   ```powershell
   ctest --test-dir build/frequency-task8-r24-candidate-v2 -R '^(decoder_test|waveform_analyzer_test|frequency_color_waveform_analyzer_test|waveform_cache_test|frequency_color_waveform_cache_test|settings_controller_test|waveform_provider_test|waveform_coordinate_mapper_test|waveform_item_test|qml_waveform_test|qml_audio_visual_impact_test|qml_main_window_test|qml_integrated_theme_test|qml_mini_player_test|qml_immersive_integration_test|frequency_color_waveform_stress_test)$' --output-on-failure
   ```

   退出 `0`，**16/16 通过**，47.67 s。
3. `scripts/qa-main-smoke.ps1 -BuildDirectory build/frequency-task8-r24-candidate-v2`：退出 `0`。真实 `AgPlayer.exe` 打开 sine-440 Hz fixture，截图为 `40,773 B`，日志没有 WARN/ERROR/FATAL。
4. `scripts/qa-final-ui-matrix.ps1 -BuildDirectory build/frequency-task8-r24-candidate-v2 -OutputDirectory artifacts/frequency-color-waveform/task9-ui-matrix -Languages zh -Themes dark,light,system -Surfaces playback,mini,settings`：退出 `0`，9 captures。产物目录：`D:\ai\AgPlayer\.worktrees\codex-task8-r24-candidate\artifacts\frequency-color-waveform\task9-ui-matrix`。
5. 全套 `ctest --test-dir build/frequency-task8-r24-candidate-v2 --output-on-failure -j 2`：退出 `8`，**136/140 通过**，220.29 s。失败详见下一节。

## 完整套件失败（阻止整体通过）

这些失败发生于隔离候选；它们不属于频彩目标，但也不能被掩盖或归类为频彩已通过的依据。

| CTest | 结果 | 精确观察 |
| --- | --- | --- |
| `window_controller_test` | SEGFAULT | 全套运行中在 `dockedListFollowsMainWindow` 观察到 `0xc0000005`，栈顶 `QGuiApplicationPrivate::processFocusWindowEvent`；本轮未单独复跑。 |
| `integrated_shell_lifecycle_smoke_test` | Timeout | `AgPlayer.exe --qa-test-mode --qa-integrated-shell-lifecycle-probe ...` 在 20.08 s CTest watchdog 超时。 |
| `window_mixed_dpi_transition_test` | Failed | Windows QPA 下 `window_controller_test.exe auxiliaryWindowsKeepNativeSizeAcrossScreens -o -,txt` 在 19.23 s 失败；CTest 未输出更细的断言文本。 |
| `windows_shell_runtime_test` | Failed | 11.18 s，`AgPlayer did not publish one taskbar window with complete shell identity`。 |

## R29 普通波形性能结论

严格协议固定单核 affinity、进程内 High priority、相同预热、至少 30 次内部分析、随机平衡 ABBA（至少 10 对）、原始样本和 bootstrap 区间。R29 结果为：

| 指标 | 值 | 门槛 | 结论 |
| --- | ---: | ---: | --- |
| 冷路径配对中位回归 | -1.5664% | <= 3% | 单一中位数不构成通过。 |
| 冷路径相对 MAD | 7.2637% | <= 3% | 超限。 |
| 热路径配对中位回归 | -5.2542% | <= 3% | 单一中位数不构成通过。 |
| 热路径相对 MAD | 9.7411% | <= 3% | 超限。 |

最终标记为 **`ENVIRONMENT_UNSUITABLE`**。原始证据：
`D:\ai\AgPlayer\.worktrees\codex-task8-r24-candidate\artifacts\frequency-color-waveform-r29-protocol-short-3\frequency-color-metrics.json`、`environment-preflight.json` 和 `frequency-color-summary.md`。

## 600 秒频彩压力记录（evidence-only）

原始 JSON：`D:\ai\AgPlayer\.worktrees\codex-task8-r24-candidate\artifacts\frequency-color-waveform-r31-600s\frequency-stress-600s.json`。

| 项目 | 实测值 | 限制 | 结果 |
| --- | ---: | ---: | --- |
| 固定进度更新 | 36,000 / 36,000 | 600 s × 60 Hz | PASS |
| 几何 revision | 1 | 进度中不重建 | PASS |
| 材质 revision | 36,001 | 每次进度可变 | PASS |
| 进度进程 CPU time | 969 ms | 记录项 | 已记录 |
| RSS 增长 | 192,512 B | <= 8 MiB | PASS |
| 冷分析 ms | 23, 18, 17, 18, 20 | 记录项 | 已记录 |
| 五个 FCW1 | 6,064 B each | < 8,192 B | PASS |
| 五次 decoder open | 1, 1, 1, 1, 1 | exactly 1 | PASS |
| Task8 overlay focused CTest | 5/5，8.37 s | 通过 | PASS |

上述运行源自 Task8 已验证的干净候选工作树，但未同时记录完整 clean-state 和可执行文件 SHA；根据 Task8 的身份规则，保留为 **evidence-only**，而不是可复现发布门禁。

## 未验证与硬件环境限制

- 未证明 GPU/RHI 加速路径像素输出；本机 QA 已使用 offscreen/software 路径，不能替代真实 GPU 读回。
- 未用真实音频设备验证连续播放、underrun、实际拖动与设备切换；sine fixture smoke 只证明应用可启动、播放参数和截图管线。
- 未完成频彩模式下的深/浅截图、灰阶/色觉缺陷模拟、低亮度显示器检查；现有 9 张矩阵截图不含频彩模式。
- 未完成 100/125/150/200% DPI、720p/1080p/2K/4K、窄/宽/全屏矩阵；脚本当前没有公开这些控制维度。
- 完整 CTest 有四项真实失败，见上表；因此不能发布或打包。

## 后续验收条件

1. 先修复或明确隔离并批准完整 CTest 的四项失败；全套须无未解释失败。
2. 给 QA 矩阵提供确定性的频彩模式、DPI 与窗口尺寸输入，再捕获曜釉深/浅与系统主题的真实频彩截图，并做可标注的灰阶/色觉模拟。
3. 在清洁、SHA 可识别的候选上重跑 R31 600 秒驱动和 R29 协议；若 R29 仍因环境离散而不可判定，保留 `ENVIRONMENT_UNSUITABLE`，不得转换为通过。
4. 在可用 GPU 与真实音频设备上完成播放、scrub、缩放、DPI 和设备切换复核后，才可评估打包/发布。
