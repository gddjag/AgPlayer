# 18 段 EQ 参考复刻验证记录

日期：2026-08-31

工作树：`D:\ai\AgPlayer\.worktrees\eq-18band-reference`

分支：`codex/eq-18band-reference`
基线：`7ec0f6a9675ed940cb8d844ae2a9d49b93a56cfc`

## 已实现

- 核心 EQ 从 17 个固定频段扩展为 18 个，在 8 kHz 与 12.5 kHz 之间加入真实 10 kHz peaking band；独立前级不计入 18 段。
- DSP 增益边界扩展到 ±18 dB；范围提供 ±6/±12/±18，精度提供 0.1/0.5/1.0 dB，均由控制器真实持久化和量化。
- 8 个内置预设的 ID、名称、原 17 段参数和前级保持不变；新增 10 kHz 固定为 0 dB。schema 3 将旧 17 段和 10 段数据安全迁移为 18 段。
- 输出电平读取已经过 EQ、ReplayGain、主音量、静音与淡入淡出的实际输出 sample peak；沿用实时回调中的乘法循环累计，不增加第二次 PCM 扫描。
- EQ 窗口实现启用、预设应用、保存、管理/改名/删除、重置、范围、精度、18 个频段、前级、旁路、自动防削波和输出表的真实绑定。
- 主图使用真实 `bandGain` 控制包络，按参考图等频段槽排列；纵轴仍严格采用线性 dB。

## 构建与聚焦验证

在 VS 2022 x64 环境中执行 Release 构建：

```text
cmake --build build/release --config Release --parallel 4
```

结果：通过；完整默认构建生成了应用、Worker 和全部测试可执行文件。没有生成安装包。

EQ 聚焦 CTest：

```text
ctest --test-dir build/release -C Release --output-on-failure -R '^(graphic_equalizer_test|audio_engine_test|equalizer_controller_test|qml_equalizer_visual_test)$'
```

结果：4/4 通过，总计 6.82 秒：

- `audio_engine_test`：通过，2.43 秒。
- `graphic_equalizer_test`：通过，0.34 秒。
- `equalizer_controller_test`：通过，0.73 秒。
- `qml_equalizer_visual_test`：通过，3.27 秒。

主窗口 QML 套件使用相同 Release 可执行文件直接运行并输出文本日志：111 passed、0 failed、1 skipped、43.921 秒。唯一 skip 是 offscreen 平台下必须依赖原生 Windows 消息的 `WM_DROPFILES` 用例。

语言覆盖脚本最初按预期失败，因为它仍要求旧标题“十八段图形均衡器”。按当前中文/英文语言规格同步 `zh/en` 的 `EqualizerWindow` 源字符串和覆盖契约后，`phase6_translation_coverage_test` 1/1 通过；本功能不新增对旧泰语/越南语目录的集成依赖。

## 完整 135 项回归

全部测试目标构建后执行：

```text
ctest --test-dir build/release -C Release --output-on-failure --parallel 4
```

结果：130/135 通过，5 项在 CTest 固定时限或并行负载下失败；没有一项是 EQ 聚焦测试：

| 测试 | 完整套件结果 | 隔离复核 |
| --- | --- | --- |
| `playback_controller_test` | 10.20 秒 timeout | 7.61 秒通过 |
| `track_waveform_thumbnail_provider_test` | timeout | CTest 单跑仍受 10 秒限制；直接运行 14.24 秒退出码 0 |
| `audio_preview_controller_test` | 10.20 秒 timeout | 2.61 秒通过 |
| `separation_worker_process_test` | 并行运行失败 | 3.91 秒通过 |
| `qml_main_window_test` | 36.08 秒 timeout | 直接运行 111/0/1，43.921 秒 |

此外，主窗口套件一次带逐条文本日志的运行出现 9 个拖拽/波形时序失败；使用相同命令立即复跑为 111/0/1。该现象记录为非 EQ 用例的时序脆弱性，未做基线归因，也未把复跑通过扩大为“全套始终稳定”。

结论：EQ 相关构建和聚焦回归为绿色；完整 CTest 不是全绿，剩余表现为 CTest 时限/并行时序问题，本次没有越权修改全局 timeout，也未做基线归因。

## 视觉与交互验收

- 最终实现截图：`design-qa/eq-reference-implementation-1672x941.png`。
- 同画布对照：`design-qa/eq-reference-comparison-1672x941.png`，左侧参考、右侧实现，各 1672 × 941。
- 响应式证据：`design-qa/eq-reference-implementation-1180x680.png`、`design-qa/eq-reference-implementation-880x520.png`。
- Pass 1：blocked，1 个 P1、3 个 P2。
- Pass 2：blocked，仅余响应图横轴 1 个 P2。
- Pass 3：passed，无 P0/P1/P2；仅保留 2–5 px 局部节奏、阴影和面板层次等 P3 差异。

QML 自动化实际操作了启用、预设、保存、管理、改名、删除、重置、范围、精度、键盘、滚轮、拖动、双击、前级、旁路和自动防削波；窄窗测试验证底栏输出文本与前级可完整滚动到可视区。

## 未验证与剩余风险

- 未进行真实声卡上的人工听音、爆音/失真/延迟观察，因此不声明硬件听感通过。
- 输出表是 post-processing sample peak，不是 true-peak limiter，也没有做硬件 true-peak 校准。
- 未执行 Debug 全量构建或安装包验证；按当前范围没有打包 EXE/安装程序。
- 完整 CTest 的固定 timeout 和一次主窗口时序波动仍需独立治理，不能描述为全仓库回归全绿。
