# 四模块音频工具与十段 EQ：Windows 验收记录

最后更新：2026-08-10

此文档只记录已经可复现的证据；“编译通过”或“控件存在”不作为交付证据。

## 已通过的自动化证据

| 需求 | 证据 |
| --- | --- |
| 四模块固定为轻度剪辑、格式转换、元数据修改、文件名处理 | `audio_tools_end_to_end_test`：`audioToolsControllerUsesFourStableTools` |
| 文件/文件夹递归发现、中文路径、异步入队 | `audio_tools_end_to_end_test`：`toolsExpandDroppedFoldersRecursively`、`audioFileDiscoveryExpandsFoldersOffTheGuiThread` |
| Windows `WM_DROPFILES` 接收与目标分派 | `native_drop_router_test`：3 项通过，包含真实 Windows 消息构造与路径规范化 |
| 原生转码、格式/码率/采样率校验、取消、冲突命名、重新打开输出 | `audio_tools_end_to_end_test`：转换相关 13 项 |
| 元数据写入与文件名两阶段安全事务 | `audio_tools_end_to_end_test`：`metadataEditorWritesTags`、文件名相关 4 项 |
| 多 Clip、16 轨、剪切、吸附、撤销重做、BPM、混音预览与导出 | `light_editor_controller_test`、`qml_light_editor_test`、`timeline_preview_mixer_test` |
| 工具标题栏拖动与工具目标文件投递 | `qml_light_editor_test`：`test_toolsWindowTitleBarMovesFromRealPointerDrag`、`test_nativeDropEventsReachEveryAudioTool` |
| 十段 EQ 真正位于 PCM 输出链，含 RBJ 双二阶、旁路平滑、预设与声道独立 | `graphic_equalizer_test`、`equalizer_controller_test` |
| EQ 紧凑窗口 | `EqualizerWindow.qml`：780×460；`qml_main_window_test` |
| 音频处理未通过外部 ffmpeg 进程 | 源码扫描：`core/`、`qt/src/` 与 `app/` 无音频 `QProcess`；唯一 `QProcess::startDetached` 仅用于系统资源管理器显示文件夹 |

## 仍必须在真实 Windows 桌面验收

- [ ] 用资源管理器实际拖入 MP3、WAV、FLAC、中文命名目录到播放器、列表与四个工具。
- [ ] 每个工具完成导入、试听、参数变化、导出、重新导入回放。
- [ ] EQ 在耳机/声卡上验证：0 dB 近似透明、任一频段 ±6 dB、连续拖动、旁路与预设切换无杂音。
- [ ] 轻度剪辑验证鼠标框选、滚轮缩放、中键平移、边缘裁剪、快捷键与非破坏性项目保存。
- [ ] 对真实播放页验证波形进度、Seek、自动换歌、窗口缩放和窗口组层次。

## 最近命令结果

```text
graphic_equalizer_test            PASS
equalizer_controller_test         PASS
audio_tools_end_to_end_test       PASS
qml_light_editor_test             PASS
qml_main_window_test              57 PASS / 0 FAIL / 1 SKIP
native_drop_router_test           3 PASS / 0 FAIL
```

`qml_main_window_test` 的唯一跳过项是 `WM_DROPFILES`：离屏 Qt 平台没有 Windows 原生窗口句柄，不能把该跳过项视为资源管理器拖放已验收。

## 已完成的真实窗口抽查

- 2026-08-10：最新 Release 的主窗口以 `1228×380` 启动；白色播放游标存在。
- 同一窗口中，在整段 04:52 波形约 75% 位置点击后，播放器时间立即为 03:40，和比例时间轴一致；该结果只证明本次样本的 Seek 映射，仍需覆盖 VBR、短音频、长音频和自动切歌。
- 点击主播放器“音频工具”入口后，真实的“AgPlayer · 音频工具”窗口打开，横向四模块与 16 轨轻度剪辑页可见。
- 向真实轻度剪辑窗口投递 Windows `WM_DROPFILES`（中文文件名 WAV）后，文件进入 Track 1，生成 Clip 和文件信息；验证使用与资源管理器相同的原生投递路径。此前同步 `SendMessage` 不会经过 Qt 的消息循环，不能作为拖放验证方式。
- 对格式转换、元数据修改、文件名处理窗口逐项投递同一原生消息：三页都出现该中文 WAV 的真实任务/文件行。格式转换显示编码参数与桌面输出目录；元数据页显示批量字段策略；文件名页生成重命名预览。
