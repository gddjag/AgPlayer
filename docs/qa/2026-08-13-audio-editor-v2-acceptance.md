# 音频编辑 V2 验收记录

日期：2026-08-13

## 结论

- 生产代码、UI、核心音频处理、旧模块清理已落地。
- Release 构建成功；音频编辑相关测试全部通过。
- 仍不能宣称发布验收完成：本机无录音输入端点，无法执行真实麦克风验收；全量回归还有 1 个与音频编辑无关的旧主窗口 QML 套件失败。
- 未生成安装包或发布 EXE。

## 已验证功能

- 工具顺序：音频编辑、格式转换、元数据修改、文件名处理。
- 单轨单/双声道文档、帧精确选区、标记、撤销/重做。
- 打开、拖入、播放、Seek、循环、剪切、复制、粘贴、删除、裁剪、静音、插入静音、淡入、淡出、增益、归一化。
- WAV/FLAC/MP3/M4A/OGG 保存与导出，支持选区、取消、进度、重解码校验和原子提交。
- BPM、速度、保持音调、音高预览与后台应用。
- Windows WASAPI Shared 录音代码路径、设备枚举、监听、暂停/继续、停止、取消、Journal 恢复和未保存覆盖保护。
- 四语目录各 705 条，unfinished 为 0。

## UI 证据

- 参考图：`C:/Users/Administrator/Desktop/音视频播放器/AgPlayer音频播放器完整版/音频编辑.png`
- 最终实机渲染：`D:/ai/AgPlayer/.worktrees/revised-ui/build/qa-audio-editor/audio-editor-final-v5-1672x942.png`
- 尺寸：1672×942，中文、深色主题、真实 WAV、双声道波形、选区、播放头。
- 目视结果：无左栏；命令栏、摘要、中央波形、缩略导航、Transport、状态栏完整；右侧仅“录音”和“速度与音高”，无裁切。

## 自动化证据

- 全量 CTest：74 项，73 通过，1 失败。
- 音频编辑相关：`audio_document_test`、`document_writer_test`、`time_pitch_session_test`、`recording_session_test`、`editor_viewport_test`、`qml_audio_editor_test`、`audio_editor_controller_test`、`audio_editor_waveform_item_test`、`translation_catalog_test`、`audio_editor_feature_options_test`、`audio_tools_layout_contract_test` 均通过。
- 唯一失败：`qml_main_window_test`，56 通过、9 失败、1 跳过；失败集中在空库入口、播放列表拖拽/选择、主题色、播放器主波形定位，与音频编辑模块无调用交集。

## 旧模块删除

- 已移除 LightEditPage、MultiTrackWaveform、LightEditorController、light_editor、multitrack_editor、timeline_preview_mixer 及其旧 C API、QML 注册、CMake 项和测试。
- 生产代码残留扫描为 0；仅布局测试保留禁止旧名称回归的断言。

## 体积

- 现存 `build/package`：237 文件，111.58 MiB；基线为 237 文件，111.32 MiB。
- 该目录未按本轮最终代码重新封装，因此 0.26 MiB 仅作临时观测，不作为发布体积结论。

## 未完成的发布门

- 本机仅检测到 Realtek 扬声器，无录音输入端点；真实麦克风、电平、监听、录音、暂停/继续、恢复与回放未验收。
- 两小时文件内存峰值、4K FPS、处理倍率和导出性能尚无实测证据。
- 未执行最终同口径打包体积测量。
- `qml_main_window_test` 旧主窗口回归仍需独立修复到全绿。
