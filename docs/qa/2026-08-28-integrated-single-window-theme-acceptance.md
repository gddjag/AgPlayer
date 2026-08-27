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
