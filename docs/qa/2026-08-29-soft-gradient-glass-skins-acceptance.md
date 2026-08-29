# 柔和渐变毛玻璃皮肤：Task 8 验收记录（2026-08-30）

## 结论

**状态：主题功能与 Product Design 视觉验收通过；允许进入跨分支集成，不等同于最终发布批准。** Phase A 基线 `49d2164` 的 Windows Release 全量 CTest 为 `120/120`；Debug 为 `117/121`，四个失败均已串行复现并保留。随后至 Task 8 最终 HEAD `0e6b93f` 的真实 Details、多语言和 QM 修正通过聚焦 Release 复测，但没有再次执行全量 CTest。五套推荐渐变、自定义 Solid/Gradient、紧凑 Picker、Default 蓝色控件/固定紫色播放中行、Generated 自适应播放中表面、四语言布局、真实 WAV 播放中切换、Windows System Light/Dark 和冷启动稳定状态均有本轮证据。

本记录不把截图当成交互 QA、读屏器验证或 WCAG 认证；也不把非等时长性能采样解释为无泄漏或性能通过。macOS/Linux 未实机。

Task 8 最终功能/视觉基线为 `codex/full-custom-theme-engine@0e6b93f`；Fresh 构建和全量 CTest 的 Phase A 基线为 `49d2164`，最终 HEAD 的聚焦复测范围在下表单列。证据根目录：

```text
build/qa/2026-08-29-soft-gradient-glass-skins/
```

## 环境与主要命令

工具版本记录在 `phase-a-tool-versions.log`：Visual Studio 2022 Developer PowerShell 17.8.3、MSVC x64 19.38.33133、CMake 4.4.0、Ninja 1.13.2、Qt 6.7.0。

主要可重复命令：

```powershell
cmake --fresh --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug --parallel
cmake --fresh --preset windows-msvc-release
cmake --build --preset windows-msvc-release --parallel

ctest --test-dir build/release --output-on-failure
ctest --test-dir build/debug --output-on-failure

cmake --build build/release --target all_qmllint --parallel
cmake -DROOT='D:/ai/AgPlayer/.worktrees/full-custom-theme-engine' `
  -P cmake/CheckQmlThemeColors.cmake

scripts/qa-main-smoke.ps1 -BuildDirectory build/release
scripts/qa-final-ui-matrix.ps1 -BuildDirectory build/release `
  -OutputDirectory build/qa/2026-08-29-soft-gradient-glass-skins/matrix

git diff --check
```

视觉矩阵分批运行以覆盖推荐项、Default 四语言、自定义极端色和 Picker；精确参数与逐项结果保存在各批次的 `matrix.csv` 和同名日志中。

## 构建、测试与静态检查

| 检查 | 结果 | 证据和边界 |
| --- | --- | --- |
| Fresh Debug 配置/构建 | PASS（`49d2164`） | 配置 exit 0；构建 395 steps，exit 0。 |
| Fresh Release 配置/构建 | PASS（`49d2164`） | 配置 exit 0；构建 358 steps，exit 0。 |
| Release 聚焦主题集合 | PASS，7/7（`49d2164`） | ThemeManager、Settings、Picker、静态/翻译、主/迷你窗口相关测试。 |
| Release 全量 CTest | **PASS，120/120，234.51 s（`49d2164`）** | `phase-a-postfix-full-release.log`。 |
| Debug 全量 CTest | **PARTIAL，117/121，349.87 s（`49d2164`）** | `phase-a-postfix-full-debug.log`；四项失败未忽略。 |
| 最终 HEAD 聚焦 Release | **PASS，6/6（`0e6b93f`）** | translation manager/catalog/context、`qml_main_window_test`、`qml_color_picker_test`、phase6 coverage；最终增量未再次跑全量。 |
| `all_qmllint` | PASS，exit 0 | 仅 `Theme.qml` 现有未使用 `import AgPlayer` 信息。 |
| QML 静态颜色分类 | PASS | 输出 `QML theme color classification passed`。 |
| `git diff --check` | PASS | 验收修正后无空白错误。 |

### Debug 保留失败

