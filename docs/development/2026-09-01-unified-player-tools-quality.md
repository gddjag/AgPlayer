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
| 5 | 波形缩略图优先加载速度 | 完成（复用现有受限加载链） | 可见行优先、离屏取消、双工作线程、256 项缓存/队列边界；provider/item/stress 测试 |
| 10 | 歌曲列表缩略波形亮度，默认 66%，跨主题共享 | 完成 | C++ 持久化/边界/重置测试；共享 `TrackWaveformThumbnail` 实时亮度测试；工作区 Release 深浅主题实机检查 |

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

## Task 2 实施记录：缩略波形可见行优先与共享亮度

完成内容：

- 复核并保留现有轻量加载链：只为视口行创建缩略图、离屏立即取消、可见请求优先、最多两个读取工作线程、成功缓存与等待队列均限制为 256 项。
- 将 `trackWaveformBrightness` 直接绑定到共享 `TrackWaveformThumbnail` 的场景图透明度；双窗口、单窗口和滚动主题复用同一 `TrackList`，因此无需各主题重复实现。
- 明亮度变化只更新 GPU 合成透明度，不重新读取缓存、不重新量化峰值，也不重建波形几何。

验证结果：

- `track_waveform_thumbnail_provider_test`：通过。
- `track_waveform_thumbnail_item_test`：通过。
- `track_waveform_thumbnail_stress_test`：通过。
- `MainWindowControls::test_z_thumbnail_brightness_recolors_without_cache_read`：通过，3/3。
- 可见行边界、列表复用和模式切换不重复读取的既有 QML 回归继续通过。
