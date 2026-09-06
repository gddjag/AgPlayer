# 英文本地化闭环记录（2026-09-06）

本轮没有直接用 `lupdate` 覆盖发布目录，而是通过 Release 构建生成的
`AgPlayer_lupdate_project.json` 将当前源码语义提取到临时 TS，再按
`context + source` 合并回既有兼容目录。提取结果共有 1561 个当前 source；
合并前英文目录缺少 584 条，中文目录缺少 0 条。584 条英文译文已逐上下文
完成并检查占位符，且没有 `unfinished` 标记。

合并前的缺项分布如下：

| 上下文 | 条数 |
| --- | ---: |
| VocalSeparationPage | 168 |
| VocalSeparationController | 99 |
| MetadataEditPage | 68 |
| AudioEditorController | 24 |
| LyricsPanel | 24 |
| ExternalSeparationRuntime | 19 |
| FilenameProcessPage | 17 |
| FormatConverter | 14 |
| TransportControls | 12 |
| TagManagementPanel | 10 |
| SeparationProcessClient | 10 |
| CudaSeparationRuntime | 9 |
| IntegratedPlayerControls | 8 |
| FileAssociationController | 7 |
| FormatConvertPage | 7 |
| VideoTransportBar | 7 |
| PlayerControls | 6 |
| FormatSettingsPanel | 6 |
| ImmersiveSurface | 6 |
| ListWindow | 5 |
| SideNavigation | 5 |
| PlayerPane | 5 |
| LibrarySidePanel | 4 |
| ColorField | 4 |
| FormatTaskTable | 4 |
| ResourceFolderController | 4 |
| LibraryFileOperations | 3 |
| EqualizerWindow | 3 |
| Main | 3 |
| PlayerVolumeControl | 3 |
| TrackList | 3 |
| TrackSubtitle | 2 |
| LosslessTaskPanel | 2 |
| MetadataEditor | 2 |
| FormatPreflightDialog | 2 |
| TagModel | 1 |
| ThemedButton | 1 |
| IntegratedPlayerShell | 1 |
| ImmersiveWindow | 1 |
| ImmersiveQueueDrawer | 1 |
| VideoPlaybackView | 1 |
| FormatErrorDialog | 1 |
| SearchFilter | 1 |
| WaveSelectionOverlay | 1 |
| **合计** | **584** |

合并后的发布目录状态：

- `agplayer_en.ts`：1865 个唯一消息键，覆盖全部 1561 个当前 source，缺项 0；
- `agplayer_zh.ts`：1867 个唯一消息键，覆盖全部 1561 个当前 source，缺项 0；
- 英文 304 条、中文 306 条不再出现在当前源码中的兼容消息仍原样保留，没有
  因源覆盖检查被删除；
- `%1`、`%2`、`%n` 等 Qt 占位符按源文案保持一致。

回归测试 `phase6_translation_coverage_test.ps1` 现在会调用与
`AgPlayer_lupdate` 目标相同的工程描述，将实际源码提取到一次性临时目录，
再验证中英文发布目录的语义源覆盖、完成状态、英文中文回退和占位符。因此，
只更新既有目录而漏掉新 `tr()` / `qsTr()` 文案会直接失败。

验证证据：

- RED：`build/qa/translation-source-coverage-red.txt`，英文目录首先报告
  `[PlayerVolumeControl] 取消静音` 缺失；
- GREEN：`build/qa/translation-source-coverage-green.txt`，当前源码覆盖通过；
- 目录规则：`build/qa/translation-catalog-source-complete.txt`，发布目录完整性通过。

当前源码待译条目为 **0**。若其他并行改动在本次提取之后新增用户可见文案，
仍须重新执行语义源覆盖测试；本记录不把尚未重新提取的未来字符串算作已翻译。
