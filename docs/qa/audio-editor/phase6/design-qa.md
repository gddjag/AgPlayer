# Phase 6 音频编辑视觉与功能验收

## 参考与候选

- 唯一视觉基准：`C:\Users\Administrator\Desktop\音视频播放器\AgPlayer音频播放器完整版\音频编辑.png`
- 最终 Release 候选：`build/qa/phase6/final-gate-20260826/dynamic-tools-zh-theme0-tool0-1672x941.png`
- 响应式候选：同目录的 `dynamic-tools-zh-theme0-tool0-1280x720.png`、`dynamic-tools-zh-theme0-tool0-880x560.png`
- 视觉输入：同目录的 `dynamic-envelope-225s.wav`（225 秒、44.1 kHz、立体声，含动态包络、静音和瞬态）
- 最终真实音频证据：`build/qa/final-real-audio/` 内的 WAV 1672×941、FLAC 1280×720、MP3 880×560 截图和日志
- 同屏对照与差异蒙版：`build/qa/phase6/comparisons-review-final5/`

## 视觉与交互结论

- 1672×941 外壳、49 px 标题栏、43 px 文字 Tab、13 个命令、文件信息条、轨道头、标尺、284 px 波形区、时间线滚动条、录音/播放面板、快捷键卡、A–E 检查器和状态区均已按参考图收口。
- 录音控制固定为时间/状态、麦克风、暂停、录音、停止；播放控制固定为开头、后退、播放、前进、停止。鼠标提示显示真实快捷键，点击和键盘共用同一 Controller 动作。
- 播放改为共享 AudioEngine 的实时 EventTimeline 流，不再等待整曲临时 WAV。Space、Home、左右方向键和 Ctrl+Space 会立即发布播放状态与播放头。
- 录音使用实际采集声道的滚动峰值；33 ms 节拍同步录音时间、播放头、自动滚动和输入 Peak/RMS。无输入设备时控件正确禁用，不显示伪设备或伪电平。
- BPM 检测读取当前真实时间线并可取消；导出目录为空时先请求目录，有效目录执行异步导出并重新解码验证结果。
- 编辑时间线与主播放器复用同一蓝色填充波形风格。编辑、分割、裁剪、撤销期间保留上一代有效峰值；深度放大读取可见 PCM，空隙透明，异步结果按 generation 丢弃过期发布，单声道点数保持在 `2 × logical width` 预算内。
- 早期 `Night Drive - 128 BPM.wav` QA 输入的 2,048 个抽样桶绝对峰值均为 0.2512，正确渲染即为恒幅水平带，因此已从视觉判定中作废；动态真实 PCM 重拍后可见连续中心线、声道包络、静音区和瞬态，不存在时间线波形丢失。
- 中央音量线独立控制事件 Gain；双击添加 Envelope 点，点可拖动时间和音量，一次手势只生成一个 Undo 项。选区仅保留细虚线边框，左下胶囊固定为“拖出片段”。
- 1280×720 与 880×560 保持主时间线优先，检查器可滚动，播放与导出入口始终可达。
- 独立代码复审确认本次可执行范围内无 Blocking/P1；Ponytail 轻量化检查无阻断项。

## 自动验证

- Release 完整构建：通过。
- Release 全量 CTest：88/88 通过，日志 `build/release/final_gate_release_ctest_full_green.log`。
- Release `qml_audio_editor_test`：39/39 通过。
- Debug 音频编辑聚焦门禁：8/8 通过；最终直接相关 `waveform_item_test`、`qml_audio_editor_test`、`audio_editor_controller_test`：3/3 通过。
- Debug 全量仍有 3 个既有旧工具 QML 崩溃：格式转换、文件名处理、元数据修改；Release 对应测试通过，本次音频编辑增量未扩大该基线。
- 真实 WAV、FLAC、MP3 均完成导入和启动 smoke，日志未出现 error、failed 或 critical。
- `git diff --check`：通过（仅 Git 行尾转换提示）。

## 剩余硬件验收边界

- 当前机器未提供可验证的真实麦克风输入；录音波形、计时、电平、声道和保存路径已由注入式采集测试覆盖，真实麦克风的听感与硬件电平仍需在带输入设备的 Windows 机器复验。
