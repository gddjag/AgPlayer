# Qt 6.7.0 Windows 退出期 DPI 崩溃诊断与兼容修复

日期：2026-09-05。范围：隔离工作树 `full-ui-performance-20260905`，Windows Release / Qt 6.7.0。

## 现象与根因证据

全页面截图矩阵中，部分页面成功保存 PNG 后进程以 `-1073741819`（`0xc0000005`）退出。经典播放页、列表页以及纯设置/工具页均出现，不依赖播放或波形生成；其他页面偶尔正常退出不能消除这个失败。

Windows Application Error 事件 1000 在本次隔离应用的多次失败中均记录 `Qt6Quick.dll` 版本 `6.7.0.0`、故障偏移 `0x55D20`、异常 `0xc0000005`。两份实际 minidump 均显示：

- 异常指令对应 `QQuickItem::flags()`，模块 RVA `0x55D20`；`RCX`（`this`）为零，尝试读取地址 `0x8`。
- 栈顶返回地址为 Qt6Quick RVA `0xFC698`，位于 `QQuickWindow::physicalDpiChanged()` 导出入口 `0xFC5B0 + 0xE8`。
- 对本机 Qt6Quick.dll 的反汇编显示，`0xFC651` 从窗口私有对象读取 `contentItem`，`0xFC660` 检查空值并跳过第一处更新；随后 `0xFC684` 再次读取同一 `contentItem`，`0xFC690` 将其传入 `RCX`，`0xFC693` 无空值保护地调用 `flags()`，返回地址正是 `0xFC698`。

这与官方 Qt 6.7.0 实现一致：`physicalDpiChanged()` 只为 `updatePixelRatioHelper()` 检查 `contentItem`，随后无条件调用 `forcePolish()`；后者只检查 screen，直接将 `contentItem` 传入调用 `item->flags()` 的 helper。`QEvent::DevicePixelRatioChange` 进入这个处理路径。[官方 Qt 6.7.0 qquickwindow.cpp](https://raw.githubusercontent.com/qt/qtdeclarative/v6.7.0/src/quick/items/qquickwindow.cpp)

结论：此次转储确认的是窗口内容已销毁后的 DPI 通知触发 Qt Quick 空指针访问。它不支持将故障归因于本轮 waveform worker 或元数据线程。进程退出事件和实际对象销毁时序仍由后续复验覆盖。

## 转储保全

原始来源为 `%LOCALAPPDATA%/CrashDumps`，已复制且只保留以下两份诊断转储，避免 WER 自动清理；转储没有纳入版本控制。

| 隔离工作树内路径 | SHA-256 |
| --- | --- |
| `build/qa/dpi-shutdown/AgPlayer.exe.11336.dmp` | `F5716D8A605B18410A479A812485B9918A4ABFE38212A0B95D22720FCCBB0415` |
| `build/qa/dpi-shutdown/AgPlayer.exe.33252.dmp` | `533BC724A488C773A113D178036260CDD6628D41DBF9935F0F4FCA1B1920AF52` |

工作树绝对目录：`D:/ai/AgPlayer/.worktrees/full-ui-performance-20260905`。

## 最小兼容修复

仅在 `app/main.cpp` 增加应用事件过滤器，且同时限定 Windows 和编译 Qt 版本恰为 `6.7.0`。只有以下三个条件全部满足才消费事件：

1. 事件类型为 `QEvent::DevicePixelRatioChange`；
2. 接收对象为 `QQuickWindow`；
3. 该窗口的 `contentItem()` 已为空。

正常窗口的 DPI 变化、其他事件和其他对象全部沿现有路径处理。过滤器是 `QApplication` 创建后、`QQmlApplicationEngine` 创建前的栈对象，因此覆盖 QML engine 的完整析构过程，并在应用析构前自动解除注册。没有新线程、定时轮询、依赖或对波形/播放生命周期的改动。

官方 Qt 6.7.3 源码仍含相同的缺失检查，不能将升级到 6.7.3 当作已确认的修复；本次不扩大版本支持声明。[官方 Qt 6.7.3 qquickwindow.cpp](https://raw.githubusercontent.com/qt/qtdeclarative/v6.7.3/src/quick/items/qquickwindow.cpp)

## 验证状态

- 已完成：两份真实转储交叉核对、Windows 事件核对、故障 DLL 导出符号及指令偏移核对、官方源码比对、转储 SHA-256 复核和补丁 Diff Review。
- 诊断阶段待统一执行的构建、截图和 DPI 检查现已完成：隔离任务现行页面 44 张及实际 DPR 为 1.25/1.5 的 20 次捕获均正常退出；主线集成后 Release、all_qmllint、163/163 CTest 与深浅主题 18 张捕获通过。详见 `2026-09-05-full-ui-performance-results.md`；不将这些结果外推为所有图形后端或所有崩溃原因均已排除。
- 不使用重试成功覆盖首次失败，不通过关闭所有 DPI 事件、延长退出等待或强制结束进程规避验收。
