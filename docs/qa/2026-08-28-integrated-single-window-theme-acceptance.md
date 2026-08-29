# AgPlayer Integrated 单窗口主题验收记录

日期：2026-08-28  
基线：`514816053dd6e201a176b00f2227f269aeec7a8e`

## 自动验证

- Debug 干净完整构建：通过。
- Debug 聚焦测试：播放器选区、窗口、波形坐标/缩放、设置、拖出、Shell 契约与生命周期全部通过。
- Release 完整构建：通过。
- Release 顺序全量 CTest：111/113 通过。本次新增/修改关联测试全部通过；剩余 `audio_editor_controller_test` 和 `qml_main_window_test`（20 秒超时）均为基线已知失败，不计作本功能通过。
- Release QML lint：通过；仅报告两个既有/必要 import 的 info 级提示。
- Release 真实导入、播放、Integrated 截图、正常退出 smoke：退出码 0。
- 独立终审发现的两个 P1 已关闭：选区面、拖出按钮和左右 Handle 均转发 Ctrl+滚轮缩放；精确波形时长缩短时重新夹紧/清除选区。新增回归测试在 Debug 与 Release 均通过。
- `git diff --check`：通过。

## 视觉证据

所有截图均来自真实 Release/Debug 程序、真实媒体文件和真实模型；没有固定演示封面或伪造列表。

- 1672×941 + 选区：`evidence/integrated-theme/integrated-1672x941-selection.png`
- Release 1672×941：`evidence/integrated-theme/integrated-release-1672x941.png`
- 1280×720：`evidence/integrated-theme/integrated-1280x720.png`
- 1440×900：`evidence/integrated-theme/integrated-1440x900.png`
- 缩放模拟：`integrated-dpi-125.png`、`integrated-dpi-150.png`、`integrated-dpi-200.png`
- 同尺寸参考对比：`evidence/integrated-theme/reference-vs-actual-1672x941.png`

100% DPI 下主要区域与参考图一致：顶栏、三栏、大波形、底栏均完整；Integrated 左栏按需求文档隐藏“标签管理”。1280×720 与 1440×900 未出现裁切、重叠或跨栏滚动。

## 体积与依赖

同一 MSVC Release 配置下：

| 项目 | 基线 | Integrated | 增量 |
|---|---:|---:|---:|
| EXE | 7,436,288 B | 7,683,072 B | 246,784 B |
| QML 源模块 | 813,052 B | 873,371 B | 60,319 B |
| 合计 | 8,249,340 B | 8,556,443 B | 307,103 B（299.91 KiB） |

增量低于 1 MiB。部署 DLL 集合与基线完全一致；没有新增第三方包。

## 尚未宣称通过的人工项目

- Windows 桌面、资源管理器、Premiere Pro、DaVinci Resolve、剪映的真实跨进程拖放：当前没有受控交互式目标应用会话，状态为 blocked/unverified。自动测试已验证阈值前零文件、阈值后单个有效 WAV URL、采样格式与帧误差。
- 同时滚动三栏、缩放、调 Handle、循环和拖出时的主观爆音/掉帧：自动 smoke 与压力测试未发现崩溃或持续任务，但未做真实声卡监听和长时间 GPU/内存采样，状态为 partial。
- DPI 截图为 Qt 缩放环境模拟；真实 100/125/150/200% 多显示器切换仍需人工检查。

本阶段未制作安装包。

## 2026-08-29 调整验收补充

### 自动验证

- Release 构建、`all_qmllint`、波形、Integrated 9 项、主窗口 107 项、主题契约和 Integrated 生命周期：通过。
- Release 顺序全量 CTest：111/114 通过；本轮相关测试全部通过。未通过项为：
  - `audio_editor_controller_test`：当前主机无可用录音采集后端，3 个录音能力断言失败；
  - `qml_format_converter_visual_fixture_test`：独立复跑 3 次为 2 次通过、1 次进程崩溃，属于与本轮文件无交集的既有退出不稳定；
  - `windows_shell_runtime_test`：Codex 桌面会话不允许 `SetForegroundWindow` 将已有播放器置前。
