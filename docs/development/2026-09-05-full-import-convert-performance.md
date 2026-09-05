# 导入、库索引、格式转换与元数据性能实测

日期：2026-09-05。范围：隔离工作树 `D:/ai/AgPlayer/.worktrees/full-ui-performance-20260905`，Windows 11 / Qt 6.7.0 / MSVC Release。所有构建写入本工作树 `build/release`；第一阶段曾只读复用主目录既有 vcpkg，第二阶段已恢复为本工作树独立 vcpkg 安装树，并由主代理 clean-first 全量构建通过。未提交、未打包。

## 实测结论

先在未修改本范围生产代码的版本新增相同测试并构建，取得基线，再修改并用同一输入生成器、文件数量、编码参数重测。测试顺序执行，没有同时启动其他构建。每个表格数值是一轮观测值，不是多轮中位数；本地文件已由测试写入，属于暖文件缓存场景。大幅卡顿改善来自可定位的冗余工作移除，几毫秒的转换差异不作速度提升结论。

| 操作与输入 | 修改前 | 修改后 | 验证与含义 |
|---|---:|---:|---|
| 导入 2000 个真实 WAV，16 kHz/16 bit/mono/1 s | 3524 ms | 2835 ms | 每条时长、采样率、导入 ID 和总数均正确；约 19.6% 耗时下降 |
| 上述导入，5 ms 主线程心跳最大间隙 | 311 ms | 17 ms | 导入调用本身两次均 0 ms 返回；采样不是渲染帧时间 |
| 转换任务列表 2000 条，删除交替勾选的 1000 条 | 481.419 ms | 14.541 ms | 约 33.1 倍；保留条数与原先相对顺序逐条验证 |
| 转换页加载同一组 2000 WAV | 8677 ms | 8372 ms | 探测路径未改；不声称这 3.5% 差异由本次优化造成 |
| 400 个真实 FLAC 批改 title | 28280 ms | 5370 ms | 约 5.27 倍；所有文件重新打开读取标签，确认无 `.agbak` 残留 |
| 上述元数据批改，5 ms 主线程心跳最大间隙 | 22875 ms | 62 ms | 完成回写的二次复杂度及重复文件系统查询消除；该用例未挂接 LibraryModel |
| 8 个 30 s WAV → FLAC，16 kHz/mono，2 并行，不归一化 | 118 ms | 119 ms | 编码路径保留；8 个输出共 363488 字节，两次一致，逐个验证 FLAC、采样率、30000 ms 时长 |

元数据优化第一轮仍做每路径一次 canonical 查询时，实测 5938 ms / 最大心跳间隙 209 ms。复核加载及后台快照的不变条件后，将完成回写改成已规范化路径的内存匹配，最终为表中 5370 ms / 62 ms。

## 修改及调用链

使用已刷新的 `AgPlayer-main-audit-20260905` codebase-memory-mcp 索引定位 `loadFiles`、`removeChecked`、`startApply`、`rescan`、`refreshMetadataForPaths` 及关系；具体代码和最终 Diff 均读取隔离工作树。

1. `ImportController::importUrls → handleBatch → LibraryModel::insertBatch`：发现生产导入已经是 4 个探测线程、每批最多 64 条、按发现顺序交付。保留线程数及顺序，`handleBatch` 复用 `insertBatch` 返回的 ID，正常新增批次不再重复规范化/查询每条路径；有重复项时仍从模型查询完整成功 ID，保留跳过计数与导入选择语义。
2. `LibraryModel::insertBatch`：保留路径规范化和去重、批量插入通知。插入前缀未移动，不再清空重建所有索引，只更新插入位置至尾部。空库连续追加从反复扫描整个库变成仅处理新行。中间插入仍更新所有被移动行。
3. `FormatConverter::removeChecked`：原先逐个 taskId 扫全部行，即 N² 次 ID 检查；改为每行读取一次勾选角色。原先反复 `removeAt` 移动尾部，改为稳定的 `removeIf` 一次压缩，保留剩余顺序。
4. `MetadataEditor::startApply` 完成回调：原先成功条目逐个扫描现有条目，内层双方不断 `canonicalFilePath`，400 文件可触发约 16 万次文件系统查询，导致 22 秒 GUI 线程停顿。加载阶段已经规范化路径，worker 使用该快照并把相同路径写回结果，因此完成回调只需平台大小写规则下的路径哈希索引。仍只回写成功项，失败与取消状态保留；不改变写入/预检流程。

第一阶段未修改头文件。第二阶段新增库刷新结果结构与API声明，涉及 `library_model.hpp`、`metadata_editor.hpp`；没有新增模型成员或线程。未修改 QML、Theme、CMake、播放核心、波形、分离 worker/controller。

