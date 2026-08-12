# 文件名处理生产化实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 AgPlayer 中交付与参考图一致、真实可用、可回滚并能同步播放器资料库的文件名批处理模块。

**Architecture:** 将规则转换、名称校验、计划生成、事务执行和日志恢复拆分为 C++ 独立组件；`FilenameProcessor` 保持 QML 门面，只发布模型与命令。预览和执行使用同一份不可变 `RenamePlan`，所有磁盘操作经同目录暂存名完成，避免交换名称、循环名称与仅变更大小写时发生冲突。

**Tech Stack:** Qt 6 / QML、C++17、QtConcurrent、Qt Test、CMake、Windows 原生 Unicode 文件路径。

## Global Constraints

- 只重命名同一父目录内的真实本地音频文件；不得转码、改写音频、元数据、时间戳或移动目录。
- QML 不直接执行文件系统操作；规则、校验、哈希、计划、事务、回滚、撤销均在 C++ 中完成。
- 路径使用 `QString` 与 Qt Unicode API；不得转为 ANSI 路径。
- 采用测试先行：每项生产行为先观察到对应测试失败，再写最小实现。
- 不覆盖工作树中其他未提交模块的改动。

---

## 文件职责

- 新建 `qt/src/filename_transform_engine.hpp/.cpp`：纯名称转换，处理 stem/extension、大小写、空格、前后缀、自动序号。
- 新建 `qt/src/filename_validator.hpp/.cpp`：跨平台名称、路径、符号链接、长度、保留名与冲突键校验。
- 新建 `qt/src/rename_plan.hpp/.cpp`：稳定顺序的不可变计划、状态、冲突策略和预览数据。
- 新建 `qt/src/rename_transaction.hpp/.cpp`：哈希快照、同目录暂存、覆盖备份、提交、回滚与安全撤销。
- 新建 `qt/src/rename_journal_store.hpp/.cpp`：AppData `rename-transactions` 中的原子事务日志与未完成记录恢复。
- 修改 `qt/src/filename_processor.hpp/.cpp`：装配以上组件，发布 QML 状态，异步执行和资料库路径同步。
- 修改 `qt/src/library_model.hpp/.cpp`、`qt/src/playback_controller.hpp/.cpp`：批量路径映射与正在播放曲目释放/恢复接口。
- 修改 `app/qml/AgPlayer/components/tools/FilenameProcessPage.qml`：按参考图布局，消费后端计划状态。
- 修改 `qt/CMakeLists.txt`、`tests/CMakeLists.txt`：新增源与测试目标。
- 新建 `tests/qt/filename_processing_test.cpp`、`tests/qt/rename_transaction_test.cpp`：规则、计划、真实文件事务/撤销、资料库同步测试。
- 修改 `tests/qt/audio_tools_end_to_end_test.cpp` 与 `tests/scripts/audio_tools_layout_contract_test.ps1`：真实 QML 命令链与四尺寸布局契约。

## Task 1: 纯规则与校验层

