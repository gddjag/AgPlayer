# Task 2 完成报告：音频编辑、播放、录音与导出

实现提交：`9f1ed1c`、`a9b24ca`。以 `b49b81c` 为差异基线检查了 `409b1f2`，没有 cherry-pick；只补入当前集成分支仍缺失的行为。

## 实现与复核

- 保留现有真实文档清除/未保存确认、单条居中波形、编辑后视口重取、选区默认循环、独立播放头、24 px 选区边缘、原生拖出 WAV、增益/自动化/淡入淡出、WASAPI 录音和宽中窄响应式实现。
- 首次播放准备和变速/变调预览现在发布真实渲染进度；取消使用任务代次屏蔽过期回调，并向界面返回明确的取消或原生错误。操作条提供进度和取消入口。
- BPM 检测优先渲染当前选区，否则渲染完整时间线；检测具备进度、取消、代次和详细渲染错误，取消后不会发布过期 BPM。
- 导出成功后发布实际输出路径；继续消费 Task 3 的共享导出设置、Desktop 回退和有效有损码率。错误仍由底层写入结果原样进入界面。
- 播放、录音和工具栏按钮展示真实快捷键提示。`Space`、Home、Left、End、Ctrl+Space、Ctrl+R、R、Shift+R 均有对应窗口快捷键，并在文本输入或模态对话框中让出。
- 时间线只保留右上选区范围和左下“拖出片段”胶囊；移除重复时长胶囊。播放头时间胶囊改为半透明绿色玻璃样式。
- 编辑器播放/录音继续通过 `exclusivePreviewStarting` 在应用层先停止主播放器；录音仍以 33 ms 周期发布红色播放头、居中实时波形和绿色分段电平。

## TDD 与验证

先加入失败测试，再实现：BPM 选区优先/进度/取消防过期结果、首次播放进度与取消错误、导出实际路径、播放/录音快捷键和操作反馈、时间线胶囊位置与去重。

全新 Release 构建目录 `build/task2-clean` 的结果：

```text
cmake --build build\task2-clean --target \
  recording_session_test audio_editor_controller_test \
  audio_editor_waveform_item_test qml_audio_editor_test AgPlayer --parallel 4

ctest --test-dir build\task2-clean --output-on-failure \
  -R "^(recording_session_test|qml_audio_editor_test|audio_editor_controller_test|audio_editor_waveform_item_test|audio_tools_layout_contract_test)$"

5/5 passed
Release AgPlayer target built successfully
qmllint AudioEditorPage.qml: passed
git diff --check: passed
```

覆盖证据包括真实清除、全局 Space 让出文本/模态、播放/录音互斥信号、选区循环、波形像素预算、编辑/撤销刷新、原生临时 WAV 拖出、增益/淡化单步撤销、录音实时 UI、共享导出设置和 1672/1280/最小窗口布局。

## 未执行的人工/硬件验收

- 未使用真实默认 WASAPI 麦克风验证系统权限、设备拔插、实际 33 ms 采集抖动、绿色电平与录音回放音质。
- 未通过物理扬声器/声卡试听主播放器互斥、选区无缝循环、变速/变调/保持音调和导出文件的听感。
- 未在真实桌面鼠标拖到资源管理器完成原生文件拖放，也未人工截图验收 1672×942、1280×720 和最小窗口；本次仅有自动化 QML 几何与交互证据。
