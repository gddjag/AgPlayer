# 2026-09-04 AgPlayer 17 项修复与发布验收

## 范围

本轮最终收尾以 `main@f790eeb` 为基线，完成播放器波形、共享列表、滚动时间轴、外置歌词窗口、自定义分离模型诊断、元数据安全写入与沉浸视觉回归验收。旧分支已在基线中，不重复合并。

## 需求与实现证据

| 范围 | 实现/既有能力 | 验收证据 |
| --- | --- | --- |
| 首次安装与默认值 | 新配置保持双窗口主题、转码 MP3；缩略图亮度默认 50%；频彩未播放区明亮度 0–100%，默认 12%，升级用户值不覆盖 | `settings_controller_test`、`qml_main_window_test`、`qml_format_converter_test` |
| 双窗口/迷你波形 | 首尾峰值统一保留半个物理像素；未播放区改为降低波形自身亮度而非透明叠加，深浅背景使用同一设置且均保持可见；两处均恢复悬停时间胶囊 | `waveform_item_test`、`qml_waveform_test`、深浅主题频彩截图 |
| 公共播放器界面 | 信息块和歌词多行居中；外置歌词窗口支持上、下、左、右磁吸并跟随播放器；主题菜单、颜色选择器及右键菜单收紧 | `qml_player_controls_layout_test`、`qml_integrated_theme_test`、`qml_mini_player_test`、`qml_main_window_test` |
| 单窗口/滚动共享列表 | 尾部列固定宽度并以 8px 分隔；波形左右保持 14px 视觉间距；标题与波形弹性增长；筛选栏贴近底边且 BPM 内容居中 | `qml_main_window_test`、`qml_rolling_theme_test`、`library_list_layout_contract_test` |
| 滚动时间轴 | 总览进度、悬停和点击统一走 `WaveformItem` 时间/像素映射；中心时间去除胶囊底色；既有固定 8 拍视口和中心针逻辑保留 | `waveform_coordinate_mapper_test`、`qml_rolling_theme_test` |
| 音频编辑 | 拖出片段使用“原文件名_片段_起点-终点”；既有缩放条、左/右裁剪和 Theme Token 路径回归通过 | `selection_drag_controller_test`、`audio_editor_controller_test`、`qml_audio_editor_test` |
| 人声伴奏分离 | 页面打开即探测设备并复核模型；任意层级递归发现 ONNX/PTH/TH；本地诊断项不再显示成下载失败，支持安全删除；完整说明通过悬停提示和可滚动备用地址窗口呈现 | `vocal_separation_controller_test`、`separation_*` 测试组、`qml_vocal_separation_test` |
| 元数据 | 强制正规化仍保留音频包载荷、编解码参数和并发修改验证；仅允许源或暂存侧缺失的采样格式/位深元数据视作正规化差异，真实载荷变化仍回滚并保留 `.agbak` | `metadata_writer_test`、`qml_metadata_editor_test`、`audio_tools_end_to_end_test` |
| 沉浸视觉 | 沿用现有 QRhi 场景图渲染器、隐藏/最小化停止渲染、资源生命周期与自适应质量；空间歌词两行层级与普通歌词三行显示分别回归 | `terrain_reactor_gpu_smoke_test`、`qml_immersive_integration_test` |

## 自动化验证

- MSVC Release 编译通过。
- `all_qmllint` 通过。
- Release CTest 在最终修改后串行复跑：163/163 通过。
- Windows 生命周期、任务栏、单实例、运行时部署、安装器与版本契约均通过。
- UI 截图矩阵：中文深色/浅色下的播放器、迷你、设置和共享列表共 8 张，全部通过尺寸、非空与无运行时警告检查；另对频彩模式输出深浅主题截图，确认未播放区在两种背景下均可辨识。
- 主窗口真实启动/播放与 8 种格式导入、持久化、恢复冒烟测试通过，运行日志无 WARN/ERROR/FATAL。

## 硬件与外置模型边界

验收主机检测到 NVIDIA GeForce RTX 4070 Ti SUPER，用户模型目录中的 `.onnx`、`.pth`、`.th` 示例文件存在。未携带受信 sidecar/外置 Python 运行时的裸模型按设计显示诊断且禁止执行；因此未把这些文件记录为“已成功分离”。GPU Provider 和真实长音频吞吐仍取决于用户安装的 ONNX Runtime Provider、模型契约和驱动组合。