| 失败项 | 串行复现症状 | 为什么不计为主题通过 |
| --- | --- | --- |
| `audio_document_test` | Debug-only fade 状态读到无效负值，而不是预期 `800/200`；Release 通过 | 仍是实际 Debug 失败，不能被 Release 结果掩盖。 |
| `import_controller_test` | 约 `35.02 s` 超时；Task 3 已记录同一症状；Release 通过 | 基线/基础设施问题未在本主题范围修复。 |
| `qml_main_window_test` | CTest 约 `35.04 s` 超时；解除 CTest 超时后直接运行约 74 秒，结果为 99 passed / 10 failed / 1 skipped，失败涉及当前曲目、Seek 与拖放状态；Release 通过 | 不只是基础设施超时；Debug 的实际状态失败同样保留，不声称 Debug 全绿。 |
| `runtime_deployment_test` | 在 Debug 部署中错误要求 Release 名称 `Qt6Core.dll` | 同一构建的 `debug_runtime_deployment_test` 通过，但原测试仍失败。 |

在最初修正前还出现过 `qml_audio_editor_test` 的旧玻璃颜色期望；测试合同修正后 Release/Debug 定向复跑都通过，Release 全量随后达到 `120/120`。它不是上述最终四个 Debug 保留失败之一。

## 视觉矩阵与证据校正

### 初始 283 张：只保留为结构证据

初始矩阵共 `283` 行、`283` 张 PNG 和 `283` 份逐图日志，全部 CSV 行为 `PASS`：

- 五套推荐：`5 x zh x Light/Dark x 10 surfaces = 100`。
- Default：`zh/en/th/vi x Light/Dark/System x 10 surfaces = 120`。
- Custom 黑色 Solid、白色 Solid、RGB Gradient：`3 x zh x Light/Dark x 10 surfaces = 60`。
- Picker Start/Middle/End：`3`。

这些图均通过尺寸、非空和日志等结构检查，但 Product Design 并排复核发现原始 `28` 对 `list/details` 像素完全相同，证明旧 `details` 捕获没有真正打开详情。故 `283/283` 不能当作最终详情视觉证据，也不能单独证明交互正确。

### 修正后的真实 Details 与多语言证据

QA 捕获改为调用真实列表详情路径，并验证详情面板已经出现；脚本增加 `list/details` 差异必须 `>= 1%` 的门禁。

| 证据集 | 数量 | 可复核结果 |
| --- | --- | --- |
| `rerun-multilingual-fix/settings-default-en-th-vi/` | `en/th/vi x Light/Dark/System = 9` 张设置图 | `9/9` CSV PASS、9 份空日志；中文残留消失，泰/越长标签不再与控件重叠。 |
| `rerun-multilingual-fix/` | 全部 `28` 对真实 list/details，即 56 张图 | 每对四像素网格差异 `19.14%–21.49%`，均超过 1%；56 份日志为空。 |
| `rerun-side-navigation-fix/details-default-en-th-vi/` | `en/th/vi x Light/Dark/System x list/details = 18` 张 | `18/18` CSV PASS；九对差异 `19.16%–19.30%`；18 份日志为空。 |

最终 18 张 SideNavigation 图逐张检查：英文显示 `Resource Folders`，泰文显示 `โฟลเดอร์ทรัพยากร`，越文使用侧栏既定宽度内的截断；没有可见中文残留或详情标签/值重叠。设置页窄侧栏的预期省略仍保留在分配列宽内，不判为裁切缺陷。

### Product Design 结论

最终并排检查使用了用户参考图和实际运行截图。结论为 **设计批准 / 视觉通过**：

- 五套推荐渐变有可辨识差异，整体保持柔和，不再表现为只改 Accent/Highlight。
- 背景、卡片、Hover/Pressed、边界、文字和普通图标形成同色层级；Generated 整窗变色成立。
- 自定义黑/白仍保持无色相身份，RGB 三色渐变可辨识。
- Picker 比旧系统对话框明显更小，三 Stop 编辑状态清楚。
- 多语言设置与详情修正后的层级、间距和标签/值关系可接受。

该批准是当前参考图与 Windows 截图的视觉判断。截图不是键盘/鼠标交互测试、真实读屏器测试或 WCAG 审计；对比度阈值来自 C++ 算法测试。实现也没有真实背景模糊或 Shader，“毛玻璃”只指半透明同色层、边界和高光形成的视觉效果。

## Default、Generated 与固定媒体色

专用播放中行证据：

- `phase-c-current-row-default-light.png`：Default Light 行背景取样 `#D9C6ED` over `#FFFFFF`。
- `phase-c-current-row-default-dark.png`：Default Dark 行背景取样 `#402E57` over `#17181B`。
- `phase-c-current-row-generated-rgb-light.png`：Generated RGB Light 行背景取样 `#C7BAF0` over `#F0EEF6`。

