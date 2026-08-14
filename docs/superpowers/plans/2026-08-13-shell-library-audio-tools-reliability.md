# Shell, Library, and Audio Tools Reliability Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task with review checkpoints.

**Goal:** 修复 Windows 应用身份、评分颜色、曲库分类以及四个音频工具页面的真实交互与执行失败。

**Architecture:** 保留现有 Qt/QML 门面和 FFmpeg/音频编辑核心，在既有能力目录、预检、异步任务和事务式文件操作边界内补齐缺失行为。所有慢 I/O 留在工作线程；QML 只处理布局和输入。

**Tech Stack:** C++17、Qt 6/QML/Qt Test、FFmpeg libraries、CMake/CTest、Inno Setup。

## Global Constraints

- 工作分支：`codex/fix-shell-and-audio-tools-20260813`。
- 每个行为先写会失败的测试，再做最小修复并单独提交。
- 不启动外部 `ffmpeg.exe`，不引入新运行时，不打包。
- 自动测试不能替代真实任务栏固定和麦克风录音；这两项在交付中明确列为人工验收。

---

## Task 1: Windows 外壳身份与评分色

**Files:**
- Modify: `app/main.cpp`
- Modify: `app/qml/AgPlayer/theme/Theme.qml`
- Modify: `tests/scripts/installer_contract_test.ps1`
- Modify: `tests/qml/tst_main_window.qml`

1. 增加会失败的行为契约：窗口身份包含稳定 AppUserModelID 及 relaunch command/display/icon；评分索引 1–5 返回同一橘黄色。
2. 运行 `ctest --test-dir build/release-integration-msvc -C Release -R "installer_contract_test|qml_main_window_test" --output-on-failure`，确认失败。
3. 在 `applyWindowsShellIdentity()` 写入四个 Windows 属性并提交属性存储；失败只记录，不中断启动。
4. 将主题评分色统一为单一橘黄色，保留未评分色。
5. 重跑测试并提交 `fix: stabilize Windows shell identity and rating color`。

## Task 2: 最近添加与从未播放真实筛选

**Files:**
- Modify: `qt/src/library_model.cpp`
- Modify: `qt/src/library_store.cpp`
- Modify: `tests/qt/library_model_test.cpp`
- Modify: `tests/qt/library_store_test.cpp`
- Modify: `tests/qt/library_filter_model_test.cpp`
- Modify: `tests/qml/tst_main_window.qml`

1. 增加测试：新批次自动取得 `addedAtMs`；旧记录从文件时间迁移；点击两个分类后模型行数与预期一致；播放后歌曲退出“从未播放”。
2. 运行相关四个测试，确认旧实现至少在新增时间戳和真实点击用例失败。
3. 在 `LibraryModel::insertBatch()` 为缺失时间戳的记录写入批次时间；加载旧数据时用创建/修改时间迁移并持久化。
4. 确认 `markPlayed()` 发出的角色更新会使代理重新筛选，必要时补齐失效通知。
5. 重跑测试并提交 `fix: restore recent and unplayed library filters`。

## Task 3: 能力驱动的格式转换预设与预检

**Files:**
- Modify: `qt/src/format_converter.hpp`
- Modify: `qt/src/format_converter.cpp`
- Modify: `app/qml/AgPlayer/components/audio_tools/FormatSettingsPanel.qml`
- Modify: `app/qml/AgPlayer/components/audio_tools/FormatConverterPage.qml`
- Modify: `tests/qt/format_conversion_plan_test.cpp`
- Modify: `tests/qt/audio_tools_end_to_end_test.cpp`
- Modify: `tests/qml/tst_format_converter.qml`

1. 先增加表驱动测试，覆盖当前构建可用格式的推荐参数、非法模式/码率/采样率拒绝和质量参数传递；增加短 WAV 的真实转换并重新打开验证。
2. 运行 `format_conversion_plan_test`、`audio_tools_end_to_end_test`、`qml_format_converter_test`，确认失败。
3. 为能力映射增加推荐/高质量/兼容配置和可选参数；在 `buildPreflight()` 冻结、校验并展示实际参数。
4. 将 quality、bitrate mode、sample rate、sample format、channel layout 从 QML 贯通到 `runTranscode()`；去除硬编码 quality。
5. QML 增加预设选择和可折叠高级参数，只显示当前格式支持项，声道默认自动。
6. 重跑三项测试和 `format_matrix_test`，提交 `fix: validate and execute smart transcode profiles`。

## Task 4: 波形选区、播放头与缩略导航

