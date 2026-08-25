# 全自定义主题引擎：证据型预集成验收（2026-08-26）

## 结论

**状态：NEEDS WORK / 不批准最终发布。** Theme 功能的 Release 构建、主题聚焦测试、完整 Release CTest、当前运行截图和两种真实 WAV 播放启动状态均有本轮证据；但 Debug 全量仍有 3 个既有非主题失败，且 QA harness 尚不能抓取菜单、Popup、Tooltip 或同列表的播放中/仅选中双状态。本记录不执行分支集成、打包或安装器复制。

## 审核范围和命令

基线为 `679c56c03a1a9b05f24592cec413bb791092cd4f`，工作树在开始和结束时没有主题生产代码改动。本轮在 Visual Studio 2022 x64 Developer PowerShell（MSVC 19.38）中执行：

```powershell
cmake --fresh --preset windows-msvc-release
cmake --build --preset windows-msvc-release --parallel 4
cmake --fresh --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug --parallel 4
cmake -DROOT='D:/ai/AgPlayer/.worktrees/full-custom-theme-engine' -P cmake/CheckQmlThemeColors.cmake
ctest --test-dir build/release -R '^(theme_manager_test|settings_controller_test|qml_color_picker_test|qml_theme_color_contract_test|qml_audio_editor_test|translation_catalog_test|qml_main_window_test)$' --output-on-failure
ctest --test-dir build/debug -R '^(theme_manager_test|settings_controller_test|qml_color_picker_test|qml_theme_color_contract_test|qml_audio_editor_test|translation_catalog_test|qml_main_window_test)$' --output-on-failure
ctest --test-dir build/release --output-on-failure
ctest --test-dir build/debug --output-on-failure
git diff --check
```

Release、Debug 重新配置和构建均成功。最早一次在未加载 VS Developer Shell 的 Release 尝试无法解析 `<cmath>`；该命令环境无效，未用于判断产品质量，随后以 `cmake --fresh` 和正确环境完整重跑。

## 自动化结果

| 检查 | 结果 | 证据 / 精确边界 |
| --- | --- | --- |
| Release 聚焦主题/设置/QML/翻译 | PASS，7/7，22.86 s | 包含 `theme_manager_test`、`settings_controller_test`、`qml_color_picker_test`、`qml_theme_color_contract_test`、`qml_audio_editor_test`、`translation_catalog_test`、`qml_main_window_test`。 |
| Debug 聚焦同一集合 | PARTIAL，6/7 | 仅 `qml_main_window_test` 在 35.03 s 超时；其余六项通过。 |
| QML 静态色彩契约 | PASS | `CheckQmlThemeColors.cmake` 输出 `QML theme color classification passed`。 |
| Release 全量 CTest | PASS，107/107，149.34 s | 本轮完整运行，无保留失败。 |
| Debug 全量 CTest | BASELINE FAILURE，105/108，265.64 s | `audio_document_test`（0.26 s 无诊断失败）、`qml_main_window_test`（35.03 s 超时）、`runtime_deployment_test`（缺 `Qt6Core.dll`）。同一 Debug 构建的 `debug_runtime_deployment_test` 通过；Release 的对应项通过。 |
| `qmllint --bare`（SettingsPage、Theme、ThemeColorSelector 和两份相关 QML 测试） | WARNING BASELINE，exit -1 | 90 warning：`unresolved-alias` 9（全在 `SettingsPage.qml`）、`missing-type` 70（SettingsPage 67、ThemeColorSelector 3）、`incompatible-type` 11（Theme 的固定语义色字符串）。无运行时 QML 警告被截图脚本记录；Release QML 测试通过。 |
| `git diff --check` | PASS | QA 文档写入前后均无空白错误。 |

## 当前运行截图与人工目检

脚本：

```powershell
scripts/qa-final-ui-matrix.ps1 -BuildDirectory build/release `
  -OutputDirectory build/qa/2026-08-26-theme-preintegration/default `
  -Languages zh -Themes light,dark,system `
  -Surfaces startup,playback,mini,settings,list,details,tool-0,tool-1,tool-2,tool-3 `
  -Skin default
```

对 `#FF0000`、`#FFD400`、`#000000`、`#FFFFFF` 分别以同一脚本运行 Light/Dark/System 和 `playback,mini,settings,list,tool-0`。结果为 Default 30 + 四组各 15，共 **90** 张 PNG；每份 `matrix.csv` 的 `Result` 都是 `PASS`。脚本验证了目标尺寸、足够颜色/不透明样本、外角透明度、运行日志和 Light/Dark 采样差异。

