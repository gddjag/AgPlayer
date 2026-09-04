# 2026-09-04 AgPlayer 17 项修复与发布验收

## 范围

本轮最终收尾以 `main@e5ec7c2` 为基线，完成播放器波形、共享列表、滚动时间轴、外置歌词窗口、自定义分离模型诊断、元数据安全写入与沉浸视觉回归验收。旧分支已在基线中，不重复合并。

## 需求与实现证据

| 范围 | 实现/既有能力 | 验收证据 |
| --- | --- | --- |
| 首次安装与默认值 | 新配置保持双窗口主题、转码 MP3；缩略图亮度默认 50%；频彩未播放区明暗度 0–100%，默认调暗 68%（保留 32% 亮度），旧亮度按 `1 - 旧值` 迁移 | `settings_controller_test`、`qml_main_window_test`、`qml_format_converter_test` |
| 双窗口/迷你波形 | 两处复用 `FullTrackWaveformView` 的同一内容矩形、播放裁剪、物理像素边界和时间映射，最后一个有效峰值落在容器内；深浅主题直接绑定同一未播放明暗度；悬停时间胶囊默认开启 | `waveform_item_test`、`qml_main_window_test`、`qml_mini_player_test`、深浅/DPI 截图矩阵 |
| 公共播放器界面 | 信息块和歌词多行居中；外置歌词窗口支持上、下、左、右磁吸并跟随播放器；主题菜单、颜色选择器及右键菜单收紧 | `qml_player_controls_layout_test`、`qml_integrated_theme_test`、`qml_mini_player_test`、`qml_main_window_test` |
| 单窗口/滚动共享列表 | 尾部列固定宽度并以 8px 分隔；波形两侧保持等效 8px 间距；标题与波形弹性增长；筛选控件为 28px 内容高度并贴近底边 | `qml_main_window_test`、`qml_rolling_theme_test`、`library_list_layout_contract_test` |
| 滚动时间轴 | 总览进度、悬停和点击统一走 `WaveformItem` 时间/像素映射；中心时间去除胶囊底色；既有固定 8 拍视口和中心针逻辑保留 | `waveform_coordinate_mapper_test`、`qml_rolling_theme_test` |
| 音频编辑 | 拖出片段使用“原文件名_片段_起点-终点”；既有缩放条、左/右裁剪和 Theme Token 路径回归通过 | `selection_drag_controller_test`、`audio_editor_controller_test`、`qml_audio_editor_test` |
| 人声伴奏分离 | 页面打开即探测设备并复核模型；任意层级递归发现 ONNX/PTH/TH；本地诊断项不再显示成下载失败，支持安全删除；自动模式在 DirectML 完整模型会话失败时清理临时输出并用 CPU 重试 | `vocal_separation_controller_test`、`separation_*` 测试组、真实 HTDemucs 自动回退测试、`qml_vocal_separation_test` |
| 元数据 | 强制正规化仍保留音频包载荷、编解码参数和并发修改验证；仅允许源或暂存侧缺失的采样格式/位深元数据视作正规化差异，真实载荷变化仍回滚并保留 `.agbak` | `metadata_writer_test`、`qml_metadata_editor_test`、`audio_tools_end_to_end_test` |
| 沉浸视觉 | 沿用现有 QRhi 场景图渲染器、隐藏/最小化停止渲染、资源生命周期与自适应质量；空间歌词两行层级与普通歌词三行显示分别回归 | `terrain_reactor_gpu_smoke_test`、`qml_immersive_integration_test` |

## 自动化验证

- MSVC Release 编译通过。
- `all_qmllint` 通过。
- Release CTest 在最终修改后串行复跑：163/163 通过。
- Windows 生命周期、任务栏、单实例、运行时部署、安装器与版本契约均通过。
- UI 截图矩阵：中文深色/浅色下的播放器、迷你、设置、共享列表和音频工具在 100%、125%、150% DPI 共 26 张，全部通过尺寸、非空、主题差异与无运行时警告检查。
- 主窗口真实启动/播放与 8 种格式导入、持久化、恢复冒烟测试通过，运行日志无 WARN/ERROR/FATAL。

## 硬件与外置模型边界

验收主机的 ONNX Runtime 探测到 DirectML Provider，用户模型目录中的 `.onnx`、`.pth`、`.th` 示例文件存在。HQ3 在 CPU 和 DirectML 上均完成真实短音频分离；HTDemucs 的 DirectML 大模型会话被当前驱动拒绝后，自动模式已验证能回退 CPU 并完成。未携带受信 sidecar/外置 Python 运行时的裸 `.pth/.th` 模型仍按设计显示诊断且禁止执行。
