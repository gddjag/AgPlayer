# 2026-09-01 播放器与音频工具统一质量整改

## 范围与边界

- 本记录对应已确认的统一整改规格与实施计划。
- 沉浸视觉第 8 条由专属会话负责，本主线不修改沉浸窗口、地形反应堆或相关着色器。
- 滚动播放主题固定使用频彩波形并移除波形切换入口，已纳入后续滚动主题任务。

## 需求追踪

| 编号 | 需求 | 状态 | 验收证据 |
| --- | --- | --- | --- |
| 1 | 频彩波形频段命名、纯色底色 `#9098A6`、共享进度明暗差 | 进行中（设置与共享参数完成） | `settings_controller_test`、`waveform_item_test`、QML 设置测试；播放器主题矩阵留待滚动/双窗口任务 |
| 2 | 颜色选择器缩小 | 完成 | 紧凑 HSV 选择器最大 `360 x 430`；确认/取消自动化测试；工作区 Release 实机深色检查 |
| 10 | 歌曲列表缩略波形亮度，默认 66%，跨主题共享 | 完成（设置核心） | C++ 持久化/边界/重置测试；QML 20–100 滑条测试；工作区 Release 深浅主题实机检查 |

## Task 1 实施记录：共享波形设置与颜色选择器

完成内容：

- `SettingsController` 新增 `trackWaveformBrightness`，默认 `0.66`，范围 `0.20–1.00`，支持持久化、重置和无效值归一化。
- 歌曲列表设置将“频彩/纯色”与“明亮度 66%”放在同一行，避免设置卡片增高并符合“颜色选项后面加入”的规格。
- 频彩调色板显示八个频段名称，不再显示十六进制色号。
- 原生大颜色对话框替换为项目主题化紧凑 HSV 选择器；取消不写入，确定才提交。
- 纯色波形默认底色回归断言为 `#9098A6`。

验证结果：

- `settings_controller_test`：通过。
- `waveform_item_test`：通过。
- `all_qmllint`：通过；仅保留已有的两个未使用 import 信息提示。
- `MainWindowControls::test_settings_frequency_palette_uses_band_labels_and_compact_picker`：通过。
- `MainWindowControls::test_settings_list_waveform_brightness_is_live_and_defaults_to_66_percent`：通过。
- QML 颜色选择器测试覆盖紧凑尺寸、取消不提交、确认提交。
- 工作区 Release 实机路径：`D:\ai\AgPlayer\build\release\app\AgPlayer.exe`。
- Computer Use 实机检查：深色与浅色下亮度滑条、八频段标签与色块排版正常；紧凑颜色选择器无溢出。

已知基线：

- 完整 `qml_main_window_test` 仍有两个既有失败：真实播放点击寻址期望差异，以及隐藏播放指引时进度色可见性断言；归入后续双窗口/滚动波形任务，不在本 Task 1 冒充通过。
- 视觉验收最初误启动了已安装版本；已通过进程路径核对纠正，并仅以工作区 Release 进程作为本记录证据。