**Files:**
- Modify: `app/qml/AgPlayer/components/audio_tools/EditorWaveformCanvas.qml`
- Modify: `app/qml/AgPlayer/components/audio_tools/OverviewNavigator.qml`
- Modify: `tests/qml/tst_audio_editor.qml`
- Modify: `tests/qt/editor_viewport_test.cpp`

1. 增加真实鼠标测试：拖播放头不改变选区；拖左右边缘只调整一侧；拖缩略视口无首次跳变且连续更新。
2. 运行 `qml_audio_editor_test` 和 `editor_viewport_test`，确认失败。
3. 增加播放头手柄、左右选区命中区及明确的交互优先级；空白区域才允许新建选区。
4. 缩略导航保存按下偏移，拖动时按同一时间映射连续更新。
5. 重跑测试并提交 `fix: make editor waveform handles directly draggable`。

## Task 5: 非阻塞录音与速度/音高面板

**Files:**
- Modify: `qt/src/audio_editor/audio_editor_controller.hpp`
- Modify: `qt/src/audio_editor/audio_editor_controller.cpp`
- Modify: `app/qml/AgPlayer/components/audio_tools/RecordingInspectorSection.qml`
- Modify: `app/qml/AgPlayer/components/audio_tools/TimePitchInspectorSection.qml`
- Modify: `tests/qt/audio_editor_controller_test.cpp`
- Modify: `tests/qml/tst_audio_editor.qml`

1. 增加控制器测试：设备启动失败异步返回且状态可恢复；停止/收尾不冻结；速度、BPM、半音、音分和复位保持一致。
2. 增加 QML 点击测试：录音开始/暂停/继续/停止/刷新设备与速度音高预览/应用/复位均调用真实控制器接口。
3. 运行 `audio_editor_controller_test` 和 `qml_audio_editor_test`，确认失败。
4. 用 `QtConcurrent`/`QFutureWatcher` 执行可能阻塞的录音启动和收尾，所有状态回写切回 GUI 线程。
5. 将右栏改为统一栅格，提供可见操作按钮和双向绑定，保持窄窗口可滚动且不溢出。
6. 重跑测试并提交 `fix: restore responsive recording and time pitch controls`。

## Task 6: 元数据字段编辑闭环

**Files:**
- Modify: `app/qml/AgPlayer/components/audio_tools/MetadataEditPage.qml`
- Modify: `tests/qt/audio_tools_end_to_end_test.cpp`
- Create: `tests/qml/tst_metadata_editor.qml`
- Modify: `tests/CMakeLists.txt`

1. 增加真实交互测试：直接输入使字段进入“设为”、清除进入“清除”、应用按钮启用；真实文件写回后重新读取值。
2. 注册并运行 `qml_metadata_editor_test` 和 `audio_tools_end_to_end_test`，确认失败。
3. 为字段 delegate 增加显式 id；所有内部控件通过该 id 读取 fieldKey/mode。文本编辑自动切换“设为”，清除按钮显式切换“清除”。
4. 重跑测试并提交 `fix: reconnect metadata field editing workflow`。

## Task 7: 文件名添加与删除规则

**Files:**
- Modify: `qt/src/filename_transform_engine.hpp`
- Modify: `qt/src/filename_transform_engine.cpp`
- Modify: `qt/src/filename_processor.cpp`
- Modify: `app/qml/AgPlayer/components/audio_tools/FilenameProcessPage.qml`
- Modify: `tests/qt/filename_processing_test.cpp`
- Modify: `tests/qml/tst_filename_process.qml`

1. 增加字面预期测试：自动识别删除、精确前/后缀删除、开头/结尾序号删除、添加规则，以及选中项和整批真实重命名。
2. 运行 `filename_processing_test` 和 `qml_filename_process_test`，确认失败。
3. 扩展 typed rules 和变换引擎；删除只匹配明确规则，扩展名永不参与处理。
4. 页面拆分“添加 / 删除”，暴露所有规则并共用同一个预览/提交 payload。
5. 重跑测试并提交 `fix: support explicit filename affix removal`。

## Task 8: 综合回归与交付检查

1. 运行 Release 构建：`cmake --build build/release-integration-msvc --config Release --parallel 2`。
2. 运行全量：`ctest --test-dir build/release-integration-msvc -C Release --output-on-failure`。
3. 运行 `git diff --check`，确认工作树干净、无冲突、无未跟踪产物。
4. 做人工桌面烟测：任务栏图标/固定、真实鼠标拖动、真实麦克风录音。无法在当前会话完成的硬件检查明确列出，不声称通过。
5. 阅读并执行 `superpowers:verification-before-completion` 与 `superpowers:finishing-a-development-branch`，提交必要的最后清理；不打包。