我使用本地图片查看器实际检查了以下本轮产物，而不是只接受 CSV：

1. `default/zh-light-skin-default-settings.png`：设置卡片、十个色点、`#D27722` 自定义色、按钮和底部操作区完整；无右边裁切。
2. `default/zh-dark-skin-default-settings.png`：深色文字和边界清晰，色点/已选 Default 状态清楚。
3. `default/zh-dark-skin-default-playback.png`：WAV 播放暂停态可见，进度/波形和运输控制完整。
4. `default/zh-dark-skin-default-list.png`：列表筛选栏、八个条目、列标题和底部控件没有溢出。
5. `default/zh-dark-skin-default-mini.png`：迷你播放器标题、波形、时间和控制区完整。
6. `default/zh-dark-skin-default-tool-0.png`：音频编辑器工具栏、时间线、检查器和底部控制区可见，无空白截图。
7. `red/zh-light-skin-FF0000-settings.png`：生成的淡红表面、红色操作/选中态与自定义 `#FF0000` 一致；普通文字仍可读。
8. `yellow/zh-dark-skin-FFD400-settings.png`：暗黄表面与黄色控件层级可分辨；黄色主按钮的黑色文本可读。
9. `black/zh-light-skin-000000-list.png`：极端黑 seed 没有使浅色列表文字或筛选控件消失。
10. `white/zh-dark-skin-FFFFFF-tool-0.png`：极端白 seed 没有使深色工具页的启用/禁用控件不可辨。

可见的正向结果是 Default Light/Dark 的中性表面保持稳定；Red/Yellow 的普通控件和表面一起变色；Black/White 极端 seed 没有产生空白页或可见裁切；波形、录音、收藏/评分等语义/媒体色仍独立，符合规格。没有以“豪华/毛玻璃”作未证实质量声明；代码扫描只发现现有 `glassEffect` 的明确 `theme-color-allow` 使用。

## 交互、播放和性能证据

- `ThemeColorSelector` 的 Space/Enter、预设/自定义、事务取消/恢复和媒体色不变由上述 Release 测试覆盖；截图不能证明读屏器实际宣布或键盘焦点移动，因此不作辅助技术合规声称。
- Default 和 `#FF0000` 各启动一次 `AgPlayer.exe --qa-test-mode --qa-play build/release/tests/fixtures/sine-440hz.wav --qa-skin ... --qa-screenshot-main ...`。两次都生成有效截图和 QA 日志、没有 `[WARN]`/`[ERROR]`/`[FATAL]`、退出码 0，并在 30 秒守护时间内自行结束。产物在 `build/qa/2026-08-26-theme-preintegration/playback-performance/`。
- 每个启动仅采样本进程早期两个时间点：Default 0.30 s 为 CPU 0.125 s / Working Set 30.64 MiB，0.80 s 为 0.359 s / 56.70 MiB；Red 0.30 s 为 0.250 s / 57.12 MiB，0.80 s 为 0.375 s / 57.39 MiB。它们不是长期 CPU/内存基准，也不能证明热切换的性能。

## 未关闭的风险和集成门禁

1. Debug 的三项全量 CTest 失败必须维持为发布门禁：`audio_document_test`、`qml_main_window_test`、`runtime_deployment_test`。它们不在主题修改范围，但不应被 Release 成功掩盖。
2. QA CLI 仅支持截图 `startup/playback/mini/settings/list/details/tool-0..3`。没有 Menu、Popup、Tooltip 的显式打开参数；没有可同时设置播放中和仅选中列表行的 fixture。该缺口需要后续 QA harness 或人工交互验收关闭。
3. System 截图采用当前 Windows 外观（本轮为 dark）；没有变更宿主注册表或在真实 OS 外观改变时观察事件。`theme_manager_test` 覆盖逻辑路径，不替代该实机事件。
4. 本轮视觉矩阵只人工检查中文；英文、泰文、越南文的翻译目录自动测试通过，但未重新做布局/换行目检。
5. macOS/Linux 无真实主机、构建、播放或视觉证据；不能声称跨平台实机通过。
6. 后续集成前需重新检查分支祖先/patch-id，集成后重跑 Release 主题聚焦、静态契约、完整 Release CTest、代表截图与 WAV 冒烟；打包和安装器校验仅在这些门禁关闭后执行。

## 审核评级

- 主题功能证据：**Good**（Release 自动化和当前运行图片充分，未见本轮可见的主题回归）。
- 整体预集成健康度：**B- / NEEDS WORK**（Debug 基线失败和关键 QA 状态不可见仍阻止最终发布）。
- QA 代理：EvidenceQA；证据日期：2026-08-26。
