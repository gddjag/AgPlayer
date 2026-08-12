# AgPlayer 波形时间轴精准同步系统升级 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 使渲染、播放游标、鼠标 Seek 与已播放颜色在所有窗口尺寸和 DPI 下共享唯一坐标系统，自动误差不超过 0.5 像素。

**Architecture:** 新增纯 C++ `WaveformCoordinateMapper`，由 `WaveformItem` 持有实际 Scene Graph 绘制宽度并暴露游标；不可变 PeakSnapshot 承载采样元数据，resize 只重映射 GPU 顶点。

**Tech Stack:** Qt 6 Quick/Scene Graph/Test、C++17、CMake/CTest。

## Global Constraints

- 逐条满足桌面需求文档，不降低 ≤0.5 pixel、GPU、resize 不重分析、DPI/多屏门禁。
- 工作树固定为 `D:\ai\AgPlayer\.worktrees\revised-ui`；保留全部既有未提交修改。
- 不引入新依赖，不重写波形系统，不更换 FFmpeg/WaveformCache。
- 自动化不能替代真实显示器、DPI、CPU和音频播放 QA；未验证不得写“已达成”。

---

### Task 1: 纯坐标映射器

**Files:** Create `qt/src/waveform_coordinate_mapper.hpp`, `qt/src/waveform_coordinate_mapper.cpp`; Test `tests/qt/waveform_coordinate_mapper_test.cpp`; Modify `qt/CMakeLists.txt`, `tests/CMakeLists.txt`.

**Interfaces:** Produces `timeToPixel`, `pixelToTime`, `timeToSample`, `sampleToPeak`, `timeToPeak` 静态函数。

- [ ] 写表驱动失败测试，使用手算期望值覆盖 0/60000/150000/299999ms 与 500/1000/3840px。
- [ ] 构建测试并确认因映射器缺失而 RED。
- [ ] 实现输入夹取、连续映射和确定性整数取整。
- [ ] 运行测试确认 GREEN。

### Task 2: WaveformItem 单一坐标源

**Files:** Modify `qt/src/waveform_item.hpp`, `qt/src/waveform_item.cpp`; Test `tests/qt/waveform_item_test.cpp`.

**Interfaces:** Produces QML properties `renderWidth`, `waveformCursorX`; `timeForX()` consumes the same render width.

- [ ] 写 cursor/Seek/resize/played-boundary 失败测试并确认 RED。
- [ ] 由 `updatePaintNode()` 发布实际逻辑绘制宽度，全部坐标调用 mapper。
- [ ] 播放推进只更新颜色/游标；resize 复用 snapshot，不触发分析。
- [ ] 运行 WaveformItem 测试确认 GREEN。

### Task 3: PeakSnapshot 采样索引链

**Files:** Modify `qt/src/waveform_item.hpp`, `qt/src/waveform_item.cpp`, `qt/src/waveform_provider.hpp`, `qt/src/waveform_provider.cpp`; Test `tests/qt/waveform_item_test.cpp`, `tests/qt/waveform_provider_test.cpp`.

**Interfaces:** PeakSnapshot stores `totalSamples`, `sampleRate`, `peakCount`; mapper consumes them for continuous peak position.

- [ ] 写元数据传播与 time→sample→peak 失败测试并确认 RED。
- [ ] 扩展 provider 结果与 item snapshot，保留旧 QML 输入兼容。
- [ ] 确保 resize 不修改 snapshot revision 或请求 provider。
- [ ] 运行 provider/item 测试确认 GREEN。

### Task 4: QML 游标与交互收口

**Files:** Modify `app/qml/AgPlayer/components/PlayerPane.qml`; Test `tests/qml/tst_waveform.qml`, `tests/qml/tst_main_window.qml`.

**Interfaces:** QML displays `WaveformItem.waveformCursorX`; committed seek uses `WaveformItem.timeForX()` only.

- [ ] 写实际点击、拉伸与游标重合失败测试并确认 RED。
- [ ] 删除 QML 中独立比例计算，绑定 C++ 属性。
- [ ] 运行 QML 波形测试确认 GREEN。

### Task 5: 全量验证与真实 QA

**Files:** Modify `docs/development/2026-08-12-waveform-timeline-precision-sync.md`.

**Interfaces:** Produces requirement-by-requirement evidence ledger.

- [ ] Debug 构建并运行 mapper/item/provider/QML 相关测试。
- [ ] 运行完整 CTest 与 `git diff --check`。
- [ ] 测量 resize 前后分析调用数、CPU 与内存分配行为。
- [ ] 在可用硬件上执行 800×500、1920×1080、3840×2160、100/125/150% DPI、多屏真实播放 QA。
- [ ] 只有证据齐全的条目标记 PASS；其余保持 BLOCKED/待验收。