前两项是固定 `rgba(#8F57C9, 0.34)` 的实际合成结果；第三项证明 Generated 的 `currentTrackSurface` 会随皮肤适配，而不是错误沿用 Default 紫色。播放中指示条在三图均为固定媒体色 `#E62E9B`，缩略波形采样均包含固定调色板色 `#9F1239`。Default 的 Switch/激活控件仍由精确回归测试锁定为 `#007AFF`。

## 真实 WAV、System 外观与冷启动

运行时使用 Release 应用和真实文件 `build/release/tests/fixtures/sine-440hz.wav`（352,844 bytes）：

- `scripts/qa-main-smoke.ps1`：PASS；界面显示 `0:01/0:02` 和暂停按钮，日志为空。
- 同一进程依次切换 Default -> Aurora -> Sunset -> black Solid -> white Solid -> RGB Gradient；两秒 WAV 循环持续推进，播放状态没有可见中断。
- 波形/频谱设置在切换前后保持 `#00B4A0/#00D4FF/#7B2FF7/#E62E9B`。
- 本项证明短 WAV 的真实播放器路径和运行中切换，不等于声卡听音、长音频、设备切换或多编码器验收。

Windows System 路径也做了真实 Light/Dark 变更与广播：

- 冷启动证据：`phase-c-system-cold-light-startup.png`、`phase-c-system-cold-dark-startup.png`，日志均为空。
- 同一 PID 在 WAV 播放状态下从 Light 切到 Dark，界面实时变化且日志为空，进程正常退出。
- 冷启动图是在应用 ready 后再稳定约 1.5 秒抓取，不是 literal first-paint 仪器测量，因此不宣称已量化“首帧绝无跳色”。
- HKCU Personalize 的四个原值在 `finally` 路径恢复并重播系统消息；`phase-c-system-theme-restored.json` 记录 `MatchesOriginal=true`。

## 性能观测：不能形成 A/B 结论

探索性同进程运行约 `571.015 s`，观察到 Working Set 增加约 `206.6 MB`。后续 Default-only A 运行 `115.423 s`，主题切换 B 运行 `721.548 s`；B 包含串行 UI 操作、视觉确认和闲置等待，时长与操作负载都不一致。

因此这些数值只能作为后续调查线索，不能隔离 Theme Engine 开销，也不能证明或否定内存泄漏。若要关闭风险，必须做等时长、等播放负载、预热一致的重复采样，必要时配合堆/分配分析。

## 状态恢复、平台边界与剩余风险

- Windows Personalize 注册表已经验证恢复。
- 原 QA AppData 已从 `AgPlayer-QA.task8-backup-20260830` 移回；生成态保存到 `phase-c-generated-qa-data-final`。`phase-c-qa-data-final-restoration.json` 记录路径和计数。
- 另一个 worktree 的 AgPlayer 进程仍可能继续写共享 `qttest/session.json`，所以不声称恢复后文件保持字节级静止；这不改变本轮恢复动作已经完成的事实。
- macOS/Linux 没有主机、构建、系统主题、视觉或播放实机证据。共享 Qt 事件路径和自动测试不能替代实机。
- 没有真实声卡听音、读屏器宣布、长时间等负载性能或多编码格式验证。
- Debug 四项保留失败仍是整体工程风险；本记录不能被表述为所有配置全绿。
- 跨分支集成、集成后全量回归和版本化安装器不属于 Task 8；它们必须在最终交付记录中另行给出证据。

## 验收判定

| 门禁 | 判定 |
| --- | --- |
| 完整 Theme Engine、五套推荐、Solid/Gradient | PASS |
| 紧凑应用内 Picker | PASS |
| Default 精确回归与媒体/语义色独立 | PASS |
| Release 构建/全量测试 | PASS，120/120（Phase A `49d2164`）；最终 `0e6b93f` 仅聚焦 6/6 |
| Debug 全量测试 | NOT GREEN，117/121（Phase A `49d2164`） |
| 最终多语言与真实 Details 视觉证据 | PASS |
| Product Design 视觉审查 | APPROVED |
| 真实 WAV 播放中切换 | PASS（短 WAV 路径 smoke） |
| Windows System Light/Dark 实时切换 | PASS |
| 冷启动 ready 后稳定图 | PASS（稳定状态证据） |
| 冷启动首帧无跳色 | NOT ESTABLISHED |
| 性能/内存无泄漏声明 | NOT ESTABLISHED |
| macOS/Linux 实机 | NOT RUN |
| 最终跨分支集成与安装器 | PENDING，Task 9 |