## 检查覆盖及保留决定

| 子系统 | 已检查的工作/并发/完整性边界 | 本次决定与证据 |
|---|---|---|
| 文件发现 | `audio_file_discovery` 的后台递归扩展、扩展名集合、规范化路径去重 | 保留；现有导入/音频工具测试覆盖目录、中文路径、别名、重复、错误继续 |
| 导入管线 | 四线程有界探测、64 条通知、保持顺序、销毁/取消生命周期、10K 轻量记录 | 保留并发语义；2000 真文件实测及 `import_controller_test` 全目标通过 |
| 库批插入 | 索引重建、前缀与移动尾部、已有条目去重 | 已优化；新增 `insertBatchPreservesPrefixAndUpdatesShiftedIndexes` 同时验证中间插入、追加、重复、ID/路径双索引 |
| 库管理扫描/过滤（历史基线） | 当时 `library_manager_controller.cpp` 的 QtConcurrent 扫描、取消标记、相同大小分组后才哈希、mtime/大小缓存、可见行内存过滤 | 历史 `library_manager_controller_test` 通过，包括后台取消与 10K 过滤预算；后续用户确认该专用页面已取消，主线删除页面及专用控制器，不作为最终产品保留项。基础 `LibraryModel` 及挂库刷新优化独立保留 |
| 转换加载/预检 | 后台加载、输入指纹、保存的流探测快照、冻结参数、输出路径预留 | 保留；加载仍有 metadata 与 transcode 双探测，二者字段/流选择语义不同，未为速度直接合并 |
| 编码与输出 | 用户指定 1–10 并行的局部池、取消 token、输出事务、重新验证 | 保留；同参数真实 FLAC 批转换证明此范围未退化；全音频工具测试另覆盖格式矩阵、质量、CBR/VBR、取消保留现有输出、路径冲突等 |
| 元数据处理 | 异步预检/写入、源身份冻结、成功/失败统计、替换/备份/读回验证 | 仅优化完成回写，不改 core metadata_writer 的事务/保真逻辑；400 文件逐一读回成功，metadata_writer 全目标通过 |

剩余边界：

- 第一阶段 400 文件的 62 ms 数据针对独立元数据控制器；第二阶段已补真实 LibraryModel 挂接验收，最终最大心跳间隙为 17 ms。两者均为控制器/模型集成测量，不包含真实窗口渲染帧率或听音。
- 导入探测线程数有界，但为维持顺序，若第一个文件异常缓慢，后续结果可在等待队列积累；本次未改变这个独立的内存背压/取消设计。
- 未测冷缓存、网络盘、极慢/挂起文件系统、长时大批媒体或 CPU/GPU 能耗。单次计时不可外推所有编码格式或硬件。
- 本记录不包含播放、分离、波形、QML 渲染和实际听音；由并行负责范围另行验收。

## 实际验证

历史阶段构建目标：`import_controller_test library_model_test library_manager_controller_test audio_tools_end_to_end_test metadata_writer_test`，当时 Release 全部通过。后续主线删除已取消的专用 LibraryManager 页面/控制器/测试，此处 5/5 记录只表示该阶段历史验证，不是最终测试清单；基础 LibraryModel 挂库刷新仍是有效产品功能。

```powershell
ctest --test-dir build/release --output-on-failure `
  -R '^(import_controller_test|library_model_test|library_manager_controller_test|audio_tools_end_to_end_test|metadata_writer_test)$' `
  -j 1 --output-log build/release/import-convert-regression.txt
```

实际结果：5/5 CTest 通过，35.09 s；分别为 3.77 s、0.39 s、2.54 s、14.88 s、13.48 s（按上述日志运行顺序 metadata_writer、library_model、library_manager、import、audio_tools）。新增性能用例默认跳过，避免常规测试夹带大批造文件成本；显式启用后已实际执行并通过：

```powershell
$env:PATH='D:\Qt\6.7.0\msvc2019_64\bin;D:\ai\AgPlayer\build\release\vcpkg_installed\x64-windows\bin;'+$env:PATH
$env:AGPLAYER_RUN_PERFORMANCE='1'
& build/release/tests/import_controller_test.exe performanceRealImportResponsiveness
& build/release/tests/audio_tools_end_to_end_test.exe performanceConverterCheckedRemoval performanceConversionAndMetadataBatch
```

原始日志在隔离工作树的 `build/release/import-perf-before.txt`、`import-perf-after.txt`、`convert-metadata-perf-before.txt`、`convert-metadata-perf-after.txt`、`convert-metadata-perf-final.txt`、`import-convert-regression.txt`。输入文件位于每次测试自己的临时目录，验证后由 QTemporaryDir 清理。

