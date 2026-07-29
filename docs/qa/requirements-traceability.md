# AgPlayer Windows MVP 需求追踪

日期：2026-07-29
范围：Qt 6/QML + C++17 Windows MVP；不包含安装包。

| 需求域 | 当前实现证据 | 自动化证据 | 结论 |
|---|---|---|---|
| 本地播放、暂停、Seek、音量、静音 | `core/src/audio_engine.cpp`、`core/src/decoder.cpp`、`qt/src/playback_controller.cpp` | `audio_engine_test`、`decoder_test`、`playback_controller_test`、生产播放冒烟 | 已验证 |
| MP3/WAV/FLAC/AAC/M4A/OGG/Opus/WMA | FFmpeg 解码与 `ImportController` 元数据探测 | `format_matrix_test`、`import_controller_test`、8 格式库冒烟 | 已验证 |
| 顺序、随机、单曲循环、列表循环、无间隙队列 | `PlaybackSession`、`AudioEngine` 预解码队列 | `playback_session_test`、`queue_gapless_test` | 已验证 |
| 空白启动、导入后播放、恢复上次位置 | `Main.qml`、`app/main.cpp`、`PlaybackController::loadRow` | `qml_main_window_test`、`playback_controller_test`、UI 矩阵 | 已验证 |
| 波形 GPU 渲染、三种模式、进度、Hover、拖拽 Seek、密度与粗细 | `WaveformItem`、`WaveformProvider`、`PlayerPane.qml` | `waveform_item_test`、`waveform_provider_test`、`qml_waveform_test`、UI 矩阵 | 已验证 |
| 导入、收藏、评分、排序、历史、歌单 | `LibraryModel`、`PlaylistModel`、`LibraryStore` | 模型、存储、歌单与导入测试 | 已验证 |
| 关键词、评分、BPM 双端筛选与 200 ms 防抖 | `LibraryFilterModel`、`SearchFilter.qml` | `library_filter_model_test`、10,000 行模型压力测试 | 已验证 |
| 主窗口、独立列表、迷你播放器、置顶、15 px 四向磁吸、联动与解绑 | `WindowController`、三个 QML 窗口 | `window_controller_test`、`qml_mini_player_test`、UI 矩阵 | 已验证 |
| 格式转换 | `FormatConverter` + FFmpeg 转码；元数据/视频提取选项独立，覆盖策略按任务冻结 | `transcoder_test`、`audio_tools_end_to_end_test`、QML 选项回归 | 已验证 |
| 六轨轻度剪辑、裁剪、分割、合并、复制粘贴、撤销重做、淡入淡出、静音、BPM 对齐、吸附、缩放与导出 | `LightEditor`、`multitrack_editor`、`LightEditPage.qml` | `multitrack_editor_test`、`light_editor_controller_test`、`qml_light_editor_test`、端到端测试 | 已验证 |
| 调整速度/BPM、保持音高、预览与导出 | `SpeedAdjuster`、`BpmAnalyzer`；设置变化实时同步保持音高 | `bpm_analyzer_test`、`audio_tools_end_to_end_test`、`qml_light_editor_test` | 已验证 |
| 升降调、Cent、人声保护、保持时长、预览与导出 | `PitchShifter` | `pitch_shifter_test`、`audio_tools_end_to_end_test` | 已验证 |
| 标签、封面、文件名、批量应用 | `MetadataEditor`、`metadata_writer` | `metadata_writer_test`、`audio_tools_end_to_end_test` | 已验证 |
| 深色、浅色、跟随系统与双色图标 | `Theme.qml`、`SettingsController` | QML 测试；四语 × 深浅主题 UI 矩阵 | 已验证；跟随系统由 Qt 系统配色驱动 |
| 中文、英文、泰语、越南语 | 四个 TS/QM 目录、`TranslationManager` | `translation_catalog_test`、`translation_manager_test`、UI 矩阵 | 已验证 |
| 设置保存、取消、恢复默认、运行时生效 | `SettingsController` 与 QML 绑定；旧版播放模式枚举自动迁移 | `settings_controller_test`、QML 测试 | 已验证 |
| 文件关联与图标 | `FileAssociationController` | `file_association_controller_test` | Windows 已验证 |
| 缓存分项清理、容量限制、退出清理 | `CacheJanitor`、`SettingsController`、关机链路 | `settings_controller_test`、`shutdown_test` | 已验证 |
| 纯本地、无上传、无广告、无统计、无凭据 | 未链接 Qt Network；媒体、歌单、设置均为本地路径 | 源码隐私扫描 | 已验证；“访问官网”仅在用户点击后交给系统浏览器 |
| C++ 核心与 Qt 解耦 | `core/` 无 Qt 依赖；`c_api.h` 为稳定 C 边界 | C API 生命周期、恢复与核心测试 | 已验证 |
| Seek < 20 ms、10,000+ 曲库、内存 < 60 MB | Release 基准与压力工具 | Seek P95 0.0222 ms；10,000 行与 10,000 波形压力通过；峰值 16.04 MiB | 已验证 |

## 有意不提供的伪设置

- 无间隙播放和采样率匹配由引擎始终启用，不提供无效开关。
- 独占音频模式与歌曲切换交叉淡化尚未进入当前跨设备稳定实现，因此不显示不可兑现的控件。
- 当前仅列出真实可用的系统默认输出设备；设备枚举与独占模式需后续按平台分别实现。

## 发布前人工门禁

- 真实声卡/扬声器的可听播放、Seek、切歌、音量与静音验收。
- Windows 多声卡、蓝牙断连/重连和设备热插拔验收。
- macOS、Linux、HarmonyOS 的构建、音频后端、文件关联与分发验证。
- EXE/安装器、DMG、AppImage 打包；必须收到单独指令后执行。
