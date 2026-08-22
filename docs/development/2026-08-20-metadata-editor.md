# 元数据修改模块验收记录（2026-08-20）

## 验收基线

- UI 参考：`C:\Users\Administrator\Desktop\音视频播放器\AgPlayer音频播放器完整版\元数据修改.png`（1672 × 941）。
- 功能规格：`C:\Users\Administrator\Desktop\音视频播放器\AgPlayer音频播放器完整版\AgPlayer_元数据修改模块_开发提示词.md`。
- 实现截图：`build/metadata-final/metadata-ui-actual-v5.png`（1672 × 941，中文、深色主题、12 个真实音频夹具）。
- 同视口并排对照：`build/metadata-final/metadata-ui-comparison-v5.png`（左侧参考图，右侧实机图）。

## 需求到证据

| 要求 | 实现 | 验证证据 |
| --- | --- | --- |
| 参考图双栏工作台、文件表格、右侧编辑器、摘要和底部操作 | `MetadataEditPage.qml`、元数据页专用 `AudioToolsWindow.qml` / `ToolSidebar.qml` 外观 | 1672 × 941 实机截图与像素布局合同 |
| 添加文件、文件夹、播放列表，移除、清空、搜索、状态筛选、排序、全选和导出 | `MetadataEditPage.qml` | `qml_metadata_editor_test`、`metadata_editor_layout_contract_test.ps1` |
| 当前文件 / 已选文件 / 全部文件作用范围 | `metadataScopeBox` 与 `targetIndices()` | `tst_metadata_editor.qml::test_scope_current_selected_all` |
| 标题、艺术家、专辑、专辑艺术家、流派、年份、日期、作曲、BPM 九个文本字段 | 统一 `fieldDefinitions` 与 `MetadataEditPlan` | QML 合同断言字段键恰好为 9；端到端真实写入回读 |
| 每个文本字段保留 / 设为 / 清除三态；空设为无效 | 字段行单选组、QML 和核心双层校验 | `test_tri_state_is_mutually_exclusive`、`metadata_writer_test` |
| 年份和日期冲突可见且不得同时写同一物理标签 | QML 摘要警告、Qt 载荷校验、核心计划校验 | `metadata_writer_test` 日期回读与冲突校验 |
| 封面保留 / 替换 / 移除，预览和格式/大小校验 | `MetadataEditor` + `MetadataEditPage.qml` | `metadataEditorWritesTags` 真实 BMP 写入、回读和库封面缓存刷新 |
| 仅元数据模式同容器流复制，不启用解码器/编码器 | `write_metadata_plan` | `metadata_writer_test` 和端到端结果断言 `decoderOpenCount == 0`、`encoderOpenCount == 0`、音频流验证不变 |
| 预检、临时文件、备份、原子替换、失败回滚、空间预算 | `metadata_writer.cpp` | 注入式失败矩阵、既有 `.agbak` 保留、确定性双份空间预算测试 |
| 大标签 / 大封面和异常分配不得使 `noexcept` 边界终止进程 | `decoder.cpp` 有界探测和异常边界 | `decoder_test` 分配失败注入 |
| 处理完成后刷新库标题、艺术家和嵌入封面 | `LibraryModel::refreshMetadataForPaths` | `metadataEditorWritesTags` |
| 不支持项可取消或跳过，错误和逐文件结果可见并可导出 | 预检决策对话框、错误对话框、JSON/CSV 导出 | QML 结构合同、`metadataEditorPreflightIsAsyncAndRequiresDecision` |
| 转换时写入新文件并复用格式转换设置 | `FormatConverter.setMetadataEditPlanForFiles` 将一次性 Edit Plan 绑定本次目标路径 | QML 结构合同、旧队列隔离及后续普通转换不继承回归测试 |

## 已执行验证