最终范围内 `git diff --check` 通过；检查了路径去重、插入 ID 顺序、失败/取消回写、Qt 模型通知和索引一致性。此次属于局部功能与性能验收，不代表整个应用已经发布就绪。

## 第二阶段：实际挂接 LibraryModel（已完成验收）

新增 `AGPLAYER_PERF_LIBRARY=1` 模式，以相同的 400 个 FLAC、相同标题修改调用绑定真实 LibraryModel。基线已执行并通过：18375 ms、主线程 5 ms 心跳最大间隙 356 ms；400 条文件与库标题、路径、采样率、时长相符，收藏/评分保持。原始日志 `build/release/metadata-library-perf-before.txt`。这轮观测与第一阶段不应跨轮相减；期间环境随后发生共享依赖文件消失，需要恢复隔离依赖，吞吐结论应结合新构建后同输入观测谨慎解释。

实际实现：

- 抽出原同步刷新使用的 `readLibraryMetadata`，沿原 metadata worker 已经打开的读回句柄生成完整库元数据快照，包括文件属性、BPM、封面缓存。没有再为库刷新重复打开媒体文件，没有删除原刷新字段。
- `LibraryModel::beginMetadataRefresh / completeMetadataRefreshes` 复用现有 generation 与 in-flight 状态。批量更新只接受匹配 track ID、generation 与规范化存储路径的结果；只更新原元数据字段，保留用户收藏、评分、播放历史、标签等。一次批次发送一次 `dataChanged / flushRequested`。
- 编辑器捕获开始时的库 QPointer 和路径快照。换库或原库被销毁时不向错误对象回写；watcher 销毁时按准确 generation 清理未交付 claim，不能清掉后来请求。没有新增后台线程或依赖。
- 新增 `metadataRefreshRejectsStaleClaimsAndPreservesUserState` 验证旧请求、错误路径、重复结果、迁移和删除重建；新增 `metadataEditorReadbackCannotOverwriteRelocatedLibraryTrack` 用受控暂停的既有线程池验证实际写文件期间库路径迁移后不会被旧读回覆盖。
- 挂库性能用例要求 400 条库更新恰好一次 dataChanged，并逐条核实标签、路径与技术属性以及收藏/评分。
- 实际挂库首轮回写优化后仍测到 147 ms 主线程间隙，定位到预检调用在 GUI 线程对所有文件采集 canonical/size/mtime。将此只读指纹采集移到已有 preflight worker，并通过原 summary.entries 回传冻结快照。预检完成后文件改变仍在写入前拒绝；忙碌期间使用捕获的条目快照，不被后续 UI 输入替换。

最终验证：隔离依赖恢复后，主代理 clean-first 全构建通过；最后预检指纹调整使用 `cmake --build --preset windows-msvc-release --target library_model_test audio_tools_end_to_end_test` 增量编译通过。随后独占测试窗口顺序执行挂库性能和两个完整 CTest targets。

| 400 FLAC 同输入挂库流程 | 原同步库刷新基线 | 最终结果 |
|---|---:|---:|
| 文件写入、读回、库字段更新总耗时 | 18375 ms | 9165 ms |
| 5 ms 主线程心跳最大间隙 | 356 ms | 17 ms |
| 文件/模型字段核验 | 400/400 通过 | 400/400 通过 |
| 最终库 dataChanged 次数 | 原代码每成功文件发送一次 | 实测恰好 1 次 |

最终 `library_model_test` 和 `audio_tools_end_to_end_test` 两完整 CTest targets 2/2 通过，17.74 s（0.75 s / 16.65 s）。测试包括旧 generation、错误路径、重放结果、删除重建、实际写期间库路径迁移、预检后源变化拒绝、用户字段保留、封面及元数据完整读回。原始日志：`metadata-library-perf-final.txt`、`metadata-library-regression-final.txt`。

上述总耗时属于单轮观测。恢复独立依赖并全量重建之后，本机未修改的 FLAC 编码用例也从早期 118 ms 波动到最终轮 669 ms；8 个输出仍共 363488 字节，格式/采样率/时长全部核验通过。因此本记录不将跨轮总耗时变化或编码耗时视作稳定吞吐倍率；可确认的结构改善是 GUI 不再重复读媒体、回写不再二次扫描或逐文件通知、预检指纹采集不再占用 GUI 线程，实际挂库最大心跳间隙降低到 17 ms。

复现第二阶段时，运行 PATH 应使用隔离目录 `build/release/vcpkg_installed/x64-windows/bin`，并同时设置 `AGPLAYER_RUN_PERFORMANCE=1` 和 `AGPLAYER_PERF_LIBRARY=1` 后运行 `performanceConversionAndMetadataBatch`。第一阶段 PATH 示例仅保留为历史基线记录，不应继续依赖已漂移的主目录依赖树。
