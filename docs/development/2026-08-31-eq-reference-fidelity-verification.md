# 18 段 EQ 参考复刻验证记录

日期：2026-08-31

工作树：`D:\ai\AgPlayer\.worktrees\eq-18band-reference`

分支：`codex/eq-18band-reference`
基线：`7ec0f6a9675ed940cb8d844ae2a9d49b93a56cfc`

## 已实现

- 核心 EQ 从 17 个固定频段扩展为 18 个，在 8 kHz 与 12.5 kHz 之间加入真实 10 kHz peaking band；独立前级不计入 18 段。
- DSP 增益边界扩展到 ±18 dB；范围提供 ±6/±12/±18，精度提供 0.1/0.5/1.0 dB，均由控制器真实持久化和量化。
- 8 个内置预设的 ID、名称、原 17 段参数和前级保持不变；新增 10 kHz 固定为 0 dB。schema 3 将旧 17 段和 10 段数据安全迁移为 18 段。
- schema 3 / 18 段声明若缺少 `bandGains`、实际长度损坏，或声明 bandCount 与实际长度不一致，会降级为 `custom`；保存的内置预设 ID 不能绕过该校验。
- 输出电平读取已经过 EQ、ReplayGain、主音量、静音与淡入淡出的实际输出 sample peak；沿用实时回调中的乘法循环累计，不增加第二次 PCM 扫描。
- EQ 窗口实现启用、预设应用、保存、管理/改名/删除、重置、范围、精度、18 个频段、前级、旁路、自动防削波和输出表的真实绑定。
- 主图使用真实 `bandGain` 控制包络，按参考图等频段槽排列；纵轴仍严格采用线性 dB。
- 控制器仅在提交成功后递增 revision 并保存完整状态；提交失败时会回滚启用、旁路、保护、前级、范围、全部频段和当前预设到上一次成功 snapshot。
- 核心的 UI/C API `set_equalizer()` 路径在 DSP program 邮箱提交成功前不发布候选 settings/status；one-shot 测试 seam 可精确验证单次失败不会污染已发布状态，随后正常提交仍可继续。
- 英文资源和语言覆盖契约补齐 EQ 窗口新增的 16 个可见字符串；最终语言范围仍为中文和英文。

## 构建与聚焦验证

在 VS 2022 x64 环境中执行 Release 构建：

```text
cmake --build build/release --config Release --parallel 4
```

结果：通过；完整默认构建生成了应用、Worker 和全部测试可执行文件。最终审查补丁后又单独重建了 `AgPlayer`、`qml_main_window_test` 和控制器测试目标；没有生成安装包。

EQ 聚焦 CTest：

```text
ctest --test-dir build/release -C Release --output-on-failure -R '^(graphic_equalizer_test|audio_engine_test|equalizer_controller_test|qml_equalizer_visual_test|phase6_translation_coverage_test)$'
```

最终审查补丁后再次复验：5/5 通过，总计 20.10 秒：

- `audio_engine_test`：通过，2.05 秒。
- `graphic_equalizer_test`：通过，0.73 秒。
- `equalizer_controller_test`：通过，7.00 秒。
- `qml_equalizer_visual_test`：通过，7.32 秒。
- `phase6_translation_coverage_test`：通过，2.83 秒。

新增失败回滚后，`qml_equalizer_visual_test` 首次确定性暴露 3 项失败；根因是主窗口测试夹具仍注册 `nullptr` fallback EQ，所有 QML 编辑都被正确视为提交失败并回滚。夹具改为持有并注册真实 `EqualizerController(core_)` 后，同一测试由 4 passed / 3 failed 变为完整通过；生产失败回滚没有被放宽。

主窗口通用 QML 套件在最终夹具补丁后由主代理直接运行两次，均为 102 passed、9 failed、1 skipped；9 项是同一组曲目选择、波形 seek 和拖放用例。独立终审代理在相同最终夹具补丁、环境和可执行文件上第三次直跑得到 111 passed、0 failed、1 skipped、45.711 秒（原始日志 `%TEMP%\agplayer-main-review.txt`）。该通用套件继续记录为非 EQ 的顺序/时序波动，不能择优当作全绿证据；EQ 专用 QML 套件已在同一最终测试二进制上通过。唯一 skip 是 offscreen 平台下必须依赖原生 Windows 消息的 `WM_DROPFILES` 用例。