- Release 构建：`AgPlayer`、`qml_audio_tools_test`、`decoder_test`、`metadata_writer_test`、`audio_tools_end_to_end_test` 目标构建成功（MSVC，`/W4 /WX`）。
- `qmllint`：`MetadataEditPage.qml`、`ToolSidebar.qml`、`AudioToolsWindow.qml` 通过。
- `metadata_editor_layout_contract_test.ps1`：通过。
- 定向 CTest：元数据核心、转换、C ABI、曲库刷新、QML 相邻页、翻译和布局共 14/14 通过。
- QML 相邻工具回归：`qml_audio_editor_test`、`qml_format_converter_test`、`qml_filename_process_test`、`qml_metadata_editor_test`，4/4 通过。
- 元数据与转换端到端定向运行覆盖九字段/封面回读、追加去重/聚合、内容型封面检测、异步预检、混合容器批量、转换输出写入计划、目标队列隔离及不支持项编码前拒绝。
- 上述端到端定向集合最终为 13/13 通过；全量 `audio_tools_end_to_end_test` 为 36 通过、5 失败，失败项全部位于既存文件名处理事务。
- 真实窗口：深色中文 1672 × 941，12 个音频文件，截图退出码 0；无 QML 装载、绑定或截图错误。
- 全量 CTest 为 66/75 通过；9 个失败包括上述聚合测试中的文件名处理失败，以及 `rename_transaction_test`、`window_controller_test`、`waveform_item_test`、`settings_controller_test`、`file_association_controller_test`、`equalizer_controller_test`、`qml_main_window_test` 和错误选择 WinGet MinGW 工具链的 `audio_editor_feature_options_test`。这些失败不在本次元数据改动路径内，本次未误报为全量通过。

## 实测支持矩阵

| 容器 | 九个标准文本字段 | Year / Date | 封面 | 结果 |
| --- | --- | --- | --- | --- |
| MP3 / ID3 | 支持 | 共用物理日期；必须提交等价 Set/Clear，否则预检冲突 | Set / Keep / Replace / Clear | 真实写入与精确字节回读通过 |
| FLAC / Vorbis Comment | 支持 | 独立 | Set / Keep / Replace / Clear | 真实写入与精确字节回读通过 |
| OGG Vorbis / Opus | 支持 | 独立 | 本构建不支持替换封面 | 文本通过；封面预检明确拒绝 |
| M4A / MP4 | 除 BPM Set 外支持 | 共用物理日期；必须提交等价 Set/Clear | Set / Keep / Replace / Clear | BPM 与日期冲突在编码前拒绝；其余回读通过 |
| WMA / ASF | 支持 | 独立 | 本构建不支持替换封面 | 文本通过；封面预检明确拒绝 |
| WAV / RIFF | 标题、艺术家、专辑、流派、年份/日期；其余字段明确不支持 | 共用物理日期 | 本构建不支持替换封面 | 仅支持字段写入通过；其余预检拒绝 |
| APE / APEv2 | 当前 FFmpeg 构建无安全可验证写入适配 | 不承诺 | 不承诺 | `AG_UNSUPPORTED_FORMAT`，源字节不变 |

## 安全与保真结论

- metadata-only 路径只做同容器 packet stream-copy；运行时指标验证 decoder/encoder 打开次数均为 0，并对音频 packet payload 做哈希比较。
- 阶段文件与替换后回读都验证九字段、封面字节、所有未管理全局/流字典键值、章节 ID/时间基/起止时间/完整 metadata；非音频且非封面流在预检拒绝。
- Cover Keep 不会把封面流的 `title=Album cover` / `comment=Cover (front)` 当成曲目字段清除。
- 写头、写包、写 trailer、阶段回读、替换后回读、源文件外部变化、取消、旧备份恢复和原文件恢复失败均有故障注入；失败时源文件不变，恢复失败时保留并报告 `.agbak.recovery-*` / `.preserved-*` 路径。
- 转换元数据在创建输出容器后、首次 `avformat_write_header()` 前注入；计划按目标路径一次性绑定，旧队列与后续普通转换不继承。

## 视觉结论

左右栏边界、52 像素文件行、右侧 212 像素封面槽、223 像素摘要和底部操作区与参考图落点一致。正式规格要求的搜索/状态筛选、作用范围、处理方式和三态控件属于相对参考图的必要增强。QA 夹具没有嵌入标签或封面，因此截图展示保留态和空封面；真实设置、清除、封面替换及文件元数据回读由端到端测试覆盖。

final result: passed
