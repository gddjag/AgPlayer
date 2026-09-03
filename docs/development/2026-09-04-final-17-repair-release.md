# 2026-09-04 AgPlayer 17 项修复与发布验收

## 范围

本轮以 `main@482cf6a` 为基线，完成播放器波形、设置默认值、共享列表、滚动时间轴、音频编辑、自定义分离模型诊断、元数据安全写入与沉浸视觉回归验收。旧分支已在基线中，不重复合并。

## 需求与实现证据

| 范围 | 实现/既有能力 | 验收证据 |
| --- | --- | --- |
| 首次安装与默认值 | 新配置保持双窗口主题、转码 MP3；缩略图亮度默认 50%；频彩未播放区明亮度 0–100%，默认 20%，升级用户值不覆盖 | `settings_controller_test`、`qml_main_window_test`、`qml_format_converter_test` |
| 双窗口/迷你波形 | 首尾峰值统一保留半个物理像素，频彩未播放区直接绑定公共设置，移除主题透明度下限 | `waveform_item_test`、`qml_waveform_test`、深浅主题及 100/125/150% DPI 截图矩阵 |
| 公共播放器界面 | 歌词/主题/沉浸/迷你图标 22px、点击区至少 32px；信息块和歌词居中；主题菜单及右键菜单收紧 | `qml_player_controls_layout_test`、`qml_integrated_theme_test`、`qml_mini_player_test` |
| 单窗口/滚动共享列表 | 尾部列固定宽度并以 12px 分隔；标题与波形弹性增长；筛选栏和 BPM 内容沿用共享布局 | `qml_main_window_test`、`qml_rolling_theme_test`、`library_list_layout_contract_test` |
| 滚动时间轴 | 总览进度、悬停和点击统一走 `WaveformItem` 时间/像素映射；中心时间去除胶囊底色；既有固定 8 拍视口和中心针逻辑保留 | `waveform_coordinate_mapper_test`、`qml_rolling_theme_test` |
| 音频编辑 | 拖出片段使用“原文件名_片段_起点-终点”；既有缩放条、左/右裁剪和 Theme Token 路径回归通过 | `selection_drag_controller_test`、`audio_editor_controller_test`、`qml_audio_editor_test` |
| 人声伴奏分离 | 任意层级递归发现 ONNX/PTH/TH；模型项补齐路径、声部数、后端、可用性和失败原因；无受信 sidecar 或外置运行时的文件只显示诊断，不伪装可执行 | `vocal_separation_controller_test`、`separation_*` 测试组、深浅主题分离页截图 |
| 元数据 | 沿用强制正规化、分阶段回读验证、音频等价检查、失败回滚和 `.agbak`；本轮整套容器写入回归无失败 | `metadata_writer_test`、`qml_metadata_editor_test`、`audio_tools_end_to_end_test` |
| 沉浸视觉 | 沿用隐藏/最小化停止渲染、资源生命周期与自适应质量；本轮 50 次 GPU 窗口开关压力用例通过 | `terrain_reactor_gpu_smoke_test`、`qml_immersive_integration_test` |

## 自动化验证

- MSVC Release 编译通过。
- `all_qmllint` 通过。
- Release CTest 在最终修改后串行复跑：163/163 通过，总耗时 316.24 秒。
- Windows 生命周期、任务栏、单实例、运行时部署、安装器与版本契约均通过。
- UI 截图矩阵：中文、深色/浅色、100%/125%/150% DPI，播放器/迷你/设置/共享列表共 24 张，全部通过尺寸、透明圆角和非空检查。
- 音频编辑、格式转换、元数据和分离页深浅主题截图共 8 张，通过运行时无警告检查，并与用户参考图并排复核布局。

## 硬件与外置模型边界

验收主机检测到 NVIDIA GeForce RTX 4070 Ti SUPER，用户模型目录中的 `.onnx`、`.pth`、`.th` 示例文件存在。未携带受信 sidecar/外置 Python 运行时的裸模型按设计显示诊断且禁止执行；因此未把这些文件记录为“已成功分离”。GPU Provider 和真实长音频吞吐仍取决于用户安装的 ONNX Runtime Provider、模型契约和驱动组合。
