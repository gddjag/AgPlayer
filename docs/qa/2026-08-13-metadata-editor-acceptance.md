# 元数据修改模块验收记录（2026-08-13）

## 结论

元数据修改页已按参考图重构，并接入可执行的批量修改链路。当前 Release
构建、核心格式矩阵、Qt 端到端、布局契约、QML 静态检查和应用截图启动均通过。

本次没有打包 EXE。

## UI 与交互

- 左侧：文件/文件夹/播放器列表导入、移除、清空、搜索、状态筛选、排序、全选、
  多选、列表导出、封面与文件状态展示。
- 右侧：作用范围、处理方式、三态编辑说明、修改预览、预检、结果导出、取消、应用。
- 10 个编辑目标：标题、艺术家、专辑、专辑艺术家、流派、年份、日期、作曲、BPM、封面。
- 9 个文本字段均为独立的“保留/设为/清空”；封面为“保留/替换/移除”。
- 文件发现、元数据读取、预检和写入在后台执行；支持中途取消和完成后媒体库刷新。
- 处理方式支持原文件元数据流复制，以及“格式转换 + 元数据”最终输出发布。

## 写入安全

- 元数据读取使用纯探测路径，不打开音频解码器。
- 原文件模式显式选择安全 muxer，将音频包、章节及非封面流复制到同目录暂存文件。
- 暂存文件完成 trailer/flush 后重新打开，逐字段和逐字节校验封面。
- 对音频流校验 codec、采样率、声道、格式、位深、time base、时长、包数量、包字节哈希和时间戳哈希。
- 校验成功后才执行原子替换；替换前创建 `.agbak` 备份。
- 替换后的最终复读若失败或不一致，会自动用 `.agbak` 恢复原文件。
- 预检覆盖：只读文件、占用文件、磁盘暂存空间、容器写入器、字段能力、封面能力和流保留能力。
- 9 个确定性故障点覆盖 header、包读写、trailer、重探测、校验、源文件变化、原子替换和替换后复读；失败时源文件保持不变且不残留暂存文件。

## 实测容器能力

| 容器 | 文本字段 | 封面 | 结果 |
|---|---|---|---|
| MP3 | 全部 9 项 | 支持 | 通过 |
| FLAC | 全部 9 项 | 支持 | 通过 |
| OGG | 全部 9 项 | 不支持 | 文本通过，封面预检拒绝 |
| Opus | 全部 9 项 | 不支持 | 文本通过，封面预检拒绝 |
| WMA | 全部 9 项 | 不支持 | 文本通过，封面预检拒绝 |
| M4A | 除 BPM“设为”外 | 支持 | 支持项通过；BPM 预检拒绝 |
| WAV | 标题、艺术家、专辑、流派、年份/日期 | 不支持 | 支持项通过；其余预检拒绝 |
| APE | 当前 FFmpeg 构建无安全可写 muxer | 不支持 | 预检拒绝 |
| 独立 AAC | 未注册安全元数据写入适配器 | 不支持 | 预检拒绝 |

说明：年份与日期映射到同一物理标签，单独编辑均可，同时编辑会在预检阶段拒绝。
当前 FFmpeg `ipod/m4a` muxer 会静默丢弃 BPM 标签，因此本模块明确拒绝 M4A
的 BPM“设为”，不会伪造成功状态。

## 验证证据

- Release 构建：`AgPlayer`、`metadata_writer_test`、`library_model_test`、
  `audio_tools_end_to_end_test` 目标构建成功。
- 关键测试：4/4 通过，耗时 7.85 秒。
  - `metadata_writer_test`
  - `library_model_test`
  - `audio_tools_end_to_end_test`
  - `metadata_editor_layout_contract_test`
- `qmllint MetadataEditPage.qml`：通过。
- `git diff --check`（元数据相关文件）：通过。
- 应用 QA 启动：退出码 0，运行日志 0 字节。
- 高 DPI：100%、125%、150%、200% 四档截图均成功生成并人工检查。
- `qml_light_editor_test`：Qt Quick Test runner 在本机 offscreen 模式无输出挂起；未计入通过项。
- 全仓 74 项测试没有达到全绿：首次运行中 17 项因测试目标尚未构建而未运行，
  另有翻译、主窗口、迷你播放器、轻编辑器布局等共享改动失败；随后尝试构建全部
  测试时，被 `audio_document_test` 与当前 `AudioDocument` API 不一致阻断。这些不属于
  元数据模块，但意味着当前共享分支尚不满足整体合并门槛。

## 验收产物

- 最终截图：`design-qa/metadata-editor-acceptance.png`
- 运行日志：`design-qa/metadata-editor-acceptance.log`
- 高 DPI 截图：`design-qa/metadata-editor-dpi-1.png`、
  `metadata-editor-dpi-125.png`、`metadata-editor-dpi-15.png`、
  `metadata-editor-dpi-2.png`
- 设计规格：`docs/superpowers/specs/2026-08-12-metadata-editor-reference-ui-design.md`
- 实施计划：`docs/superpowers/plans/2026-08-12-metadata-editor-reference-ui.md`

## 已知边界

- 转换模式在转码暂存输出完成后、最终文件发布前执行同格式元数据流复制与验证；
  功能上保持最终输出原子发布，但不是把标签直接注入主转码器的首次 header。
- `.agbak` 是成功覆盖后的可恢复备份策略；转换模式的中间备份会在最终发布前清理。
- 未进行真实声卡播放验收；本模块本身不应打开解码器或编码器，测试记录两者计数均为 0。
