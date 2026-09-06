# 英文翻译待办（2026-09-06）

当前发布目录以既有完整目录为兼容基底，仅合并已经人工审校且没有
`unfinished` 标记的新译文。运行 `AgPlayer_lupdate` 后发现的其余 584 条
英文新增源文案尚未完成审校，因此没有写入发布用 `agplayer_en.ts`，也不能
视为已经翻译完成。

后续翻译时应重新运行 `AgPlayer_lupdate` 提取最新 source，逐上下文完成英文
翻译并检查 `%1`、`%2`、`%n` 等占位符，再合并回兼容目录。待办分布如下：

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

本轮已完成并纳入英文目录的新增范围为 `RollingPlayerShell`、
`ImmersiveControlPanel`、`SettingsPage` 和 `UpdateChecker`；中文目录已覆盖本次
提取的全部 1557 条当前 source，同时保留旧兼容条目。