**Files:**
- Create: `qt/src/filename_transform_engine.hpp`
- Create: `qt/src/filename_transform_engine.cpp`
- Create: `qt/src/filename_validator.hpp`
- Create: `qt/src/filename_validator.cpp`
- Create: `tests/qt/filename_processing_test.cpp`
- Modify: `qt/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces `agplayer::qt::FilenameRuleSet`, `FilenameTransformEngine::transform`, `FilenameValidator::validate`。
- Consumes only `QString`、`QFileInfo` 与规则值；不依赖 QML 或文件写操作。

- [ ] **Step 1: 写失败测试**

```cpp
void FilenameProcessingTest::transformsStemWithoutChangingExtension()
{
    const FilenameRuleSet rules{u"[Live]_"_s, u"_Remaster"_s, true, u"_"_s,
                                CaseRule::Keep, true, 1, 2,
                                NumberPosition::AfterPrefix, u"_"_s, true};
    QCOMPARE(FilenameTransformEngine::transform(u"Neon City.flac"_s, rules, 0),
             u"[Live]_01_Neon_City_Remaster.flac"_s);
}
```

- [ ] **Step 2: 运行并确认失败**

Run: `ctest --test-dir build/debug -R filename_processing_test --output-on-failure`

Expected: 失败，因为 `FilenameTransformEngine` 与测试目标尚不存在。

- [ ] **Step 3: 最小实现规则与校验**

```cpp
struct FilenameRuleSet {
    QString prefix, suffix, spaceReplacement, numberSeparator;
    bool replaceSpaces = false, autoNumber = false, preserveExtension = true;
    int numberStart = 1, numberDigits = 2;
    CaseRule caseRule = CaseRule::Keep;
    NumberPosition numberPosition = NumberPosition::AfterPrefix;
};
```

实现最后一个点分割扩展名、隐藏文件/无扩展名、多点文件名、连续普通空格替换、Unicode 感知大小写、四种序号位置及位数溢出不截断。校验器必须返回结构化 `RenameValidationIssue{severity, code, message}`，拒绝路径分隔符、控制字符、Windows 保留设备名、尾随空格/句点、符号链接和跨目录目标。

- [ ] **Step 4: 运行绿色测试**

Run: `ctest --test-dir build/debug -R filename_processing_test --output-on-failure`

Expected: 测试通过，涵盖截图示例、中文、Emoji、NFC/NFD、大小写、隐藏文件、无扩展名、非法名称与序号溢出。

## Task 2: 稳定 RenamePlan 与冲突策略

**Files:**
- Create: `qt/src/rename_plan.hpp`
- Create: `qt/src/rename_plan.cpp`
- Modify: `tests/qt/filename_processing_test.cpp`
- Modify: `qt/CMakeLists.txt`

**Interfaces:**
- Consumes `FilenameRuleSet`、校验器、按导入顺序的 `RenameSource`。
- Produces `RenamePlan` 与 `RenamePlanItem`，其 `action` 只能为 Rename、Skip、NoOp、Overwrite。

- [ ] **Step 1: 写失败测试**

```cpp
void FilenameProcessingTest::plansInternalCollisionsInImportOrder()
{
    const RenamePlan plan = RenamePlanner::build(sources, rules,
        ConflictPolicy::AutoNumber);
    QCOMPARE(plan.items.at(0).proposedFileName, u"Song.flac"_s);
    QCOMPARE(plan.items.at(1).proposedFileName, u"Song (1).flac"_s);
}
```

- [ ] **Step 2: 运行并确认失败**

Run: `ctest --test-dir build/debug -R filename_processing_test --output-on-failure`

Expected: 失败，因为 `RenamePlanner::build` 尚不存在。

- [ ] **Step 3: 实现不可变计划**

计划生成保留导入顺序；使用目标文件系统的 Unicode 规范化和大小写折叠键检测外部、批内和大小写等价冲突。实现 Skip、Overwrite、AutoNumber、StopBatch：覆盖策略标记需二次确认；StopBatch 在任何 Error/Conflict 时阻止执行；NoOp 必须显示“无需修改”，不可执行。计划记录源大小、修改时间、SHA-256、同目录暂存/备份名和规则快照。

- [ ] **Step 4: 运行绿色测试**

Run: `ctest --test-dir build/debug -R filename_processing_test --output-on-failure`

Expected: 测试通过，覆盖重复目标、外部目标、循环目标、大小写变更和四种策略。

## Task 3: 两阶段事务、日志与安全撤销

**Files:**
- Create: `qt/src/rename_transaction.hpp`
- Create: `qt/src/rename_transaction.cpp`
- Create: `qt/src/rename_journal_store.hpp`
- Create: `qt/src/rename_journal_store.cpp`
- Create: `tests/qt/rename_transaction_test.cpp`
- Modify: `qt/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes frozen `RenamePlan`。
- Produces `RenameTransactionResult` 与 `RenameUndoRecord`；日志位于 `QStandardPaths::AppDataLocation/rename-transactions/`。

- [ ] **Step 1: 写失败测试**

```cpp
void RenameTransactionTest::renamesSwapAndKeepsSha256()
{
    const auto result = executor.execute(planForSwap(tempDir));
    QVERIFY(result.committed);
    QCOMPARE(hash(fileAAfter), hash(fileBBefore));
    QCOMPARE(hash(fileBAfter), hash(fileABefore));
}
```

- [ ] **Step 2: 运行并确认失败**

Run: `ctest --test-dir build/debug -R rename_transaction_test --output-on-failure`

Expected: 失败，因为事务执行器尚不存在。

- [ ] **Step 3: 实现真实文件事务**

执行前重验文件身份、父目录、大小、时间、哈希和计划过期状态。所有 Rename 先移至同目录唯一 `.agplayer-rename-<tx>-<item>.tmp`，再提交最终名称；失败或取消在当前原子步骤结束后以同类暂存流程回滚。Overwrite 先同目录备份外部目标，提交后保留备份直至撤销窗口结束。每步以原子写日志，失败保留日志与人工恢复路径。撤销重新验证提交后身份/大小/哈希、目标占用和覆盖备份，然后同样经暂存名恢复。

- [ ] **Step 4: 运行绿色测试**

Run: `ctest --test-dir build/debug -R rename_transaction_test --output-on-failure`

Expected: 测试通过 A/B 交换、三项循环、仅大小写、覆盖备份/撤销、注入失败回滚、取消、外部替换拒绝撤销及真实 SHA-256/字节数不变。

## Task 4: QML 门面、资料库同步与异步状态