- Debug 构建、`waveform_item_test`、Integrated 9 项、Shell 契约和 Integrated 启停 smoke：通过。
- Debug 完整主窗口 QML 套件超过 CTest 的 35 秒固定上限，单独运行仍以非零状态退出；不能用 Release 通过掩盖，记录为未解决的 Debug 退出/超时风险。
- `git diff --check` 在提交前复核；QML lint 仅保留 `Theme.qml` 和 `WaveformSession.qml` 的两个 info 级 unused-import 提示。

### 最终视觉证据

- 当前实现：`build/evidence/integrated-theme-adjustments/integrated-1672x941-v4.png`
- 参考/实现合成：`build/evidence/integrated-theme-adjustments/comparison-source-left-impl-right-v4.png`
- 右栏局部：`build/evidence/integrated-theme-adjustments/comparison-right-panel-v4.png`
- 波形与底栏局部：`build/evidence/integrated-theme-adjustments/comparison-wave-footer-v4.png`
- 响应式：`integrated-1280x720-100.png`、`integrated-1440x900-100.png`
- DPI：`integrated-1280x720-125dpi.png`、`integrated-1280x720-150dpi.png`、`integrated-1280x720-200dpi.png`

首次截图发现并关闭了右侧页签漂移 P1；最终 v4 视觉审查为 P0/P1/P2 均 0。详细记录见项目根目录 `design-qa.md`。

### 体积复核

相对原基线 `5148160`：Release EXE + QML 源模块 + 两个用户提供 SVG 的合计增量为 451,799 B（441.21 KiB），低于 1 MiB 门槛。部署 DLL 与第三方包没有新增。

外部拖放到 Windows 桌面/文件夹、Premiere Pro、DaVinci Resolve、剪映以及真实声卡爆音/长时内存测试仍为 blocked/unverified；本轮没有制作安装包。

## 2026-08-29 第三轮视觉修复补充

- Debug：`qml_integrated_theme_test` 通过；受本次共享 `SearchFilter` 影响的 Classic 两个主窗口筛选/稳定网格用例单独通过。完整 `qml_main_window_test` 仍超过既有 35 秒 CTest 上限，未用 Release 结果掩盖。
- Release：应用与 QML lint 构建通过；主窗口、Integrated、波形、标签、列表、Shell 契约和生命周期聚焦回归通过。
- 主题颜色契约通过；所有新增运行时颜色均由 `Theme` token 管理。
- 最终 1672×941 程序截图：`evidence/integrated-theme/integrated-1672x941-soft-outlines-v2.png`。
- 同尺寸参考/实现合成：`evidence/integrated-theme/reference-vs-current-1672x941-soft-outlines-v2.png`。
- 视觉复核：表头、筛选/BPM/清空、标签输入、折叠图标、五个主容器软描边及波形导航玻璃条均满足最新反馈；这些本轮修复项的 P0/P1/P2 为 0。参考与实现使用不同歌曲、选区和标签数据，因此不把合成图作为完整内容状态的像素一致性证明。

本轮未增加部署 DLL、第三方包、图片、字体或渲染 Shader；未制作安装包。

## 2026-08-29 第四轮修复补充

- TDD 红灯：歌名区域不足 500px、换歌后仍保留 25,000–75,000ms 可见范围、波形与播放栏间距大于 8px，三个断言均按预期失败。
- 绿灯：Release/Debug `qml_integrated_theme_test` 均通过；换歌后等待新波形时长就绪并恢复完整范围。
- 视觉证据：`evidence/integrated-theme/integrated-1672x941-title-spacing-v3.png`；同尺寸合成为 `evidence/integrated-theme/reference-vs-current-1672x941-title-spacing-v3.png`。
- Runtime 日志仅含 INFO；中央播放控件仍为 x=668、width=300，520px 当前歌曲摘要没有发生重叠。

## 2026-08-29 第五轮修复补充

- TDD 红灯：主波形 `position` 仍为 0、`integratedPlayedWaveform` 裁切叠图仍存在、波形和播放栏实际间距大于 4px，断言按预期失败。
- 绿灯：主波形直接绑定真实播放位置，裁切叠图被移除；波形上下间距均为 4px，播放栏高度为 91px。
- 视觉证据：`evidence/integrated-theme/integrated-1672x941-single-waveform-v4.png`；同尺寸合成为 `evidence/integrated-theme/reference-vs-current-1672x941-single-waveform-v4.png`。
- Runtime 截图进程退出码为 0，日志仅含 INFO；本轮未增加依赖、图片资源、缓存或线程。