语言覆盖脚本最初按预期失败，因为它仍要求旧标题“十八段图形均衡器”；最终审查又先以缺失“预设：”英文覆盖失败。补齐 EQ 窗口新增的 16 个英文界面字符串和覆盖契约后，最终组合 focused 中 `phase6_translation_coverage_test` 通过。本功能不新增对旧泰语/越南语目录的集成依赖。

## 完整 135 项回归

全部测试目标构建后执行：

```text
ctest --test-dir build/release -C Release --output-on-failure --parallel 4
```

最终审查补丁后的最新一次结果：129/135 通过，6 项失败；同一轮中的 `audio_engine_test`、`graphic_equalizer_test`、`equalizer_controller_test`、`qml_equalizer_visual_test` 和语言覆盖测试均通过：

| 测试 | 最新完整套件结果 |
| --- | --- |
| `editor_playback_stream_test` | failed |
| `library_manager_controller_test` | timeout |
| `library_navigation_model_test` | timeout |
| `qml_main_window_test` | timeout |
| `audio_editor_feature_options_test` | timeout |
| `single_instance_test` | failed |

此前当前工作树完整回归曾得到 130/135 和 128/135；与最终 129/135 的失败集合和数量均不同，说明并行负载下非 EQ 套件结果存在波动。以上只记录当前工作树观察到的结果，不做基线归因，也不把隔离直跑通过扩大为“全套始终稳定”。

结论：最终审查补丁后的 EQ 构建和 5 项聚焦回归为绿色；完整 CTest 与主窗口通用套件不是全绿。本次没有越权修改全局 timeout，也未做基线归因。

## 视觉与交互验收

- 最终实现截图：`design-qa/eq-reference-implementation-1672x941.png`。
- 同画布对照：`design-qa/eq-reference-comparison-1672x941.png`，左侧参考、右侧实现，各 1672 × 941。
- 响应式证据：`design-qa/eq-reference-implementation-1180x680.png`、`design-qa/eq-reference-implementation-880x520.png`。
- 英文 Release 在最小 880 × 520 窗口实拍复核：顶栏 `Save / Manage / Reset` 均完整；管理弹层扩为窗口内 640 px 后，`Manage Custom Presets`、`Auto Clipping Protection`、`No Attenuation` 和 `0.0 dB` 均无截断或重叠，完整 Accessible name 仍保留。
- Pass 1：blocked，1 个 P1、3 个 P2。
- Pass 2：blocked，仅余响应图横轴 1 个 P2。
- Pass 3：passed，无 P0/P1/P2；仅保留 2–5 px 局部节奏、阴影和面板层次等 P3 差异。

QML 自动化实际操作了启用、预设、保存、管理、改名、删除、重置、范围、精度、键盘、滚轮、拖动、双击、前级、旁路和自动防削波；窄窗测试验证底栏输出文本与前级可完整滚动到可视区。

## 未验证与剩余风险

- 未进行真实声卡上的人工听音、爆音/失真/延迟观察，因此不声明硬件听感通过。
- 输出表是 post-processing sample peak，不是 true-peak limiter，也没有做硬件 true-peak 校准。
- 未执行 Debug 全量构建或安装包验证；按当前范围没有打包 EXE/安装程序。
- 完整 CTest 的固定 timeout 和一次主窗口时序波动仍需独立治理，不能描述为全仓库回归全绿。
- 采样率切换与 UI 同时提交 EQ 时，既有的 `publish_equalizer_for_rate()` 与 UI producer 仍不是一个跨 producer 原子事务，且采样率重发路径仍忽略 mailbox 返回值；本次只验证了明确的 UI/C API 单次失败不污染已发布状态，不扩大为所有 producer 的并发保证。
