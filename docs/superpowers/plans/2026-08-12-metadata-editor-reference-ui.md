# Metadata Editor Reference UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 完整复刻参考图的元数据编辑页面，并接通正式提示词规定的真实批处理、安全写入、转换复用和验收流程。

**Architecture:** 保留 QML 页面、Qt `MetadataEditor`、core `metadata_writer` 与 `FormatConverter` 四层边界。Core 产出能力/预检/结果模型，Qt 异步调度并暴露任务状态，QML 只组装标准 Edit Plan 和展示真实结果。

**Tech Stack:** Qt 6.7/QML、C++17、FFmpeg C API、QtConcurrent/QFutureWatcher、Qt Test、CMake/CTest。

## Global Constraints

- metadata-only 路径只允许同容器 packet stream copy，decoder/encoder 打开次数必须为 0。
- 只保留标题、艺术家、专辑、专辑艺术家、流派、年份、日期、作曲、BPM 和封面。
- 不重置或覆盖工作树中其他模块的未提交修改；不使用外部 `ffmpeg.exe`；不打包 EXE。
- 任何失败必须保留源文件、清理临时文件并返回结构化错误；应用不得崩溃。

---

### Task 1: Core 能力与结构化结果

**Files:**
- Modify: `core/src/metadata_writer.hpp`
- Modify: `core/src/metadata_writer.cpp`
- Test: `tests/core/metadata_writer_test.cpp`

**Interfaces:**
- Produces: `MetadataPreflightReport preflight_metadata_edit(...)`
- Produces: `MetadataFileResult write_metadata_plan(..., std::atomic_bool* cancel)`

- [ ] 写失败测试：稳定错误码、字段前后值/状态、封面结果、容器能力、年份/日期冲突和取消。
- [ ] 运行 `ctest --test-dir build/release-msvc -R ^metadata_writer_test$ --output-on-failure`，确认新断言失败。
- [ ] 增加预检/错误/验证模型和取消检查；显式复制章节与安全流，无法安全保留时返回 Unsupported。
- [ ] 增加同目录唯一暂存、空间/只读/目录可写/源变更检测和原子替换错误映射。
- [ ] 验证字段、封面和音频流等价结果，保证异常边界不越过 core。
- [ ] 重跑 core 测试至通过。

### Task 2: Qt 异步任务与三种作用范围

**Files:**
- Modify: `qt/src/metadata_editor.hpp`
- Modify: `qt/src/metadata_editor.cpp`
- Test: `tests/qt/audio_tools_end_to_end_test.cpp`

**Interfaces:**
- Consumes: Task 1 的预检与写入结果。
- Produces: `preflightMetadata(fields, indices)`、`applyPreflightDecision(policy)`、`cancel()`、任务统计和每文件状态。

- [ ] 写失败测试：当前/已选/全部范围、全部范围不受过滤影响、异步预检、取消、继续/遇错停止、结果统计和 JSON/CSV 导出。
- [ ] 运行 `audio_tools_end_to_end_test`，确认新用例失败。
- [ ] 将预检移入工作线程；把作用范围解析成稳定索引快照，拒绝空目标和空 Edit Plan。
- [ ] 在应用前缓存预检报告；存在不支持项时等待显式策略，不直接写入。
- [ ] 将取消令牌传到 core；为未开始文件生成 Cancelled 结果，并限制并发。
- [ ] 暴露 success/partial/failed/unsupported/cancelled 数量与逐文件阶段。
- [ ] 重跑 Qt 测试至通过。

### Task 3: 曲库刷新与转换模式复用

**Files:**
- Modify: `qt/src/metadata_editor.hpp`
- Modify: `qt/src/metadata_editor.cpp`
- Modify: `qt/src/format_converter.cpp`
- Modify: `qt/src/qml_registration.cpp`
- Test: `tests/qt/audio_tools_end_to_end_test.cpp`
- Test: `tests/qt/library_model_test.cpp`

**Interfaces:**
- Consumes: 标准 `MetadataEditPlan` 与成功文件结果。
- Produces: `LibraryModel.applyMaintenanceResults(...)` 更新和转换输出回读结果。

- [ ] 写失败测试：原位修改后曲库字段/封面刷新；转换模式输出新文件并保留源文件；不支持字段在转换前可见。
- [ ] 运行相关 Qt 测试，确认失败。
- [ ] 将 `LibraryModel` 注入 `MetadataEditor`，批量应用维护结果并失效封面/搜索缓存。
- [ ] 让 `FormatConverter` 在写 header 前消费同一 Edit Plan，并把实际回读结果返回页面。
- [ ] 重跑相关测试至通过。

### Task 4: 参考 UI 布局与完整交互

**Files:**
- Modify: `app/qml/AgPlayer/components/tools/MetadataEditPage.qml`
- Test: `tests/qml/tst_light_editor.qml`
- Test: `tests/scripts/audio_tools_layout_contract_test.ps1`

**Interfaces:**
- Consumes: Task 2/3 的状态、结果、统计和决策接口。
- Produces: 参考图的工具栏、双栏、十项编辑、封面、摘要和底栏交互。

- [ ] 写 QML 失败测试：字段三态、混合值、搜索/排序/筛选、作用范围、封面、应用前决策、按钮状态和结果统计。
- [ ] 运行 QML/布局契约测试，确认失败。
- [ ] 按参考图调整 62:38 双栏、表头/列宽/行高、右栏密度、深色层级、蓝色高亮和底栏。
- [ ] 接通文件/文件夹/播放列表、移除、清空、搜索、筛选、排序、全选和导出当前列表。
- [ ] 增加预检决策对话框、三态说明、状态枚举、真实进度和取消反馈。
- [ ] 保证 100%/125%/150%/200% 缩放及窄窗口时右栏仍可用。
- [ ] 重跑 QML/契约测试至通过。

### Task 5: 容器矩阵与故障注入

**Files:**
- Modify: `tests/core/metadata_writer_test.cpp`
- Modify: `tests/qt/audio_tools_end_to_end_test.cpp`
- Create: `docs/verification/metadata-editor-support-matrix.md`

**Interfaces:**
- Consumes: Task 1-4 完成实现。
- Produces: 当前 FFmpeg 构建的真实支持矩阵与安全证据。

- [ ] 为 MP3、FLAC、OGG、Opus、M4A AAC/ALAC、WAV 及构建可用的 APE/WMA 增加字段与封面矩阵测试。
- [ ] 增加损坏输入、只读、占用、空间不足、header/packet/trailer/验证/替换失败及源变更注入测试。
- [ ] 断言所有失败保留源文件哈希并清理 `.agmeta-*` 暂存。
- [ ] 记录实际 Supported/Unsupported，不把未测试格式记为支持。

### Task 6: Release 与桌面视觉验收

**Files:**
- Create: `design-qa.md`

**Interfaces:**
- Consumes: 完整 Release 构建和参考图片。
- Produces: 同状态截图对比、交互验收记录和最终通过/阻塞结论。

- [ ] 运行 Release 构建、`qmllint`、目标 CTest 集和 `git diff --check`。
- [ ] 启动构建目录中的 AgPlayer，用真实桌面 UI 完成添加、选择、搜索、预检、设为/清除/封面、取消、转换跳转和导出。
- [ ] 在与参考图相同视口截取实现，和原图并排检查布局、间距、字体、颜色、边框、圆角与裁切。
- [ ] 修复所有 P0/P1/P2 差异并重复对比，直到 `design-qa.md` 为 `final result: passed`。
- [ ] 输出修改文件、架构、stream-copy 证据、支持矩阵、安全测试、测试命令、未支持项，并明确未打包 EXE。