**Files:**
- Modify: `qt/src/filename_processor.hpp`
- Modify: `qt/src/filename_processor.cpp`
- Modify: `qt/src/library_model.hpp`
- Modify: `qt/src/library_model.cpp`
- Modify: `qt/src/playback_controller.hpp`
- Modify: `qt/src/playback_controller.cpp`
- Modify: `app/main.cpp`
- Modify: `qt/src/qml_registration.cpp`
- Modify: `tests/qt/audio_tools_end_to_end_test.cpp`

**Interfaces:**
- `FilenameProcessor` exposes `planRows`, `summary`, `validationIssues`, `busy`, `canUndo`, `applyPlan`, `cancel` and `undoLast` to QML。
- `LibraryModel::updateTrackPaths(const QHash<QString, QString>&)` applies a batch mapping while preserving track IDs and metadata。

- [ ] **Step 1: 写失败测试**

```cpp
void AudioToolsEndToEndTest::filenameRenameUpdatesLibraryWithoutChangingTrackId()
{
    processor.loadFiles({QUrl::fromLocalFile(trackPath)});
    processor.applyPlan(rules, {}, u"autoNumber"_s);
    QTRY_COMPARE(library.trackForId(trackId).value(u"path"_s).toString(), renamedPath);
    QCOMPARE(library.trackForId(trackId).value(u"trackId"_s).toString(), trackId);
}
```

- [ ] **Step 2: 运行并确认失败**

Run: `ctest --test-dir build/debug -R audio_tools_end_to_end_test --output-on-failure`

Expected: 失败，因为批量路径映射与事务完成回调尚未连接。

- [ ] **Step 3: 接入应用状态**

使用受控 `QtConcurrent` 运行预检、流式哈希、计划和事务；所有模型更新投递至 GUI 线程。执行前播放器协调层保存当前曲目/队列，释放自身文件句柄，提交完成后用原 trackId 重建队列并恢复播放位置；外部占用返回真实错误。事务成功后一次性应用 library 路径映射，缓存按旧路径失效/迁移；资料库同步失败必须记录可恢复状态，UI 不得显示整体成功。

- [ ] **Step 4: 运行绿色测试**

Run: `ctest --test-dir build/debug -R audio_tools_end_to_end_test --output-on-failure`

Expected: 测试通过，证明 trackId、收藏、评分、播放队列和当前曲目路径被正确保留/更新。

## Task 5: 参考图 QML 对齐与真实验收

**Files:**
- Modify: `app/qml/AgPlayer/components/tools/FilenameProcessPage.qml`
- Modify: `tests/scripts/audio_tools_layout_contract_test.ps1`
- Modify: `tests/qt/audio_tools_end_to_end_test.cpp`

**Interfaces:**
- QML 只绑定 `FilenameProcessor` 的模型和命令；不拼接文件名或推导冲突。

- [ ] **Step 1: 写失败布局/命令链测试**

```powershell
& $appPath --test-open-audio-tools filename --size 1672x942
Assert-UiControl -Name 'filenameFilePanel'
Assert-UiControl -Name 'filenameRulesPanel'
Assert-UiControl -Name 'filenamePreviewTable'
Assert-UiControl -Name 'filenameValidationPanel'
```

- [ ] **Step 2: 运行并确认失败**

Run: `ctest --test-dir build/debug -R "audio_tools_layout_contract_test|audio_tools_end_to_end_test" --output-on-failure`

Expected: 失败，因为参考图要求的预览/验证/摘要对象与状态绑定尚未完整存在。

- [ ] **Step 3: 重构页面至参考结构**

实现工具栏、带三态全选的左侧文件表、右上规则区、中部预览表、右侧验证统计、底部成功/冲突/撤销摘要与开始/取消动作。宽度 >=1380 保持三列，1000–1379 将验证区置于预览下方，<1000 启用整体垂直滚动与表格横向滚动；按钮包含 accessible name、tooltip、焦点、真实 enabled 条件。就绪/警告/错误/跳过由后端 severity 直接着色。

- [ ] **Step 4: 运行绿色验证**

Run: `ctest --test-dir build/debug -R "filename_processing_test|rename_transaction_test|audio_tools_end_to_end_test|audio_tools_layout_contract_test" --output-on-failure`

Expected: 所有新增与关联测试通过。

- [ ] **Step 5: 构建、四尺寸截图与真实音频验收**

Run: `cmake --build build/debug --config Debug --parallel`

Run: `ctest --test-dir build/debug --output-on-failure`

Run: 使用临时目录中的 FLAC、WAV、MP3、M4A、OPUS、AAC、OGG 文件执行真实重命名，记录前后路径、大小、SHA-256、ffprobe 探测和四个窗口尺寸（1672x942、1440x810、1024x720、800x600）截图。

Expected: 构建无新增警告；测试、真实文件完整性与 UI 截图均提供可复查证据。
